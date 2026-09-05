#include "InterpolationSystem.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_set>

#include <App/Settings.h>
#include <App/World/NetworkWorldSystem.h>

extern std::filesystem::path GCyberpunkMpLocation;

namespace
{
// Dev-only movement tracing for tools/netlab (-sync-trace). One NDJSON line per
// received sample ("in") and per applied pose ("out"), into the mod's logs folder so it
// ships to the server with the session logs - a far player's real network behaviour can
// then be replayed through candidate algorithms without asking them for anything. The
// "out" records are the trust anchor: replay.py --validate checks the lab's port of
// THIS file against what THIS file actually rendered, on the same input.
std::ofstream& SyncTraceFile()
{
    static std::ofstream file;
    static bool opened = false;
    if (!opened)
    {
        opened = true;
        std::error_code ec;
        std::filesystem::create_directories(GCyberpunkMpLocation / "logs", ec);
        file.open(GCyberpunkMpLocation / "logs" /
                  fmt::format("sync-trace-{}.ndjson", NetworkWorldSystem::GetTick()));
    }
    return file;
}

void SyncTraceIn(const uint64_t aId, const uint64_t aTick, const glm::vec3& aPos,
                 const glm::vec3& aRot, const float aSpeed, const bool aIsVehicle,
                 const uint32_t aWorldRevision, const int32_t aCellX, const int32_t aCellY,
                 const uint32_t aSequence, const uint32_t aAuthorityEpoch,
                 const bool aCorrection)
{
    if (!Settings::Get().syncTrace)
        return;

    SyncTraceFile() << fmt::format(
        R"({{"k":"in","id":"{:x}","tick":{},"tr":{} ,"p":[{:.3f},{:.3f},{:.3f}],"r":[{:.4f},{:.4f},{:.4f}],"v":{:.3f},"veh":{},"wr":{},"cx":{},"cy":{},"seq":{},"epoch":{},"corr":{}}})",
        aId, aTick, NetworkWorldSystem::GetTick(), aPos.x, aPos.y, aPos.z,
        aRot.x, aRot.y, aRot.z, aSpeed, aIsVehicle ? 1 : 0, aWorldRevision,
        aCellX, aCellY, aSequence, aAuthorityEpoch, aCorrection ? 1 : 0) << '\n';
}

void SyncTraceOut(const uint64_t aId, const int64_t aRenderTick, const glm::vec3& aPos)
{
    if (!Settings::Get().syncTrace)
        return;

    SyncTraceFile() << fmt::format(
        R"({{"k":"out","id":"{:x}","rt":{},"p":[{:.3f},{:.3f},{:.3f}]}})",
        aId, aRenderTick, aPos.x, aPos.y, aPos.z) << '\n';
}
} // namespace

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
                        const glm::vec3& aPosition, float aYaw, float aSpeed, uint32_t aLocomotion,
                        float aFrameDeltaMs)
{
    // Vehicle-exit grace. The engine rebuilds the puppet's components over several
    // frames after an unmount, and this path writing transforms plus re-binding into
    // that rebuild is the prime suspect for every 2026-08-19 exit crash. Stand down
    // completely until the deadline passes; the puppet holds still for the moment,
    // which beats a dead game.
    if (aDriver.SuppressUntil > std::chrono::steady_clock::now())
        return;

    const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();
    const auto entityHandle = pSystem->GetEntity(aEntityComponent.Id);

    // Every early-out logs ONCE per puppet: a frozen puppet must name its gate.
    if (!entityHandle)
    {
        if (aDriver.Driver && !aDriver.Driver->GateLogged)
        {
            aDriver.Driver->GateLogged = true;
            spdlog::warn("[Driver] puppet {:x} has no engine entity - cannot move it", aEntityComponent.Id.hash);
        }
        return;
    }

    if (!entityHandle->placedComponent)
    {
        if (aDriver.Driver && !aDriver.Driver->GateLogged)
        {
            aDriver.Driver->GateLogged = true;
            spdlog::warn("[Driver] puppet {:x} has no placedComponent - transform writes have nowhere to go",
                         aEntityComponent.Id.hash);
        }
        return;
    }

    Red::WorldTransform transform{};
    transform.Position = Red::WorldPosition(Red::Vector4{aPosition.x, aPosition.y, aPosition.z, 0.f});
    transform.Orientation = Game::ToRed(glm::quat(glm::vec3{0.f, 0.f, aYaw}));

    PlacedComponent_SetTransform(entityHandle->placedComponent, transform);

    if (aDriver.Driver)
    {
        if (!aDriver.Driver->FirstWriteLogged)
        {
            aDriver.Driver->FirstWriteLogged = true;
            spdlog::info("[Driver] puppet {:x} first transform write -> ({:.1f}, {:.1f}, {:.1f})",
                         aEntityComponent.Id.hash, aPosition.x, aPosition.y, aPosition.z);
        }

        // Mounts rebuild components; the driver re-binds itself (rate-limited) instead
        // of dying the way the engine-attached controller did.
        aDriver.Driver->EnsureAttached(entityHandle.GetPtr(), aEntityComponent.Id.hash);
        aDriver.Driver->Tick(aFrameDeltaMs * 0.001f, aSpeed, aLocomotion);
    }
}

