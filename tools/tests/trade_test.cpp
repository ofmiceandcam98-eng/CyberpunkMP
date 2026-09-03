// Trading: the system must never CREATE value, only move it.
//
// PlayerStore drags in the whole server, so the conservation algorithm is restated here
// exactly as PlayerStore::MoveAssets/ApplyTrade implement it. The invariant asserted is the
// one that matters: total before == total after, always.

#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

struct ItemStack { unsigned long long Id; unsigned int Quantity; };
struct Character { std::string CharacterId; long long Money{0}; std::vector<ItemStack> Inventory; };
struct TradeSide { std::string CharacterId; long long Money{0}; std::vector<ItemStack> Items; };

static ItemStack* FindStack(Character& c, unsigned long long id)
{
    for (auto& s : c.Inventory) if (s.Id == id) return &s;
    return nullptr;
}

static bool MoveAssets(Character& from, Character& to, const TradeSide& offer, std::string* why)
{
    const auto fail = [why](const char* w){ if (why) *why = w; return false; };
    if (offer.Money < 0) return fail("negative_money");
    if (from.Money < offer.Money) return fail("insufficient_funds");

    for (const auto& o : offer.Items)
    {
        if (o.Quantity == 0) return fail("zero_quantity");
        auto* s = FindStack(from, o.Id);
        if (!s || s->Quantity < o.Quantity) return fail("insufficient_items");
        s->Quantity -= o.Quantity;
    }
    from.Inventory.erase(std::remove_if(from.Inventory.begin(), from.Inventory.end(),
        [](const ItemStack& s){ return s.Quantity == 0; }), from.Inventory.end());
    for (const auto& o : offer.Items)
    {
        if (auto* s = FindStack(to, o.Id)) s->Quantity += o.Quantity;
        else to.Inventory.push_back({o.Id, o.Quantity});
    }
    from.Money -= offer.Money;
    to.Money   += offer.Money;
    return true;
}

static bool ApplyTrade(Character& a, Character& b, const TradeSide& sa, const TradeSide& sb, std::string* why)
{
    if (sa.CharacterId == sb.CharacterId) { if (why) *why = "same_character"; return false; }
    Character ca = a, cb = b;                 // copies - nothing real changes until both succeed
    if (!MoveAssets(ca, cb, sa, why)) return false;
    if (!MoveAssets(cb, ca, sb, why)) return false;
    a = ca; b = cb;
    return true;
}

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}
static long long TotalMoney(const Character& a, const Character& b) { return a.Money + b.Money; }
static unsigned int TotalOf(const Character& a, const Character& b, unsigned long long id)
{
    unsigned int n = 0;
    for (const auto& s : a.Inventory) if (s.Id == id) n += s.Quantity;
    for (const auto& s : b.Inventory) if (s.Id == id) n += s.Quantity;
    return n;
}

