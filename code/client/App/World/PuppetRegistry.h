#pragma once

#include <mutex>
#include <unordered_set>

// Thread-safe registry of the entity IDs we spawned as remote-player puppets.
//
// Why this exists: HookIdleController_SetAnimation runs on the game's ANIMATION
// thread, not the main thread. It used to identify our puppets by reading
// pOwner->tags.Contains("CyberpunkMP.Puppet") - walking a heap-allocated DynArray
// on an entity the main thread may still be assembling. That race crashed the game
// a few hundred milliseconds after a remote player spawned, and because the crash
// was on a different thread it appeared at seemingly random points in the main
// thread's log.
//
// Checking a plain 64-bit EntityID against a mutex-protected set touches no game
// memory beyond that one field.
namespace App::PuppetRegistry
{
inline std::mutex& GetMutex()
{
    static std::mutex s_mutex;
    return s_mutex;
}

inline std::unordered_set<uint64_t>& GetSet()
{
    static std::unordered_set<uint64_t> s_puppets;
    return s_puppets;
}

inline void Add(uint64_t aEntityIdHash)
{
    std::lock_guard _(GetMutex());
    GetSet().insert(aEntityIdHash);
}

inline void Remove(uint64_t aEntityIdHash)
{
    std::lock_guard _(GetMutex());
    GetSet().erase(aEntityIdHash);
}

inline bool Contains(uint64_t aEntityIdHash)
{
    std::lock_guard _(GetMutex());
    return GetSet().find(aEntityIdHash) != GetSet().end();
}

inline void Clear()
{
    std::lock_guard _(GetMutex());
    GetSet().clear();
}
} // namespace App::PuppetRegistry
