#include "InterpolationSystem.h"

#include <algorithm>
#include <cmath>

#include <App/Components/AttachedComponent.h>
#include <App/Threading/ThreadService.h>
#include <App/World/PuppetRegistry.h>

#include "App/Network/NetworkService.h"
#include "RED4ext/Scripting/Natives/Generated/game/EntityStubComponentPS.hpp"
#include "RED4ext/Scripting/Natives/Generated/move/Component.hpp"
#include "RED4ext/Scripting/Natives/Generated/ent/IPlacedComponent.hpp"
#include "RED4ext/Scripting/Natives/Generated/ent/AnimationControllerComponent.hpp"
#include <RED4ext/Scripting/Natives/Generated/vehicle/MoveSystem.hpp>
#include "RED4ext/Scripting/Natives/Generated/vehicle/BaseObject.hpp"
#include "RED4ext/Scripting/Natives/Generated/WorldPosition.hpp"
#include "RED4ext/Scripting/Natives/Vector3.hpp"

#include "Core/Hooking/HookingAgent.hpp"
#include "NetworkWorldSystem.h"
#include "App/Components/EntityComponent.h"
#include "App/Components/DriverComponent.h"
#include "Math/Math.h"
#include "App/Components/InterpolationComponent.h"
#include "App/Components/SpawningComponent.h"
#include "Game/Utils.h"
#include "Game/Movement.h"
#include "Game/Animation/AnimationData.h"
#include "Game/Animation/MultiMovementController.h"

#include <Math/Spline.h>

inline void SetSimpleMovement(Red::vehicle::IMoveSystem* apMoveSystem, const Red::EntityID& aEntityId, bool enabled)
{
    reinterpret_cast<void (*)(Red::vehicle::IMoveSystem*, const Red::EntityID&, bool)>(*(uintptr_t*)(*(uintptr_t*)apMoveSystem + 0x1F0))(apMoveSystem, aEntityId, enabled);
}

// The driver path's movement primitive: write the placed transform directly - the
// exact native Codeware's Entity.SetWorldTransform calls. No move controller, no
// engine pipeline, works on any record type.
static Core::RawFunc<1828854026UL, void (*)(Red::IPlacedComponent*, const Red::WorldTransform&)>
    PlacedComponent_SetTransform;

// Place a driver-puppet and advance its animation for this frame.
static void DriveEntity(const DriverComponent& aDriver, const EntityComponent& aEntityComponent,
                        const glm::vec3& aPosition, float aYaw, float aSpeed, float aFrameDeltaMs)
{
    const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto entityHandle = pSystem->GetEntity(aEntityComponent.Id);
    if (!entityHandle || !entityHandle->placedComponent)
        return;

    Red::WorldTransform transform{};
    transform.Position = Red::WorldPosition(Red::Vector4{aPosition.x, aPosition.y, aPosition.z, 0.f});
    transform.Orientation = Game::ToRed(glm::quat(glm::vec3{0.f, 0.f, aYaw}));

    PlacedComponent_SetTransform(entityHandle->placedComponent, transform);

    if (aDriver.Driver)
    {
        // Mounts rebuild components; the driver re-binds itself (rate-limited) instead
        // of dying the way the engine-attached controller did.
        aDriver.Driver->EnsureAttached(entityHandle.GetPtr(), aEntityComponent.Id.hash);
        aDriver.Driver->Tick(aFrameDeltaMs * 0.001f, aSpeed);
    }
}

