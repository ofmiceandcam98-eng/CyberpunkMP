#pragma once

#include "Core/Stl.hpp"
#include "RED4ext/Scripting/Natives/Generated/Vector4.hpp"
#include "RED4ext/Scripting/Natives/Generated/Quaternion.hpp"

struct VehicleSystem : RED4ext::IScriptable
{
    RTTI_IMPL_TYPEINFO(VehicleSystem)
    RTTI_IMPL_ALLOCATOR();

    void Update(uint64_t aTick);

    void OnInitialize(const RED4ext::JobHandle& aJob);
    void OnWorldAttached(RED4ext::world::RuntimeScene* aScene);
    void OnAfterWorldDetach();
    void OnDisconnected();

    std::optional<uint64_t> GetVehicleRemoteId() const;
    std::optional<Red::EntityID> GetVehicleGameId() const;

    // Stamped on every MoveEntityRequest for the vehicle we simulate. The server drops
    // movement whose epoch is stale - packets from before we lost (or gained) the car.
    uint32_t GetAuthorityEpoch() const;

protected:

    void OnVehicleEnter(Red::EntityID aVehicle, const Red::TweakDBID& aVehicleTdbid, Red::CName aName, const Red::Vector4& aPostion, const Red::Quaternion& aOrientation);
    void OnVehicleExit();
    void OnVehicleReady(const Red::EntityID& vehicle);

    bool HandleVehicleLoadMessage(const PacketEvent<server::NotifyVehicleLoad>& aMessage);
    bool HandleVehicleEnterMessage(const PacketEvent<server::NotifyVehicleEnter>& aMessage);
    bool HandleVehicleExitMessage(const PacketEvent<server::NotifyVehicleExit>& aMessage);
    bool HandleAuthorityAssigned(const PacketEvent<server::NotifyAuthorityAssigned>& aMessage);
    bool HandleAuthorityRevoked(const PacketEvent<server::NotifyAuthorityRevoked>& aMessage);

    // Listens alongside NetworkWorldSystem's own unload handling (entt sinks fan out).
    // The server unloading the vehicle this client simulates is an implicit revoke -
    // without this, the client kept streaming movement for a destroyed car (512
    // rejected packets in one session). Queued mounts for the entity die with it too.
    bool HandleEntityUnload(const PacketEvent<server::NotifyEntityUnload>& aMessage);

    void DoMount(flecs::entity aCharacter, Red::EntityID aVehicle, Red::CName aSit);

private:
    bool m_ready{false};
    // Keyed by SERVER id, not engine EntityID: the engine id is resolved through the
    // flecs mirror, which returns 0 whenever the mirror does not exist yet - and a mount
    // queued under key 0 can never be found again. Both "Couldn't find vehicle" losses
    // in the 2026-08-18 test were this. The server id is stable from the moment the
    // message arrives.
    Core::Map<uint64_t, Vector<PacketEvent<server::NotifyVehicleEnter>>> m_pendingMounts;
    Red::CBaseFunction* m_pSpawnVehicle;
    Red::CBaseFunction* m_pEnterVehicle;
    Red::CBaseFunction* m_pExitVehicle;
    std::optional<uint64_t> m_vehicleRemoteId;
    std::optional<Red::EntityID> m_vehicleGameId;
    // What the local player is currently sitting in, for ExitVehicleRequest to name.
    // m_mountedServerId is known at enter time for a resolved network car; for a car we
    // spawned ourselves it arrives later via HandleAuthorityAssigned (m_vehicleRemoteId).
    std::optional<uint64_t> m_mountedServerId;
    uint64_t m_mountedSlot{0};
    // Our own spawned car, remembered across exits. The server never replicates a
    // vehicle spawn back to its owner, so FindEntity can never resolve our own car -
    // without this memory every re-enter position-spawned a DUPLICATE server entity
    // (two overlapping cars for everyone else) and a seat swap through a passenger
    // door was refused as a desync fork. Cleared when the car is unloaded, when
    // authority over it moves away, and on disconnect.
    std::optional<Red::EntityID> m_lastOwnVehicleGameId;
    std::optional<uint64_t> m_lastOwnVehicleServerId;
    uint32_t m_authorityEpoch{0};
};

RTTI_DEFINE_CLASS(VehicleSystem, { 
    RTTI_ALIAS("CyberpunkMP.World.VehicleSystem");
    RTTI_METHOD(OnVehicleEnter);
    RTTI_METHOD(OnVehicleExit);
    RTTI_METHOD(OnVehicleReady);
});