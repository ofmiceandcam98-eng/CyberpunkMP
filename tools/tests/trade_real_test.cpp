// Trade, through the REAL PlayerStore::ApplyTrade.
//
// WHY THIS EXISTS ALONGSIDE trade_test.
//
// trade_test restates the conservation algorithm because PlayerStore could not be compiled
// in the harness. That was an honest compromise and it is now an outdated one: Verify passes
// glm and spdlog through, so the production path can be driven directly - and Stage 4B
// changed that path, which means a mirror is exactly the wrong thing to be trusting.
//
// The divergence is concrete. MoveAssets now routes money through Economy::Transfer, which
// enforces kMaxPlausibleMoney - a ceiling the old hand-written arithmetic did not have. A
// mirror updated by hand would have kept passing either way; only the real path can say
// whether trade actually behaves the way the rest of the server expects.
//
// trade_test keeps its place: it proves the conservation INVARIANT over many permutations
// cheaply. This proves the SHIPPED implementation.

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

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

static constexpr uint64_t kPistol = 0x1111;
static constexpr uint64_t kAmmo = 0x2222;

// Two accounts, one character each, seeded through a real players.json.
static void Seed(const std::filesystem::path& acPath, int64_t aMoneyA, int64_t aMoneyB)
{
    nlohmann::json doc = nlohmann::json::array();

    auto make = [](const char* acDiscord, const char* acCharacterId, int64_t aMoney,
                   bool aWithItems)
    {
        nlohmann::json character;
        character["CharacterId"] = acCharacterId;
        character["Name"] = acCharacterId;
        character["Money"] = aMoney;
        character["Inventory"] = nlohmann::json::array();

        if (aWithItems)
        {
            character["Inventory"].push_back({{"Id", kPistol}, {"Quantity", 1}});
            character["Inventory"].push_back({{"Id", kAmmo}, {"Quantity", 500}});
        }

        nlohmann::json record;
        record["DiscordId"] = acDiscord;
        record["Username"] = acDiscord;
        record["Characters"] = nlohmann::json::array({character});
        return record;
    };

    doc.push_back(make("111", "CHAR-A", aMoneyA, true));
    doc.push_back(make("222", "CHAR-B", aMoneyB, false));

    std::ofstream f(acPath, std::ios::binary);
    f << doc.dump(2);
}

static const CharacterRecord* CharacterOf(const PlayerStore& acStore, const char* acDiscord)
{
    const auto* pRecord = acStore.Find(acDiscord);
    if (!pRecord || pRecord->Characters.empty())
        return nullptr;

    return &pRecord->Characters[0];
}

