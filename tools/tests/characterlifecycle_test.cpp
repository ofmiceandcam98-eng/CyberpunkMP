// DELETING A CHARACTER REMOVES EXACTLY THE ONE YOU AIMED AT. IT DOES NOT TOUCH THE OTHERS,
// AND IT DOES NOT DESTROY WHAT IT REMOVES.
//
// zeldfep, 2026-09-14: "lets lock them as permanents and stop breaking this specific part
// we've looped like 5 times over this exact fix" - "specifically for the testing suite as
// well."
//
// The loop this test ends: delete kept coming back broken. Across five rebuilds it failed in
// five different places - a slot-less DeleteCharacter that hit whatever the active slot
// happened to be, a UI that never refreshed, a server that refused because the puppet was
// still alive - and each fix was real but none of them was WRITTEN DOWN as a guarantee, so the
// next change to the neighbourhood quietly reintroduced a cousin of the same bug. The handler
// and the redscript are hard to unit-test; the STORE primitive underneath them is not, and it
// is where "delete slot N" either means slot N or means something else.
//
// So this is the guarantee, not the mechanism. RetireCharacter(id, slot) is what a delete
// resolves to. The sentences a regression has to break:
//
//   * retiring slot N removes slot N and NOTHING ELSE - the sibling characters are still there,
//     still addressable by their own slot numbers (the original bug wrote/read a hardcoded or
//     active slot, so deleting "a" character deleted the wrong one);
//   * a deleted character is RETIRED, not destroyed - it moves to RetiredCharacters so it can
//     be recovered, and a future "just erase it" is a different, worse bug;
//   * slots are keyed on their Slot number, never their position in the list - after deleting
//     the middle of three, the survivors do NOT renumber to 0,1, across a disk reload either;
//   * the freed slot is reusable without collision - create lands in the hole and adds a
//     character; it does not resurrect or duplicate the retired one;
//   * deleting a slot that holds nothing is a no-op that changes nothing and says so (false).
//
// Compiling the real PlayerStore needs glm, nlohmann and spdlog, which Verify.ps1 passes
// through - the same arrangement characterslots_test and playerstore_migration_test use.

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>

// PlayerStore.h opens files without including <fstream> itself, so every test that compiles
// it has to supply one.
#include <fstream>
#include <sstream>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "PlayerStore.h"

static int failures = 0;

static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c)
        ++failures;
}

static CharacterRecord MakeCharacter(int aSlot, const char* acName)
{
    CharacterRecord character;
    character.Slot = aSlot;
    character.Name = acName;
    character.Appearance = "AAAA";
    return character;
}

static bool HasCharacterNamed(const PlayerStore& acStore, const std::string& acDiscordId,
                              int aSlot, const char* acName)
{
    const auto* pCharacter = acStore.FindCharacter(acDiscordId, aSlot);
    return pCharacter && pCharacter->Name == acName;
}

// Is a name present in the RETIRED list (a delete moved it there rather than dropping it)?
static bool IsRetired(const PlayerStore& acStore, const std::string& acDiscordId,
                      const char* acName)
{
    const auto* pRecord = acStore.Find(acDiscordId);
    if (!pRecord)
        return false;

    for (const auto& character : pRecord->RetiredCharacters)
        if (character.Name == acName)
            return true;

    return false;
}

static size_t ActiveCount(const PlayerStore& acStore, const std::string& acDiscordId)
{
    const auto* pCharacters = acStore.GetCharacters(acDiscordId);
    return pCharacters ? pCharacters->size() : 0;
}

