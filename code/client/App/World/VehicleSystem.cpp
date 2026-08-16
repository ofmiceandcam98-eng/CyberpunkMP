
#include "VehicleSystem.h"

#include "App/Network/NetworkService.h"
#include "RED4ext/Scripting/Natives/Generated/game/Puppet.hpp"
#include "RED4ext/Scripting/Natives/gameIEntityStubSystem.hpp"
#include "RED4ext/Scripting/Natives/Generated/game/EntityStubComponentPS.hpp"
#include "RED4ext/Scripting/Natives/Generated/vehicle/WheeledBaseObject.hpp"

#include "NetworkWorldSystem.h"
#include "Game/Utils.h"
#include "App/Components/EntityComponent.h"
#include "App/Components/AttachedComponent.h"
#include "App/Components/SpawningComponent.h"
#include "App/Components/InterpolationComponent.h"


// Puts a network vehicle fully under remote control: no local player input, no local
// physics. Interpolation drives these with ForceMoveTo, so a live rigid body here is not
// just wasted work - a network copy spawns at the same transform as the local world's own
// parked instance of that car, and two interpenetrating physics bodies resolve as a
// depenetration impulse. That impulse is the "cars explode when another player gets in"
// report. Called from OnVehicleReady so the copy never simulates a single frame; DoMount
// repeats it, which is harmless, for vehicles that were already spawned when we mounted
// someone into them.
static void MakeRemoteDriven(const Red::Handle<Red::vehicle::WheeledBaseObject>& aVehicle)
{
    if (!aVehicle)
        return;

    // called from vehicle::actions::DriveAction::OnStart
    static Core::RawFunc<4039776020UL, void (*)(Red::vehicle::BaseObject*, bool)> SetIsPlayerControlled;
    static Core::RawFunc<1620777158UL, void (*)(Red::vehicle::BaseObject*, uint32_t)> SetFlags;
    static Core::RawFunc<1585713002UL, void (*)(Red::vehicle::BaseObject*, bool)> SetKinematic;

    SetIsPlayerControlled(aVehicle, false);
    // turn on engine
    reinterpret_cast<void (*)(Red::vehicle::WheeledBaseObject*, bool)>(*(uintptr_t*)(*(uintptr_t*)aVehicle.instance + 0x328))(aVehicle, true);
    if (aVehicle->engineData)
        aVehicle->engineData->unk61 = 0;
    SetFlags(aVehicle, 0x10);
    SetFlags(aVehicle, 0x80);
    SetKinematic(aVehicle, true);
}

// The inverse of MakeRemoteDriven: hands a vehicle to the LOCAL simulation. Called when
// the server assigns us authority over a network-spawned car - MakeRemoteDriven put its
// physics to sleep at spawn, and without waking it back up you can be assigned a car you
// cannot actually drive (the control-handoff gap MakeRemoteDriven's introduction
// documented). Engine state is left alone - it is already running on a car anyone was
// just driving, and DoMount manages it for fresh mounts.
static void MakeLocallyDriven(const Red::Handle<Red::vehicle::WheeledBaseObject>& aVehicle)
{
    if (!aVehicle)
        return;

    static Core::RawFunc<4039776020UL, void (*)(Red::vehicle::BaseObject*, bool)> SetIsPlayerControlled;
    static Core::RawFunc<1585713002UL, void (*)(Red::vehicle::BaseObject*, bool)> SetKinematic;

    SetKinematic(aVehicle, false);
    SetIsPlayerControlled(aVehicle, true);
}

void VehicleSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{
    m_ready = true;
    Red::CallVirtual(this, "OnWorldAttached");
}

void VehicleSystem::OnAfterWorldDetach()
{
    m_ready = false;
}

void VehicleSystem::OnDisconnected()
{
    m_vehicleRemoteId = std::nullopt;
    m_vehicleGameId = std::nullopt;
    m_authorityEpoch = 0;
}

void VehicleSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&VehicleSystem::HandleVehicleLoadMessage>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleVehicleEnterMessage>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleVehicleExitMessage>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleAuthorityAssigned>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleAuthorityRevoked>(this);

    m_pSpawnVehicle = Red::Detail::GetFunction(GetType(), "SpawnVehicle");
    m_pEnterVehicle = Red::Detail::GetFunction(GetType(), "EnterVehicle");
    m_pExitVehicle = Red::Detail::GetFunction(GetType(), "ExitVehicle");
}

std::optional<uint64_t> VehicleSystem::GetVehicleRemoteId() const
{
    return m_vehicleRemoteId;
}

std::optional<Red::EntityID> VehicleSystem::GetVehicleGameId() const
{
    return m_vehicleGameId;
}

uint32_t VehicleSystem::GetAuthorityEpoch() const
{
    return m_authorityEpoch;
}

