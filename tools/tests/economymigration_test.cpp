// Phase 5, stage 3: the trust-once migration mechanism.
//
// WHAT IS REAL HERE AND WHAT IS MODELLED - worth being exact, because the distinction is
// the difference between a test and a reassurance.
//
//   REAL: every classification, validation and mutation rule comes from the shipped
//         EconomyMigration.h. Classify, Apply and Inspect are the production functions.
//
//   MODELLED: the file transaction lives in PlayerStore::CommitEconomyMigration, and
//         PlayerStore.h cannot be compiled standalone (glm, spdlog). So the ORDERING
//         property - persist before swapping into live state - is exercised here against a
//         miniature store that mimics that shape, driven by the real functions. The write
//         itself is already covered by atomicwrite_test.
//
// The property that matters most: a FAILED migration is NO migration. Not "mostly migrated".

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "EconomyMigration.h"

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

static constexpr int64_t kNow = 1787000000;

static CharacterRecord Unmigrated(int64_t aMoney, const char* acId = "AAAA-0001")
{
    CharacterRecord r{};
    r.CharacterId = acId;
    r.Money = aMoney;
    r.Inventory.push_back({0x1111, 5});
    return r;
}

static std::vector<const CharacterRecord*> Pointers(const std::vector<CharacterRecord>& acRecords)
{
    std::vector<const CharacterRecord*> out;
    for (const auto& r : acRecords)
        out.push_back(&r);
    return out;
}

