#pragma once

#include "Core/Buffer.h"
#include "Core/ScratchAllocator.h"
#include "Core/Foundation/Feature.hpp"
#include "Network/Client.h"
#include "Network/Packet.h"
#include "Rpc/RpcService.h"

template <typename T> struct DebugType;

template <typename T>
concept NetworkMessage = requires(T a, Buffer::Writer writer, Buffer::Reader reader)
{
    {
        a.serialize(writer)
    } -> std::convertible_to<bool>;
    {
        a.deserialize(reader)
    } -> std::convertible_to<bool>;
};

struct NetworkService final
    : Client
    , Core::Feature
{
    NetworkService();
    ~NetworkService() override;

    template <NetworkMessage T>
    bool Send(const T& acMessage);

    template <NetworkMessage T>
    auto GetSink() noexcept { return m_dispatcher.sink<PacketEvent<T>>(); }

    template <auto Func, typename... T>
    auto RegisterHandler(T&&... args) noexcept
    {
        using MessageType = typename std::remove_cv_t<std::remove_reference_t < typename std::tuple_element<0, typename details::signature<decltype(Func)>::type>::type >>::Type;

        static_assert(NetworkMessage<MessageType>, "Handler should take a NetworkMessage as first parameter!");

        return m_dispatcher.sink<PacketEvent<MessageType>>().connect<Func>(std::forward<T>(args)...);
    }

    entt::dispatcher& GetDispatcher() { return m_dispatcher; }
    const server::Settings& GetServerSettings() const { return m_settings; }

    TP_NOCOPYMOVE(NetworkService);

protected:
    void BindMessageHandlers();

    void OnConsume(const void* apData, uint32_t aSize) override;
    void OnConnected() override;
    void OnDisconnected(EDisconnectReason aReason) override;
    void OnUpdate() override;
    void OnGameUpdate(RED4ext::CGameApplication* apApp) override;

    void HandleAuthentication(const PacketEvent<server::AuthenticationResponse>& aResponse);

public:
    // Announce this player to the server: position, equipment, appearance.
    //
    // Split out of HandleAuthentication because connecting can now happen at the main
    // menu, where there is no player to describe. When that happens the spawn is held and
    // this is called again once the world is up.
    void SendSpawnCharacterRequest();

    // Decide, AT THE MOMENT SOMEBODY ASKS TO CONNECT, whether this connection may announce
    // the player by itself.
    //
    // Called immediately before Connect(). The timing is the whole point - see the banner
    // over the implementation. aFromMenu: the main menu asked, so the spawn is held whatever
    // world happens to be loaded behind it.
    void CaptureSpawnIntent(bool aFromMenu = false);

    // "The player has now asked to be in the world." Said by EnterWorld(), which is PLAY.
    void AuthorizeSpawn();

    // "The player has left the world but is still connected." Said by LeaveWorld().
    void HoldSpawnUntilEntry();

    // Is a spawn still owed? True between authenticating at the menu and entering the
    // world.
    bool IsSpawnDeferred() const { return m_spawnDeferred; }

    // ---------------------------------------------------------------------------
    // The resume: an involuntary disconnect mid-session (deploy flip, dropped link)
    // redials by itself for a short window instead of ending the session. No token and
    // no protocol change: identity is the Discord token that rides every connect, and
    // the server restores the character from its stores. The NightCoop hub needs a
    // signed pass because it keeps sessions only in memory; our server does not.
    // Atlas: reconnect-pass-so-deploy-flips-stop-bouncing-players-nightco.
    // ---------------------------------------------------------------------------

    // Said by OnDisconnected when the end of the session was nobody's choice and nobody
    // refused anything.
    void ArmResume();

    // Said by the player's own Disconnect(), and by the window closing or a refusal
    // arriving. aWhy goes in the log - a resume that stops silently reads as a hang.
    void CancelResume(const char* acpWhy);

    // True from the involuntary disconnect until the session is re-authenticated or
    // given up. Script reads it (via NetworkWorldSystem) to say "rejoining" instead of
    // treating the disconnect as final.
    bool IsResuming() const { return m_resumeArmed; }

private:

    static ScratchAllocator& GetScratch();

private:

    // Logs ping / link quality on a slow heartbeat, and immediately when the link degrades.
    void ReportConnectionHealth();

    // Redials while a resume is armed. Runs from OnUpdate, which ticks whether or not a
    // connection exists.
    void TickResume();

    // The resume window. 30 seconds covers a deploy flip (the container is back in a few)
    // and spans the transport's own dead-peer detection, so a lingering server-side
    // session for this account ages out before the window closes. 3 seconds between
    // dials: an unreachable server answers nothing, so there is no refusal to wait for.
    static constexpr std::chrono::seconds kResumeWindow{30};
    static constexpr std::chrono::seconds kResumeDialGap{3};

    bool m_resumeArmed = false;
    std::chrono::time_point<std::chrono::steady_clock> m_resumeDeadline{};
    std::chrono::time_point<std::chrono::steady_clock> m_nextResumeDial{};

    std::chrono::time_point<std::chrono::steady_clock> m_lastUpdate;
    std::chrono::time_point<std::chrono::steady_clock> m_lastHealthReport;
    bool m_ready = false;
    bool m_authenticated = false;

    // Authenticated, but with no world to spawn into yet - the character selector state.
    bool m_spawnDeferred = false;

    // May this connection announce the player on its own when authentication comes back?
    // Captured when the connect is ASKED FOR, never inferred from the world a second later.
    bool m_spawnAuthorized = false;
    bool m_isPaused = false;
    entt::dispatcher m_dispatcher;
    uint64_t m_lastCharacterUpdate{};
    server::Settings m_settings;
};

template <NetworkMessage T>
bool NetworkService::Send(const T& acMessage)
{
    ScopedResetAllocator _{GetScratch()};

    Buffer buffer(1 << 18);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Skip the first byte as it is used by packet

    client::Serializer::Process(writer, acMessage);

    PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), (uint32_t)writer.Size());
    Client::Send(&packet, T::kReliable ? kReliable : kUnreliable);

    return true;
}
