#pragma once

/**
 * Vehicle seats, by name rather than by magic number.
 *
 * HOW THE IDS ARE KNOWN
 *
 * A seat id on the wire is the game's CName hash of the seat's name, and CName is FNV1a64.
 * That was not assumed: Level.cpp carried exactly one seat constant with its name in a
 * comment - 0xb000b1d029d0cea0 // seat_front_left - and FNV1a64("seat_front_left")
 * reproduces it exactly. Verified offline before any of this was written.
 *
 * The consequence is the useful part: every seat id can be DERIVED from its name at
 * compile time. No pasted hashes, no WolvenKit export, no running game, and nothing to
 * re-verify when a seat is added - the name is the source and the number falls out of it.
 *
 * A pasted hash is opaque, unverifiable by eye, and goes silently wrong when somebody
 * mistypes a digit. That is the same trap recorded for vehicle model ids in the map, and
 * this file exists so seats never join it.
 *
 * WHAT THIS DOES AND DOES NOT DECIDE
 *
 * It decides whether a seat id is a SEAT - one of the positions the game actually names.
 * It does not decide which seats a particular car has. That is game data the server cannot
 * see, and inventing it would mean either refusing seats that exist or offering seats that
 * do not; both are worse than letting the game's own mount system answer, which it already
 * does when the client picks a door.
 *
 * The division is the one the vehicle audit already settled: the server is authoritative
 * over STATE and PERMISSION, not over the game's data. It owns who is in which seat. It
 * does not own how many seats a Mackinaw has.
 */

#include <cstdint>
#include <cstddef>
#include <string>

/**
 * FNV1a64, which is what CName is.
 *
 * constexpr so the seat table below is computed by the compiler and costs nothing at
 * runtime - and so a seat id is a NAME in the source, never a literal.
 */
constexpr uint64_t Fnv1a64(const char* acpText)
{
    uint64_t hash = 0xCBF29CE484222325ULL;

    for (const char* p = acpText; *p; ++p)
    {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(*p));
        hash *= 0x100000001B3ULL;
    }

    return hash;
}

/**
 * The seats the game names.
 *
 * Front left is the driver on every vehicle in this game. The back pair is what "four
 * people in a four-door car" means, and the middle seats exist on the handful of vehicles
 * with a bench - listed because naming a seat costs nothing and discovering later that the
 * table was too small costs a protocol conversation.
 *
 * NOT a claim that every vehicle has all of these. See the file comment: which seats a
 * given car has is the game's business, and a client asking for one it cannot reach simply
 * never sends it.
 */
struct VehicleSeat
{
    const char* Name;
    uint64_t Id;
};

inline constexpr VehicleSeat kVehicleSeats[] = {
    {"seat_front_left", Fnv1a64("seat_front_left")},     // the driver, always
    {"seat_front_right", Fnv1a64("seat_front_right")},
    {"seat_back_left", Fnv1a64("seat_back_left")},
    {"seat_back_right", Fnv1a64("seat_back_right")},
    {"seat_back_middle", Fnv1a64("seat_back_middle")},   // benches - Emperor, Thrax
    {"seat_middle_left", Fnv1a64("seat_middle_left")},
    {"seat_middle_right", Fnv1a64("seat_middle_right")},
};

inline constexpr size_t kVehicleSeatCount = sizeof(kVehicleSeats) / sizeof(kVehicleSeats[0]);

/**
 * The driver's seat, and the ONE seat that is special.
 *
 * Kept as a named constant because two places need it for different reasons: the
 * desync-fork guard only permits spawning a car from the driver's seat, and authority
 * follows whoever is driving. Spelling the hash in either place would be a magic number
 * that nothing connects back to this table.
 */
inline constexpr uint64_t kDriverSeat = Fnv1a64("seat_front_left");

/**
 * Is this a seat at all?
 *
 * The check that was missing. Occupancy was enforced - one person per seat - but nothing
 * asked whether the id WAS a seat, so any 64-bit number a client invented was accepted as
 * an empty seat and occupied. Four people already fit; a fifth simply had to name a
 * position nobody else had claimed.
 */
inline bool IsKnownSeat(uint64_t aSeatId)
{
    for (const auto& seat : kVehicleSeats)
    {
        if (seat.Id == aSeatId)
            return true;
    }

    return false;
}

/**
 * The seat's name, for logs and for anything a player reads.
 *
 * Every vehicle log line said "seat 3f2a91..." which is unreadable and unsearchable, and
 * made every seat bug start with working out which seat it was about. Unknown ids answer
 * with the hex so a rejection can still be traced.
 */
inline std::string VehicleSeatName(uint64_t aSeatId)
{
    for (const auto& seat : kVehicleSeats)
    {
        if (seat.Id == aSeatId)
            return seat.Name;
    }

    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "unknown(%016llx)",
                  static_cast<unsigned long long>(aSeatId));

    return buffer;
}

/**
 * Something a player can read. "the driver's seat", "the back left seat".
 *
 * Separate from VehicleSeatName because a log wants the exact identifier and a person
 * wants a phrase - and using one for both means either logs full of prose or refusals that
 * say "seat_back_left" at somebody.
 */
inline std::string VehicleSeatDescription(uint64_t aSeatId)
{
    if (aSeatId == Fnv1a64("seat_front_left"))
        return "the driver's seat";
    if (aSeatId == Fnv1a64("seat_front_right"))
        return "the passenger seat";
    if (aSeatId == Fnv1a64("seat_back_left"))
        return "the back left seat";
    if (aSeatId == Fnv1a64("seat_back_right"))
        return "the back right seat";
    if (aSeatId == Fnv1a64("seat_back_middle"))
        return "the middle back seat";
    if (aSeatId == Fnv1a64("seat_middle_left"))
        return "the middle left seat";
    if (aSeatId == Fnv1a64("seat_middle_right"))
        return "the middle right seat";

    return "that seat";
}
