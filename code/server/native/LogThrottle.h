#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

/**
 * One log line per key per interval, carrying the count of the ones that were not written.
 *
 * WHY. A packet the server rejects on a path the client drives at movement rate is
 * rejected thirty times a second per entity for as long as the client keeps sending it,
 * and each rejection was a warning. Measured on both servers on 2026-11-11: 89,531 of the
 * live server's 128,008 kept log lines were ONE message from one morning, at up to 124 a
 * second, and 31,832 of the test server's 76,678 were another. Release logging flushes on
 * every warning and the log is rotating 5 MB files, so a flood like that is a disk write
 * per rejected packet on the tick thread, and it pushes the history that would explain it
 * out of the files - two of the live server's three kept files were nothing else.
 *
 * WHAT IT KEEPS. The first occurrence for a key is always written, with everything the
 * caller knows. After that, one line per interval per key, saying how many were swallowed
 * since that key's previous line - so the log still says which ids, since when, and how
 * hard. The key is whatever tells one cause from another: the entity id a client keeps
 * sending, not the connection, because one confused client is usually wrong about several
 * entities at once and each of them is a separate fact.
 *
 * WHAT IT DOES NOT DO. Nothing here runs on a timer, so a burst that stops inside the
 * interval is reported by that key's NEXT line, if there ever is one. One line means "at
 * least one", never "exactly one".
 *
 * BOUNDED. A client inventing ids cannot grow the table without limit: at the cap, keys
 * that have gone quiet are forgotten, and whatever still does not fit shares one slot
 * rather than being logged unthrottled.
 *
 * Not thread-safe: one instance per call site, on the thread that handles the packets.
 * The clock is passed in so tools/tests/logthrottle_test.cpp can prove the arithmetic
 * without sleeping.
 */
class LogThrottle
{
public:
    using Clock = std::chrono::steady_clock;

    explicit LogThrottle(const Clock::duration aInterval = std::chrono::seconds(5), const size_t aMaxKeys = 256)
        : m_interval(aInterval)
        , m_maxKeys(aMaxKeys)
    {
    }

    /**
     * True when this occurrence should be written. aSkipped is then how many occurrences
     * of the same key were swallowed since its previous line; it is untouched otherwise.
     */
    bool ShouldLog(const uint64_t aKey, const Clock::time_point aNow, uint64_t& aSkipped)
    {
        const auto it = m_entries.find(aKey);
        if (it != m_entries.end())
            return Offer(it->second, aNow, aSkipped);

        if (m_entries.size() >= m_maxKeys)
        {
            ForgetQuiet(aNow);

            if (m_entries.size() >= m_maxKeys)
            {
                // Full of keys that are all still arriving. This one shares a slot with
                // every other key that did not fit, so it is counted and never unthrottled.
                if (!m_overflowUsed)
                {
                    m_overflowUsed = true;
                    m_overflow = Entry{aNow, aNow, 0};
                    aSkipped = 0;
                    return true;
                }

                return Offer(m_overflow, aNow, aSkipped);
            }
        }

        m_entries.emplace(aKey, Entry{aNow, aNow, 0});
        aSkipped = 0;
        return true;
    }

    bool ShouldLog(const uint64_t aKey, uint64_t& aSkipped)
    {
        return ShouldLog(aKey, Clock::now(), aSkipped);
    }

    size_t TrackedKeys() const { return m_entries.size(); }

private:
    struct Entry
    {
        Clock::time_point LastLogged;
        Clock::time_point LastSeen;
        uint64_t Skipped;
    };

    bool Offer(Entry& aEntry, const Clock::time_point aNow, uint64_t& aSkipped) const
    {
        aEntry.LastSeen = aNow;

        if (aNow - aEntry.LastLogged < m_interval)
        {
            ++aEntry.Skipped;
            return false;
        }

        aSkipped = aEntry.Skipped;
        aEntry.Skipped = 0;
        aEntry.LastLogged = aNow;
        return true;
    }

    // A key nothing has arrived for in kForgetAfter intervals has stopped happening, and
    // its slot is worth more to a key that is happening now. The count it was still
    // holding goes with it - see WHAT IT DOES NOT DO above.
    //
    // Twelve intervals, not one, because forgetting is also what hands out fresh "first
    // occurrence" lines: a client sending each invented id once could otherwise turn the
    // whole table over every interval. This way the worst it can buy is a table's worth
    // of lines a minute - about four a second at the defaults - whatever rate it sends at.
    void ForgetQuiet(const Clock::time_point aNow)
    {
        const auto quiet = m_interval * kForgetAfter;

        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            if (aNow - it->second.LastSeen >= quiet)
                it = m_entries.erase(it);
            else
                ++it;
        }

        if (m_overflowUsed && aNow - m_overflow.LastSeen >= quiet)
            m_overflowUsed = false;
    }

    static constexpr int kForgetAfter = 12;

    Clock::duration m_interval;
    size_t m_maxKeys;
    std::unordered_map<uint64_t, Entry> m_entries;
    Entry m_overflow{};
    bool m_overflowUsed{false};
};
