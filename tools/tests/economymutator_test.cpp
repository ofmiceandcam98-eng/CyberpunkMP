// The economy mutation boundary: every money and inventory change the server makes on
// purpose goes through here, and none of them may corrupt a record.
//
// Tests the SHIPPED EconomyMutator.h. The rule these protect is the one that outlives every
// individual caller: a failed operation leaves the record byte-identical, and nothing is
// ever silently clamped.

#include <cstdio>
#include <cstdint>
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

static CharacterRecord Rich(int64_t aMoney = 20000)
{
    CharacterRecord r{};
    r.Money = aMoney;
    r.Inventory.push_back({0x1111, 100});
    return r;
}

int main()
{
    using namespace Economy;

    // ---------------------------------------------------------------- money ----
    { // exact debit and credit
        auto r = Rich();
        Check(Debit(r, 5000) == Result::Success && r.Money == 15000, "an exact debit");
        Check(Credit(r, 2000) == Result::Success && r.Money == 17000, "an exact credit");
    }

    { // the whole balance can be spent
        auto r = Rich(500);
        Check(Debit(r, 500) == Result::Success && r.Money == 0, "the entire balance can be debited");
    }

    { // zero and negative are refused, not treated as no-ops
        auto r = Rich();
        Check(Debit(r, 0) == Result::InvalidAmount, "a zero debit is refused");
        Check(Credit(r, 0) == Result::InvalidAmount, "a zero credit is refused");
        Check(Debit(r, -100) == Result::InvalidAmount, "a negative debit is refused");
        Check(Credit(r, -100) == Result::InvalidAmount, "a negative credit is refused");
        Check(r.Money == 20000, "and none of them changed the balance");
    }

    { // THE POINT: overspending FAILS rather than zeroing
        auto r = Rich(100);
        Check(Debit(r, 101) == Result::InsufficientFunds, "spending more than you have fails");
        Check(r.Money == 100, "and the balance is UNCHANGED - not clamped to zero");
    }

    { // and over-crediting FAILS rather than saturating
        auto r = Rich(EconomyMigration::kMaxPlausibleMoney);
        Check(Credit(r, 1) == Result::Overflow, "crediting past the ceiling fails");
        Check(r.Money == EconomyMigration::kMaxPlausibleMoney,
              "and the balance is UNCHANGED - not clamped to the maximum");
    }

    { // int64 overflow specifically, not just the ceiling
        auto r = Rich();
        r.Money = std::numeric_limits<int64_t>::max() - 5;
        Check(Credit(r, 100) == Result::Overflow, "an int64 wrap is caught before it happens");
    }

    { // the ceiling is the SAME one the save path and migration use
        auto r = Rich(0);
        Check(Credit(r, EconomyMigration::kMaxPlausibleMoney) == Result::Success,
              "a credit exactly to the ceiling is allowed");
        Check(r.Money == EconomyMigration::kMaxPlausibleMoney, "and lands exactly there");
    }

    // ------------------------------------------------------------- transfer ----
    { // the ordinary case
        auto a = Rich(20000);
        auto b = Rich(5000);

        Check(Transfer(a, b, 3000) == Result::Success, "a transfer succeeds");
        Check(a.Money == 17000 && b.Money == 8000, "and both balances move by exactly the amount");
        Check(a.Money + b.Money == 25000, "TOTAL MONEY IS CONSERVED");
    }

    { // insufficient funds leaves BOTH untouched
        auto a = Rich(100);
        auto b = Rich(5000);

        Check(Transfer(a, b, 500) == Result::InsufficientFunds, "an unaffordable transfer fails");
        Check(a.Money == 100 && b.Money == 5000, "and NEITHER side moved");
    }

    { // THE DESTRUCTION CASE: the recipient's ceiling is checked before the payer is touched
        auto a = Rich(20000);
        auto b = Rich(EconomyMigration::kMaxPlausibleMoney);

        Check(Transfer(a, b, 1) == Result::Overflow, "a transfer that cannot land is refused");
        Check(a.Money == 20000, "AND THE PAYER STILL HAS THEIR MONEY - it was not destroyed");
        Check(b.Money == EconomyMigration::kMaxPlausibleMoney, "the recipient is unchanged too");
    }

    { // self-transfer conserves rather than duplicating, if a caller ever does it
        auto a = Rich(1000);
        const auto before = a.Money;
        Transfer(a, a, 500);
        Check(a.Money == before, "transferring to yourself does not create money");
    }

    // ------------------------------------------------------------ inventory ----
    { // adding creates, then merges
        CharacterRecord r{};
        Check(AddItem(r, 0x2222, 5) == Result::Success, "adding a new item creates a stack");
        Check(r.Inventory.size() == 1 && r.Inventory[0].Quantity == 5, "with the right quantity");

        Check(AddItem(r, 0x2222, 3) == Result::Success, "adding more merges");
        Check(r.Inventory.size() == 1 && r.Inventory[0].Quantity == 8, "into the existing stack");
    }

    { // removing part, then the rest
        auto r = Rich();
        Check(RemoveItem(r, 0x1111, 40) == Result::Success && Held(r, 0x1111) == 60,
              "a partial removal leaves the remainder");

        Check(RemoveItem(r, 0x1111, 60) == Result::Success, "removing the rest works");
        Check(Held(r, 0x1111) == 0, "and nothing is held");
        Check(r.Inventory.empty(), "the emptied stack is ERASED, not left as a zero ghost");
    }

    { // removing more than owned fails and changes nothing
        auto r = Rich();
        Check(RemoveItem(r, 0x1111, 101) == Result::InsufficientQuantity,
              "removing more than owned fails");
        Check(Held(r, 0x1111) == 100, "and the quantity is UNCHANGED - not floored at zero");
    }

    { // an item you do not have
        CharacterRecord r{};
        Check(RemoveItem(r, 0x9999, 1) == Result::InsufficientQuantity,
              "removing an item you do not have fails");
        Check(r.Inventory.empty(), "and creates nothing");
    }

    { // quantity overflow on add
        CharacterRecord r{};
        r.Inventory.push_back({0x1111, std::numeric_limits<uint32_t>::max() - 5});

        Check(AddItem(r, 0x1111, 10) == Result::Overflow, "a stack that would wrap uint32 fails");
        Check(r.Inventory[0].Quantity == std::numeric_limits<uint32_t>::max() - 5,
              "and the quantity is unchanged - nothing was destroyed by a wrap");
    }

    { // id 0 and quantity 0 are not holdings
        CharacterRecord r{};
        Check(AddItem(r, 0, 5) == Result::InvalidItem, "item id 0 is not an item");
        Check(AddItem(r, 0x1111, 0) == Result::InvalidAmount, "a zero quantity is not a holding");
        Check(RemoveItem(r, 0, 5) == Result::InvalidItem, "and neither can be removed");
        Check(r.Inventory.empty(), "none of which created anything");
    }

    { // duplicate stacks: FIRST MATCH, matching PlayerStore exactly
        //
        // Preserved rather than fixed - see the note on FindStack. Asserted so that if
        // anybody ever changes it, they do so deliberately.
        CharacterRecord r{};
        r.Inventory.push_back({0x1111, 10});
        r.Inventory.push_back({0x1111, 90});   // only a client save can produce this

        Check(Held(r, 0x1111) == 10, "Held reports the FIRST stack only, as PlayerStore does");
        Check(RemoveItem(r, 0x1111, 20) == Result::InsufficientQuantity,
              "so a removal larger than the first stack fails - under-reporting, the safe way");
        Check(r.Inventory.size() == 2, "and nothing was merged behind the caller's back");
    }

    { // a failed operation is byte-identical, checked by serialising the whole record
        auto r = Rich();
        const auto before = nlohmann::json(r).dump();

        Debit(r, 999999);
        Credit(r, -1);
        AddItem(r, 0, 5);
        RemoveItem(r, 0x1111, 500);

        Check(nlohmann::json(r).dump() == before,
              "four failed operations leave the record byte-for-byte identical");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
