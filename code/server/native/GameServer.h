#pragma once

#include "System/Path.h"
#include "System/Log.h"
#include "Config.h"
#include "BanList.h"
#include "PlayerStore.h"
#include "Game/World.h"

template <typename T>
concept NetworkMessage = requires(T a, Buffer::Writer writer, Buffer::Reader reader) {
    {
        a.serialize(writer)
    } -> std::convertible_to<bool>;
    {
        a.deserialize(reader)
    } -> std::convertible_to<bool>;
};


struct GameServer final : Server
{
    TP_NOCOPYMOVE(GameServer);

    GameServer();
    ~GameServer() override;

    void Kill();
    void Run();

    template <NetworkMessage T>
    bool Send(ConnectionId aConnectionId, const T& acMessage) const;

    template<NetworkMessage T>
    auto GetSink() noexcept { return m_dispatcher.sink<PacketEvent<T>>(); }

    template <auto Func, typename... T> auto RegisterHandler(T&&... args) noexcept
    {
        using MessageType = typename std::remove_cv_t<std::remove_reference_t<typename std::tuple_element<0, typename details::signature<decltype(Func)>::type>::type>>::Type;

        static_assert(NetworkMessage<MessageType>, "Handler should take a NetworkMessage as first parameter!");

        return m_dispatcher.sink<PacketEvent<MessageType>>().template connect<Func>(std::forward<T>(args)...);
    }

    gsl::not_null<const Config*> GetConfig() const noexcept { return &m_config; }
    gsl::not_null<World*> GetWorld() noexcept { return m_pWorld.get(); }
    gsl::not_null<TaskQueue*> GetTaskQueue() noexcept { return &m_tasks; }
    gsl::not_null<Log*> GetLog() noexcept { return &m_log; }

protected:
    void OnUpdate() override;
    void OnConsume(const void* apData, uint32_t aSize, ConnectionId aConnectionId) override;
    void OnConnection(ConnectionId aHandle) override;
    void OnDisconnection(ConnectionId aConnectionId, EDisconnectReason aReason) override;

    void HandleAuthentication(const PacketEvent<client::AuthenticationRequest>& aRequest);

    static ScratchAllocator& GetScratch();

private:

    void FetchServerEntitlements();

    // Result of asking Discord to vouch for a connecting player.
    enum class EDiscordAuthResult
    {
        kOk,
        kInvalidToken,  // Discord does not recognise it, or it expired
        kNotAMember,    // real account, but not in the configured guild
        kUnreachable    // we could not ask - treated as a refusal, never as a pass
    };

    struct DiscordIdentity
    {
        std::string Id;
        std::string Username;
        std::vector<std::string> RoleIds;
        EPermissionLevel Level{EPermissionLevel::kPlayer};
    };

    // Asks Discord who a token belongs to and whether they are in the guild. The client's
    // own claims about its identity are ignored entirely.
    EDiscordAuthResult VerifyDiscordToken(const std::string& acToken, DiscordIdentity& aOutIdentity) const;

    // A short-lived record of a successful verification.
    //
    // Discord rate-limits unauthenticated requests, and every connect costs two calls.
    // A group joining together - which is exactly when a server is busiest - can trip
    // that limit, and because verification fails closed, the result would be nobody
    // getting in. Caching turns a burst of joins into one pair of calls per player
    // instead of one per attempt.
    struct CachedIdentity
    {
        DiscordIdentity Identity;
        std::chrono::steady_clock::time_point Expires;
    };

    // Keyed on the token. Guarded because the re-verification loop runs off the game
    // thread and reads this concurrently with connecting players.
    mutable std::mutex m_discordCacheMutex;
    mutable Map<std::string, CachedIdentity> m_discordCache;

    // Deliberately short. A cached entry means a Discord ban takes up to this long to
    // block a RECONNECT - the in-session re-verification loop still removes them within
    // two minutes either way, and local bans are checked separately and never cached.
    static constexpr auto kDiscordCacheTtl = std::chrono::minutes(3);

    // Logs per-player ping / packet loss on a slow heartbeat, so a laggy player can be
    // told apart from a struggling server.
    void ReportPlayerConnections(std::chrono::steady_clock::time_point aNow);

    // Re-checks connected players against Discord, so a ban or role change there takes
    // effect here without waiting for them to reconnect.
    void ReverifyPlayers(std::chrono::steady_clock::time_point aNow);

    // Writes everyone's position to disk on a timer, so a server crash costs seconds
    // rather than a session. Disconnects save immediately and do not wait for this.
    void SavePlayerPositions(std::chrono::steady_clock::time_point aNow);

    // Keeps jailed players in their cell, and lets them out when the time is up.
    //
    // The cell is a rule, not a room. Cyberpunk's doors are not synchronised - every
    // client has its own - so a real cell door would hold nobody. What the server DOES
    // know, every tick, is where everyone is, so the sentence is enforced by putting
    // anyone who wanders too far straight back.
    void EnforceJail(std::chrono::steady_clock::time_point aNow);

public:
    // Where players reappear after dying, set with /setspawn. Persisted alongside the
    // player records so it survives a restart.
    void SetRespawnPoint(const glm::vec3& acPosition, float aYaw);
    bool GetRespawnPoint(glm::vec3& aPosition, float& aYaw) const;

    BanList& GetBanList() noexcept { return m_bans; }
    PlayerStore& GetPlayerStore() noexcept { return m_players; }

private:

    Path m_path;
    Log m_log;
    UniquePtr<World> m_pWorld;
    TaskQueue m_tasks;
    bool m_run = true;
    std::chrono::steady_clock::time_point m_lastUpdate;
    std::chrono::steady_clock::time_point m_lastConnectionReport;
    std::chrono::steady_clock::time_point m_lastReverify;
    BanList m_bans;
    PlayerStore m_players;
    std::chrono::steady_clock::time_point m_lastPlayerSave;
    std::chrono::steady_clock::time_point m_lastJailCheck;

    glm::vec3 m_respawnPosition{};
    float m_respawnYaw{0.f};
    bool m_hasRespawnPoint{false};
    std::filesystem::path m_respawnPath;
    entt::dispatcher m_dispatcher;

    Config m_config;
};

template <NetworkMessage T>
bool GameServer::Send(ConnectionId aConnectionId, const T& acMessage) const
{
    ScopedResetAllocator _{GetScratch()};

    Buffer buffer(1 << 18);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Skip the first byte as it is used by packet

    server::Serializer::Process(writer, acMessage);

    PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), (uint32_t)writer.Size());
    Server::Send(aConnectionId, &packet, T::kReliable ? kReliable : kUnreliable);

    return true;
}


extern GameServer* GServer;
