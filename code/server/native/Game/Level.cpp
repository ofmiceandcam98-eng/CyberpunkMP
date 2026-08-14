#include "Level.h"

#include <Components/LevelTag.h>
#include <Components/MovementComponent.h>
#include <Components/PlayerComponent.h>
#include <Components/AppearanceComponent.h>
#include <Components/AttachmentComponent.h>
#include "Components/CellComponent.h"
#include <Components/CharacterComponent.h>
#include <Components/VehicleComponent.h>

#include "GameServer.h"
#include "World.h"
#include "PlayerManager.h"
#include "Systems/ChatSystem.h"   // telling someone their seat is taken
#include "Validation.h"           // sanity checks on anything a client sent


constexpr static float sCellSize = 60 * 100;
constexpr static int16_t sCellLoadRadius = 3;
constexpr static int16_t sCellUnloadRadius = 4;

GridCell::TPosition Level::ToCell(const glm::vec3& acLocation) noexcept
{
    return {static_cast<int16_t>(acLocation.x / sCellSize), static_cast<int16_t>(acLocation.y / sCellSize)};
}

Level::Level(World* apWorld) noexcept
    : m_pWorld(apWorld)
{
    GServer->RegisterHandler<&Level::HandleSpawnCharacterRequest>(this);
    GServer->RegisterHandler<&Level::HandleMoveEntityRequest>(this);
    GServer->RegisterHandler<&Level::HandleEnterVehicleRequest>(this);
    GServer->RegisterHandler<&Level::HandleExitVehicleRequest>(this);

    m_updateSystem = m_pWorld->system<const LevelActorTag>("Level Update")
        .each([this](flecs::entity aEntity, const LevelActorTag&)
        {
            Update(aEntity);
        });

    m_updateSystem.child_of(m_pWorld->entity("systems"));
}

Level::Level(Level&& aLevel) noexcept
    : m_cells(std::exchange(aLevel.m_cells, {}))
    , m_pWorld(std::exchange(aLevel.m_pWorld, nullptr))
{
}

Level& Level::operator=(Level&& aLevel) noexcept
{
    std::swap(m_pWorld, aLevel.m_pWorld);
    std::swap(m_cells, aLevel.m_cells);

    return *this;
}

void Level::Add(flecs::entity aEntity) noexcept
{
    if (!aEntity)
        return;

    if(aEntity.has<PlayerComponent>())
    {
        AddPlayer(aEntity);
        return;
    }

    aEntity.add<LevelActorTag>();

    auto* pMovementComponent = aEntity.get<MovementComponent>();
    if (!pMovementComponent)
    {
        spdlog::error("Attempt to add entity to level without a movement component!");
        return;
    }

    const auto cellPosition = ToCell(pMovementComponent->Position);
    const auto pCell = GetCell(cellPosition);

    server::NotifyCharacterLoad load = Serialize(aEntity);

    flecs::entity owner = aEntity.parent();

    GetWorld()->each([this, &load, owner](flecs::entity aEntity, const PlayerComponent& aPlayerComponent, const LevelSystemTag&)
    {
        if (!IsDebug() && owner == aEntity)
            return;

        GServer->Send(aPlayerComponent.Connection, load);
    });

    pCell->Add(aEntity);
}

void Level::Remove(flecs::entity aEntity) noexcept
{
    if (!aEntity)
        return;

    if (aEntity.has<PlayerComponent>())
    {
        RemovePlayer(aEntity);
        return;
    }

    server::NotifyEntityUnload unload;
    unload.set_id(aEntity);

    flecs::entity owner = aEntity.parent();

    GetWorld()->each([this, &unload, owner](flecs::entity aEntity, const PlayerComponent& aPlayerComponent)
    {
        if (!IsDebug() && owner == aEntity)
            return;

        GServer->Send(aPlayerComponent.Connection, unload);
    });

    if (auto* pCellComponent = aEntity.get<CellComponent>(); pCellComponent)
    {
        auto& pCell = pCellComponent->pCell;

        pCell->Remove(aEntity);

        if (pCell->Count() == 0)
        {
            m_cells.erase(pCell->GetPosition());
        }
    }

    aEntity.destruct();
}


