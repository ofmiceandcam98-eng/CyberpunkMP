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
    kModerator = 10,
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
    case EPermissionLevel::kOwner:     return "owner";
    case EPermissionLevel::kAdmin:     return "admin";
    case EPermissionLevel::kModerator: return "moderator";
    default:                           return "player";
    }
}
