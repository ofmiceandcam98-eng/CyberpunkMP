// Voice flood guard: a speaker is never touched, a flood is bounded, and a rejected frame
// costs the relay NOTHING.
//
// Level.cpp drags in the whole server, so the guard is restated here exactly as
// HandleVoiceFrameRequest implements it - including the ORDER, which is the part that
// matters. The size check is first, the rate check second, and the relay walk last; a guard
// that ran after the walk would be the amplifier it exists to prevent.
//
// The relay is modelled as a counter of recipients touched, so the tests can assert the
// thing that actually costs the server: not "was the frame refused" but "how much work did
// refusing it cost".

#include <cstdio>
#include <cstdint>
#include <cstddef>

// Mirrors the constants in Level::HandleVoiceFrameRequest.
constexpr size_t kMaxFrame = 1024;
constexpr int64_t kVoiceWindowMs = 1000;
constexpr uint32_t kVoiceBurst = 100;

// What a real client produces: 20ms Opus frames, so ~50/s at ~200 bytes.
constexpr int kLegitimateFps = 50;
constexpr size_t kRealFrameBytes = 200;

struct Speaker
{
    int64_t WindowStartMs{0};
    uint32_t InWindow{0};
    bool Warned{false};
};

struct Cost
{
    long long relayRecipientsTouched{0};   // the world().each work + sends
    long long logLines{0};                 // disk
    long long framesAccepted{0};
};

// Returns true if the frame was relayed. `nearby` is how many other players are in radius.
static bool Offer(Speaker& s, Cost& cost, size_t frameBytes, int64_t nowMs, int nearby)
{
    // 1. size - cheapest rejection, first, exactly as in the handler
    if (frameBytes == 0 || frameBytes > kMaxFrame)
        return false;

    // 2. rate - before any per-population work
    if (nowMs - s.WindowStartMs >= kVoiceWindowMs)
    {
        s.WindowStartMs = nowMs;
        s.InWindow = 0;
        s.Warned = false;
    }

    ++s.InWindow;

    if (s.InWindow > kVoiceBurst)
    {
        if (!s.Warned)
        {
            s.Warned = true;
            ++cost.logLines;
        }
        return false;   // no movement lookup, no partner search, no relay walk
    }

    // 3. the expensive part, only now
    cost.relayRecipientsTouched += nearby;
    ++cost.framesAccepted;
    return true;
}

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

// Run `fps` frames spread evenly across one second.
static void RunSecond(Speaker& s, Cost& cost, int fps, int nearby, int64_t secondIndex = 0)
{
    for (int i = 0; i < fps; ++i)
    {
        const int64_t t = secondIndex * 1000 + static_cast<int64_t>(i * 1000 / fps);
        Offer(s, cost, kRealFrameBytes, t, nearby);
    }
}

int main()
{
    { // 1. a normal speaker, 50 fps
        Speaker s; Cost c;
        RunSecond(s, c, kLegitimateFps, 10);
        Check(c.framesAccepted == kLegitimateFps, "50 fps: every frame relayed");
        Check(c.logLines == 0, "50 fps: nothing logged");
    }

    { // 2. 75 fps - jitter, or a burst after a stall
        Speaker s; Cost c;
        RunSecond(s, c, 75, 10);
        Check(c.framesAccepted == 75, "75 fps: still entirely untouched");
        Check(c.logLines == 0, "75 fps: nothing logged");
    }

    { // 3. exactly at the ceiling
        Speaker s; Cost c;
        RunSecond(s, c, 100, 10);
        Check(c.framesAccepted == 100, "100 fps: the whole budget is allowed");
        Check(c.logLines == 0, "100 fps: still no warning - the limit is > not >=");
    }

    { // 4. one over
        Speaker s; Cost c;
        RunSecond(s, c, 101, 10);
        Check(c.framesAccepted == 100, "101 fps: exactly the budget gets through");
        Check(c.logLines == 1, "101 fps: warned once");
    }

    { // 5. 200 fps
        Speaker s; Cost c;
        RunSecond(s, c, 200, 10);
        Check(c.framesAccepted == 100, "200 fps: bounded to the budget");
        Check(c.logLines == 1, "200 fps: still only one log line");
    }

    { // 6. 1000 fps - the attack
        Speaker s; Cost c;
        RunSecond(s, c, 1000, 31);
        Check(c.framesAccepted == 100, "1000 fps: bounded to the budget");
        Check(c.relayRecipientsTouched == 100 * 31, "1000 fps: relay work bounded to 3100, not 31000");
        Check(c.logLines == 1, "1000 fps: ONE log line, not a thousand");
    }

    { // 7. sustained flood across a minute
        Speaker s; Cost c;
        for (int64_t sec = 0; sec < 60; ++sec)
            RunSecond(s, c, 1000, 31, sec);

        Check(c.framesAccepted == 100 * 60, "sustained flood: 100/s ceiling holds for a minute");
        Check(c.logLines == 60, "sustained flood: one log line per window, not per frame");
        Check(c.relayRecipientsTouched == 100 * 31 * 60, "sustained flood: relay work stays bounded");
    }

    { // 8. oversized frames are refused before anything else
        Speaker s; Cost c;
        for (int i = 0; i < 500; ++i)
            Offer(s, c, kMaxFrame + 1, 0, 31);

        Check(c.framesAccepted == 0, "oversized frames are never relayed");
        Check(c.relayRecipientsTouched == 0, "and cost the relay nothing");
        Check(s.InWindow == 0, "and do not even consume the rate budget - size is checked first");
    }

    { // 9. flood of oversized frames mixed with valid ones
        Speaker s; Cost c;
        for (int i = 0; i < 1000; ++i)
        {
            Offer(s, c, kMaxFrame + 1, 0, 31);        // junk
            Offer(s, c, kRealFrameBytes, 0, 31);      // valid
        }
        Check(c.framesAccepted == 100, "junk does not let extra valid frames through");
        Check(c.relayRecipientsTouched == 100 * 31, "and the relay is still bounded");
    }

    { // 10. THE POINT: a rejected frame reaches no part of the relay path
        Speaker s; Cost c;
        RunSecond(s, c, 100, 31);                     // exhaust the budget
        const auto before = c.relayRecipientsTouched;

        for (int i = 0; i < 10000; ++i)
            Offer(s, c, kRealFrameBytes, 999, 31);    // same window, all refused

        Check(c.relayRecipientsTouched == before, "10000 refused frames add ZERO relay work");
        Check(c.logLines == 1, "and ZERO extra log lines");
    }

    { // the worst-case bandwidth this permits, stated as a test so it cannot drift
        // 100 frames/s x 31 recipients x ~200 bytes of real Opus = ~620 KB/s from one
        // speaker. The 1KB frame ceiling bounds the pathological case at ~3.1 MB/s.
        const long long realWorst = 100LL * 31 * kRealFrameBytes;
        const long long ceilingWorst = 100LL * 31 * kMaxFrame;

        Check(realWorst == 620000, "worst case at real frame size is ~620 KB/s per speaker");
        Check(ceilingWorst == 3174400, "worst case at the 1KB ceiling is ~3.1 MB/s per speaker");
    }

    { // a speaker whose window rolls is warned again - a new offence, not the same one
        Speaker s; Cost c;
        RunSecond(s, c, 200, 5, 0);
        RunSecond(s, c, 200, 5, 1);
        Check(c.logLines == 2, "a flood in a later window warns again");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
