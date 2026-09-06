// The pre-cutover integration test Stage 3 recorded as MISSING.
//
// Stage 3 tested the migration POLICY against the real EconomyMigration.h, and the
// TRANSACTION SHAPE against a miniature store, and said so plainly rather than implying the
// production path was covered. This closes that gap: it drives the actual
// PlayerStore::CommitEconomyMigration, through the actual Stage 1 atomic persistence, against
// a real players.json in a temporary directory.
//
// It is the test that has to pass before migration is ever pointed at live data.
//
// Compiling the real PlayerStore needs glm and spdlog, which Verify.ps1 now passes through -
// both are header-only packages already on disk for the normal build, so nothing new is
// depended on.

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Before PlayerStore.h, in this order - it relies on the precompiled header having supplied
// these, the same arrangement character_body_test uses for nlohmann.
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "PlayerStore.h"

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

static std::string Read(const std::filesystem::path& acPath)
{
    std::ifstream file(acPath, std::ios::binary);
    if (!file)
        return {};

    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

static constexpr int64_t kNow = 1787000000;

// A players.json exactly as one exists today: no EconomyRevision, no MigratedAt.
static const char* kLegacyFile = R"([
  {
    "DiscordId": "111",
    "Username": "PlayerOne",
    "Characters": [
      { "CharacterId": "AAAA-0001", "Name": "Jack",  "Money": 20000,
        "Inventory": [ { "Id": 4369, "Quantity": 7 } ] }
    ]
  },
  {
    "DiscordId": "222",
    "Username": "PlayerTwo",
    "Characters": [
      { "CharacterId": "BBBB-0002", "Name": "Vera",  "Money": 500,
        "Inventory": [ { "Id": 8738, "Quantity": 1 } ] }
    ],
    "RetiredCharacters": [
      { "CharacterId": "CCCC-0003", "Name": "Old",   "Money": 99,
        "Inventory": [] }
    ]
  }
])";

