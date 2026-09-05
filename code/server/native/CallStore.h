#pragma once

/**
 * Player-to-player phone calls, owned by the server.
 *
 * WHY THIS DOES NOT TOUCH THE GAME'S PHONE SYSTEM
 *
 * The mod blocks every call the game tries to make. `PhoneSystem.OnTriggerCall` is wrapped
 * in Quests.reds and refuses unconditionally while connected, because a call from the
 * game is a call from a singleplayer story that nobody on this server is in - Songbird
 * being the one people notice, and roughly twenty other Phantom Liberty holocalls being
 * the ones they would notice next.
 *
 * The obvious way to add player calls is to relax that gate: let a call through when it
 * looks player-initiated. That is the wrong shape, and it is worth saying why at length
 * because it will keep looking right.
 *
 * `OnTriggerCall` takes a `questTriggerCallRequest`. That is not a general "make a call"
 * entry point - it is the QUEST SYSTEM's request type, and the quest system is the only
 * thing that builds one. Its `isPlayerTriggered` field does NOT mean "a multiplayer
 * player started this call". It means "the player triggered this QUEST call", which is
 * what happens when somebody rings a fixer back out of their journal - the same
 * singleplayer story arriving through a different door.
 *
 * So relaxing the gate on that field would let a class of story calls back in, would make
 * the Songbird suppression depend on a field the mod does not populate and cannot audit,
 * and would give the two kinds of call one code path where telling them apart is a
 * judgement call made at runtime.
 *
 * Instead, a player call NEVER BECOMES A questTriggerCallRequest. It arrives as a network
 * request, lives here, and is presented by the mod. The two kinds of call have no shared
 * field and no shared entry point, so the origin is unambiguous BY CONSTRUCTION rather
 * than by inspection - there is nothing for a game call to set that would make it look
 * like a player call, because the two never meet.
 *
 * The consequence is the one that matters: the Songbird gate is not modified by this
 * feature at all, so it cannot be regressed by it. "Player calls work AND the prologue is
 * still blocked" is not a test that has to pass; it is a property of the shape.
 *
 * WHAT A CALL IS MADE OF
 *
 * Everything here is addressed by CharacterId. A player with two characters has two
 * phones, two histories and two sets of blocks, and switching characters ends whatever
 * the old one was doing rather than carrying it across - see EndFor.
 *
 * Sessions are IN MEMORY, history is on disk. A call is a conversation, not property: if
 * the server restarts mid-call the right outcome is that the call ended, which is exactly
 * what an empty session list means. The same reasoning as ChatSystem::PendingSale.
 */

#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <cstdio>   // snprintf - MSVC pulls this in transitively, GCC does not
#include <fstream>
#include <filesystem>
#include <chrono>
#include <random>

#include <nlohmann/json.hpp>
#include <utility>

/**
 * How long a phone rings before it is a missed call, in seconds.
 *
 * Long enough to walk across a room, short enough that a caller is not left listening to
 * nothing. A ring that never times out is worse than either: both characters are stuck in
 * a call state nothing will clear, and the only way out is a reconnect.
 */
inline constexpr int64_t kCallRingSeconds = 30;

/**
 * How many history entries a character keeps.
 *
 * Capped for the same reason conversations are: nothing here is deleted by a player, and
 * an uncapped list is only ever loaded in full. Oldest-first, so what survives is the
 * recent history rather than the first calls the character ever made.
 */
inline constexpr size_t kCallHistoryCap = 100;

/**
 * Where a call is, decided by the server and only displayed by clients.
 *
 * Dialing exists as a distinct state even though the server moves through it in the same
 * breath as Ringing. It marks the window in which validation runs, and a call that fails
 * validation ends in Failed FROM Dialing - which is what makes "the call never rang"
 * distinguishable from "it rang and nobody picked up" in the history.
 */
enum class CallState : uint32_t
{
    Dialing = 0,
    Ringing = 1,
    Connected = 2,
    Declined = 3,
    Missed = 4,
    Busy = 5,
    Ended = 6,
    Cancelled = 7,
    Failed = 8,
};

