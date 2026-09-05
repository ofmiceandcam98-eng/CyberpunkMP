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

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
