// Chat flood control: a machine is stopped, a conversation is not.
//
// ChatSystem drags in the whole server, so the sliding window is restated here exactly as
// HandleChatMessageRequest implements it. The invariants asserted are the two that matter:
// a human talking normally is NEVER refused, and a flood is refused and told once.
//
// The second half is the easy one to get wrong. A limiter that replies to every message of
// a flood is the same denial of service with the server volunteering to do the work.

#include <cstdio>
#include <cstdint>

// Mirrors the constants in ChatSystem::HandleChatMessageRequest.
//
// kChatBurst was 10 when this test was written, and the "line every 400ms" case below
// FAILED - 2.5 a second is fast typing, not a bot, and refusing it is exactly the "making
// normal RP communication annoying" both briefs warn against. The limit was raised to 20
// rather than the test being weakened, because a real flood is thousands a second and any
// number in this range stops it identically. Worth remembering if anyone tightens it: the
// test below is what says whether a human would notice.
constexpr int64_t kChatWindowMs = 5000;
constexpr uint32_t kChatBurst = 20;

struct Limiter
{
    int64_t WindowStartMs{0};
    uint32_t InWindow{0};
    bool Warned{false};

    // How many times we would have told the player to slow down.
    int warnings{0};

    // Returns true if the message is allowed through.
    bool Offer(int64_t nowMs)
    {
        if (nowMs - WindowStartMs >= kChatWindowMs)
        {
            WindowStartMs = nowMs;
            InWindow = 0;
            Warned = false;
        }

        ++InWindow;

        if (InWindow > kChatBurst)
        {
            if (!Warned)
            {
                Warned = true;
                ++warnings;
            }
            return false;
        }

        return true;
    }
};

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

int main()
{
    { // a normal burst in a scene is not touched
        Limiter l;
        bool allAllowed = true;
        for (uint32_t i = 0; i < kChatBurst; ++i)
            if (!l.Offer(1000 + static_cast<int64_t>(i) * 200)) allAllowed = false;

        Check(allAllowed, "ten messages inside one window are all allowed");
        Check(l.warnings == 0, "and nobody is told to slow down");
    }

    { // the eleventh is refused
        Limiter l;
        for (uint32_t i = 0; i < kChatBurst; ++i) l.Offer(1000);
        Check(!l.Offer(1000), "the message past the budget is refused");
        Check(l.warnings == 1, "and the player is told once");
    }

    { // THE POINT: the refusal cannot itself be spammed
        Limiter l;
        for (int i = 0; i < 500; ++i) l.Offer(1000);
        Check(l.warnings == 1, "500 messages in one window produce exactly ONE warning");
    }

    { // the window rolls
        Limiter l;
        for (uint32_t i = 0; i < kChatBurst; ++i) l.Offer(1000);
        Check(!l.Offer(1000), "over budget inside the window");
        Check(l.Offer(1000 + kChatWindowMs), "allowed again once the window has passed");
        Check(l.warnings == 1, "and the earlier warning was not repeated by the reset");
    }

    { // a rolled window can warn again - it is a new offence, not the same one
        Limiter l;
        for (int i = 0; i < 50; ++i) l.Offer(1000);
        for (int i = 0; i < 50; ++i) l.Offer(1000 + kChatWindowMs);
        Check(l.warnings == 2, "a second flood in a later window warns again");
    }

    { // sustained human conversation, half an hour of it, never trips
        Limiter l;
        bool everRefused = false;
        // One line per second for 1800 seconds - faster than anyone sustains, and well
        // under two a second.
        for (int64_t s = 0; s < 1800; ++s)
            if (!l.Offer(s * 1000)) everRefused = true;

        Check(!everRefused, "1800 messages at one per second are never refused");
        Check(l.warnings == 0, "and no warning was ever sent");
    }

    { // fast typing: a line every 400ms for a minute. THIS is the case that set the limit.
        Limiter l;
        bool everRefused = false;
        for (int64_t i = 0; i < 150; ++i)
            if (!l.Offer(i * 400)) everRefused = true;

        Check(!everRefused, "a line every 400ms for a minute is still normal conversation");
    }

    { // and the other side of it - a genuine flood IS stopped
        Limiter l;
        int allowed = 0;
        // A thousand messages in the same millisecond, which is what a script does.
        for (int i = 0; i < 1000; ++i)
            if (l.Offer(1000)) ++allowed;

        Check(allowed == static_cast<int>(kChatBurst), "a 1000-message burst gets through exactly the budget");
        Check(l.warnings == 1, "and is told once, not a thousand times");
    }

    { // the boundary is >, not >= - exactly the budget must pass
        Limiter l;
        for (uint32_t i = 0; i < kChatBurst - 1; ++i) l.Offer(1000);
        Check(l.Offer(1000), "the LAST message inside the budget is allowed");
        Check(!l.Offer(1000), "and only the next one is refused");
    }

    { // a client that reconnects gets a fresh limiter, not a poisoned one
        Limiter first;
        for (int i = 0; i < 100; ++i) first.Offer(1000);
        Limiter fresh;   // the component is per-player and dies with them
        Check(fresh.Offer(1000), "a new player starts with a clean window");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