inline const char* CallStateName(CallState aState)
{
    switch (aState)
    {
    case CallState::Dialing: return "dialing";
    case CallState::Ringing: return "ringing";
    case CallState::Connected: return "connected";
    case CallState::Declined: return "declined";
    case CallState::Missed: return "missed";
    case CallState::Busy: return "busy";
    case CallState::Ended: return "ended";
    case CallState::Cancelled: return "cancelled";
    case CallState::Failed: return "failed";
    }

    return "?";
}

inline bool IsCallOver(CallState aState)
{
    return aState != CallState::Dialing && aState != CallState::Ringing &&
           aState != CallState::Connected;
}

/**
 * One line in a character's call history.
 *
 * Written as TWO entries per call, one per participant, rather than one row read from
 * both ends. Same reasoning as the money ledger: a character's history is then a filter on
 * CharacterId alone, with no need to know which side of a join they were on, and a
 * participant who is later deleted does not take the other's history with them.
 */
struct CallHistoryEntry
{
    std::string CallId;

    // Whose history this line belongs to. The filter key.
    std::string CharacterId;

    // "out" or "in". Stored rather than derived, because deriving it needs the other
    // participant's record, which may be gone by the time anybody reads this.
    std::string Direction;

    // The other end, as it was AT THE TIME. A number rather than only a character id, so a
    // history line still says something after the other character is retired.
    std::string OtherNumber;
    std::string OtherCharacterId;

    int64_t StartedAt{0};

    // Seconds actually connected. Zero for anything that never connected, which is what
    // makes "missed" and "two-second wrong number" different lines.
    int64_t Duration{0};

    // The terminal state, by name: completed, missed, declined, busy, cancelled, failed.
    std::string Result;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(CallHistoryEntry, CallId, CharacterId, Direction,
                                                OtherNumber, OtherCharacterId, StartedAt,
                                                Duration, Result)
};

/**
 * A call in progress. Never persisted - see the file comment.
 */
struct CallSession
{
    std::string CallId;

    std::string CallerCharacterId;
    std::string CallerNumber;

    std::string TargetCharacterId;
    std::string TargetNumber;

    CallState State{CallState::Dialing};

    int64_t CreatedAt{0};
    int64_t ConnectedAt{0};

    bool Involves(const std::string& acCharacterId) const
    {
        return CallerCharacterId == acCharacterId || TargetCharacterId == acCharacterId;
    }

    std::string Other(const std::string& acCharacterId) const
    {
        if (CallerCharacterId == acCharacterId)
            return TargetCharacterId;
        if (TargetCharacterId == acCharacterId)
            return CallerCharacterId;
        return {};
    }
};

class CallStore
{
public:
    void Load(const std::filesystem::path& acPath)
    {
        m_path = acPath;

        std::ifstream file(m_path);
        if (!file.is_open())
            return;

        try
        {
            nlohmann::json json;
            file >> json;

            m_history = json.get<std::vector<CallHistoryEntry>>();

            spdlog::info("Loaded {} call history entries from {}", m_history.size(),
                         m_path.string());
        }
        catch (const std::exception& e)
        {
            // Loud and non-destructive, like MessageStore. An empty in-memory list after a
            // failed parse must never be written back over the file it failed to read.
            spdlog::error("Could not read {}: {}. Call history is NOT loaded and will not be "
                          "written until this is fixed.", m_path.string(), e.what());
            m_readable = false;
        }
    }

    // ------------------------------------------------------------------ sessions ----

    /**
     * Whatever call this character is in, or nullptr.
     *
     * One at a time, deliberately. Call waiting is a feature; two live sessions naming the
     * same character is a bug, and every function that answers "the call" would have to
     * pick one.
     */
    CallSession* Active(const std::string& acCharacterId)
    {
        if (acCharacterId.empty())
            return nullptr;

        for (auto& session : m_sessions)
        {
            if (!IsCallOver(session.State) && session.Involves(acCharacterId))
                return &session;
        }

        return nullptr;
    }

    const CallSession* Active(const std::string& acCharacterId) const
    {
        return const_cast<CallStore*>(this)->Active(acCharacterId);
    }

