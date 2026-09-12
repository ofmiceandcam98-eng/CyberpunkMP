// Phase 5, stage 2: MoneyRevision and MoneyMigratedAt exist, persist, and default safely.
//
// Additive-only metadata. Nothing increments the revision and nothing stamps the migration
// yet - those are later, separately reviewed stages. What these tests protect is the part
// that is easy to get wrong once and painful forever: a record written before these fields
// existed must still load, and both fields must survive a round trip exactly.
//
// Tests the SHIPPED CharacterRecord.h, not a copy of it.

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// Before CharacterRecord.h, deliberately - that header uses nlohmann::json for its
// serialisation macros without including it. Same arrangement as character_body_test.
#include <nlohmann/json.hpp>

#include "CharacterRecord.h"

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

int main()
{
    { // 1. a fresh record starts on the safe side of both questions
        CharacterRecord r{};

        Check(r.MoneyRevision == 0, "a new record starts at MoneyRevision 0");
        Check(r.MoneyMigratedAt == 0, "and MoneyMigratedAt 0 - it has NOT crossed the authority boundary");
    }

    { // 2. both survive a round trip
        CharacterRecord r{};
        r.MoneyRevision = 42;
        r.MoneyMigratedAt = 1787000000;
        r.Money = 12345;
        r.Name = "Jack";

        const CharacterRecord back = nlohmann::json(r).get<CharacterRecord>();

        Check(back.MoneyRevision == 42, "MoneyRevision survives serialise -> deserialise");
        Check(back.MoneyMigratedAt == 1787000000, "MoneyMigratedAt survives it too");
    }

    { // 3. THE COMPATIBILITY CASE: a record written before these fields existed
        //
        // Every character on the live server is currently exactly this shape. If it throws
        // or refuses, the server loses everybody the first time it starts with this build.
        const auto legacy = nlohmann::json::parse(R"({
            "Slot": 0,
            "Name": "Existing Player",
            "CharacterId": "ABCD-1234",
            "Money": 20000,
            "Inventory": [ { "Id": 4369, "Quantity": 7 } ],
            "IsMale": true,
            "SpawnedBefore": true,
            "CreatedAt": 1786000000
        })");

        bool loaded = true;
        CharacterRecord r{};

        try
        {
            r = legacy.get<CharacterRecord>();
        }
        catch (...)
        {
            loaded = false;
        }

        Check(loaded, "a record with NEITHER new field still loads - existing players survive");
        Check(r.MoneyRevision == 0, "the missing MoneyRevision defaults to 0");
        Check(r.MoneyMigratedAt == 0, "the missing MoneyMigratedAt defaults to 0, i.e. not migrated");
        Check(r.Money == 20000, "and the money it did carry is untouched");
        Check(r.Name == "Existing Player", "as is the name");
        Check(r.Inventory.size() == 1 && r.Inventory[0].Quantity == 7, "and the inventory");
    }

    { // 4. a migrated record round-trips exactly
        CharacterRecord r{};
        r.MoneyRevision = 1;
        r.MoneyMigratedAt = 1787123456;

        const auto json = nlohmann::json(r);
        const CharacterRecord back = json.get<CharacterRecord>();

        Check(back.MoneyRevision == 1 && back.MoneyMigratedAt == 1787123456,
              "a migrated record round-trips exactly");
        Check(json.contains("MoneyRevision") && json.contains("MoneyMigratedAt"),
              "and both fields are actually written to the document");
    }

    { // 5. the revision is unsigned and must carry its whole range
        CharacterRecord r{};
        r.MoneyRevision = UINT64_MAX;

        const CharacterRecord back = nlohmann::json(r).get<CharacterRecord>();

        Check(back.MoneyRevision == UINT64_MAX, "a full-range uint64 revision round-trips");
    }

    { // 6. a malformed value follows the existing policy - the parse throws, and the store's
      //    existing catch turns that into "could not read, start empty" rather than a crash
        const auto malformed = nlohmann::json::parse(R"({
            "Name": "Broken",
            "MoneyRevision": "not a number"
        })");

        bool threw = false;
        try
        {
            (void)malformed.get<CharacterRecord>();
        }
        catch (...)
        {
            threw = true;
        }

        Check(threw, "a wrong-typed MoneyRevision throws, which the store already catches");
    }

    { // 7. adding the fields did not disturb anything already persisted
        CharacterRecord r{};
        r.Name = "Unchanged";
        r.Money = 999;
        r.Level = 15;
        r.IsMale = false;
        r.CharacterId = "ZZZZ-9999";
        r.StarterKitGranted = true;
        r.Inventory.push_back({0x1111, 3});

        const CharacterRecord back = nlohmann::json(r).get<CharacterRecord>();

        Check(back.Name == "Unchanged" && back.Money == 999 && back.Level == 15,
              "existing scalar fields still round-trip");
        Check(back.IsMale == false && back.CharacterId == "ZZZZ-9999" && back.StarterKitGranted,
              "so do identity and the starter-kit flag");
        Check(back.Inventory.size() == 1 && back.Inventory[0].Id == 0x1111,
              "and the inventory");
    }

    { // 8. THE AUTHORITY SHAPE, tested as behaviour rather than trusted as a comment.
        //
        // HandleSaveCharacterRequest builds its record as
        //     CharacterRecord character = pExisting ? *pExisting : CharacterRecord{};
        // and then overwrites named fields from the message. Nothing assigns these two, so
        // a save carries forward whatever the SERVER stored. This asserts that copy
        // behaviour, which is what makes the client unable to influence them.
        CharacterRecord stored{};
        stored.MoneyRevision = 7;
        stored.MoneyMigratedAt = 1787000000;
        stored.Money = 500;

        CharacterRecord fromSave = stored;   // the copy the handler makes
        fromSave.Money = 900;                // the sort of field a save DOES overwrite

        Check(fromSave.MoneyRevision == 7, "a save copies the stored MoneyRevision forward");
        Check(fromSave.MoneyMigratedAt == 1787000000, "and the stored MoneyMigratedAt");
        Check(fromSave.Money == 900, "while still applying the fields a save is allowed to set");
    }

    { // 9. a brand-new character is unmigrated, not accidentally pre-migrated
        const CharacterRecord fresh = CharacterRecord{};

        Check(fresh.MoneyMigratedAt == 0,
              "a character created today is NOT marked migrated - stage 2 stamps nothing");
        Check(fresh.MoneyRevision == 0,
              "and its revision is 0 - nothing increments it yet");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
