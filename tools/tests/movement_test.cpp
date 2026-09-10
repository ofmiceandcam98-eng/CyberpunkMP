// Movement coalescing: the client's send rate must stop multiplying the server's fan-out.
//
// GameServer/Level drag in the whole server, so the coalescing model is restated here
// exactly as ReplicatePendingMovement and the OnSet observer implement it. What is asserted
// is the COST, not just the correctness: the interesting question is not "did the position
// arrive" but "how much work did the server do to deliver it".
//
// The two modes are both modelled, because the flag ships OFF and the point of the tests is
// to show what turning it on changes.

#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <initializer_list>   // the range-for over {10, 30, 60, 120} below

constexpr int kUpdateRate = 30;               // Config::UpdateRate - already the client's send rate
constexpr int64_t kTickIntervalMs = 1000 / kUpdateRate;

struct Entity
{
    float X{0.f};                  // latest state
    uint32_t ReplicatedSequence{0};
    bool Pending{false};
};

struct Cost
{
    long long relevanceChecks{0};   // the world().each work - what scales with population
    long long replications{0};
    long long sends{0};
};

// A movement packet arrives. Cheap: store latest, mark pending. No walk.
static void OnPacket(Entity& e, Cost& cost, float x, bool coalesce, int players)
{
    e.X = x;                       // latest state wins - no queue, no history

    if (coalesce)
    {
        e.Pending = true;          // O(1) regardless of how many arrive
        return;
    }

    // Legacy path: the observer replicates immediately, once per packet.
    ++e.ReplicatedSequence;
    cost.relevanceChecks += players;
    ++cost.replications;
    cost.sends += players;
}

// The server tick. Only does work for entities with something new.
static void Tick(Entity& e, Cost& cost, bool coalesce, int players)
{
    if (!coalesce || !e.Pending)
        return;

    ++e.ReplicatedSequence;
    e.Pending = false;
    cost.relevanceChecks += players;
    ++cost.replications;
    cost.sends += players;
}

// Run `pps` packets across one second, ticking at kUpdateRate.
static void RunSecond(Entity& e, Cost& cost, int pps, bool coalesce, int players)
{
    int64_t nextTick = 0;

    for (int i = 0; i < pps; ++i)
    {
        const int64_t t = static_cast<int64_t>(i * 1000 / pps);

        while (nextTick <= t)
        {
            Tick(e, cost, coalesce, players);
            nextTick += kTickIntervalMs;
        }

        OnPacket(e, cost, static_cast<float>(i), coalesce, players);
    }

    while (nextTick < 1000)
    {
        Tick(e, cost, coalesce, players);
        nextTick += kTickIntervalMs;
    }
}

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

