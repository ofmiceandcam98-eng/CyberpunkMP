#include "stdafx.h"

#include "VoiceAudioManager.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
/**
 * COM, per thread, without fighting whoever initialised it first.
 *
 * The game has already called CoInitializeEx on its own threads with its own apartment
 * model, and RPC_E_CHANGED_MODE means exactly that - somebody got here first and the
 * existing model is fine to use. Treating it as failure would refuse to enumerate devices
 * on the very thread the UI calls from.
 *
 * Only uninitialises when this scope is the one that initialised.
 */
class ComScope
{
public:
    ComScope()
    {
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_owned = SUCCEEDED(hr);
        m_ok = m_owned || hr == RPC_E_CHANGED_MODE;
    }

    ~ComScope()
    {
        if (m_owned)
            CoUninitialize();
    }

    bool Ok() const { return m_ok; }

private:
    bool m_owned{false};
    bool m_ok{false};
};

std::string WideToUtf8(const wchar_t* apWide)
{
    if (!apWide)
        return {};

    const int needed = WideCharToMultiByte(CP_UTF8, 0, apWide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1)
        return {};

    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, apWide, -1, out.data(), needed, nullptr, nullptr);

    return out;
}

/**
 * Does this endpoint's name suggest it is the computer's own output fed back in?
 *
 * Windows reports Stereo Mix and its relatives as ordinary capture endpoints, so there is
 * no flag to read - the name is the only signal available. Matched loosely and lower-cased
 * because vendors label them differently.
 *
 * A HINT, not a verdict. Nothing is excluded on the strength of it; the UI warns and the
 * player decides, because on some rigs a loopback is deliberately what they want.
 */
bool NameLooksLikeLoopback(const std::string& acName)
{
    std::string lower = acName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    static const char* kHints[] = {
        "stereo mix", "wave out", "what u hear", "what you hear", "loopback", "mixed output"
    };

    for (const auto* pHint : kHints)
    {
        if (lower.find(pHint) != std::string::npos)
            return true;
    }

    return false;
}

std::vector<VoiceDevice> Enumerate(EDataFlow aFlow)
{
    std::vector<VoiceDevice> devices;

    ComScope com;
    if (!com.Ok())
        return devices;

    IMMDeviceEnumerator* pEnumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnumerator))))
    {
        return devices;
    }

    // Which one Windows currently considers default, so the UI can mark it. eCommunications
    // rather than eConsole for microphones: Windows tracks a separate default for voice
    // chat, and that is the one a player means by "my headset mic".
    std::string defaultId;
    {
        IMMDevice* pDefault = nullptr;
        const auto role = (aFlow == eCapture) ? eCommunications : eConsole;

        if (SUCCEEDED(pEnumerator->GetDefaultAudioEndpoint(aFlow, role, &pDefault)) && pDefault)
        {
            LPWSTR pId = nullptr;
            if (SUCCEEDED(pDefault->GetId(&pId)))
            {
                defaultId = WideToUtf8(pId);
                CoTaskMemFree(pId);
            }
            pDefault->Release();
        }
    }

    IMMDeviceCollection* pCollection = nullptr;

    // ACTIVE only. Disabled and unplugged endpoints are still enumerable, and listing a
    // microphone that cannot be opened is worse than not listing it - somebody picks it,
    // it fails, and nothing explains why.
    if (SUCCEEDED(pEnumerator->EnumAudioEndpoints(aFlow, DEVICE_STATE_ACTIVE, &pCollection)) && pCollection)
    {
        UINT count = 0;
        pCollection->GetCount(&count);

        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* pDevice = nullptr;
            if (FAILED(pCollection->Item(i, &pDevice)) || !pDevice)
                continue;

            VoiceDevice entry;

            LPWSTR pId = nullptr;
            if (SUCCEEDED(pDevice->GetId(&pId)))
            {
                entry.Id = WideToUtf8(pId);
                CoTaskMemFree(pId);
            }

            IPropertyStore* pProps = nullptr;
            if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps)) && pProps)
            {
                PROPVARIANT name;
                PropVariantInit(&name);

                if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR)
                    entry.Name = WideToUtf8(name.pwszVal);

                PropVariantClear(&name);
                pProps->Release();
            }

            // The format the endpoint actually runs at, for the UI to report. Read from the
            // mix format rather than assumed: interfaces run at 44.1, 48 and 96 kHz, and
            // telling somebody the wrong one is worse than telling them nothing.
            IAudioClient* pClient = nullptr;
            if (SUCCEEDED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                            reinterpret_cast<void**>(&pClient))) && pClient)
            {
                WAVEFORMATEX* pFormat = nullptr;
                if (SUCCEEDED(pClient->GetMixFormat(&pFormat)) && pFormat)
                {
                    entry.Channels = pFormat->nChannels;
                    entry.SampleRate = pFormat->nSamplesPerSec;
                    CoTaskMemFree(pFormat);
                }
                pClient->Release();
            }

            if (!entry.Id.empty())
            {
                entry.IsDefault = (entry.Id == defaultId);
                entry.LooksLikeLoopback = (aFlow == eCapture) && NameLooksLikeLoopback(entry.Name);

                devices.push_back(std::move(entry));
            }

            pDevice->Release();
        }

        pCollection->Release();
    }

    pEnumerator->Release();

    return devices;
}
} // namespace

