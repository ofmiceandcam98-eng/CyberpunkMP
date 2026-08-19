#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

// Lock-free set of the entity IDs we spawned as remote-player puppets.
//
// Why this exists at all: HookIdleController_SetAnimation runs on the game's ANIMATION
// thread, not the main thread. It used to identify our puppets by reading
// pOwner->tags.Contains("CyberpunkMP.Puppet") - walking a heap-allocated DynArray on an
// entity the main thread may still be assembling. That race crashed the game a few
// hundred milliseconds after a remote player spawned, and because the crash was on a
// different thread it appeared at seemingly random points in the main thread's log.
//
// Why it is lock-free: the first fix used a mutex-protected unordered_set, which removed
// the crash and introduced a frame-rate problem. That hook fires for every animated
// entity in the scene, from several animation worker threads at once, and Night City has
// hundreds of them - so every NPC on the street was queueing on one global mutex, and the
// cost rose exactly when the scene got busiest. Driving was the worst case, which is
// where Cam noticed it.
//
// A fixed array of atomics turns the read path into a handful of relaxed loads with no
// contention at all: threads never block each other, and the whole table is a few cache
// lines. Writes still take the mutex, but they only happen when somebody joins or leaves.
//
// The cap is the number of remote puppets that can exist at once. A server that somehow
// exceeded it would leave the extra puppets unregistered - they would animate through the
// game's own controller rather than ours, which looks wrong but breaks nothing.
namespace App::PuppetRegistry
{
inline constexpr size_t kCapacity = 128;

inline std::array<std::atomic<uint64_t>, kCapacity>& GetSlots()
{
    static std::array<std::atomic<uint64_t>, kCapacity> s_slots{};
    return s_slots;
}

// Writers only. Readers never take this.
inline std::mutex& GetWriteMutex()
{
    static std::mutex s_mutex;
    return s_mutex;
}

// How far into the array a reader has to look. Never decreases while anyone is
// registered, which keeps it a single relaxed load rather than something needing its own
// synchronisation - and it means the common case of nobody connected costs one load and
// no loop at all.
inline std::atomic<size_t>& GetHighWater()
{
    static std::atomic<size_t> s_highWater{0};
    return s_highWater;
}

inline void Add(uint64_t aEntityIdHash)
{
    if (aEntityIdHash == 0)
        return; // zero is the empty marker

    std::lock_guard _(GetWriteMutex());

    auto& slots = GetSlots();

    for (auto& slot : slots)
    {
        if (slot.load(std::memory_order_relaxed) == aEntityIdHash)
            return;
    }

    for (size_t i = 0; i < kCapacity; ++i)
    {
        if (slots[i].load(std::memory_order_relaxed) == 0)
        {
            // Release so a reader that sees this id also sees everything the spawn path
            // wrote before registering it.
            slots[i].store(aEntityIdHash, std::memory_order_release);

            auto& highWater = GetHighWater();
            if (highWater.load(std::memory_order_relaxed) < i + 1)
                highWater.store(i + 1, std::memory_order_release);

            return;
        }
    }
}

inline void Remove(uint64_t aEntityIdHash)
{
    if (aEntityIdHash == 0)
        return;

    std::lock_guard _(GetWriteMutex());

    for (auto& slot : GetSlots())
    {
        if (slot.load(std::memory_order_relaxed) == aEntityIdHash)
        {
            slot.store(0, std::memory_order_release);
            return;
        }
    }
}

inline bool Contains(uint64_t aEntityIdHash)
{
    if (aEntityIdHash == 0)
        return false;

    const size_t used = GetHighWater().load(std::memory_order_acquire);
    if (used == 0)
        return false;

    const auto& slots = GetSlots();

    for (size_t i = 0; i < used; ++i)
    {
        if (slots[i].load(std::memory_order_acquire) == aEntityIdHash)
            return true;
    }

    return false;
}

inline void Clear()
{
    std::lock_guard _(GetWriteMutex());

    for (auto& slot : GetSlots())
        slot.store(0, std::memory_order_release);

    GetHighWater().store(0, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// The DRIVER subset: puppets moved and animated by the mod-owned PuppetDriver.
//
// The idle-controller hook must do NOTHING for these - not attach the legacy
// controller (which would outrank the driver and freeze the puppet: the exact live
// failure of 2026-08-19), not forward to the vanilla idle writes. Same lock-free
// shape as the main table because it is read from the same animation threads.
// ---------------------------------------------------------------------------

inline std::array<std::atomic<uint64_t>, kCapacity>& GetDriverSlots()
{
    static std::array<std::atomic<uint64_t>, kCapacity> s_slots{};
    return s_slots;
}

inline std::atomic<size_t>& GetDriverHighWater()
{
    static std::atomic<size_t> s_highWater{0};
    return s_highWater;
}

inline void AddDriver(uint64_t aEntityIdHash)
{
    if (aEntityIdHash == 0)
        return;

    std::lock_guard _(GetWriteMutex());

    auto& slots = GetDriverSlots();

    for (auto& slot : slots)
    {
        if (slot.load(std::memory_order_relaxed) == aEntityIdHash)
            return;
    }

    for (size_t i = 0; i < kCapacity; ++i)
    {
        if (slots[i].load(std::memory_order_relaxed) == 0)
        {
            slots[i].store(aEntityIdHash, std::memory_order_release);

            auto& highWater = GetDriverHighWater();
            if (highWater.load(std::memory_order_relaxed) < i + 1)
                highWater.store(i + 1, std::memory_order_release);

            return;
        }
    }
}

inline void RemoveDriver(uint64_t aEntityIdHash)
{
    if (aEntityIdHash == 0)
        return;

    std::lock_guard _(GetWriteMutex());

    for (auto& slot : GetDriverSlots())
    {
        if (slot.load(std::memory_order_relaxed) == aEntityIdHash)
        {
            slot.store(0, std::memory_order_release);
            return;
        }
    }
}

inline bool IsDriver(uint64_t aEntityIdHash)
{
    if (aEntityIdHash == 0)
        return false;

    const size_t used = GetDriverHighWater().load(std::memory_order_acquire);
    if (used == 0)
        return false;

    const auto& slots = GetDriverSlots();

    for (size_t i = 0; i < used; ++i)
    {
        if (slots[i].load(std::memory_order_acquire) == aEntityIdHash)
            return true;
    }

    return false;
}

inline void ClearDrivers()
{
    std::lock_guard _(GetWriteMutex());

    for (auto& slot : GetDriverSlots())
        slot.store(0, std::memory_order_release);

    GetDriverHighWater().store(0, std::memory_order_release);
}
} // namespace App::PuppetRegistry
