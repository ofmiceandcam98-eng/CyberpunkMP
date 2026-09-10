// EconomyRevision semantics - Phase 5 stage 5.
//
// THE RULE THESE PROTECT: revision the TRANSACTION, not the primitive.
//
// A revision number is only useful if it counts committed changes to authoritative state.
// The two ways to make it useless are both tested here:
//
//   - advancing inside Debit/Credit/AddItem/RemoveItem, so one starter kit counts as five
//     changes and the number stops meaning anything;
//   - advancing on a transaction that FAILED, so it counts attempts instead of changes.
//
// The second half of the file covers stale classification, which is observation-only in this
// stage. Stage 7 will refuse on it; it is proven now so that when the wire field finally
// exists the only new thing is the field.

#include <cstdio>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "EconomyMutator.h"

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

// A record as it exists AFTER migration: revision 1, migrated at some real time.
static CharacterRecord Migrated(int64_t aMoney = 20000, uint64_t aRevision = 1)
{
    CharacterRecord r{};
    r.Money = aMoney;
    r.EconomyRevision = aRevision;
    r.MigratedAt = 1'700'000'000;
    r.Inventory.push_back({0x1111, 100});
    return r;
}

// A record as every character on the server is today: never migrated.
static CharacterRecord Legacy(int64_t aMoney = 20000)
{
    CharacterRecord r{};
    r.Money = aMoney;
    r.Inventory.push_back({0x1111, 100});
    return r;
}

