#pragma once

#include <string>
#include <cstdint>

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

    // ----------------------------------------------------------------- medical ----
    //
    // The medical state lives HERE, on the component that already owns health and
    // LifeState, rather than in a MedicalComponent beside it. The brief is explicit about
    // not creating a second health authority, and two components that both describe
    // "how alive is this person" would be exactly that - they would disagree the first
    // time either changed, and the disagreement would decide whether somebody could be
    // revived.

    /**
     * When they went down, on the SERVER's clock. Zero when they are not down.
     *
     * The bleedout timer is derived from this rather than counted down, because a counter
     * has to be ticked and a timestamp does not: a tick that is skipped, doubled, or runs
     * while the server is busy changes a counter and cannot change a timestamp. The client
     * is sent the deadline and DISPLAYS it; it never decides that it has passed.
     */
    int64_t DownedAt{0};

    /**
     * Stabilised: the bleedout is held off, but they are still down.
     *
     * Deliberately not "more time". A stabilised patient does not bleed out at all while
     * somebody is looking after them - that is what the treatment is FOR - and expressing
     * it as extra seconds would mean a medic who did everything right still watching their
     * patient die on a timer they cannot see.
     */
    bool Stabilized{false};

    // Which character is treating them, so two medics cannot work on one patient and
    // neither can tell whose procedure finished. Empty when nobody is.
    std::string TreatedBy;

    // When the current procedure finishes, on the server's clock. Zero when none is
    // running. A revive is a procedure with a duration, not a button - an instant one is
    // indistinguishable from a cheat and removes the entire point of a medic being present.
    int64_t TreatmentEndsAt{0};
};
