#pragma once

/**
 * Remembers what a request already did, so repeating it does not do it twice.
 *
 * WHAT THIS IS FOR
 *
 * A client that does not hear back retries. That is correct behaviour and it is what a
 * reliable transport does for you automatically - but "did the server not receive it" and
 * "did the server receive it and the reply was lost" look identical from the client, so a
 * retry can arrive for work that already happened. For a movement packet that is harmless.
 * For anything that CREATES something it is a duplicate: two texts, two trades, two
 * characters, two payments.
 *
 * The fix is not to make the client cleverer. It is to make the operation IDEMPOTENT: the
 * client stamps an id on the request, and the server, seeing an id it has already answered,
 * returns the original answer instead of doing the work again.
 *
 * WHY IT IS SEPARATE FROM ANY ONE STORE
 *
 * Every phone, trade and money mutation wants this, and they must not each grow their own
 * half-version. One ledger, asked the same way by all of them, is also the only shape in
 * which "was this request already handled" has a single answer.
 *
 * BOUNDED, ALWAYS - this is the security half
 *
 * A cache keyed on client-supplied strings is a memory-exhaustion surface if it grows
 * without limit: a client sending a million distinct ids would otherwise make the server
 * remember a million of them. Three bounds, all enforced on insert:
 *
 *   - a TTL, because a retry arrives within seconds, not hours;
 *   - a per-owner cap, so one player cannot consume the whole ledger;
 *   - a global cap, so all players together cannot either.
 *
 * Oldest-first eviction. Losing the oldest entry is safe in a way losing the newest is not:
 * a retry of something remembered long ago is far less likely than a retry of something
 * that just happened, and the failure mode of a miss is a duplicate rather than a crash.
 *
 * DELIBERATELY NOT CLEARED ON DISCONNECT. The case this exists for includes "the client
 * dropped before it heard the answer, reconnected, and asked again" - clearing on
 * disconnect would defeat exactly the retry it is meant to catch. Entries expire on time
 * instead, which does not care why the connection went away.
 *
 * KEYED ON THE OWNER, so one player cannot replay another player's request id. The owner is
 * whatever identity the caller has already authenticated - a character id or a Discord id -
 * never anything the client supplied alongside the request.
 */

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <algorithm>

class RequestLedger
{
public:
    /**
     * How long a remembered result stays useful.
     *
     * A retry follows its original by seconds - a transport gives up long before a minute.
     * Five minutes is generous enough to cover a client that dropped, reconnected and asked
     * again, and short enough that the ledger stays small.
     */
    static constexpr int64_t kTtlSeconds = 300;

    // One player cannot fill the ledger on their own.
    static constexpr size_t kPerOwnerLimit = 64;

    // Nor can everyone together. At 64 each this is a hundred players' worth.
    static constexpr size_t kGlobalLimit = 4096;

    /**
     * Has this request already been answered? Returns the original result, or nullptr.
     *
     * An empty request id is "the caller did not supply one", which is not an error - it
     * means that path has no idempotency yet, and it must behave exactly as it did before.
     * Never treated as a key, or every request without an id would collide with every
     * other one and the second text anybody sent would silently vanish.
     */
    const std::string* Find(const std::string& acOwner, const std::string& acRequestId) const
    {
        if (acOwner.empty() || acRequestId.empty())
            return nullptr;

        const auto now = Now();

        for (const auto& entry : m_entries)
        {
            if (entry.Owner == acOwner && entry.RequestId == acRequestId)
            {
                // Expired entries are treated as absent rather than swept here, so a lookup
                // stays a read. Expire() does the removing.
                if (now - entry.At >= kTtlSeconds)
                    return nullptr;

                return &entry.Result;
            }
        }

        return nullptr;
    }

    /**
     * Remember what a request produced.
     *
     * Recording the same id twice replaces the result rather than appending, so the ledger
     * cannot be grown by repeating one id - which is the obvious way to try.
     */
    void Record(const std::string& acOwner, const std::string& acRequestId, const std::string& acResult)
    {
        if (acOwner.empty() || acRequestId.empty())
            return;

        const auto now = Now();

        for (auto& entry : m_entries)
        {
            if (entry.Owner == acOwner && entry.RequestId == acRequestId)
            {
                entry.Result = acResult;
                entry.At = now;
                return;
            }
        }

        m_entries.push_back({acOwner, acRequestId, acResult, now});

        Trim(acOwner, now);
    }

    /**
     * Drop everything past its TTL. Called from the server tick.
     *
     * Separate from the bounds above because they are different guarantees: the caps stop a
     * flood, and this stops a slow trickle from accumulating forever on a quiet server.
     */
    void Expire()
    {
        const auto now = Now();

        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                       [now](const Entry& acEntry)
                                       { return now - acEntry.At >= kTtlSeconds; }),
                        m_entries.end());
    }

    size_t Size() const { return m_entries.size(); }

    // For tests, which cannot wait five minutes to watch an entry expire.
    void SetClockForTesting(int64_t aSeconds) { m_testClock = aSeconds; }

private:
    struct Entry
    {
        std::string Owner;
        std::string RequestId;
        std::string Result;
        int64_t At{0};
    };

    int64_t Now() const
    {
        if (m_testClock >= 0)
            return m_testClock;

        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    /**
     * Enforce the caps, oldest first.
     *
     * The per-owner pass runs before the global one on purpose: without it, one player
     * sending thousands of ids would evict everybody ELSE's entries and their retries would
     * duplicate. Bounding each owner first means a flood costs the flooder their own
     * history and nobody else's.
     */
    void Trim(const std::string& acOwner, int64_t aNow)
    {
        (void)aNow;

        size_t owned = 0;
        for (const auto& entry : m_entries)
        {
            if (entry.Owner == acOwner)
                ++owned;
        }

        while (owned > kPerOwnerLimit)
        {
            const auto oldest =
                std::find_if(m_entries.begin(), m_entries.end(),
                             [&acOwner](const Entry& acEntry) { return acEntry.Owner == acOwner; });

            if (oldest == m_entries.end())
                break;

            m_entries.erase(oldest);
            --owned;
        }

        while (m_entries.size() > kGlobalLimit)
            m_entries.erase(m_entries.begin());
    }

    // Insert order is age order - entries are appended and only ever erased, so the front
    // is always the oldest. That is what makes oldest-first eviction a front erase rather
    // than a sort.
    std::vector<Entry> m_entries;

    int64_t m_testClock{-1};
};