void Level::Update(flecs::entity aEntity) noexcept
{
}

void Level::AddPlayer(flecs::entity aEntity) noexcept
{
    aEntity.add<LevelSystemTag>();

    auto* pPlayerComponent = aEntity.get<PlayerComponent>();
    if (!pPlayerComponent)
        return;

    Add(pPlayerComponent->Puppet);

    GetWorld()->each(
        [this, player = aEntity, pPlayerComponent](flecs::entity aEntity, const LevelActorTag&)
        {
            if (aEntity.parent() == player)
                return;

            GServer->Send(pPlayerComponent->Connection, Serialize(aEntity));
        });
}

void Level::RemovePlayer(flecs::entity aEntity) noexcept
{
    aEntity.remove<LevelSystemTag>();
}

// Takes a leaving player's cars with them.
//
// Vehicles are created as children of whoever is driving, so flecs destroys them when the
// player entity goes - silently. Everyone else's client had been told to spawn a copy and
// is never told to remove it, so every disconnect while driving left a permanent
// abandoned car in the other players' worlds. Routing it through Remove() sends the
// unload first.
void Level::RemoveOwnedVehicles(flecs::entity aPlayer) noexcept
{
    if (!aPlayer)
        return;

    Vector<flecs::entity> owned;

    GetWorld()->each(
        [aPlayer, &owned](flecs::entity aEntity, const VehicleComponent&)
        {
            if (aEntity.parent() == aPlayer)
                owned.push_back(aEntity);
        });

    // Collected first. Destroying entities from inside the iteration would invalidate it.
    for (auto vehicle : owned)
        Remove(vehicle);
}

void Level::HandleSpawnCharacterRequest(PacketEvent<client::SpawnCharacterRequest>& aMessage) noexcept
{
    auto player = GetWorld()->get<PlayerManager>()->GetByConnectionId(aMessage.ConnectionId);

    glm::vec3 pos = {aMessage.get_position().get_x(), aMessage.get_position().get_y(), aMessage.get_position().get_z()};
    glm::vec3 rot = {0.f, 0.f, aMessage.get_rotation()};
    const Vector<uint64_t> equipment = aMessage.get_equipment();
    const Vector<uint8_t> ccstate = aMessage.get_ccstate();

    server::SpawnCharacterResponse response;
    response.set_cookie(aMessage.get_cookie());

    auto* pComponent = player.get_mut<PlayerComponent>();
    if (!pComponent)
    {
        spdlog::warn("Received a spawn character request from player who doesn't have a player component!");

        GServer->Send(aMessage.ConnectionId, response);
        return;
    }

    // A spawn arrives once, and everything in it is stored and rebroadcast to every other
    // client - so this is the packet where a bad value does the most damage and gets the
    // least scrutiny.
    if (!Validation::IsSanePosition(pos) || !Validation::IsSaneRotation(rot))
    {
        spdlog::warn("Refused a spawn from {} at a nonsense position ({}, {}, {})",
                     pComponent->Username, pos.x, pos.y, pos.z);

        // Answered rather than ignored. The client waits on this response and would hang
        // at a black screen if we simply dropped it.
        GServer->Send(aMessage.ConnectionId, response);
        return;
    }

    // The appearance blob is relayed verbatim to everyone in range. Unbounded, it is a way
    // to make one join allocate arbitrary memory on the server and on every other client.
    // Real ones measure 6-9KB.
    constexpr size_t kMaxCcstate = 256 * 1024;
    constexpr size_t kMaxEquipment = 64;

    if (ccstate.size() > kMaxCcstate || equipment.size() > kMaxEquipment)
    {
        spdlog::warn("Refused a spawn from {} - {} bytes of appearance, {} equipment item(s)",
                     pComponent->Username, ccstate.size(), equipment.size());

        GServer->Send(aMessage.ConnectionId, response);
        return;
    }

    if (IsDebug())
    {
        pos += glm::vec3(2, 0, 0);
        rot += glm::vec3(0, 0, 3.1415);
    }

    // Put returning players back where they left off.
    //
    // The client spawns at whatever position its own singleplayer save had, which has
    // nothing to do with where this character was standing on the server. Somebody who
    // crashes in the middle of a scene should come back to that scene, not to wherever
    // their offline save last put V.
    //
    // The server's record wins, and the client is asked to move itself - it owns its own
    // position, so editing our copy alone would be undone by its next update.
    const auto* pSaved = GServer->GetPlayerStore().Find(pComponent->DiscordId);
    if (pSaved)
    {
        pos = glm::vec3(pSaved->X, pSaved->Y, pSaved->Z);
        rot = glm::vec3(0.f, 0.f, pSaved->Yaw);

        server::NotifyTeleport teleport;

        common::Vector3 position;
        position.set_x(pSaved->X);
        position.set_y(pSaved->Y);
        position.set_z(pSaved->Z);
        teleport.set_position(position);
        teleport.set_rotation(pSaved->Yaw);

        GServer->Send(aMessage.ConnectionId, teleport);

        spdlog::info("Restored {} to ({:.1f}, {:.1f}, {:.1f})", pComponent->Username,
                     pSaved->X, pSaved->Y, pSaved->Z);
    }

    pComponent->Puppet = GetWorld()->entity()
        .child_of(player)
        .set<MovementComponent>({pos, rot, {}})
        .set<AppearanceComponent>({equipment, ccstate});

    response.set_id(pComponent->Puppet);
    GServer->Send(aMessage.ConnectionId, response);

    Add(player);
}

