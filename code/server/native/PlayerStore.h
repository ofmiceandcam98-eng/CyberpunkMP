#pragma once

#include "CharacterRecord.h"

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

    // Jail.
    //
    // Persisted for the same reason bans are: a sentence that ends when you alt-F4 is not
    // a sentence. Quitting, crashing, or waiting out a server restart must not release
    // anyone - otherwise the first thing every jailed player learns is to close the game.
    int64_t JailedUntil{0};   // unix seconds; 0 means not jailed
    float JailX{0.f};
    float JailY{0.f};
    float JailZ{0.f};
    std::string JailedBy;
    std::string JailReason;

    // The multiplayer characters this account owns. Empty means they have never made one,
    // which is what the join flow branches on.
    std::vector<CharacterRecord> Characters;

    // Which slot they are playing. Only 0 exists today.
    int ActiveSlot{0};

    // Characters they have replaced. Kept rather than deleted.
    //
    // Rerolling is a single click and a character is hours of somebody's evening: a
    // misclick that silently destroys one is the kind of thing people quit over. Nothing
    // reads these yet - they exist so that "can you get my old character back" has an
    // answer other than no.
    std::vector<CharacterRecord> RetiredCharacters;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(PlayerRecord, DiscordId, Username, X, Y, Z, Yaw, LastSeen,
                                                JailedUntil, JailX, JailY, JailZ, JailedBy, JailReason,
                                                Characters, ActiveSlot, RetiredCharacters)
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

            // Every character that predates ids gets one now, on load.
            //
            // The alternative was assigning them on the next save, which sounds equivalent
            // and is not: a character is only saved when its appearance changes, so
            // somebody who is happy with how they look would have had no id for weeks.
            // /whois would have answered "none yet" and /tp by id would have failed, for
            // exactly the established players an admin is most likely to be looking up.
            //
            // Retired characters are included. They are the ones most in need of a stable
            // handle, since their owner is no longer playing them and their name may since
            // have been taken by the replacement.
            int assigned = 0;

            for (auto& record : m_records)
            {
                for (auto* list : {&record.Characters, &record.RetiredCharacters})
                {
                    for (auto& character : *list)
                    {
                        if (character.CharacterId.empty())
                        {
                            character.CharacterId = GenerateCharacterId();
                            ++assigned;
                        }
                    }
                }
            }

            if (assigned > 0)
            {
                // Written straight back out. Ids generated but never persisted would be
                // different on every restart, which is worse than not having them - an id
                // an admin wrote down would stop working overnight.
                m_dirty = true;
                Flush();

                spdlog::info("Gave {} existing character(s) a permanent id", assigned);
            }
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

    static int64_t Now()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // ---------------------------------------------------------------- characters ----

    /**
     * Does this account already have a character to come back to?
     *
     * This is what the join flow branches on: somebody with a character is offered it,
     * somebody without goes straight to creation and is never asked to pick a save.
     */
    bool HasCharacter(const std::string& acDiscordId) const
    {
        return FindCharacter(acDiscordId) != nullptr;
    }

    const CharacterRecord* FindCharacter(const std::string& acDiscordId, int aSlot = -1) const
    {
        const auto* pRecord = Find(acDiscordId);
        if (!pRecord)
            return nullptr;

        const int slot = (aSlot < 0) ? pRecord->ActiveSlot : aSlot;

        for (const auto& character : pRecord->Characters)
        {
            if (character.Slot == slot)
                return &character;
        }

        return nullptr;
    }

    /**
     * Stores a newly created or updated character.
     *
     * Written through immediately. A character is the single most expensive thing a
     * player produces here, and losing one to a crash in the thirty-second window before
     * the next timed flush is not a trade worth making for one file write.
     */
    void SaveCharacter(const std::string& acDiscordId, const std::string& acUsername,
                       CharacterRecord acCharacter)
    {
        if (acDiscordId.empty())
            return;

        // Given an id the first time it is stored, and never again.
        //
        // Assigned here rather than at creation because this is the single point every
        // character passes through - the creator, a ripperdoc edit, a rename and an admin
        // change all end up on this line. Anything that arrives without an id is either
        // brand new or predates ids existing, and both want the same treatment.
        if (acCharacter.CharacterId.empty())
        {
            if (const auto* pExisting = FindCharacter(acDiscordId, acCharacter.Slot);
                pExisting && !pExisting->CharacterId.empty())
            {
                // Editing a character that already has one. Keeping it is the whole point:
                // an id that changed when you visited a ripperdoc would be no use to any
                // command that references it.
                acCharacter.CharacterId = pExisting->CharacterId;
            }
            else
            {
                acCharacter.CharacterId = GenerateCharacterId();
            }
        }

        auto* pRecord = FindMutable(acDiscordId);

        if (!pRecord)
        {
            m_records.push_back({acDiscordId, acUsername});
            pRecord = &m_records.back();
        }

        pRecord->Username = acUsername;

        const auto now = Now();

        for (auto& existing : pRecord->Characters)
        {
            if (existing.Slot != acCharacter.Slot)
                continue;

            const auto created = existing.CreatedAt;   // not reset by an edit
            existing = acCharacter;
            existing.CreatedAt = created ? created : now;
            existing.UpdatedAt = now;

            m_dirty = true;
            Flush();
            return;
        }

        auto added = acCharacter;
        added.CreatedAt = now;
        added.UpdatedAt = now;
        pRecord->Characters.push_back(added);

        m_dirty = true;
        Flush();
    }

    /**
     * Puts the current character aside so a new one can be made in its place.
     *
     * Retired rather than deleted. Rerolling is one click and a character is hours of
     * somebody's evening - "I clicked the wrong thing" needs a better answer than "it is
     * gone". Nothing reads the retired list yet; it exists so that answer can be written
     * later without a time machine.
     */
    bool RetireCharacter(const std::string& acDiscordId, int aSlot = -1)
    {
        auto* pRecord = FindMutable(acDiscordId);
        if (!pRecord)
            return false;

        const int slot = (aSlot < 0) ? pRecord->ActiveSlot : aSlot;

        for (auto it = pRecord->Characters.begin(); it != pRecord->Characters.end(); ++it)
        {
            if (it->Slot != slot)
                continue;

            auto retired = *it;
            retired.UpdatedAt = Now();
            pRecord->RetiredCharacters.push_back(retired);

            pRecord->Characters.erase(it);

            m_dirty = true;
            Flush();
            return true;
        }

        return false;
    }

    // Jail is written through immediately rather than waiting for the next timed flush.
    // Thirty seconds is a fine window to lose a position in; it is not a fine window in
    // which a sentence can vanish because the server went down.
    void SetJail(const std::string& acDiscordId, const std::string& acUsername,
                 int64_t aUntil, const glm::vec3& acCell,
                 const std::string& acBy, const std::string& acReason)
    {
        if (acDiscordId.empty())
            return;

        auto* pRecord = FindMutable(acDiscordId);

        if (!pRecord)
        {
            m_records.push_back({acDiscordId, acUsername});
            pRecord = &m_records.back();
        }

        pRecord->Username = acUsername;
        pRecord->JailedUntil = aUntil;
        pRecord->JailX = acCell.x;
        pRecord->JailY = acCell.y;
        pRecord->JailZ = acCell.z;
        pRecord->JailedBy = acBy;
        pRecord->JailReason = acReason;

        m_dirty = true;
        Flush();
    }

    void ClearJail(const std::string& acDiscordId)
    {
        auto* pRecord = FindMutable(acDiscordId);
        if (!pRecord)
            return;

        pRecord->JailedUntil = 0;
        pRecord->JailedBy.clear();
        pRecord->JailReason.clear();

        m_dirty = true;
        Flush();
    }

    PlayerRecord* FindMutable(const std::string& acDiscordId)
    {
        if (acDiscordId.empty())
            return nullptr;

        for (auto& record : m_records)
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
