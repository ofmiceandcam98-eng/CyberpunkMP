// Log throttle - a refusal that arrives at movement rate is a line every few seconds, and
// the lines that are written still say which ids and how many.
//
// WHY THIS EXISTS. The move handler warned once per rejected packet. On 2026-11-11 the
// live server's kept logs were 70% one such warning from a single morning (89,531 lines,
// up to 124 a second) and the test server's were 41% another. LogThrottle.h is what stops
// that, and it is easy to get wrong in two directions: throttle too little and the flood
// is back, throttle too much and the log stops saying WHICH entity - which is the only
// reason those warnings exist. Both directions are pinned here, on the real header, with
// the clock passed in so nothing sleeps.

#include <cstdio>
#include <cstdint>

#include "LogThrottle.h"

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

using Clock = LogThrottle::Clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

int main()
{
    const Clock::time_point start{};

    // ---- the first refusal for an id is always written ---------------------------------
    {
        LogThrottle throttle;
        uint64_t skipped = 99;

        Check(throttle.ShouldLog(0xA, start, skipped), "the first refusal for an id is written");
        Check(skipped == 0, "and it reports nothing skipped before it");

        skipped = 99;
        Check(!throttle.ShouldLog(0xA, start + milliseconds(33), skipped), "the next one, 33ms later, is not");
        Check(skipped == 99, "a swallowed refusal leaves the caller's count alone");
    }

    // ---- the measured flood: one id, 124 a second, two hours ---------------------------
    {
        LogThrottle throttle;
        uint64_t written = 0, reported = 0, offered = 0;

        const int64_t total = 124LL * 60 * 60 * 2;
        for (int64_t i = 0; i < total; ++i)
        {
            // 124 a second is one every ~8.06ms; microseconds keep the spacing honest.
            const auto now = start + std::chrono::microseconds(i * 1'000'000 / 124);
            uint64_t skipped = 0;
            ++offered;

            if (throttle.ShouldLog(0xA, now, skipped))
            {
                ++written;
                reported += skipped;
            }
        }

        // Two hours at one line per five seconds is 1,440 lines; the first is the 1,441st.
        Check(written >= 1440 && written <= 1441, "892,800 refusals for one id become one line every five seconds");

        // Every refusal up to the last written line is either a line or counted on one.
        // What follows the last line is at most one interval's worth, still held.
        Check(offered - (written + reported) < 124 * 5 + 1, "and the lines between them account for every refusal swallowed");
        Check(throttle.TrackedKeys() == 1, "one id costs one slot, however long it floods");
    }

    // ---- several ids at once are separate facts ----------------------------------------
    {
        LogThrottle throttle;
        uint64_t skipped = 0;

        // A client wrong about four entities sends them interleaved, in the same order
        // every time. A throttle keyed on nothing would only ever name the first of them.
        bool allFirst = true;
        for (uint64_t id = 1; id <= 4; ++id)
            allFirst = allFirst && throttle.ShouldLog(id, start + milliseconds(id), skipped);
        Check(allFirst, "four ids refused in the same breath each get their own first line");

        int lines[5] = {0, 0, 0, 0, 0};
        for (int tick = 1; tick <= 30 * 60; ++tick)   // a minute at thirty a second
            for (uint64_t id = 1; id <= 4; ++id)
                if (throttle.ShouldLog(id, start + milliseconds(tick * 33 + static_cast<int>(id)), skipped))
                    ++lines[id];

        Check(lines[1] > 0 && lines[1] == lines[2] && lines[2] == lines[3] && lines[3] == lines[4],
              "and over the next minute every one of the four keeps being named");
        Check(lines[1] <= 12, "at no more than a line every five seconds each");
    }

    // ---- an id that returns reports what was swallowed before it went quiet ------------
    {
        LogThrottle throttle;
        uint64_t skipped = 0;

        throttle.ShouldLog(0xA, start, skipped);
        for (int i = 1; i <= 40; ++i)
            throttle.ShouldLog(0xA, start + milliseconds(i * 33), skipped);

        skipped = 0;
        Check(throttle.ShouldLog(0xA, start + seconds(30), skipped), "an id that comes back after a gap is written again");
        Check(skipped == 40, "and its line says how many were swallowed before the gap");
    }

    // ---- invented ids cannot buy an unthrottled log or an unbounded table --------------
    {
        LogThrottle throttle(seconds(5), 8);
        uint64_t skipped = 0;
        int written = 0;

        // 10,000 distinct ids inside one second: every one of them is a "first refusal".
        for (uint64_t id = 1; id <= 10'000; ++id)
            if (throttle.ShouldLog(id, start + std::chrono::microseconds(id * 100), skipped))
                ++written;

        Check(written == 9, "10,000 invented ids in a second write a table's worth of lines plus one shared one");
        Check(throttle.TrackedKeys() == 8, "and the table stops at its cap");

        // Still arriving a second later: the ones that did not fit stay throttled together.
        written = 0;
        for (uint64_t id = 20'000; id < 20'100; ++id)
            if (throttle.ShouldLog(id, start + seconds(2), skipped))
                ++written;
        Check(written == 0, "more invented ids inside the interval are counted, not written");

        // The same client keeps going for a minute, each id sent once. Forgetting is what
        // hands out new first lines, so it must not turn the table over every interval.
        written = 0;
        uint64_t id = 30'000;
        for (int ms = 3'000; ms < 63'000; ms += 10)
            if (throttle.ShouldLog(id++, start + milliseconds(ms), skipped))
                ++written;
        Check(written <= 8 + 12 + 1, "a minute of invented ids buys about one table of lines, not one per interval");
    }

    // ---- a full table makes room once its ids have really stopped ----------------------
    {
        LogThrottle throttle(seconds(5), 2);
        uint64_t skipped = 0;

        throttle.ShouldLog(1, start, skipped);
        throttle.ShouldLog(2, start, skipped);

        // Five minutes later both are long gone, and a new id is a new fact.
        Check(throttle.ShouldLog(3, start + seconds(300), skipped), "a new id is written once the old ones have gone quiet");
        Check(throttle.TrackedKeys() == 1, "and the quiet ones gave their slots up");
    }

    std::printf("\n%s (%d failing)\n", failures ? "FAILED" : "all good", failures);
    return failures ? 1 : 0;
}