void Level::HandleMoveEntityRequest(PacketEvent<client::MoveEntityRequest>& aMessage) noexcept
{
    flecs::entity target(GetWorld()->get_world(), aMessage.get_id());

    if (!target)
    {
        spdlog::warn("Attempt to move invalid entity {:x} from connection {:x}", aMessage.get_id(), aMessage.ConnectionId);
        return;
    }

    const auto player = target.parent();
    if (!player)
    {
        spdlog::warn("Attempt to move an entity without an owner from connection {:x}", aMessage.ConnectionId);
        return;
    }

    auto* pPlayer = player.get<PlayerComponent>();
    if (!pPlayer)
    {
        spdlog::warn("The entity's owner is not a player! From connection {:x}", aMessage.ConnectionId);
        return;
    }

    if (pPlayer->Connection != aMessage.ConnectionId)
    {
        spdlog::warn("The entity's owner is not the current player! From connection {:x}", aMessage.ConnectionId);
        return;
    }

    MovementComponent component;

    auto& pos = aMessage.get_position();

    if (aMessage.has_full_rotation())
    {
        component.Rotation = {aMessage.get_full_rotation().get_x(), aMessage.get_full_rotation().get_y(), aMessage.get_full_rotation().get_z()};
    }
    else
    {
        auto& rot = aMessage.get_rotation();

        component.Rotation = {0.f, 0.f, rot};
    }

    component.Position = {pos.get_x(), pos.get_y(), pos.get_z()};
    component.Velocity = aMessage.get_speed();
    component.Tick = aMessage.get_tick();

    // Carried forward, because the component is replaced wholesale below rather than
    // edited. Interest management sends only every Nth update to distant players, and a
    // sequence that reset to zero on every packet would make "every 4th" mean "every one".
    if (const auto* pPrevious = target.get<MovementComponent>())
        component.Sequence = pPrevious->Sequence + 1;

    // Dropped, not clamped. See Validation.h - a non-finite position does not crash
    // anything, it silently switches off every system that measures distance, and then
    // gets written to the persistent store on disconnect so it survives a restart.
    //
    // Keeping the last good position is right: this arrives thirty times a second, so
    // discarding one is invisible, and inventing a position the player is not at would be
    // a desync we would then have to debug.
    if (!Validation::IsSanePosition(component.Position) ||
        !Validation::IsSaneRotation(component.Rotation) ||
        !Validation::IsSaneSpeed(component.Velocity))
    {
        // Rate-limited: a client stuck producing NaN would otherwise write thirty lines a
        // second and bury everything else in the log.
        static thread_local std::chrono::steady_clock::time_point s_lastComplaint{};
        const auto now = std::chrono::steady_clock::now();

        if (now - s_lastComplaint > std::chrono::seconds(5))
        {
            s_lastComplaint = now;
            spdlog::warn("Dropped a nonsense movement packet from {} - pos ({}, {}, {}) speed {}",
                         pPlayer->Username, component.Position.x, component.Position.y,
                         component.Position.z, component.Velocity);
        }

        return;
    }

    if constexpr (IsDebug())
    {
        component.Position += glm::vec3(5, 0, 0);
        if (!target.has<VehicleComponent>())
            component.Rotation += glm::vec3(0, 0, 3.1415);
    }

    target.set(component);
}

