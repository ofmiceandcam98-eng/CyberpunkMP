#pragma once
#include <chrono>
#include <string>

#include <atomic>

struct World;
struct ServerListSystem
{
    ServerListSystem(gsl::not_null<World*> apWorld);

private:
    void Tick() noexcept;
    void Announce() noexcept;

    // Not static any more: it reports back through m_refused, so a list that refuses us stops
    // the announcing instead of killing the server. Takes the endpoint because it is
    // configuration now (Config::ServerListEndpoint), not a hardcoded host.
    void PostAnnouncement(
        const std::string& acEndpoint, const std::string& acName, const std::string& acDesc, const std::string& acIconUrl, uint16_t aPort, uint16_t aTick, uint16_t aPlayerCount,
        uint16_t aPlayerMaxCount, const std::string& acTagList, bool aPublic, bool aPassword, int32 aFlags) noexcept;

    gsl::not_null<World*> m_pWorld;
    flecs::system m_updateSystem;
    flecs::observer m_serverListObserver;
    mutable std::chrono::steady_clock::time_point m_nextAnnounce;

    // BOTH ATOMIC because the announce runs on a DETACHED THREAD and these are read on the
    // main thread's tick. This project has already lost a day to a data race that "could not
    // happen" (see the flecs stack allocator entry in the map) - a plain bool here would be
    // the same mistake in miniature, and cheap to avoid.
    std::atomic<bool> m_announcedDisabled{false};
    std::atomic<bool> m_refused{false};
};
