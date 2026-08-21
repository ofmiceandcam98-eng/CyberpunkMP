#pragma once

#include <string>
#include "steam/steamnetworkingsockets.h"
#include "SteamInterface.h"
#include "SynchronizedClock.h"


struct Packet;
struct Client
{
    enum EDisconnectReason
    {
        kTimeout,
        kLocalProblem,
        kKicked,
        kCannotResolve,
        kAborted,
        kNormal
    };

    struct Statistics
    {
        uint32_t SentBytes{};
        uint32_t RecvBytes{};
        uint32_t UncompressedSentBytes{};
        uint32_t UncompressedRecvBytes{};
    };

    Client(uint64_t aClientIdentifier, uint64_t aServerIdentifier) noexcept;
    virtual ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;

    bool Connect(const SteamNetworkingIPAddr& acEndpoint) noexcept;
    bool Connect(const std::string& acEndpoint) noexcept;
    bool ConnectByIp(const std::string& acEndpoint) noexcept;
    void Close() noexcept;

    void Update() noexcept;

    virtual void OnConsume(const void* apData, uint32_t aSize) = 0;
    virtual void OnConnected() = 0;
    virtual void OnDisconnected(EDisconnectReason aReason) = 0;
    virtual void OnUpdate() = 0;

    void Send(Packet* apPacket, EPacketFlags acPacketFlags = kReliable) const noexcept;

    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] SteamNetConnectionRealTimeStatus_t GetConnectionStatus() const noexcept;
    [[nodiscard]] Statistics GetStatistics() const noexcept;
    [[nodiscard]] const SynchronizedClock& GetClock() const noexcept;

    // The last transport-level refusal code (EDenialCode values), 0 if the last
    // disconnect carried none. Survives the disconnect that delivered it - it exists
    // precisely so the layer above can explain a connection that closed before
    // authentication ever ran. Cleared when a new connect attempt starts.
    [[nodiscard]] uint8_t GetLastRefusalCode() const noexcept { return m_lastRefusalCode; }

private:

    static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* apInfo);
    static void UVGetAddrInfoCallback(void* apHandle, int aStatus, void* apResult);
    void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* apInfo);

    void HandleMessage(const void* apData, uint32_t aSize) noexcept;
    void HandleServerTime(const void* apData, uint32_t aSize) noexcept;
    void HandleCompressedPayload(const void* apData, uint32_t aSize) noexcept;
    void HandleRefused(const void* apData, uint32_t aSize) noexcept;

    // Reads out everything the transport has already delivered for this connection.
    // Called before acting on a close notification: GameNetworkingSockets can hand us
    // the close in the same tick as the server's parting message (a refusal code, an
    // AuthenticationResponse with the denial reason), and invalidating the connection
    // first would destroy that message unread.
    void DrainPendingMessages() noexcept;

    void SendHandshake() const noexcept;

    HSteamNetConnection m_connection;
    ISteamNetworkingSockets* m_pInterface;
    SynchronizedClock m_clock;
    uint64_t m_lastStatisticsPoint{};
    mutable Statistics m_currentFrame{};
    Statistics m_previousFrame{};
    void* m_pLoop{};
    void* m_pHandle{};
    uint64_t m_clientIdentifier;
    uint64_t m_serverIdentifier;
    uint8_t m_lastRefusalCode{};
};
