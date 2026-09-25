// Trade ABUSE - the cases somebody reaches for when they want free things.
//
// WHY THIS EXISTS ALONGSIDE trade_test AND trade_real_test.
//
// Those two prove the HAPPY PATH: trade_test proves the conservation invariant over many
// permutations, trade_real_test proves the shipped implementation does the same. Between
// them exactly one hostile case was covered - "offering the same stack twice to reach 120
// of 100 is refused". Everything else a griefer would actually try was unguarded by any
// test, which was found while planning the two-player session (2026-09-24): the session was
// going to be asked to prove duplication by hand, with two humans, at the end of a night.
//
// A duplication bug is the worst class this project can ship. It is silent, it is
// retroactive, and by the time somebody reports "my gun is gone" the economy has already
// been laundered through half the server. So the abuse matrix belongs here, where it runs
// on every Verify, and the live session only has to CONFIRM rather than discover.
//
// THE INVARIANT EVERY CASE BELOW ASSERTS: a refused trade changes NOTHING. Not one eddie,
// not one item, on either side. ApplyTrade validates on copies and assigns only on success
// (see its banner in PlayerStore.h) - these tests are what stop that property being
// refactored away by somebody who does not know it is load-bearing.

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
static constexpr uint64_t kNeverOwned = 0x9999;

// A: 1 pistol + 500 ammo. B: no items. Money per the arguments.
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

// "Nothing moved" as one assertion, because every refusal case below needs exactly this and
// a per-field spelling of it is where a missed field hides.
static bool Untouched(const PlayerStore& acStore, int64_t aMoneyA, int64_t aMoneyB)
{
    const auto* pA = CharacterOf(acStore, "111");
    const auto* pB = CharacterOf(acStore, "222");

    if (!pA || !pB)
        return false;

    return pA->Money == aMoneyA && pB->Money == aMoneyB &&
           Economy::Held(*pA, kPistol) == 1 && Economy::Held(*pB, kPistol) == 0 &&
           Economy::Held(*pA, kAmmo) == 500 && Economy::Held(*pB, kAmmo) == 0;
}

int main()
{
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "nco-trade-abuse";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    const auto path = dir / "players.json";

    { // Trading with yourself. Free duplication if it ever succeeds: one record, copied
      // twice, both copies mutated, last write wins.
        Seed(path, 1000, 1000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Items.push_back({kPistol, 1});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-A";

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "a self-trade is refused");
        Check(why == "same_character", "and says same_character");
        Check(Untouched(store, 1000, 1000), "self-trade changed nothing");
    }

    { // A character that does not exist. Must not half-apply the side that DOES exist.
        Seed(path, 1000, 1000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 500;
        left.Items.push_back({kPistol, 1});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-GHOST";

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "trading with a character that does not exist is refused");
        Check(why == "no_character", "and says no_character");
        Check(Untouched(store, 1000, 1000), "the real side was NOT charged for a ghost");
    }

    { // Offering an item you have never owned.
        Seed(path, 1000, 1000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Items.push_back({kNeverOwned, 1});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "offering an item you do not own is refused");
        Check(Untouched(store, 1000, 1000), "and nothing moved");
    }

    { // Offering more of a real item than you hold.
        Seed(path, 1000, 1000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Items.push_back({kAmmo, 501});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "offering 501 of 500 is refused");
        Check(Untouched(store, 1000, 1000), "and nothing moved");
    }

    { // Offering more eddies than you hold.
        Seed(path, 100, 1000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 101;

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "offering more eddies than you hold is refused");
        Check(Untouched(store, 100, 1000), "and nothing moved");
    }

    /*
     * THE HALF-TRADE. The case the whole design exists to prevent, and the one a hand-rolled
     * implementation gets wrong: the FIRST side is perfectly valid and the SECOND is not.
     *
     * A naive implementation moves A's goods, discovers B cannot pay, and either leaves the
     * goods gone or tries to unwind by hand. ApplyTrade moves both on COPIES and writes back
     * only if both succeeded, so the failure must be total.
     */
    { // A offers a real pistol and real eddies; B offers eddies it does not have.
        Seed(path, 5000, 10);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 1000;
        left.Items.push_back({kPistol, 1});
        left.Items.push_back({kAmmo, 200});

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";
        right.Money = 9999; // B holds 10

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "a valid side plus an invalid side is refused WHOLE");
        Check(Untouched(store, 5000, 10),
              "THE HALF-TRADE INVARIANT: the valid side kept its pistol, its ammo and its eddies");
    }

    { // And the mirror, so the guard is not only on one ordering.
        Seed(path, 10, 5000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 9999; // A holds 10

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";
        right.Money = 1000;

        std::string why;
        Check(!store.ApplyTrade(left, right, &why), "the same, with the invalid side FIRST");
        Check(Untouched(store, 10, 5000), "still nothing moved");
    }

    /*
     * REPLAY. This one DOCUMENTS a real property rather than asserting a guard, and the
     * distinction matters for whoever reads a green run.
     *
     * ApplyTrade is deliberately NOT idempotent: it is the thing that moves assets, and
     * asking it to also recognise "I have seen this exchange before" would make it the
     * authority on request identity as well. That job belongs to the request ledger
     * (RequestLedger, tools/tests/requestledger_test.cpp).
     *
     * So a replayed COMMIT does move the assets a second time. That is correct here and
     * dangerous one layer up - it means the protection against a duplicated or replayed
     * trade packet lives entirely in the caller, and the live session must exercise it
     * there (§5 of the two-player plan) rather than assuming the store will catch it.
     */
    { // Apply the identical trade twice.
        Seed(path, 5000, 5000);
        PlayerStore store;
        store.Load(path);

        PlayerStore::TradeSide left;
        left.CharacterId = "CHAR-A";
        left.Money = 1000;

        PlayerStore::TradeSide right;
        right.CharacterId = "CHAR-B";

        std::string why;
        Check(store.ApplyTrade(left, right, &why), "the first commit succeeds");
        Check(store.ApplyTrade(left, right, &why),
              "REPLAY: a second identical commit ALSO succeeds - ApplyTrade is not idempotent by design");

        const auto* pA = CharacterOf(store, "111");
        const auto* pB = CharacterOf(store, "222");
        Check(pA && pB && pA->Money == 3000 && pB->Money == 7000,
              "so the money moved TWICE - replay protection belongs to the caller, not the store");
        Check(pA && pB && (pA->Money + pB->Money) == 10000,
              "conservation still holds even under replay - nothing was created");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all trade-abuse checks passed");
    return failures ? 1 : 0;
}