int main()
{
    const auto dir = std::filesystem::temp_directory_path() / "nco-lifecycle-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    // ------------------------------------------------------ delete is slot-precise
    //
    // Three characters in slots 0/1/2. Delete the MIDDLE one. The bug always showed itself as
    // "the wrong character disappeared", so the whole assertion is about who is left.
    {
        const auto path = dir / "precise.json";

        PlayerStore store;
        store.Load(path);

        store.SaveCharacter("42", "cam", MakeCharacter(0, "Anders"));
        store.SaveCharacter("42", "cam", MakeCharacter(1, "Briar"));
        store.SaveCharacter("42", "cam", MakeCharacter(2, "Cyrus"));
        Check(ActiveCount(store, "42") == 3, "three characters go in");

        Check(store.RetireCharacter("42", 1), "deleting slot 1 succeeds");

        Check(!store.FindCharacter("42", 1), "slot 1 is empty afterwards");
        Check(HasCharacterNamed(store, "42", 0, "Anders"),
              "slot 0 is untouched - deleting the middle did not take the first");
        Check(HasCharacterNamed(store, "42", 2, "Cyrus"),
              "slot 2 is untouched - deleting the middle did not take the last");
        Check(ActiveCount(store, "42") == 2, "exactly one character was removed, not more");

        // A delete is recoverable. It retires the character; it does not erase it. A change
        // that "cleans up" by dropping the record instead would pass every slot check above
        // and still be wrong.
        Check(IsRetired(store, "42", "Briar"),
              "the deleted character is RETIRED, not destroyed");
        Check(!IsRetired(store, "42", "Anders") && !IsRetired(store, "42", "Cyrus"),
              "and only the deleted one was retired");
    }

    // ------------------------------------------------ slots are numbers, not positions
    //
    // After deleting the middle, the survivors keep their own slot numbers - they do NOT
    // renumber to 0,1. This is the property that makes "delete slot N" mean the same thing
    // before and after any other delete. It has to survive a round-trip through disk, because
    // a load that re-packs the list would be the same bug wearing a JSON hat.
    {
        const auto path = dir / "noncontiguous.json";

        {
            PlayerStore store;
            store.Load(path);
            store.SaveCharacter("55", "cam", MakeCharacter(0, "Zero"));
            store.SaveCharacter("55", "cam", MakeCharacter(1, "One"));
            store.SaveCharacter("55", "cam", MakeCharacter(2, "Two"));
            Check(store.RetireCharacter("55", 1), "delete the middle of three");
        }

        PlayerStore reloaded;
        reloaded.Load(path);

        Check(HasCharacterNamed(reloaded, "55", 0, "Zero"),
              "slot 0 is still slot 0 after a reload");
        Check(!reloaded.FindCharacter("55", 1),
              "slot 1 is still the hole after a reload - survivors did not renumber into it");
        Check(HasCharacterNamed(reloaded, "55", 2, "Two"),
              "slot 2 is still slot 2 after a reload - it did not slide down to 1");
        Check(IsRetired(reloaded, "55", "One"),
              "the retired character survives the reload too");
    }

    // ------------------------------------------------ the freed slot is reusable, cleanly
    //
    // Delete opens a hole; the next create fills exactly that hole and ADDS a character. It
    // must not resurrect the retired one, and it must not duplicate it.
    {
        const auto path = dir / "reuse.json";

        PlayerStore store;
        store.Load(path);
        store.SaveCharacter("77", "cam", MakeCharacter(0, "Keeper"));
        store.SaveCharacter("77", "cam", MakeCharacter(1, "Doomed"));
        store.SaveCharacter("77", "cam", MakeCharacter(2, "AlsoKeeper"));

        Check(store.RetireCharacter("77", 1), "delete slot 1 to open a hole");
        Check(store.FirstFreeSlot("77", EPermissionLevel::kAdmin) == 1,
              "the freed slot is the one offered next - not the end of the list");

        store.SaveCharacter("77", "cam", MakeCharacter(1, "Replacement"));
        Check(HasCharacterNamed(store, "77", 1, "Replacement"),
              "the new character lands in the freed slot");
        Check(ActiveCount(store, "77") == 3, "and the account is back to three, not four");
        Check(IsRetired(store, "77", "Doomed") && !HasCharacterNamed(store, "77", 1, "Doomed"),
              "the retired character stayed retired - reuse did not resurrect it");
    }

    // ------------------------------------------------ deleting nothing is a quiet no-op
    //
    // A delete aimed at a slot that holds nothing must change NOTHING and report that it did
    // nothing - the client tells the difference between "gone" and "there was nothing to
    // delete" from this bool.
    {
        const auto path = dir / "empty.json";

        PlayerStore store;
        store.Load(path);
        store.SaveCharacter("88", "cam", MakeCharacter(0, "Only"));

        Check(!store.RetireCharacter("88", 2), "deleting an empty slot returns false");
        Check(HasCharacterNamed(store, "88", 0, "Only"),
              "and the character that IS there is untouched");
        Check(ActiveCount(store, "88") == 1, "nothing was removed");
        Check(!store.RetireCharacter("does-not-exist", 0),
              "deleting from an account that does not exist is false, not a crash");
    }

    std::filesystem::remove_all(dir);

    std::printf("\n%s\n", failures == 0 ? "characterlifecycle_test: all checks passed"
                                        : "characterlifecycle_test: FAILURES");
    return failures == 0 ? 0 : 1;
}
