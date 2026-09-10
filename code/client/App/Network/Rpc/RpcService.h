#pragma once
#include "Core/Foundation/Feature.hpp"
#include "Core/Hooking/HookingAgent.hpp"

struct RpcId
{
    uint64_t Klass;
    uint64_t Function;

    bool operator==(const RpcId& acRhs) const { return Klass == acRhs.Klass && Function == acRhs.Function; }
};

struct RpcHandler;
struct CachedRpcHandler
{
    RpcId Id;
    RpcHandler* Handler;
};

template <> struct std::hash<RpcId>
{
    std::size_t operator()(const RpcId& s) const noexcept { return s.Klass ^ (s.Function << 1); }
};

struct RpcService : Core::Feature, Core::HookingAgent
{
    RpcService(RED4ext::PluginHandle aPlugin, const RED4ext::Sdk* aSdk);
    ~RpcService() override;

    void OnInitialize() override;
    void OnShutdown() override;

    std::optional<uint32_t> GetRpcId(uint64_t aKlass, uint64_t aFunction) const;

protected:

    static bool PrepareRpc(RED4ext::CGameApplication* aApp);

    void HandleRpc(const PacketEvent<server::RpcCall>& aMessage);
    void HandleRpcDefinitions(const PacketEvent<server::RpcDefinitions>& aMessage);
    bool Call(const server::RpcCall& aMessage) const;

private:

    Map<RpcId, uint32_t> m_serverRpcs;
    Vector<CachedRpcHandler> m_clientRpcs;

    // Ids already reported as unhandled, so each is logged ONCE per connection.
    //
    // A refusal is not a one-off: a server that pushes a snapshot to a client predating the
    // function refuses on every push, for every player, forever. That turns a real signal
    // into noise nobody reads, and it is the failure mode the phone snapshot would create
    // first. Mutable because Call() is const and this is bookkeeping about logging, not
    // about the RPC table itself; cleared whenever definitions arrive, since that is a new
    // table and the old ids mean nothing.
    mutable Set<uint32_t> m_refusedIds;
};
