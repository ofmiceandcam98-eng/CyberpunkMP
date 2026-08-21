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

public:
    // Tell everyone except the owner that this puppet looks different now.
    //
    // Public because the character save path calls it too: a save that lands a good
    // appearance after a spawn went out with a bad one is exactly when the watching
    // clients need to be corrected.
    void BroadcastAppearance(flecs::entity aPuppet) noexcept;

private:
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
