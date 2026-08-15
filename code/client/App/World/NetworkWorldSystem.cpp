#include "NetworkWorldSystem.h"

#include <App/Settings.h>

#include "App/Network/NetworkService.h"
#include "RED4ext/Scripting/Natives/Generated/game/Puppet.hpp"
#include "RED4ext/Scripting/Natives/Generated/vehicle/BaseObject.hpp"
#include "RED4ext/Scripting/Natives/gameIEntityStubSystem.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/EntityStubComponentPS.hpp"
#include <RED4ext/Scripting/Natives/Generated/game/mounting/MountingFacility.hpp>
#include <RED4ext/Scripting/Natives/Generated/game/mounting/MountingInfo.hpp>
#include <RED4ext/Scripting/Natives/Generated/vehicle/MoveSystem.hpp>

#include "App/Components/EntityComponent.h"
#include "App/Components/SpawningComponent.h"
#include "App/Components/InterpolationComponent.h"
#include "App/World/PuppetRegistry.h"
#include "Game/Utils.h"
#include "Game/CharacterCustomizationSystem.h"

#include "ChatSystem.h"

static uint64_t GTick = 0;

uint64_t NetworkWorldSystem::GetTick()
{
    return GTick;
}

NetworkWorldSystem::NetworkWorldSystem()
{
    set_entity_range(10'000'000, 20'000'000);
}

bool NetworkWorldSystem::Spawn(uint64_t aServerId, const Red::Vector4& aPosition, const Red::Quaternion& aRotation, const Red::DynArray<Red::TweakDBID>& aEquipment, const Vector<uint8_t> aCcstate, const std::string& acUsername)
{
    if (!m_ready)
        return false;

    const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
    Red::EntityID id;
    Red::ScriptGameInstance game;

    spdlog::info("[Spawn] remote id {} - ccstate {} bytes, {} equipment item(s)", aServerId, aCcstate.size(),
                 aEquipment.size);

    if (!m_pCreatePuppet)
    {
        spdlog::error("[Spawn] CreatePuppet script function was never resolved - aborting");
        return false;
    }

    // Default to male if we have no appearance data. An empty buffer leaves the
    // customization state uninitialized, and reading it would be undefined.
    bool isBodyGenderMale = true;

    // The customization state is scoped deliberately, so its handle is released here
    // rather than surviving to the end of Spawn().
    {
        Red::Handle<game::ui::CharacterCustomizationState> stateHandle;
        CreateHandle_CharacterCustomizationState(&stateHandle);

        if (!stateHandle.instance)
        {
            spdlog::error("[Spawn] CreateHandle_CharacterCustomizationState returned a null instance - aborting");
            return false;
        }

        if (!aCcstate.empty())
        {
            auto reader = CMPReader(aCcstate);
            CharacterCustomizationState_Serialize(stateHandle.instance, &reader);
            isBodyGenderMale = stateHandle.instance->isBodyGenderMale;
        }
        else
        {
            spdlog::warn("[Spawn] remote player sent no ccstate - spawning with default appearance");
        }

    }

    const auto& record = isBodyGenderMale ? Settings::Get().puppetRecordMale : Settings::Get().puppetRecordFemale;

    if (!Red::Detail::CallFunctionWithArgs(m_pCreatePuppet, handle, id, aPosition, aRotation, isBodyGenderMale,
                                           Red::CString(record.c_str())))
    {
        spdlog::error("[Spawn] CreatePuppet call failed for remote id {}", aServerId);
        return false;
    }

    auto apprSystem = Red::GetGameSystem<NetworkWorldSystem>()->GetAppearanceSystem();
    apprSystem->AddEntity(id, aEquipment, aCcstate);

    // Recorded BEFORE ApplyAppearance runs, since that is what reads it back to set the
    // nameplate. Doing it afterwards leaves the puppet named after whatever record it was
    // built from.
    apprSystem->SetEntityName(id, acUsername);

    if (!id.IsDynamic())
    {
        spdlog::warn("[Spawn] entity id is not dynamic - bailing out for remote id {}", aServerId);
        return false;
    }

    // Register BEFORE the entity finishes assembling. The animation-thread hook
    // identifies our puppets through this registry instead of reading the entity's
    // tag array off-thread, which was racing against the main thread's setup.
    App::PuppetRegistry::Add(id.hash);

    auto spawned = make_alive(aServerId);

    spawned.emplace<SpawningComponent>(id);

    // Give the puppet somewhere to put movement immediately.
    //
    // InterpolationComponent is normally added by an observer on EntityComponent, which
    // does not exist until the promotion poll runs up to 200ms later. Movement arriving
    // before that had nowhere to go - it used to crash on a null component, and merely
    // guarding that would instead silently discard the first fifth of a second of a
    // player's movement and make them snap on arrival. Adding it here means their very
    // first update is buffered.
    spawned.add<InterpolationComponent>();

    return true;
}

