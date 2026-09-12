#pragma once

#include <glm/glm.hpp>

#include "AnimationDriver.h"
#include "States/Base.h"

// A puppet's movement-and-animation driver that the ENGINE never owns.
//
// The old path handed the engine a replacement idle controller and depended on the
// engine calling it - which only ever happens for NPC-record puppets, and stops the
// moment a vehicle mount rebuilds the components. This object is plain mod state:
// ticked from the interpolation pass on the main thread, writing animation parameters
// through AnimationDriver (component-level, record-agnostic) while position is written
// directly to the placed transform. Nothing here can be torn off by the engine; a
// rebuilt component is detected and re-attached, never lost.
struct PuppetDriver final : States::ILocomotionHost
{
    static inline Red::CName NAME{"Multiplayer Driver"};

    // Binds the animation writer to the entity's AnimationControllerComponent and
    // starts the state machine. Safe to call again after the engine rebuilds the
    // component (vehicle mounts do) - rate-limited to once a second.
    void EnsureAttached(Red::Entity* apEntity, uint64_t aServerId);

    void Detach();

    // One frame: advance the locomotion state machine and push its parameters.
    // aLocomotion is the sender's gamePSMLocomotionStates value from the wire.
    void Tick(float aDeltaSeconds, float aSpeed, uint32_t aLocomotion = 0);

    float GetAnimLength(Red::CName aName) const override;
    float GetCurrentSpeed() const override { return m_speed; }

    // One-shot diagnostics for the movement path - which gate blocked, or proof the
    // first transform write happened. Public: written by the interpolation pass.
    bool FirstWriteLogged{false};
    bool GateLogged{false};

    // Exit-grace ease, written by the interpolation pass (DriveEntity). Lives here rather
    // than on DriverComponent for the same reason the flags above do: DriveEntity receives
    // that component by const ref, and the shared_ptr to this object stays mutable through
    // it. WasSuppressed marks that the puppet was frozen in the vehicle-exit grace; the
    // first frame past the grace then eases from LastPosition (where it was frozen) toward
    // the live position until ExitEaseUntil, instead of teleporting the caught-up distance.
    glm::vec3 LastPosition{};
    bool HasLastPosition{false};
    glm::vec3 ExitEaseFrom{};
    std::chrono::steady_clock::time_point ExitEaseUntil{};
    bool WasSuppressed{false};

private:
    AnimationDriver m_animationDriver;
    UniquePtr<States::Base> m_pState;
    float m_speed{0.f};
    bool m_everAttached{false};
    std::chrono::steady_clock::time_point m_lastAttachTry{};
};
