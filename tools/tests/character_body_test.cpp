// Body type is chosen once. Security-relevant in the same sense as the staff ladder:
// every check here is "can this save overwrite who somebody is".
//
// Tests the SHIPPED CharacterRecord.h. The rule it protects came from a live failure on
// 2026-09-04: a capture reporting the wrong body was stored and then broadcast, so one
// player appeared to everybody else as a completely different character - female corpo V
// in prologue clothes - while insisting they had picked male. Cyberpunk fixes body type at
// creation and offers no way to change it, so a save that flips an ESTABLISHED character's
// body is never a player's decision; it is a capture of the wrong thing.
//
// The two halves matter equally: refuse the flip, and never block a character being made.

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// Before CharacterRecord.h, deliberately: that header uses nlohmann::json for its
// serialisation macros without including it, relying on its .cpp to have done so first.
// Verify.ps1 passes the package's include path when it can find it, so this works here
// and fails loudly (rather than silently skipping the rule) if the package moves.
#include <nlohmann/json.hpp>

#include "CharacterRecord.h"

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

// A character as stored once somebody has made it and played it.
static CharacterRecord Established(const bool aIsMale)
{
    CharacterRecord r{};
    r.IsMale = aIsMale;
    r.Appearance = "some-base64-appearance-blob";
    r.SpawnedBefore = true;
    return r;
}

int main()
{
    using CR = CharacterRecord;

    // --- the flip is refused, both directions -------------------------------------
    const auto male = Established(true);
    const auto female = Established(false);

    Check(CR::WouldFlipEstablishedBody(&male, false),
          "an established male character cannot be saved as female");
    Check(CR::WouldFlipEstablishedBody(&female, true),
          "an established female character cannot be saved as male");

    // --- the same body is always allowed through ----------------------------------
    Check(!CR::WouldFlipEstablishedBody(&male, true),
          "saving an established male character as male is allowed (a face edit)");
    Check(!CR::WouldFlipEstablishedBody(&female, false),
          "saving an established female character as female is allowed (a face edit)");

    // --- making a character must NEVER be blocked ---------------------------------
    //
    // This is the half that would turn a guard into an outage. Each of these is a
    // character that is still being made, and every one of them may set its body.
    Check(!CR::WouldFlipEstablishedBody(nullptr, true),
          "a first-ever save has nothing to contradict (no record)");
    Check(!CR::WouldFlipEstablishedBody(nullptr, false),
          "a first-ever save may be female too");

    CharacterRecord neverPlayed = Established(true);
    neverPlayed.SpawnedBefore = false;
    Check(!CR::WouldFlipEstablishedBody(&neverPlayed, false),
          "a character that has never spawned may still change body (mid-creation)");

    CharacterRecord noAppearance = Established(true);
    noAppearance.Appearance.clear();
    Check(!CR::WouldFlipEstablishedBody(&noAppearance, false),
          "a record with no appearance yet may set any body (nothing to protect)");

    CharacterRecord fresh{};
    Check(!CR::WouldFlipEstablishedBody(&fresh, false),
          "a default-constructed record is not established, so it blocks nothing");

    // --- IsEstablished means both conditions, not either --------------------------
    Check(!fresh.IsEstablished(), "a fresh record is not established");
    Check(!neverPlayed.IsEstablished(), "appearance without having played is not established");
    Check(!noAppearance.IsEstablished(), "having played without an appearance is not established");
    Check(male.IsEstablished(), "appearance plus having played IS established");

    std::printf("\n%s\n", failures == 0 ? "character body: all checks passed"
                                        : "character body: FAILURES ABOVE");
    return failures == 0 ? 0 : 1;
}
