#pragma once

/**
 * Catches a frozen main thread and writes down where it is stuck.
 *
 * WHY THIS EXISTS
 *
 * The new-character holocall hangs the game. The engine's own watchdog notices after 120
 * seconds and kills the process, which produces "Watchdog timeout!" in the RED4ext log and
 * nothing else - no stack, no function, no clue. Five hypotheses were tested against that
 * silence and all five were wrong, because a hang leaves no evidence in a log written BY
 * the thread that is stuck.
 *
 * Attaching a debugger works but needs somebody at the keyboard inside a two-minute window,
 * on the exact launch that happens to reproduce. This does the same job unattended: the
 * main thread leaves a heartbeat, a watcher thread notices when it stops, and then walks
 * the stalled thread's stack and logs it with symbol names.
 *
 * DELIBERATELY DIAGNOSTIC
 *
 * It suspends a thread, reads its context and resumes it. That is intrusive and it belongs
 * in a build made to find this bug, not in one people play on. It fires ONCE per session
 * and never touches anything again.
 */

#include <cstdint>

namespace App
{
struct HangWatchdog
{
    // Called from the main thread every frame. Cheap by design - a single atomic store,
    // because anything that costs more would change the timing of the thing being measured.
    static void Heartbeat();

    // Starts the watcher. Safe to call twice; the second call does nothing.
    static void Start();

    static void Stop();
};
} // namespace App
