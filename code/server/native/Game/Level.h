#pragma once

#include "GridCell.h"

struct World;

struct Level
{
    static GridCell::TPosition ToCell(const glm::vec3& acLocation) noexcept;

    Level(World* apWorld) noexcept;
    Level(Level&& aLevel) noexcept;
    Level& operator=(Level&& aLevel) noexcept;

    void Add(flecs::entity aEntity) noexcept;
    void Remove(flecs::entity aEntity) noexcept;

    gsl::not_null<GridCell*> GetCell(GridCell::TPosition aPosition) noexcept;
    gsl::not_null<World*> GetWorld() noexcept { return m_pWorld; }

    template <class T> void ForEachInRange(const GridCell* pCell, int16_t aRange, T func) noexcept
    {
        const auto pos = pCell->GetPosition();
        for (int i = pos.x - aRange; i < (aRange + pos.x); ++i)
            for (int j = pos.y - aRange; j < (aRange + pos.y); ++j)
            {
                const auto itor = m_cells.find(GridCell::TPosition{static_cast<int16_t>(i), static_cast<int16_t>(j)});
                if (itor != std::end(m_cells))
                {
                    itor.value()->ForEach(func);
                }
            }
    }

    template <class T> void ForEachCellInRange(const GridCell* pCell, int16_t aRange, T func) noexcept
    {
        const auto pos = pCell->GetPosition();
        for (int i = pos.x - aRange; i < (aRange + pos.x); ++i)
            for (int j = pos.y - aRange; j < (aRange + pos.y); ++j)
            {
                const auto itor = m_cells.find(GridCell::TPosition{static_cast<int16_t>(i), static_cast<int16_t>(j)});
                if (itor != std::end(m_cells))
                {
                    func(itor.value().get());
                }
            }
    }


protected:
    friend struct Handle;

    void Update(flecs::entity aEntity) noexcept;
    void AddPlayer(flecs::entity aEntity) noexcept;
    void RemovePlayer(flecs::entity aEntity) noexcept;

    void HandleSpawnCharacterRequest(PacketEvent<client::SpawnCharacterRequest>& aMessage) noexcept;
    void HandleMoveEntityRequest(PacketEvent<client::MoveEntityRequest>& aMessage) noexcept;
    void HandleEnterVehicleRequest(PacketEvent<client::EnterVehicleRequest>& aMessage) noexcept;
    void HandleExitVehicleRequest(PacketEvent<client::ExitVehicleRequest>& aMessage) noexcept;

    // Clothing and appearance changed on somebody already in the world. See the message's
    // own note in client.proto for why this is not SaveCharacterRequest.
    void HandleUpdateAppearanceRequest(PacketEvent<client::UpdateAppearanceRequest>& aMessage) noexcept;

    // One encoded slice of somebody talking, relayed to whoever is close enough to hear it.
    // The frame is never decoded here - see NotifyVoiceFrame in server.proto.
    void HandleVoiceFrameRequest(PacketEvent<client::VoiceFrameRequest>& aMessage) noexcept;

    // Somebody attacked somebody. Validated here, applied here, and broadcast as a result -
    // nothing a client sends is ever applied directly. See CombatEventRequest.
    void HandleCombatEventRequest(PacketEvent<client::CombatEventRequest>& aMessage) noexcept;

    // Somebody fired, reloaded or changed weapon. Ammunition is decided here, never
    // accepted from the client - see WeaponComponent.
    void HandleWeaponEventRequest(PacketEvent<client::WeaponEventRequest>& aMessage) noexcept;

    // A quickhack landed a status effect on somebody. Relayed above all to the person it
    // happened TO - their own game has no other way of knowing.
    void HandleStatusEffectRequest(PacketEvent<client::StatusEffectRequest>& aMessage) noexcept;

    // A quickhack upload started or finished. Relayed so the people around it can see it
    // happening - see QuickhackUploadRequest.
    // 'Run this hack on this person.' AUTHORITATIVE - the server owns RAM, cooldown and
    // what the hack does. The request names which hack and nothing else.
    void HandleQuickhackRequest(PacketEvent<client::QuickhackRequest>& aMessage) noexcept;

    void HandleQuickhackUploadRequest(PacketEvent<client::QuickhackUploadRequest>& aMessage) noexcept;

public:
    // Tell everyone who can see this combatant what its health and life state now are.
    // Quiet state, not an event - see NotifyCombatState.
    void BroadcastCombatState(flecs::entity aPuppet) noexcept;

private:

public:
    // Tell everyone except the owner that this puppet looks different now.
    //
    // Public because the character save path calls it too: a save that lands a good
    // appearance after a spawn went out with a bad one is exactly when the watching
    // clients need to be corrected.
    void BroadcastAppearance(flecs::entity aPuppet) noexcept;

private:
    // Hand a vehicle to somebody still inside it: the simulation AND the driver's seat.
    //
    // One function because the two must not drift apart. Transferring authority alone is
    // what shipped until now, and it left a passenger responsible for a car they could not
    // drive; sending the seat alone would put somebody at the wheel of a car another
    // machine is still simulating.
    void PromoteToDriver(flecs::entity aVehicle, flecs::entity aPlayer) noexcept;

    void ReleaseVehicleIfEmpty(flecs::entity aVehicle) noexcept;

public:
    void RemoveOwnedVehicles(flecs::entity aPlayer) noexcept;

protected:

    static server::NotifyCharacterLoad Serialize(flecs::entity aEntity) noexcept;

    void TransferCell(flecs::entity aEntity, GridCell* apOldCell, GridCell* apNewCell) noexcept;
    void CollectCells(const GridCell* apNewCell, const GridCell* apOldCell, Set<GridCell*>& aToLoad, Set<GridCell*>& aToUnload) noexcept;

private:
    Map<GridCell::TPosition, UniquePtr<GridCell>> m_cells;
    World* m_pWorld;
    flecs::system m_updateSystem;
    flecs::system m_releaseSystem;
};
