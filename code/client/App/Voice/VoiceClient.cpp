#include "stdafx.h"

#include "VoiceClient.h"

#include <mmdeviceapi.h>
#include <audioclient.h>

// <opus/opus.h>, not <opus.h> - the package installs its headers into an opus/ directory
// and only the parent is on the include path.
#include <opus/opus.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace
{
/**
 * COM, per thread, without fighting whoever initialised it first.
 *
 * Same reasoning as the copy in VoiceAudioManager.cpp: RPC_E_CHANGED_MODE means somebody got
 * here first with a different apartment model, which is fine to use. Duplicated rather than
 * shared because that one lives in an anonymous namespace, and exporting it to share eight
 * lines would put a Windows header into a public one.
 */
class ComScope
{
public:
    ComScope()
    {
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_owned = SUCCEEDED(hr);
    }

    ~ComScope()
    {
        if (m_owned)
            CoUninitialize();
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool m_owned{false};
};

uint64_t NowMs()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// How long after their last frame somebody stops counting as "talking" for the HUD. Slightly
// longer than one frame so the indicator does not flicker between syllables.
constexpr uint64_t kSpeakingHoldMs = 250;

// Ceiling on undelivered audio per speaker. Reached only when the sound card is not draining
// - a stalled endpoint, or a machine that cannot keep up - and the right answer then is to
// drop the oldest audio rather than grow without limit and play a conversation from a minute
// ago. Two seconds at 48kHz.
constexpr size_t kMaxPendingSamples = VoiceClient::kSampleRate * 2;
} // namespace

VoiceClient::~VoiceClient()
{
    Stop();
}

bool VoiceClient::Start(const std::string& acInputDevice, const std::string& acOutputDevice)
{
    if (m_running.load(std::memory_order_relaxed))
        Stop();

    m_stopRequested.store(false, std::memory_order_relaxed);

    int error = OPUS_OK;

    // VOIP rather than AUDIO: it biases the codec towards intelligibility of speech over
    // fidelity of music, which is the correct trade for people talking to each other.
    m_pEncoder = opus_encoder_create(static_cast<opus_int32>(kSampleRate), 1, OPUS_APPLICATION_VOIP, &error);

    if (error != OPUS_OK || !m_pEncoder)
    {
        SetError("the voice encoder could not be created");
        m_pEncoder = nullptr;
        return false;
    }

    opus_encoder_ctl(m_pEncoder, OPUS_SET_BITRATE(kBitrate));

    // Tell the codec that packet loss is expected, so it encodes with a little redundancy.
    // Voice runs on the unreliable channel by design - a late frame is worse than a lost one.
    opus_encoder_ctl(m_pEncoder, OPUS_SET_PACKET_LOSS_PERC(10));

    m_captureAccum.clear();
    m_resamplePhase = 0.0;
    m_resampleLast = 0.f;
    m_sequence = 0;

    m_running.store(true, std::memory_order_relaxed);

    // Playback first. Being able to HEAR people is the half that works even when somebody's
    // microphone is broken, so it must not depend on capture starting.
    m_renderThread = std::thread(&VoiceClient::RenderThread, this, acOutputDevice);

    m_audio.SetCaptureCallback([this](const float* apSamples, size_t aFrames, uint32_t aChannels, uint32_t aSampleRate)
                               { OnCaptured(apSamples, aFrames, aChannels, aSampleRate); });

    /**
     * Not fatal. Somebody with no working microphone should still hear everyone else.
     *
     * NOTE WHAT THIS RETURN VALUE MEANS, because it used to be read as more than it is:
     * StartCapture answers "the capture thread was spawned", not "a microphone is open".
     * It cannot answer the second without waiting on a driver, and waiting on a driver here
     * would hang the connect path.
     *
     * So false means the thread could not even start - rare - and TRUE MEANS NOTHING YET.
     * The old code logged "[Voice] running" on true, which is how two sessions of logs came
     * to say "running" while the stats line underneath said "mic NOT CAPTURING" three
     * hundred times and no error was recorded anywhere.
     *
     * The real answer arrives asynchronously: the capture thread logs its own failure now
     * (see VoiceAudioManager::SetError), and the stats line below reports IsCapturing plus
     * the reason. This line no longer claims anything it cannot know.
     */
    if (!m_audio.StartCapture(acInputDevice))
        spdlog::warn("[Voice] no microphone - you can hear others, they cannot hear you: {}", m_audio.GetLastError());
    else
        spdlog::info("[Voice] started - opening {} microphone, result follows",
                     acInputDevice.empty() ? "the default" : "the saved");

    return true;
}

void VoiceClient::Stop()
{
    if (!m_running.load(std::memory_order_relaxed) && !m_renderThread.joinable())
        return;

    m_stopRequested.store(true, std::memory_order_relaxed);
    m_running.store(false, std::memory_order_relaxed);

    // Capture first: it calls into this object, so it must be stopped before anything it
    // touches is torn down.
    m_audio.StopCapture();
    m_audio.SetCaptureCallback(nullptr);

    if (m_renderThread.joinable())
        m_renderThread.join();

    if (m_pEncoder)
    {
        opus_encoder_destroy(m_pEncoder);
        m_pEncoder = nullptr;
    }

    // Decoders are render-thread state and the render thread has been joined, so this is the
    // one safe moment to free them.
    for (auto& [id, speaker] : m_speakers)
    {
        if (speaker.pDecoder)
            opus_decoder_destroy(speaker.pDecoder);
    }

    m_speakers.clear();

    {
        std::lock_guard lock(m_incomingLock);
        m_incoming.clear();
    }

    {
        std::lock_guard lock(m_outgoingLock);
        m_outgoing.clear();
    }

    {
        std::lock_guard lock(m_speakerStateLock);
        m_activeSpeakers.clear();
    }

    spdlog::info("[Voice] stopped");
}

void VoiceClient::OnCaptured(const float* apSamples, size_t aFrames, uint32_t aChannels, uint32_t aSampleRate)
{
    if (!apSamples || aFrames == 0 || aChannels == 0 || aSampleRate == 0 || !m_pEncoder)
        return;

    // Capture, convert and accumulate ALWAYS - even when not transmitting.
    //
    // This is what makes the first word survive. Somebody starts a word fractionally before
    // the key is fully down; if nothing is kept until the press, that leading syllable does
    // not exist to send. A short rolling window means it does. See kPreRollSamples.
    //
    // The resampler also has to keep running: resetting its phase on every key press would
    // put a discontinuity at the start of every transmission, heard as a click on the first
    // word - the very thing this is meant to protect.
    const bool transmitting = m_transmitting.load(std::memory_order_relaxed);

    // Downmix to mono. Voice is mono end to end: it halves the bandwidth, and the position of
    // a speaker is decided by where their puppet is, not by which side of their microphone
    // they stood on.
    m_resampleScratch.resize(aFrames);

    for (size_t frame = 0; frame < aFrames; ++frame)
    {
        float sum = 0.f;
        for (uint32_t channel = 0; channel < aChannels; ++channel)
            sum += apSamples[frame * aChannels + channel];

        m_resampleScratch[frame] = sum / static_cast<float>(aChannels);
    }

    // Linear resample to 48kHz.
    //
    // Linear, not windowed-sinc: the artefacts it introduces sit above the band speech
    // occupies, Opus discards most of them, and the cost is a multiply per sample on a thread
    // that must never be late. Most endpoints already run at 48k, where step is exactly 1.0
    // and this reduces to a copy.
    //
    // Phase and the last sample are carried between callbacks. Without them the interpolation
    // restarts at every buffer boundary, which is heard as a click every few milliseconds.
    const double step = static_cast<double>(aSampleRate) / static_cast<double>(kSampleRate);
    const auto* mono = m_resampleScratch.data();

    auto sampleAt = [&](long index) -> float
    {
        if (index < 0)
            return m_resampleLast;
        if (index >= static_cast<long>(aFrames))
            return mono[aFrames - 1];
        return mono[index];
    };

    for (double position = m_resamplePhase; position < static_cast<double>(aFrames); position += step)
    {
        const long index = static_cast<long>(std::floor(position));
        const double fraction = position - static_cast<double>(index);

        m_captureAccum.push_back(
            static_cast<float>((1.0 - fraction) * sampleAt(index) + fraction * sampleAt(index + 1)));

        m_resamplePhase = position + step;
    }

    m_resamplePhase -= static_cast<double>(aFrames);

    if (m_resamplePhase < 0.0)
        m_resamplePhase = 0.0;

    m_resampleLast = mono[aFrames - 1];

    // Not transmitting: keep only the pre-roll window and encode nothing.
    //
    // Trimmed from the FRONT, so what survives is always the most recent audio - the
    // fraction of a second immediately before a key press, which is the part worth having.
    // Nothing here is sent anywhere; it only becomes a frame if somebody presses talk.
    if (!transmitting)
    {
        if (m_captureAccum.size() > kPreRollSamples)
        {
            m_captureAccum.erase(m_captureAccum.begin(),
                                 m_captureAccum.begin() +
                                     static_cast<long>(m_captureAccum.size() - kPreRollSamples));
        }

        return;
    }

    // Encode every whole frame we now have - starting with the pre-roll, which is already
    // sitting at the front of the accumulator.
    const float gain = static_cast<float>(m_micVolume.load(std::memory_order_relaxed)) / 100.f;

    while (m_captureAccum.size() >= kFrameSamples)
    {
        float* pFrame = m_captureAccum.data();

        // Gain then clamp. Above 100% is deliberately allowed, so clamping is not optional -
        // an unclamped sample wraps and is heard as a crack rather than as loudness.
        for (uint32_t i = 0; i < kFrameSamples; ++i)
            pFrame[i] = std::clamp(pFrame[i] * gain, -1.f, 1.f);

        unsigned char encoded[4000];
        const auto written = opus_encode_float(m_pEncoder, pFrame, static_cast<int>(kFrameSamples), encoded,
                                               static_cast<opus_int32>(sizeof(encoded)));

        if (written > 0)
        {
            std::lock_guard lock(m_outgoingLock);

            // If the game thread has not drained this, it is not running - dropping the
            // oldest is right, because the newest audio is the audio worth sending.
            if (m_outgoing.size() > 50)
                m_outgoing.erase(m_outgoing.begin());

            m_outgoing.emplace_back(reinterpret_cast<const char*>(encoded), static_cast<size_t>(written));
            m_framesEncoded.fetch_add(1, std::memory_order_relaxed);
        }

        m_captureAccum.erase(m_captureAccum.begin(), m_captureAccum.begin() + kFrameSamples);
    }
}

std::vector<std::string> VoiceClient::TakeOutgoing()
{
    std::vector<std::string> frames;

    {
        std::lock_guard lock(m_outgoingLock);
        frames.swap(m_outgoing);
    }

    return frames;
}

void VoiceClient::OnFrameReceived(uint64_t aSpeakerId, const uint8_t* apData, size_t aSize, uint32_t aSequence)
{
    if (!apData || aSize == 0 || !m_running.load(std::memory_order_relaxed))
        return;

    std::lock_guard lock(m_incomingLock);

    // Bounded, for the same reason as the outgoing queue: if the render thread has stopped
    // draining, the queue must not grow until the process runs out of memory.
    if (m_incoming.size() > 200)
        m_incoming.pop_front();

    m_incoming.push_back(
        IncomingFrame{aSpeakerId, aSequence, std::string(reinterpret_cast<const char*>(apData), aSize)});

    m_framesReceived.fetch_add(1, std::memory_order_relaxed);
}

std::vector<uint64_t> VoiceClient::GetActiveSpeakers() const
{
    std::lock_guard lock(m_speakerStateLock);
    return m_activeSpeakers;
}

std::string VoiceClient::GetLastError() const
{
    std::lock_guard lock(m_errorLock);
    return m_lastError;
}

void VoiceClient::RenderThread(std::string aDeviceId)
{
    ComScope com;

    IMMDeviceEnumerator* pEnumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&pEnumerator))) ||
        !pEnumerator)
    {
        SetError("Windows audio could not be reached");
        return;
    }

    // Resolving the output device, in order of preference and NEVER giving up early.
    //
    // The first version asked for eRender/eCommunications and quit if that was null, which
    // killed playback outright for anybody without a communications device configured -
    // "[Voice] running" immediately followed by "those speakers are not available", the
    // render thread exiting, and nobody able to hear a thing. eCommunications is a SEPARATE
    // Windows default from the ordinary playback device and is very often simply unset.
    //
    // A saved device id that is no longer plugged in has the same shape: a specific choice
    // that cannot be honoured must fall back to something that works, not to silence.
    IMMDevice* pDevice = nullptr;

    // The same two Chromium sentinels the capture side handles - see the long note in
    // VoiceAudioManager::CaptureThread. "default" and "communications" are what the
    // launcher's web-based device picker calls the system defaults, not endpoint ids.
    //
    // This side already fell back when a lookup failed, which is the ONLY reason output
    // kept working while input died - the asymmetry, not the sentinel, is what made the
    // bug look like a microphone problem. Handled explicitly anyway, so the log stops
    // claiming a device "is not connected" when nobody ever chose one.
    if (aDeviceId == "default" || aDeviceId == "communications")
        aDeviceId.clear();

    if (!aDeviceId.empty())
    {
        const int wide = MultiByteToWideChar(CP_UTF8, 0, aDeviceId.c_str(), -1, nullptr, 0);
        std::wstring id(static_cast<size_t>(wide > 0 ? wide - 1 : 0), L'\0');

        if (wide > 0)
            MultiByteToWideChar(CP_UTF8, 0, aDeviceId.c_str(), -1, id.data(), wide);

        if (FAILED(pEnumerator->GetDevice(id.c_str(), &pDevice)))
            pDevice = nullptr;

        if (!pDevice)
            spdlog::warn("[Voice] the chosen output device is not connected - using the Windows default");
    }

    // eCommunications is what Windows routes voice chat to when it is set...
    if (!pDevice && FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eCommunications, &pDevice)))
        pDevice = nullptr;

    // ...and eConsole is the ordinary "default playback device", which every working system
    // has. This is the line whose absence made voice inaudible.
    if (!pDevice && FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)))
        pDevice = nullptr;

    if (!pDevice)
    {
        SetError("no working playback device - you will not hear anyone");
        pEnumerator->Release();
        return;
    }

    IAudioClient* pClient = nullptr;
    if (FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&pClient))) ||
        !pClient)
    {
        SetError("those speakers could not be opened");
        pDevice->Release();
        pEnumerator->Release();
        return;
    }

    WAVEFORMATEX* pFormat = nullptr;
    if (FAILED(pClient->GetMixFormat(&pFormat)) || !pFormat)
    {
        SetError("those speakers did not report a format");
        pClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        return;
    }

    // Shared mode at the endpoint's own format, exactly as capture does - so voice does not
    // yank the system sample rate around and does not fight the game's own audio for the
    // device. Whatever it wants, we convert to.
    constexpr REFERENCE_TIME kBuffer = 1000000; // 100ms in 100ns units

    if (FAILED(pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBuffer, 0, pFormat, nullptr)))
    {
        SetError("those speakers refused the format Windows offered for them");
        CoTaskMemFree(pFormat);
        pClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        return;
    }

    IAudioRenderClient* pRender = nullptr;
    if (FAILED(pClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&pRender))) || !pRender)
    {
        SetError("those speakers could not be written to");
        CoTaskMemFree(pFormat);
        pClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        return;
    }

    const uint16_t channels = pFormat->nChannels;
    const uint16_t bits = pFormat->wBitsPerSample;
    const uint32_t sampleRate = pFormat->nSamplesPerSec;

    bool isFloat = (pFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);

    if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const auto* pExt = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pFormat);
        isFloat = (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    CoTaskMemFree(pFormat);
    pFormat = nullptr;

    UINT32 bufferFrames = 0;
    pClient->GetBufferSize(&bufferFrames);

    if (FAILED(pClient->Start()))
    {
        SetError("those speakers would not start");
        pRender->Release();
        pClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        return;
    }

    spdlog::info("[Voice] playing - {} channel(s), {}Hz, {}-bit {}", channels, sampleRate, bits,
                 isFloat ? "float" : "int");

    // Position within a decoded 48kHz stream, for converting to the device's rate. Carried
    // across iterations for the same reason capture carries its phase.
    const double step = static_cast<double>(kSampleRate) / static_cast<double>(sampleRate);

    std::vector<float> mixed;

    m_playbackAlive.store(true, std::memory_order_relaxed);

    while (!m_stopRequested.load(std::memory_order_relaxed))
    {
        // Everything that arrived since last time, decoded into its speaker's queue.
        std::deque<IncomingFrame> incoming;

        {
            std::lock_guard lock(m_incomingLock);
            incoming.swap(m_incoming);
        }

        const auto now = NowMs();

        for (const auto& frame : incoming)
        {
            auto& speaker = m_speakers[frame.SpeakerId];

            if (!speaker.pDecoder)
            {
                int error = OPUS_OK;
                speaker.pDecoder = opus_decoder_create(static_cast<opus_int32>(kSampleRate), 1, &error);

                if (error != OPUS_OK || !speaker.pDecoder)
                {
                    speaker.pDecoder = nullptr;
                    continue;
                }
            }

            // Out of order. Unsigned wrap-around is why this compares a difference rather
            // than the values: sequence is allowed to wrap, and > would drop everything for
            // a full cycle after it did.
            if (speaker.HasSequence)
            {
                const int32_t age = static_cast<int32_t>(frame.Sequence - speaker.LastSequence);

                if (age <= 0)
                    continue;
            }

            float decoded[kFrameSamples];
            const auto samples =
                opus_decode_float(speaker.pDecoder, reinterpret_cast<const unsigned char*>(frame.Data.data()),
                                  static_cast<opus_int32>(frame.Data.size()), decoded,
                                  static_cast<int>(kFrameSamples), 0);

            if (samples <= 0)
                continue;

            speaker.LastSequence = frame.Sequence;
            speaker.HasSequence = true;
            speaker.LastHeardMs = now;

            speaker.Pending.insert(speaker.Pending.end(), decoded, decoded + samples);
            m_framesDecoded.fetch_add(1, std::memory_order_relaxed);

            // A queue this deep means the sound card is not draining. Keep the newest.
            while (speaker.Pending.size() > kMaxPendingSamples)
                speaker.Pending.pop_front();
        }

        // Who counts as talking, for the HUD.
        {
            std::vector<uint64_t> active;

            for (const auto& [id, speaker] : m_speakers)
            {
                if (!speaker.Pending.empty() || now - speaker.LastHeardMs < kSpeakingHoldMs)
                    active.push_back(id);
            }

            std::lock_guard lock(m_speakerStateLock);
            m_activeSpeakers.swap(active);
        }

        // How much room the device has.
        UINT32 padding = 0;
        if (FAILED(pClient->GetCurrentPadding(&padding)))
            break;

        const UINT32 available = bufferFrames > padding ? bufferFrames - padding : 0;

        if (available == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        BYTE* pData = nullptr;
        if (FAILED(pRender->GetBuffer(available, &pData)) || !pData)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // Mix every speaker into one mono stream at the device's rate.
        mixed.assign(available, 0.f);

        const float volume = static_cast<float>(m_playbackVolume.load(std::memory_order_relaxed)) / 100.f;

        for (auto& [id, speaker] : m_speakers)
        {
            if (speaker.Pending.empty())
                continue;

            double position = 0.0;

            for (UINT32 i = 0; i < available; ++i)
            {
                const size_t index = static_cast<size_t>(position);

                if (index >= speaker.Pending.size())
                    break;

                mixed[i] += speaker.Pending[index];
                position += step;
            }

            // Drop what was consumed. Summing rather than averaging, then clamping below -
            // averaging would make everybody quieter as more people joined, which is not how
            // a room works.
            const size_t consumed = std::min(static_cast<size_t>(position), speaker.Pending.size());
            speaker.Pending.erase(speaker.Pending.begin(), speaker.Pending.begin() + consumed);
        }

        // Write it out in whatever the device wanted, duplicated across its channels. Voice
        // is mono, so every channel gets the same sample - positioning belongs to the game's
        // own audio, not here.
        if (isFloat && bits == 32)
        {
            auto* pOut = reinterpret_cast<float*>(pData);

            for (UINT32 i = 0; i < available; ++i)
            {
                const float value = std::clamp(mixed[i] * volume, -1.f, 1.f);

                for (uint16_t channel = 0; channel < channels; ++channel)
                    pOut[i * channels + channel] = value;
            }
        }
        else if (!isFloat && bits == 16)
        {
            auto* pOut = reinterpret_cast<int16_t*>(pData);

            for (UINT32 i = 0; i < available; ++i)
            {
                const float value = std::clamp(mixed[i] * volume, -1.f, 1.f);
                const auto sample = static_cast<int16_t>(value * 32767.f);

                for (uint16_t channel = 0; channel < channels; ++channel)
                    pOut[i * channels + channel] = sample;
            }
        }
        else
        {
            // An format we cannot write is silence, not noise. Releasing with SILENT rather
            // than leaving the buffer at whatever was in memory.
            pRender->ReleaseBuffer(available, AUDCLNT_BUFFERFLAGS_SILENT);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        pRender->ReleaseBuffer(available, 0);

        // Roughly half a buffer. Long enough not to spin, short enough that the device never
        // runs dry between wakeups.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    m_playbackAlive.store(false, std::memory_order_relaxed);

    pClient->Stop();
    pRender->Release();
    pClient->Release();
    pDevice->Release();
    pEnumerator->Release();
}

void VoiceClient::SetError(const std::string& acError)
{
    {
        std::lock_guard lock(m_errorLock);
        m_lastError = acError;
    }

    spdlog::warn("[Voice] {}", acError);
}
