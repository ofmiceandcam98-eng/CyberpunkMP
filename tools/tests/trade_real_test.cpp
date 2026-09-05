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

    std::filesystem::remove_all(dir, ec);

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
