#pragma once

/**
 * Downed, stabilised, revived, dead - the rules, in one place.
 *
 * WHAT ALREADY EXISTED, because this is a layer rather than a system
 *
 * The server has owned health since the combat work: HealthComponent holds Health and
 * LifeState (0 alive, 1 downed, 2 dead, 3 reviving), lethal damage already puts a player
 * DOWN rather than killing them, and the state is already broadcast to everybody through
 * NotifyCombatState. None of that is rebuilt here.
 *
 * What was missing is everything that happens NEXT: a bleedout that ends in death, somebody
 * able to prevent it, and a way back up. This file is those rules and nothing else - it
 * deliberately owns no health, no permissions and no inventory, because all three already
 * have an authority and a second one would disagree with the first.
 *
 * THE TIMER IS A DEADLINE, NOT A COUNTDOWN
 *
 * DownedAt is a timestamp and the bleedout is derived from it. A countdown has to be
 * ticked, and a tick that is skipped, doubled, or delayed by a busy server changes the
 * answer; a timestamp cannot be. The client is TOLD the deadline and displays it, and never
 * decides that it has passed - a client that could decide that could decide it had not.
 *
 * STABILISED MEANS THE BLEEDING STOPPED
 *
 * Not "more time". A stabilised patient does not bleed out while somebody is looking after
 * them, because that is what the treatment is for. Expressing it as extra seconds would
 * mean a medic who did everything right still watching their patient die on a clock they
 * cannot see, which is the opposite of what the mechanic is for.
 */

#include <cstdint>
#include <chrono>

#include "PermissionLevel.h"

/**
 * How long somebody bleeds out, in seconds.
 *
 * Three minutes. Long enough that a medic across the district can plausibly arrive, which
 * is the whole point of having medics; short enough that a body is not a permanent fixture
 * of the street. This is the number to tune once the server has people in it - it is the
 * single value that decides whether medical roleplay is a profession or a formality.
 */
inline constexpr int64_t kBleedoutSeconds = 180;

// How long treatment takes. A revive is a PROCEDURE, not a button: an instant one is
// indistinguishable from a cheat, and it removes the reason a medic has to be present.
inline constexpr int64_t kStabilizeSeconds = 8;
inline constexpr int64_t kReviveSeconds = 12;

// How close a medic must be, in metres. Treating somebody is hands-on.
inline constexpr float kTreatmentDistance = 3.f;

/**
 * How much health a revived patient comes back with, as a percentage.
 *
 * Low, deliberately. Somebody who was dying a moment ago and stands up at full health has
 * lost nothing by going down, and every fight becomes a stalemate between people who cannot
 * be removed from it. Coming back fragile is what makes being downed cost something without
 * costing the character.
 */
inline constexpr float kRevivedHealth = 15.f;

/**
 * Who may treat.
 *
 * A permission, checked against the level the SERVER derived from Discord - never against
 * anything a client claims, like every other permission here.
 *
 * Set to kPlayer today, which means anybody can help. That is a deliberate STARTING point
 * rather than an oversight: a server with no medics yet and a rule that only medics may
 * revive is a server where everybody who goes down dies, and the first thing anyone would
 * learn is that going down is unrecoverable. Raise it to a medical role the moment there
 * are people holding one - it is one constant and the check already exists.
 */
inline constexpr EPermissionLevel kTreatPermission = EPermissionLevel::kPlayer;

inline int64_t MedicalNow()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Life states, named. The numbers are the wire's (NotifyCombatState.life_state) and were
// already in use; naming them stops the next reader having to find the comment that says
// which is which.
namespace LifeState
{
constexpr uint32_t kAlive = 0;
constexpr uint32_t kDowned = 1;
constexpr uint32_t kDead = 2;
constexpr uint32_t kReviving = 3;
}
