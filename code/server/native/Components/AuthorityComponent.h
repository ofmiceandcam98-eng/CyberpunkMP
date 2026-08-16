#pragma once

/**
 * Which grant of simulation rights over this entity is current.
 *
 * WHO owns an entity is not stored here - it is the entity's parentage, as it always has
 * been, and HandleMoveEntityRequest already refuses movement from anyone but the parent's
 * connection. What parentage cannot express is ORDER across a handoff: MoveEntityRequest
 * is unreliable, so packets from the previous simulator can arrive after a transfer. The
 * epoch names the grant; movement stamped with a stale one is dropped.
 *
 * Only entities that can change hands carry this - vehicles today. Player puppets never
 * transfer, so they never need it, and the epoch check simply does not apply to them.
 */
struct AuthorityComponent
{
    uint32_t Epoch{0};

    static void Register(flecs::world& aWorld);
};

/**
 * Moves simulation of an entity to another player's client.
 *
 * Does the four things a handoff needs, in the order that keeps exactly one simulator
 * alive at all times: bump the epoch (only when the owner actually changes - a
 * re-announcement to the same owner must not invalidate movement already in flight),
 * reparent, tell the OLD owner to stop, tell the NEW owner to start. Everyone else hears
 * nothing - they were interpolating and keep interpolating.
 *
 * Passing a null player parks the entity server-owned: nobody simulates, nobody may move
 * it, and it freezes at its last replicated state until somebody takes it over.
 */
void TransferAuthority(flecs::entity aEntity, flecs::entity aNewOwnerPlayer);