void InterpolateEntity(flecs::entity aEntity, const EntityComponent& aEntityComponent, InterpolationComponent& aInterpolation, float aSimulationDelay, Red::vehicle::IMoveSystem* apMoveSystem)
{
    const float tick = NetworkWorldSystem::GetTick() - aSimulationDelay;

    // Advance the segment. Everything that is now behind render time becomes the anchor
    // we interpolate FROM; the first sample still ahead of it is what we interpolate TO.
    //
    // The old loop threw the past samples away entirely and then lerped from the last
    // drawn pose towards the target, which is a chase rather than an interpolation - the
    // fraction covered per frame grew as the target got closer, so every 100ms segment
    // was a small acceleration followed by a jump to the next one.
    while (!aInterpolation.TimePoints.empty() &&
           static_cast<float>(aInterpolation.TimePoints.front().Tick) <= tick)
    {
        aInterpolation.PreviousFrame = aInterpolation.TimePoints.front();
        aInterpolation.HasPrevious = true;
        aInterpolation.TimePoints.pop_front();
    }

    // Nothing to work from yet - the puppet has spawned but no movement has arrived.
    if (!aInterpolation.HasPrevious)
        return;

    const auto pEntityStubSystem = Red::GetGameSystem<Red::game::IEntityStubSystem>();
    const auto* pStub = pEntityStubSystem->FindStub(aEntityComponent.Id);
    if (!pStub)
        return;

    if (aEntity.has<AttachedComponent>())
        return;

    const auto& first = aInterpolation.PreviousFrame;

    // Buffer starvation. A dropped or late packet leaves nothing ahead of render time,
    // and simply stopping there reads as a stutter every time the network hiccups.
    // Carrying on along the last known heading for a short while covers the gap; past
    // that the guess is worse than standing still, so it stops.
    if (aInterpolation.TimePoints.empty())
    {
        constexpr float kMaxExtrapolationMs = 250.f;

        const float ahead = tick - static_cast<float>(first.Tick);
        if (ahead <= 0.f || ahead > kMaxExtrapolationMs)
            return;

        if (aEntityComponent.IsVehicle)
            return;

        // Velocity is metres per second and the tick is milliseconds.
        const glm::vec3 heading{-std::sin(first.Rotation.z), std::cos(first.Rotation.z), 0.f};
        const glm::vec3 guessed = first.Position + heading * (first.Velocity * ahead * 0.001f);

        if (const auto* pDriver = aEntity.get<DriverComponent>())
        {
            const float frameDelta = (aInterpolation.LastRenderTick > 0.f)
                                         ? std::max(tick - aInterpolation.LastRenderTick, 0.f)
                                         : 16.f;
            DriveEntity(*pDriver, aEntityComponent, guessed, first.Rotation.z, first.Velocity, frameDelta);
            aInterpolation.LastRenderTick = tick;
        }
        else if (aEntityComponent.Controller)
        {
            const auto pos = Red::Vector4{guessed.x, guessed.y, guessed.z, 0.f};
            aEntityComponent.Controller->SetTransform(pos, first.Rotation.z, first.Velocity);
        }
        return;
    }

    const auto& second = aInterpolation.TimePoints.front();

    // Where render time sits between the two samples, 0..1.
    auto ratio = 0.f;
    const auto tickDelta = static_cast<float>(second.Tick) - static_cast<float>(first.Tick);
    if (tickDelta > 0.f)
    {
        ratio = (tick - static_cast<float>(first.Tick)) / tickDelta;
        ratio = std::clamp(ratio, 0.f, 1.f);
    }

    const glm::vec3 position{Lerp(first.Position, second.Position, ratio)};

    if (aEntityComponent.IsVehicle)
    {
        const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();
        const auto vehicle = Red::Cast<Red::vehicle::BaseObject>(pSystem->GetEntity(aEntityComponent.Id));
        if (vehicle)
        {
            const auto deltaAngleX{DeltaAngle(first.Rotation.x, second.Rotation.x, true) * ratio};
            const auto directionX = Mod(first.Rotation.x + deltaAngleX, 2.f * static_cast<float>(Pi));

            const auto deltaAngleY{DeltaAngle(first.Rotation.y, second.Rotation.y, true) * ratio};
            const auto directionY = Mod(first.Rotation.y + deltaAngleY, 2.f * static_cast<float>(Pi));

            const auto deltaAngleZ{DeltaAngle(first.Rotation.z, second.Rotation.z, true) * ratio};
            const auto directionZ = Mod(first.Rotation.z + deltaAngleZ, 2.f * static_cast<float>(Pi));

            const auto rot = glm::vec3(directionX, directionY, directionZ);

            auto transform = Red::WorldTransform();
            transform.Position = Red::WorldPosition(Red::Vector4{position.x, position.y, position.z, 0.f});
            transform.Orientation = Game::ToRed(glm::quat(rot));

            static Core::RawFunc<2933986803UL, void (*)(Red::vehicle::BaseObject*, const Red::WorldTransform&, float)> ForceMoveTo;
            static Core::RawFunc<457775029UL, void (*)(Red::vehicle::BaseObject*, const Red::WorldTransform&, bool)> ForceTransform;
            static Core::RawFunc<2592348558UL, void (*)(Red::vehicle::BaseObject*, uint32_t, bool)> EnablePhysics;

            SetSimpleMovement(apMoveSystem, aEntityComponent.Id, true);

            // modeled after DriveToPoint::OnUpdate_Internal, 3713079867
            if ((vehicle->physicsState & 0x100) == 0)
            {
                EnablePhysics(vehicle, 0x100, false);
            }

            // How long this move should take: the time since we last drew, not the time
            // since the last network sample. PreviousFrame used to be rewritten every
            // frame so the two were the same number; now that it holds a real sample,
            // the frame delta has to be tracked on its own.
            const float frameDelta = (aInterpolation.LastRenderTick > 0.f)
                                         ? std::max(tick - aInterpolation.LastRenderTick, 0.f)
                                         : 16.f;

            ForceMoveTo(vehicle, transform, frameDelta);
            //ForceTransform(vehicle, transform, true);
        }
    }
    else
    {
        const auto speed{Lerp(first.Velocity, second.Velocity, ratio)};
        const auto deltaAngle{DeltaAngle(first.Rotation.z, second.Rotation.z, true) * ratio};
        const auto direction = Mod(first.Rotation.z + deltaAngle, 2.f * static_cast<float>(Pi));

        if (const auto* pDriver = aEntity.get<DriverComponent>())
        {
            // Driver path FIRST - it outranks any controller a hook race may have
            // attached before the suppressor registered this puppet as driver-owned.
            const float frameDelta = (aInterpolation.LastRenderTick > 0.f)
                                         ? std::max(tick - aInterpolation.LastRenderTick, 0.f)
                                         : 16.f;
            DriveEntity(*pDriver, aEntityComponent, position, direction, speed, frameDelta);
        }
        else if (aEntityComponent.Controller)
        {
            // Legacy path: the engine-attached controller (NPC-record puppets).
            const auto pos = Red::Vector4{position.x, position.y, position.z, 0.f};
            aEntityComponent.Controller->SetTransform(pos, direction, speed);
        }
    }

    aInterpolation.LastRenderTick = tick;
}

void InterpolationSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{
    spdlog::info("[InterpolationSystem] OnWorldAttached");

    static std::once_flag s_flag;
    std::call_once(s_flag, [this]()
    {
        const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();

        m_entityObserver = pSystem->observer<EntityComponent>()
           .event(flecs::OnAdd)
           .write<InterpolationComponent>()
           .each([](flecs::entity aEntity, EntityComponent&)
           {
               aEntity.add<InterpolationComponent>();
           });
    });

    m_ready = true;
}

void InterpolationSystem::OnAfterWorldDetach()
{
    spdlog::info("[InterpolationSystem] OnAfterWorldDetach");

    m_ready = false;
}

void InterpolationSystem::OnConnected()
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();

    const float cSimulationDelay = 50.f + (1500.f / pNetworkService->GetServerSettings().get_update_rate());

    const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto pMoveSystem = Red::GetGameSystem<RED4ext::vehicle::IMoveSystem>();

    m_interpolator = pSystem->system<const EntityComponent, InterpolationComponent>().each(
        [cSimulationDelay, pMoveSystem](flecs::entity aEntity, const EntityComponent& aEntityComponent, InterpolationComponent& aInterpolation)
        {
            InterpolateEntity(aEntity, aEntityComponent, aInterpolation, cSimulationDelay, pMoveSystem);
        });
}

void InterpolationSystem::OnDisconnected()
{
    if (m_interpolator)
        m_interpolator.destruct();
}

void InterpolationSystem::HandleNotifyEntityMove(const PacketEvent<server::NotifyEntityMove>& aMessage)
{
    const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto entity = pSystem->GetEntityByServerId(aMessage.get_id());

    const glm::vec3 position{aMessage.get_position().get_x(), aMessage.get_position().get_y(), aMessage.get_position().get_z()};
    glm::vec3 rotation{0.f, 0.f, 0.f};

    if (aMessage.has_full_rotation())
        rotation = {aMessage.get_full_rotation().get_x(), aMessage.get_full_rotation().get_y(), aMessage.get_full_rotation().get_z()};
    else
        rotation = {0.f, 0.f, aMessage.get_rotation()};

    if (!entity)
        return;

    auto* pInterpolation = entity.get_mut<InterpolationComponent>();

    // get_mut returns NULL when the entity does not have the component yet, and this was
    // dereferenced unconditionally.
    //
    // InterpolationComponent is added by an observer on EntityComponent, which is only
    // attached once the promotion poll runs - up to 200ms after the puppet spawns. A real
    // player is sending movement the entire time, so their first update reliably lands
    // inside that window and dereferenced a null pointer: an access violation with no
    // crash report, a few milliseconds after the spawn completed.
    //
    // It never reproduced with a fabricated test player because those never move.
    if (!pInterpolation)
        return;

    // Drop anything that arrives out of order. The buffer is usually non-empty so its
    // last sample is the newest thing we hold, but once it drains the newest thing we
    // hold is the anchor - and without that second check a late packet could reopen a
    // segment we have already walked past and drag the puppet backwards.
    if (!pInterpolation->TimePoints.empty())
    {
        if (pInterpolation->TimePoints.back().Tick > aMessage.get_tick())
            return;
    }
    else if (pInterpolation->HasPrevious && pInterpolation->PreviousFrame.Tick > aMessage.get_tick())
    {
        return;
    }

    pInterpolation->TimePoints.push_back(InterpolationComponent::Timepoint{position, rotation, aMessage.get_speed(), aMessage.get_tick()});
}