// Traces a DRIVERLESS network vehicle through this function, to bisect the join crash.
//
// Scope is deliberately narrow. A car whose driver disconnected is the only entity class
// that reproduces the crash (docs/MAP.md: quit in a car, rejoin, dead ~1.6s after
// MakeRemoteDriven), and there is at most a handful in a world - so this is cheap, while
// tracing every remote player every frame would be thousands of lines a minute and would
// bury the one that matters.
//
// NOT behind a launch flag, on purpose. The last two attempts to capture this produced
// empty sessions: the redscript side logged through FTLog, which reaches no file anyone
// collects, and every surviving log came from a run where the flag was not passed. A
// diagnostic that has to be armed in advance is a diagnostic that is off when it matters.
//
// `aStage` names the point reached. The LAST stage printed before the log stops is the
// killer - the same bisection MakeRemoteDriven's own step logging describes.
static void TraceDriverless(flecs::entity aEntity, const EntityComponent& aEntityComponent,
                            InterpolationComponent& aInterpolation, int64_t aRenderTick,
                            const char* aStage)
{
    if (!aEntityComponent.IsVehicle || aEntity.has<DriverComponent>())
        return;

    // The first frames are where the crash lives, so they all print. After that it settles
    // into a heartbeat: enough to bracket the moment of death, not enough to drown the log.
    constexpr uint32_t cAlwaysFirst = 12;
    constexpr int64_t cHeartbeatMs = 250;

    const bool early = aInterpolation.TraceCount < cAlwaysFirst;
    if (!early && (aRenderTick - aInterpolation.LastTraceTick) < cHeartbeatMs)
        return;

    ++aInterpolation.TraceCount;
    aInterpolation.LastTraceTick = aRenderTick;

    spdlog::info("[OrphanVehicle] {:x} #{} {} - samples {}, prevTick {}, renderTick {}",
                 aEntityComponent.Id.hash, aInterpolation.TraceCount, aStage,
                 aInterpolation.TimePoints.size(), aInterpolation.PreviousFrame.Tick, aRenderTick);
}

