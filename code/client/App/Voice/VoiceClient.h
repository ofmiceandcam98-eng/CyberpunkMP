#pragma once

/**
 * Voice: microphone to somebody else's speakers.
 *
 * WHAT OWNS WHICH THREAD, because everything here is a threading decision
 *
 *   capture thread  (VoiceAudioManager) - converts, resamples, encodes, and QUEUES.
 *   game thread     (NetworkWorldSystem::Update) - drains that queue and sends.
 *   network thread  (packet handler) - QUEUES received frames. Decodes nothing.
 *   render thread   (this class) - decodes, mixes, and writes to the speakers.
 *
 * The two queues exist so that neither the audio path nor the network path ever calls into
 * the other. Encoding on the capture thread is fine - it is arithmetic on samples we already
 * hold - but SENDING from it is not: the transport is not documented as safe to use from an
 * arbitrary thread, and a voice feature that corrupts the connection would take the whole
 * session down rather than just sounding bad.
 *
 * Decoders are touched only by the render thread for the same reason. An Opus decoder holds
 * per-stream state, so two threads reaching into one is a data race that presents as
 * intermittent noise, which is close to undebuggable.
 *
 * FAIL SOFT, ALWAYS
 *
 * Every failure here is silence, never a crash and never a stall. A microphone that will not
 * open, a codec that refuses a frame, a speaker device that vanishes mid-session: all of them
 * stop voice and leave the game running. Nobody should lose a session because they unplugged
 * a headset.
 */

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "VoiceAudioManager.h"

// Opaque in opus.h too - forward declared so this header does not drag the codec into every
// translation unit that only wants to ask whether somebody is talking.
struct OpusEncoder;
struct OpusDecoder;

class VoiceClient
{
public:
    // Opus works at 8/12/16/24/48kHz. 48k is its native rate and what every endpoint on a
    // modern machine already runs at, so choosing it usually means no resampling at all.
    static constexpr uint32_t kSampleRate = 48000;

    // 20ms. The codec's default and the standard trade: short enough that a lost frame is
    // heard as a blip rather than a gap, long enough that per-packet overhead is not most of
    // the bandwidth.
    static constexpr uint32_t kFrameSamples = 960;

    // Speech, not music. 24kbps is transparent for a voice and a fortieth of raw PCM.
    static constexpr int32_t kBitrate = 24000;

    /**
     * How much audio from BEFORE the key went down is sent anyway. 120ms - six frames.
     *
     * People start a word slightly before the key is fully down; that is how everybody
     * uses push-to-talk, and discarding everything until the press means the first
     * syllable is clipped off. Keeping a short rolling window and sending it when
     * transmission starts is what makes the first word arrive intact.
     *
     * THE COST, because it is a real one: a transmission that begins 120ms in the past
     * stays 120ms behind for its whole duration - the queue drains in real time, so the
     * head start never closes. That is the trade. 120ms is short enough to sit inside
     * normal conversational latency and long enough to catch a leading consonant.
     *
     * The window is only ever held in memory. Nothing is transmitted unless somebody
     * presses the key.
     */
    static constexpr size_t kPreRollSamples = (kSampleRate / 1000) * 120;

    VoiceClient() = default;
    ~VoiceClient();

    VoiceClient(const VoiceClient&) = delete;
    VoiceClient& operator=(const VoiceClient&) = delete;

    /**
     * Open the microphone and the speakers and start running.
     *
     * Empty ids mean "whatever Windows currently calls the default", which is what somebody
     * means when they have not chosen deliberately.
     *
     * Returns false and leaves everything stopped on failure - never throws, never partially
     * starts. Capture failing is survivable on its own (you can still hear people), so that
     * is reported but not treated as fatal.
     */
    bool Start(const std::string& acInputDevice, const std::string& acOutputDevice);
    void Stop();

    bool IsRunning() const { return m_running.load(std::memory_order_relaxed); }

    // Push-to-talk. Frames are only produced while this is true; silence is the absence of
    // frames rather than a message saying "I stopped", so a dropped packet can never leave
    // a microphone open.
    void SetTransmitting(bool aOn) { m_transmitting.store(aOn, std::memory_order_relaxed); }
    bool IsTransmitting() const { return m_transmitting.load(std::memory_order_relaxed); }

