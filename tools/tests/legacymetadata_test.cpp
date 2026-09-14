// The stage 6 metadata rename, and the one case it must never guess at.
//
// EconomyRevision / MigratedAt became MoneyRevision / MoneyMigratedAt when money and
// inventory stopped crossing the authority boundary together.
//
// The rename is safe for a precise reason, not a hopeful one: no migration has ever run, so
// every persisted value is 0, and a record carrying only the old keys loads with the new ones
// defaulted to 0 - not migrated, which is exactly correct. THIS FILE MAKES THAT TESTABLE
// rather than assumed.
//
// The dangerous case is a NONZERO old key. It would mean a record crossed the boundary under
// the old meaning, where the mark claimed money AND inventory, and nothing in the number says
// how to narrow it. Reading it as MoneyMigratedAt would silently shrink what it asserted;
// reading it as both would claim inventory authority the server does not have. So it is
// detected and reported and NOT reinterpreted, and the record stays unmigrated - the safe
// reading in both directions.

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "EconomyMigration.h"
#include "EconomyMutator.h"   // Economy::IsMigrated - the check the server itself uses

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

// A players.json shaped document with one character carrying whatever metadata is asked for.
static nlohmann::json Document(const nlohmann::json& acCharacterExtras,
                               const char* acCharacterId = "CHAR-A")
{
    nlohmann::json character;
    character["CharacterId"] = acCharacterId;
    character["Name"] = "Test";
    character["Money"] = 20000;

    for (auto it = acCharacterExtras.begin(); it != acCharacterExtras.end(); ++it)
        character[it.key()] = it.value();

    nlohmann::json record;
    record["DiscordId"] = "111";
    record["Username"] = "tester";
    record["Characters"] = nlohmann::json::array({character});

    return nlohmann::json::array({record});
}