void NetworkWorldSystem::DeSpawn(uint64_t aServerId) const
{
    const auto entity = GetEntityByServerId(aServerId);

    if (!entity)
        return;

    if (auto* pEntity = entity.get<EntityComponent>())
    {
        App::PuppetRegistry::Remove(pEntity->Id.hash);
        const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
        Red::Detail::CallFunctionWithArgs(m_pDeletePuppet, handle, pEntity->Id);
    }
    else if (auto* pEntity = entity.get<SpawningComponent>())
    {
        App::PuppetRegistry::Remove(pEntity->Id.hash);
        const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
        Red::Detail::CallFunctionWithArgs(m_pDeletePuppet, handle, pEntity->Id);
    }

    entity.destruct();
}

Red::Handle<Red::Entity> NetworkWorldSystem::GetEntity(Red::EntityID aId) const
{
    Red::Handle<Red::IGameSystem> dynamicEntitySystem;
    if (!Red::CallStatic("ScriptGameInstance", "GetDynamicEntitySystem", dynamicEntitySystem))
        return nullptr;

    Red::Handle<Red::Entity> entity;
    if (!Red::CallVirtual(dynamicEntitySystem, "GetEntity", entity, aId))
        return nullptr;

    return entity;
}

flecs::entity NetworkWorldSystem::GetEntityByServerId(uint64_t aServerId) const
{
    return flecs::entity(*this, aServerId);
}

Red::EntityID NetworkWorldSystem::GetEntityIdByServerId(uint64_t aServerId) const
{
    const auto entity = GetEntityByServerId(aServerId);
    if (!entity)
        return 0;

    if (auto* pEntity = entity.get<EntityComponent>())
        return pEntity->Id;

    if (auto* pEntity = entity.get<SpawningComponent>())
        return pEntity->Id;

    return 0;
}

flecs::entity NetworkWorldSystem::FindEntity(Red::EntityID aId) const
{
    auto entity = query<EntityComponent>().find(
        [aId](const EntityComponent& component)
        {
            return component.Id == aId;
        });

    if (!entity)
    {
        entity = query<SpawningComponent>().find(
            [aId](const SpawningComponent& component)
            {
                return component.Id == aId;
            });
    }

    return entity;
}

void NetworkWorldSystem::Update(uint64_t aTick)
{
    GTick = aTick;

    const auto delta = std::min(aTick - m_lastTick, 1000ull);
    m_lastTick = aTick;

    const auto service = Core::Container::Get<NetworkService>();
    if (service && service->IsConnected())
        progress(static_cast<float>(delta) / 1000.f);
}

void NetworkWorldSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{
    if (Settings::IsDisabled())
    {
        return;
    }
    spdlog::info("[NetworkWorldSystem] OnWorldAttached");
    IGameSystem::OnWorldAttached(aScene);

    m_chatSystem->OnWorldAttached(aScene);
    m_appearanceSystem->OnWorldAttached(aScene);
    m_interpolationSystem->OnWorldAttached(aScene);
    m_vehicleSystem->OnWorldAttached(aScene);

    m_ready = true;

    // NO automatic connecting.
    //
    // This used to connect here, and it was wrong twice over. Cyberpunk's MAIN MENU is
    // itself a world, so OnWorldAttached fires about half a second after startup - long
    // before any save exists. The client connected from the menu, the server began
    // streaming players into a world with no game in it, and it died trying to spawn
    // them.
    //
    // The deeper problem is that joining a server should be a decision, not something
    // that happens to you because of how the game was launched. Connecting is now driven
    // explicitly - see the MULTIPLAYER entry in MainMenu.reds.
}

