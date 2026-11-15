// Tick pacing - the loop turns as often as it was asked to, and the meter says how often
// it really did.
//
// WHY THIS EXISTS. Server::Update slept for "one tick minus the time since the previous turn
// STARTED". That span already holds the previous turn's sleep, so the turns came in pairs:
// one straight after a full sleep with no rest at all, the next after almost nothing with a
// whole tick of rest. Two turns to a tick - a server asked for 60 a second ran at 116
// (measured with real sleeps, 2026-11-15) - and the world was stepped by a full tick, then by
// almost nothing, over and over. It ran like that for months because nothing counted turns.
//
// The sum now lives in code/common/Network/TickPacer.h with the clock passed in, so this can
// run a minute of turns in no time and count them. The sum it replaced is kept below, as it
// was, so the number it produced stays on record and so the meter is shown catching it.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>

#include "../../code/common/Network/TickPacer.h"

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

using Clock = TickPacer::Clock;
using std::chrono::hours;
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using std::chrono::seconds;

static bool Near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

static Clock::time_point At(nanoseconds aSinceStart)
{
    // An hour in, so "before the start" is a real time and not a negative one.
    return Clock::time_point{} + std::chrono::duration_cast<Clock::duration>(hours(1) + aSinceStart);
}

// What one turn does: given the turn's number, how long its work takes.
using Work = std::function<nanoseconds(uint64_t)>;
// Called at the top of every turn with the pretend time.
using OnTurn = std::function<void(Clock::time_point)>;

struct Run
{
    uint64_t Turns{0};
    double TurnsPerSecond{0.0};
    nanoseconds ShortestGap{nanoseconds::max()};
    nanoseconds LongestGap{nanoseconds::zero()};
};

static void Record(Run& aRun, Clock::time_point aTurnStart, Clock::time_point& aPrevious, bool& aHasPrevious)
{
    if (aHasPrevious)
    {
        const auto gap = std::chrono::duration_cast<nanoseconds>(aTurnStart - aPrevious);
        if (gap < aRun.ShortestGap) aRun.ShortestGap = gap;
        if (gap > aRun.LongestGap) aRun.LongestGap = gap;
    }
    aPrevious = aTurnStart;
    aHasPrevious = true;
    ++aRun.Turns;
}

// The real TickPacer, driven the way Server::Update drives it, through aFor of pretend time.
static Run RunPacer(uint32_t aRate, const Work& aWork, seconds aFor, const OnTurn& aOnTurn = nullptr)
{
    TickPacer pacer;
    pacer.SetRate(aRate);

    Run run;
    auto now = At(nanoseconds::zero());
    const auto end = now + aFor;
    Clock::time_point previous{};
    bool hasPrevious = false;

    while (now < end)
    {
        pacer.Begin(now);
        if (aOnTurn) aOnTurn(now);
        Record(run, now, previous, hasPrevious);

        now += std::chrono::duration_cast<Clock::duration>(aWork(run.Turns));
        now += std::chrono::duration_cast<Clock::duration>(pacer.Rest(now));
    }

    run.TurnsPerSecond = run.Turns / std::chrono::duration<double>(aFor).count();
    return run;
}

// The sum Server::Update used until 2026-11-15, exactly as it was: whole milliseconds, and
// the rest worked out from when the PREVIOUS turn started. Do not copy this anywhere.
static Run RunOldSum(uint32_t aRate, const Work& aWork, seconds aFor, const OnTurn& aOnTurn = nullptr)
{
    const milliseconds timeBetweenUpdates = milliseconds(1000) / aRate;

    Run run;
    auto now = At(nanoseconds::zero());
    const auto end = now + aFor;
    Clock::time_point lastUpdateTime{};
    Clock::time_point previous{};
    bool hasPrevious = false;

    while (now < end)
    {
        const auto currentTick = now;
        if (aOnTurn) aOnTurn(now);
        Record(run, now, previous, hasPrevious);

        now += std::chrono::duration_cast<Clock::duration>(aWork(run.Turns));

        const auto frameTime = currentTick - lastUpdateTime;
        const auto sleepTime = timeBetweenUpdates - frameTime;
        if (sleepTime > nanoseconds::zero())
            now += std::chrono::duration_cast<Clock::duration>(sleepTime);

        lastUpdateTime = currentTick;
    }

    run.TurnsPerSecond = run.Turns / std::chrono::duration<double>(aFor).count();
    return run;
}