int main()
{
    { // A. trust-once: metadata changes, possessions do NOT
        auto r = Unmigrated(20000);
        const auto moneyBefore = r.Money;
        const auto inventoryBefore = r.Inventory;

        Check(EconomyMigration::Apply(r, kNow), "an unmigrated record is migrated");
        Check(r.MigratedAt == kNow, "MigratedAt is stamped with the server's time");
        Check(r.EconomyRevision == 1, "and the opening revision is 1");
        Check(r.Money == moneyBefore, "MONEY IS UNCHANGED - that is the whole of trust-once");
        Check(r.Inventory.size() == inventoryBefore.size() &&
                  r.Inventory[0].Id == inventoryBefore[0].Id &&
                  r.Inventory[0].Quantity == inventoryBefore[0].Quantity,
              "and the inventory is byte-for-byte what it was");
    }

    { // B. idempotency - a second run must not move the line or re-open it
        auto r = Unmigrated(20000);
        EconomyMigration::Apply(r, kNow);

        const bool changedAgain = EconomyMigration::Apply(r, kNow + 99999);

        Check(!changedAgain, "a second migration reports that it changed nothing");
        Check(r.MigratedAt == kNow, "the ORIGINAL timestamp is kept, not replaced with T2");
        Check(r.EconomyRevision == 1, "and the revision is not reset to a second opening balance");
        Check(r.Money == 20000, "money still untouched");
    }

    { // C. an already-migrated record with a later revision is left entirely alone
        auto r = Unmigrated(5000);
        r.MigratedAt = 1786000000;
        r.EconomyRevision = 47;      // the server has since changed its economy 46 more times

        Check(!EconomyMigration::Apply(r, kNow), "an established record is not re-migrated");
        Check(r.EconomyRevision == 47, "its revision is NOT reset to 1");
        Check(r.MigratedAt == 1786000000, "and its original boundary is kept");
    }

    { // D + E. a mixed population: only the unmigrated are candidates
        std::vector<CharacterRecord> population;
        population.push_back(Unmigrated(100, "A"));
        population.push_back(Unmigrated(200, "B"));

        auto done = Unmigrated(300, "C");
        done.MigratedAt = 1786000000;
        done.EconomyRevision = 3;
        population.push_back(done);

        const auto report = EconomyMigration::Inspect(Pointers(population));

        Check(report.Total == 3, "all three are seen");
        Check(report.Candidates == 2, "two are candidates");
        Check(report.AlreadyMigrated == 1, "one is already migrated");
        Check(report.CanCommit(), "and the set is safe to commit");
    }

    { // F. inconsistent metadata is REPORTED, never repaired
        std::vector<CharacterRecord> population;

        auto stampedOnly = Unmigrated(100, "STAMPED-ONLY");
        stampedOnly.MigratedAt = kNow;      // revision still 0
        population.push_back(stampedOnly);

        auto revisedOnly = Unmigrated(100, "REVISED-ONLY");
        revisedOnly.EconomyRevision = 5;    // never stamped
        population.push_back(revisedOnly);

        const auto report = EconomyMigration::Inspect(Pointers(population));

        Check(report.Inconsistent == 2, "both halves-disagree shapes are detected");
        Check(!report.CanCommit(), "and they BLOCK the whole migration");
        Check(report.NeedsAttention.size() == 2, "both are named for the operator");

        // and neither is silently corrected
        auto copy = stampedOnly;
        Check(!EconomyMigration::Apply(copy, kNow), "an inconsistent record is not migrated");
        Check(copy.EconomyRevision == 0, "and is NOT quietly repaired to look consistent");
    }

    { // G. an impossible balance blocks rather than being blessed
        std::vector<CharacterRecord> population;
        population.push_back(Unmigrated(-1, "NEGATIVE"));
        population.push_back(Unmigrated(EconomyMigration::kMaxPlausibleMoney + 1, "ABSURD"));

        const auto report = EconomyMigration::Inspect(Pointers(population));

        Check(report.Blocked == 2, "a negative and an above-ceiling balance are both blocked");
        Check(!report.CanCommit(), "so the migration refuses to run at all");

        auto negative = Unmigrated(-1);
        Check(!EconomyMigration::Apply(negative, kNow), "and neither is migrated individually");
        Check(negative.Money == -1, "nor is the balance quietly corrected");
    }

    { // G2. the ceiling matches the live save guard exactly - not stricter, not looser
        auto atCeiling = Unmigrated(EconomyMigration::kMaxPlausibleMoney);
        Check(EconomyMigration::Classify(atCeiling).Result == EconomyMigration::State::Unmigrated,
              "a balance exactly at the ceiling is allowed, matching the save path");
    }

    { // malformed inventory blocks too
        auto noId = Unmigrated(100);
        noId.Inventory.clear();
        noId.Inventory.push_back({0, 5});

        auto noQuantity = Unmigrated(100);
        noQuantity.Inventory.clear();
        noQuantity.Inventory.push_back({0x1111, 0});

        Check(EconomyMigration::Classify(noId).Result == EconomyMigration::State::Blocked,
              "an item stack with no id blocks");
        Check(EconomyMigration::Classify(noQuantity).Result == EconomyMigration::State::Blocked,
              "and so does one with zero quantity");
    }

    { // M. a legacy record - no metadata at all - is a clean candidate
        const auto legacy = nlohmann::json::parse(R"({
            "CharacterId": "LEGACY-1",
            "Money": 20000,
            "Inventory": [ { "Id": 4369, "Quantity": 7 } ]
        })").get<CharacterRecord>();

        Check(EconomyMigration::Classify(legacy).Result == EconomyMigration::State::Unmigrated,
              "a pre-stage-2 record is a clean migration candidate");
    }

    { // N. inspection changes nothing
        std::vector<CharacterRecord> population;
        population.push_back(Unmigrated(20000, "DRY"));

        const auto before = nlohmann::json(population).dump();
        (void)EconomyMigration::Inspect(Pointers(population));
        const auto after = nlohmann::json(population).dump();

        Check(before == after, "a dry run leaves every byte of state identical");
    }

    // ---------------------------------------------------------------------------------
    // THE TRANSACTION. Modelled - see the header comment - but driven by the real functions.
    //
    // Mirrors PlayerStore::CommitEconomyMigration: inspect, refuse unless clean, COPY,
    // migrate the copy, persist, and only then swap. The ordering is the property.
    // ---------------------------------------------------------------------------------
    struct MiniStore
    {
        std::vector<CharacterRecord> Live;
        std::string Disk;
        bool FailPersist{false};

        bool Commit(int64_t aNow)
        {
            std::vector<const CharacterRecord*> pointers;
            for (const auto& r : Live)
                pointers.push_back(&r);

            if (!EconomyMigration::Inspect(pointers).CanCommit())
                return false;

            std::vector<CharacterRecord> candidate = Live;   // copy; Live untouched

            for (auto& r : candidate)
                EconomyMigration::Apply(r, aNow);

            if (FailPersist)
                return false;                                // Live STILL untouched

            Disk = nlohmann::json(candidate).dump();
            Live = std::move(candidate);                     // only after persistence
            return true;
        }
    };

    { // K. success puts disk and memory in the same migrated state
        MiniStore store;
        store.Live.push_back(Unmigrated(20000, "OK"));
        store.Disk = nlohmann::json(store.Live).dump();

        Check(store.Commit(kNow), "a clean population commits");
        Check(store.Live[0].MigratedAt == kNow && store.Live[0].EconomyRevision == 1,
              "memory shows the migration");
        Check(store.Disk == nlohmann::json(store.Live).dump(),
              "and disk holds exactly the same snapshot");
    }

    { // H/I. FAILED MIGRATION = NO MIGRATION, in memory as well as on disk
        MiniStore store;
        store.Live.push_back(Unmigrated(20000, "FAILS"));
        store.Disk = nlohmann::json(store.Live).dump();
        const auto diskBefore = store.Disk;

        store.FailPersist = true;

        Check(!store.Commit(kNow), "a failed persist reports failure");
        Check(store.Live[0].MigratedAt == 0, "and memory is NOT falsely marked migrated");
        Check(store.Live[0].EconomyRevision == 0, "nor is its revision advanced");
        Check(store.Disk == diskBefore, "and the disk state is unchanged");
    }

    { // J. a retry after a failure works
        MiniStore store;
        store.Live.push_back(Unmigrated(20000, "RETRY"));
        store.Disk = nlohmann::json(store.Live).dump();

        store.FailPersist = true;
        store.Commit(kNow);

        store.FailPersist = false;
        Check(store.Commit(kNow), "the retry succeeds");
        Check(store.Live[0].MigratedAt == kNow, "and migrates properly the second time");
    }

    { // all-or-nothing: one bad record stops every good one
        MiniStore store;
        store.Live.push_back(Unmigrated(100, "GOOD-1"));
        store.Live.push_back(Unmigrated(-5, "BAD"));
        store.Live.push_back(Unmigrated(300, "GOOD-2"));

        Check(!store.Commit(kNow), "one blocked record refuses the whole migration");
        Check(store.Live[0].MigratedAt == 0 && store.Live[2].MigratedAt == 0,
              "and the GOOD records are not migrated either - all or nothing");
    }

    { // O. ordinary operation does not migrate anybody
        //
        // Asserted here as the invariant it is: nothing outside an explicit Commit call
        // changes these fields. A grep for the field names outside CharacterRecord.h,
        // EconomyMigration.h and PlayerStore.h returns nothing.
        auto r = Unmigrated(20000);
        const auto serialised = nlohmann::json(r).dump();
        const auto reloaded = nlohmann::json::parse(serialised).get<CharacterRecord>();

        Check(reloaded.MigratedAt == 0,
              "saving and loading a character does NOT stamp it migrated");
        Check(reloaded.EconomyRevision == 0, "nor advance its revision");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