int main()
{
    using namespace EconomyMigration;

    // ------------------------------------------ the ordinary case: old keys, all zero ----
    { // EVERY FILE ON DISK TODAY LOOKS LIKE THIS
        const auto doc = Document({{"EconomyRevision", 0}, {"MigratedAt", 0}});
        const auto found = InspectLegacyMetadata(doc);

        Check(found.Present, "zero-valued legacy keys are noticed");
        Check(!found.Nonzero, "but NOT flagged - zero is the state every live record is in");
        Check(found.Characters.empty(), "and nobody needs looking at");

        // The load path is what actually matters: it must come back unmigrated.
        const auto record = doc[0]["Characters"][0].get<CharacterRecord>();

        Check(record.MoneyRevision == 0, "a legacy record loads with MoneyRevision 0");
        Check(record.MoneyMigratedAt == 0, "and MoneyMigratedAt 0");
        Check(record.Money == 20000, "with its balance intact - the rename touches nothing else");
        Check(Classify(record).Result == State::Unmigrated,
              "and classifies as UNMIGRATED, which is what it is");
    }

    { // a record written before stage 2 has neither old key nor new
        const auto doc = Document({});
        const auto found = InspectLegacyMetadata(doc);

        Check(!found.Present, "a pre-stage-2 record has no legacy keys at all");
        Check(!found.Nonzero, "and nothing to flag");

        const auto record = doc[0]["Characters"][0].get<CharacterRecord>();
        Check(record.MoneyRevision == 0 && record.MoneyMigratedAt == 0,
              "and still loads as unmigrated");
    }

    { // the new keys, written by this build
        const auto doc = Document({{"MoneyRevision", 4}, {"MoneyMigratedAt", 1700000000}});
        const auto found = InspectLegacyMetadata(doc);

        Check(!found.Present, "new-format keys are not mistaken for legacy ones");

        const auto record = doc[0]["Characters"][0].get<CharacterRecord>();
        Check(record.MoneyRevision == 4 && record.MoneyMigratedAt == 1700000000,
              "and they load as themselves");
    }

    // ------------------------------------------------- THE CASE THAT MUST NOT BE GUESSED ----
    { // a nonzero legacy revision
        const auto doc = Document({{"EconomyRevision", 7}, {"MigratedAt", 0}});
        const auto found = InspectLegacyMetadata(doc);

        Check(found.Nonzero, "a NONZERO legacy revision is flagged");
        Check(found.Characters.size() == 1 && found.Characters[0] == "CHAR-A",
              "and the character is named, so a human knows where to look");

        const auto record = doc[0]["Characters"][0].get<CharacterRecord>();

        Check(record.MoneyRevision == 0,
              "AND IT IS NOT REINTERPRETED - MoneyRevision stays 0, not 7");
        Check(!Economy::IsMigrated(record),
              "so the record is treated as UNMIGRATED, the safe reading in both directions");
    }

    { // a nonzero legacy timestamp
        const auto doc = Document({{"EconomyRevision", 0}, {"MigratedAt", 1690000000}});
        const auto found = InspectLegacyMetadata(doc);

        Check(found.Nonzero, "a NONZERO legacy timestamp is flagged too");

        const auto record = doc[0]["Characters"][0].get<CharacterRecord>();
        Check(record.MoneyMigratedAt == 0, "and is not carried across either");
    }

    { // both keys checked independently - an inconsistent pair is exactly what needs a human
        const auto doc = Document({{"EconomyRevision", 3}, {"MigratedAt", 1690000000}});
        const auto found = InspectLegacyMetadata(doc);

        Check(found.Nonzero, "a fully-populated legacy pair is flagged");
        Check(found.Characters.size() == 1,
              "and reported ONCE for the character, not once per key");
    }

    { // several characters, only some affected
        nlohmann::json doc = nlohmann::json::array();

        nlohmann::json record;
        record["DiscordId"] = "111";
        record["Username"] = "tester";
        record["Characters"] = nlohmann::json::array();

        auto make = [](const char* id, int revision)
        {
            nlohmann::json c;
            c["CharacterId"] = id;
            c["Name"] = id;
            c["Money"] = 100;
            c["EconomyRevision"] = revision;
            return c;
        };

        record["Characters"].push_back(make("CLEAN-1", 0));
        record["Characters"].push_back(make("DIRTY-1", 5));
        record["Characters"].push_back(make("CLEAN-2", 0));
        record["Characters"].push_back(make("DIRTY-2", 9));
        doc.push_back(record);

        const auto found = InspectLegacyMetadata(doc);

        Check(found.Nonzero, "a mixed file is flagged");
        Check(found.Characters.size() == 2, "with exactly the affected characters");
        Check(found.Characters[0] == "DIRTY-1" && found.Characters[1] == "DIRTY-2",
              "named individually, in file order");
    }

    // ------------------------------------------------------------------ malformed input ----
    //
    // The scan runs on every load, including a load of something that is not a players.json.
    // Reporting a parse opinion is PlayerStore's job; this must simply not fall over.
    {
        Check(!InspectLegacyMetadata(nlohmann::json::object()).Present, "an object is not scanned");
        Check(!InspectLegacyMetadata(nlohmann::json::array()).Present, "an empty array is fine");
        Check(!InspectLegacyMetadata(nlohmann::json(42)).Present, "a bare number is fine");

        nlohmann::json odd = nlohmann::json::array();
        odd.push_back("not a record");
        odd.push_back(nlohmann::json::object({{"Characters", "not an array"}}));
        odd.push_back(nlohmann::json::object({{"Characters", nlohmann::json::array({7})}}));
        Check(!InspectLegacyMetadata(odd).Present, "and neither are wrongly-shaped entries");
    }

    { // a legacy key holding something that is not a number
        const auto doc = Document({{"EconomyRevision", "seven"}});
        const auto found = InspectLegacyMetadata(doc);

        Check(found.Present, "a non-numeric legacy key is still noticed as present");
        Check(!found.Nonzero,
              "but not flagged as nonzero - a string is not a migration mark, and the record "
              "already fails to load for a reason the store reports");
    }

    { // a character with no id still gets reported rather than skipped
        nlohmann::json c;
        c["Name"] = "Nameless";
        c["Money"] = 1;
        c["MigratedAt"] = 1690000000;

        nlohmann::json record;
        record["DiscordId"] = "111";
        record["Characters"] = nlohmann::json::array({c});

        const auto found = InspectLegacyMetadata(nlohmann::json::array({record}));

        Check(found.Nonzero, "an id-less character with legacy metadata is still flagged");
        Check(found.Characters.size() == 1 && found.Characters[0] == "(unnamed)",
              "and reported under a placeholder rather than dropped");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
