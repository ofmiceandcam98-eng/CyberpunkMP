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

bool NetworkWorldSystem::Spawn(uint64_t aServerId, const Red::Vector4& aPosition, const Red::Quaternion& aRotation, const Red::DynArray<Red::TweakDBID>& aEquipment, const Vector<uint8_t> aCcstate)
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

    // The customization state is scoped deliberately. It used to live until Spawn()
    // returned, meaning its handle was RELEASED immediately after [PROBE 10] logged - which
    // is exactly where crashing clients go silent.
    //
    // Every puppet that survived carried an EMPTY ccstate, so this object was created and
    // released without ever being deserialised. The one that crashed carried 6352 bytes,
    // so it was populated first. Releasing a populated state is therefore the last thing
    // that happens before the crash, and nothing had ever measured it.
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

        spdlog::info("[PROBE 23] releasing customization state ({} bytes deserialised)", aCcstate.size());
    }

    spdlog::info("[PROBE 24] customization state released cleanly");

    // TEMPORARY [PROBE 22]. Every puppet that survived so far carried no appearance data and
    // therefore defaulted to male / MaMuppet. The first one with real appearance data crashed,
    // from across the map, which rules out proximity. Gender is the one thing real ccstate
    // changes about the entity actually built, so record which record we are asking for.
    spdlog::info("[PROBE 22] creating puppet: isBodyGenderMale={} -> record {}", isBodyGenderMale,
                 isBodyGenderMale ? "Character.MaMuppet" : "Character.WaMuppet");

    if (!Red::Detail::CallFunctionWithArgs(m_pCreatePuppet, handle, id, aPosition, aRotation, isBodyGenderMale))
    {
        spdlog::error("[Spawn] CreatePuppet call failed for remote id {}", aServerId);
        return false;
    }

    // TEMPORARY [PROBE] lines bisect the remote-spawn crash. Both crash logs end at
    // AddEntity's first log line, so the fault is somewhere in this block. Strip these
    // once the culprit is identified.
    spdlog::info("[PROBE 1] puppet created, entity id {:x} - calling AddEntity", id.hash);

    auto apprSystem = Red::GetGameSystem<NetworkWorldSystem>()->GetAppearanceSystem();
    apprSystem->AddEntity(id, aEquipment, aCcstate);

    spdlog::info("[PROBE 5] AddEntity returned");

    if (!id.IsDynamic())
    {
        spdlog::warn("[PROBE 6] entity id is NOT dynamic - bailing out of Spawn");
        return false;
    }

    spdlog::info("[PROBE 6] entity id is dynamic");

    // Register BEFORE the entity finishes assembling. The animation-thread hook
    // identifies our puppets through this registry instead of reading the entity's
    // tag array off-thread, which was racing against the main thread's setup.
    App::PuppetRegistry::Add(id.hash);

    spdlog::info("[PROBE 7] PuppetRegistry::Add done");

    // Split from the emplace below so the two can be told apart. The low 32 bits are
    // the flecs id proper and the high 32 are its generation counter; set_entity_range
    // above declares 10'000'000-20'000'000, and server ids routinely fall outside it.
    spdlog::info("[PROBE 8] calling make_alive({}) - low32 {}, generation {}", aServerId,
                 static_cast<uint32_t>(aServerId), static_cast<uint32_t>(aServerId >> 32));

    auto spawned = make_alive(aServerId);

    spdlog::info("[PROBE 9] make_alive returned - calling emplace<SpawningComponent>");

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

    spdlog::info("[PROBE 10] Spawn complete for remote id {}", aServerId);

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

void NetworkWorldSystem::RequestJoin()
{
    spdlog::info("[NetworkWorldSystem] join requested from the main menu");
    m_joinRequested = true;
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
        equipment.EmplaceBack(item);
    }

    auto ccstate = aMessage.get_ccstate();

    Spawn(aMessage.get_id(), position, rotation, equipment, ccstate);
}

void NetworkWorldSystem::HandleEntityUnload(const PacketEvent<server::NotifyEntityUnload>& aMessage)
{
    DeSpawn(aMessage.get_id());
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
        float speed = puppet->moveComponent->speed.Magnitude();

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
    pNetworkService->RegisterHandler<&NetworkWorldSystem::HandleSpawnCharacterResponse>(this);

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
            // TEMPORARY [PROBE 11..16]. Spawn() itself is cleared - a solo /dummy run
            // reached [PROBE 10] and then died, so the fault is here or later, while the
            // game is still assembling the entity asynchronously.
            spdlog::info("[PROBE 11] poll: resolving entity {:x}", aSpawning.Id.hash);

            const auto pEntity = GetEntity(aSpawning.Id);

            if (!pEntity)
                return;

            spdlog::info("[PROBE 12] poll: GetEntity returned a handle");

            if (const auto pOwner = Red::Cast<Red::GameObject>(pEntity))
            {
                spdlog::info("[PROBE 13] poll: cast to GameObject ok - reading tags");

                if (pOwner->tags.Contains("CyberpunkMP.Vehicle"))
                    return;

                spdlog::info("[PROBE 14] poll: tags read ok - promoting to EntityComponent");

                 aEntity.emplace<EntityComponent>(aSpawning.Id, false, aSpawning.Controller);
                 aEntity.remove<SpawningComponent>();

                spdlog::info("[PROBE 15] poll: promoted - about to MUTATE tags array");

                 pOwner->tags.Add("CyberpunkMP.Puppet");

                spdlog::info("[PROBE 16] poll: tags.Add survived - entity fully promoted");
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
