#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

// Lock-free map from an engine EntityID hash to the server id we know it by.
//
// WHY THIS EXISTS. GetServerIdByEntity used to answer by running a flecs query:
//
//     query<EntityComponent>().find([aId](const EntityComponent& c) { return c.Id == aId; })
//
// and it is called from the hit hook, which the game dispatches on its job-system
// worker threads. A thread census on 27 August logged FindEntity arriving from FOURTEEN
// distinct threads in a single session, none of them the thread running progress().
//
// Creating a flecs query takes a cursor from the stage's iterator stack. That allocator
// has no locking, deliberately - flecs documents it as per-stage and single-threaded -
// so fourteen worker threads were walking and rewinding the same cursor chain while the
// main thread iterated systems through it. The crash that produced was two threads
// faulting at the same instruction in flecs_stack_restore_cursor, in the same
// millisecond, with the same ecs_stack_t* in RCX and different cursors in RDX.
//
// It also explains everything that looked inexplicable about that crash for a day: the
// small integers where pointers belonged (one thread reading the chain while another
// rewound tail_page->sp past it), the assert that never fired (a race only has to land
// wrong, it does not have to violate is_free), and survival times ranging from two
// seconds to six minutes.
//
// WHY LOCK-FREE, not a mutex. PuppetRegistry next door has the same shape and its
// comment records why: a mutex version fixed an identical off-thread crash and
// introduced a frame-rate problem, because these hooks fire for every entity in the
// scene from several worker threads at once. The same reasoning applies here - the hit
// hook runs for every bullet in Night City, ours or not.
//
// The read path is a bounded scan of relaxed atomic loads with no contention. Writes
// take a mutex but only happen when an entity is registered or unloaded, on the main
// thread.
//
// The cap is how many networked entities can be resolved at once. Beyond it, the extra
// entities simply do not resolve - a hit on one is treated as "not one of ours", which
// is the same answer the query gave for the overwhelming majority of hits anyway.
namespace App::ServerIdRegistry
{
inline constexpr size_t kCapacity = 512;

inline std::array<std::atomic<uint64_t>, kCapacity>& GetEntityIds()
{
    static std::array<std::atomic<uint64_t>, kCapacity> s_entityIds{};
    return s_entityIds;
}

inline std::array<std::atomic<uint64_t>, kCapacity>& GetServerIds()
{
    static std::array<std::atomic<uint64_t>, kCapacity> s_serverIds{};
    return s_serverIds;
}

// Writers only. Readers never take this.
inline std::mutex& GetWriteMutex()
{
    static std::mutex s_mutex;
    return s_mutex;
}

// How far a reader has to scan. Never decreases while anything is registered, so it
// stays a single relaxed load, and an empty table costs one load and no loop.
inline std::atomic<size_t>& GetHighWater()
{
    static std::atomic<size_t> s_highWater{0};
    return s_highWater;
}

inline void Add(uint64_t aEntityIdHash, uint64_t aServerId)
{
    if (aEntityIdHash == 0)
        return; // zero is the empty marker

    std::lock_guard _(GetWriteMutex());

    auto& entityIds = GetEntityIds();
    auto& serverIds = GetServerIds();

    // Already known - update the server id in place rather than adding a second row for
    // the same entity, which would make lookups depend on scan order.
    for (size_t i = 0; i < kCapacity; ++i)
    {
        if (entityIds[i].load(std::memory_order_relaxed) == aEntityIdHash)
        {
            serverIds[i].store(aServerId, std::memory_order_release);
            return;
        }
    }

    for (size_t i = 0; i < kCapacity; ++i)
    {
        if (entityIds[i].load(std::memory_order_relaxed) == 0)
        {
            // Server id FIRST, entity id second with release. A reader that sees the
            // entity id is therefore guaranteed to see the server id that goes with it,
            // rather than the zero that was there a moment earlier.
            serverIds[i].store(aServerId, std::memory_order_relaxed);
            entityIds[i].store(aEntityIdHash, std::memory_order_release);

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

    auto& entityIds = GetEntityIds();

    for (size_t i = 0; i < kCapacity; ++i)
    {
        if (entityIds[i].load(std::memory_order_relaxed) == aEntityIdHash)
        {
            // Entity id cleared first so no reader can match this row while the server
            // id is being torn down.
            entityIds[i].store(0, std::memory_order_release);
            GetServerIds()[i].store(0, std::memory_order_relaxed);
            return;
        }
    }
}

// Safe from any thread. Returns 0 for "not one of ours", which is the common case: the
// hit hook fires for every NPC, prop and piece of scenery in the game.
inline uint64_t Find(uint64_t aEntityIdHash)
{
    if (aEntityIdHash == 0)
        return 0;

    const auto high = GetHighWater().load(std::memory_order_acquire);
    auto& entityIds = GetEntityIds();
    auto& serverIds = GetServerIds();

    for (size_t i = 0; i < high; ++i)
    {
        if (entityIds[i].load(std::memory_order_acquire) == aEntityIdHash)
            return serverIds[i].load(std::memory_order_acquire);
    }

    return 0;
}

inline void Clear()
{
    std::lock_guard _(GetWriteMutex());

    for (size_t i = 0; i < kCapacity; ++i)
    {
        GetEntityIds()[i].store(0, std::memory_order_release);
        GetServerIds()[i].store(0, std::memory_order_relaxed);
    }

    GetHighWater().store(0, std::memory_order_release);
}
} // namespace App::ServerIdRegistry
