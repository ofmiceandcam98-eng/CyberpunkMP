#pragma once

/**
 * Microphones and speakers, through Windows' own audio endpoint system.
 *
 * WHY WASAPI AND NOT A LIST OF MANUFACTURERS
 *
 * A Scarlett Solo, an Apollo Twin, an RME Babyface, a £20 USB headset and the microphone
 * built into a laptop lid are all the same thing to Windows: an audio endpoint. Asking
 * Windows for its endpoints supports every one of them, plus every device nobody has
 * thought of yet, with no per-vendor code to write or maintain.
 *
 * There is deliberately no `if (device == "Scarlett")` anywhere in this file, and there
 * should never be one. The manufacturer's name is something to SHOW a player, never
 * something to branch on.
 *
 * CAPTURE ONLY - NEVER LOOPBACK
 *
 * WASAPI can also capture what the computer is PLAYING; that is loopback, and it is how a
 * voice system accidentally transmits the game, Discord and Spotify instead of a voice.
 * Nothing here opens a render endpoint for capture. Devices are enumerated from eCapture
 * for microphones and eRender for outputs, and the two are never crossed.
 *
 * Windows still exposes some loopback hardware (Stereo Mix, What U Hear) as genuine
 * capture endpoints, so those cannot be excluded by asking Windows what they are. They are
 * flagged by name for the UI to warn about - flagged rather than hidden, because on some
 * rigs a loopback is deliberately the point.
 *
 * NOT ON THE GAME THREAD
 *
 * Capture runs on its own thread. A microphone being unplugged, a driver stalling, or an
 * endpoint refusing a format must never freeze the game - so nothing here is called from
 * the frame loop except the reads, which touch one atomic.
 */

#include <atomic>
#include <functional>
#include <thread>
#include <mutex>
#include <string>
#include <vector>

struct VoiceDevice
{
    // The Windows endpoint ID, which is what identifies a device. NOT the display name:
    // two identical interfaces produce two identical names, names change with a driver
    // update, and a saved name matches the wrong device the day someone buys a second
    // headset.
    std::string Id;

    // What to show a player. Whatever Windows calls it - "Microphone (2- Focusrite USB
    // Audio)" - because that is the string they will recognise from their sound settings.
    std::string Name;

    bool IsDefault{false};

    // True when the name matches a known system-audio loopback. A guess, and labelled as
    // one in the UI, because Windows reports these as ordinary capture endpoints.
    bool LooksLikeLoopback{false};

    uint32_t Channels{0};
    uint32_t SampleRate{0};
};

class VoiceAudioManager
{
public:
    VoiceAudioManager() = default;
    ~VoiceAudioManager();

    VoiceAudioManager(const VoiceAudioManager&) = delete;
    VoiceAudioManager& operator=(const VoiceAudioManager&) = delete;

    // Microphones (eCapture) and speakers (eRender). Separate calls rather than one with a
    // flag, so a caller cannot ask for microphones and be handed render endpoints.
    std::vector<VoiceDevice> EnumerateInputDevices() const;
    std::vector<VoiceDevice> EnumerateOutputDevices() const;

    /**
     * Open a microphone and start reading it.
     *
     * An empty id means "whatever Windows currently calls the default", which is what
     * somebody wants when they have not chosen deliberately - it follows a headset being
     * plugged in without them touching anything.
     *
     * Returns false and changes nothing on failure. A microphone that will not open is a
     * message to show, never a reason to stop the game.
     */
    bool StartCapture(const std::string& acDeviceId);
    void StopCapture();

    /**
     * Receives captured audio, as interleaved float in the endpoint's OWN format.
     *
     * Format is passed with every call rather than fixed, because a shared-mode endpoint
     * decides its own: 44100 or 48000, mono or stereo, whatever the driver prefers. Forcing
     * a format is how a perfectly good interface ends up refusing to open. Converting is the
     * caller's job.
     *
     * CALLED ON THE CAPTURE THREAD, every few milliseconds. Nothing in here may touch the
     * game, allocate unpredictably, or block - a stall here is a stall in the audio path,
     * which is heard as a gap in somebody's voice.
     *
     * Set it BEFORE StartCapture. It is deliberately not guarded by a lock: taking one on
     * the audio thread to support a change nobody makes would cost more than it protects.
     */
    using CaptureCallback =
        std::function<void(const float* apSamples, size_t aFrames, uint32_t aChannels, uint32_t aSampleRate)>;

    void SetCaptureCallback(CaptureCallback aCallback) { m_callback = std::move(aCallback); }

    bool IsCapturing() const { return m_capturing.load(std::memory_order_relaxed); }

    /**
     * Loudest sample seen since the last read, 0..1.
     *
     * Reading CLEARS it, so the meter shows the peak of the interval the caller is drawing
     * rather than the loudest thing since capture began - which would only ever climb and
     * then sit still.
     *
     * A single atomic, so the frame loop can call this without touching a lock.
     */
    float ReadInputPeak();

    // Why capture stopped, when it stopped by itself. Empty while healthy.
    std::string GetLastError() const;

private:
    void CaptureThread(std::string aDeviceId);
    void SetError(const std::string& acError);

    std::thread m_thread;
    std::atomic<bool> m_capturing{false};
    std::atomic<bool> m_stopRequested{false};

    // Stored as a bit pattern so the whole thing stays lock-free; float atomics are not
    // guaranteed lock-free on every target and this is read every frame.
    std::atomic<uint32_t> m_peakBits{0};

    CaptureCallback m_callback;

    // Scratch for converting the endpoint's samples to float before handing them over.
    // Owned by the capture thread and reused, so a running capture does not allocate.
    std::vector<float> m_convertBuffer;

    mutable std::mutex m_errorLock;
    std::string m_lastError;
};
