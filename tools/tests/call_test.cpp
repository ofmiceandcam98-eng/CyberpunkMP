// The call state machine, against the SHIPPED CallStore.h.
//
// spdlog is shimmed because the store only uses it for load/flush diagnostics; everything
// under test is the state machine and the per-character isolation.

#include <cstdio>
#include <string>

namespace spdlog
{
template <class... Args> void info(const char*, Args&&...) {}
template <class... Args> void error(const char*, Args&&...) {}
template <class... Args> void warn(const char*, Args&&...) {}
}

#include "CallStore.h"

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

// Two characters on ONE account, plus a second player. The isolation between A1 and A2 is
// the non-negotiable part.
static const std::string A1 = "AEC-MJ6P";
static const std::string A2 = "H7K-M4X3";
static const std::string B1 = "TWX-9DFN";
static const std::string N_A1 = "555-000-111";
static const std::string N_A2 = "555-000-222";
static const std::string N_B1 = "555-000-333";

int main()
{
    // --- answer, talk, hang up ------------------------------------------------------
    {
        CallStore calls;
        auto& s = calls.Begin(A1, N_A1, B1, N_B1);
        Check(s.State == CallState::Ringing, "a new call rings");
        Check(calls.Active(A1) && calls.Active(B1), "both parties are on it");
        Check(calls.Active(A2) == nullptr, "the caller's OTHER character is not");

        s.State = CallState::Connected;
        s.ConnectedAt = s.CreatedAt;
        Check(calls.ConnectedPartner(A1) == B1, "voice routes A1 -> B1");
        Check(calls.ConnectedPartner(A2).empty(), "voice does NOT route to the other character");

        calls.End(s, CallState::Ended);
        calls.Sweep();
        Check(calls.Active(A1) == nullptr, "the call is gone after ending");
        Check(calls.History(A1).size() == 1 && calls.History(A1)[0].Direction == "out", "A1 has one outgoing");
        Check(calls.History(B1).size() == 1 && calls.History(B1)[0].Direction == "in",  "B1 has one incoming");
        Check(calls.History(A2).empty(), "the player's OTHER character has no history");
    }

    // --- declined --------------------------------------------------------------------
    {
        CallStore calls;
        auto& s = calls.Begin(A1, N_A1, B1, N_B1);
        calls.End(s, CallState::Declined);
        calls.Sweep();
        Check(calls.History(A1)[0].Result == "declined", "a declined call is recorded as declined");
        Check(calls.History(A1)[0].Duration == 0, "and has no duration");
    }

    // --- missed, via the ring timeout -------------------------------------------------
    {
        CallStore calls;
        auto& s = calls.Begin(A1, N_A1, B1, N_B1);
        Check(calls.Expired().empty(), "a call that just started has not expired");
        s.CreatedAt -= (kCallRingSeconds + 1);
        auto exp = calls.Expired();
        Check(exp.size() == 1, "a call that rang too long expires");
        calls.End(*exp[0], CallState::Missed);
        calls.Sweep();
        Check(calls.History(A1)[0].Result == "missed", "an unanswered call is missed");
    }

    // --- busy -------------------------------------------------------------------------
    {
        CallStore calls;
        auto& first = calls.Begin(A1, N_A1, B1, N_B1);
        first.State = CallState::Connected;
        auto& second = calls.Begin(A2, N_A2, B1, N_B1);
        calls.End(second, CallState::Busy);
        calls.Sweep();
        Check(calls.History(A2)[0].Result == "busy", "the second caller gets a busy line");
        Check(calls.ConnectedPartner(B1) == A1, "the live call was not disturbed");
    }

    // --- disconnect / character switch --------------------------------------------------
    {
        CallStore calls;
        auto& s = calls.Begin(A1, N_A1, B1, N_B1);
        s.State = CallState::Connected;
        s.ConnectedAt = s.CreatedAt;
        auto* ended = calls.EndFor(A1, CallState::Ended);
        Check(ended != nullptr && ended->Other(A1) == B1, "EndFor finds the call and names who to tell");
        calls.Sweep();
        Check(calls.Active(B1) == nullptr, "no ghost session is left behind");
        Check(calls.ConnectedPartner(B1).empty(), "and voice stops routing");
    }

    // --- ending twice must not write two pairs of history lines ---------------------------
    {
        CallStore calls;
        auto& s = calls.Begin(A1, N_A1, B1, N_B1);
        calls.End(s, CallState::Ended);
        calls.End(s, CallState::Declined);
        Check(calls.History(A1).size() == 1, "ending twice records one call, not two");
        Check(calls.History(A1)[0].Result == "completed", "and the first ending wins");
    }

    // --- history is capped PER CHARACTER --------------------------------------------------
    {
        CallStore calls;
        for (size_t i = 0; i < kCallHistoryCap + 20; ++i)
        {
            auto& s = calls.Begin(A1, N_A1, B1, N_B1);
            calls.End(s, CallState::Ended);
            calls.Sweep();
        }
        auto& late = calls.Begin(A2, N_A2, B1, N_B1);
        calls.End(late, CallState::Ended);
        calls.Sweep();
        Check(calls.History(A1, 1000).size() == kCallHistoryCap, "A1's history is capped");
        Check(calls.History(A2, 1000).size() == 1, "A2's call was NOT evicted by A1's flood");
    }

    // --- held pointers survive other calls starting (the list, not vector, choice) ---------
    {
        CallStore calls;
        auto* first = &calls.Begin(A1, N_A1, B1, N_B1);
        const auto id = first->CallId;
        for (int i = 0; i < 50; ++i) calls.Begin(A2, N_A2, B1, N_B1);
        Check(first->CallId == id, "a held session pointer survives 50 new calls");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