static Work Steady(nanoseconds aWork)
{
    return [aWork](uint64_t) { return aWork; };
}

int main()
{
    const seconds minute(60);
    const nanoseconds sixtieth = nanoseconds(seconds(1)) / 60;

    // --- the interval ---------------------------------------------------------------------
    {
        TickPacer pacer;
        Check(pacer.Interval() == milliseconds(100), "a pacer nobody has set rests a tenth of a second, as the loop always did");

        pacer.SetRate(60);
        Check(pacer.Interval() == sixtieth, "60 a second is a sixtieth of a second, not 16 whole milliseconds");
        Check(pacer.Interval() > milliseconds(16) && pacer.Interval() < milliseconds(17), "which is between 16 and 17 milliseconds");

        pacer.SetRate(0);
        Check(pacer.Interval() == seconds(1), "a rate of nothing is one a second, not a division by zero");
    }

    // --- one turn's rest ------------------------------------------------------------------
    {
        TickPacer pacer;
        pacer.SetRate(60);
        const auto start = At(nanoseconds::zero());
        pacer.Begin(start);

        Check(pacer.Rest(start) == sixtieth, "a turn that took no time rests a whole tick");
        Check(pacer.Rest(start + milliseconds(4)) == sixtieth - milliseconds(4), "a turn that took 4ms rests the tick less 4ms");
        Check(pacer.Rest(start + std::chrono::duration_cast<Clock::duration>(sixtieth)) == nanoseconds::zero(), "a turn that took exactly a tick does not rest");
        Check(pacer.Rest(start + seconds(5)) == nanoseconds::zero(), "a turn that stalled for 5s does not rest, and the rest is never negative");
        Check(pacer.Rest(start - milliseconds(1)) == sixtieth, "a clock that reads earlier than the turn's start still rests one tick, never longer");
    }

    // --- the rate over a minute -------------------------------------------------------------
    {
        Check(Near(RunPacer(60, Steady(nanoseconds::zero()), minute).TurnsPerSecond, 60.0, 0.05), "asked for 60 with no work: 60 turns a second");
        Check(Near(RunPacer(60, Steady(microseconds(300)), minute).TurnsPerSecond, 60.0, 0.05), "asked for 60 with a light turn: 60");
        Check(Near(RunPacer(60, Steady(milliseconds(8)), minute).TurnsPerSecond, 60.0, 0.05), "asked for 60 with half a tick of work: 60");
        Check(Near(RunPacer(60, Steady(milliseconds(16)), minute).TurnsPerSecond, 60.0, 0.05), "asked for 60 with nearly a whole tick of work: 60");
        Check(Near(RunPacer(30, Steady(milliseconds(2)), minute).TurnsPerSecond, 30.0, 0.05), "asked for 30: 30");
        Check(Near(RunPacer(10, Steady(milliseconds(2)), minute).TurnsPerSecond, 10.0, 0.05), "asked for 10: 10");
        Check(Near(RunPacer(120, Steady(milliseconds(2)), minute).TurnsPerSecond, 120.0, 0.05), "asked for 120: 120");
        Check(Near(RunPacer(60, Steady(milliseconds(20)), minute).TurnsPerSecond, 50.0, 0.05), "a turn that takes 20ms cannot do 60: it does 50, back to back, and no faster");
    }

    // --- every turn starts one tick after the last ------------------------------------------
    {
        // Work that alternates between light and heavy: the gap between turn starts is what
        // the world is stepped by, and it must not follow the work around.
        const Work uneven = [](uint64_t aTurn) { return (aTurn % 2 == 0) ? nanoseconds(milliseconds(1)) : nanoseconds(milliseconds(10)); };
        const Run run = RunPacer(60, uneven, minute);
        Check(run.ShortestGap == sixtieth && run.LongestGap == sixtieth, "with uneven work every turn still starts exactly one tick after the last");

        // One long stall, then back to light turns. The stall is paid for once.
        const Work stall = [](uint64_t aTurn) { return aTurn == 100 ? nanoseconds(seconds(2)) : nanoseconds(microseconds(300)); };
        const Run after = RunPacer(60, stall, minute);
        Check(after.ShortestGap == sixtieth, "after a 2s stall no turn starts sooner than a tick after the last - no burst to catch up");
        Check(after.LongestGap == seconds(2), "and the stall itself is the only long gap");
    }

    // --- the sum this replaced, on record ---------------------------------------------------
    {
        const Run old = RunOldSum(60, Steady(microseconds(300)), minute);
        const Run now = RunPacer(60, Steady(microseconds(300)), minute);
        std::printf("      asked for 60 with a light turn - the old sum: %.1f a second, the pacer: %.1f\n", old.TurnsPerSecond, now.TurnsPerSecond);

        Check(old.TurnsPerSecond > 110.0, "the old sum turned about twice a tick (this is the bug, kept on record)");
        Check(old.ShortestGap <= microseconds(300) && old.LongestGap >= milliseconds(15), "and its turns came in pairs: one straight after the last, one a whole tick later");
    }

    // --- the meter ----------------------------------------------------------------------------
    {
        Check(!TickMeter::OffPace(60.0, 60), "60 when asked for 60 is on the pace");
        Check(!TickMeter::OffPace(56.0, 60), "56 when asked for 60 is on the pace - a sleep is never exact");
        Check(!TickMeter::OffPace(65.9, 60), "65.9 is on the pace");
        Check(TickMeter::OffPace(66.1, 60), "66.1 is off it");
        Check(TickMeter::OffPace(53.9, 60), "53.9 is off it");
        Check(TickMeter::OffPace(116.4, 60), "116.4 - what the loop really did - is off it");
        Check(TickMeter::OffPace(32.0, 60), "32 - a host whose sleeps overshoot - is off it");

        // Somebody connected the whole time: one answer a minute, and it is the real rate.
        {
            TickMeter meter;
            int answers = 0;
            TickMeter::Reading last;
            RunPacer(60, Steady(microseconds(300)), seconds(185), [&](Clock::time_point aNow) {
                TickMeter::Reading reading;
                if (meter.Count(aNow, true, reading)) { ++answers; last = reading; }
            });
            Check(answers == 3, "three minutes with somebody on is three answers");
            Check(Near(last.TurnsPerSecond, 60.0, 0.05), "and the answer is 60 a second");
            Check(last.Turns >= 3600 && last.Turns <= 3601, "counted from a minute's worth of turns, 3600 of them");
            Check(!TickMeter::OffPace(last.TurnsPerSecond, 60), "which the meter calls on the pace");
        }

        // The meter on the old sum: this is the line the server log never had.
        {
            TickMeter meter;
            int answers = 0;
            TickMeter::Reading last;
            RunOldSum(60, Steady(microseconds(300)), seconds(65), [&](Clock::time_point aNow) {
                TickMeter::Reading reading;
                if (meter.Count(aNow, true, reading)) { ++answers; last = reading; }
            });
            Check(answers == 1 && TickMeter::OffPace(last.TurnsPerSecond, 60), "the meter calls the old sum off its pace within the first minute");
        }

        // Nobody connected: the loop idles on purpose, and the meter says nothing about it.
        {
            TickMeter meter;
            int answers = 0;
            RunPacer(60, Steady(milliseconds(200)), seconds(185), [&](Clock::time_point aNow) {
                TickMeter::Reading reading;
                if (meter.Count(aNow, false, reading)) ++answers;
            });
            Check(answers == 0, "three idle minutes are no answers");
        }

        // Somebody arrives half way through a minute, and leaves half way through a later one.
        {
            TickMeter meter;
            int answers = 0;
            const auto arrives = At(seconds(30));
            const auto leaves = At(seconds(150));
            double answered = 0.0;
            RunPacer(60, Steady(microseconds(300)), seconds(245), [&](Clock::time_point aNow) {
                TickMeter::Reading reading;
                if (meter.Count(aNow, aNow >= arrives && aNow < leaves, reading)) { ++answers; answered = reading.TurnsPerSecond; }
            });
            // Minutes: 0-60 mixed (arrived at 30), 60-120 connected throughout, 120-180 mixed
            // (left at 150), 180-240 idle.
            Check(answers == 1, "only the minute with somebody on from start to finish is answered");
            Check(Near(answered, 60.0, 0.05), "and that answer is not diluted by the minutes around it");
        }
    }

    std::printf("%s - %d failure(s)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