    CallSession* Find(const std::string& acCallId)
    {
        if (acCallId.empty())
            return nullptr;

        for (auto& session : m_sessions)
        {
            if (session.CallId == acCallId)
                return &session;
        }

        return nullptr;
    }

    /**
     * Start ringing. The caller has already been validated by whoever calls this.
     *
     * Validation lives at the call site rather than here because it needs the player store
     * - who holds a number, who has blocked whom, who is online - and threading all of
     * that through would make this file depend on most of the server. What this DOES own
     * is the state machine, which is the part that must not be reimplemented per surface.
     */
    CallSession& Begin(const std::string& acCallerCharacterId, const std::string& acCallerNumber,
                       const std::string& acTargetCharacterId, const std::string& acTargetNumber)
    {
        CallSession session;
        session.CallId = GenerateCallId();
        session.CallerCharacterId = acCallerCharacterId;
        session.CallerNumber = acCallerNumber;
        session.TargetCharacterId = acTargetCharacterId;
        session.TargetNumber = acTargetNumber;
        session.State = CallState::Ringing;
        session.CreatedAt = Now();

        m_sessions.push_back(std::move(session));
        return m_sessions.back();
    }

    /**
     * Move a call to a terminal state and write both history lines.
     *
     * The ONE place a call ends. Everything - answer timeout, decline, hang up,
     * disconnect, character switch - comes through here, so a call cannot end without
     * being recorded and cannot be recorded twice.
     */
    void End(CallSession& aSession, CallState aState)
    {
        if (IsCallOver(aSession.State))
            return;   // already ended; a second ending would write a second pair of lines

        const auto now = Now();

        const int64_t duration =
            aSession.ConnectedAt > 0 ? std::max<int64_t>(0, now - aSession.ConnectedAt) : 0;

        aSession.State = aState;

        const char* result = "failed";

        switch (aState)
        {
        case CallState::Ended: result = "completed"; break;
        case CallState::Declined: result = "declined"; break;
        case CallState::Missed: result = "missed"; break;
        case CallState::Busy: result = "busy"; break;
        case CallState::Cancelled: result = "cancelled"; break;
        default: break;
        }

        Record({aSession.CallId, aSession.CallerCharacterId, "out", aSession.TargetNumber,
                aSession.TargetCharacterId, aSession.CreatedAt, duration, result});

        Record({aSession.CallId, aSession.TargetCharacterId, "in", aSession.CallerNumber,
                aSession.CallerCharacterId, aSession.CreatedAt, duration, result});

        Flush();
    }

    /**
     * End whatever this character is in, for a reason that is not their choosing.
     *
     * Disconnects, crashes and CHARACTER SWITCHES all land here. The switch is the one
     * worth naming: a call belongs to the character that made it, so somebody swapping to
     * their other character must not carry it across - the new character has its own
     * phone, and the person on the other end is talking to somebody who has left.
     *
     * Returns the session so the caller can tell the other party. Ending a call silently
     * leaves them holding a phone that will never do anything again.
     */
    CallSession* EndFor(const std::string& acCharacterId, CallState aState)
    {
        auto* pSession = Active(acCharacterId);

        if (!pSession)
            return nullptr;

        End(*pSession, aState);
        return pSession;
    }

    /**
     * Calls that have rung for too long. Called from the server tick.
     *
     * Returns them rather than ending them, because the caller has to notify both sides
     * and cannot do that once the session is gone. Ending is still done through End().
     */
    std::vector<CallSession*> Expired()
    {
        std::vector<CallSession*> expired;

        const auto now = Now();

        for (auto& session : m_sessions)
        {
            if (session.State != CallState::Ringing)
                continue;

            if (now - session.CreatedAt >= kCallRingSeconds)
                expired.push_back(&session);
        }

        return expired;
    }

    /**
     * Drop finished sessions.
     *
     * Separate from End so that a caller can end a call, notify both sides from the
     * session, and only then have it swept - rather than the session vanishing underneath
     * them mid-notification.
     */
    void Sweep()
    {
        m_sessions.remove_if([](const CallSession& acSession)
                             { return IsCallOver(acSession.State); });
    }

