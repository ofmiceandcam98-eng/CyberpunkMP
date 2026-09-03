#pragma once

/**
 * What a player is allowed to do, derived from their Discord roles at connect time.
 *
 * Lives in its own header because both the config (which maps Discord role ids onto
 * these) and the player component (which stores the result) need it, and neither
 * should have to include the other.
 *
 * Values are ordered and spaced deliberately:
 *   - Ordered, so a check reads `level >= kModerator` instead of enumerating every
 *     role that happens to qualify. Add a rank later and existing checks still hold.
 *   - Spaced by ten, so a rank can be inserted between two existing ones without
 *     renumbering anything already stored.
 */
enum class EPermissionLevel : uint8_t
{
    kPlayer = 0,

    /**
     * Support: extra character slots, and NOTHING ELSE.
     *
     * Cam's rule, 2026-09-02. Support answers tickets; they do not need to kick, jail,
     * mute, teleport or spawn anything, and every one of those is a power that only has to
     * be misused once.
     *
     * BELOW moderator on purpose, and that placement is what enforces the rule rather than
     * anybody remembering it: every existing `>= kModerator` check excludes support
     * automatically, so support cannot acquire a moderation power by somebody adding one
     * later. The only check they satisfy is the slot rule, which asks for `>= kSupport`.
     *
     * This replaces the old arrangement where support WAS the moderator rung because no
     * support level existed - which quietly gave ticket staff the full moderator toolkit.
     */
    kSupport = 5,

    kModerator = 10,

    /**
     * Event staff: everything a moderator can do, plus the tools for running an event.
     *
     * THE RUNG THE SPACING WAS FOR. Cam's rule (2026-09-02) is that event staff can spawn
     * things "for they are our EVENT staff", while support and moderators cannot.
     *
     * That needs its own level, and the alternative was worse. Lowering the spawn commands
     * to kModerator would have handed /givecar, /npc, /time and /weather to SUPPORT as
     * well - people whose job is answering tickets, who have no reason to mint vehicles or
     * change the weather, and whose access to the economy should stay at zero.
     *
     * Fifteen, so every existing check still means what it did: `>= kModerator` now also
     * admits event staff (correct - they are senior to moderators), and `>= kAdmin` is
     * untouched, so nothing gained a power by this being inserted.
     */
    kEventStaff = 15,

    kAdmin = 20,
    kOwner = 30
};

/**
 * The player-facing id, derived from a Discord snowflake. Defined in GameServer.cpp,
 * where the crypto headers are in scope.
 *
 * MUST match the launcher's derivePlayerId exactly - same salt, same hash, same
 * modulus - or the number a player reads off their launcher will not be the number
 * in the server log, and moderation reports become untraceable.
 */
std::string DerivePlayerId(const std::string& acSnowflake);

inline const char* ToString(EPermissionLevel aLevel)
{
    switch (aLevel)
    {
    case EPermissionLevel::kOwner:      return "owner";
    case EPermissionLevel::kAdmin:      return "admin";
    case EPermissionLevel::kEventStaff: return "event staff";
    case EPermissionLevel::kModerator:  return "moderator";
    case EPermissionLevel::kSupport:    return "support";
    default:                            return "player";
    }
}