void NetworkWorldSystem::HandleTeleport(const PacketEvent<server::NotifyTeleport>& aMessage)
{
    const auto& destination = aMessage.get_position();

    const Red::Vector4 position{destination.get_x(), destination.get_y(), destination.get_z(), 1.f};

    spdlog::info("[NetworkWorldSystem] teleport to ({:.1f}, {:.1f}, {:.1f})", position.X, position.Y, position.Z);

    // Handed to redscript because the teleportation facility is script-side machinery -
    // it deals with streaming the destination in and putting the camera somewhere sane,
    // which writing a position straight into the entity does not.
    Red::CallVirtual(this, "TeleportLocalPlayer", position, aMessage.get_rotation());
}

void NetworkWorldSystem::RequestJoin()
{
    spdlog::info("[NetworkWorldSystem] join requested from the main menu");
    m_joinRequested = true;
}

void NetworkWorldSystem::RequestRespawn()
{
    const auto& service = Core::Container::Get<NetworkService>();
    if (!service || !service->IsConnected())
        return;

    spdlog::info("[NetworkWorldSystem] downed - asking the server where to respawn");

    client::RespawnRequest request;
    service->Send(request);
}

/**
 * Sends the player's current appearance to the server as their character.
 *
 * Called by the creator when it closes - see CharacterCreator.reds. The spawn path already
 * serialises this exact blob, but that one describes whoever the loaded save contained;
 * this is the deliberate "this is me", and the only one the server keeps.
 */
void NetworkWorldSystem::SaveCharacterAppearance()
{
    const auto& service = Core::Container::Get<NetworkService>();
    if (!service || !service->IsConnected())
    {
        spdlog::warn("[Character] not connected - cannot save the character");
        return;
    }

    auto ccSystem = Red::GetGameSystem<Red::game::ui::CharacterCustomizationSystem>();
    auto stateHandle = GetCustomizationState(ccSystem);

    // GetCustomizationState() returns a pointer that can never be null; the instance
    // behind it can be, and is during ordinary gameplay. Serialising a null instance
    // crashes the game, so the instance is what gets checked.
    if (!stateHandle || !stateHandle->instance)
    {
        spdlog::error("[Character] no customization state to save");
        return;
    }

    auto writer = CMPWriter();
    CharacterCustomizationState_Serialize(stateHandle->instance, &writer);

    if (writer.bytes.empty())
    {
        spdlog::error("[Character] the customization state serialised to nothing - not saving");
        return;
    }

    client::SaveCharacterRequest request;
    request.set_ccstate(writer.bytes);
    request.set_is_male(stateHandle->instance->isBodyGenderMale);
    request.set_name(Settings::Get().discordName.c_str());

    service->Send(request);

    spdlog::info("[Character] sent {} bytes of appearance to the server", writer.bytes.size());
}

bool NetworkWorldSystem::IsConnected() const
{
    const auto& service = Core::Container::Get<NetworkService>();
    return service && service->IsConnected();
}

bool NetworkWorldSystem::ConsumeJoinRequest()
{
    // Deliberately one-shot. Loading a save from the MULTIPLAYER entry should connect;
    // loading another save afterwards from the pause menu should not silently reconnect
    // someone who never asked for it a second time.
    const bool requested = m_joinRequested;
    m_joinRequested = false;

    return requested;
}

void NetworkWorldSystem::OnAfterWorldDetach()
{
    if (Settings::IsDisabled())
    {
        return;
    }
    spdlog::info("[NetworkWorldSystem] OnAfterWorldDetach");
    m_ready = false;

    m_interpolationSystem->OnAfterWorldDetach();
    m_chatSystem->OnAfterWorldDetach();
    m_vehicleSystem->OnAfterWorldDetach();

    m_remotePlayerId = std::nullopt;

    IGameSystem::OnAfterWorldDetach();
}

void NetworkWorldSystem::OnBeforeWorldDetach(RED4ext::world::RuntimeScene* aScene)
{
    if (Settings::IsDisabled())
    {
        return;
    }
    IGameSystem::OnBeforeWorldDetach(aScene);

    m_appearanceSystem->OnBeforeWorldDetach(aScene);
}

