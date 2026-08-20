#pragma once

#include "VehicleRecord.h"

/**
 * Every vehicle anybody owns, kept by the server and nowhere else.
 *
 * Separate from PlayerStore rather than nested inside a character, because ownership
 * outlives the thing it is attached to. A car sold to somebody else keeps its identity and
 * changes hands; a car whose owner deletes their character still exists and can be
 * reassigned. Nesting the record inside a character would make both of those a rewrite of
 * two records instead of one field.
 *
 * Flat file, keyed on nothing - the list is walked. That is the right shape at this size:
 * a server with a hundred cars costs nothing to scan, and an index that has to be kept in
 * step is one more thing to get wrong. Revisit when somebody owns ten thousand vehicles.
 */
struct VehicleStore
{
    void Load(const std::filesystem::path& acPath) noexcept;
    void Save() const noexcept;

    // Everything this account owns. Returned by value: the caller usually wants to send it
    // somewhere, and a reference into a vector that Create() might reallocate is a trap.
    std::vector<VehicleRecord> OwnedBy(const std::string& acDiscordId) const noexcept;

    const VehicleRecord* Find(const std::string& acId) const noexcept;

    // Creates one and returns its id. Plate collisions are retried rather than accepted -
    // two identical plates on one server defeats the entire point of having them.
    std::string Create(const std::string& acOwnerId, uint64_t aModel, const std::string& acModelName,
                       int64_t aPrice) noexcept;

    // Changes hands. Returns false when the vehicle does not exist, or is locked by a
    // transaction other than the one asking.
    bool Transfer(const std::string& acId, const std::string& acNewOwnerId,
                  const std::string& acLockToken = {}) noexcept;

    bool Lock(const std::string& acId, const std::string& acToken) noexcept;
    bool Unlock(const std::string& acId, const std::string& acToken) noexcept;

    size_t Count() const noexcept { return m_vehicles.size(); }

private:
    std::vector<VehicleRecord> m_vehicles;
    std::filesystem::path m_path;
};
