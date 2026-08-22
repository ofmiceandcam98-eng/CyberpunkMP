#pragma once

// What a combatant has left, owned by the server.
//
// The FIRST piece of gameplay state this server owns outright. Position is relayed - a
// client says where it is and the server passes it on - and appearance is stored but never
// judged. Health is different: if a client can assert its own health, it can assert that it
// still has some, and every other rule in the combat system is decoration on top of that.
//
// Attached to the PUPPET, not the player. A puppet is what players and server-declared NPCs
// both are, so Player -> NPC, NPC -> Player and NPC -> NPC all resolve against the same
// component with the same code. That is the brief's Combatant abstraction, and it costs
// nothing here because the entity layout already worked this way.
struct HealthComponent
{
    // Cyberpunk's health pool is 0..100 as a percentage, which is what the client reports
    // and what every existing script in this project already assumes (see MpDeathFloor).
    // Kept in the same units so nothing has to convert, and so a mismatch is visible rather
    // than silently scaled.
    float Health{100.f};
    float MaxHealth{100.f};

    // 0 alive, 1 downed, 2 dead, 3 reviving. Mirrors NotifyCombatState.life_state.
    //
    // Downed exists as its own state rather than "dead but revivable" because the client
    // currently prevents death entirely - Death.reds applies the engine's Immortal flag so
    // the death menu can never open, since its only option rebuilds the world and ends the
    // session for everyone watching. Replacing that hack means the server has to have
    // somewhere to put "should be down but is not gone".
    uint32_t LifeState{0};

    // The last combat sequence accepted from this entity's owner.
    //
    // Duplicate and replayed events are rejected by comparing against this rather than by
    // remembering every event id: a burst of fire is a rising sequence, a replay is not.
    // Wraps, so only differences between neighbours are ever compared - the same rule the
    // voice frames use.
    uint32_t LastCombatSequence{0};
    bool HasCombatSequence{false};

    // When this combatant was last allowed to damage anybody, as a server timestamp.
    // Fire-rate validation needs a floor to measure from, and it has to be the SERVER's
    // clock - a rate limit measured with the attacker's own timestamps is not a limit.
    uint64_t LastAcceptedMs{0};
};
