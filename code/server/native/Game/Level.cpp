#include "Level.h"

#include <Components/LevelTag.h>
#include <Components/MovementComponent.h>
#include <Components/PlayerComponent.h>
#include <Components/AppearanceComponent.h>
#include <Components/AttachmentComponent.h>
#include "Components/CellComponent.h"
#include <Components/CharacterComponent.h>
#include <Components/VehicleComponent.h>
#include <Components/AuthorityComponent.h>
#include <Components/NpcComponent.h>

#include "GameServer.h"
#include "World.h"
#include "PlayerManager.h"
#include "WorldClock.h"
#include "Systems/ChatSystem.h"   // telling someone their seat is taken
#include "Validation.h"           // sanity checks on anything a client sent

#include <chrono>

constexpr static float sCellSize = 60 * 100;
constexpr static int16_t sCellLoadRadius = 3;
constexpr static int16_t sCellUnloadRadius = 4;

// An empty vehicle awaiting teardown.
//
// NOTHING SETS THIS ANY MORE, and the system that consumes it therefore never fires.
// Kept, rather than deleted, because it is the shape an abandoned-vehicle cleanup policy
// will want: a deadline per entity and a sweep that acts when it passes.
//
// It became unused when ReleaseVehicleIfEmpty stopped destroying empty vehicles and began
// parking them instead - a car whose driver got out is a parked car, not litter. The delay
// existed to absorb a seat swap, which is an exit and an enter a millisecond apart, where
// destroying at the exit deleted the car out from under the re-enter with its simulator
// still driving it. Parking is reversible, so that race no longer needs absorbing.
//
// Anyone reviving this: the trigger for removing a car should be a real lifecycle rule -
// stored, destroyed, owner reclaimed it, idle for hours - and never "the driver got out".
struct PendingReleaseComponent
{
    std::chrono::steady_clock::time_point At;
};

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
    GServer->RegisterHandler<&Level::HandleUpdateAppearanceRequest>(this);
    GServer->RegisterHandler<&Level::HandleVoiceFrameRequest>(this);

    m_updateSystem = m_pWorld->system<const LevelActorTag>("Level Update")
        .each([this](flecs::entity aEntity, const LevelActorTag&)
        {
            Update(aEntity);
        });

    m_updateSystem.child_of(m_pWorld->entity("systems"));

    // Sweeps vehicles whose teardown grace expired. Occupancy is re-checked at the
    // deadline because an enter can arrive by a path that never touches the tag.
    m_releaseSystem = m_pWorld->system<const PendingReleaseComponent>("Vehicle release grace")
        .each([this](flecs::entity aVehicle, const PendingReleaseComponent& aPending)
        {
            if (std::chrono::steady_clock::now() < aPending.At)
                return;

            bool occupied = false;

            GetWorld()->each(
                [aVehicle, &occupied](flecs::entity, const AttachmentComponent& aAttachment)
                {
                    if (aAttachment.Parent == aVehicle)
                        occupied = true;
                });

            if (occupied)
            {
                aVehicle.remove<PendingReleaseComponent>();
                return;
            }

            Remove(aVehicle);
        });

    m_releaseSystem.child_of(m_pWorld->entity("systems"));
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

    // Vehicles are the exception to skipping the owner: an ADOPTED car's owner holds a
    // network mirror (and engine copy) of it, and a spawned car's owner holds a memory
    // pairing its ids - without the unload, both linger as the ghost cars players kept
    // trying to board. Puppets keep the skip; a client must not be told to unload its
    // own character.
    const bool includeOwner = aEntity.has<AuthorityComponent>();

    GetWorld()->each([this, &unload, owner, includeOwner](flecs::entity aEntity, const PlayerComponent& aPlayerComponent)
    {
        if (!IsDebug() && owner == aEntity && !includeOwner)
            return;

        GServer->Send(aPlayerComponent.Connection, unload);
    });

    // The owner is skipped by the unload broadcast, so when the server destroys an
    // entity whose simulator still believes they are driving it, nothing told their
    // client to stop. That silence was a 512-packet rejected-move flood in one session.
    // An explicit revoke runs the client's existing stop-simulating path.
    if (const auto* pAuthority = aEntity.get<AuthorityComponent>(); pAuthority && owner)
    {
        if (const auto* pPlayer = owner.get<PlayerComponent>())
        {
            server::NotifyAuthorityRevoked revoked;
            revoked.set_entity_id(aEntity);
            revoked.set_epoch(pAuthority->Epoch);

            GServer->Send(pPlayer->Connection, revoked);
        }
    }

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

    // The world's clock and sky, before anything stands in the world - so the city a
    // player loads into is already showing the same hour everyone else is living in.
    if (auto* pClock = GetWorld()->get_mut<WorldClock>())
        pClock->SendTo(pPlayerComponent->Connection);

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