void Level::HandleEnterVehicleRequest(PacketEvent<client::EnterVehicleRequest>& aMessage) noexcept
{
    flecs::entity target(GetWorld()->get_world(), aMessage.get_id());

    if (!target)
    {
        spdlog::warn("Attempt to enter vehicle from invalid entity from connection {:x}", aMessage.ConnectionId);
        return;
    }

    const auto player = target.parent();
    if (!player)
    {
        spdlog::warn("Attempt to enter vehicle an entity without an owner from connection {:x}", aMessage.ConnectionId);
        return;
    }

    auto* pPlayer = player.get<PlayerComponent>();
    if (!pPlayer)
    {
        spdlog::warn("The entity's owner is not a player! From connection {:x}", aMessage.ConnectionId);
        return;
    }

    if (pPlayer->Connection != aMessage.ConnectionId)
    {
        spdlog::warn("The entity's owner is not the current player! From connection {:x}", aMessage.ConnectionId);
        return;
    }

    flecs::entity vehicle;

    if (aMessage.has_remote_vehicle_id())
    {
        vehicle = flecs::entity(GetWorld()->get_world(), aMessage.get_remote_vehicle_id());
        if (!vehicle)
        {
            spdlog::warn("Attempt to enter invalid vehicle {:x} from connection {:x}", aMessage.get_remote_vehicle_id(), aMessage.ConnectionId);
            return;
        }

        spdlog::info("Player {:x} entered vehicle {:x}", aMessage.get_id(), vehicle.id());
    }
    else
    {
        glm::vec3 pos = {aMessage.get_position().get_x(), aMessage.get_position().get_y(), aMessage.get_position().get_z()};
        glm::vec3 rot = {0.f, 0.f, aMessage.get_rotation()};

        if (IsDebug())
        {
            pos += glm::vec3(5, 0, 0);
        }

        vehicle = GetWorld()->entity().child_of(player).set<MovementComponent>({pos, rot, {}}).set<VehicleComponent>({aMessage.get_vehicle_id()});

        spdlog::info("Player {:x} spawned and entered vehicle {:x}", aMessage.get_id(), vehicle.id());
    }

    // One person per seat.
    //
    // Nothing checked this. Each client decides its own seat locally, and two people
    // getting into the same car from opposite sides can both report seat_front_left -
    // whereupon the server replicated two puppets into one seat and they rendered inside
    // each other. That is what "four people cannot fit in a four-door car" looks like from
    // the inside: the seats are there, they were just all being asked for at once.
    //
    // Refused rather than silently reassigned. Being told the seat is taken is something a
    // player can act on; being moved somewhere they did not choose is not.
    const auto requestedSeat = aMessage.get_sit_id();
    bool seatTaken = false;

    GetWorld()->each(
        [vehicle, requestedSeat, target, &seatTaken](flecs::entity aOccupant, const AttachmentComponent& aAttachment)
        {
            if (aOccupant == target)
                return;

            if (aAttachment.Parent == vehicle && aAttachment.SlotId == requestedSeat)
                seatTaken = true;
        });

    if (seatTaken)
    {
        spdlog::info("Player {:x} tried to take an occupied seat in vehicle {:x}", aMessage.get_id(), vehicle.id());

        // Told, then left alone. The client is already sitting there locally, so saying
        // nothing would leave them looking at a seat the server disagrees about.
        if (auto* pChat = GetWorld()->get_mut<ChatSystem>())
            pChat->Tell(*pPlayer, "Someone is already in that seat - try another door.");

        return;
    }

    target.set<AttachmentComponent>({vehicle, requestedSeat});
}