    /**
     * Is this character in a CONNECTED call, and with whom?
     *
     * Used by the voice path, which runs per frame per speaker and must stay cheap.
     * Returns the other participant's id, or empty.
     */
    std::string ConnectedPartner(const std::string& acCharacterId) const
    {
        for (const auto& session : m_sessions)
        {
            if (session.State == CallState::Connected && session.Involves(acCharacterId))
                return session.Other(acCharacterId);
        }

        return {};
    }

    // ------------------------------------------------------------------- history ----

    std::vector<CallHistoryEntry> History(const std::string& acCharacterId,
                                          size_t aLimit = 15) const
    {
        std::vector<CallHistoryEntry> mine;

        for (const auto& entry : m_history)
        {
            if (entry.CharacterId == acCharacterId)
                mine.push_back(entry);
        }

        std::sort(mine.begin(), mine.end(),
                  [](const CallHistoryEntry& acLeft, const CallHistoryEntry& acRight)
                  {
                      // CallId as the tiebreak so the order is total - two calls can share
                      // a second, and an unstable sort would shuffle them between reads.
                      if (acLeft.StartedAt != acRight.StartedAt)
                          return acLeft.StartedAt > acRight.StartedAt;
                      return acLeft.CallId > acRight.CallId;
                  });

        if (mine.size() > aLimit)
            mine.resize(aLimit);

        return mine;
    }

    void Flush()
    {
        if (!m_dirty || m_path.empty() || !m_readable)
            return;

        try
        {
            std::filesystem::create_directories(m_path.parent_path());

            std::ofstream file(m_path);
            file << nlohmann::json(m_history).dump(2);

            m_dirty = false;
        }
        catch (const std::exception& e)
        {
            spdlog::error("Could not write {}: {}", m_path.string(), e.what());
        }
    }

private:
    static int64_t Now()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Not the character-id format. Nobody reads a call id aloud, so it needs no check
    // symbol and no restricted alphabet - only that two never collide.
    static std::string GenerateCallId()
    {
        static std::mt19937_64 engine{std::random_device{}()};

        char buffer[17] = {};
        std::snprintf(buffer, sizeof(buffer), "%016llx",
                      static_cast<unsigned long long>(engine()));

        return buffer;
    }

    void Record(CallHistoryEntry aEntry)
    {
        // A line with nobody to file it under is dropped rather than stored. It could
        // never be read back - History filters on CharacterId - so keeping it would only
        // grow the file.
        if (aEntry.CharacterId.empty())
            return;

        const auto owner = aEntry.CharacterId;

        m_history.push_back(std::move(aEntry));

        // Trimmed PER CHARACTER, never globally. A global cap lets one busy character
        // evict everybody else's history, which is how a shared cap always fails.
        size_t mine = 0;

        for (const auto& entry : m_history)
        {
            if (entry.CharacterId == owner)
                ++mine;
        }

        if (mine > kCallHistoryCap)
        {
            // The oldest line belonging to THIS character. m_history is append-ordered, so
            // the first match is the oldest, and everybody else's lines are untouched.
            const auto oldest = std::find_if(m_history.begin(), m_history.end(),
                                             [&owner](const CallHistoryEntry& acEntry)
                                             { return acEntry.CharacterId == owner; });

            if (oldest != m_history.end())
                m_history.erase(oldest);
        }

        m_dirty = true;
    }

    /**
     * A LIST, not a vector, and that is not a style choice.
     *
     * Active(), Find() and Expired() all hand out pointers into this container, and a
     * caller holds one while notifying both sides of a call - which can start another.
     * A vector would reallocate on that push_back and every outstanding pointer would
     * dangle, intermittently and only on a busy server. A list's references survive
     * insertion and erasure of other elements, which removes the whole class of bug
     * rather than relying on nobody ever holding a pointer across a Begin().
     */
    std::list<CallSession> m_sessions;
    std::vector<CallHistoryEntry> m_history;

    std::filesystem::path m_path;
    bool m_dirty{false};
    bool m_readable{true};
};