// The occupant who should inherit a departing simulator's car, front passenger first.
//
// Excludes everyone belonging to the departing PLAYER (their puppet may still be marked
// as sitting in the driver seat at disconnect time). The hashes are CNames - FNV1a64 of
// the seat names; the algorithm is verified because the front-left constant the seat
// guard has used all along reproduces exactly from FNV1a64("seat_front_left"). A seat
// not in the list (a bike pillion, a future vehicle) still counts as a fallback - an
// arbitrary simulator beats none.
static flecs::entity NextOccupant(flecs::entity aVehicle, flecs::entity aDepartingPlayer) noexcept
{
    constexpr uint64_t cSeatPriority[] = {
        0x63c846db887c0035ULL, // seat_front_right
        0xb06da35221954b3eULL, // seat_back_left
        0xc90fa7831f484433ULL, // seat_back_right
    };
    constexpr size_t cSeatCount = sizeof(cSeatPriority) / sizeof(cSeatPriority[0]);

    flecs::entity bySeat[cSeatCount]{};
    flecs::entity fallback{};

    aVehicle.world().each(
        [&](flecs::entity aOccupant, const AttachmentComponent& aAttachment)
        {
            if (aAttachment.Parent != aVehicle || aOccupant.parent() == aDepartingPlayer)
                return;

            for (size_t i = 0; i < cSeatCount; ++i)
            {
                if (aAttachment.SlotId == cSeatPriority[i])
                {
                    bySeat[i] = aOccupant;
                    return;
                }
            }

            if (!fallback)
                fallback = aOccupant;
        });

    for (const auto& occupant : bySeat)
        if (occupant)
            return occupant;

    return fallback;
}