int main()
{
    using namespace Economy;

    // ------------------------------------------------------- what counts as migrated ----
    {
        Check(!IsMigrated(Legacy()), "a fresh record is not migrated");
        Check(IsMigrated(Migrated()), "a migrated record is");

        CharacterRecord halfA{};
        halfA.MigratedAt = 1'700'000'000;          // stamped, but revision never set
        Check(!IsMigrated(halfA), "a timestamp without a revision is NOT migrated");

        CharacterRecord halfB{};
        halfB.EconomyRevision = 1;                 // revision, but never stamped
        Check(!IsMigrated(halfB), "a revision without a timestamp is NOT migrated");
    }

    // --------------------------------------------------------------- advancing once ----
    {
        auto r = Migrated();
        Check(AdvanceRevision(r) == Result::Success && r.EconomyRevision == 2,
              "one advance moves the revision by exactly one");

        AdvanceRevision(r);
        AdvanceRevision(r);
        Check(r.EconomyRevision == 4, "and each subsequent one by exactly one more");
    }

    { // THE POINT OF THE WHOLE STAGE: primitives do not touch the revision
        auto r = Migrated();
        const auto before = r.EconomyRevision;

        Debit(r, 100);
        Credit(r, 100);
        AddItem(r, 0x2222, 5);
        RemoveItem(r, 0x2222, 5);
        Transfer(r, r, 50);

        Check(r.EconomyRevision == before,
              "FIVE successful primitive operations advance the revision ZERO times");
    }

    { // a starter kit is one transaction, not five
        auto r = Migrated(0);
        const auto before = r.EconomyRevision;

        AddItem(r, 0x1001, 1);
        AddItem(r, 0x1002, 1);
        AddItem(r, 0x1003, 200);
        Credit(r, 2500);
        AdvanceRevision(r);                        // the transaction boundary, once

        Check(r.EconomyRevision == before + 1,
              "a four-part starter kit advances the revision exactly once");
    }

    { // legacy records are left alone entirely
        auto r = Legacy();

        Check(AdvanceRevision(r) == Result::Success,
              "advancing an unmigrated record SUCCEEDS - its transaction really happened");
        Check(r.EconomyRevision == 0 && r.MigratedAt == 0,
              "but leaves it at (0, 0) - it does not accidentally migrate anything");

        for (int i = 0; i < 50; ++i)
            AdvanceRevision(r);

        Check(r.EconomyRevision == 0, "and fifty more advances still leave it at zero");
    }

    // ------------------------------------------------------------------- exhaustion ----
    {
        auto r = Migrated();
        r.EconomyRevision = std::numeric_limits<uint64_t>::max();

        Check(CanAdvanceRevision(r) == Result::RevisionExhausted,
              "an exhausted record reports it BEFORE anything is mutated");
        Check(AdvanceRevision(r) == Result::RevisionExhausted, "and refuses to advance");
        Check(r.EconomyRevision == std::numeric_limits<uint64_t>::max(),
              "STUCK, NOT WRAPPED - a wrap would make every stale client compare as current");
    }

    {
        auto r = Migrated();
        r.EconomyRevision = std::numeric_limits<uint64_t>::max() - 1;

        Check(CanAdvanceRevision(r) == Result::Success, "one below the ceiling still has room");
        Check(AdvanceRevision(r) == Result::Success, "and takes it");
        Check(CanAdvanceRevision(r) == Result::RevisionExhausted, "which uses the last of it");
    }

    { // an unmigrated record can never be exhausted - it never counts
        auto r = Legacy();
        r.EconomyRevision = std::numeric_limits<uint64_t>::max();   // nonsense, but survivable

        Check(CanAdvanceRevision(r) == Result::Success,
              "an unmigrated record is never refused for exhaustion");
    }

    // -------------------------------------------------- validate before you mutate ----
    //
    // The two-party shape every transaction boundary uses: ask both, then move. Proven here
    // as a pattern so the call sites can be read against something.
    {
        auto payer = Migrated(20000);
        auto payee = Migrated(5000);
        payee.EconomyRevision = std::numeric_limits<uint64_t>::max();

        const bool headroom = CanAdvanceRevision(payer) == Result::Success &&
                              CanAdvanceRevision(payee) == Result::Success;

        Check(!headroom, "a transaction with ONE exhausted participant fails validation");

        if (!headroom)
        {
            Check(payer.Money == 20000 && payee.Money == 5000,
                  "and because it was checked FIRST, no money moved");
        }
    }

    // ------------------------------------------------------ stale classification ----
    {
        const auto r = Migrated(20000, 7);

        Check(ClassifyClientRevision(r, 7) == RevisionView::Match, "an equal revision is current");
        Check(ClassifyClientRevision(r, 6) == RevisionView::Stale, "a lower one is stale");
        Check(ClassifyClientRevision(r, 0) == RevisionView::Stale, "and zero is very stale");
        Check(ClassifyClientRevision(r, 8) == RevisionView::Future,
              "a higher one is impossible - the server has never been there");
        Check(ClassifyClientRevision(r, std::numeric_limits<uint64_t>::max()) == RevisionView::Future,
              "including an absurd one");
    }

    { // every comparison against an unmigrated record is Legacy, whatever the client claims
        const auto r = Legacy();

        Check(ClassifyClientRevision(r, 0) == RevisionView::Legacy, "0 against an unmigrated record");
        Check(ClassifyClientRevision(r, 1) == RevisionView::Legacy, "1 against an unmigrated record");
        Check(ClassifyClientRevision(r, 999) == RevisionView::Legacy, "and 999 - all legacy");
    }

    { // THE TRAP: matching revisions are not permission
        //
        // Asserted as a comment-with-teeth. A client at the server's revision can still send
        // a fabricated balance; Match says only "your base version is not stale". If Stage 7
        // ever treats Match as authority to accept values, this is the note that says why it
        // was wrong.
        const auto r = Migrated(20000, 7);
        Check(ClassifyClientRevision(r, 7) == RevisionView::Match,
              "a matching revision says the VERSION agrees - never that the values are honest");
    }

    { // the descriptions are distinct - they end up in operator-facing logs
        std::vector<std::string> seen;
        for (auto v : {RevisionView::Match, RevisionView::Stale, RevisionView::Future,
                       RevisionView::Legacy})
        {
            seen.push_back(Describe(v));
        }

        bool distinct = true;
        for (size_t i = 0; i < seen.size(); ++i)
            for (size_t j = i + 1; j < seen.size(); ++j)
                if (seen[i] == seen[j]) distinct = false;

        Check(distinct, "every RevisionView describes itself differently");
    }

    // --------------------------------------------------------- serialisation ----
    { // the revision survives a round trip, because a version that resets is not a version
        auto r = Migrated(1234, 42);

        const auto restored = nlohmann::json(r).get<CharacterRecord>();
        Check(restored.EconomyRevision == 42 && restored.MigratedAt == r.MigratedAt,
              "revision and migration stamp survive a JSON round trip");
        Check(IsMigrated(restored), "and the record is still migrated on the other side");
    }

    { // a record written before Stage 2 has no such fields, and must load as legacy
        auto legacy = nlohmann::json::parse(R"({"CharacterId":"OLD","Name":"Old","Money":900})");
        const auto restored = legacy.get<CharacterRecord>();

        Check(restored.EconomyRevision == 0 && restored.MigratedAt == 0,
              "a pre-Stage-2 record loads with no revision");
        Check(!IsMigrated(restored), "so it is legacy, which is what every live character is");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