int main()
{
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "nco-playerstore-migration";

    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    const auto path = dir / "players.json";

    { // a legacy file loads, and every character is an unmigrated candidate
        { std::ofstream f(path, std::ios::binary); f << kLegacyFile; }

        PlayerStore store;
        store.Load(path);

        const auto report = store.InspectEconomyMigration();

        Check(report.Total == 3, "all three characters are seen, retired included");
        Check(report.Candidates == 3, "and all three are unmigrated candidates");
        Check(report.AlreadyMigrated == 0, "none is already migrated");
        Check(report.CanCommit(), "the population is safe to migrate");

        // N. the dry run changed nothing on disk
        Check(Read(path) == kLegacyFile, "InspectEconomyMigration wrote NOTHING to disk");
    }

    { // SUCCESS: disk and RAM both migrated, possessions identical
        { std::ofstream f(path, std::ios::binary); f << kLegacyFile; }

        PlayerStore store;
        store.Load(path);

        EconomyMigration::Report report;
        std::string error;

        Check(store.CommitEconomyMigration(kNow, &report, &error),
              "the real CommitEconomyMigration succeeds on a clean population");

        // RAM
        const auto* pRecord = store.Find("111");
        Check(pRecord != nullptr, "the account is still there afterwards");

        if (pRecord && !pRecord->Characters.empty())
        {
            const auto& jack = pRecord->Characters[0];

            Check(jack.MigratedAt == kNow, "RAM: the character is stamped");
            Check(jack.EconomyRevision == 1, "RAM: opening revision is 1");
            Check(jack.Money == 20000, "RAM: MONEY IS UNCHANGED");
            Check(jack.Inventory.size() == 1 && jack.Inventory[0].Id == 4369 &&
                      jack.Inventory[0].Quantity == 7,
                  "RAM: inventory is exactly what it was");
        }

        // DISK - reloaded through a second store, so this is what actually persisted
        PlayerStore reloaded;
        reloaded.Load(path);

        const auto* pFromDisk = reloaded.Find("111");
        Check(pFromDisk != nullptr, "the file reloads");

        if (pFromDisk && !pFromDisk->Characters.empty())
        {
            const auto& jack = pFromDisk->Characters[0];

            Check(jack.MigratedAt == kNow, "DISK: the stamp persisted");
            Check(jack.EconomyRevision == 1, "DISK: so did the revision");
            Check(jack.Money == 20000, "DISK: money unchanged");
        }

        // the retired character crossed too
        const auto* pTwo = reloaded.Find("222");
        if (pTwo && !pTwo->RetiredCharacters.empty())
        {
            Check(pTwo->RetiredCharacters[0].MigratedAt == kNow,
                  "a RETIRED character is migrated as well - a restore must not reopen the line");
        }

        // Stage 1's backup exists beside it
        Check(std::filesystem::exists(std::filesystem::path(path).concat(".bak")),
              "the atomic write left a backup, so the pre-migration state is recoverable");
    }

    { // IDEMPOTENCY through the real path
        PlayerStore store;
        store.Load(path);   // already migrated from the block above

        const auto before = Read(path);

        EconomyMigration::Report report;
        Check(store.CommitEconomyMigration(kNow + 99999, &report),
              "running it again on an already-migrated population succeeds as a no-op");
        Check(report.Candidates == 0, "with no candidates");
        Check(report.AlreadyMigrated == 3, "and everybody already migrated");

        PlayerStore reloaded;
        reloaded.Load(path);
        const auto* pRecord = reloaded.Find("111");

        if (pRecord && !pRecord->Characters.empty())
        {
            Check(pRecord->Characters[0].MigratedAt == kNow,
                  "the ORIGINAL timestamp survives - not moved to T2");
        }
    }

    { // BLOCKED: one impossible balance refuses the whole migration, and nothing is written
        const auto blockedPath = dir / "blocked.json";

        const char* withBad = R"([
          { "DiscordId": "1", "Characters": [
              { "CharacterId": "GOOD", "Money": 100, "Inventory": [] },
              { "CharacterId": "BAD",  "Money": -5,  "Inventory": [] } ] }
        ])";

        { std::ofstream f(blockedPath, std::ios::binary); f << withBad; }
        const auto before = Read(blockedPath);

        PlayerStore store;
        store.Load(blockedPath);

        EconomyMigration::Report report;
        std::string error;

        Check(!store.CommitEconomyMigration(kNow, &report, &error),
              "a negative balance refuses the whole migration");
        Check(report.Blocked == 1, "the bad record is counted");
        Check(!error.empty(), "and a reason is returned");

        // DISK unchanged
        Check(Read(blockedPath) == before, "DISK: nothing was written");

        // RAM unchanged - the good character was NOT migrated either
        const auto* pRecord = store.Find("1");
        if (pRecord && !pRecord->Characters.empty())
        {
            Check(pRecord->Characters[0].MigratedAt == 0,
                  "RAM: the GOOD character is not migrated either - all or nothing");
        }
    }

    { // INCONSISTENT metadata blocks and is not repaired
        const auto oddPath = dir / "odd.json";

        const char* odd = R"([
          { "DiscordId": "1", "Characters": [
              { "CharacterId": "HALF", "Money": 100, "EconomyRevision": 5, "MigratedAt": 0,
                "Inventory": [] } ] }
        ])";

        { std::ofstream f(oddPath, std::ios::binary); f << odd; }

        PlayerStore store;
        store.Load(oddPath);

        EconomyMigration::Report report;
        Check(!store.CommitEconomyMigration(kNow, &report),
              "a half-set record blocks the migration");
        Check(report.Inconsistent == 1, "and is reported as inconsistent");

        const auto* pRecord = store.Find("1");
        if (pRecord && !pRecord->Characters.empty())
        {
            Check(pRecord->Characters[0].EconomyRevision == 5,
                  "and is NOT silently repaired");
        }
    }

    { // FAILURE: an unwritable target leaves RAM and disk both unmigrated
        //
        // The path is a DIRECTORY, so the atomic write cannot replace it.
        const auto failPath = dir / "unwritable.json";
        std::filesystem::remove_all(failPath, ec);

        // Load from a good file first, then point the store at the impossible path by
        // loading it - Load sets m_path.
        const auto seed = dir / "seed.json";
        { std::ofstream f(seed, std::ios::binary); f << kLegacyFile; }

        PlayerStore store;
        store.Load(seed);

        // Now make the target impossible to write.
        std::filesystem::create_directories(failPath, ec);
        store.Load(failPath);   // a directory: parse fails, records cleared - not the case we want

        // Instead: seed properly, then replace the file with a directory.
        PlayerStore store2;
        const auto racy = dir / "racy.json";
        { std::ofstream f(racy, std::ios::binary); f << kLegacyFile; }
        store2.Load(racy);

        std::filesystem::remove(racy, ec);
        std::filesystem::create_directories(racy, ec);   // now a directory

        EconomyMigration::Report report;
        std::string error;

        const bool ok = store2.CommitEconomyMigration(kNow, &report, &error);

        Check(!ok, "a write that cannot succeed reports failure");

        const auto* pRecord = store2.Find("111");
        if (pRecord && !pRecord->Characters.empty())
        {
            Check(pRecord->Characters[0].MigratedAt == 0,
                  "RAM IS NOT FALSELY MIGRATED - failed migration is NO migration");
            Check(pRecord->Characters[0].EconomyRevision == 0,
                  "and no revision was advanced");
        }
    }

    { // O. ordinary operation does not migrate anybody
        const auto normalPath = dir / "normal.json";
        { std::ofstream f(normalPath, std::ios::binary); f << kLegacyFile; }

        PlayerStore store;
        store.Load(normalPath);

        // The things a running server does: remember a position, then flush.
        store.Remember("111", "PlayerOne", glm::vec3{1.f, 2.f, 3.f}, 0.5f);
        store.Flush();

        PlayerStore reloaded;
        reloaded.Load(normalPath);

        const auto* pRecord = reloaded.Find("111");
        if (pRecord && !pRecord->Characters.empty())
        {
            Check(pRecord->Characters[0].MigratedAt == 0,
                  "a normal position save does NOT stamp anybody migrated");
            Check(pRecord->Characters[0].EconomyRevision == 0,
                  "nor advance a revision");
        }
        Check(pRecord && pRecord->X == 1.f, "while the position it WAS asked to save persisted");
    }

    std::filesystem::remove_all(dir, ec);

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