// Takes a leaving player's cars with them - unless somebody is still inside.
//
// Vehicles are created as children of whoever is driving, so flecs destroys them when the
// player entity goes - silently. Everyone else's client had been told to spawn a copy and
// is never told to remove it, so every disconnect while driving left a permanent
// abandoned car in the other players' worlds. Routing it through Remove() sends the
// unload first.
//
// A passenger changes the answer entirely: deleting the car around them is worse than
// any alternative, so the car is handed to them instead and lives on.
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
    {
        if (const auto next = NextOccupant(vehicle, aPlayer))
        {
            PromoteToDriver(vehicle, next.parent());
            continue;
        }

        // Nobody left inside, so park it rather than delete it.
        //
        // A player's connection dropping is not a reason for their car to stop existing.
        // It used to be: the vehicle was a child of the player entity, so losing the player
        // took the car with it - somebody's parked car evaporating out from under the
        // people standing next to it because its last driver alt-tabbed and timed out.
        //
        // Same parking as the exit path. The car keeps its network id, its position, and
        // its place on every client; the only thing it loses is a simulator, which is
        // correct, because the machine that was simulating it has gone.
        TransferAuthority(vehicle, flecs::entity::null());
    }
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
    // A player the server has never seen starts where the server says, not where the world
    // template happens to leave V standing.
    //
    // Checked BEFORE the saved-position branch below, and only when there is no record at
    // all - a returning player is put back where they were, which is the whole point of
    // the position store. Getting this order wrong would teleport everybody to the
    // arrivals point on every single join.
    glm::vec3 startPosition;
    float startYaw = 0.f;

    // "Brand-new" means a brand-new CHARACTER, not an account nobody has seen.
    //
    // This is why /setstart appeared to do nothing. It tested whether the account had a
    // PlayerRecord, and a record is created the moment anything about a player is stored -
    // so by the time anyone reached this line they already had one, the condition was
    // false, and the arrivals point was skipped every single time. It could only ever have
    // fired for an account that had never been stored at all.
    //
    // Worse, it was the wrong question anyway: somebody who replaces their character with
    // a new one is exactly who the arrivals point is for, and they always have a record.
    //
    // The character's own flag answers it properly, and a replacement character defaults
    // to false so it is sent to the start point without anything having to reset it.
    const auto* pExistingCharacter = GServer->GetPlayerStore().FindCharacter(pComponent->DiscordId);
    const bool isNewHere = (pExistingCharacter == nullptr) || !pExistingCharacter->SpawnedBefore;

    bool placedAtStart = false;

    if (isNewHere && GServer->GetStartPoint(startPosition, startYaw))
    {
        placedAtStart = true;

        pos = startPosition;
        rot = glm::vec3(0.f, 0.f, startYaw);

        server::NotifyTeleport teleport;

        common::Vector3 destination;
        destination.set_x(startPosition.x);
        destination.set_y(startPosition.y);
        destination.set_z(startPosition.z);
        teleport.set_position(destination);
        teleport.set_rotation(startYaw);

        GServer->Send(aMessage.ConnectionId, teleport);

        spdlog::info("New arrival {} placed at the start point ({:.1f}, {:.1f}, {:.1f})",
                     pComponent->Username, startPosition.x, startPosition.y, startPosition.z);

        // Recorded straight away, so this happens once per character rather than on every
        // join. Written through with the rest of the character.
        if (pExistingCharacter)
        {
            auto updated = *pExistingCharacter;
            updated.SpawnedBefore = true;
            GServer->GetPlayerStore().SaveCharacter(pComponent->DiscordId, pComponent->Username, updated);
        }
    }

    // Skipped entirely when they were just placed at the arrivals point.
    //
    // Both branches teleport, and this one runs second - so without the guard it would
    // immediately drag a new arrival back to the last position their account happened to
    // have stored, undoing the placement above and leaving no trace of why.
    const auto* pSaved = placedAtStart ? nullptr
                                       : GServer->GetPlayerStore().Find(pComponent->DiscordId);
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

    // The server's character wins over whatever the client's save happens to contain.
    //
    // This is the line that ends the dependency on singleplayer saves. The client still
    // loads A save, because Cyberpunk has no way to build a world without one - but which
    // save that is stops mattering, because the appearance everyone SEES comes from here.
    // The save becomes a world template rather than anybody's identity.
    //
    // A player with no character yet keeps what they arrived with, so this is inert until
    // the creator exists and nothing breaks in the meantime.
    Vector<uint8_t> appearance = ccstate;

    if (const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(pComponent->DiscordId))
    {
        if (!pCharacter->Appearance.empty())
        {
            const auto stored = Base64::Decode(pCharacter->Appearance);

            // A blob that decodes to nothing means the record is corrupt. Falling back to
            // what the client sent gets them into the world looking wrong, which beats
            // refusing to spawn them at all.
            if (!stored.empty())
            {
                appearance.assign(stored.begin(), stored.end());

                spdlog::info("Spawned {} as their saved character '{}' ({} bytes of appearance)",
                             pComponent->Username,
                             pCharacter->Name.empty() ? "unnamed" : pCharacter->Name,
                             stored.size());
            }
            else
            {
                spdlog::warn("Stored appearance for {} did not decode - using the client's",
                             pComponent->Username);
            }
        }

        // Ask on spawn if they still have not chosen a name.
        //
        // Asking only after the creator missed everybody who already had a character,
        // which was every existing player - their character was made before the prompt
        // existed, so the one moment that triggers it had already passed for them. This
        // is the route that reaches them, and it stops as soon as they answer.
        if (!pCharacter->NameChosen)
        {
            if (auto* pChat = GetWorld()->get_mut<ChatSystem>())
                pChat->AskForCharacterName(*pComponent, *pCharacter);
        }
    }

    pComponent->Puppet = GetWorld()->entity()
        .child_of(player)
        .set<MovementComponent>({pos, rot, {}})
        .set<AppearanceComponent>({equipment, appearance});

    // The one line that says a person actually arrived in the world. Vehicle spawns log
    // similar-sounding lines and have been misread as player spawns during diagnosis;
    // this one names the human.
    spdlog::info("Puppet {:x} spawned for {} (connection {:x})",
                 pComponent->Puppet.id(), pComponent->Username, aMessage.ConnectionId);

    // Somebody with no character is told, once, on arrival.
    //
    // The server is the only side that knows whether they have one, and a player who is
    // silently playing as whoever the world template contains has no way to find out that
    // is not who they are meant to be. Being asked is the difference between a system that
    // exists and a system anybody uses.
    if (!GServer->GetPlayerStore().HasCharacter(pComponent->DiscordId))
    {
        // Ask for whatever they are wearing right now, and keep it as their character.
        //
        // This is what makes MULTIPLAYER - NEW CHARACTER work. That flow runs the game's
        // creator BEFORE the world exists and before anyone connects, so the client's
        // appearance watcher - which only runs while connected - never sees the session at
        // all. Somebody could spend ten minutes building a face and have none of it kept.
        //
        // Asking on first spawn catches it: by now they are standing in the world as
        // whoever they just made. The reply carries the body gender too, which the server
        // cannot read out of the appearance blob itself.
        server::OpenCharacterCreator capture;
        capture.set_capture_only(true);
        GServer->Send(aMessage.ConnectionId, capture);

        if (auto* pChat = GetWorld()->get_mut<ChatSystem>())
        {
            pChat->Tell(*pComponent, "Welcome. This is now your character - the server will remember you.");
            pChat->Tell(*pComponent, "Choose what you are called with /name <name>.");
            pChat->Tell(*pComponent, "Any ripperdoc can change how you look later, and it saves by itself.");
        }

        spdlog::info("{} has no character yet - capturing the one they arrived as", pComponent->Username);
    }

    // Hand back what this character owns.
    //
    // Sent with the spawn rather than afterwards, so there is no window in which somebody
    // is standing in the world holding whatever their local save gave them. Their save
    // decides nothing about their possessions any more; this does.
    //
    // has_possessions is the important flag. An empty inventory is ambiguous - it means
    // either "this character owns nothing" or "the server has never been told what they
    // own", and the two demand opposite behaviour. Applying an empty record to a character
    // created before possessions were stored would empty their pockets on their next
    // login, which is a far worse failure than a character keeping a save's contents for
    // one more session.
    if (const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(pComponent->DiscordId))
    {
        const bool known = !pCharacter->Inventory.empty() || pCharacter->Money > 0;

        if (known)
        {
            // Built as a vector and set in one go - the generator here is netpack, not
            // protobuf, so there is no add_inventory() to append with.
            Vector<server::ItemStack> stacks;
            stacks.reserve(pCharacter->Inventory.size());

            for (const auto& stack : pCharacter->Inventory)
            {
                server::ItemStack entry;
                entry.set_id(stack.Id);
                entry.set_quantity(stack.Quantity);
                stacks.push_back(entry);
            }

            response.set_inventory(stacks);
            response.set_money(pCharacter->Money);
        }

        response.set_has_possessions(known);

        if (!pCharacter->Proficiencies.empty())
        {
            Vector<server::Proficiency> profs;
            profs.reserve(pCharacter->Proficiencies.size());

            for (const auto& prof : pCharacter->Proficiencies)
            {
                server::Proficiency entry;
                entry.set_type(prof.Type);
                entry.set_level(prof.Level);
                profs.push_back(entry);
            }

            response.set_proficiencies(profs);
        }

        if (!pCharacter->Attributes.empty())
        {
            Vector<server::Attribute> attrs;
            attrs.reserve(pCharacter->Attributes.size());

            for (const auto& a : pCharacter->Attributes)
            {
                server::Attribute entry;
                entry.set_type(a.Type);
                entry.set_value(a.Value);
                attrs.push_back(entry);
            }

            response.set_attributes(attrs);
        }

        if (!pCharacter->Perks.empty())
        {
            Vector<server::Perk> perks;
            perks.reserve(pCharacter->Perks.size());

            for (const auto& k : pCharacter->Perks)
            {
                server::Perk entry;
                entry.set_type(k.Type);
                entry.set_level(k.Level);
                perks.push_back(entry);
            }

            response.set_perks(perks);
        }

        // What appears in this player's phone.
        //
        // Derived from the vehicles they OWN, not from what their client reported having
        // unlocked. The client's own garage is a consequence of ownership here, never a
        // source of it - otherwise "which cars do I have" would be answered by the machine
        // most motivated to lie about it.
        //
        // Models rather than instances, because the phone menu is a list of models and
        // cannot be anything else. Owning three Quadras puts one Quadra in the menu; which
        // particular one arrives is the server's business, and /garage is where the
        // difference between them is visible.
        {
            Vector<String> models;

            for (const auto& vehicle : GServer->GetVehicles().OwnedBy(pComponent->DiscordId))
            {
                if (vehicle.ModelName.empty())
                    continue;

                const auto already = std::any_of(models.begin(), models.end(),
                                                 [&vehicle](const String& acName)
                                                 { return acName == vehicle.ModelName.c_str(); });

                if (!already)
                    models.push_back(String(vehicle.ModelName.c_str()));
            }

            if (!models.empty())
            {
                response.set_vehicles(models);
                spdlog::info("{} owns {} vehicle model(s) - unlocking them in their phone",
                             pComponent->Username, models.size());
            }
        }

        spdlog::info("{} spawns with {} stored item stack(s) and {} eddies{}",
                     pComponent->Username, pCharacter->Inventory.size(), pCharacter->Money,
                     known ? "" : " - nothing stored yet, their save keeps what it has");
    }

    // The server's version of which doors are open.
    //
    // Sent to everybody, not just to characters with stored possessions - an unlocked
    // building is a property of the world rather than of a character, and a new player
    // should walk into the same city as everyone else.
    {
        const auto& facts = GServer->GetWorldFacts().All();

        if (!facts.empty())
        {
            Vector<server::WorldFact> wire;
            wire.reserve(facts.size());

            for (const auto& fact : facts)
            {
                server::WorldFact entry;
                entry.set_name(fact.Name.c_str());
                entry.set_value(fact.Value);
                wire.push_back(entry);
            }

            response.set_facts(wire);
        }
    }

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
        // Every observed desync on the container deployment funnels through this branch,
        // so it says everything it knows: which id the client sent (generation bits
        // included), whether that entity is alive, and what the parent actually is.
        spdlog::warn("The entity's owner is not a player! move id={:#x} alive={} target='{}' parent id={:#x} parent='{}' from connection {:x}",
                     aMessage.get_id(), target.is_alive(), target.name().c_str(),
                     player.raw_id(), player.name().c_str(), aMessage.ConnectionId);
        return;
    }

    if (pPlayer->Connection != aMessage.ConnectionId)
    {
        spdlog::warn("The entity's owner is not the current player! From connection {:x}", aMessage.ConnectionId);
        return;
    }

    // Stale-authority guard. Movement is unreliable and unordered across a handoff, so
    // the PREVIOUS simulator's packets can arrive after a transfer. The epoch names the
    // grant; a mismatch is dropped silently - that race is expected after every handoff,
    // not an attack, and a warning per stale packet would be pure noise. Entities that
    // never change hands (player puppets) carry no AuthorityComponent and skip this.
    if (const auto* pAuthority = target.get<AuthorityComponent>())
    {
        if (aMessage.get_epoch() != pAuthority->Epoch)
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
    component.Locomotion = aMessage.get_locomotion();
    component.UpperBody = aMessage.get_upper_body();

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

void Level::PromoteToDriver(flecs::entity aVehicle, flecs::entity aPlayer) noexcept
{
    if (!aVehicle || !aVehicle.is_alive() || !aPlayer || !aPlayer.is_alive())
        return;

    TransferAuthority(aVehicle, aPlayer);

    const auto* pPlayer = aPlayer.get<PlayerComponent>();
    if (!pPlayer)
        return;

    // The server picks the driver, never the clients.
    //
    // Every client can see who is in the car, so every client could work out a new driver
    // for itself - and they would not always agree, which is two people at one wheel. The
    // choice is made once here, by seat priority, and the chosen client is told.
    server::NotifyVehicleControlAssigned message;
    message.set_vehicle_id(aVehicle.id());

    GServer->Send(pPlayer->Connection, message);

    spdlog::info("{} promoted to driver of vehicle {:x}", pPlayer->Username, aVehicle.id());
}

void Level::BroadcastAppearance(flecs::entity aPuppet) noexcept
{
    if (!aPuppet || !aPuppet.is_alive())
        return;

    const auto* pAppearance = aPuppet.get<AppearanceComponent>();
    if (!pAppearance)
        return;

    const auto owner = aPuppet.parent();

    server::NotifyAppearanceUpdate message;
    message.set_id(aPuppet.id());
    message.set_equipment(pAppearance->equipment);
    message.set_ccstate(pAppearance->ccstate);

    // Everyone except the person it is about. Their own game already shows what they are
    // wearing - it is where the change came from - and echoing it back invites the client
    // to re-apply an appearance to the local player, which is a different code path with
    // its own hazards.
    GetWorld()->get_world().each(
        [&message, owner](flecs::entity player, const PlayerComponent& aPlayerComponent)
        {
            if (player == owner)
                return;

            GServer->Send(aPlayerComponent.Connection, message);
        });
}

void Level::HandleVoiceFrameRequest(PacketEvent<client::VoiceFrameRequest>& aMessage) noexcept
{
    auto* pPlayerManager = GetWorld()->get_mut<PlayerManager>();

    // The speaker comes from the CONNECTION, exactly as the appearance path does. There is
    // no id field on this request, so "play this audio as me" cannot be spelled "play it as
    // somebody else" - impersonation over voice is not a bug anyone should have to notice.
    const auto speaker = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!speaker)
        return;

    const auto* pSpeaker = speaker.get<PlayerComponent>();
    if (!pSpeaker || !pSpeaker->Puppet || !pSpeaker->Puppet.is_alive())
        return;

    // A 20ms Opus frame at any sane bitrate is well under 200 bytes; 1KB is generous and
    // still bounds what one connection can make the server relay to everybody near them.
    // Empty frames are dropped rather than forwarded - silence is the absence of frames.
    constexpr size_t kMaxFrame = 1024;

    if (aMessage.get_data().empty() || aMessage.get_data().size() > kMaxFrame)
        return;

    const auto* pSpeakerMovement = pSpeaker->Puppet.get<MovementComponent>();
    if (!pSpeakerMovement)
        return;

    // What the range values MEAN, decided here rather than by the client.
    //
    // The client sends an intent - whisper, local, yell - and the server turns it into
    // metres. A client that invents a range of 9 gets the default, not the horizon.
    float radius;

    switch (aMessage.get_range())
    {
    case 0: radius = 6.f; break;   // whisper - the people immediately around you
    case 2: radius = 60.f; break;  // yell - across a junction
    default: radius = 25.f; break; // local - normal speech, and the fall-back
    }

    const float radiusSquared = radius * radius;
    const auto speakerPosition = pSpeakerMovement->Position;

    server::NotifyVoiceFrame message;
    message.set_id(pSpeaker->Puppet.id());
    message.set_data(aMessage.get_data());
    message.set_sequence(aMessage.get_sequence());

    GetWorld()->get_world().each(
        [&message, speaker, speakerPosition, radiusSquared](flecs::entity player,
                                                            const PlayerComponent& aPlayerComponent)
        {
            // Never echo somebody their own voice. Hearing yourself a ping later is the
            // single most disorienting thing a voice system can do.
            if (player == speaker)
                return;

            if (!aPlayerComponent.Puppet || !aPlayerComponent.Puppet.is_alive())
                return;

            const auto* pMovement = aPlayerComponent.Puppet.get<MovementComponent>();
            if (!pMovement)
                return;

            // Squared distance - no square root, and this runs per listener per frame at
            // 50 frames a second per speaker.
            const auto delta = pMovement->Position - speakerPosition;

            if (glm::dot(delta, delta) > radiusSquared)
                return;

            GServer->Send(aPlayerComponent.Connection, message);
        });
}

