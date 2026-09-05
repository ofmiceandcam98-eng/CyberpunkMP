#pragma once

/**
 * Text messages between characters, owned by the server.
 *
 * WHY THIS IS NOT IN CharacterRecord
 *
 * Contacts and phone numbers live on the character, because they belong to exactly one.
 * A message does not. It has a sender and a recipient, and storing it on either one is
 * wrong in a way that only shows up later:
 *
 *  - Stored on both, it is duplicated, and the two copies disagree the first time one is
 *    edited, deleted or trimmed. "Which copy is the message" has no good answer.
 *  - Stored on one, the other cannot read their own inbox without walking every account
 *    on the server looking for messages addressed to them.
 *  - Either way it rides inside CharacterRecord, which SaveCharacter REPLACES wholesale
 *    from what a client reported. A message that arrives between a client's read and its
 *    write is silently gone, and nothing anywhere reports an error. That is the same
 *    class of bug as the money thrash, and money at least gets shouted about.
 *
 * So messages are their own store, addressed by CharacterId.
 *
 * ADDRESSED BY CHARACTER, NEVER BY ACCOUNT
 *
 * This is the single load-bearing decision in this file. A player has one Discord id and
 * up to four characters, and the moment a second slot exists, "this account's messages"
 * stops meaning anything. Two characters belonging to the same person are strangers, and
 * one must not be able to read the other's inbox - that is not a nicety, it is most of
 * what makes a second character worth having.
 *
 * Keying on CharacterId makes that structural rather than a rule somebody has to
 * remember. There is no code path here that can be handed an account, so there is no code
 * path here that can leak across one. Every function takes character ids; the caller does
 * the account-to-active-character resolution once, where the connection is.
 *
 * OFFLINE IS THE NORMAL CASE
 *
 * Most messages are sent to somebody who is not online - that is what distinguishes a
 * phone from proximity chat, which the server already has. A message is therefore written
 * to disk FIRST and delivered second, and delivery is a property of the message rather
 * than an event. A recipient who was offline, who crashed, or whose client dropped the
 * push gets it on their next arrival, because "has this been shown to them" is stored
 * rather than assumed.
 *
 * REPLICABLE INSTANCES
 *
 * Like players.json this is a local file today, and like players.json that is the thing
 * that has to change before a second instance can run. It is called out here rather than
 * left implicit: two instances each holding half of a conversation is worse than one
 * instance holding all of it, because the failure is silent - each side sees a thread
 * with the other half missing and reads it as the other person not replying.
 *
 * The shape is already right for the move. Messages are addressed by CharacterId, which
 * is globally unique and not derived from any machine, and conversations are identified
 * by a deterministic function of the two participants rather than by an autoincrementing
 * key that two instances would both hand out.
 */

#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>   // snprintf - MSVC pulls this in transitively, GCC does not
#include <fstream>
#include <filesystem>
#include <chrono>
#include <random>

#include <nlohmann/json.hpp>
#include <cstddef>
#include <utility>

#include "RequestLedger.h"   // idempotent sends - a retry must not create a second message

/**
 * How many messages a single conversation keeps.
 *
 * A cap for the same reason the character row ceiling exists: nothing here is ever
 * deleted by a player, so without one, two people who talk every day grow a file that is
 * only ever loaded in full. Oldest-first trimming, so what survives is the recent
 * conversation rather than its opening lines.
 *
 * Deliberately generous. This is not a chat window that scrolls away in an evening - a
 * thread is a relationship, and people quote things back at each other days later.
 */
inline constexpr size_t kConversationMessageCap = 250;

/**
 * How long a message body may be.
 *
 * Enforced at the STORE rather than only at the command that parses one, because this is
 * what protects the file: a client that sends a megabyte of text should be refused by the
 * thing that would have to write it, not only by the one surface that happens to check.
 */
inline constexpr size_t kMessageBodyLimit = 400;