void InterpolateEntity(flecs::entity aEntity, const EntityComponent& aEntityComponent, InterpolationComponent& aInterpolation, float aSimulationDelay, Red::vehicle::IMoveSystem* apMoveSystem)
{
    // Render time, in the integer domain the ticks actually live in.
    //
    // THIS LINE USED TO BE `const float tick = GetTick() - aSimulationDelay;` AND IT WAS
    // THE FREEZE.
    //
    // GetTick() returns milliseconds since the epoch as a uint64 - about 1.787e12. A
    // 32-bit float carries 24 bits of mantissa, so at that magnitude the gap between
    // consecutive representable values is 131072, or roughly 131 SECONDS. Converting the
    // tick to float threw away everything below that. Three consequences, all silent:
    //
    //   1. Subtracting a 100ms simulation delay did nothing whatsoever - the result was
    //      bit-identical to the input, so the interpolation buffer had no delay at all.
    //   2. In the drain loop below, a sample 30ms old and the current render time rounded
    //      to the SAME float, so `sample.Tick <= tick` was true for the whole buffer and
    //      it emptied every single frame.
    //   3. With the buffer always empty, the extrapolation branch computed
    //      `ahead = tick - first.Tick` as exactly 0.0, hit its own `ahead <= 0` guard,
    //      and returned without applying anything. Every frame. Forever.
    //
    // The remote puppet therefore stayed wherever it was last placed - its spawn point -
    // while 30 packets a second arrived, the clocks agreed to within 20ms, and the
    // appearance applied correctly. Everything looked healthy because everything WAS
    // healthy except this arithmetic. Verified against real ticks out of a session log:
    // float(1787286555346) and float(1787286555346 - 100) are the same number.
    //
    // The rule from here on: absolute ticks stay integer, and only DIFFERENCES - which
    // are small - are allowed to become float.
    const int64_t renderTick =
        static_cast<int64_t>(NetworkWorldSystem::GetTick()) - static_cast<int64_t>(aSimulationDelay);

    TraceDriverless(aEntity, aEntityComponent, aInterpolation, renderTick, "enter");

    // Advance the segment. Everything that is now behind render time becomes the anchor
    // we interpolate FROM; the first sample still ahead of it is what we interpolate TO.
    //
    // The old loop threw the past samples away entirely and then lerped from the last
    // drawn pose towards the target, which is a chase rather than an interpolation - the
    // fraction covered per frame grew as the target got closer, so every 100ms segment
    // was a small acceleration followed by a jump to the next one.
    while (!aInterpolation.TimePoints.empty() &&
           static_cast<int64_t>(aInterpolation.TimePoints.front().Tick) <= renderTick)
    {
        aInterpolation.PreviousFrame = aInterpolation.TimePoints.front();
        aInterpolation.HasPrevious = true;
        aInterpolation.TimePoints.pop_front();
    }

    // Nothing to work from yet - the puppet has spawned but no movement has arrived.
    if (!aInterpolation.HasPrevious)
        return;

    // The stub gate predates driver puppets and only means anything on the legacy
    // path - player-record entities may never get a stub at all, and this silently
    // freezing them was indistinguishable from every other freeze. Driver puppets
    // skip it; their own gates log.
    //
    // It now SAYS when it freezes something, which it never did before.
    //
    // On 19 Aug two players stood next to each other on foot and neither could see the
    // other move, with 30 packets a second arriving, full appearance applied and the
    // interpolation controller attached. This return was the obvious suspect: the only
    // path producing exactly that and leaving no trace.
    //
    // It was not the cause. With the return removed, a puppet ran twelve seconds of
    // interpolation and the warning below never fired once - so this gate was not being
    // reached. The return is therefore back exactly as it was, and the warning stays,
    // because a gate that freezes a puppet in silence is what made this cost an evening.
    //
    // Logged once per entity. This runs every frame for every remote player, so an
    // unconditional line would be thousands a minute and would bury what it reveals.
    if (!aEntity.has<DriverComponent>())
    {
        const auto pEntityStubSystem = Red::GetGameSystem<Red::game::IEntityStubSystem>();
        const auto* pStub = pEntityStubSystem->FindStub(aEntityComponent.Id);

        if (!pStub)
        {
            static std::unordered_set<uint32_t> reported;
            if (reported.insert(aEntityComponent.Id.hash).second)
            {
                spdlog::warn("[Interpolation] no entity stub for puppet {:x} - FREEZING here",
                             aEntityComponent.Id.hash);
            }

            // Behaviour restored to stock. Removing this return was a guess that the
            // measurement did not support: on 19 Aug a puppet ran twelve seconds of
            // interpolation with the return removed and the warning above never fired
            // once, which means this gate was not what froze it. The warning stays,
            // because the gate being silent is what made it a suspect for a whole
            // evening in the first place.
            return;
        }
    }

    if (aEntity.has<AttachedComponent>())
    {
        TraceDriverless(aEntity, aEntityComponent, aInterpolation, renderTick, "exit: attached");
        return;
    }

    const auto& first = aInterpolation.PreviousFrame;

    // Buffer starvation. A dropped or late packet leaves nothing ahead of render time,
    // and simply stopping there reads as a stutter every time the network hiccups.
    // Carrying on along the last known heading for a short while covers the gap; past
    // that the guess is worse than standing still, so it stops.
    if (aInterpolation.TimePoints.empty())
    {
        const float maxExtrapolationMs = aEntityComponent.IsVehicle ? 500.f : 250.f;

        // A difference, so float is safe here - this is milliseconds, not epoch time.
        const float ahead = static_cast<float>(renderTick - static_cast<int64_t>(first.Tick));
        if (ahead <= 0.f || ahead > maxExtrapolationMs)
        {
            TraceDriverless(aEntity, aEntityComponent, aInterpolation, renderTick,
                            "exit: extrapolation window closed");
            return;
        }

        // Velocity is metres per second and the tick is milliseconds.
        const glm::vec3 heading{-std::sin(first.Rotation.z), std::cos(first.Rotation.z), 0.f};
        const glm::vec3 guessed = first.Position + heading * (first.Velocity * ahead * 0.001f);

        aInterpolation.LastRenderedPosition = guessed;
        aInterpolation.HasLastRenderedPosition = true;
        aInterpolation.WasExtrapolating = true;

        SyncTraceOut(aEntity.id(), renderTick, guessed);

        if (aEntityComponent.IsVehicle)
        {
            const auto pSystem = Red::GetGameSystem<NetworkWorldSystem>();
            const auto vehicle = Red::Cast<Red::vehicle::BaseObject>(pSystem->GetEntity(aEntityComponent.Id));
            if (vehicle)
            {
                auto transform = Red::WorldTransform();
                transform.Position = Red::WorldPosition(Red::Vector4{guessed.x, guessed.y, guessed.z, 0.f});
                transform.Orientation = Game::ToRed(glm::quat(first.Rotation));

                static Core::RawFunc<2933986803UL, void (*)(Red::vehicle::BaseObject*, const Red::WorldTransform&, float)> ForceMoveTo;
                const float frameDelta = (aInterpolation.LastRenderTick > 0)
                                             ? static_cast<float>(std::max(
                                                   renderTick - aInterpolation.LastRenderTick, INT64_C(0)))
                                             : 16.f;
                TraceDriverless(aEntity, aEntityComponent, aInterpolation, renderTick,
                                "extrapolate: about to ForceMoveTo");
                ForceMoveTo(vehicle, transform, frameDelta);
                TraceDriverless(aEntity, aEntityComponent, aInterpolation, renderTick,
                                "extrapolate: ForceMoveTo survived");
                    aInterpolation.LastRenderTick = renderTick;
            }
        }
        else if (const auto* pDriver = aEntity.get<DriverComponent>())
        {
            const float frameDelta = (aInterpolation.LastRenderTick > 0)
                                         ? static_cast<float>(std::max(
                                               renderTick - aInterpolation.LastRenderTick, INT64_C(0)))
                                         : 16.f;
            DriveEntity(*pDriver, aEntityComponent, guessed, first.Rotation.z, first.Velocity,
                        first.Locomotion, frameDelta);
            aInterpolation.LastRenderTick = renderTick;
        }
        else if (aEntityComponent.Controller)
        {
            const auto pos = Red::Vector4{guessed.x, guessed.y, guessed.z, 0.f};
            aEntityComponent.Controller->SetTransform(pos, first.Rotation.z, first.Velocity, first.Locomotion);
        }
        return;
    }

    const auto& second = aInterpolation.TimePoints.front();

    // Where render time sits between the two samples, 0..1.
    // Both of these are differences between two ticks - tens of milliseconds - so they are
    // safe in float. Subtracting in the integer domain FIRST is the whole point: taking
    // float(second.Tick) - float(first.Tick) rounded both to the same value and produced a
    // delta of zero, which pinned ratio at 0 and left every segment sitting on its first
    // sample even when the buffer did have something to interpolate towards.
    auto ratio = 0.f;
    const auto tickDelta =
        static_cast<float>(static_cast<int64_t>(second.Tick) - static_cast<int64_t>(first.Tick));
    if (tickDelta > 0.f)
    {
        ratio = static_cast<float>(renderTick - static_cast<int64_t>(first.Tick)) / tickDelta;
        ratio = std::clamp(ratio, 0.f, 1.f);
    }

    glm::vec3 position{Lerp(first.Position, second.Position, ratio)};

    if (aInterpolation.WasExtrapolating &&
        aInterpolation.HasLastRenderedPosition)
    {
        const int64_t recoveryMs = aEntityComponent.IsVehicle ? 150 : 300;

        if (aInterpolation.RecoveryStartTick == 0)
        {
            aInterpolation.RecoveryFromPosition = aInterpolation.LastRenderedPosition;
            aInterpolation.RecoveryStartTick = renderTick;
        }

        const auto recoveryElapsed = renderTick - aInterpolation.RecoveryStartTick;
        const auto recoveryRatio = std::clamp(
            static_cast<float>(recoveryElapsed) / static_cast<float>(recoveryMs), 0.f, 1.f);
        position = Lerp(aInterpolation.RecoveryFromPosition, position, recoveryRatio);

        if (recoveryRatio >= 1.f)
        {
            aInterpolation.WasExtrapolating = false;
            aInterpolation.RecoveryStartTick = 0;
        }
    }

    if (aInterpolation.RecoveryStartTick != 0 &&
        aInterpolation.HasLastRenderedPosition)
    {
        const auto frameDelta = aInterpolation.LastRenderTick > 0
                                    ? std::max(renderTick - aInterpolation.LastRenderTick, INT64_C(1))
                                    : INT64_C(16);
        const auto maxStep = std::max(0.25f, second.Velocity * static_cast<float>(frameDelta) * 0.001f * 3.f);
        const auto correction = position - aInterpolation.LastRenderedPosition;
        const auto correctionDistance = glm::length(correction);
        if (correctionDistance > maxStep)
            position = aInterpolation.LastRenderedPosition + correction * (maxStep / correctionDistance);
    }

    aInterpolation.LastRenderedPosition = position;
    aInterpolation.HasLastRenderedPosition = true;

    SyncTraceOut(aEntity.id(), renderTick, position);

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

            TraceDriverless(aEntity, aEntityComponent, aInterpolation, renderTick,
                            "interpolate: about to SetSimpleMovement");
            SetSimpleMovement(apMoveSystem, aEntityComponent.Id, true);

            // modeled after DriveToPoint::OnUpdate_Internal, 3713079867
            if ((vehicle->physicsState & 0x100) == 0)
            {
                TraceDriverless(aEntity, aEntityComponent, aInterpolation, renderTick,
                                "interpolate: about to EnablePhysics");
                EnablePhysics(vehicle, 0x100, false);
            }

            // How long this move should take: the time since we last drew, not the time
            // since the last network sample. PreviousFrame used to be rewritten every
            // frame so the two were the same number; now that it holds a real sample,
            // the frame delta has to be tracked on its own.
            const float frameDelta = (aInterpolation.LastRenderTick > 0)
                                         ? static_cast<float>(std::max(
                                               renderTick - aInterpolation.LastRenderTick, INT64_C(0)))
                                         : 16.f;

            TraceDriverless(aEntity, aEntityComponent, aInterpolation, renderTick,
                            "interpolate: about to ForceMoveTo");
            ForceMoveTo(vehicle, transform, frameDelta);
            TraceDriverless(aEntity, aEntityComponent, aInterpolation, renderTick,
                            "interpolate: ForceMoveTo survived");
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
            const float frameDelta = (aInterpolation.LastRenderTick > 0)
                                         ? static_cast<float>(std::max(
                                               renderTick - aInterpolation.LastRenderTick, INT64_C(0)))
                                         : 16.f;
            // The band state comes from the sample AHEAD - it is where the mover is
            // heading, and it is the fresher of the two.
            DriveEntity(*pDriver, aEntityComponent, position, direction, speed, second.Locomotion, frameDelta);
        }
        else if (aEntityComponent.Controller)
        {
            // Legacy path: the engine-attached controller (NPC-record puppets).
            const auto pos = Red::Vector4{position.x, position.y, position.z, 0.f};
            aEntityComponent.Controller->SetTransform(pos, direction, speed, second.Locomotion);
        }
    }

    aInterpolation.LastRenderTick = renderTick;
}

void InterpolationSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{
    spdlog::info("[InterpolationSystem] OnWorldAttached");

    // The OnAdd observer that used to live here is gone. InterpolationComponent is now
    // added explicitly wherever EntityComponent is, which is only two places.
    //
    // What it was: observer<EntityComponent>().event(flecs::OnAdd) whose callback did
    // aEntity.add<InterpolationComponent>() - a structural change performed DURING OnAdd
    // dispatch, which moves the entity to a new archetype while flecs is still notifying
    // about the previous one.
    //
    // Why it was removed, on 27 August, while hunting a crash inside
    // flecs_stack_restore_cursor: it was the only remaining path in the client that
    // mutated during observer dispatch, and one of its two triggers runs UNDEFERRED.
    // VehicleSystem::OnVehicleReady is an RTTI_METHOD called from redscript, not from a
    // system, so ecs_emplace_id there takes its immediate branch - which ends with
    // flecs_defer_end(), flushing the command queue and dispatching this observer inline,
    // whereupon the callback made a further structural change. flecs's own error text in
    // that function warns about modifying components inside an OnAdd(T) observer.
    //
    // Adding it at the two emplace sites is equivalent and has none of that: no observer
    // iterator, no cursor, no mutation mid-dispatch. NetworkWorldSystem.cpp already did
    // exactly this for its own spawn path, so this makes the three sites consistent.
    //
    // NOT yet proven to be the crash. It is the last candidate standing after the page
    // and worker iterator paths, manual fini, the .run() leak and ten others were ruled
    // out with source evidence. If the crash survives this, instrument cursor
    // create/restore with a generation counter - and do NOT use FLECS_DEBUG, which
    // changes ecs_stack_cursor_t's layout.

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

    // Movement for an id nothing is registered under.
    //
    // The sixth silent return of the evening, and the last one on this path that could
    // produce a frozen puppet while every log looks healthy: packets arrive at 30/s, the
    // link is perfect, and every single one is dropped here because the id in the movement
    // message does not match the id anything was spawned under.
    //
    // THE WARNING USED TO SAY "this is a frozen remote player" AND THAT WAS WRONG - it cost
    // a real investigation. Server ids are a SINGLE FLAT NAMESPACE shared by every network
    // object (GetEntityByServerId is literally flecs::entity(*this, aServerId), so the
    // server id IS the entity id), and there is no kind tag to consult before the entity
    // exists. On 2026-09-04 every id this fired for turned out to be a VEHICLE, each one
    // followed moments later by "OnVehicleReady: mounting queued character ... into vehicle
    // N" - movement simply arrived before the spawn did, and the puppet system was innocent.
    // Diagnosing that as a frozen player sent someone into PuppetDriver and the appearance
    // path for an ordering race in the vehicle path.
    //
    // So it now reports what it actually knows and names the two candidates, in the order
    // they have actually occurred. Logged once per unknown id rather than 30 times a second,
    // so a mismatch is legible rather than a wall.
    if (!entity)
    {
        static std::unordered_set<uint64_t> reportedUnknown;

        if (reportedUnknown.insert(aMessage.get_id()).second)
        {
            spdlog::warn("[Interpolation] movement for id {} but nothing is registered under it - "
                         "the object is not spawned (yet). Ids are one namespace for players AND "
                         "vehicles, so this is NOT necessarily a player: look for a following "
                         "'OnVehicleReady' for id {} (an ordering race, harmless and self-correcting) "
                         "before suspecting a dropped character spawn (check for a '[Spawn]' line "
                         "for the same id, which is the case that leaves somebody frozen).",
                         aMessage.get_id(), aMessage.get_id());
        }

        return;
    }

    // Tick 0 is not a timestamp - it means "this player has not moved yet".
    //
    // MovementComponent::Tick is uninitialised on the server and only ever assigned when a
    // movement message arrives (Level.cpp). Between spawning and taking their first step, a
    // player is therefore replicated with Tick = 0, and that sample reaches everybody who
    // can see them.
    //
    // Downstream it is catastrophic rather than merely wrong. Our clock is milliseconds
    // since the epoch - about 1.787e12 - so a zero tick is a sample roughly fifty-seven
    // YEARS in the past. It anchors the interpolation buffer there, every subsequent real
    // sample is newer by an amount no smoothing can absorb, and the puppet stays where it
    // spawned. Kozzi's log shows exactly this: "their tick 0 ... DIFFERENT CLOCKS - this is
    // the freeze" on the first sample, then "same clock - fine" on every one after, and a
    // remote player who never appeared to move or to get into a car.
    //
    // Dropped rather than clamped. There is genuinely nothing to interpolate towards for
    // somebody who has not moved - they are already drawn at their spawn point - and the
    // first real sample arrives the moment they do.
    if (aMessage.get_tick() == 0)
        return;

    // Is the sender's clock the same clock we interpolate against?
    //
    // This is the one difference between a real remote player and the test dummy that has
    // never been eliminated. The dummy borrows the LOCAL player's tick, so its samples
    // always land just ahead of render time - and it walks perfectly. A real player stamps
    // packets with THEIR OWN SynchronizedClock.
    //
    // Both clocks track the server's, so in theory they agree within half a ping. If they
    // do not - if one client's clock never synchronised, or drifted - every sample from
    // that player is either permanently in the future (never played, puppet frozen at its
    // spawn point) or permanently in the past (popped instantly, buffer always empty,
    // puppet frozen at the last sample). Both look exactly like "remote players do not
    // move", which is what has been chased all evening.
    //
    // Logged once per entity, with the numbers rather than a verdict, because a guess
    // about which side is wrong is what has cost this the most time.
    {
        static std::unordered_set<uint64_t> reportedClocks;

        if (reportedClocks.insert(aMessage.get_id()).second)
        {
            const auto ours = NetworkWorldSystem::GetTick();
            const auto theirs = aMessage.get_tick();
            const auto delta = static_cast<int64_t>(theirs) - static_cast<int64_t>(ours);

            spdlog::info("[Clock] remote {} first sample: their tick {}, our tick {}, delta {}ms "
                         "({})",
                         aMessage.get_id(), theirs, ours, delta,
                         std::abs(delta) < 1000 ? "same clock - fine"
                                                : "DIFFERENT CLOCKS - this is the freeze");
        }
    }

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

    const auto& settings = Core::Container::Get<NetworkService>()->GetServerSettings();
    const auto* pEntityComponent = entity.get<EntityComponent>();
    const bool isVehicle = pEntityComponent && pEntityComponent->IsVehicle;
    const auto cellSize = settings.get_cell_size();
    const auto expectedCellX = cellSize
                                   ? static_cast<int32_t>(std::floor(position.x / static_cast<float>(cellSize)))
                                   : 0;
    const auto expectedCellY = cellSize
                                   ? static_cast<int32_t>(std::floor(position.y / static_cast<float>(cellSize)))
                                   : 0;

    if (settings.get_cell_size() == 0 ||
        aMessage.get_world_revision() != 1 ||
        aMessage.get_cell_x() != expectedCellX ||
        aMessage.get_cell_y() != expectedCellY)
    {
        spdlog::warn("[Interpolation] dropped map-invalid snapshot for {}: revision {}, cell ({}, {}), expected ({}, {})",
                     aMessage.get_id(), aMessage.get_world_revision(),
                     aMessage.get_cell_x(), aMessage.get_cell_y(), expectedCellX, expectedCellY);
        return;
    }

    if (pInterpolation->HasSequence &&
        aMessage.get_sequence() <= pInterpolation->LastSequence)
        return;

    if (isVehicle && pInterpolation->HasAuthorityEpoch &&
        aMessage.get_authority_epoch() < pInterpolation->LastAuthorityEpoch)
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

    pInterpolation->TimePoints.push_back(InterpolationComponent::Timepoint{
        position, rotation, aMessage.get_speed(), aMessage.get_tick(),
        aMessage.get_cell_x(), aMessage.get_cell_y(), aMessage.get_sequence(),
        aMessage.get_authority_epoch(), aMessage.get_correction(),
        aMessage.get_locomotion(), aMessage.get_upper_body()});
    pInterpolation->LastSequence = aMessage.get_sequence();
    pInterpolation->HasSequence = true;
    if (isVehicle)
    {
        pInterpolation->LastAuthorityEpoch = aMessage.get_authority_epoch();
        pInterpolation->HasAuthorityEpoch = true;
    }

    if (Settings::Get().syncTrace)
    {
        const auto* pEntityComponent = entity.get<EntityComponent>();
        SyncTraceIn(aMessage.get_id(), aMessage.get_tick(), position, rotation,
                    aMessage.get_speed(), pEntityComponent && pEntityComponent->IsVehicle,
                    aMessage.get_world_revision(), aMessage.get_cell_x(), aMessage.get_cell_y(),
                    aMessage.get_sequence(), aMessage.get_authority_epoch(),
                    aMessage.get_correction());
    }
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

            // A vehicle exit rebuilds this puppet's components over the next several
            // frames, and the engine hands it a fresh idle controller mid-teardown.
            // Attaching ours into that window killed the observing client on
            // 2026-08-20 (log ends 2s after the attach line below, 16s before the
            // connection died). While the exit is being digested, the vanilla idle
            // runs instead - engine code on an engine controller - and the multi
            // controller re-attaches on the first frame after the grace lapses.
            if (App::PuppetRegistry::InExitGrace(pOwner->id.hash))
            {
                RealIdleController_SetAnimation(apController, data);
                return;
            }

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
