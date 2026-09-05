#include "VehicleStore.h"
#include "CharacterRecord.h"   // GenerateCharacterId - the same id shape, for the same reasons
#include <algorithm>

void VehicleStore::Load(const std::filesystem::path& acPath) noexcept
{
    m_path = acPath;
    m_vehicles.clear();

    if (!std::filesystem::exists(m_path))
    {
        Save();
        spdlog::info("No vehicles yet - created {}", m_path.string());
        return;
    }

    try
    {
        std::ifstream file(m_path);
        nlohmann::json document;
        file >> document;

        m_vehicles = document.get<std::vector<VehicleRecord>>();

        // Any lock in the file is stale by definition: a lock only means something while
        // the transaction holding it is live, and no transaction survives a restart.
        // Leaving them set would strand cars nobody can sell or call, with nothing in the
        // interface explaining why.
        size_t cleared = 0;
        for (auto& vehicle : m_vehicles)
        {
            if (!vehicle.LockedBy.empty())
            {
                vehicle.LockedBy.clear();
                ++cleared;
            }
        }

        spdlog::info("Loaded {} vehicle(s) from {}{}", m_vehicles.size(), m_path.string(),
                     cleared ? fmt::format(" - cleared {} stale transaction lock(s)", cleared) : "");

        if (cleared)
            Save();
    }
    catch (const std::exception& e)
    {
        // Left exactly as it is. Overwriting somebody's vehicle records with an empty list
        // because one line failed to parse is unrecoverable; an unreadable file is not.
        spdlog::error("Could not read {} - {}. No vehicles loaded; the file is untouched.",
                      m_path.string(), e.what());
    }
}

void VehicleStore::Save() const noexcept
{
    if (m_path.empty())
        return;

    try
    {
        std::filesystem::create_directories(m_path.parent_path());

        std::ofstream file(m_path);
        file << nlohmann::json(m_vehicles).dump(2);
    }
    catch (const std::exception& e)
    {
        spdlog::error("Could not write {} - {}", m_path.string(), e.what());
    }
}

std::vector<VehicleRecord> VehicleStore::OwnedBy(const std::string& acDiscordId) const noexcept
{
    std::vector<VehicleRecord> owned;

    if (acDiscordId.empty())
        return owned;

    for (const auto& vehicle : m_vehicles)
    {
        if (vehicle.OwnerId == acDiscordId)
            owned.push_back(vehicle);
    }

    return owned;
}

const VehicleRecord* VehicleStore::Find(const std::string& acId) const noexcept
{
    for (const auto& vehicle : m_vehicles)
    {
        if (vehicle.Id == acId)
            return &vehicle;
    }

    return nullptr;
}

std::string VehicleStore::Create(const std::string& acOwnerId, uint64_t aModel,
                                 const std::string& acModelName, int64_t aPrice) noexcept
{
    if (acOwnerId.empty() || aModel == 0)
        return {};

    VehicleRecord record;
    record.Id = GenerateCharacterId();   // same 16-hex shape, same reasoning
    record.OwnerId = acOwnerId;
    record.Model = aModel;
    record.ModelName = acModelName;
    record.Price = aPrice;
    record.PurchasedAt = std::time(nullptr);

    // Retried rather than accepted. Two identical plates on one server defeats the whole
    // point of having them, and the odds of a clash are small rather than zero. Bounded so
    // a full plate space cannot hang the server - a duplicate is better than a freeze.
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        record.Plate = GeneratePlate();

        const bool taken = std::any_of(m_vehicles.begin(), m_vehicles.end(),
                                       [&record](const VehicleRecord& acOther)
                                       { return acOther.Plate == record.Plate; });

        if (!taken)
            break;
    }

    m_vehicles.push_back(record);
    Save();

    spdlog::info("Vehicle {} created: {} plate {} for {}", record.Id, acModelName, record.Plate,
                 acOwnerId);

    return record.Id;
}

bool VehicleStore::Transfer(const std::string& acId, const std::string& acNewOwnerId,
                            const std::string& acLockToken) noexcept
{
    if (acNewOwnerId.empty())
        return false;

    for (auto& vehicle : m_vehicles)
    {
        if (vehicle.Id != acId)
            continue;

        // A vehicle locked by a DIFFERENT transaction is mid-sale to somebody else. The
        // token is what distinguishes "the sale that is completing right now" from "some
        // other sale that has not finished", and without it the last transfer to arrive
        // would win regardless of which was legitimate.
        if (!vehicle.LockedBy.empty() && vehicle.LockedBy != acLockToken)
        {
            spdlog::warn("Refused to transfer vehicle {} - locked by '{}'", acId, vehicle.LockedBy);
            return false;
        }

        const auto previous = vehicle.OwnerId;

        vehicle.OwnerId = acNewOwnerId;
        vehicle.LockedBy.clear();
        Save();

        spdlog::info("Vehicle {} ({}) transferred from {} to {}", acId, vehicle.Plate, previous,
                     acNewOwnerId);
        return true;
    }

    return false;
}

bool VehicleStore::Lock(const std::string& acId, const std::string& acToken) noexcept
{
    if (acToken.empty())
        return false;

    for (auto& vehicle : m_vehicles)
    {
        if (vehicle.Id != acId)
            continue;

        // Already locked by somebody else - the caller must not proceed, and must not
        // silently take the lock either.
        if (!vehicle.LockedBy.empty() && vehicle.LockedBy != acToken)
            return false;

        vehicle.LockedBy = acToken;
        Save();
        return true;
    }

    return false;
}

bool VehicleStore::Unlock(const std::string& acId, const std::string& acToken) noexcept
{
    for (auto& vehicle : m_vehicles)
    {
        if (vehicle.Id != acId)
            continue;

        // Only the holder releases it. Anything else lets a cancelled sale free a lock a
        // different, live sale is relying on.
        if (vehicle.LockedBy != acToken)
            return false;

        vehicle.LockedBy.clear();
        Save();
        return true;
    }

    return false;
}