struct StoredMessage
{
    /**
     * This message's own id.
     *
     * Not an index and not a timestamp. It exists so a message can be referred to - by a
     * read receipt, a delivery acknowledgement, or a duplicate arriving because a client
     * retried - without that reference breaking the moment the conversation is trimmed
     * and every index shifts by one.
     */
    std::string MessageId;

    // Who sent it. The recipient is whoever in the conversation is NOT this, which is
    // why a conversation is exactly two participants and not a group.
    std::string SenderCharacterId;

    std::string Body;

    int64_t SentAt{0};

    /**
     * Has the RECIPIENT been shown this?
     *
     * The sender is never waiting on their own message, so one flag covers it. False
     * means "still owed to somebody", and that is the entire offline story: delivery is
     * driven by scanning for this rather than by remembering who was away.
     *
     * Set when the message is handed to a connected client, not when it is written. The
     * two are different by however long the recipient stays offline, and conflating them
     * is how an inbox silently empties itself while nobody is looking.
     */
    bool Delivered{false};

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(StoredMessage, MessageId, SenderCharacterId,
                                                Body, SentAt, Delivered)
};

struct Conversation
{
    /**
     * The two characters, stored in sorted order.
     *
     * Sorted so that a conversation has ONE identity regardless of who spoke first.
     * Without that, A-texts-B and B-texts-A are two threads that each contain half of
     * what was said, and both people see the other ignoring them. It is the kind of bug
     * that looks like a UI problem for a week.
     */
    std::string A;
    std::string B;

    std::vector<StoredMessage> Messages;

    int64_t LastMessageAt{0};

    bool Involves(const std::string& acCharacterId) const
    {
        return A == acCharacterId || B == acCharacterId;
    }

    // The other participant, given one of them. Empty if the id is not in this thread.
    std::string Other(const std::string& acCharacterId) const
    {
        if (A == acCharacterId)
            return B;
        if (B == acCharacterId)
            return A;
        return {};
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Conversation, A, B, Messages, LastMessageAt)
};

class MessageStore
{
public:
    void Load(const std::filesystem::path& acPath)
    {
        m_path = acPath;

        std::ifstream file(m_path);
        if (!file.is_open())
            return;   // nothing sent yet; an absent file is the normal first-run state

        try
        {
            nlohmann::json json;
            file >> json;

            m_conversations = json.get<std::vector<Conversation>>();

            // Repaired on load rather than trusted.
            //
            // The file is hand-editable by design, and a pair written the wrong way round
            // would produce a thread that neither participant can find - Find() looks for
            // the sorted pair and would miss it, so the messages would be present in the
            // file and invisible in the game. Sorting here means a hand edit cannot
            // create that state.
            for (auto& conversation : m_conversations)
            {
                if (conversation.A > conversation.B)
                {
                    std::swap(conversation.A, conversation.B);
                    m_dirty = true;
                }
            }

            spdlog::info("Loaded {} conversation(s) from {}", m_conversations.size(),
                         m_path.string());
        }
        catch (const std::exception& e)
        {
            // Loud, and NOT destructive. The file is left exactly as it is: a parse error
            // is usually a hand edit gone wrong, and the worst possible response is to
            // overwrite everybody's messages with an empty list on the next flush.
            spdlog::error("Could not read {}: {}. Messages are NOT loaded and will not be "
                          "written until this is fixed.", m_path.string(), e.what());
            m_readable = false;
        }
    }

