#pragma once

struct World;

// The server's metronome: one canonical game clock and one sky for everyone.
//
// Singleplayer gives every machine its own private noon - each client simulates its own
// time of day and rolls its own weather, so no two players ever stood under the same
// sky. This makes both of them server state. The clock ticks even while the server is
// empty, so the city has one continuous timeline instead of resetting whenever the
// first player joins, and it survives restarts through config/worldstate.json.
//
// Clients apply what they are sent and re-assert it locally; the server re-broadcasts
// on an interval anyway, because the singleplayer brain drifts the clock given the
// chance. See docs/WORLD-STATE.md ("Clock and weather - the server's metronome").
struct WorldClock
{
    WorldClock(World* apWorld) noexcept;
    WorldClock(WorldClock&&) noexcept = default;
    WorldClock& operator=(WorldClock&&) noexcept = default;

    // One player, on join - everyone else is already in sync.
    void SendTo(ConnectionId aConnectionId) noexcept;

    // Everyone, on change and on the re-assert interval.
    void Broadcast() noexcept;

    uint64_t GetGameTimeSeconds() const noexcept { return static_cast<uint64_t>(m_gameTimeSeconds); }

    // For a future admin command (/time, /weather). Changing either broadcasts at once.
    void SetTime(uint64_t aGameTimeSeconds) noexcept;
    void SetWeather(uint64_t aWeatherId, float aTransitionSeconds) noexcept;

protected:
    void Tick() noexcept;
    void Load() noexcept;
    void Save() noexcept;

    server::NotifyWorldState BuildMessage() const noexcept;

private:
    World* m_pWorld;

    // double, not integer seconds: at time_scale 8 a 100ms tick advances 0.8 game
    // seconds, and integer truncation every tick would make the clock run slow.
    double m_gameTimeSeconds{8.0 * 3600.0}; // day 0, 08:00 - a sensible first sunrise
    float m_timeScale{8.f};                 // game-seconds per real second
    uint64_t m_weatherId{0};                // 0 = leave the local sky alone
    float m_transitionSeconds{10.f};

    std::chrono::steady_clock::time_point m_lastTick;
    std::chrono::steady_clock::time_point m_lastBroadcast;
    std::chrono::steady_clock::time_point m_lastSave;

    std::filesystem::path m_statePath;
    flecs::system m_tickSystem;
};