void Level::HandleUpdateAppearanceRequest(PacketEvent<client::UpdateAppearanceRequest>& aMessage) noexcept
{
    auto* pPlayerManager = GetWorld()->get_mut<PlayerManager>();

    const auto player = pPlayerManager->GetByConnectionId(aMessage.ConnectionId);
    if (!player)
        return;

    auto* pComponent = player.get_mut<PlayerComponent>();
    if (!pComponent || !pComponent->Puppet || !pComponent->Puppet.is_alive())
        return;

    // The puppet comes from the CONNECTION, never from the message.
    //
    // There is no id field on this request precisely so that "change my clothes" cannot be
    // spelled "change theirs". The authenticated connection decides whose appearance this
    // is, and a client that wants to dress somebody else has nowhere to say so.
    const auto puppet = pComponent->Puppet;

    auto* pAppearance = puppet.get_mut<AppearanceComponent>();
    if (!pAppearance)
        return;

    // Same bounds the spawn path uses, for the same reason - this is relayed to every other
    // client, so an unbounded blob is a way to make one wardrobe visit allocate arbitrary
    // memory everywhere.
    constexpr size_t kMaxCcstate = 256 * 1024;
    constexpr size_t kMaxEquipment = 64;

    if (aMessage.get_ccstate().size() > kMaxCcstate || aMessage.get_equipment().size() > kMaxEquipment)
    {
        spdlog::warn("Refused an appearance update from {} - {} bytes, {} item(s)",
                     pComponent->Username, aMessage.get_ccstate().size(),
                     aMessage.get_equipment().size());
        return;
    }

    bool changed = false;

    if (aMessage.get_equipment() != pAppearance->equipment)
    {
        pAppearance->equipment = aMessage.get_equipment();
        changed = true;
    }

    // Only when it is plausible AND actually sent.
    //
    // Absence means "clothing only", which is most updates - clearing the stored face
    // because a jacket changed would undo a ripperdoc visit every time somebody got
    // dressed. Too small means the customization state was read before it was populated,
    // which is the 23-byte case that has already reached storage once.
    constexpr size_t kMinCcstate = 1024;

    if (aMessage.get_ccstate().size() >= kMinCcstate && aMessage.get_ccstate() != pAppearance->ccstate)
    {
        pAppearance->ccstate = aMessage.get_ccstate();
        changed = true;
    }

    if (!changed)
        return;

    puppet.modified<AppearanceComponent>();

    spdlog::info("{} changed appearance - {} item(s), {} bytes", pComponent->Username,
                 pAppearance->equipment.size(), pAppearance->ccstate.size());

    BroadcastAppearance(puppet);
}