static Core::RawFunc<4018412273UL, float (*)(Red::move::Component*, MultiMovementController*)> AttachController;

void (*RealIdleController_SetAnimation)(Game::Controller*, AnimationData&);
void HookIdleController_SetAnimation(Game::Controller* apController, AnimationData& data)
{
    auto* pMoveComponent = apController->MoveComponent;
    Game::EntityPtr entity(pMoveComponent);
    if (const auto pOwner = Red::Cast<Red::GameObject>(entity.GetValuePtr()))
    {
        // This hook runs on the game's ANIMATION thread. Reading pOwner->tags here
        // walks a heap DynArray on an entity the main thread may still be building,
        // which raced and crashed the game shortly after a remote player spawned.
        // PuppetRegistry only touches the entity's 64-bit id.
        if (App::PuppetRegistry::Contains(pOwner->id.hash))
        {
            // Driver puppets are moved and animated by the PuppetDriver from the main
            // thread. This hook attaching its legacy controller to them OUTRANKED the
            // driver (interpolation preferred a non-null Controller) and froze every
            // driver puppet solid - the 2026-08-19 live failure. Total silence for
            // them: no attach, and no vanilla idle writes either.
            if (App::PuppetRegistry::IsDriver(pOwner->id.hash))
                return;

            if (apController->m_type == MultiMovementController::kMulti)
                return;

            // Reaching here means the engine handed this puppet a FRESH idle controller.
            // At spawn that is expected (it is how the multi controller gets attached);
            // any later sighting means the engine tore our controller off - the vehicle
            // mount pipeline is the suspect - and this timestamp against the mount line
            // in the log is the evidence.
            spdlog::info("[Interpolation] idle controller (re)entered for puppet {:x} - attaching multi controller", pOwner->id.hash);

            // The kMulti guard above is evaluated on the ANIMATION thread while the attach
            // below happens later on the MAIN thread, so every animation frame in that gap
            // queues another attach for the same move component. Harmless - the second one
            // finds the controller already multi - but it is why the same entity shows up
            // several times in a row in the logs.
            // ONLY the id crosses the thread boundary. This lambda used to capture the
            // animation thread's raw component pointer and attach through it later on
            // the main thread - but the engine rebuilds a puppet's components during a
            // vehicle mount, in exactly the window where mounts tear our controller off
            // and queue this re-attach. Attaching through the freed pointer was a
            // use-after-free with no log line: the observing client died seconds after
            // a remote car materialized with its driver, twice, on the faster machine
            // only. Everything is re-resolved from the id once we are ON the main
            // thread, where the entity cannot be rebuilt out from under us.
            ThreadService::RunInMainThread([id = pOwner->id]
            {
                const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();

                const auto entityHandle = pSystem->GetEntity(id);
                if (!entityHandle)
                {
                    spdlog::warn("[Interpolation] puppet {:x} vanished before its controller attach - skipped", id.hash);
                    return;
                }

                Red::Handle<Red::move::Component> moveComponent;
                for (const auto& component : entityHandle->componentsStorage.components)
                {
                    if (auto casted = Red::Cast<Red::move::Component>(component))
                    {
                        moveComponent = casted;
                        break;
                    }
                }

                if (!moveComponent)
                {
                    spdlog::warn("[Interpolation] puppet {:x} has no move component right now - attach skipped", id.hash);
                    return;
                }

                const auto entityQuery = pSystem->query<const EntityComponent>();
                auto flecsEntity = entityQuery.find([id](const EntityComponent& component) { return component.Id == id; });

                auto* pController = Red::Memory::New<MultiMovementController>();

                AttachController(moveComponent.instance, pController);

                if (flecsEntity)
                {
                    flecsEntity.get_mut<EntityComponent>()->Controller = pController;
                }
                else
                {
                    const auto spawnQuery = pSystem->query<const SpawningComponent>();
                    flecsEntity = spawnQuery.find([id](const SpawningComponent& component) { return component.Id == id; });
                    if (flecsEntity)
                    {
                        flecsEntity.get_mut<SpawningComponent>()->Controller = pController;
                    }
                }
            });

            return;
        }
    }

    RealIdleController_SetAnimation(apController, data);
}


void InterpolationSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    Hook<Game::IdleController_SetAnimation>(&HookIdleController_SetAnimation, &RealIdleController_SetAnimation);

    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&InterpolationSystem::HandleNotifyEntityMove>(this);
}
