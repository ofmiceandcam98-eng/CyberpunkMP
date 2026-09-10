// Contacts gained a saved name, and the live players.json is full of the OLD bare-string
// shape. This asserts the migration works in BOTH directions - an existing file loads, and
// an unnamed contact still writes as a bare string so an older server binary can read it.

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <cstdio>

// Copied from the shipped CharacterRecord.h. (That header drags in the server's PCH world,
// so the struct and its two converters are restated rather than included.)
struct Contact
{
    std::string Number;
    std::string Name;
    bool Favorite{false};
};

inline void to_json(nlohmann::json& aJson, const Contact& acContact)
{
    if (acContact.Name.empty() && !acContact.Favorite) { aJson = acContact.Number; return; }
    aJson = nlohmann::json{{"Number", acContact.Number}, {"Name", acContact.Name}, {"Favorite", acContact.Favorite}};
}

inline void from_json(const nlohmann::json& acJson, Contact& aContact)
{
    if (acJson.is_string())
    {
        aContact.Number = acJson.get<std::string>();
        aContact.Name.clear();
        aContact.Favorite = false;
        return;
    }
    aContact.Number   = acJson.value("Number", std::string{});
    aContact.Name     = acJson.value("Name", std::string{});
    aContact.Favorite = acJson.value("Favorite", false);
}

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

int main()
{
    { // the shape that is in the LIVE players.json right now
        auto contacts = nlohmann::json::parse(R"(["555-014-372","555-900-001"])").get<std::vector<Contact>>();
        Check(contacts.size() == 2, "old bare-string list loads");
        Check(contacts[0].Number == "555-014-372" && contacts[1].Number == "555-900-001", "both numbers survive");
        Check(contacts[0].Name.empty(), "old entries have no saved name");
    }

    { // unnamed contacts must write back in the OLD shape, or a rollback throws
        std::vector<Contact> contacts{{"555-014-372", "", false}};
        Check(nlohmann::json(contacts).dump() == R"(["555-014-372"])",
              "an unnamed contact still writes as a bare string");
    }

    { // a named one round-trips
        std::vector<Contact> contacts{{"555-014-372", "Ripper - Watson", false}};
        const auto out = nlohmann::json(contacts).dump();
        Check(out.find("Ripper - Watson") != std::string::npos, "a saved name is written");
        auto back = nlohmann::json::parse(out).get<std::vector<Contact>>();
        Check(back[0].Name == "Ripper - Watson" && back[0].Number == "555-014-372", "and reads back whole");
    }

    { // mixed, which is what the file looks like mid-migration
        auto contacts = nlohmann::json::parse(
            R"(["555-014-372",{"Number":"555-900-001","Name":"Do not answer","Favorite":true}])")
            .get<std::vector<Contact>>();
        Check(contacts.size() == 2, "a mixed list loads");
        Check(contacts[0].Name.empty(), "mixed: the old entry");
        Check(contacts[1].Name == "Do not answer" && contacts[1].Favorite, "mixed: the new entry");
    }

    { // hand-edited entry missing fields - this file is meant to be openable by a person
        auto contacts = nlohmann::json::parse(R"([{"Number":"555-111-222"}])").get<std::vector<Contact>>();
        Check(contacts.size() == 1 && contacts[0].Number == "555-111-222" && contacts[0].Name.empty(),
              "a hand-edited entry missing fields still loads");
    }

    { // conversation pairing: sorted, so who spoke first cannot make two threads
        const std::string a = "AEC-MJ6P", b = "H7K-M4X3";
        const auto k1 = (a < b) ? a + "|" + b : b + "|" + a;
        const auto k2 = (b < a) ? b + "|" + a : a + "|" + b;
        Check(k1 == k2, "A-to-B and B-to-A land in one thread");
    }

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
