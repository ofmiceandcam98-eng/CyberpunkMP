#pragma once

#include <cmath>
#include <glm/glm.hpp>

// Sanity checks for anything a client sent us.
//
// The server already checks OWNERSHIP - you cannot move an entity that is not yours - but
// it trusted the VALUES completely. That gap is worse than it sounds, and it does not need
// anybody to be malicious.
//
// A non-finite position is the case that matters. Comparisons against NaN are false, so a
// single bad packet does not crash anything - it silently disables the systems that use
// distance:
//
//   * glm::distance(...) <= range   is false, so the player hears no local chat and
//     nobody hears them. Chat appears broken for one person with no error anywhere.
//   * glm::distance(...) > kJailRadius  is false, so a jailed player stops being dragged
//     back to their cell.
//   * the position is written to the persistent player store on disconnect, so it
//     SURVIVES A RESTART and follows them into the next session.
//
// And it can happen by accident: eulerAngles() on a degenerate quaternion produces NaN,
// and the client sends whatever it computed.
//
// The rule here is that a bad packet is DROPPED and logged, never clamped. Clamping
// invents a position the player is not at, which is a desync we would then have to debug.
namespace Validation
{
// Night City is roughly 3km across. This is deliberately far looser than the playable
// area: the job is to catch garbage, not to police where anyone can stand, and a bound
// that is too tight becomes a bug the first time someone finds a legitimate spot outside
// it.
constexpr float kWorldLimit = 20000.f;

// Faster than any vehicle in the game by a wide margin. Only catches nonsense.
constexpr float kSpeedLimit = 1000.f;

inline bool IsFinite(float aValue)
{
    return std::isfinite(aValue);
}

inline bool IsFinite(const glm::vec3& acValue)
{
    return IsFinite(acValue.x) && IsFinite(acValue.y) && IsFinite(acValue.z);
}

inline bool IsSanePosition(const glm::vec3& acPosition)
{
    if (!IsFinite(acPosition))
        return false;

    return std::abs(acPosition.x) <= kWorldLimit &&
           std::abs(acPosition.y) <= kWorldLimit &&
           std::abs(acPosition.z) <= kWorldLimit;
}

// Rotation only has to be finite. Angles outside 0..2pi are normal - they wrap - so
// rejecting them would drop legitimate packets.
inline bool IsSaneRotation(const glm::vec3& acRotation)
{
    return IsFinite(acRotation);
}

inline bool IsSaneSpeed(float aSpeed)
{
    return IsFinite(aSpeed) && std::abs(aSpeed) <= kSpeedLimit;
}
} // namespace Validation