void VehicleSystem::OnVehicleEnter(Red::EntityID aVehicle, const Red::TweakDBID& aVehicleTdbid, Red::CName aName, const Red::Vector4& aPostion, const Red::Quaternion& aOrientation)
{
    spdlog::info("[VehicleSystem] OnVehicleEnter");
    const auto pNetworkService = Core::Container::Get<NetworkService>();
    if (!pNetworkService->IsConnected())
        return;

    const auto handle = Red::GetGameSystem<NetworkWorldSystem>();

    client::EnterVehicleRequest request;
    request.set_id(*handle->GetRemotePlayerId());
    request.set_vehicle_id(aVehicleTdbid.value);
    request.set_sit_id(aName.hash);

    const auto serverVehicle = handle->FindEntity(aVehicle);
    if (serverVehicle)
    {
        request.set_remote_vehicle_id(serverVehicle);

        m_vehicleGameId = std::nullopt;
    }
    else
    {
        const auto cEntityRotation = eulerAngles(Game::ToGlm(aOrientation));

        common::Vector3 position;
        position.set_x(aPostion.X);
        position.set_y(aPostion.Y);
        position.set_z(aPostion.Z);
        request.set_position(position);
        request.set_rotation(cEntityRotation.z);

        m_vehicleGameId = aVehicle;
    }

    pNetworkService->Send(request);
}

void VehicleSystem::OnVehicleExit()
{
    m_vehicleGameId = std::nullopt;
    m_vehicleRemoteId = std::nullopt;

    spdlog::info("[VehicleSystem] OnVehicleExit");
    const auto pNetworkService = Core::Container::Get<NetworkService>();
    if (!pNetworkService->IsConnected())
        return;

    client::ExitVehicleRequest request;

    const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
    request.set_id(*handle->GetRemotePlayerId());

    pNetworkService->Send(request);
}

bool VehicleSystem::HandleVehicleLoadMessage(const PacketEvent<server::NotifyVehicleLoad>& aMessage)
{
    spdlog::info("[VehicleSystem] HandleVehicleLoadMessage");
    const auto handle = Red::Handle(this);
    Red::EntityID id;
    Red::ScriptGameInstance game;

    Red::Vector4 position;
    position.X = aMessage.get_position().get_x();
    position.Y = aMessage.get_position().get_y();
    position.Z = aMessage.get_position().get_z();

    const auto eulerAngles = glm::vec3(0.f, 0.f, aMessage.get_rotation());
    const auto quat = glm::quat(eulerAngles);
    Red::Quaternion rotation = Game::ToRed(quat);

    if (!Red::Detail::CallFunctionWithArgs(m_pSpawnVehicle, handle, id, aMessage.get_tweak_id(), position, rotation))
        return false;

    if (!id.IsDynamic())
        return false;

    // spdlog::info("[VehicleSystem] * Spawned: {}, {}", aMessage.get_id(), id.hash);
    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();
    worldSystem->make_alive(aMessage.get_id()).emplace<SpawningComponent>(id);

    return true;
}

bool VehicleSystem::HandleVehicleEnterMessage(const PacketEvent<server::NotifyVehicleEnter>& aMessage)
{
    spdlog::info("[VehicleSystem] HandleVehicleEnterMessage");

    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();

    const auto sit = Red::CName(aMessage.get_sit_id());
    const auto character = worldSystem->GetEntityByServerId(aMessage.get_character_id());

    // m_vehicleGameId must be checked too: when we were assigned control of a vehicle we
    // did NOT locally own (we sat in the driver seat of a network-spawned car),
    // m_vehicleRemoteId is set but m_vehicleGameId is nullopt - and *m_vehicleGameId is
    // undefined behaviour. Fall through to the server-id lookup instead, which resolves
    // the same vehicle through its EntityComponent.
    if (m_vehicleRemoteId && aMessage.get_vehicle_id() == *m_vehicleRemoteId && m_vehicleGameId)
    {
        DoMount(character, *m_vehicleGameId, sit);
    }
    else
    {
        const auto entity = worldSystem->GetEntityByServerId(aMessage.get_vehicle_id());
        if (entity.has<EntityComponent>())
        {
            DoMount(character, entity.get<EntityComponent>()->Id, sit);
        }
        else
        {
            // spdlog::info("[VehicleSystem] * Queueing vehicle, server: {}", aMessage.get_vehicle_id());
            const auto vehicle = worldSystem->GetEntityIdByServerId(aMessage.get_vehicle_id());
            // spdlog::info("[VehicleSystem]                     entity: {}", vehicle.hash);
            m_pendingMounts[vehicle].push_back(aMessage);
        }
    }

    // 
    // const auto sit = Red::CName(aMessage.get_sit_id());

    // if (!Red::Detail::CallFunctionWithArgs(m_pEnterVehicle, handle, res, character, vehicle, sit))
    //     return false;

    return true;
}