void Level::HandleExitVehicleRequest(PacketEvent<client::ExitVehicleRequest>& aMessage) noexcept
{
    flecs::entity target(GetWorld()->get_world(), aMessage.get_id());

    if (!target)
    {
        spdlog::warn("Attempt to exit vehicle from invalid entity from connection {:x}", aMessage.ConnectionId);
        return;
    }

    const auto player = target.parent();
    if (!player)
    {
        spdlog::warn("Attempt to exit vehicle an entity without an owner from connection {:x}", aMessage.ConnectionId);
        return;
    }

    auto* pPlayer = player.get<PlayerComponent>();
    if (!pPlayer)
    {
        spdlog::warn("The entity's owner is not a player! From connection {:x}", aMessage.ConnectionId);
        return;
    }

    if (pPlayer->Connection != aMessage.ConnectionId)
    {
        spdlog::warn("The entity's owner is not the current player! From connection {:x}", aMessage.ConnectionId);
        return;
    }

    // Which vehicle they were in, before we forget.
    flecs::entity vehicle;
    if (const auto* pAttachment = target.get<AttachmentComponent>())
        vehicle = pAttachment->Parent;

    // Start interpolation again
    target.remove<AttachmentComponent>();

    ReleaseVehicleIfEmpty(vehicle);
}

// Destroys a network vehicle once nobody is sitting in it.
//
// Nothing used to. HandleEnterVehicleRequest creates a fresh entity for every car a
// player gets into, so every entry told every other client to spawn another one, and
// nothing ever told them to take it away. Get in and out of the same car three times and
// everyone else has three copies of it stacked in the road - which is both the "it
// duplicates it underneath the car" report and, with each copy carrying full physics, a
// large part of why frames collapse while driving. One test session left seven.
//
// Passengers matter: the driver leaving must not delete a car with someone still in the
// back, so this counts occupants rather than assuming.
void Level::ReleaseVehicleIfEmpty(flecs::entity aVehicle) noexcept
{
    if (!aVehicle || !aVehicle.is_alive())
        return;

    if (!aVehicle.has<VehicleComponent>())
        return;

    bool occupied = false;

    GetWorld()->each(
        [aVehicle, &occupied](flecs::entity, const AttachmentComponent& aAttachment)
        {
            if (aAttachment.Parent == aVehicle)
                occupied = true;
        });

    if (occupied)
        return;

    Remove(aVehicle);
}