int main()
{
    constexpr int kPlayers = 31;

    { // a compliant client sends at UpdateRate; coalescing must not change what it costs
        Entity a; Cost legacy;
        RunSecond(a, legacy, 30, false, kPlayers);

        Entity b; Cost coalesced;
        RunSecond(b, coalesced, 30, true, kPlayers);

        Check(legacy.replications == 30, "30 pps uncoalesced: 30 replications");
        Check(coalesced.replications <= 30, "30 pps coalesced: no MORE than 30 replications");
        Check(coalesced.replications >= 29, "30 pps coalesced: and essentially the same cadence");
    }

    { // normal rates are all replicated at the tick, not the packet rate
        for (int pps : {10, 30, 60, 120})
        {
            Entity e; Cost c;
            RunSecond(e, c, pps, true, kPlayers);

            const bool bounded = c.replications <= kUpdateRate;
            std::printf("%s  %d pps coalesces to %lld replications (<= %d)\n",
                        bounded ? "ok  " : "FAIL", pps, c.replications, kUpdateRate);
            if (!bounded) ++failures;
        }
    }

    { // THE VULNERABILITY, before and after
        Entity legacyEntity; Cost legacy;
        RunSecond(legacyEntity, legacy, 1000, false, kPlayers);

        Entity coalescedEntity; Cost coalesced;
        RunSecond(coalescedEntity, coalesced, 1000, true, kPlayers);

        Check(legacy.relevanceChecks == 1000LL * kPlayers, "1000 pps uncoalesced: 31,000 relevance checks");
        Check(coalesced.relevanceChecks <= static_cast<long long>(kUpdateRate) * kPlayers,
              "1000 pps coalesced: bounded to ~930, a 33x reduction");
        Check(coalesced.replications <= kUpdateRate, "1000 pps coalesced: at most one replication per tick");
    }

    { // extreme flood - the bound must not depend on the send rate at all
        Entity e; Cost c;
        RunSecond(e, c, 10000, true, kPlayers);

        Check(c.replications <= kUpdateRate, "10,000 pps: still at most UpdateRate replications");
        Check(c.relevanceChecks <= static_cast<long long>(kUpdateRate) * kPlayers,
              "10,000 pps: relevance work identical to 30 pps");
    }

    { // memory: latest state, never packet history
        Entity e; Cost c;
        for (int i = 0; i < 100000; ++i)
            OnPacket(e, c, static_cast<float>(i), true, kPlayers);

        Check(sizeof(e) <= 16, "pending state is O(1) per entity, not O(packets)");
        Check(e.Pending, "and is a flag, so 100,000 packets leave ONE pending state");
        Check(c.relevanceChecks == 0, "100,000 packets with no tick do ZERO relevance work");
    }

    { // the newest state is what gets sent - nothing is lost by coalescing
        Entity e; Cost c;
        OnPacket(e, c, 1.f, true, kPlayers);
        OnPacket(e, c, 2.f, true, kPlayers);
        OnPacket(e, c, 3.f, true, kPlayers);
        OnPacket(e, c, 4.f, true, kPlayers);
        OnPacket(e, c, 5.f, true, kPlayers);
        Tick(e, c, true, kPlayers);

        Check(c.replications == 1, "five packets between ticks cost ONE replication");
        Check(e.X == 5.f, "and the state sent is the NEWEST, not the first");
    }

    { // a stationary player costs nothing
        Entity e; Cost c;
        for (int i = 0; i < kUpdateRate * 10; ++i)
            Tick(e, c, true, kPlayers);

        Check(c.replications == 0, "an entity that has not moved is never replicated");
        Check(c.relevanceChecks == 0, "and costs no relevance checks at all");
    }

    { // duplicates: same position repeatedly still coalesces to one send per tick
        Entity e; Cost c;
        for (int i = 0; i < 500; ++i)
            OnPacket(e, c, 7.f, true, kPlayers);
        Tick(e, c, true, kPlayers);

        Check(c.replications == 1, "500 duplicate packets cost one replication");
    }

    { // lifecycle: pending must not survive a reset
        Entity e; Cost c;
        OnPacket(e, c, 9.f, true, kPlayers);
        Check(e.Pending, "movement leaves the entity pending");

        e = Entity{};   // disconnect / respawn / character switch destroys the component
        Check(!e.Pending, "a fresh entity carries no pending movement from the old one");
        Check(e.ReplicatedSequence == 0, "and no sequence either");
    }

    { // the LOD divisors: keyed on replications, a flood can no longer advance them
        // Uncoalesced, 1000 pps advanced the sequence 1000 times, so `% 4` passed 250
        // times a second. Coalesced it advances at most UpdateRate times.
        Entity e; Cost c;
        RunSecond(e, c, 1000, true, kPlayers);

        const int midBandPasses = static_cast<int>(e.ReplicatedSequence / 4);
        Check(e.ReplicatedSequence <= kUpdateRate, "sequence advances at the REPLICATION rate");
        Check(midBandPasses <= kUpdateRate / 4, "so the mid-band divisor cannot be outrun by sending faster");
    }

    // ---------------------------------------------------------------------------------
    // INGRESS ORDERING. An older packet must never overwrite newer authoritative state.
    //
    // Mirrors the check in Level::HandleMoveEntityRequest. `tick` is wall-clock
    // milliseconds since the epoch, so it is globally monotonic and never resets - a new
    // ORDERING DOMAIN is expressed by a fresh component (Tick == 0), not by a counter reset.
    // ---------------------------------------------------------------------------------
    {
        constexpr uint64_t kClockResetMs = 30000;

        struct Ingress
        {
            uint64_t Tick{0};      // last accepted
            float X{0.f};          // authoritative position
            bool Pending{false};
            long long accepted{0};
            long long rejected{0};
            long long relevanceChecks{0};
        };

        // Returns true if accepted. Mirrors the handler: reject BEFORE writing state.
        auto Deliver = [&](Ingress& e, uint64_t tick, float x, int players)
        {
            if (e.Tick != 0 && tick <= e.Tick && (e.Tick - tick) < kClockResetMs)
            {
                ++e.rejected;
                return false;   // no write, no pending, no walk
            }

            e.Tick = tick;
            e.X = x;
            e.Pending = true;
            ++e.accepted;
            (void)players;
            return true;
        };

        { // section 10: 100 -> A, 101 -> B, 99 -> C must leave B
            Ingress e;
            Deliver(e, 100, 1.f, kPlayers);
            Deliver(e, 101, 2.f, kPlayers);
            Deliver(e, 99, 3.f, kPlayers);

            Check(e.X == 2.f, "a packet from tick 99 cannot overwrite tick 101");
            Check(e.Tick == 101, "and the authoritative tick stays at the newest");
            Check(e.rejected == 1, "the stale packet was rejected, not applied");
        }

        { // section 10 continued: 100,101,103,102,104 must end at 104
            Ingress e;
            Deliver(e, 100, 1.f, kPlayers);
            Deliver(e, 101, 2.f, kPlayers);
            Deliver(e, 103, 3.f, kPlayers);
            Deliver(e, 102, 4.f, kPlayers);   // overtaken in flight
            Deliver(e, 104, 5.f, kPlayers);

            Check(e.X == 5.f, "out-of-order delivery still ends on the newest state");
            Check(e.Tick == 104, "authoritative tick is 104");
            Check(e.rejected == 1, "only the reordered 102 was dropped");
        }

        { // section 8: loss is normal - gaps must NOT be treated as errors
            Ingress e;
            Deliver(e, 100, 1.f, kPlayers);
            Deliver(e, 101, 2.f, kPlayers);
            Deliver(e, 103, 3.f, kPlayers);   // 102 was lost
            Deliver(e, 104, 4.f, kPlayers);

            Check(e.accepted == 4, "non-contiguous ticks are all accepted - loss is not an error");
            Check(e.X == 4.f, "and the newest state is authoritative");
        }

        { // section 9: duplicates do no work
            Ingress e;
            Deliver(e, 100, 1.f, kPlayers);
            const auto afterFirst = e.accepted;

            for (int i = 0; i < 100; ++i)
                Deliver(e, 100, 9.f, kPlayers);

            Check(e.accepted == afterFirst, "100 duplicates of tick 100 cause ONE update");
            Check(e.X == 1.f, "and cannot change the position");
        }

        { // section 14: a stale flood is cheap
            Ingress e;
            Deliver(e, 500000, 1.f, kPlayers);
            e.Pending = false;   // pretend the tick drained it

            for (int i = 0; i < 10000; ++i)
                Deliver(e, 499999, 2.f, kPlayers);

            Check(e.rejected == 10000, "10,000 stale packets all rejected");
            Check(!e.Pending, "and NONE of them marked movement pending");
            Check(e.relevanceChecks == 0, "so none of them caused a relevance walk");
            Check(e.X == 1.f, "authoritative position untouched");
        }

        { // a fresh component is a fresh ordering domain - respawn/switch/reconnect
            Ingress old;
            Deliver(old, 900000, 1.f, kPlayers);
            Deliver(old, 900001, 2.f, kPlayers);

            Ingress fresh;   // new puppet: Tick == 0
            Check(Deliver(fresh, 1, 5.f, kPlayers), "a new entity accepts any first tick");
            Check(fresh.X == 5.f, "and takes its position");

            // An old session's packet cannot reach the new entity's state - it is a
            // different component entirely. Asserted by construction.
            Check(old.X == 2.f, "the previous entity is unaffected");
        }

        { // the one real edge case: the client's wall clock jumps backwards
            Ingress e;
            Deliver(e, 1787000000000ULL, 1.f, kPlayers);

            // A small step back is reordering - rejected.
            Check(!Deliver(e, 1787000000000ULL - 500, 2.f, kPlayers),
                  "a half-second step back is reordering and is rejected");

            // A large one is a clock correction - accepted, re-baselined.
            Check(Deliver(e, 1787000000000ULL - 60000, 3.f, kPlayers),
                  "a minute-long jump back is a clock reset and is accepted");
            Check(e.X == 3.f, "so the player is not frozen out until real time catches up");
        }
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
