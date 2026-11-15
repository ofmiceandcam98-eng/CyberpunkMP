#pragma once

#include <chrono>
#include <cstdint>

// The pace of the server's loop: how long it rests at the end of a turn, and how fast it
// turned out to be going.
//
// WHY THIS IS ITS OWN FILE. Server::Update used to work the rest out inline, and from the
// wrong span. It took the time at the top of a turn, subtracted the time the PREVIOUS turn
// started, and slept for what was left of the tick. But that span already holds the previous
// turn's sleep. So the turn after a full sleep saw a whole tick gone and did not sleep at
// all, the turn after that saw almost nothing gone and slept a whole tick, and the loop ran
// in pairs: two turns to a tick, about twice the configured rate, with the world stepped by
// a full tick and then by almost nothing. Asked for 60 a second it ran at 116 (measured with
// real sleeps, 2026-11-15).
//
// Nothing measured the loop, so nothing noticed - it was found by reading. Hence two halves:
//
//   TickPacer  the sum, with the clock passed in, so tools/tests/tickpacer_test.cpp can run a
//              minute of turns in no time and count them.
//   TickMeter  the measurement the server never had. It counts real turns and says how many
//              a second there were, so the next time the pace is wrong the log says so.
struct TickPacer
{
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::nanoseconds;

    // Whole nanoseconds, not milliseconds: 1000ms / 60 is 16ms, which is 62.5 a second.
    // A rate of nothing is treated as one a second rather than a division by zero.
    void SetRate(uint32_t aTurnsPerSecond) noexcept
    {
        m_interval = Duration(std::chrono::seconds(1)) / (aTurnsPerSecond == 0 ? 1u : aTurnsPerSecond);
    }

    [[nodiscard]] Duration Interval() const noexcept { return m_interval; }

    // The top of a turn.
    void Begin(Clock::time_point aNow) noexcept { m_turnStart = aNow; }

    // The work of the turn is done: how long to sleep so the next turn starts one interval
    // after THIS one started. A turn that overran rests for nothing, and the loop does not
    // run short turns afterwards to catch up - a stall is paid for once, not replayed as a
    // burst.
    [[nodiscard]] Duration Rest(Clock::time_point aNow) const noexcept
    {
        const Duration spent = std::chrono::duration_cast<Duration>(aNow - m_turnStart);

        if (spent <= Duration::zero())
            return m_interval;

        return spent >= m_interval ? Duration::zero() : m_interval - spent;
    }

private:
    Duration m_interval{std::chrono::milliseconds(100)};
    Clock::time_point m_turnStart{};
};

// How fast the loop really went, one answer a window.
//
// Only a window in which somebody was connected the whole time gets an answer. With nobody
// on, Server::Update adds a 200ms sleep to every turn on purpose, and a window that mixes
// the two paces says nothing about either.
struct TickMeter
{
    using Clock = std::chrono::steady_clock;

    struct Reading
    {
        double TurnsPerSecond{0.0};
        uint32_t Turns{0};
    };

    explicit TickMeter(std::chrono::seconds aWindow = std::chrono::seconds(60)) noexcept
        : m_window(aWindow)
    {
    }

    // Count one turn. True when a window has just closed with somebody connected throughout;
    // aOut then says how fast it went. The first call only starts the clock: the rate is
    // turns over the time between the first and the last of them, so nothing is counted
    // that the span does not cover.
    bool Count(Clock::time_point aNow, bool aSomebodyConnected, Reading& aOut) noexcept
    {
        if (!m_started)
        {
            m_started = true;
            Restart(aNow, aSomebodyConnected);
            return false;
        }

        ++m_turns;
        m_connectedThroughout = m_connectedThroughout && aSomebodyConnected;

        const auto elapsed = aNow - m_windowStart;
        if (elapsed < m_window)
            return false;

        const bool answered = m_connectedThroughout;
        if (answered)
        {
            aOut.Turns = m_turns;
            aOut.TurnsPerSecond = m_turns / std::chrono::duration<double>(elapsed).count();
        }

        Restart(aNow, aSomebodyConnected);
        return answered;
    }

    // More than a tenth away from what was asked for. A sleep is never exact, so a loop asked
    // for 60 that turns 58 times is on its pace; one that turns 116 times is not.
    [[nodiscard]] static bool OffPace(double aTurnsPerSecond, uint32_t aAskedFor) noexcept
    {
        const double asked = aAskedFor;
        return aTurnsPerSecond > asked * 1.1 || aTurnsPerSecond < asked * 0.9;
    }

private:
    void Restart(Clock::time_point aNow, bool aSomebodyConnected) noexcept
    {
        m_windowStart = aNow;
        m_turns = 0;
        m_connectedThroughout = aSomebodyConnected;
    }

    std::chrono::seconds m_window;
    Clock::time_point m_windowStart{};
    uint32_t m_turns{0};
    bool m_started{false};
    bool m_connectedThroughout{false};
};