    /**
     * Send one message. Returns the id it was stored under, or empty with a reason.
     *
     * Everything is checked here rather than at the caller, because there will be more
     * than one caller - a chat command today, a phone app later - and a rule enforced at
     * one surface is a rule the other one gets to break.
     */
    /**
     * `acRequestId` makes sending idempotent - a retry returns the original message id
     * instead of creating a second message.
     *
     * OPTIONAL, and empty means "this caller has no id", which behaves exactly as before.
     * That matters: the only phone path today is the /text command over the reliable chat
     * channel, which has no request id to offer, and treating an empty id as a key would
     * make every idless send collide with every other one - the second text anybody sent
     * would silently return the first one's id and never be written.
     *
     * The id is supplied by the CLIENT, and that is safe because the ledger is keyed on the
     * authenticated sender as well: one player cannot replay another player's id, and the
     * worst a client can do with its own is decline to have its own retries deduplicated.
     */
    std::string Send(const std::string& acFromCharacterId,
                     const std::string& acToCharacterId,
                     const std::string& acBody,
                     std::string* apReason = nullptr,
                     const std::string& acRequestId = {})
    {
        const auto fail = [apReason](const char* acpWhy) -> std::string
        {
            if (apReason)
                *apReason = acpWhy;
            return {};
        };

        // Answered before anything is validated, parsed or written. A retry must be cheap
        // as well as safe - re-running the work and then discarding it would be neither.
        if (const auto* pAlready = m_requests.Find(acFromCharacterId, acRequestId))
            return *pAlready;

        if (!m_readable)
            return fail("store_unreadable");

        if (acFromCharacterId.empty() || acToCharacterId.empty())
            return fail("no_character");

        // Texting yourself is refused rather than stored. A conversation's two
        // participants are sorted and compared, so a self-thread would have A == B and
        // Other() would answer with the sender - every function here would then be
        // subtly wrong for exactly one thread.
        if (acFromCharacterId == acToCharacterId)
            return fail("self");

        if (acBody.empty())
            return fail("empty");

        if (acBody.size() > kMessageBodyLimit)
            return fail("too_long");

        auto& conversation = FindOrCreate(acFromCharacterId, acToCharacterId);

        StoredMessage message;
        message.MessageId = GenerateMessageId();
        message.SenderCharacterId = acFromCharacterId;
        message.Body = acBody;
        message.SentAt = Now();
        message.Delivered = false;

        conversation.Messages.push_back(message);
        conversation.LastMessageAt = message.SentAt;

        // Trimmed here, at the one place messages are added, so the cap cannot be
        // bypassed by a future caller that forgets it exists.
        if (conversation.Messages.size() > kConversationMessageCap)
        {
            const auto excess = conversation.Messages.size() - kConversationMessageCap;

            conversation.Messages.erase(conversation.Messages.begin(),
                                        conversation.Messages.begin() +
                                            static_cast<std::ptrdiff_t>(excess));
        }

        // Marked, not written. The write is debounced - see FlushIfDue. Flushing here meant
        // one text re-serialised every conversation on the server and rewrote the whole
        // file, which is a full disk write bought with a single client packet.
        m_dirty = true;

        if (apReason)
            apReason->clear();

        // Recorded only on SUCCESS. A refused send must stay refused on retry - remembering
        // a failure would turn "you had no eddies" or "that number is blocked" into a
        // permanent verdict that outlives the reason for it.
        m_requests.Record(acFromCharacterId, acRequestId, message.MessageId);

        return message.MessageId;
    }

    // Expiry runs from the server tick, not on lookup - see RequestLedger.
    RequestLedger& Requests() { return m_requests; }

    /**
     * Everything owed to this character, oldest first.
     *
     * Read-only. Marking them delivered is a SEPARATE call, made after the client has
     * actually been told - so a send that fails halfway leaves the messages owed rather
     * than consumed. The alternative, marking them here, loses somebody's messages
     * whenever a push does not land, and does it invisibly.
     */
    struct PendingMessage
    {
        std::string MessageId;
        std::string FromCharacterId;
        std::string Body;
        int64_t SentAt{0};
    };

    std::vector<PendingMessage> Undelivered(const std::string& acCharacterId) const
    {
        std::vector<PendingMessage> pending;

        if (acCharacterId.empty())
            return pending;

        for (const auto& conversation : m_conversations)
        {
            if (!conversation.Involves(acCharacterId))
                continue;

            for (const auto& message : conversation.Messages)
            {
                // Their own messages are never owed to them.
                if (message.Delivered || message.SenderCharacterId == acCharacterId)
                    continue;

                pending.push_back({message.MessageId, message.SenderCharacterId,
                                   message.Body, message.SentAt});
            }
        }

        std::sort(pending.begin(), pending.end(),
                  [](const PendingMessage& acLeft, const PendingMessage& acRight)
                  {
                      // Id as the tiebreak, so the order is TOTAL. Two messages can share
                      // a second, and an unstable order would shuffle them between reads -
                      // which reads as the server rewriting a conversation.
                      if (acLeft.SentAt != acRight.SentAt)
                          return acLeft.SentAt < acRight.SentAt;
                      return acLeft.MessageId < acRight.MessageId;
                  });

        return pending;
    }

