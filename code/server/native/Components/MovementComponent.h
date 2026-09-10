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

    // Counts updates RECEIVED for this entity. Incremented in Level::HandleMoveEntityRequest
    // once per accepted packet.
    //
    // This used to be the number put on the wire and fed to the distance LOD, and that was
    // a vulnerability: the LOD divisors (% 4, % 16) reduce how often distant players are
    // told about you, but keyed on a counter the SENDER advances, a client sending ten
    // times as fast simply passes the filter ten times as often. The divisors cut the
    // constant, not the growth. See ReplicatedSequence.
    uint32_t Sequence{0};

    /**
     * Counts times this entity was REPLICATED, which is what goes on the wire.
     *
     * Two different questions were being answered by one counter:
     *   - the client asks "is this state newer than the last one I applied"
     *     (InterpolationSystem.cpp:721 drops anything <= LastSequence)
     *   - the server asks "is this the Nth update, so should a distant player get it"
     *
     * The first only needs the number to increase. The second needs it to advance at the
     * rate the SERVER replicates, not the rate a client transmits - otherwise the
     * reduction it exists to provide can be defeated by sending faster.
     *
     * Incremented once per replication, so it is correct for both. With coalescing off,
     * replication happens per packet and this increments per packet, which is exactly the
     * behaviour that shipped. With it on, it advances once per replication tick and the
     * divisors mean what they were written to mean.
     */
    uint32_t ReplicatedSequence{0};

    /**
     * Has state arrived that has not been replicated yet?
     *
     * The whole of coalescing, in one flag. A movement packet sets the component and marks
     * this; the replication pass finds it, sends ONE update carrying the newest state, and
     * clears it. Five packets between ticks therefore cost one relevance walk instead of
     * five.
     *
     * DELIBERATELY A FLAG AND NOT A QUEUE. A queue of pending movement would grow with
     * incoming traffic, which is the amplification moved rather than removed - memory
     * instead of CPU. Latest state, not packet history: O(players), never O(packets).
     */
    bool ReplicationPending{false};

    static void Register(flecs::world& aWorld);
};

/**
 * Send this entity's current movement to everyone who should see it.
 *
 * Declared here because there are now two callers: the OnSet observer, which runs it once
 * per received packet, and GameServer::ReplicatePendingMovement, which runs it once per
 * tick for entities with pending state. Which of the two is live depends on
 * Config::CoalesceMovement.
 *
 * Clears ReplicationPending and advances ReplicatedSequence itself, so neither caller has
 * to remember to.
 */
void ReplicateMovementComponent(flecs::entity aEntity, const MovementComponent& aComponent);