void NetworkWorldSystem::HandleCharacterLoad(const PacketEvent<server::NotifyCharacterLoad>& aMessage)
{
    auto& pos = aMessage.get_position();
    auto& rot = aMessage.get_rotation();

    const Red::Vector4 position{pos.get_x(), pos.get_y(), pos.get_z(), 1.f};
    const auto eulerAngles = glm::vec3(0.f, 0.f, rot);
    const auto quat = glm::quat(eulerAngles);

    const Red::Quaternion rotation{quat.x, quat.y, quat.z, quat.w};

    auto equipment = Red::DynArray<Red::TweakDBID>(this->GetAllocator());
    for (auto item : aMessage.get_equipment())
    {
        // The wire carries the ID's numeric value; TweakDBID is a thin wrapper around
        // exactly that, so this is a reinterpretation rather than a lookup. Constructing
        // one from a STRING would hash the text - which is what used to happen, with an
        // empty string, producing an ID that matches no item at all.
        Red::TweakDBID id;
        id.value = item;

        equipment.EmplaceBack(id);
    }

    auto ccstate = aMessage.get_ccstate();

    Spawn(aMessage.get_id(), position, rotation, equipment, ccstate, aMessage.get_username().c_str());
}

void NetworkWorldSystem::HandleEntityUnload(const PacketEvent<server::NotifyEntityUnload>& aMessage)
{
    DeSpawn(aMessage.get_id());
}

/**
 * The server has asked this client to make a character.
 *
 * Handed straight to redscript - the creator is script-side machinery, and driving the
 * game's own UI from native would mean reimplementing what redscript can already call.
 */
void NetworkWorldSystem::HandleOpenCharacterCreator(const PacketEvent<server::OpenCharacterCreator>& aMessage)
{
    if (aMessage.get_capture_only())
    {
        spdlog::info("[Character] the server asked for our current appearance");
        SaveCharacterAppearance();
        return;
    }

    spdlog::info("[Character] the server asked us to open the character creator");

    Red::CallVirtual(this, "OpenCharacterCreator");
}

void NetworkWorldSystem::HandleSpawnCharacterResponse(const PacketEvent<server::SpawnCharacterResponse>& aMessage)
{
    if (!aMessage.has_id())
    {
        spdlog::error("Failed to spawn our character on the server...");
        return;
    }

    SetRemotePlayerId(aMessage.get_id());
}

static Core::RawFunc<
    1160782872UL,
    bool (*)(Red::game::mounting::MountingFacility *, const Red::ent::Entity &, const Red::game::mounting::MountingSlotId &, bool)>
    IsMountedToObject;
static Core::RawFunc<
    3120376212UL,
    bool (*)(Red::game::mounting::MountingFacility *, const Red::ent::Entity &, const Red::game::mounting::MountingSlotId &, Red::game::mounting::MountingInfo &)>
    GetMountingInfo;