VoiceAudioManager::~VoiceAudioManager()
{
    StopCapture();
}

std::vector<VoiceDevice> VoiceAudioManager::EnumerateInputDevices() const
{
    return Enumerate(eCapture);
}

std::vector<VoiceDevice> VoiceAudioManager::EnumerateOutputDevices() const
{
    return Enumerate(eRender);
}

void VoiceAudioManager::SetError(const std::string& acError)
{
    std::lock_guard lock(m_errorLock);
    m_lastError = acError;
}

std::string VoiceAudioManager::GetLastError() const
{
    std::lock_guard lock(m_errorLock);
    return m_lastError;
}

float VoiceAudioManager::ReadInputPeak()
{
    // Exchange rather than load: the meter wants the peak of the interval it is drawing,
    // and a value that is never cleared only ever climbs and then sits at the loudest
    // thing that has ever happened.
    const uint32_t bits = m_peakBits.exchange(0, std::memory_order_relaxed);

    float value = 0.f;
    std::memcpy(&value, &bits, sizeof(value));

    return value;
}

bool VoiceAudioManager::StartCapture(const std::string& acDeviceId)
{
    StopCapture();

    SetError({});
    m_stopRequested.store(false, std::memory_order_relaxed);

    try
    {
        m_thread = std::thread(&VoiceAudioManager::CaptureThread, this, acDeviceId);
    }
    catch (const std::exception& e)
    {
        SetError(std::string("could not start the capture thread: ") + e.what());
        return false;
    }

    return true;
}

void VoiceAudioManager::StopCapture()
{
    m_stopRequested.store(true, std::memory_order_relaxed);

    if (m_thread.joinable())
        m_thread.join();

    m_capturing.store(false, std::memory_order_relaxed);
    m_peakBits.store(0, std::memory_order_relaxed);
}

/**
 * Read the microphone until asked to stop.
 *
 * Its own thread, because every step here can block: opening an endpoint negotiates with a
 * driver, and a driver that stalls would take the frame loop with it if this ran inline.
 *
 * Polling rather than event-driven. An event-driven client is lower latency and will be
 * wanted once audio is actually being sent; for a level meter it buys nothing and costs a
 * wait handle whose timeout is one more thing that can hang.
 */
