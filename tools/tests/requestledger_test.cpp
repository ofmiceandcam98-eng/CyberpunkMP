// Idempotency: a retry must not do the work twice, and the ledger that guarantees it must
// not become a memory-exhaustion surface.
//
// Unlike the other tests here, this one includes the REAL header rather than restating the
// algorithm - RequestLedger depends on nothing but the standard library, so there is no
// reason to test a copy of it. If it stops being self-contained, that is worth knowing too:
// this file will stop compiling.

#include "RequestLedger.h"

#include <cstdio>
#include <string>

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

int main()
{
    { // the whole point: the same request answered twice gives the same answer once
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);

        Check(ledger.Find("charA", "req-1") == nullptr, "an unseen request is not remembered");

        ledger.Record("charA", "req-1", "MSG-0001");

        const auto* pAgain = ledger.Find("charA", "req-1");
        Check(pAgain != nullptr, "a retry finds the original");
        Check(pAgain && *pAgain == "MSG-0001", "and gets the ORIGINAL message id, not a new one");
    }

    { // distinct requests are distinct work
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);

        ledger.Record("charA", "req-1", "MSG-0001");
        ledger.Record("charA", "req-2", "MSG-0002");

        Check(*ledger.Find("charA", "req-1") == "MSG-0001", "req-1 keeps its own result");
        Check(*ledger.Find("charA", "req-2") == "MSG-0002", "req-2 keeps its own");
    }

    { // CROSS-PLAYER REPLAY: one player cannot claim another's request id
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);

        ledger.Record("charA", "req-1", "MSG-0001");

        Check(ledger.Find("charB", "req-1") == nullptr,
              "another character replaying the same id gets NOTHING");
        Check(*ledger.Find("charA", "req-1") == "MSG-0001", "and the owner's entry is untouched");
    }

    { // an empty id is "no idempotency", never a key
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);

        ledger.Record("charA", "", "MSG-0001");

        Check(ledger.Find("charA", "") == nullptr,
              "an empty request id is never remembered - or every idless send would collide");
        Check(ledger.Size() == 0, "and nothing was stored for it");
    }

    { // an empty owner is refused too - it would pool every unauthenticated caller together
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);
        ledger.Record("", "req-1", "MSG-0001");
        Check(ledger.Size() == 0, "an empty owner is not stored");
    }

    { // TTL: a retry long afterwards is new work, not a duplicate
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);
        ledger.Record("charA", "req-1", "MSG-0001");

        ledger.SetClockForTesting(1000 + RequestLedger::kTtlSeconds - 1);
        Check(ledger.Find("charA", "req-1") != nullptr, "still remembered just inside the TTL");

        ledger.SetClockForTesting(1000 + RequestLedger::kTtlSeconds);
        Check(ledger.Find("charA", "req-1") == nullptr, "and forgotten once past it");
    }

    { // Expire actually reclaims, rather than only hiding
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);
        for (int i = 0; i < 10; ++i)
            ledger.Record("charA", "req-" + std::to_string(i), "MSG");

        Check(ledger.Size() == 10, "ten entries stored");

        ledger.SetClockForTesting(1000 + RequestLedger::kTtlSeconds);
        ledger.Expire();

        Check(ledger.Size() == 0, "Expire reclaims them, so a quiet server does not accumulate");
    }

    { // MEMORY EXHAUSTION: one owner cannot grow the ledger without limit
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);

        for (int i = 0; i < 5000; ++i)
            ledger.Record("flooder", "req-" + std::to_string(i), "MSG");

        Check(ledger.Size() <= RequestLedger::kPerOwnerLimit,
              "5000 distinct ids from one owner are capped to the per-owner limit");
    }

    { // and a flooder cannot evict everybody else
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);

        ledger.Record("victim", "important", "MSG-KEEP");

        for (int i = 0; i < 5000; ++i)
            ledger.Record("flooder", "req-" + std::to_string(i), "MSG");

        const auto* pKept = ledger.Find("victim", "important");
        Check(pKept != nullptr, "a flood does NOT evict another player's entry");
        Check(pKept && *pKept == "MSG-KEEP", "so their retry is still deduplicated");
    }

    { // repeating ONE id does not grow anything - the obvious way to try
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);

        for (int i = 0; i < 10000; ++i)
            ledger.Record("charA", "same", "MSG-0001");

        Check(ledger.Size() == 1, "10,000 records of one id store exactly one entry");
    }

    { // the global cap holds across many owners
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);

        for (int owner = 0; owner < 500; ++owner)
            for (int i = 0; i < 20; ++i)
                ledger.Record("char-" + std::to_string(owner), "req-" + std::to_string(i), "MSG");

        Check(ledger.Size() <= RequestLedger::kGlobalLimit,
              "10,000 entries across 500 owners are bounded by the global cap");
    }

    { // a re-recorded id refreshes rather than duplicating
        RequestLedger ledger;
        ledger.SetClockForTesting(1000);
        ledger.Record("charA", "req-1", "MSG-0001");

        ledger.SetClockForTesting(1000 + RequestLedger::kTtlSeconds - 1);
        ledger.Record("charA", "req-1", "MSG-0001");   // refreshed

        ledger.SetClockForTesting(1000 + RequestLedger::kTtlSeconds + 1);
        Check(ledger.Find("charA", "req-1") != nullptr, "re-recording refreshes the TTL");
        Check(ledger.Size() == 1, "and does not add a second entry");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