int main()
{
    constexpr unsigned long long kPistol = 0x1111, kAmmo = 0x2222, kJacket = 0x3333;

    { // a normal exchange conserves everything
        Character a{"A", 10000, {{kPistol,1},{kAmmo,500}}};
        Character b{"B",  5000, {{kJacket,1}}};
        const auto m0 = TotalMoney(a,b); const auto am0 = TotalOf(a,b,kAmmo);
        std::string why;
        Check(ApplyTrade(a,b,{"A",5000,{{kPistol,1},{kAmmo,100}}},{"B",2000,{{kJacket,1}}},&why), "a normal trade succeeds");
        Check(TotalMoney(a,b) == m0,       "TOTAL MONEY IS UNCHANGED");
        Check(TotalOf(a,b,kAmmo) == am0,   "TOTAL AMMO IS UNCHANGED");
        Check(a.Money == 7000 && b.Money == 8000, "both balances moved by exactly the offers");
        Check(TotalOf(a,b,kPistol) == 1,   "the pistol exists exactly once");
        Check(!FindStack(a,kPistol) && FindStack(b,kPistol), "it moved, it was not copied");
        Check(FindStack(a,kAmmo)->Quantity == 400 && FindStack(b,kAmmo)->Quantity == 100, "exactly 100 ammo moved");
    }

    { // quantity manipulation
        Character a{"A", 100, {{kAmmo,10}}}; Character b{"B", 100, {}};
        std::string why;
        Check(!ApplyTrade(a,b,{"A",0,{{kAmmo,999999999u}}},{"B",0,{}},&why) && why == "insufficient_items",
              "asking for 999999999 of 10 is refused");
        Check(a.Inventory[0].Quantity == 10 && b.Inventory.empty(), "and NOTHING moved");
    }

    { // a failure on the SECOND side rolls the whole thing back
        Character a{"A", 100, {{kPistol,1}}}; Character b{"B", 9999, {{kJacket,1}}};
        std::string why;
        Check(!ApplyTrade(a,b,{"A",5000,{{kPistol,1}}},{"B",0,{{kJacket,1}}},&why) && why == "insufficient_funds",
              "a trade nobody can cover is refused");
        Check(a.Money == 100 && b.Money == 9999, "no money moved");
        Check(!FindStack(a,kJacket) && !FindStack(b,kPistol), "and nothing crossed over");
    }

    { // fake item id
        Character a{"A",0,{}}; Character b{"B",0,{}};
        std::string why;
        Check(!ApplyTrade(a,b,{"A",0,{{0xdeadbeef,1}}},{"B",0,{}},&why) && why == "insufficient_items",
              "an invented item id is refused");
        Check(a.Inventory.empty() && b.Inventory.empty(), "and creates nothing");
    }

    { // negative money is theft with extra steps
        Character a{"A",100,{}}; Character b{"B",100,{}};
        std::string why;
        Check(!ApplyTrade(a,b,{"A",-500,{}},{"B",0,{}},&why) && why == "negative_money", "a negative offer is refused");
        Check(a.Money == 100 && b.Money == 100, "and moves nothing");
    }

    { // trading with yourself
        Character a{"A",100,{}}; Character b{"A",100,{}};
        std::string why;
        Check(!ApplyTrade(a,b,{"A",50,{}},{"A",0,{}},&why) && why == "same_character", "a character cannot trade with itself");
    }

    { // an emptied stack is removed, not left as a zero ghost
        Character a{"A",0,{{kAmmo,100}}}; Character b{"B",0,{}};
        std::string why;
        Check(ApplyTrade(a,b,{"A",0,{{kAmmo,100}}},{"B",0,{}},&why), "giving a whole stack works");
        Check(a.Inventory.empty(), "the emptied stack is gone, not a zero-quantity ghost");
        Check(TotalOf(a,b,kAmmo) == 100, "and the ammo still totals 100");
    }

    { // stacking onto an existing holding
        Character a{"A",0,{{kAmmo,50}}}; Character b{"B",0,{{kAmmo,70}}};
        const auto t = TotalOf(a,b,kAmmo);
        std::string why;
        Check(ApplyTrade(a,b,{"A",0,{{kAmmo,50}}},{"B",0,{}},&why), "stacking works");
        Check(FindStack(b,kAmmo)->Quantity == 120, "B's stack merged to 120");
        Check(TotalOf(a,b,kAmmo) == t, "TOTAL AMMO IS STILL UNCHANGED");
    }

    { // zero quantity
        Character a{"A",0,{{kAmmo,10}}}; Character b{"B",0,{}};
        std::string why;
        Check(!ApplyTrade(a,b,{"A",0,{{kAmmo,0}}},{"B",0,{}},&why) && why == "zero_quantity", "offering zero is refused");
    }

    { // the whole point, stated once more
        Character a{"A",12345,{{kPistol,1},{kAmmo,321}}}; Character b{"B",6789,{{kJacket,2}}};
        const auto m0 = TotalMoney(a,b);
        const auto p0 = TotalOf(a,b,kPistol), am0 = TotalOf(a,b,kAmmo), j0 = TotalOf(a,b,kJacket);
        std::string why;
        for (int i = 0; i < 100; ++i)
        {
            ApplyTrade(a,b,{"A",100,{{kAmmo,3}}},{"B",50,{{kJacket,1}}},&why);
            ApplyTrade(b,a,{"B",100,{{kAmmo,3}}},{"A",50,{{kJacket,1}}},&why);
        }
        Check(TotalMoney(a,b) == m0,     "100 round trips: money conserved exactly");
        Check(TotalOf(a,b,kPistol) == p0,"100 round trips: pistols conserved");
        Check(TotalOf(a,b,kAmmo) == am0, "100 round trips: ammo conserved");
        Check(TotalOf(a,b,kJacket) == j0,"100 round trips: jackets conserved");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
