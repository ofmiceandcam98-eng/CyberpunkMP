#pragma once

/**
 * Where each player was, kept across sessions.
 *
 * This is the server-side answer to "can we have an autosave". Overwriting a Cyberpunk
 * save file would be the wrong mechanism twice over: those saves are tens of megabytes
 * and hitch the game when written, and repeatedly overwriting one means a crash mid-write
 * costs you the save entirely. They also describe a SINGLEPLAYER game - nothing about the
 * server, or the session, is in them.
 *
 * So the server keeps its own record instead. It already knows where everyone is; writing
 * that down is nearly free, cannot corrupt anyone's game, and is what actually needs to
 * survive - the position you were standing in when the game died, not the state of your
 * offline save.
 *
 * Keyed on Discord id for the same reason bans are: it is the only identifier a player
 * cannot change. Rename yourself, reinstall, come back on a different connection, and the
 * server still knows which character is yours.
 *
 * Deliberately small for now - position and facing. Inventory and appearance belong here
 * too eventually, and this is the file they go in.
 */
struct PlayerRecord
{
    std::string DiscordId;
    std::string Username;   // for reading the file by eye; identity is the id

    float X{0.f};
    float Y{0.f};
    float Z{0.f};
    float Yaw{0.f};

    int64_t LastSeen{0};    // unix seconds

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerRecord, DiscordId, Username, X, Y, Z, Yaw, LastSeen)
};

struct PlayerStore
{
    void Load(const std::filesystem::path& acPath)
    {
        m_path = acPath;

        std::ifstream file(m_path);
        if (!file.is_open())
            return;   // nobody has played yet - a normal first run

        try
        {
            const auto data = nlohmann::json::parse(file);
            m_records = data.get<std::vector<PlayerRecord>>();
            spdlog::info("Loaded {} saved player position(s)", m_records.size());
        }
        catch (const std::exception& e)
        {
            // Unlike bans, losing this is not a safety problem - the worst case is
            // everyone spawns where their own save puts them, which is what happened
            // before this existed. So carry on rather than refusing to start.
            spdlog::error("Could not read {}: {}. Starting with no saved positions.",
                          m_path.string(), e.what());
            m_records.clear();
        }
    }

    /**
     * Records where someone is. Overwrites their previous position.
     *
     * Called often, so it does NOT write to disk - see Flush. Writing a file every time
     * anyone moves would put a disk write in the movement path for no benefit.
     */
    void Remember(const std::string& acDiscordId, const std::string& acUsername,
                  const glm::vec3& acPosition, float aYaw)
    {
        if (acDiscordId.empty())
            return;   // unverified player - nothing durable to key on

        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        for (auto& record : m_records)
        {
            if (record.DiscordId == acDiscordId)
            {
                record.Username = acUsername;
                record.X = acPosition.x;
                record.Y = acPosition.y;
                record.Z = acPosition.z;
                record.Yaw = aYaw;
                record.LastSeen = now;
                m_dirty = true;
                return;
            }
        }

        m_records.push_back({acDiscordId, acUsername, acPosition.x, acPosition.y, acPosition.z, aYaw, now});
        m_dirty = true;
    }

    // Null when this player has never been seen, which is how a first-time player keeps
    // whatever position their own save gave them.
    const PlayerRecord* Find(const std::string& acDiscordId) const
    {
        if (acDiscordId.empty())
            return nullptr;

        for (const auto& record : m_records)
        {
            if (record.DiscordId == acDiscordId)
                return &record;
        }

        return nullptr;
    }

    // Writes only when something actually changed, so calling this on a timer is cheap.
    void Flush()
    {
        if (!m_dirty || m_path.empty())
            return;

        try
        {
            std::filesystem::create_directories(m_path.parent_path());

            std::ofstream file(m_path);
            file << nlohmann::json(m_records).dump(2);

            m_dirty = false;
        }
        catch (const std::exception& e)
        {
            spdlog::error("Could not write {}: {}", m_path.string(), e.what());
        }
    }

private:
    std::vector<PlayerRecord> m_records;
    std::filesystem::path m_path;
    bool m_dirty{false};
};
