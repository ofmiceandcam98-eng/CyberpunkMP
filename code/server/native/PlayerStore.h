#pragma once

#include <limits>   // numeric_limits, for the trade overflow guards - MSVC supplies this
                    // transitively, the GCC container the server builds in does not

#include <mutex>    // Flush is serialised - see its note

#include "AtomicWrite.h"
#include "CharacterRecord.h"

#include "PermissionLevel.h"
#include <algorithm>
#include <chrono>
#include <utility>
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
    /**
     * Is this number already somebody's?
     *
     * Retired characters count. Their numbers are not free to reissue: people still have
     * them saved, and handing a retired character's number to a new one silently redirects
     * every message meant for someone who no longer exists to someone who never knew them.
     */
    bool IsPhoneNumberTaken(const std::string& acNumber) const
    {
        if (acNumber.empty())
            return true;   // never hand out an empty number

        for (const auto& record : m_records)
        {
            for (const auto& character : record.Characters)
            {
                if (character.PhoneNumber == acNumber)
                    return true;
            }

            for (const auto& character : record.RetiredCharacters)
            {
                if (character.PhoneNumber == acNumber)
                    return true;
            }
        }

        return false;
    }

    /**
     * Whoever currently holds this number, or nullptr.
     *
     * Resolved at the moment of use rather than stored, because a contact list holds
     * numbers and the person behind a number is a separate question - one that has a
     * different answer after a character is retired.
     */
    /**
     * A character by its own id, across every account.
     *
     * Keyed on CharacterId rather than the Discord account deliberately. Cam's reason:
     * "just in case they rename their discord account". A display name is not an identifier
     * - it changes whenever somebody feels like it - and an admin repairing a mangled
     * character name needs to name the CHARACTER, not the person who happens to own it
     * today.
     *
     * Returns the owner's Discord id through apOwnerDiscordId, because SaveCharacter is
     * keyed on the account and the caller will need it to write the change back.
     */
    const CharacterRecord* FindCharacterById(const std::string& acCharacterId,
                                             std::string* apOwnerDiscordId = nullptr,
                                             std::string* apOwnerUsername = nullptr) const
    {
        if (acCharacterId.empty())
            return nullptr;

        // Normalise before comparing, because this is what a PERSON typed.
        //
        // Ids are stored grouped ("AEC-MJ6P") and read aloud without the hyphen. A correct
        // id typed as "aecmj6p" is the same id and must find the same character; a
        // mistyped one must find nothing rather than somebody else's. ParseCharacterId
        // strips case, spaces and hyphens, verifies the check symbol, and hands back the
        // stored form - so a typo fails HERE instead of resolving to a stranger.
        //
        // An unparseable id falls through to the raw comparison rather than refusing
        // outright: legacy 16-hex ids parse fine, but anything else stored before this
        // existed should still be findable by its exact text.
        std::string reason;
        const std::string normalised = ParseCharacterId(acCharacterId, &reason);
        const std::string& wanted = normalised.empty() ? acCharacterId : normalised;

        for (const auto& record : m_records)
        {
            for (const auto& character : record.Characters)
            {
                if (character.CharacterId != wanted)
                    continue;

                if (apOwnerDiscordId)
                    *apOwnerDiscordId = record.DiscordId;

                if (apOwnerUsername)
                    *apOwnerUsername = record.Username;

                return &character;
            }
        }

        return nullptr;
    }

    const CharacterRecord* FindCharacterByPhoneNumber(const std::string& acNumber,
                                                      std::string* apOwnerDiscordId = nullptr) const
    {
        if (acNumber.empty())
            return nullptr;

        for (const auto& record : m_records)
        {
            for (const auto& character : record.Characters)
            {
                if (character.PhoneNumber != acNumber)
                    continue;

                if (apOwnerDiscordId)
                    *apOwnerDiscordId = record.DiscordId;

                return &character;
            }
        }

        return nullptr;
    }

    /**
     * Contacts and quest grants change HERE, never through SaveCharacter.
     *
     * SaveCharacter deliberately carries these fields across an incoming save, because the
     * save is built from what the client reported and the client does not know about them.
     * That protection makes it useless as a way to CHANGE them - the carried-over value
     * would win over the new one, and the change would vanish with every sign of having
     * worked. Separate mutators keep both behaviours correct instead of trading one for the
     * other.
     *
     * All of them act on the ACTIVE character, and all return false when there is nothing
     * to act on, so a caller can say what happened rather than reporting success blindly.
     */
    bool AddContact(const std::string& acDiscordId, const std::string& acNumber,
                    const std::string& acName = {})
    {
        auto* pCharacter = FindCharacterMutable(acDiscordId);
        if (!pCharacter || acNumber.empty())
            return false;

        if (FindContact(*pCharacter, acNumber))
            return false;   // already theirs - the caller says so rather than duplicating it

        Contact contact;
        contact.Number = acNumber;
        contact.Name = acName;

        pCharacter->Contacts.push_back(std::move(contact));
        pCharacter->UpdatedAt = Now();

        m_dirty = true;
        Flush();
        return true;
    }

    /**
     * Rename an entry the character already has.
     *
     * Separate from AddContact rather than folded into it as an upsert. "Add" and "rename"
     * fail for different reasons and a player needs to be told which happened - an upsert
     * that silently creates a contact when somebody meant to rename one is how you end up
     * with a phone book full of numbers nobody recognises.
     *
     * An empty name CLEARS it, falling back to whoever holds the number. That is a real
     * thing to want and there is no other way to express it.
     */
    bool SetContactName(const std::string& acDiscordId, const std::string& acNumber,
                        const std::string& acName)
    {
        auto* pCharacter = FindCharacterMutable(acDiscordId);
        if (!pCharacter)
            return false;

        auto* pContact = FindContactMutable(*pCharacter, acNumber);
        if (!pContact)
            return false;

        pContact->Name = acName;
        pCharacter->UpdatedAt = Now();

        m_dirty = true;
        Flush();
        return true;
    }

    /**
     * Forget a contact. Deliberately does NOT touch anything else.
     *
     * Message history, payments and call records all survive, because they are records of
     * things that happened and deleting a phone book entry does not un-happen them. The
     * thread simply shows the number where the name used to be. This is why messages live
     * in their own store keyed on CharacterId rather than hanging off the contact list -
     * the separation is what makes this the easy behaviour instead of the careful one.
     */
    bool RemoveContact(const std::string& acDiscordId, const std::string& acNumber)
    {
        auto* pCharacter = FindCharacterMutable(acDiscordId);
        if (!pCharacter)
            return false;

        const auto before = pCharacter->Contacts.size();

        pCharacter->Contacts.erase(
            std::remove_if(pCharacter->Contacts.begin(), pCharacter->Contacts.end(),
                           [&acNumber](const Contact& acContact)
                           { return acContact.Number == acNumber; }),
            pCharacter->Contacts.end());

        if (pCharacter->Contacts.size() == before)
            return false;

        pCharacter->UpdatedAt = Now();
        m_dirty = true;
        Flush();
        return true;
    }

    // ------------------------------------------------------------------ blocking ----

    /**
     * Does this character refuse messages from that number?
     *
     * Const and cheap, because it is checked on the path of every message rather than only
     * when somebody asks. A block that is enforced at the command surface and not at the
     * delivery surface is not a block - it is a rule the next caller gets to forget.
     */
    bool IsBlocked(const std::string& acDiscordId, const std::string& acNumber) const
    {
        const auto* pCharacter = FindCharacter(acDiscordId);
        if (!pCharacter)
            return false;

        return std::find(pCharacter->Blocked.begin(), pCharacter->Blocked.end(), acNumber) !=
               pCharacter->Blocked.end();
    }

    // The same question asked of a character directly, for paths that already have one and
    // must not resolve an account a second time - a block is the ACTIVE character's, and
    // re-resolving invites the wrong one to answer.
    static bool IsBlockedBy(const CharacterRecord& acCharacter, const std::string& acNumber)
    {
        return std::find(acCharacter.Blocked.begin(), acCharacter.Blocked.end(), acNumber) !=
               acCharacter.Blocked.end();
    }

    bool Block(const std::string& acDiscordId, const std::string& acNumber)
    {
        auto* pCharacter = FindCharacterMutable(acDiscordId);
        if (!pCharacter || acNumber.empty())
            return false;

        if (std::find(pCharacter->Blocked.begin(), pCharacter->Blocked.end(), acNumber) !=
            pCharacter->Blocked.end())
            return false;

        pCharacter->Blocked.push_back(acNumber);
        pCharacter->UpdatedAt = Now();

        m_dirty = true;
        Flush();
        return true;
    }

    bool Unblock(const std::string& acDiscordId, const std::string& acNumber)
    {
        auto* pCharacter = FindCharacterMutable(acDiscordId);
        if (!pCharacter)
            return false;

        const auto before = pCharacter->Blocked.size();

        pCharacter->Blocked.erase(
            std::remove(pCharacter->Blocked.begin(), pCharacter->Blocked.end(), acNumber),
            pCharacter->Blocked.end());

        if (pCharacter->Blocked.size() == before)
            return false;

        pCharacter->UpdatedAt = Now();
        m_dirty = true;
        Flush();
        return true;
    }

    // A character's saved name for a number, or nullptr when they have not saved one.
    static const Contact* FindContact(const CharacterRecord& acCharacter,
                                      const std::string& acNumber)
    {
        for (const auto& contact : acCharacter.Contacts)
        {
            if (contact.Number == acNumber)
                return &contact;
        }

        return nullptr;
    }

    static Contact* FindContactMutable(CharacterRecord& aCharacter, const std::string& acNumber)
    {
        for (auto& contact : aCharacter.Contacts)
        {
            if (contact.Number == acNumber)
                return &contact;
        }

        return nullptr;
    }

    bool AllowQuest(const std::string& acDiscordId, const std::string& acQuest)
    {
        auto* pCharacter = FindCharacterMutable(acDiscordId);
        if (!pCharacter || acQuest.empty())
            return false;

        if (std::find(pCharacter->AllowedQuests.begin(), pCharacter->AllowedQuests.end(), acQuest) !=
            pCharacter->AllowedQuests.end())
            return false;

        pCharacter->AllowedQuests.push_back(acQuest);
        pCharacter->UpdatedAt = Now();

        m_dirty = true;
        Flush();
        return true;
    }

    bool DenyQuest(const std::string& acDiscordId, const std::string& acQuest)
    {
        auto* pCharacter = FindCharacterMutable(acDiscordId);
        if (!pCharacter)
            return false;

        const auto before = pCharacter->AllowedQuests.size();

        pCharacter->AllowedQuests.erase(
            std::remove(pCharacter->AllowedQuests.begin(), pCharacter->AllowedQuests.end(), acQuest),
            pCharacter->AllowedQuests.end());

        if (pCharacter->AllowedQuests.size() == before)
            return false;

        pCharacter->UpdatedAt = Now();
        m_dirty = true;
        Flush();
        return true;
    }

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

            // Server-owned fields, carried across the assignment below.
            //
            // `existing = acCharacter` replaces the whole record, and the caller is usually
            // a save built from what the CLIENT reported - which knows nothing about phone
            // numbers, contacts or quest grants and therefore sends them empty. Without
            // this, every autosave would quietly erase a player's contact list and an
            // admin's quest grants, and the damage would look exactly like a save that
            // worked. Same shape as the CreatedAt line above, and the same reason.
            const auto number = existing.PhoneNumber;
            const auto contacts = existing.Contacts;
            const auto blocked = existing.Blocked;
            const auto allowed = existing.AllowedQuests;

            existing = acCharacter;
            existing.CreatedAt = created ? created : now;
            existing.UpdatedAt = now;

            if (!number.empty())
                existing.PhoneNumber = number;
            if (!contacts.empty())
                existing.Contacts = contacts;
            if (!blocked.empty())
                existing.Blocked = blocked;
            if (!allowed.empty())
                existing.AllowedQuests = allowed;

            if (existing.PhoneNumber.empty())
                existing.PhoneNumber = MakeUniquePhoneNumber();

            m_dirty = true;
            Flush();
            return;
        }

        auto added = acCharacter;
        added.CreatedAt = now;
        added.UpdatedAt = now;

        if (added.PhoneNumber.empty())
            added.PhoneNumber = MakeUniquePhoneNumber();

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
    /**
     * How many character slots an account has, from its role.
     *
     * One for a player, four for staff. Cam's rule: admins and above get four.
     *
     * A PERMISSION, so the server decides it and the client is told. A client that works out
     * its own allowance is a client that can grant itself an allowance.
     */
    static int SlotsForLevel(EPermissionLevel aLevel)
    {
        // EVERY staff rank, support included. Cam's call, 2026-09-02: "all should get 4
        // character slots", and support "just gets extra character slots, nothing else".
        //
        // kSupport is the LOWEST staff rung, so this is the widest staff test there is -
        // and it is deliberately the only check support satisfies. Everything else on the
        // server asks for `>= kModerator` or higher, which support sits below.
        return aLevel >= EPermissionLevel::kSupport ? kStaffSlots : kPlayerSlots;
    }

    static constexpr int kPlayerSlots = 1;
    static constexpr int kStaffSlots = 4;

    /**
     * The most rows one account may EVER write, across every character it has ever had.
     *
     * Separate from the slot count, and it exists because deletion is soft: a retired
     * character keeps its row so its id is never reissued and the deletion can be undone.
     * Which means create-and-delete-and-create writes a new row every single time, and
     * without a ceiling one account can grow the file forever.
     *
     * Sixty is not a considered figure - it is "far more than anybody will legitimately use,
     * and far fewer than a script can produce in an afternoon".
     */
    static constexpr int kLifetimeRowCeiling = 60;

    /**
     * Whether this account may create a character in this slot, and why not if not.
     *
     * Returns an empty string for yes, or a stable refusal code. Codes rather than sentences
     * so the caller can branch, log and render them - "slot_taken" survives translation and a
     * log grep in a way that "That slot already has a character on it" does not.
     */
    std::string MayCreateInSlot(const std::string& acDiscordId, int aSlot, EPermissionLevel aLevel) const
    {
        const int slots = SlotsForLevel(aLevel);

        if (aSlot < 0 || aSlot >= slots)
            return "slot_out_of_range";

        const auto* pRecord = Find(acDiscordId);
        if (!pRecord)
            return {}; // no record yet: the first character of a new account

        for (const auto& character : pRecord->Characters)
        {
            if (character.Slot == aSlot)
                return "slot_taken";
        }

        // Live and retired both count. The ceiling is about rows written, not characters
        // held - that is the whole reason it is separate from the slot count.
        const size_t rows = pRecord->Characters.size() + pRecord->RetiredCharacters.size();

        if (rows >= static_cast<size_t>(kLifetimeRowCeiling))
            return "row_ceiling";

        return {};
    }

    /**
     * The live characters on an account, for the selector.
     *
     * Returns them as they are stored, INCLUDING their slot numbers, which are not
     * contiguous: retiring the character in slot 1 of three leaves slots 0 and 2 occupied.
     * The caller draws holes where the gaps are rather than renumbering, because a slot
     * number that moves is a slot number somebody's UI is about to act on wrongly.
     */
    const std::vector<CharacterRecord>* GetCharacters(const std::string& acDiscordId) const
    {
        const auto* pRecord = Find(acDiscordId);
        return pRecord ? &pRecord->Characters : nullptr;
    }

    /**
     * Play as the character in this slot.
     *
     * Refuses rather than falling back to slot 0. "You asked for a character that is not
     * there, so here is a different one" is how somebody ends up playing, and saving over,
     * a character they did not choose.
     */
    std::string SelectSlot(const std::string& acDiscordId, int aSlot)
    {
        auto* pRecord = FindMutable(acDiscordId);
        if (!pRecord)
            return "no_account";

        for (const auto& character : pRecord->Characters)
        {
            if (character.Slot != aSlot)
                continue;

            if (pRecord->ActiveSlot != aSlot)
            {
                pRecord->ActiveSlot = aSlot;
                m_dirty = true;
                Flush();
            }

            return {};
        }

        return "empty_slot";
    }

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

    /**
     * The active character, writable. Mirrors FindCharacter's slot rule so a mutation and a
     * read never disagree about which character "theirs" means.
     */
    CharacterRecord* FindCharacterMutable(const std::string& acDiscordId, int aSlot = -1)
    {
        auto* pRecord = FindMutable(acDiscordId);
        if (!pRecord)
            return nullptr;

        const int slot = (aSlot < 0) ? pRecord->ActiveSlot : aSlot;

        for (auto& character : pRecord->Characters)
        {
            if (character.Slot == slot)
                return &character;
        }

        return nullptr;
    }

    // ------------------------------------------------------------------- trading ----

    /**
     * One side of an exchange: what this character GIVES.
     *
     * Addressed by CharacterId, like everything else that belongs to a character rather
     * than an account. A player's two characters are two traders.
     */
    struct TradeSide
    {
        std::string CharacterId;
        int64_t Money{0};
        std::vector<CharacterRecord::ItemStack> Items;
    };

    /**
     * Move both sides at once, or move nothing.
     *
     * HERE, IN THE STORE, and not in a trade system beside it. This file is already the
     * authority for money and possessions; a second thing that could also move them would
     * be a second authority, and the two would disagree the first time either changed.
     * The trade system decides WHETHER an exchange happens. This decides that it happens
     * completely.
     *
     * ATOMICITY, and honestly what kind. Both records are copied, both copies are mutated,
     * everything is validated against the copies, and only then are they written back -
     * followed by ONE flush. So there is no ordering in which a partial exchange becomes
     * visible to anything else on the server, and no window where one side has paid and
     * the other has not.
     *
     * What this is NOT is durable-transactional. The backing store is a JSON file; if the
     * process dies inside Flush the file can be truncated, and no amount of care here fixes
     * that. The brief asks for a write-ahead journal, and that belongs with the move to a
     * database rather than bolted onto a file - which is also where the replicable-instance
     * rule takes it. The audit ledger records every completed trade in the meantime, which
     * is what makes a bad outcome repairable rather than invisible.
     *
     * Returns false and changes NOTHING on any failure, with a stable reason.
     */
    bool ApplyTrade(const TradeSide& acLeft, const TradeSide& acRight, std::string* apReason = nullptr)
    {
        const auto fail = [apReason](const char* acpWhy)
        {
            if (apReason)
                *apReason = acpWhy;
            return false;
        };

        if (acLeft.CharacterId == acRight.CharacterId)
            return fail("same_character");

        auto* pLeftRecord = FindRecordByCharacterId(acLeft.CharacterId);
        auto* pRightRecord = FindRecordByCharacterId(acRight.CharacterId);

        if (!pLeftRecord || !pRightRecord)
            return fail("no_character");

        auto* pLeft = FindCharacterInRecord(*pLeftRecord, acLeft.CharacterId);
        auto* pRight = FindCharacterInRecord(*pRightRecord, acRight.CharacterId);

        if (!pLeft || !pRight)
            return fail("no_character");

        // Copies. Every mutation below happens on these, so a validation failure halfway
        // through leaves the real records untouched rather than half-traded.
        CharacterRecord left = *pLeft;
        CharacterRecord right = *pRight;

        if (!MoveAssets(left, right, acLeft, apReason))
            return false;

        if (!MoveAssets(right, left, acRight, apReason))
            return false;

        // Only now does anything real change.
        *pLeft = left;
        *pRight = right;

        const auto now = Now();
        pLeft->UpdatedAt = now;
        pRight->UpdatedAt = now;

        m_dirty = true;
        Flush();

        if (apReason)
            apReason->clear();

        return true;
    }

    /**
     * What a character can actually spend, given what they have already promised.
     *
     * The brief's concurrency case: somebody with 10,000 who has offered 8,000 in a live
     * trade must not also be able to send 8,000 by phone. Reserved money is not available
     * money, and every path that spends has to ask the same question or the reservation is
     * decorative.
     *
     * The reserved figure comes from the caller - the trade system owns live trades and
     * this file does not know about them - so that this stays the single authority on what
     * is OWNED without also becoming the authority on what is promised.
     */
    static int64_t AvailableMoney(const CharacterRecord& acCharacter, int64_t aReserved)
    {
        const auto available = acCharacter.Money - aReserved;
        return available > 0 ? available : 0;
    }

    // How many of an item a character holds. Zero for one they do not.
    static uint32_t HeldQuantity(const CharacterRecord& acCharacter, uint64_t aItemId)
    {
        for (const auto& stack : acCharacter.Inventory)
        {
            if (stack.Id == aItemId)
                return stack.Quantity;
        }

        return 0;
    }

    // Writes only when something actually changed, so calling this on a timer is cheap.
    /**
     * Persist every record, without ever leaving players.json truncated.
     *
     * This used to open the live file with an ofstream, which TRUNCATES it, and then stream
     * a freshly serialised document into it. Between those two moments the authoritative
     * copy of every character on the server did not exist - and a crash, a power cut, a full
     * disk or a killed container in that window left an empty or half-written file with no
     * backup to fall back on.
     *
     * See AtomicWrite for the replacement sequence and why its ORDER is the design.
     *
     * SERIALISED FIRST, INTO MEMORY. A json exception now happens before anything on disk
     * has been touched, so a record that cannot be serialised costs a log line rather than
     * the database.
     *
     * LOCKED. The three callers - the thirty-second tick, the disconnect path and the
     * destructor - are all on the game thread today, but a shutdown racing a tick would
     * interleave two serialisations into one file, and a file that is half one state and
     * half another is corrupt in a way that reads as valid JSON. The mutex is uncontended
     * on the normal path and removes the whole class.
     *
     * m_dirty is cleared ONLY on success. A failed write leaves the store dirty so the next
     * tick tries again, rather than silently believing it has been saved.
     */
    void Flush()
    {
        if (!m_dirty || m_path.empty())
            return;

        std::lock_guard<std::mutex> lock(m_writeMutex);

        // Re-checked under the lock: another caller may have written it while we waited.
        if (!m_dirty)
            return;

        std::string payload;

        try
        {
            payload = nlohmann::json(m_records).dump(2);
        }
        catch (const std::exception& e)
        {
            spdlog::error("Could not serialise {}: {}. The file on disk is UNCHANGED and still "
                          "valid; nothing was written.",
                          m_path.string(), e.what());
            return;
        }

        std::string reason;

        if (!AtomicWrite::Replace(m_path, payload, &reason))
        {
            spdlog::error("Could not write {}: {}. The previous file is intact - it will be "
                          "retried on the next save.",
                          m_path.string(), reason);
            return;
        }

        // Non-fatal detail from a successful write - a backup that could not be taken.
        if (!reason.empty())
            spdlog::warn("{}: {}", m_path.string(), reason);

        m_dirty = false;
    }

