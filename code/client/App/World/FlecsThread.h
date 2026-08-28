#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>

#include <Windows.h>
#include <spdlog/spdlog.h>

// Which thread is allowed to touch the flecs world, and a tripwire for finding out who
// does it from somewhere else.
//
// The flecs world is single-threaded. Not by our convention - by flecs's design: building
// a query takes a cursor from the stage's iterator stack, an allocator that has no locking
// because it is documented as per-stage. Two threads in it at once corrupt the cursor
// chain, which is the crash that dominated 27 August.
//
// This header exists because reading the code was not good enough TWICE. First the crash
// was "eliminated" as a thread race by reasoning about NetworkService. Then, after the
// census proved it was one, I shipped the fix believing VehicleSystem's OnVehicleEnter was
// main-thread "because it is an RTTI method called from redscript" - and Cam got on a
// motorcycle and it fired within the hour:
//
//   22:21:03.849 [VehicleSystem] OnVehicleEnter
//   22:21:03.849 [Thread] FindEntity called OFF the flecs thread: 167948 (flecs is 165360)
//
// Being called from redscript says nothing about which thread arrives, because the engine
// dispatches plenty of script work through its job system. So the rule is: anything that
// touches the world calls AssertFlecsThread with its own name, and the log says who was
// wrong instead of me guessing again.
namespace App::FlecsThread
{
inline std::atomic<uint32_t>& Get()
{
    static std::atomic<uint32_t> s_thread{0};
    return s_thread;
}

// Called from progress(). Whichever thread drives the world owns it.
inline void MarkCurrent()
{
    Get().store(GetCurrentThreadId(), std::memory_order_relaxed);
}

inline bool IsCurrent()
{
    const auto owner = Get().load(std::memory_order_relaxed);
    return owner == 0 || GetCurrentThreadId() == owner;
}

// Logs once per (call site, thread) pair - NOT once per thread. The first version of this
// deduplicated on the thread id alone, which meant a second offending call site sharing a
// thread with the first was silently swallowed. That is exactly the case this needs to
// catch, so the site name is part of the key.
inline void Assert(const char* acpSite)
{
    if (IsCurrent())
        return;

    const auto tid = GetCurrentThreadId();

    static std::mutex s_lock;
    static std::set<std::string> s_seen;

    const auto key = std::string(acpSite) + "@" + std::to_string(tid);

    std::lock_guard guard(s_lock);
    if (s_seen.insert(key).second)
    {
        spdlog::error("[Thread] {} touched flecs OFF the flecs thread: {} (flecs thread is {})", acpSite, tid,
                      Get().load(std::memory_order_relaxed));
    }
}
} // namespace App::FlecsThread
