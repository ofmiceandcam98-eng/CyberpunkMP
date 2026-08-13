#pragma once

/**
 * Persistent bans, keyed on Discord id.
 *
 * Discord id rather than username or IP, because it is the only identifier a player
 * cannot change: usernames get renamed, IPs rotate, and both punish the wrong person
 * eventually. A banned account stays banned even if they reinstall, change name, or
 * come back from a different connection.
 *
 * Written to disk on every change. A ban that evaporates when the server restarts is
 * not a ban, and restarts happen at exactly the wrong moment.
 */
struct BanEntry
{
    std::string DiscordId;
    std::string Username;   // for display - who this was, at the time
    std::string Reason;
    std::string BannedBy;   // who issued it, so decisions can be traced later
    int64_t At{0};          // unix seconds

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BanEntry, DiscordId, Username, Reason, BannedBy, At)
};

struct BanList
{
    void Load(const std::filesystem::path& acPath)
    {
        m_path = acPath;

        std::ifstream file(m_path);
        if (!file.is_open())
            return;   // no bans yet is a perfectly normal state

        try
        {
            const auto data = nlohmann::json::parse(file);
            m_entries = data.get<std::vector<BanEntry>>();
            spdlog::info("Loaded {} ban(s)", m_entries.size());
        }
        catch (const std::exception& e)
        {
            // Never start with an empty ban list because the file was malformed - that
            // silently unbans everyone. Refuse to proceed instead.
            spdlog::error("Could not read {}: {}. Fix or remove the file.", m_path.string(), e.what());
            throw;
        }
    }

    [[nodiscard]] const BanEntry* Find(const std::string& acDiscordId) const
    {
        if (acDiscordId.empty())
            return nullptr;

        for (const auto& entry : m_entries)
        {
            if (entry.DiscordId == acDiscordId)
                return &entry;
        }

        return nullptr;
    }

    bool Add(const std::string& acDiscordId, const std::string& acUsername,
             const std::string& acReason, const std::string& acBannedBy)
    {
        if (acDiscordId.empty() || Find(acDiscordId))
            return false;

        m_entries.push_back(BanEntry{acDiscordId, acUsername, acReason, acBannedBy,
                                     static_cast<int64_t>(std::time(nullptr))});
        Save();
        return true;
    }

    bool Remove(const std::string& acDiscordId)
    {
        const auto before = m_entries.size();

        std::erase_if(m_entries, [&](const BanEntry& e) { return e.DiscordId == acDiscordId; });

        if (m_entries.size() == before)
            return false;

        Save();
        return true;
    }

    [[nodiscard]] const std::vector<BanEntry>& Entries() const { return m_entries; }

private:
    void Save() const
    {
        if (m_path.empty())
            return;

        try
        {
            std::error_code ec;
            create_directories(m_path.parent_path(), ec);

            std::ofstream file(m_path);
            file << std::setw(4) << nlohmann::json(m_entries);
        }
        catch (const std::exception& e)
        {
            spdlog::error("Could not save bans to {}: {}", m_path.string(), e.what());
        }
    }

    std::vector<BanEntry> m_entries;
    std::filesystem::path m_path;
};