void VehicleSystem::OnVehicleReady(const Red::EntityID& aVehicleEntityId)
{
    spdlog::info("[VehicleSystem] OnVehicleReady");

    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();

    // Physics off FIRST, mounts second. Everything reaching this callback is a network
    // copy this client spawned (only SpawnVehicle tags CyberpunkMP.Vehicle), and it may be
    // standing inside the local world's own copy of the same parked car. It must never
    // simulate, whether or not anyone has been mounted into it yet.
    MakeRemoteDriven(Red::Cast<Red::vehicle::WheeledBaseObject>(worldSystem->GetEntity(aVehicleEntityId)));

    if (m_pendingMounts.find(aVehicleEntityId) != m_pendingMounts.end())
    {
        for (auto& message : m_pendingMounts[aVehicleEntityId])
        {
            const auto sit = Red::CName(message.get_sit_id());
            const auto character = worldSystem->GetEntityByServerId(message.get_character_id());

            auto vehicleEntity = worldSystem->GetEntityByServerId(message.get_vehicle_id());
            vehicleEntity.emplace<EntityComponent>(aVehicleEntityId, true, nullptr);
            vehicleEntity.remove<SpawningComponent>();

            const auto vehicle = worldSystem->GetEntityIdByServerId(message.get_vehicle_id());

            DoMount(character, vehicle, sit);
        }

        m_pendingMounts.erase(aVehicleEntityId);
    }
    else
    {
        spdlog::info("[VehicleSystem] * Couldn't find vehicle: {}", aVehicleEntityId.hash);
    }
}

bool VehicleSystem::HandleVehicleExitMessage(const PacketEvent<server::NotifyVehicleExit>& aMessage)
{
    spdlog::info("[VehicleSystem] HandleVehicleExitMessage");

    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto handle = Red::Handle(this);
    bool res;
    const auto character = worldSystem->GetEntityIdByServerId(aMessage.get_character_id());
    Red::Detail::CallFunctionWithArgs(m_pExitVehicle, handle, res, character);

    auto characterEntity = worldSystem->GetEntityByServerId(aMessage.get_character_id());

    characterEntity.remove<AttachedComponent>();

    return true;
}

bool VehicleSystem::HandleAuthorityAssigned(const PacketEvent<server::NotifyAuthorityAssigned>& aMessage)
{
    spdlog::info("[VehicleSystem] authority assigned: entity {:x}, epoch {}", aMessage.get_entity_id(),
                 aMessage.get_epoch());

    m_vehicleRemoteId = aMessage.get_entity_id();
    m_authorityEpoch = aMessage.get_epoch();

    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();

    // A network-spawned car resolves through its EntityComponent. Two things hang on
    // storing its game id here: UpdatePlayerLocation only streams vehicle movement when
    // m_vehicleGameId is set (an adopted car's driver never sent a single move before
    // this), and the physics MakeRemoteDriven put to sleep at spawn has to be woken for
    // the new simulator.
    const auto gameId = worldSystem->GetEntityIdByServerId(aMessage.get_entity_id());
    if (gameId.hash != 0)
    {
        m_vehicleGameId = gameId;
        MakeLocallyDriven(Red::Cast<Red::vehicle::WheeledBaseObject>(worldSystem->GetEntity(gameId)));
    }
    // else: our own locally-entered car. m_vehicleGameId was already set in
    // OnVehicleEnter, and its physics never stopped being ours.

    return true;
}

bool VehicleSystem::HandleAuthorityRevoked(const PacketEvent<server::NotifyAuthorityRevoked>& aMessage)
{
    spdlog::info("[VehicleSystem] authority revoked: entity {:x}, epoch {}", aMessage.get_entity_id(),
                 aMessage.get_epoch());

    if (!m_vehicleRemoteId || *m_vehicleRemoteId != aMessage.get_entity_id())
        return true; // not ours - nothing to stop

    m_vehicleRemoteId = std::nullopt;

    // Usually we already exited and OnVehicleExit cleared everything. If we still hold
    // the entity - we are now a passenger in a car somebody else took over - our machine
    // must stop simulating it and go back to following the wire. This is the revoke half
    // that never existed: two machines simulating one car is what passengers felt as
    // bouncing.
    if (m_vehicleGameId)
    {
        const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();
        MakeRemoteDriven(Red::Cast<Red::vehicle::WheeledBaseObject>(worldSystem->GetEntity(*m_vehicleGameId)));
    }

    return true;
}

void VehicleSystem::DoMount(flecs::entity aCharacter, Red::EntityID aVehicle, Red::CName aSit)
{
    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto character = worldSystem->GetEntityIdByServerId(aCharacter);
    const auto vehicle = Red::Cast<Red::vehicle::WheeledBaseObject>(worldSystem->GetEntity(aVehicle));
    const auto handle = Red::Handle(this);
    bool res;

    if (!vehicle)
    {
        spdlog::warn("[VehicleSystem] DoMount: vehicle entity {:x} not resolvable yet - mount dropped", aVehicle.hash);
        return;
    }

    Red::Detail::CallFunctionWithArgs(m_pEnterVehicle, handle, res, character, vehicle->id, aSit);

    aCharacter.add<AttachedComponent>();

    // Never our own car - the local player's vehicle keeps local physics and input.
    if (!m_vehicleGameId || *m_vehicleGameId != aVehicle)
    {
        MakeRemoteDriven(vehicle);
    }
}

