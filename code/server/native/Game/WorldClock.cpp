#include "WorldClock.h"
#include "World.h"

#include "GameServer.h"
#include "PlayerManager.h"
#include "Components/PlayerComponent.h"
#include "Core/Filesystem.h"

#include <fstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace
{
// Re-assert against client drift; save so a restart costs at most this much time.
constexpr auto kBroadcastEvery = std::chrono::seconds(30);
constexpr auto kSaveEvery = std::chrono::minutes(5);
}

WorldClock::WorldClock(World* apWorld) noexcept
    : m_pWorld(apWorld)
    , m_lastTick(std::chrono::steady_clock::now())
    , m_lastBroadcast(std::chrono::steady_clock::now())
    , m_lastSave(std::chrono::steady_clock::now())
    , m_statePath(GetPath() / "config" / "worldstate.json")
{
    Load();

    spdlog::info("World clock: day {} {:02}:{:02}, scale x{}, weather {:x}",
                 GetGameTimeSeconds() / 86400, (GetGameTimeSeconds() % 86400) / 3600,
                 (GetGameTimeSeconds() % 3600) / 60, m_timeScale, m_weatherId);

    m_tickSystem = apWorld->system("World clock").kind(flecs::OnUpdate).run([this](flecs::iter&) { Tick(); });
    m_tickSystem.child_of(apWorld->entity("systems"));
}

void WorldClock::Tick() noexcept
{
    const auto now = std::chrono::steady_clock::now();

    const auto delta = std::chrono::duration<double>(now - m_lastTick).count();
    m_lastTick = now;

    if (m_realTime)
    {
        // The wall clock IS the game clock. localtime honours the container's TZ, so
        // the server's community timezone is one compose env away. The day counter
        // keeps whatever day the city was on - only the hour mirrors reality.
        const auto wall = std::time(nullptr);
        std::tm parts{};
#ifdef _WIN32
        localtime_s(&parts, &wall);
#else
        localtime_r(&wall, &parts);
#endif
        const double dayStart = std::floor(m_gameTimeSeconds / 86400.0) * 86400.0;
        m_gameTimeSeconds = dayStart + parts.tm_hour * 3600.0 + parts.tm_min * 60.0 + parts.tm_sec;
    }
    // A debugger pause or a machine sleep must not fast-forward the whole city.
    else if (delta > 0.0 && delta < 60.0)
        m_gameTimeSeconds += delta * m_timeScale;

    if (now - m_lastBroadcast >= kBroadcastEvery)
    {
        m_lastBroadcast = now;
        Broadcast();
    }

    if (now - m_lastSave >= kSaveEvery)
    {
        m_lastSave = now;
        Save();
    }
}

server::NotifyWorldState WorldClock::BuildMessage() const noexcept
{
    server::NotifyWorldState state;
    state.set_game_time_seconds(GetGameTimeSeconds());
    // In real-time mode the clients advance at exactly real speed between broadcasts.
    state.set_time_scale(m_realTime ? 1.f : m_timeScale);
    state.set_weather_id(m_weatherId);
    state.set_transition_seconds(m_transitionSeconds);

    return state;
}

void WorldClock::SendTo(ConnectionId aConnectionId) noexcept
{
    GServer->Send(aConnectionId, BuildMessage());
}

void WorldClock::Broadcast() noexcept
{
    const auto message = BuildMessage();

    auto* pPlayerManager = m_pWorld->get_mut<PlayerManager>();
    if (!pPlayerManager)
        return;

    pPlayerManager->ForEach(
        [&message](flecs::entity aPlayer)
        {
            if (const auto* pPlayer = aPlayer.get<PlayerComponent>())
                GServer->Send(pPlayer->Connection, message);
        });
}

void WorldClock::SetTime(uint64_t aGameTimeSeconds) noexcept
{
    // Steering the clock by hand is a statement that it should stay steered.
    m_realTime = false;
    m_gameTimeSeconds = static_cast<double>(aGameTimeSeconds);
    Broadcast();
    Save();
}

void WorldClock::SetRealTime(bool aEnabled) noexcept
{
    m_realTime = aEnabled;
    Tick();
    Broadcast();
    Save();
}

void WorldClock::SetWeather(uint64_t aWeatherId, float aTransitionSeconds) noexcept
{
    m_weatherId = aWeatherId;
    m_transitionSeconds = aTransitionSeconds;
    Broadcast();
    Save();
}

void WorldClock::Load() noexcept
{
    try
    {
        std::ifstream file(m_statePath);
        if (!file.is_open())
            return; // first boot - defaults stand, first Save() writes the file

        const auto data = json::parse(file);
        m_gameTimeSeconds = data.value("game_time_seconds", m_gameTimeSeconds);
        m_realTime = data.value("real_time", m_realTime);
        m_timeScale = data.value("time_scale", m_timeScale);
        m_weatherId = data.value("weather_id", m_weatherId);
        m_transitionSeconds = data.value("transition_seconds", m_transitionSeconds);
    }
    catch (const std::exception& e)
    {
        spdlog::warn("worldstate.json unreadable ({}) - starting from defaults", e.what());
    }
}

void WorldClock::Save() noexcept
{
    try
    {
        json data{
            {"game_time_seconds", m_gameTimeSeconds},
            {"real_time", m_realTime},
            {"time_scale", m_timeScale},
            {"weather_id", m_weatherId},
            {"transition_seconds", m_transitionSeconds},
        };

        std::ofstream file(m_statePath);
        file << data.dump(2);
    }
    catch (const std::exception& e)
    {
        spdlog::warn("could not save worldstate.json: {}", e.what());
    }
}
