#pragma once

#include <string>
#include <vector>
#include <random>

/**
 * A vehicle somebody owns, as opposed to a vehicle model somebody has unlocked.
 *
 * THE DISTINCTION THIS EXISTS FOR
 *
 * Cyberpunk's own garage is model-level: EnablePlayerVehicle unlocks a MODEL, and calling
 * one spawns "a Quadra Type-66". The game has no notion of a particular Quadra. That is
 * fine for singleplayer, where there is one of you, and wrong for a server, where two
 * people owning the same model must own two different cars - with different plates,
 * different damage, different locations, and the ability to sell one to the other.
 *
 * So an owned vehicle is a SERVER record with its own identity, and the game's garage is
 * left to mean what it always meant: which models this character has unlocked. The two
 * coexist deliberately rather than one replacing the other.
 *
 * INSTANCE ID vs NETWORK ENTITY ID
 *
 * Id is permanent and survives everything - despawn, disconnect, restart, being sold. The
 * flecs entity that represents the car in the world right now is not stored here at all,
 * because it is temporary by nature: a car despawned and called again is the same property
 * and a different entity. Conflating them is how a sold car ends up back in the seller's
 * garage after a restart.
 */
struct VehicleRecord
{
    // Permanent, unique, never reused - not the game's entity handle, which differs
    // between clients and changes on every respawn.
    std::string Id;

    // Whose it is. A Discord id, like everything else that identifies a person here, so
    // ownership survives a rename and follows the account rather than the machine.
    std::string OwnerId;

    // The game's vehicle record, as a raw TweakDBID. Uninterpreted by the server, like
    // appearance and items - a server that understood vehicle records would need updating
    // every patch, and it has no need to know what a Quadra IS to remember you own one.
    uint64_t Model{0};

    // Kept alongside the id because EnablePlayerVehicle and the spawn path both take a
    // string, and a TweakDBID cannot be turned back into one on 2.31 - release builds ship
    // an empty debug name table. Storing both is redundant and cheaper than being unable
    // to spawn the thing.
    std::string ModelName;

    // What makes two identical models visibly different cars.
    std::string Plate;

    int64_t PurchasedAt{0};
    int64_t Price{0};

    /**
     * Locked while a sale is pending.
     *
     * Set for the duration of an offer so the same car cannot be sold to two people at
     * once, called while changing hands, or deleted mid-transaction. Cleared when the sale
     * completes, is declined, expires, or either party disconnects.
     *
     * A string rather than a bool so the log can say WHICH transaction holds it - "locked"
     * with no explanation is the kind of state somebody has to restart a server to clear.
     */
    std::string LockedBy;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(VehicleRecord, Id, OwnerId, Model, ModelName,
                                                Plate, PurchasedAt, Price, LockedBy)
};

/**
 * NC-1234. Two letters of city, four digits.
 *
 * Random rather than sequential: a counter tells everyone how many cars the server has
 * ever sold, and collides the instant two servers merge data. Four digits is ten thousand
 * plates, which is far more than this server will hold, and the caller checks for
 * collisions anyway - the point of the check is that "far more than we need" is not the
 * same as "cannot repeat".
 */
inline std::string GeneratePlate()
{
    static std::mt19937 engine{std::random_device{}()};
    static std::uniform_int_distribution<int> digits{0, 9999};

    char plate[8] = {};
    std::snprintf(plate, sizeof(plate), "NC-%04d", digits(engine));

    return plate;
}