void VoiceAudioManager::CaptureThread(std::string aDeviceId)
{
    ComScope com;
    if (!com.Ok())
    {
        SetError("COM could not be initialised on the capture thread");
        return;
    }

    IMMDeviceEnumerator* pEnumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnumerator))))
    {
        SetError("the Windows audio enumerator is unavailable");
        return;
    }

    IMMDevice* pDevice = nullptr;

    if (aDeviceId.empty())
    {
        // No explicit choice means follow Windows. eCommunications is the default Windows
        // itself uses for voice chat, which is what somebody means by "my headset mic".
        pEnumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &pDevice);
    }
    else
    {
        const int wide = MultiByteToWideChar(CP_UTF8, 0, aDeviceId.c_str(), -1, nullptr, 0);
        std::wstring id(static_cast<size_t>(wide > 0 ? wide - 1 : 0), L'\0');

        if (wide > 0)
            MultiByteToWideChar(CP_UTF8, 0, aDeviceId.c_str(), -1, id.data(), wide);

        // A saved device that is not plugged in right now. Says so rather than silently
        // moving somebody to a different microphone - being quietly switched to the laptop
        // lid mic is worse than being told the interface is not connected.
        if (FAILED(pEnumerator->GetDevice(id.c_str(), &pDevice)))
            pDevice = nullptr;
    }

    if (!pDevice)
    {
        SetError("that microphone is not available");
        pEnumerator->Release();
        return;
    }

    IAudioClient* pClient = nullptr;
    if (FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                 reinterpret_cast<void**>(&pClient))) || !pClient)
    {
        SetError("that microphone could not be opened");
        pDevice->Release();
        pEnumerator->Release();
        return;
    }

    WAVEFORMATEX* pFormat = nullptr;
    if (FAILED(pClient->GetMixFormat(&pFormat)) || !pFormat)
    {
        SetError("that microphone did not report a format");
        pClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        return;
    }

    // The endpoint's OWN format, whatever it is. Shared mode, so the interface is not
    // forced to change its system-wide rate just because a game turned up - an interface
    // running a session at 96 kHz keeps running at 96 kHz. Conversion to the voice format
    // belongs later in the pipeline, not here.
    //
    // 100ms buffer: the meter does not need less, and a smaller one only means waking more
    // often to find nothing.
    constexpr REFERENCE_TIME kBuffer = 1000000;   // 100ms in 100ns units

    if (FAILED(pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, kBuffer, 0, pFormat, nullptr)))
    {
        SetError("that microphone refused the format Windows offered for it");
        CoTaskMemFree(pFormat);
        pClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        return;
    }

    IAudioCaptureClient* pCapture = nullptr;
    if (FAILED(pClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&pCapture))) || !pCapture)
    {
        SetError("that microphone could not be read");
        CoTaskMemFree(pFormat);
        pClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        return;
    }

    const uint16_t channels = pFormat->nChannels;
    const uint16_t bits = pFormat->wBitsPerSample;

    // Read here, not in the loop: pFormat is freed below, and the voice path needs the rate
    // with every buffer to know what it is converting from.
    const uint32_t sampleRate = pFormat->nSamplesPerSec;

    // Shared mode normally hands back 32-bit float; 16-bit integer still turns up on some
    // drivers. Anything else is read as silence rather than as noise - misreading the bytes
    // produces a meter that pins at full and a microphone that sounds like a fax machine.
    bool isFloat = (pFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);

    if (pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const auto* pExt = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pFormat);
        isFloat = (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    CoTaskMemFree(pFormat);
    pFormat = nullptr;

    if (FAILED(pClient->Start()))
    {
        SetError("that microphone would not start");
        pCapture->Release();
        pClient->Release();
        pDevice->Release();
        pEnumerator->Release();
        return;
    }

    m_capturing.store(true, std::memory_order_relaxed);
    spdlog::info("[Voice] capturing - {} channel(s), {}Hz, {}-bit {}", channels, sampleRate, bits,
                 isFloat ? "float" : "int");

    while (!m_stopRequested.load(std::memory_order_relaxed))
    {
        UINT32 packet = 0;

        if (FAILED(pCapture->GetNextPacketSize(&packet)))
        {
            SetError("the microphone stopped responding");
            break;
        }

        if (packet == 0)
        {
            // Nothing ready. Sleeping beats spinning: this thread has no deadline, and a
            // busy loop on a capture thread is a core burned for a level meter.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        BYTE* pData = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;

        if (FAILED(pCapture->GetBuffer(&pData, &frames, &flags, nullptr, nullptr)))
        {
            SetError("the microphone stopped responding");
            break;
        }

        float peak = 0.f;
        size_t converted = 0;

        // AUDCLNT_BUFFERFLAGS_SILENT means the buffer is meaningless, not that it is quiet -
        // reading it produces whatever happened to be in memory.
        if (frames > 0 && pData && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))
        {
            const uint32_t samples = frames * channels;

            // Converted to float once, here, and used for BOTH the meter and the voice
            // path. Previously the samples were read only to measure them and then thrown
            // away, which is why voice had nothing to encode.
            //
            // resize() on a vector that is already large enough does not allocate, so this
            // settles after the first buffer and never allocates on the audio thread again.
            if (m_convertBuffer.size() < samples)
                m_convertBuffer.resize(samples);

            if (isFloat && bits == 32)
            {
                const auto* pSamples = reinterpret_cast<const float*>(pData);
                for (uint32_t i = 0; i < samples; ++i)
                {
                    m_convertBuffer[i] = pSamples[i];
                    peak = std::max(peak, std::fabs(pSamples[i]));
                }
                converted = samples;
            }
            else if (!isFloat && bits == 16)
            {
                const auto* pSamples = reinterpret_cast<const int16_t*>(pData);
                for (uint32_t i = 0; i < samples; ++i)
                {
                    const float value = static_cast<float>(pSamples[i]) / 32768.f;
                    m_convertBuffer[i] = value;
                    peak = std::max(peak, std::fabs(value));
                }
                converted = samples;
            }
        }

        pCapture->ReleaseBuffer(frames);

        // After ReleaseBuffer, deliberately: the samples are already copied, and holding a
        // WASAPI capture buffer across a callback of unknown cost is how an endpoint starts
        // reporting overruns.
        if (converted > 0 && m_callback)
            m_callback(m_convertBuffer.data(), converted / channels, channels, sampleRate);

        if (peak > 0.f)
        {
            // Keep the loudest peak until somebody reads it. A meter polled at frame rate
            // would otherwise miss the front of a word entirely.
            uint32_t desired = 0;
            std::memcpy(&desired, &peak, sizeof(desired));

            uint32_t current = m_peakBits.load(std::memory_order_relaxed);

            for (;;)
            {
                float currentPeak = 0.f;
                std::memcpy(&currentPeak, &current, sizeof(currentPeak));

                if (currentPeak >= peak)
                    break;

                if (m_peakBits.compare_exchange_weak(current, desired, std::memory_order_relaxed))
                    break;
            }
        }
    }

    pClient->Stop();

    pCapture->Release();
    pClient->Release();
    pDevice->Release();
    pEnumerator->Release();

    m_capturing.store(false, std::memory_order_relaxed);
    spdlog::info("[Voice] capture stopped");
}