    // 0 whisper, 1 local, 2 yell. Sent with every frame; the SERVER decides what each means
    // in metres.
    void SetRange(uint32_t aRange) { m_range.store(aRange > 2 ? 1u : aRange, std::memory_order_relaxed); }
    uint32_t GetRange() const { return m_range.load(std::memory_order_relaxed); }

    // Percentages where 100 is unchanged. Above 100 is allowed deliberately - a quiet
    // microphone on a professional interface is normal.
    void SetMicVolume(uint32_t aPercent) { m_micVolume.store(aPercent > 200 ? 200u : aPercent, std::memory_order_relaxed); }
    void SetPlaybackVolume(uint32_t aPercent) { m_playbackVolume.store(aPercent > 200 ? 200u : aPercent, std::memory_order_relaxed); }

    /**
     * A frame arrived from the server. Called on the NETWORK thread.
     *
     * Queues and returns. No decoding here - see the note at the top about decoder state.
     */
    // Pointer and size rather than a container type, so this stays independent of however
    // the protocol happens to spell "bytes" - the generator produces a vector with its own
    // allocator, and a voice codec should not have to know that.
    void OnFrameReceived(uint64_t aSpeakerId, const uint8_t* apData, size_t aSize, uint32_t aSequence);

    /**
     * Hand over everything encoded since the last call. Called on the GAME thread, which is
     * the only place it is safe to touch the network.
     *
     * Moves rather than copies - a voice frame is small, but this runs 50 times a second.
     */
    std::vector<std::string> TakeOutgoing();

    // Speaker ids heard from within the last moment, for the HUD. Cheap to call every frame.
    std::vector<uint64_t> GetActiveSpeakers() const;

    // Loudest thing the microphone heard since the last read, 0..1 - for an in-game meter.
    float ReadInputPeak() { return m_audio.ReadInputPeak(); }

    std::string GetLastError() const;

private:
    // Capture thread. Downmixes, resamples, accumulates whole frames, encodes, queues.
    void OnCaptured(const float* apSamples, size_t aFrames, uint32_t aChannels, uint32_t aSampleRate);

    // Render thread. Drains received frames, decodes them per speaker, mixes, writes.
    void RenderThread(std::string aDeviceId);

    void SetError(const std::string& acError);

    // One speaker's audio, waiting to be played.
    struct Speaker
    {
        OpusDecoder* pDecoder{nullptr};

        // Decoded samples not yet handed to the sound card.
        std::deque<float> Pending;

        // Last sequence seen, to drop frames that arrive out of order. Voice frames that
        // arrive late are worse than ones that never arrive.
        uint32_t LastSequence{0};
        bool HasSequence{false};

        // When we last had audio from them, for the HUD's "who is talking".
        uint64_t LastHeardMs{0};
    };

    // An encoded frame waiting to be decoded, exactly as it came off the wire.
    struct IncomingFrame
    {
        uint64_t SpeakerId{0};
        uint32_t Sequence{0};
        std::string Data;
    };

    VoiceAudioManager m_audio;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_transmitting{false};
    std::atomic<uint32_t> m_range{1};
    std::atomic<uint32_t> m_micVolume{100};
    std::atomic<uint32_t> m_playbackVolume{100};

    // Capture thread only, after Start.
    OpusEncoder* m_pEncoder{nullptr};
    std::vector<float> m_captureAccum;
    std::vector<float> m_resampleScratch;

    // Carried between callbacks so resampling does not click at every buffer boundary.
    double m_resamplePhase{0.0};
    float m_resampleLast{0.f};

    std::mutex m_outgoingLock;
    std::vector<std::string> m_outgoing;
    uint32_t m_sequence{0};

    std::mutex m_incomingLock;
    std::deque<IncomingFrame> m_incoming;

    // Render thread only.
    std::thread m_renderThread;
    std::map<uint64_t, Speaker> m_speakers;

    mutable std::mutex m_speakerStateLock;
    std::vector<uint64_t> m_activeSpeakers;

    mutable std::mutex m_errorLock;
    std::string m_lastError;
};