    /**
     * Everything this character is owed is now shown. Called AFTER the client was told.
     *
     * Takes the character rather than a list of ids on purpose: the alternative invites a
     * caller to acknowledge a subset, and a partially-acknowledged inbox is a state
     * nothing else here is prepared to reason about.
     */
    void MarkDelivered(const std::string& acCharacterId)
    {
        if (acCharacterId.empty())
            return;

        bool changed = false;

        for (auto& conversation : m_conversations)
        {
            if (!conversation.Involves(acCharacterId))
                continue;

            for (auto& message : conversation.Messages)
            {
                if (message.Delivered || message.SenderCharacterId == acCharacterId)
                    continue;

                message.Delivered = true;
                changed = true;
            }
        }

        if (changed)
        {
            // Debounced like Send. Losing this flag to a crash is the safe direction: an
            // undelivered message is simply delivered again next time, which is why it is
            // set only after every line has reached the connection.
            m_dirty = true;
        }
    }

    /**
     * One thread, oldest last - the order a phone shows it in.
     *
     * Returns a copy rather than a pointer. Callers format this into chat lines while
     * other handlers may be sending, and handing out a pointer into a vector that Send()
     * reallocates is a crash waiting for the server to get busy.
     */
    std::vector<StoredMessage> Thread(const std::string& acCharacterId,
                                      const std::string& acOtherCharacterId,
                                      size_t aLimit = 20) const
    {
        const auto* pConversation = Find(acCharacterId, acOtherCharacterId);

        if (!pConversation)
            return {};

        const auto& messages = pConversation->Messages;

        if (messages.size() <= aLimit)
            return messages;

        return {messages.end() - static_cast<std::ptrdiff_t>(aLimit), messages.end()};
    }

    /**
     * Who this character has threads with, most recent first, with the unread count.
     *
     * This is the inbox list. Unread is counted rather than stored so it cannot drift out
     * of step with the messages it describes - a stored counter and a message list are
     * two facts that must agree, and they eventually will not.
     */
    struct Summary
    {
        std::string OtherCharacterId;
        int64_t LastMessageAt{0};
        size_t Unread{0};
        std::string LastBody;
        bool LastWasMine{false};
    };

    std::vector<Summary> Inbox(const std::string& acCharacterId) const
    {
        std::vector<Summary> inbox;

        if (acCharacterId.empty())
            return inbox;

        for (const auto& conversation : m_conversations)
        {
            if (!conversation.Involves(acCharacterId) || conversation.Messages.empty())
                continue;

            Summary summary;
            summary.OtherCharacterId = conversation.Other(acCharacterId);
            summary.LastMessageAt = conversation.LastMessageAt;

            for (const auto& message : conversation.Messages)
            {
                if (!message.Delivered && message.SenderCharacterId != acCharacterId)
                    ++summary.Unread;
            }

            const auto& last = conversation.Messages.back();
            summary.LastBody = last.Body;
            summary.LastWasMine = last.SenderCharacterId == acCharacterId;

            inbox.push_back(std::move(summary));
        }

        std::sort(inbox.begin(), inbox.end(),
                  [](const Summary& acLeft, const Summary& acRight)
                  {
                      if (acLeft.LastMessageAt != acRight.LastMessageAt)
                          return acLeft.LastMessageAt > acRight.LastMessageAt;
                      return acLeft.OtherCharacterId < acRight.OtherCharacterId;
                  });

        return inbox;
    }

    /**
     * How many messages are waiting. For the one-line notice on arrival.
     */
    size_t UnreadCount(const std::string& acCharacterId) const
    {
        return Undelivered(acCharacterId).size();
    }

