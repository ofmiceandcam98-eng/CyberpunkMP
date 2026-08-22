#pragma once

struct MovementComponent
{
    // All four were uninitialised, and Tick is only ever assigned when a movement message
    // arrives (Level.cpp). A player who has spawned but not yet moved was therefore
    // replicated with Tick = 0, which every receiving client read as a timestamp fifty-odd
    // years in the past - the remote player froze at their spawn point and never appeared
    // to move or to enter a vehicle. The client now ignores a zero tick as well; this is
    // the other half, so the value is at least deterministic rather than whatever the
    // allocation happened to contain.
    glm::vec3 Position{0.f, 0.f, 0.f};
    glm::vec3 Rotation{0.f, 0.f, 0.f};
    float Velocity{0.f};
    uint64_t Tick{0};

    // The mover's PlayerStateMachine states (gamePSMLocomotionStates /
    // gamePSMUpperBodyStates), relayed verbatim so receivers animate what the sender
    // is doing rather than inferring it from speed. Zero means Default/unknown.
    uint32_t Locomotion{0};
    uint32_t UpperBody{0};

    // Counts updates for this entity, so replication can send only every Nth one to
    // players who are far away. A plain counter rather than a timestamp: it is the same
    // for every recipient, so no per-pair state has to be kept anywhere.
    uint32_t Sequence{0};

    static void Register(flecs::world& aWorld);
};