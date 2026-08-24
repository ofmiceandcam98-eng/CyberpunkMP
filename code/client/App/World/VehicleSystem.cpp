
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
#include "App/Components/DriverComponent.h"
#include "App/World/PuppetRegistry.h"


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

    // Step logging bisects a native crash in this path: the last line printed before
    // silence names the killer. Cheap, and this path runs a handful of times per session.
    spdlog::info("[VehicleSystem] MakeRemoteDriven: SetIsPlayerControlled");
    SetIsPlayerControlled(aVehicle, false);
    // turn on engine
    spdlog::info("[VehicleSystem] MakeRemoteDriven: engine vcall");
    reinterpret_cast<void (*)(Red::vehicle::WheeledBaseObject*, bool)>(*(uintptr_t*)(*(uintptr_t*)aVehicle.instance + 0x328))(aVehicle, true);
    spdlog::info("[VehicleSystem] MakeRemoteDriven: engineData {}", aVehicle->engineData ? "set" : "null");
    if (aVehicle->engineData)
        aVehicle->engineData->unk61 = 0;
    SetFlags(aVehicle, 0x10);
    SetFlags(aVehicle, 0x80);
    spdlog::info("[VehicleSystem] MakeRemoteDriven: SetKinematic");
    SetKinematic(aVehicle, true);
    spdlog::info("[VehicleSystem] MakeRemoteDriven: done");
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
    m_mountedServerId = std::nullopt;
    m_mountedSlot = 0;
    m_authorityEpoch = 0;
    m_pendingMounts.clear();
}

bool VehicleSystem::HandleEntityUnload(const PacketEvent<server::NotifyEntityUnload>& aMessage)
{
    const auto serverId = aMessage.get_id();

    m_pendingMounts.erase(serverId);

    // The unloaded entity can also be a CHARACTER with a mount still queued for some
    // other, still-spawning vehicle. Replaying that mount later would hand DoMount a
    // despawned mirror - engine id 0 into EnterVehicle and a component add on a dead
    // flecs entity.
    for (auto it = m_pendingMounts.begin(); it != m_pendingMounts.end();)
    {
        auto& queue = it.value();
        std::erase_if(queue, [serverId](const PacketEvent<server::NotifyVehicleEnter>& aQueued)
                      { return aQueued.get_character_id() == serverId; });

        it = queue.empty() ? m_pendingMounts.erase(it) : std::next(it);
    }

    if (m_vehicleRemoteId && *m_vehicleRemoteId == serverId)
    {
        spdlog::info("[VehicleSystem] simulated vehicle {:x} unloaded by the server - dropping authority state", serverId);
        m_vehicleRemoteId = std::nullopt;
        m_vehicleGameId = std::nullopt;
    }

    if (m_mountedServerId && *m_mountedServerId == serverId)
        m_mountedServerId = std::nullopt;

    if (m_lastOwnVehicleServerId && *m_lastOwnVehicleServerId == serverId)
    {
        m_lastOwnVehicleServerId = std::nullopt;
        m_lastOwnVehicleGameId = std::nullopt;
    }

    return true;
}

void VehicleSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&VehicleSystem::HandleVehicleLoadMessage>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleVehicleEnterMessage>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleVehicleExitMessage>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleAuthorityAssigned>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleAuthorityRevoked>(this);
    pNetworkService->RegisterHandler<&VehicleSystem::HandleEntityUnload>(this);

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
        m_mountedServerId = serverVehicle;

        // Boarding a car somebody ELSE simulates: the engine's mount flow just woke this
        // copy's physics for the local player, undoing the kinematic state OnVehicleReady
        // set. Left that way, the passenger's copy simulates a parked car while the real
        // driver drives away on his own screen - "he's able to drive but I'm stationary".
        // Re-assert remote-driven; if the seat later comes with authority,
        // HandleAuthorityAssigned wakes the physics back up.
        if (!m_vehicleRemoteId || *m_vehicleRemoteId != static_cast<uint64_t>(serverVehicle))
        {
            MakeRemoteDriven(Red::Cast<Red::vehicle::WheeledBaseObject>(handle->GetEntity(aVehicle)));
        }
    }
    else if (m_lastOwnVehicleGameId && *m_lastOwnVehicleGameId == aVehicle && m_lastOwnVehicleServerId)
    {
        // Re-entering our own car, resolved from memory (see the field's comment: our
        // own car never has a mirror). It stays a LOCAL car - our physics, our input,
        // no MakeRemoteDriven - the server just needs to know this is the same entity
        // so the enter joins it (any seat) instead of forking a duplicate.
        request.set_remote_vehicle_id(*m_lastOwnVehicleServerId);

        m_vehicleGameId = aVehicle;
        m_mountedServerId = *m_lastOwnVehicleServerId;
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
        m_mountedServerId = std::nullopt;

        // The other half of the pairing (the server id) arrives with the authority
        // assignment this spawn triggers.
        m_lastOwnVehicleGameId = aVehicle;
        m_lastOwnVehicleServerId = std::nullopt;
    }

    m_mountedSlot = aName.hash;

    pNetworkService->Send(request);
}

void VehicleSystem::OnVehicleExit()
{
    // Name the mount this exit is about BEFORE forgetting it. For a car we spawned
    // ourselves the server id arrived via HandleAuthorityAssigned rather than at enter.
    const auto exitedVehicle = m_mountedServerId ? m_mountedServerId : m_vehicleRemoteId;
    const auto exitedSlot = m_mountedSlot;

    m_vehicleGameId = std::nullopt;
    m_vehicleRemoteId = std::nullopt;
    m_mountedServerId = std::nullopt;
    m_mountedSlot = 0;

    spdlog::info("[VehicleSystem] OnVehicleExit");
    const auto pNetworkService = Core::Container::Get<NetworkService>();
    if (!pNetworkService->IsConnected())
        return;

    client::ExitVehicleRequest request;

    const auto handle = Red::GetGameSystem<NetworkWorldSystem>();
    request.set_id(*handle->GetRemotePlayerId());

    if (exitedVehicle)
    {
        request.set_vehicle_id(*exitedVehicle);
        request.set_sit_id(exitedSlot);
    }

    pNetworkService->Send(request);
}

