#include "HangWatchdog.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#pragma comment(lib, "dbghelp.lib")

namespace
{
std::atomic<uint64_t> s_lastBeat{0};
std::atomic<bool>     s_running{false};
std::atomic<bool>     s_reported{false};
HANDLE                s_tickThread = nullptr;
DWORD                 s_tickThreadId = 0;
std::thread           s_watcher;

uint64_t NowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/**
 * Walks one already-suspended thread and logs its frames.
 *
 * Suspending is the CALLER's job. A thread has to be frozen for its context to mean
 * anything - a running stack changes underneath the walk and yields frames that never
 * existed - but doing it in here would mean suspending the watcher itself when the sweep
 * reaches its own entry in the thread list.
 *
 * Only threads with our code on them are printed, plus the stalled one. Cyberpunk runs
 * dozens of workers and printing all of them buries the two that matter.
 */
void WalkThread(HANDLE aProcess, HANDLE aThread, DWORD aThreadId, const char* acpLabel)
{
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;

    if (!GetThreadContext(aThread, &context))
        return;

    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* pSymbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = MAX_SYM_NAME;

    std::string frames;
    bool ours = false;

    for (int depth = 0; depth < 32; ++depth)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, aProcess, aThread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;

        if (frame.AddrPC.Offset == 0)
            break;

        const DWORD64 moduleBase = SymGetModuleBase64(aProcess, frame.AddrPC.Offset);
        std::string moduleName = "?";

        if (moduleBase)
        {
            char fullPath[MAX_PATH] = {};
            if (GetModuleFileNameA(reinterpret_cast<HMODULE>(moduleBase), fullPath, MAX_PATH))
            {
                const std::string path = fullPath;
                const auto slash = path.find_last_of('\\');
                moduleName = (slash == std::string::npos) ? path : path.substr(slash + 1);
            }
        }

        if (_stricmp(moduleName.c_str(), "CyberpunkMP.dll") == 0)
            ours = true;

        DWORD64 displacement = 0;
        char line[640] = {};

        if (SymFromAddr(aProcess, frame.AddrPC.Offset, &displacement, pSymbol))
        {
            snprintf(line, sizeof(line), "\n[Hang]   #%02d %s!%s+0x%llx", depth, moduleName.c_str(),
                     pSymbol->Name, displacement);
        }
        else
        {
            const DWORD64 offset = moduleBase ? frame.AddrPC.Offset - moduleBase : frame.AddrPC.Offset;
            snprintf(line, sizeof(line), "\n[Hang]   #%02d %s+0x%llx", depth, moduleName.c_str(), offset);
        }

        frames += line;
    }

    if (ours || strcmp(acpLabel, "STALLED") == 0)
        spdlog::error("[Hang] --- {} thread {}{}", acpLabel, aThreadId, frames);
}

/**
 * The stalled thread, then every other thread carrying our code.
 *
 * One stack was not enough: the first capture showed the ticking thread blocked in
 * WaitForSingleObject inside the game, with nothing of ours on it. Something else in the
 * process holds whatever it is waiting for, and that thread is where our code will appear -
 * so the sweep is the half that actually names the culprit.
 */
void DumpStacks()
{
    if (!s_tickThread)
        return;

    const HANDLE process = GetCurrentProcess();

    // Initialised here rather than at startup: loading PDBs for every module is slow, and
    // paying that on every launch to serve a case that almost never happens is wrong.
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);

    spdlog::error("[Hang] tick thread {} has not ticked for 8s - dumping stacks", s_tickThreadId);

    if (SuspendThread(s_tickThread) != static_cast<DWORD>(-1))
    {
        WalkThread(process, s_tickThread, s_tickThreadId, "STALLED");
        ResumeThread(s_tickThread);
    }

    // Everything else in the process. Threads are suspended one at a time and resumed
    // immediately - freezing them all at once risks deadlocking against a thread that holds
    // the loader or heap lock while we are inside dbghelp.
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);

    if (snapshot != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);

        const DWORD self = GetCurrentThreadId();
        const DWORD pid = GetCurrentProcessId();

        if (Thread32First(snapshot, &entry))
        {
            do
            {
                if (entry.th32OwnerProcessID != pid)
                    continue;

                // Never the watcher - suspending ourselves ends the investigation - and
                // never the stalled thread twice.
                if (entry.th32ThreadID == self || entry.th32ThreadID == s_tickThreadId)
                    continue;

                const HANDLE thread =
                    OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                               FALSE, entry.th32ThreadID);

                if (!thread)
                    continue;

                if (SuspendThread(thread) != static_cast<DWORD>(-1))
                {
                    WalkThread(process, thread, entry.th32ThreadID, "other");
                    ResumeThread(thread);
                }

                CloseHandle(thread);
            } while (Thread32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
    }

    SymCleanup(process);
    spdlog::error("[Hang] end of stacks");
}
} // namespace

namespace App
{
void HangWatchdog::Heartbeat()
{
    s_lastBeat.store(NowMs(), std::memory_order_relaxed);
}

void HangWatchdog::Start()
{
    if (s_running.exchange(true))
        return;

    // A real handle, not GetCurrentThread(): that returns a pseudo-handle meaning "whoever
    // asks", so the watcher would suspend ITSELF with it.
    s_tickThreadId = GetCurrentThreadId();
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &s_tickThread,
                    THREAD_ALL_ACCESS, FALSE, 0);

    s_lastBeat.store(NowMs(), std::memory_order_relaxed);

    s_watcher = std::thread(
        []
        {
            while (s_running.load(std::memory_order_relaxed))
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));

                if (s_reported.load(std::memory_order_relaxed))
                    continue;

                const uint64_t last = s_lastBeat.load(std::memory_order_relaxed);

                // Eight seconds: long enough that a load screen or a shader compile is not
                // mistaken for a hang, and far short of the engine's own 120-second
                // watchdog - the stacks have to be captured while the process still exists.
                if (last != 0 && NowMs() - last > 8000)
                {
                    s_reported.store(true, std::memory_order_relaxed);
                    DumpStacks();
                }
            }
        });
}

void HangWatchdog::Stop()
{
    if (!s_running.exchange(false))
        return;

    if (s_watcher.joinable())
        s_watcher.join();

    if (s_tickThread)
    {
        CloseHandle(s_tickThread);
        s_tickThread = nullptr;
    }
}
} // namespace App