server::NotifyCharacterLoad Level::Serialize(flecs::entity aEntity) noexcept
{
    server::NotifyCharacterLoad message;

    if (auto* pMovementComponent = aEntity.get<MovementComponent>())
    {
        common::Vector3 pos;
        pos.set_x(pMovementComponent->Position.x);
        pos.set_y(pMovementComponent->Position.y);
        pos.set_z(pMovementComponent->Position.z);

        message.set_position(pos);
        message.set_rotation(pMovementComponent->Rotation.z);
    }

    if (auto* pCharacterComponent = aEntity.get<CharacterComponent>())
    {
        message.set_is_player(pCharacterComponent->IsPlayerPuppet);
    }

    if (auto* pAppearanceComponent = aEntity.get<AppearanceComponent>())
    {
        message.set_equipment(pAppearanceComponent->equipment);
        message.set_ccstate(pAppearanceComponent->ccstate);
    }

    // The name lives on the PLAYER, not the puppet - the puppet is a child entity with no
    // idea who owns it. Server-side and Discord-derived, so it cannot be spoofed by a
    // client claiming to be someone else.
    if (const auto owner = aEntity.parent())
    {
        if (const auto* pPlayerComponent = owner.get<PlayerComponent>())
        {
            message.set_username(pPlayerComponent->Username.c_str());
        }
    }

    message.set_id(aEntity);

    return message;
}

void Level::TransferCell(flecs::entity aEntity, GridCell* apOldCell, GridCell* apNewCell) noexcept
{
    Set<GridCell*> cellsToLoad;
    Set<GridCell*> cellsToUnload;

    apOldCell->Remove(aEntity);

    CollectCells(apNewCell, apOldCell, cellsToLoad, cellsToUnload);

    {
        server::NotifyCharacterLoad message;
        message.set_id(aEntity.id());

        for (const auto pCell : cellsToLoad)
        {
        //    pCell->ForEach([&message](flecs::entity aEntity) { apPlayer->Send(message); });
        }
    }

    {
        server::NotifyEntityUnload message;
        message.set_id(aEntity.id());

        //for (const auto pCell : cellsToUnload)
        {
        //    pCell->ForEachPlayer([&message](const Player* apPlayer) { apPlayer->Send(message); });
        }
    }

    apNewCell->Add(aEntity);

    if (apOldCell->Count() == 0)
        m_cells.erase(apOldCell->GetPosition());
}


/*void Level::SendToRelevant(Player* apCharacter, const ServerMessage& acMessage)
{
    ForEachInRange(
        apCharacter->GetCell(), sCellLoadRadius,
        [&acMessage, apCharacter](const Player* apPlayer)
        {
#ifndef DEBUG
            if (apCharacter == apPlayer)
                return;
#endif
            apPlayer->Send(acMessage);
        });
}*/

gsl::not_null<GridCell*> Level::GetCell(const GridCell::TPosition aPosition) noexcept
{
    auto itor = m_cells.find(aPosition);
    if (itor == std::end(m_cells))
    {
        const auto insertRes = m_cells.emplace(aPosition, MakeUnique<GridCell>(aPosition));
        itor = insertRes.first;
    }

    return itor.value().get();
}

void Level::CollectCells(const GridCell* apNewCell, const GridCell* apOldCell, Set<GridCell*>& aToLoad, Set<GridCell*>& aToUnload) noexcept
{
    // Gather all cells we need loaded
    ForEachCellInRange(apNewCell, sCellLoadRadius, [&aToLoad](GridCell* apCell) { aToLoad.insert(apCell); });

    // Remove all cells already loaded from old cell
    ForEachCellInRange(
        apOldCell, sCellLoadRadius,
        [&aToLoad, &aToUnload](GridCell* apCell)
        {
            if (!aToLoad.contains(apCell))
            {
                aToUnload.insert(apCell);
            }
            else
                aToLoad.erase(apCell);
        });
}

void Level::Test() noexcept
{
    for (auto i = -10; i < 10; ++i)
        for (auto j = -10; j < 10; ++j)
            TP_UNUSED(GetCell(GridCell::TPosition(i, j)));

    auto pCell = GetCell(GridCell::TPosition(0, 0));
    assert(pCell == GetCell(GridCell::TPosition(0, 0)));
    assert(pCell != GetCell(GridCell::TPosition(1, 0)));
}