int main()
{
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "nco-trade-real";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    const auto path = dir / "players.json";

    { // 1 + 3. a mixed money-and-items trade, through the real path
        Seed(path, 20000, 5000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 5000;
        left.Items.push_back({kPistol, 1});
        left.Items.push_back({kAmmo, 100});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";
        right.Money = 2000;

        std::string why;
        Check(store.ApplyTrade(left, right, &why), "a mixed money+item trade succeeds");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");

        Check(pA && pB, "both characters still exist");

        if (pA && pB)
        {
            Check(pA->Money == 17000 && pB->Money == 8000, "both balances moved by the offers");
            Check(pA->Money + pB->Money == 25000, "TOTAL MONEY CONSERVED through the real path");
            Check(Economy::Held(*pB, kPistol) == 1, "the pistol arrived");
            Check(Economy::Held(*pA, kPistol) == 0, "and left - it was moved, not copied");
            Check(Economy::Held(*pA, kAmmo) == 400 && Economy::Held(*pB, kAmmo) == 100,
                  "exactly 100 ammo moved");
        }
    }

    { // 2. an item-only trade
        Seed(path, 100, 100);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Items.push_back({kAmmo, 50});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(store.ApplyTrade(left, right, &why), "an item-only trade succeeds - zero money is not an error");

        const auto* pB = CharacterOf(store, "222");
        Check(pB && Economy::Held(*pB, kAmmo) == 50, "and the items arrived");
    }

    { // 4. insufficient money changes NEITHER side
        Seed(path, 100, 5000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 99999;

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";
        right.Items.clear();

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "an unaffordable trade is refused");
        Check(why == "insufficient_funds", "with the expected reason");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");
        Check(pA && pA->Money == 100, "the payer is untouched");
        Check(pB && pB->Money == 5000, "and so is the recipient");
    }

    { // 5. insufficient items changes NEITHER side, including the money on the other leg
        Seed(path, 20000, 5000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 1000;
        left.Items.push_back({kAmmo, 99999});      // more than the 500 held

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";
        right.Money = 500;

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "an impossible item quantity refuses the trade");
        Check(why == "insufficient_items", "with the expected reason");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");

        Check(pA && pA->Money == 20000 && pB && pB->Money == 5000,
              "AND NO MONEY MOVED - a failure on one leg commits nothing");
        Check(pA && Economy::Held(*pA, kAmmo) == 500, "the ammo is all still there");
    }

    { // 6. THE STAGE 4B CHANGE: a trade that would exceed the plausible ceiling is refused
        //
        // The old hand-written arithmetic only guarded an int64 wrap, so this would have
        // succeeded and produced a balance the SAVE path then refuses - money that appears
        // and silently reverts.
        Seed(path, 20000, EconomyMigration::kMaxPlausibleMoney);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 1;

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(!store.ApplyTrade(left, right, &why),
              "a trade pushing somebody past the ceiling is refused");
        Check(why == "money_overflow", "as an overflow");

        const auto* pA = CharacterOf(store, "111");
        Check(pA && pA->Money == 20000, "and the payer keeps their money");
    }

    { // 7. item quantity overflow on the receiving side
        Seed(path, 100, 100);
        PlayerStore store;
        store.Load(path);

        // Give B a nearly-full stack by trading it there first is impractical; instead
        // trade INTO a stack that is already near the ceiling by seeding it directly.
        {
            auto* pRecord = const_cast<PlayerRecord*>(store.Find("222"));
            if (pRecord && !pRecord->Characters.empty())
            {
                pRecord->Characters[0].Inventory.push_back(
                    {kAmmo, std::numeric_limits<uint32_t>::max() - 5});
            }
        }

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Items.push_back({kAmmo, 100});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "a stack that would wrap uint32 refuses the trade");
        Check(why == "quantity_overflow", "with the expected reason");

        const auto* pA = CharacterOf(store, "111");
        Check(pA && Economy::Held(*pA, kAmmo) == 500, "and the sender keeps their ammo");
    }

    { // trading with yourself is refused
        Seed(path, 100, 100);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 50;

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-A";

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "a character cannot trade with itself");
        Check(why == "same_character", "with the expected reason");
    }

    { // 12. the committed state is exactly what was expected, verified from DISK
        Seed(path, 1000, 1000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 250;
        left.Items.push_back({kPistol, 1});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(store.ApplyTrade(left, right, &why), "the trade commits");

        store.Flush();

        PlayerStore reloaded;
        reloaded.Load(path);

        const auto* pA = CharacterOf(reloaded, "111");
        const auto* pB = CharacterOf(reloaded, "222");

        Check(pA && pA->Money == 750, "DISK: the payer's balance persisted");
        Check(pB && pB->Money == 1250, "DISK: the recipient's did too");
        Check(pB && Economy::Held(*pB, kPistol) == 1, "DISK: and so did the item");
    }

    // ================================================================== STAGE 5 ====
    //
    // Revision behaviour through the REAL ApplyTrade. The unit tests prove the rule in
    // isolation; these prove the shipped transaction boundary actually follows it.

    { // THE MANDATORY REGRESSION: normal runtime does not migrate anybody.
        //
        // This is the one that has to keep passing for the whole of Phase 5. Migration is a
        // deliberate, inspected, one-time act - if ordinary play can produce a migrated
        // record then the migration gate means nothing, and Stage 7 would start refusing
        // clients for characters nobody ever chose to cut over.
        Seed(path, 20000, 5000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 5000;
        left.Items.push_back({kPistol, 1});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";
        right.Money = 1000;

        std::string why;
        Check(store.ApplyTrade(left, right, &why), "a trade between legacy characters succeeds");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");

        Check(pA && pA->MoneyRevision == 0 && pA->MoneyMigratedAt == 0,
              "AND THE PAYER IS STILL UNMIGRATED - runtime never migrates");
        Check(pB && pB->MoneyRevision == 0 && pB->MoneyMigratedAt == 0,
              "and so is the recipient");
        Check(pA && !Economy::IsMigrated(*pA) && pB && !Economy::IsMigrated(*pB),
              "neither counts as migrated by the same test the server uses");

        // And it survives the disk round trip - a field that only looks right in memory is
        // exactly the failure this would otherwise hide.
        store.Flush();
        PlayerStore reloaded;
        reloaded.Load(path);

        const auto* pReloaded = CharacterOf(reloaded, "111");
        Check(pReloaded && pReloaded->MoneyRevision == 0,
              "DISK: still unmigrated after the trade was persisted");
    }

    { // a migrated pair advances exactly ONCE each for one trade
        Seed(path, 20000, 5000);
        PlayerStore store;
        store.Load(path);

        // Migrate both by hand - the migration path itself is covered by
        // playerstore_migration_test; what is under test here is the trade boundary.
        for (const char* discord : {"111", "222"})
        {
            auto* pRecord = const_cast<PlayerRecord*>(store.Find(discord));
            if (pRecord && !pRecord->Characters.empty())
            {
                pRecord->Characters[0].MoneyRevision = 1;
                pRecord->Characters[0].MoneyMigratedAt = 1'700'000'000;
            }
        }

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 500;
        left.Items.push_back({kPistol, 1});
        left.Items.push_back({kAmmo, 250});      // several moves, still ONE transaction

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";
        right.Money = 100;

        std::string why;
        Check(store.ApplyTrade(left, right, &why), "a trade between migrated characters succeeds");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");

        Check(pA && pA->MoneyRevision == 2,
              "the payer advanced exactly once, despite money AND two item moves");
        Check(pB && pB->MoneyRevision == 2, "and so did the recipient");
    }

    { // ============ STAGE 6: AN ITEM-ONLY TRADE MUST NOT ADVANCE MoneyRevision ============
        //
        // The field is MoneyRevision now, not EconomyRevision. Under the old rule this
        // advanced, which would tell a client its balance had changed when nothing had
        // touched it - a lie in the one direction that matters.
        Seed(path, 20000, 5000);
        PlayerStore store;
        store.Load(path);

        for (const char* discord : {"111", "222"})
        {
            auto* pRecord = const_cast<PlayerRecord*>(store.Find(discord));
            if (pRecord && !pRecord->Characters.empty())
            {
                pRecord->Characters[0].MoneyRevision = 5;
                pRecord->Characters[0].MoneyMigratedAt = 1'700'000'000;
            }
        }

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Items.push_back({kPistol, 1});
        left.Items.push_back({kAmmo, 100});     // items only, no money either way

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(store.ApplyTrade(left, right, &why), "an item-only trade between migrated characters succeeds");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");

        Check(pA && pA->MoneyRevision == 5,
              "AND THE PAYER'S MoneyRevision DID NOT MOVE - no money changed");
        Check(pB && pB->MoneyRevision == 5, "nor the recipient's");
        Check(pB && Economy::Held(*pB, kPistol) == 1, "but the items still moved");
        Check(pA && pA->Money == 20000 && pB && pB->Money == 5000, "and no balance changed");
    }

    { // one-sided money still advances BOTH - a transfer moves two balances
        Seed(path, 20000, 5000);
        PlayerStore store;
        store.Load(path);

        for (const char* discord : {"111", "222"})
        {
            auto* pRecord = const_cast<PlayerRecord*>(store.Find(discord));
            if (pRecord && !pRecord->Characters.empty())
            {
                pRecord->Characters[0].MoneyRevision = 3;
                pRecord->Characters[0].MoneyMigratedAt = 1'700'000'000;
            }
        }

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 750;                        // only one side offers money

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";
        right.Items.clear();

        std::string why;
        Check(store.ApplyTrade(left, right, &why), "a one-sided money trade succeeds");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");

        Check(pA && pA->MoneyRevision == 4, "the payer advanced once");
        Check(pB && pB->MoneyRevision == 4, "AND SO DID THE RECIPIENT - their balance moved too");
    }

    { // an exhausted revision must NOT block an item-only trade
        //
        // Headroom is only required when a revision is going to advance. Refusing a trade
        // that never touches MoneyRevision because MoneyRevision is full would block a
        // transaction on a field it does not use.
        Seed(path, 20000, 5000);
        PlayerStore store;
        store.Load(path);

        {
            auto* pRecord = const_cast<PlayerRecord*>(store.Find("222"));
            if (pRecord && !pRecord->Characters.empty())
            {
                pRecord->Characters[0].MoneyRevision = std::numeric_limits<uint64_t>::max();
                pRecord->Characters[0].MoneyMigratedAt = 1'700'000'000;
            }
        }

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Items.push_back({kAmmo, 25});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(store.ApplyTrade(left, right, &why),
              "an item-only trade succeeds even with an EXHAUSTED counterparty revision");

        const auto* pB = CharacterOf(store, "222");
        Check(pB && Economy::Held(*pB, kAmmo) == 25, "and the items arrived");
    }

    { // a REFUSED trade advances nothing - the revision counts changes, not attempts
        Seed(path, 100, 5000);
        PlayerStore store;
        store.Load(path);

        for (const char* discord : {"111", "222"})
        {
            auto* pRecord = const_cast<PlayerRecord*>(store.Find(discord));
            if (pRecord && !pRecord->Characters.empty())
            {
                pRecord->Characters[0].MoneyRevision = 5;
                pRecord->Characters[0].MoneyMigratedAt = 1'700'000'000;
            }
        }

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 99999;                       // cannot afford it

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "an unaffordable trade is still refused");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");

        Check(pA && pA->MoneyRevision == 5, "and the payer's revision did NOT move");
        Check(pB && pB->MoneyRevision == 5, "nor the recipient's");
    }

    { // an exhausted participant is refused BEFORE anything moves
        Seed(path, 20000, 5000);
        PlayerStore store;
        store.Load(path);

        {
            auto* pRecord = const_cast<PlayerRecord*>(store.Find("222"));
            if (pRecord && !pRecord->Characters.empty())
            {
                pRecord->Characters[0].MoneyRevision = std::numeric_limits<uint64_t>::max();
                pRecord->Characters[0].MoneyMigratedAt = 1'700'000'000;
            }
        }

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 1000;

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(!store.ApplyTrade(left, right, &why),
              "a trade with an exhausted participant is refused");
        Check(why == "revision_exhausted", "with the expected reason");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");

        Check(pA && pA->Money == 20000 && pB && pB->Money == 5000,
              "AND NO MONEY MOVED - headroom is validated before mutation, not after");
        Check(pB && pB->MoneyRevision == std::numeric_limits<uint64_t>::max(),
              "the exhausted revision is stuck, not wrapped to zero");
    }

    { // a legacy character can still trade with a migrated one during the cutover
        //
        // Mixed pairs are unavoidable while migration is being rolled out, and refusing them
        // would mean a half-migrated server where some players simply cannot trade.
        Seed(path, 20000, 5000);
        PlayerStore store;
        store.Load(path);

        {
            auto* pRecord = const_cast<PlayerRecord*>(store.Find("111"));
            if (pRecord && !pRecord->Characters.empty())
            {
                pRecord->Characters[0].MoneyRevision = 3;
                pRecord->Characters[0].MoneyMigratedAt = 1'700'000'000;
            }
        }

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 1000;

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(store.ApplyTrade(left, right, &why),
              "a migrated character can trade with a legacy one");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");

        Check(pA && pA->MoneyRevision == 4, "the migrated side advances");
        Check(pB && pB->MoneyRevision == 0, "and the legacy side stays at zero");
        Check(pA && pA->Money == 19000 && pB && pB->Money == 6000, "the money still moved");
    }

    std::filesystem::remove_all(dir, ec);

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