void NetworkWorldSystem::UpdatePlayerLocation() const
{
    const auto system = Red::GetGameSystem<Game::PlayerSystem>();
    Red::Handle<Red::GameObject> player;
    system->GetLocalPlayerControlledGameObject(player);

    if (!player || !GetRemotePlayerId())
        return;

    auto puppet = Red::Cast<Red::game::Puppet>(player);

    // const auto mountingFacility = Red::GetGameSystem<Red::game::mounting::MountingFacility>();
    // Red::game::mounting::MountingInfo mountingInfo;
    // if (GetMountingInfo(mountingFacility, *puppet.instance, Red::CName("seat_front_left"), mountingInfo)) {
    if (auto vehicle_id = GetVehicleSystem()->GetVehicleGameId())
    {
        // auto vehicle = Red::Cast<Red::vehicle::BaseObject>(GetEntity(mountingInfo.parentId));
        auto vehicle = Red::Cast<Red::vehicle::BaseObject>(GetEntity(*vehicle_id));
        if (!vehicle)
        {
            spdlog::info("Couldn't find vehicle: {}", (*vehicle_id).hash);
            return;
        }
        if (auto remote_id = GetVehicleSystem()->GetVehicleRemoteId(); remote_id != std::nullopt)
        {
            auto transform = Red::WorldTransform();

            transform = vehicle->rigidBody->currentTransform;

            // const auto cEntityRotation = eulerAngles(Game::ToGlm(vehicle->placedComponent->worldTransform.Orientation));

            // about the same
            // transform = vehicle->worldTransform;

            // about the same
            // transform = vehicle->runtimeData->transform;

            // try to get transform from move system
            // seems to return 0 :/
            // auto transform = Red::WorldTransform();
            // const auto moveSystem = Red::GetGameSystem<Red::vehicle::MoveSystem>();
            // // GetCurrentTransform
            // reinterpret_cast<void (*)(const Red::vehicle::MoveSystem *, const Red::EntityID &, Red::WorldTransform*)>(*(uintptr_t*)(*(uintptr_t*)moveSystem + 0x250))(moveSystem, vehicle->id, &transform);

            const auto cEntityPosition = transform.Position;
            const auto cEntityRotation = eulerAngles(Game::ToGlm(transform.Orientation));
            float speed = vehicle->rigidBody->velocity.Magnitude();

            common::Vector3 pos;
            pos.set_x(cEntityPosition.x);
            pos.set_y(cEntityPosition.y);
            pos.set_z(cEntityPosition.z);

            common::Vector3 rot;
            rot.set_x(cEntityRotation.x);
            rot.set_y(cEntityRotation.y);
            rot.set_z(cEntityRotation.z);

            client::MoveEntityRequest request;
            request.set_position(pos);
            request.set_full_rotation(rot);
            request.set_id(*remote_id);
            request.set_speed(speed);
            request.set_tick(GetTick());

            const auto pNetworkService = Core::Container::Get<NetworkService>();
            pNetworkService->Send(request);

            client::MoveEntityRequest characterRequest;
            characterRequest.set_position(pos);
            characterRequest.set_rotation(cEntityRotation.z);
            characterRequest.set_id(*GetRemotePlayerId());
            characterRequest.set_speed(speed);
            characterRequest.set_tick(GetTick());

            pNetworkService->Send(characterRequest);
        }
    }
    else
    {
        // localTransform is not updated as the player walks (V moves via the character
        // controller), so it stays frozen at its spawn value. Rotation already reads
        // worldTransform on the next line - use it for position too.
        const auto cEntityPosition = puppet->placedComponent->worldTransform.Position;
        const auto cEntityRotation = eulerAngles(Game::ToGlm(puppet->placedComponent->worldTransform.Orientation));

        // Speed is MEASURED, not read from the game.
        //
        // This used to be moveComponent->speed.Magnitude() - a field at a hand-mapped
        // offset. That offset moved on 2.31, so it returned garbage around 3e8. The
        // animation state machine compares against 3 m/s to walk and 5 to run, so an
        // absurd value pinned every remote player past both thresholds forever and their
        // animations never matched what they were doing.
        //
        // Distance over time needs no offsets and cannot break on a game update. The
        // numbers are metres per second by construction, which is exactly what the state
        // machine wants.
        const glm::vec3 here{cEntityPosition.x, cEntityPosition.y, cEntityPosition.z};
        const auto now = std::chrono::steady_clock::now();

        float speed = 0.f;

        if (m_hasLastPosition)
        {
            const float elapsed = std::chrono::duration<float>(now - m_lastPositionAt).count();

            // Guard against a zero or absurdly small interval - dividing by it produces
            // an infinity that behaves exactly like the bug this replaces.
            if (elapsed > 0.001f)
            {
                speed = glm::distance(here, m_lastPosition) / elapsed;

                // A teleport is not sprinting. Without this, /tp or a loading screen
                // registers as several hundred metres per second and slams the puppet
                // into a sprint animation on arrival.
                constexpr float kFastestPlausible = 20.f;
                if (speed > kFastestPlausible)
                    speed = 0.f;
            }
        }

        m_lastPosition = here;
        m_lastPositionAt = now;
        m_hasLastPosition = true;

        common::Vector3 pos;
        pos.set_x(cEntityPosition.x);
        pos.set_y(cEntityPosition.y);
        pos.set_z(cEntityPosition.z);

        client::MoveEntityRequest request;
        request.set_position(pos);
        request.set_rotation(cEntityRotation.z);
        request.set_id(*GetRemotePlayerId());
        request.set_speed(speed);
        request.set_tick(GetTick());

        const auto pNetworkService = Core::Container::Get<NetworkService>();
        pNetworkService->Send(request);
    }

    // if (GetEntityByServerId(*GetRemotePlayerId()).get_mut<InterpolationComponent>()->Attached)
    // {
    //     auto vehicle_id = GetVehicleSystem()->GetVehicle(player->id);
    //     if (!vehicle_id)
    //     {
    //         spdlog::warn("No vehicle for player({})", player->id.hash);
    //         return;
    //     }
    //     auto entity = GetEntity(vehicle_id);
    //     if (!entity)
    //     {
    //         spdlog::warn("No entity for vehicle({})", vehicle_id.hash);
    //         return;
    //     }
    //     auto vehicle = Red::Cast<Red::vehicle::BaseObject>(entity);
    //     if (!vehicle)
    //     {
    //         spdlog::warn("Entity is not vehicle");
    //         return;
    //     }
    //     else
    //     {
    //         // if (vehicle->placedComponent) {
    //             // entityPosition = vehicle->placedComponent->localTransform.Position;
    //         // } else {
    //             // entityPosition = vehicle->worldTransform.Position;
    //             entityPosition = vehicle->rigidBody->worldPosition;
    //         // }
    //     }
    // }
}

void NetworkWorldSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    spdlog::info("[OnInitialize]");

    IGameSystem::OnInitialize(aJob);

    if (Settings::IsDisabled())
        return;

    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleCharacterLoad>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleEntityUnload>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleTeleport>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleSpawnCharacterResponse>(this);
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleOpenCharacterCreator>(this);

    m_remotePlayerId = std::nullopt;

    m_pCreatePuppet = Red::Detail::GetFunction(GetType(), "CreatePuppet");
    m_pDeletePuppet = Red::Detail::GetFunction(GetType(), "DeletePuppet");

    m_interpolationSystem = RED4ext::MakeHandle<InterpolationSystem>();
    m_interpolationSystem->OnInitialize(aJob);

    m_appearanceSystem = RED4ext::MakeHandle<AppearanceSystem>();
    m_appearanceSystem->OnInitialize(aJob);

    m_chatSystem = RED4ext::MakeHandle<ChatSystem>();
    m_chatSystem->OnInitialize(aJob);

    m_vehicleSystem = RED4ext::MakeHandle<VehicleSystem>();
    m_vehicleSystem->OnInitialize(aJob);
}

void NetworkWorldSystem::Connect()
{
    auto address = fmt::format("{}:{}", Settings::Get().ip, Settings::Get().port);

    // Log the address we actually dial. The launch arguments must use the
    // --ip=<addr> --port=<n> form; anything else silently leaves these at their
    // defaults (127.0.0.1:11778) and the connection times out against your own PC.
    spdlog::info("Connecting to {}", address);

    Core::Container::Get<NetworkService>()->Connect(address);
}

void NetworkWorldSystem::Disconnect()
{
    Core::Container::Get<NetworkService>()->Close();
}

void NetworkWorldSystem::OnConnected()
{
    RED4ext::StackArgs_t args;
    ExecuteFunction(this, this->GetNativeType()->GetFunction("OnConnected"), nullptr, args);

    const auto pNetworkService = Core::Container::Get<NetworkService>();

    m_updatePlayerLocation = system("Update player location")
        .kind(flecs::OnUpdate)
        .interval(1.f / pNetworkService->GetServerSettings().get_update_rate())
        .run([this](flecs::iter& it)
        {
            UpdatePlayerLocation();
        });

    m_updateSpawningEntities = system<SpawningComponent>("Spawning entity process")
        .interval(0.2f)
        .write<EntityComponent>()
        .write<SpawningComponent>()
        .each([this](flecs::entity aEntity, SpawningComponent& aSpawning)
        {
            const auto pEntity = GetEntity(aSpawning.Id);

            if (!pEntity)
                return;

            if (const auto pOwner = Red::Cast<Red::GameObject>(pEntity))
            {
                if (pOwner->tags.Contains("CyberpunkMP.Vehicle"))
                    return;

                aEntity.emplace<EntityComponent>(aSpawning.Id, false, aSpawning.Controller);
                aEntity.remove<SpawningComponent>();

                pOwner->tags.Add("CyberpunkMP.Puppet");
            }
        });

    m_interpolationSystem->OnConnected();
}

void NetworkWorldSystem::OnDisconnected(Client::EDisconnectReason aReason)
{
    each([this](flecs::entity entity, EntityComponent&)
        {
            DeSpawn(entity.raw_id());
            entity.destruct();
        });

    each([this](flecs::entity entity, SpawningComponent&)
        {
            DeSpawn(entity.raw_id());
            entity.destruct();
        });

    App::PuppetRegistry::Clear();

    if (m_updatePlayerLocation)
        m_updatePlayerLocation.destruct();

    if (m_updateSpawningEntities)
        m_updateSpawningEntities.destruct();

    m_interpolationSystem->OnDisconnected();
    m_vehicleSystem->OnDisconnected();

    RED4ext::StackArgs_t args;
    auto reason = (uint32_t)aReason;
    args.emplace_back(RED4ext::CRTTISystem::Get()->GetType("Uint32"), &reason);
    ExecuteFunction(this, this->GetNativeType()->GetFunction("OnDisconnected"), nullptr, args);
}
