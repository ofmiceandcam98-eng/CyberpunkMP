#pragma once

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

private:
    AnimationDriver m_animationDriver;
    UniquePtr<States::Base> m_pState;
    float m_speed{0.f};
    bool m_everAttached{false};
    std::chrono::steady_clock::time_point m_lastAttachTry{};
};