    /**
     * Write if enough time has passed. Called from the server tick.
     *
     * WHY THIS EXISTS - disk write amplification.
     *
     * Send() and the delivery marker used to call Flush() directly, which meant ONE text
     * message re-serialised EVERY conversation on the server to pretty-printed JSON and
     * rewrote the whole file. One client packet, one full write of the entire message
     * history - and the cost grows with that history, so it is cheapest on the day you test
     * it and worst on the day it matters.
     *
     * Debounced instead, the same way PlayerStore's positions are: mutations only mark the
     * store dirty, and the write happens on a timer. Under a flood the disk sees one write
     * per interval instead of one per message, and normal texting is unaffected because
     * nobody types faster than the interval anyway.
     *
     * TWO SECONDS, not the thirty PlayerStore uses. A lost position is re-derived from where
     * the player actually is a moment later; a lost message is gone and the sender believes
     * it was sent. Two seconds bounds the damage to something no one would notice while
     * still cutting the flood case by orders of magnitude.
     *
     * Losing the DELIVERED flag on a crash is the safe direction and always was: an
     * undelivered message is simply delivered again next time, which is why the marker is
     * set only after every line has reached the connection.
     */
    void FlushIfDue()
    {
        if (!m_dirty)
            return;

        constexpr int64_t kFlushIntervalSeconds = 2;

        const auto now = Now();

        if (now - m_lastFlushAt < kFlushIntervalSeconds)
            return;

        m_lastFlushAt = now;
        Flush();
    }

    /**
     * Write now, regardless of the timer. For disconnect and shutdown, where there is no
     * later tick to rely on.
     */
    void Flush()
    {
        if (!m_dirty || m_path.empty() || !m_readable)
            return;

        try
        {
            std::filesystem::create_directories(m_path.parent_path());

            std::ofstream file(m_path);
            file << nlohmann::json(m_conversations).dump(2);

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

    /**
     * A message id: 16 hex characters, random.
     *
     * Deliberately NOT the character-id format. That one is short, checksummed and built
     * to be read aloud down a voice channel and typed back, because a person handles it.
     * Nobody types a message id - it is only ever compared - so the tradeoff runs the
     * other way and the only property that matters is that two of them never collide.
     */
    static std::string GenerateMessageId()
    {
        static std::mt19937_64 engine{std::random_device{}()};

        char buffer[17] = {};
        std::snprintf(buffer, sizeof(buffer), "%016llx",
                      static_cast<unsigned long long>(engine()));

        return buffer;
    }

    const Conversation* Find(const std::string& acFirst, const std::string& acSecond) const
    {
        const auto& low = acFirst < acSecond ? acFirst : acSecond;
        const auto& high = acFirst < acSecond ? acSecond : acFirst;

        for (const auto& conversation : m_conversations)
        {
            if (conversation.A == low && conversation.B == high)
                return &conversation;
        }

        return nullptr;
    }

    Conversation& FindOrCreate(const std::string& acFirst, const std::string& acSecond)
    {
        const auto& low = acFirst < acSecond ? acFirst : acSecond;
        const auto& high = acFirst < acSecond ? acSecond : acFirst;

        for (auto& conversation : m_conversations)
        {
            if (conversation.A == low && conversation.B == high)
                return conversation;
        }

        Conversation created;
        created.A = low;
        created.B = high;

        m_conversations.push_back(std::move(created));
        return m_conversations.back();
    }

    std::vector<Conversation> m_conversations;
    std::filesystem::path m_path;
    // Idempotency for sends. Bounded and expiring - see RequestLedger.
    RequestLedger m_requests;

    // When the debounced write last happened - see FlushIfDue.
    int64_t m_lastFlushAt{0};
    bool m_dirty{false};

    // Cleared by a failed load, and never set again for the life of the process.
    //
    // Refusing to write after a failed READ is the whole point: the in-memory list is
    // empty because parsing failed, not because there are no messages, and flushing that
    // would replace every conversation on the server with nothing.
    bool m_readable{true};
};
