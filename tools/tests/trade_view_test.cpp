// Trade VIEW - what the trade panel is told, checked against what the /trade commands accept.
//
// WHY THIS EXISTS. The panel draws NotifyTrade and nothing else, and every figure in it is
// something a player acts on: the eddies they can still put in, the items they can pick, the
// version their CONFIRM is checked against. If BuildTradeView offers 500 of something the
// /trade item check would only take 300 of, the panel invites a click the server refuses -
// and the player reads that as the panel being broken. So the view is built from the SAME
// questions those commands ask (AvailableMoney against the reservation, HeldQuantity against
// what other trades promise), and this proves it on the real headers rather than a model.
//
// It also pins the one property the confirm guard rests on: the version is the same number
// from both sides and moves on every edit.

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>   // PlayerStore.h uses it and relies on its includer, like the other tests
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "TradeStore.h"

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

static constexpr uint64_t kPistol = 0x1111;
static constexpr uint64_t kAmmo = 0x2222;

static CharacterRecord MakeA()
{
    CharacterRecord a;
    a.CharacterId = "CHAR-A";
    a.Name = "Alpha";
    a.Money = 1000;
    a.Inventory.push_back({kPistol, 1});
    a.Inventory.push_back({kAmmo, 500});
    a.Inventory.push_back({0, 7});         // a zero id is not an item
    a.Inventory.push_back({kAmmo, 40});    // a second stack of the same id
    return a;
}

static const CharacterRecord::ItemStack* Holding(const TradeView& acView, uint64_t aId)
{
    for (const auto& stack : acView.Holdings)
    {
        if (stack.Id == aId)
            return &stack;
    }
    return nullptr;
}

int main()
{
    const auto a = MakeA();

    { // Sides, partner and the version.
        TradeStore trades;
        auto& s = trades.Begin("CHAR-A", "CHAR-B");
        s.State = TradeState::Open;

        const auto va = BuildTradeView(trades, s, "CHAR-A", &a, "Bravo");
        const auto vb = BuildTradeView(trades, s, "CHAR-B", nullptr, "Alpha");

        Check(va.Mine == &s.OfferA && va.Theirs == &s.OfferB, "A's view: mine is OfferA, theirs is OfferB");
        Check(vb.Mine == &s.OfferB && vb.Theirs == &s.OfferA, "B's view: mine is OfferB, theirs is OfferA");
        Check(!va.Incoming && vb.Incoming, "only the invited side is incoming");
        Check(va.Partner == "Bravo", "the partner is the name the caller resolved");
        Check(va.Version == 0 && vb.Version == 0, "a fresh trade is version 0 from both sides");

        s.OfferA.Touch();
        s.OfferB.Touch();
        Check(TradeVersion(s) == 2, "an edit (both offers touched) moves the version");
        Check(BuildTradeView(trades, s, "CHAR-A", &a, "Bravo").Version ==
                  BuildTradeView(trades, s, "CHAR-B", nullptr, "Alpha").Version,
              "both sides see the same version");

        s.OfferA.Confirmed = true;
        Check(TradeVersion(s) == 2, "confirming does not move the version");

        const auto vNobody = BuildTradeView(trades, s, "CHAR-Z", &a, "?");
        Check(vNobody.Mine == nullptr, "somebody not in the trade gets no view");
    }

    { // Money: this offer's own eddies are added back; another trade's are not.
        TradeStore trades;
        auto& s = trades.Begin("CHAR-A", "CHAR-B");
        s.State = TradeState::Open;
        s.OfferA.Money = 300;

        Check(BuildTradeView(trades, s, "CHAR-A", &a, "Bravo").Available == 1000,
              "money already in THIS offer still counts as available - changing it is not spending it twice");

        auto& other = trades.Begin("CHAR-A", "CHAR-C");
        other.State = TradeState::Open;
        other.OfferA.Money = 200;

        Check(BuildTradeView(trades, s, "CHAR-A", &a, "Bravo").Available == 800,
              "money promised in another live trade is not available here");

        // The figure /trade money compares against, computed the way the command does.
        const auto reserved = trades.ReservedMoney("CHAR-A") - s.OfferA.Money;
        Check(BuildTradeView(trades, s, "CHAR-A", &a, "Bravo").Available ==
                  PlayerStore::AvailableMoney(a, reserved),
              "available matches the /trade money check exactly");
    }

    { // Holdings: one row per item, the quantity /trade item accepts.
        TradeStore trades;
        auto& s = trades.Begin("CHAR-A", "CHAR-B");
        s.State = TradeState::Open;

        auto view = BuildTradeView(trades, s, "CHAR-A", &a, "Bravo");
        Check(view.Holdings.size() == 2, "two distinct items, the zero id dropped and the repeat merged");
        Check(view.Holdings.size() == 2 && view.Holdings[0].Id == kPistol, "rows keep the inventory's order");
        Check(Holding(view, kAmmo) && Holding(view, kAmmo)->Quantity == PlayerStore::HeldQuantity(a, kAmmo),
              "an item's quantity is HeldQuantity - what /trade item checks against");

        s.OfferA.Items.push_back({kAmmo, 100});
        view = BuildTradeView(trades, s, "CHAR-A", &a, "Bravo");
        Check(Holding(view, kAmmo) && Holding(view, kAmmo)->Quantity == 500,
              "ammo already in THIS offer is still all available - re-offering sets the amount");

        auto& other = trades.Begin("CHAR-A", "CHAR-C");
        other.State = TradeState::Open;
        other.OfferA.Items.push_back({kAmmo, 200});
        other.OfferA.Items.push_back({kPistol, 1});

        view = BuildTradeView(trades, s, "CHAR-A", &a, "Bravo");
        Check(Holding(view, kAmmo) && Holding(view, kAmmo)->Quantity == 300,
              "ammo promised in another live trade is held back");
        Check(Holding(view, kPistol) == nullptr,
              "an item wholly promised elsewhere is not offered at all");
    }

    { // An ended trade still carries both final offers - the client applies them on
      // Completed - but offers nothing to pick from.
        TradeStore trades;
        auto& s = trades.Begin("CHAR-A", "CHAR-B");
        s.State = TradeState::Open;
        s.OfferA.Items.push_back({kPistol, 1});
        s.OfferB.Money = 250;
        trades.End(s, TradeState::Completed);

        const auto view = BuildTradeView(trades, s, "CHAR-A", &a, "Bravo");
        Check(view.Mine && view.Mine->Items.size() == 1, "a completed trade still says what I gave");
        Check(view.Theirs && view.Theirs->Money == 250, "and what I got");
        Check(view.Available == 0 && view.Holdings.empty(), "but offers nothing to pick from");
    }

    { // No record (character gone) - still a view of the offers, nothing to pick from.
        TradeStore trades;
        auto& s = trades.Begin("CHAR-A", "CHAR-B");
        s.State = TradeState::Open;

        const auto view = BuildTradeView(trades, s, "CHAR-A", nullptr, "Bravo");
        Check(view.Mine != nullptr && view.Available == 0 && view.Holdings.empty(),
              "a missing record offers nothing rather than guessing");
    }

    std::printf("%s\n", failures == 0 ? "trade_view_test: all passed" : "trade_view_test: FAILURES");
    return failures == 0 ? 0 : 1;
}