private:
    PlayerRecord* FindRecordByCharacterId(const std::string& acCharacterId)
    {
        for (auto& record : m_records)
        {
            for (auto& character : record.Characters)
            {
                if (character.CharacterId == acCharacterId)
                    return &record;
            }
        }

        return nullptr;
    }

    static CharacterRecord* FindCharacterInRecord(PlayerRecord& aRecord,
                                                  const std::string& acCharacterId)
    {
        for (auto& character : aRecord.Characters)
        {
            if (character.CharacterId == acCharacterId)
                return &character;
        }

        return nullptr;
    }

    /**
     * Take what one side offered off them and put it on the other. Conserving, always.
     *
     * The governing rule of the whole trade system: it never CREATES value, it only moves
     * value that already exists. So every removal is checked against what is actually held,
     * and every addition is exactly what was removed - no rounding, no clamping to zero
     * that would silently vaporise the remainder, and no path where a quantity is added
     * without having been taken.
     *
     * Operates on the caller's COPIES. Returning false mid-way leaves those copies
     * inconsistent, which is exactly why ApplyTrade validates on copies and only assigns
     * the real records once both directions have succeeded.
     */
    static bool MoveAssets(CharacterRecord& aFrom, CharacterRecord& aTo, const TradeSide& acOffer,
                           std::string* apReason)
    {
        const auto fail = [apReason](const char* acpWhy)
        {
            if (apReason)
                *apReason = acpWhy;
            return false;
        };

        if (acOffer.Money < 0)
            return fail("negative_money");   // "pay me" is theft with extra steps

        if (aFrom.Money < acOffer.Money)
            return fail("insufficient_funds");

        // Items first, so a failure on the last stack does not leave money already moved
        // in this copy - it costs nothing and keeps the failure shape uniform.
        for (const auto& offered : acOffer.Items)
        {
            if (offered.Quantity == 0)
                return fail("zero_quantity");

            auto* pStack = FindStack(aFrom, offered.Id);

            if (!pStack || pStack->Quantity < offered.Quantity)
                return fail("insufficient_items");

            pStack->Quantity -= offered.Quantity;
        }

        // Erase what is now empty. A zero-quantity stack is not a holding, and leaving one
        // behind makes "do they have any" answer yes forever.
        aFrom.Inventory.erase(std::remove_if(aFrom.Inventory.begin(), aFrom.Inventory.end(),
                                             [](const CharacterRecord::ItemStack& acStack)
                                             { return acStack.Quantity == 0; }),
                              aFrom.Inventory.end());

        /*
         * OVERFLOW IS CHECKED ON THE WAY IN, not hoped away.
         *
         * The brief's section 17 lists integer overflow among the duplication exploits to
         * defend against, and until now neither add was guarded. Quantity is uint32_t and
         * Money is int64_t, so a large enough receiving side wraps: a stack of 4.29 billion
         * plus ten becomes nine, and the difference is not moved anywhere - it is destroyed.
         * The same arithmetic run the other way mints value out of nothing.
         *
         * WHY IT IS REACHABLE AT ALL, which is the part worth writing down. The offer itself
         * is bounded - a sender cannot offer more than they hold, checked above. But what
         * they HOLD arrives from SaveCharacterRequest, which is the client reporting its own
         * inventory, so a hostile client can claim to be carrying four billion of something.
         * Section 24 is explicit that every client is assumed malicious; that makes this a
         * live path rather than a theoretical one.
         *
         * Refusing is right rather than clamping. A clamp silently changes the trade both
         * players agreed to, and "you now have fewer than we said" is a support ticket
         * nobody can reconstruct. ApplyTrade validates on copies, so a refusal here leaves
         * both records exactly as they were.
         */
        for (const auto& offered : acOffer.Items)
        {
            if (auto* pStack = FindStack(aTo, offered.Id))
            {
                constexpr auto kMaxQuantity = std::numeric_limits<uint32_t>::max();

                if (pStack->Quantity > kMaxQuantity - offered.Quantity)
                    return fail("quantity_overflow");

                pStack->Quantity += offered.Quantity;
            }
            else
            {
                aTo.Inventory.push_back({offered.Id, offered.Quantity});
            }
        }

        // Same reasoning for money. acOffer.Money is already known non-negative and no
        // greater than aFrom.Money, so this is the only remaining direction that can wrap.
        if (acOffer.Money > 0 && aTo.Money > std::numeric_limits<int64_t>::max() - acOffer.Money)
            return fail("money_overflow");

        aFrom.Money -= acOffer.Money;
        aTo.Money += acOffer.Money;

        return true;
    }

    static CharacterRecord::ItemStack* FindStack(CharacterRecord& aCharacter, uint64_t aItemId)
    {
        for (auto& stack : aCharacter.Inventory)
        {
            if (stack.Id == aItemId)
                return &stack;
        }

        return nullptr;
    }

    /**
     * A number nobody else has.
     *
     * Retried rather than accepted, like vehicle plates. Bounded so a full number space
     * cannot hang the tick loop - and unlike a plate, a duplicate here is not a cosmetic
     * problem, so exhausting the attempts returns empty and the caller reports a failure
     * instead of issuing a number that already rings somebody else's phone.
     */
    std::string MakeUniquePhoneNumber() const
    {
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            auto candidate = GeneratePhoneNumber();

            if (!IsPhoneNumberTaken(candidate))
                return candidate;
        }

        spdlog::error("Could not find a free phone number after 64 attempts - the number "
                      "space may be exhausted. Character left without one.");
        return {};
    }

    std::vector<PlayerRecord> m_records;
    std::filesystem::path m_path;
    // Serialises Flush. Uncontended on the normal path; see Flush.
    mutable std::mutex m_writeMutex;
    bool m_dirty{false};
};