bool VehicleSystem::HandleVehicleLoadMessage(const PacketEvent<server::NotifyVehicleLoad>& aMessage)
{
    spdlog::info("[VehicleSystem] HandleVehicleLoadMessage");

    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto& settings = Core::Container::Get<NetworkService>()->GetServerSettings();
    const auto cellSize = settings.get_cell_size();
    const auto expectedCellX = cellSize
                                   ? static_cast<int32_t>(std::floor(aMessage.get_position().get_x() / static_cast<float>(cellSize)))
                                   : 0;
    const auto expectedCellY = cellSize
                                   ? static_cast<int32_t>(std::floor(aMessage.get_position().get_y() / static_cast<float>(cellSize)))
                                   : 0;
    if (cellSize == 0 || aMessage.get_world_revision() != 1 ||
        aMessage.get_cell_x() != expectedCellX || aMessage.get_cell_y() != expectedCellY)
    {
        spdlog::warn("[VehicleSystem] dropped map-invalid vehicle load {}", aMessage.get_id());
        return false;
    }

    // One server vehicle, one local copy.
    //
    // This handler used to call SpawnVehicle unconditionally, with nothing anywhere asking
    // whether we already had a car for this server id. The server sends a load alongside
    // the enter, so every time somebody got into a car - the same car, re-entered, or a car
    // re-replicated as it came back into range - another copy was spawned on top of the one
    // already standing there. That is the cars multiplying: they are real entities, each
    // with its own physics, stacked in the same parking space.
    //
    // The duplicates were also invisible to everything downstream. Only the newest copy is
    // recorded against the server id, so the earlier ones belong to nobody: they are never
    // moved, never mounted into, and never cleaned up, because no message can name them.
    //
    // Returning true, not false. We handled it correctly - the vehicle is present, which is
    // what the message asked for. False is for a load we could not honour.
    const auto existing = worldSystem->GetEntityByServerId(aMessage.get_id());

    if (existing && (existing.has<SpawningComponent>() || existing.has<EntityComponent>()))
    {
        spdlog::info("[VehicleSystem] HandleVehicleLoadMessage: already have vehicle {} - not spawning a duplicate",
                     aMessage.get_id());
        return true;
    }

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
    worldSystem->make_alive(aMessage.get_id()).emplace<SpawningComponent>(id, nullptr,
                                                                            aMessage.get_authority_epoch());

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
        if (entity && entity.has<EntityComponent>())
        {
            DoMount(character, entity.get<EntityComponent>()->Id, sit);
        }
        else
        {
            spdlog::info("[VehicleSystem] HandleVehicleEnterMessage: queueing mount for vehicle {:x}",
                         aMessage.get_vehicle_id());
            m_pendingMounts[aMessage.get_vehicle_id()].push_back(aMessage);
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

    auto mirror = worldSystem->FindEntity(aVehicleEntityId);
    if (!mirror)
    {
        spdlog::warn("[VehicleSystem] OnVehicleReady: no mirror for engine entity {:x} - it unloaded mid-spawn",
                     aVehicleEntityId.hash);
        return;
    }

    const uint64_t serverId = mirror;

    // Promote unconditionally, not only when a mount is queued: a copy that finished
    // spawning with nothing queued used to keep SpawningComponent forever, and stayed
    // half-resolvable to everything keyed on EntityComponent.
    mirror.emplace<EntityComponent>(aVehicleEntityId, true, nullptr);
    mirror.remove<SpawningComponent>();

    const auto pending = m_pendingMounts.find(serverId);
    if (pending != m_pendingMounts.end())
    {
        for (auto& message : pending->second)
        {
            const auto sit = Red::CName(message.get_sit_id());
            const auto character = worldSystem->GetEntityByServerId(message.get_character_id());

            // The character can die while its vehicle was still spawning (disconnect,
            // cell unload). HandleEntityUnload purges these, but the unload and this
            // callback can race - never mount a corpse.
            if (!character || !character.is_alive())
            {
                spdlog::warn("[VehicleSystem] OnVehicleReady: queued character {:x} no longer exists - mount dropped",
                             message.get_character_id());
                continue;
            }

            spdlog::info("[VehicleSystem] OnVehicleReady: mounting queued character {} into vehicle {}",
                         message.get_character_id(), message.get_vehicle_id());
            DoMount(character, aVehicleEntityId, sit);
            spdlog::info("[VehicleSystem] OnVehicleReady: mount done");
        }

        m_pendingMounts.erase(pending);
    }
}

bool VehicleSystem::HandleVehicleExitMessage(const PacketEvent<server::NotifyVehicleExit>& aMessage)
{
    // A client died ~3.5s after this handler's (then only) log line, everything past it
    // unlogged. Step logging plus the zero-id guard until that crash is named.
    spdlog::info("[VehicleSystem] HandleVehicleExitMessage: character {:x}", aMessage.get_character_id());

    const auto worldSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto character = worldSystem->GetEntityIdByServerId(aMessage.get_character_id());

    if (character.hash != 0)
    {
        const auto handle = Red::Handle(this);
        bool res;
        spdlog::info("[VehicleSystem] HandleVehicleExitMessage: ExitVehicle vcall");
        Red::Detail::CallFunctionWithArgs(m_pExitVehicle, handle, res, character);
        spdlog::info("[VehicleSystem] HandleVehicleExitMessage: ExitVehicle returned {}", res);
    }
    else
    {
        // A puppet this client never resolved has nothing to unmount - and handing the
        // engine a zero id here is a crash candidate, not a no-op.
        spdlog::warn("[VehicleSystem] HandleVehicleExitMessage: unknown character {:x} - unmount skipped",
                     aMessage.get_character_id());
    }

    auto characterEntity = worldSystem->GetEntityByServerId(aMessage.get_character_id());
    if (characterEntity && characterEntity.is_alive())
    {
        characterEntity.remove<AttachedComponent>();

        // The interpolation anchor is stale from the moment of mounting - lerping from
        // it after a drive would hurl the puppet across the map on the first frame.
        // Start fresh from the next movement sample.
        if (auto* pInterpolation = characterEntity.get_mut<InterpolationComponent>())
        {
            pInterpolation->TimePoints.clear();
            pInterpolation->HasPrevious = false;
            pInterpolation->LastRenderTick = 0.f;
        }

        // Exit grace for the idle-controller hook. The engine hands this puppet a
        // FRESH idle controller while it rebuilds the components post-exit, and the
        // hook attaching our multi controller into that teardown is the confirmed
        // 2026-08-20 23:31 crash (observer's log ends 2s after the attach line).
        // Four seconds: the live attach came 2s after "exit done", doubled for slow
        // frames. The hook forwards to vanilla idle during the window.
        if (const auto* pEntityComponent = characterEntity.get<EntityComponent>())
        {
            App::PuppetRegistry::SetExitGrace(pEntityComponent->Id.hash, 4000);
            spdlog::info("[VehicleSystem] puppet {:x} in exit grace for 4s - no controller attach during rebuild",
                         pEntityComponent->Id.hash);
        }

        // Driver-puppet stand-down. The engine spends the next several frames tearing
        // this puppet out of the vehicle and rebuilding its components; the driver
        // writing placed transforms and re-binding into that rebuild is the prime
        // suspect for every crash logged 0-15s after an exit tonight. Two seconds
        // matches the server's own post-exit grace.
        if (auto* pDriver = characterEntity.get_mut<DriverComponent>())
        {
            pDriver->SuppressUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            spdlog::info("[VehicleSystem] driver puppet {:x} standing down for 2s over component rebuild",
                         aMessage.get_character_id());
        }
    }

    spdlog::info("[VehicleSystem] HandleVehicleExitMessage: done");
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
    else
    {
        // Our own locally-entered car. m_vehicleGameId was already set in
        // OnVehicleEnter, and its physics never stopped being ours. This assignment is
        // also where we learn our own car's server id - complete the memory pairing.
        if (m_lastOwnVehicleGameId && !m_lastOwnVehicleServerId)
            m_lastOwnVehicleServerId = aMessage.get_entity_id();
    }

    return true;
}

bool VehicleSystem::HandleAuthorityRevoked(const PacketEvent<server::NotifyAuthorityRevoked>& aMessage)
{
    spdlog::info("[VehicleSystem] authority revoked: entity {:x}, epoch {}", aMessage.get_entity_id(),
                 aMessage.get_epoch());

    // Authority moving away also ends our claim to the own-car memory: resolving a
    // future enter against a car somebody else now simulates would put two machines
    // on one car's physics.
    if (m_lastOwnVehicleServerId && *m_lastOwnVehicleServerId == aMessage.get_entity_id())
    {
        m_lastOwnVehicleServerId = std::nullopt;
        m_lastOwnVehicleGameId = std::nullopt;
    }

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

    // A zero engine id means the character's mirror is gone (or never resolved) - the
    // engine's EnterVehicle on id 0 is a crash candidate, not a no-op.
    if (character.hash == 0)
    {
        spdlog::warn("[VehicleSystem] DoMount: character {:x} has no engine entity - mount dropped",
                     static_cast<uint64_t>(aCharacter));
        return;
    }

    spdlog::info("[VehicleSystem] DoMount: entering character {:x} into vehicle {:x} seat {} ({})",
                 character.hash, vehicle->id.hash, aSit.hash,
                 (m_vehicleGameId && *m_vehicleGameId == aVehicle) ? "OUR live car" : "network copy");
    Red::Detail::CallFunctionWithArgs(m_pEnterVehicle, handle, res, character, vehicle->id, aSit);
    spdlog::info("[VehicleSystem] DoMount: EnterVehicle returned {}", res);

    if (aCharacter && aCharacter.is_alive())
        aCharacter.add<AttachedComponent>();

    // Never our own car - the local player's vehicle keeps local physics and input.
    if (!m_vehicleGameId || *m_vehicleGameId != aVehicle)
    {
        MakeRemoteDriven(vehicle);
    }
}