void Level::HandleEnterVehicleRequest(PacketEvent<client::EnterVehicleRequest>& aMessage) noexcept
{
    flecs::entity target(GetWorld()->get_world(), aMessage.get_id());

    // These rejections say everything they know - which id the client sent (generation
    // bits included), whether that entity is alive, and what the parent actually is -
    // the same shape the movement rejection took when every observed desync turned out
    // to funnel through a warn that named nothing.
    if (!target)
    {
        spdlog::warn("Attempt to enter vehicle from invalid entity id={:#x} from connection {:x}",
                     aMessage.get_id(), aMessage.ConnectionId);
        return;
    }

    const auto player = target.parent();
    if (!player)
    {
        spdlog::warn("Attempt to enter vehicle from an entity without an owner! enter id={:#x} alive={} target='{}' from connection {:x}",
                     aMessage.get_id(), target.is_alive(), target.name().c_str(), aMessage.ConnectionId);
        return;
    }

    auto* pPlayer = player.get<PlayerComponent>();
    if (!pPlayer)
    {
        spdlog::warn("The entity's owner is not a player! enter id={:#x} alive={} target='{}' parent id={:#x} parent='{}' from connection {:x}",
                     aMessage.get_id(), target.is_alive(), target.name().c_str(),
                     player.raw_id(), player.name().c_str(), aMessage.ConnectionId);
        return;
    }

    if (pPlayer->Connection != aMessage.ConnectionId)
    {
        spdlog::warn("The entity's owner is not the current player! enter id={:#x} owned by '{}' on connection {:x}, sent from connection {:x}",
                     aMessage.get_id(), pPlayer->Username, pPlayer->Connection, aMessage.ConnectionId);
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

        // Someone is boarding - any scheduled teardown is off.
        vehicle.remove<PendingReleaseComponent>();
    }
    else
    {
        // Position-spawning a car into a PASSENGER seat is never intent - it means the
        // client failed to resolve a car it can see (its network copy just unloaded, or
        // never loaded). Honoring it forks a split-brain duplicate: one player driving
        // the real entity, the other sitting in a private copy nobody else can see.
        // Refused the same way a taken seat is refused - told, then left alone.
        if (aMessage.get_sit_id() != 0xb000b1d029d0cea0ULL) // seat_front_left
        {
            spdlog::warn("Player {:x} tried to spawn a vehicle into seat {:x} - refused as a desync fork (connection {:x})",
                         aMessage.get_id(), aMessage.get_sit_id(), aMessage.ConnectionId);

            if (auto* pChat = GetWorld()->get_mut<ChatSystem>())
                pChat->Tell(*pPlayer, "Couldn't sync that car - give it a moment, or take the driver's seat.");

            return;
        }

        glm::vec3 pos = {aMessage.get_position().get_x(), aMessage.get_position().get_y(), aMessage.get_position().get_z()};
        glm::vec3 rot = {0.f, 0.f, aMessage.get_rotation()};

        if (IsDebug())
        {
            pos += glm::vec3(5, 0, 0);
        }

        // AuthorityComponent from birth: the creator is the first simulator (they are the
        // parent, and their client's movement arrives with epoch 0, which matches). Every
        // later change of hands goes through TransferAuthority.
        vehicle = GetWorld()->entity().child_of(player).set<MovementComponent>({pos, rot, {}}).set<VehicleComponent>({aMessage.get_vehicle_id()}).set<AuthorityComponent>({});

        // Named "Vehicle ... spawned", not "Player ... spawned" - this line kept being
        // misread as a player spawn in log forensics.
        spdlog::info("Vehicle {:x} spawned by character {:x} (driver seat)", vehicle.id(), aMessage.get_id());
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

    // Same informative shape as the enter and movement rejections: the id as sent,
    // liveness, and the actual parent, so a live desync names itself in one line.
    if (!target)
    {
        spdlog::warn("Attempt to exit vehicle from invalid entity id={:#x} from connection {:x}",
                     aMessage.get_id(), aMessage.ConnectionId);
        return;
    }

    const auto player = target.parent();
    if (!player)
    {
        spdlog::warn("Attempt to exit vehicle from an entity without an owner! exit id={:#x} alive={} target='{}' from connection {:x}",
                     aMessage.get_id(), target.is_alive(), target.name().c_str(), aMessage.ConnectionId);
        return;
    }

    auto* pPlayer = player.get<PlayerComponent>();
    if (!pPlayer)
    {
        spdlog::warn("The entity's owner is not a player! exit id={:#x} alive={} target='{}' parent id={:#x} parent='{}' from connection {:x}",
                     aMessage.get_id(), target.is_alive(), target.name().c_str(),
                     player.raw_id(), player.name().c_str(), aMessage.ConnectionId);
        return;
    }

    if (pPlayer->Connection != aMessage.ConnectionId)
    {
        spdlog::warn("The entity's owner is not the current player! exit id={:#x} owned by '{}' on connection {:x}, sent from connection {:x}",
                     aMessage.get_id(), pPlayer->Username, pPlayer->Connection, aMessage.ConnectionId);
        return;
    }

    // Which vehicle they were in, before we forget.
    flecs::entity vehicle;
    uint64_t currentSlot = 0;
    if (const auto* pAttachment = target.get<AttachmentComponent>())
    {
        vehicle = pAttachment->Parent;
        currentSlot = pAttachment->SlotId;
    }

    // The stale-exit guard. A seat swap is exit+enter a millisecond apart; when the
    // enter is applied first, the trailing exit no longer describes reality - honoring
    // it emptied a car whose driver believed he was driving it, and it was destroyed
    // under him. An exit that names a mount we do not currently have is dropped.
    // Requests without the fields (older clients) keep the old unconditional behaviour.
    if (aMessage.has_vehicle_id() && vehicle &&
        (aMessage.get_vehicle_id() != static_cast<uint64_t>(vehicle) ||
         (aMessage.has_sit_id() && aMessage.get_sit_id() != currentSlot)))
    {
        spdlog::info("Stale exit from player {:x}: names vehicle {:x} seat {:x}, but they are in vehicle {:x} seat {:x} - dropped",
                     aMessage.get_id(), aMessage.get_vehicle_id(), aMessage.get_sit_id(),
                     vehicle.id(), currentSlot);
        return;
    }

    // Start interpolation again
    target.remove<AttachmentComponent>();

    // If the leaver was the simulator, hand the car to somebody still inside - exactly
    // one simulator at all times is the invariant. The heir cannot drive from the
    // passenger seat, but their machine keeps the car coherent instead of leaving it
    // ownerless with people in it, which was the old behaviour.
    if (vehicle && vehicle.is_alive() && vehicle.parent() == player)
    {
        if (const auto next = NextOccupant(vehicle, player))
            PromoteToDriver(vehicle, next.parent());
    }

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
    {
        aVehicle.remove<PendingReleaseComponent>();
        return;
    }

    // PARK IT. Do not destroy it.
    //
    // This used to schedule the vehicle for destruction two seconds after the last person
    // got out, which is why a car vanished the moment its driver stepped away from it. The
    // destruction was never the goal: it was the fix for a DIFFERENT bug, where entering a
    // car created a fresh network entity every single time and nothing ever removed the old
    // ones - seven copies stacked in the road in one session.
    //
    // That duplication is prevented at the other end now: a client entering a car that is
    // already networked names it with remote_vehicle_id and joins the existing entity
    // rather than making another. So the entity can safely outlive its occupants, which is
    // what a parked car in the street actually is.
    //
    // Parking is TransferAuthority with no player, which the handoff already supports:
    // nobody simulates it, nobody may move it, and it holds its last replicated position
    // until somebody takes it over. Crucially it is reversible - the two-second delay
    // existed to absorb a reordered exit/enter pair, and a parked car that is re-entered
    // simply gets an owner again. There is nothing left to race against.
    //
    // The vehicle is NOT unloaded on the clients, so their mapping from the car in front of
    // them to its network id survives, and getting back in reuses this same entity.
    //
    // Still missing, deliberately: a cleanup policy. Nothing removes an abandoned car yet,
    // so a long session will accumulate them. That is a server-side lifecycle rule to be
    // written, not a reason to keep deleting cars people are still standing next to.
    TransferAuthority(aVehicle, flecs::entity::null());

    aVehicle.remove<PendingReleaseComponent>();
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

    // A server-declared NPC: the record says WHO to build (a specific person, not a
    // player mannequin), and the name is whatever the admin called them.
    if (auto* pNpcComponent = aEntity.get<NpcComponent>())
    {
        message.set_puppet_record(pNpcComponent->Record.c_str());
        message.set_username(pNpcComponent->Name.c_str());
    }

    // The name lives on the PLAYER, not the puppet - the puppet is a child entity with no
    // idea who owns it. Server-side and Discord-derived, so it cannot be spoofed by a
    // client claiming to be someone else.
    if (const auto owner = aEntity.parent())
    {
        if (const auto* pPlayerComponent = owner.get<PlayerComponent>())
        {
            // The CHARACTER's name, when they have one - this is what other players read
            // off the nameplate when they scan someone.
            //
            // Falling back to the Discord name only until a character exists. Somebody
            // being "noremacxxi" and their character being someone else is the whole
            // point of roleplay; showing the account name over a character's head breaks
            // it every time anybody looks at anybody.
            const auto* pCharacter = GServer->GetPlayerStore().FindCharacter(pPlayerComponent->DiscordId);

            if (pCharacter && !pCharacter->Name.empty())
                message.set_username(pCharacter->Name.c_str());
            else
                message.set_username(pPlayerComponent->Username.c_str());

            // Body gender is granted here, next to the name, for the same reason: the
            // server is the only side that stored the explicit answer. A player with no
            // character yet gets the old default (male) until they save one.
            message.set_is_male(pCharacter ? pCharacter->IsMale : true);
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

