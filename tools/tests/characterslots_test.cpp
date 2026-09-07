// CREATING A CHARACTER ADDS ONE. IT DOES NOT REPLACE ONE.
//
// zeldfep, 2026-09-07: "the new player deletes the new character created", and "new character
// (replaces yours) should not be a thing anymore".
//
// The bug was one line - HandleSaveCharacterRequest wrote `character.Slot = 0` on every
// appearance save, so a character created while pointed anywhere else landed on slot 0 and
// overwrote whoever was there. Nothing caught it, and nothing WOULD have: it compiled, every
// existing test passed, and with one character per account the behaviour is indistinguishable
// from correct. It only became visible once slots existed and somebody made a second
// character and lost their first.
//
// So this test is the guarantee itself, not the mechanism: after creating into a free slot,
// the character that was already there is STILL THERE. That is the sentence a regression has
// to break, and it is the one thing neither the compiler nor the redscript gate can check.
//
// The entitlement rules ride along, because "public gets one, staff get four, three more are
// purchasable" is a permission, and a permission that silently widens is a different class of
// bug than one that silently narrows.
//
// Compiling the real PlayerStore needs glm, nlohmann and spdlog, which Verify.ps1 passes
// through - the same arrangement playerstore_migration_test uses.

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>

// PlayerStore.h opens files without including <fstream> itself, so every test that compiles
// it has to supply one - the same reason playerstore_migration_test carries these.
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

// A character with just enough on it to be stored and told apart from the others.
static CharacterRecord MakeCharacter(int aSlot, const char* acName)
{
    CharacterRecord character;
    character.Slot = aSlot;
    character.Name = acName;

    // A plausible blob. The store does not validate it - that is the request handler's job -
    // but an empty one would make "did this character survive" ambiguous.
    character.Appearance = "AAAA";

    return character;
}

static bool HasCharacterNamed(const PlayerStore& acStore, const std::string& acDiscordId,
                              int aSlot, const char* acName)
{
    const auto* pCharacter = acStore.FindCharacter(acDiscordId, aSlot);
    return pCharacter && pCharacter->Name == acName;
}

int main()
{
    const auto dir = std::filesystem::temp_directory_path() / "nco-slots-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const auto path = dir / "players.json";

    // ---------------------------------------------------------------- entitlements
    {
        Check(PlayerStore::SlotsForLevel(EPermissionLevel::kPlayer) == 1,
              "a public account is entitled to one slot");
        Check(PlayerStore::SlotsForLevel(EPermissionLevel::kSupport) == 4,
              "support gets four - the lowest staff rung, and the only check it satisfies");
        Check(PlayerStore::SlotsForLevel(EPermissionLevel::kModerator) == 4,
              "a moderator gets four");
        Check(PlayerStore::SlotsForLevel(EPermissionLevel::kAdmin) == 4,
              "an admin gets four");
        Check(PlayerStore::SlotsForLevel(EPermissionLevel::kOwner) == 4,
              "the owner gets four");

        // The ceiling is what the SCREEN draws, so an entitlement above it would be slots
        // nothing could ever show. The header static_asserts this too; saying it here means a
        // failure names the rule rather than a line number in a header.
        Check(PlayerStore::kStaffSlots <= PlayerStore::kMaxSlots,
              "no entitlement exceeds the ceiling the selection screen draws");
        Check(PlayerStore::kMaxSlots == 4,
              "four slots exist in total - one unlocked for the public, three purchasable");
    }

    // ---------------------------------------------------------------- first free slot
    {
        PlayerStore store;
        store.Load(path);

        Check(store.FirstFreeSlot("1", EPermissionLevel::kPlayer) == 0,
              "an account with no record at all is free at slot 0");

        store.SaveCharacter("1", "zeldfep", MakeCharacter(0, "First"));

        Check(store.FirstFreeSlot("1", EPermissionLevel::kPlayer) == -1,
              "a PUBLIC account with one character has no room - its other three are locked");
        Check(store.FirstFreeSlot("1", EPermissionLevel::kAdmin) == 1,
              "the same account as staff is free at slot 1");

        store.SaveCharacter("1", "zeldfep", MakeCharacter(1, "Second"));
        store.SaveCharacter("1", "zeldfep", MakeCharacter(3, "Fourth"));

        Check(store.FirstFreeSlot("1", EPermissionLevel::kAdmin) == 2,
              "the LOWEST free slot, not the next one up - slots are not filled in order");
    }

    // ---------------------------------------------------------------- selecting a slot
    {
        PlayerStore store;
        store.Load(path);   // holds slots 0, 1 and 3 from the block above

        // THE LINE THAT MAKES ADDING POSSIBLE. This used to return "empty_slot", which is why
        // creating a character could only ever land on one that already existed.
        Check(store.SelectSlot("1", 2, EPermissionLevel::kAdmin).empty(),
              "an EMPTY slot is a valid destination - this is what makes creation an addition");
        Check(store.ActiveSlotFor("1") == 2,
              "and the account is now pointed at it");

        Check(store.SelectSlot("1", 0, EPermissionLevel::kAdmin).empty(),
              "an occupied slot still selects");
        Check(store.ActiveSlotFor("1") == 0, "and the pointer follows");

        // A locked slot is a refusal about PERMISSION, and it carries its own code so the
        // client can say "not yours yet" rather than "that failed".
        Check(store.SelectSlot("1", 1, EPermissionLevel::kPlayer) == "slot_locked",
              "a public account cannot select slot 1 - it is locked, not broken");
        Check(store.ActiveSlotFor("1") == 0,
              "a refused selection does not move the pointer");

        Check(store.SelectSlot("1", PlayerStore::kMaxSlots, EPermissionLevel::kOwner)
                  == "slot_out_of_range",
              "nobody can select past the ceiling, not even the owner");
        Check(store.SelectSlot("1", -1, EPermissionLevel::kOwner) == "slot_out_of_range",
              "a negative slot is out of range rather than a wraparound");
    }

    // ---------------------------------------------------------------- THE GUARANTEE
    //
    // The whole point, end to end: point at a free slot, create there, and find BOTH
    // characters afterwards. Written the way the bug happened - the old code wrote slot 0
    // regardless, so this block would have ended with one character called "Newcomer" and
    // "Original" gone.
    {
        const auto fresh = dir / "guarantee.json";

        PlayerStore store;
        store.Load(fresh);

        store.SaveCharacter("7", "cam", MakeCharacter(0, "Original"));
        Check(HasCharacterNamed(store, "7", 0, "Original"), "the first character is stored");

        const int free = store.FirstFreeSlot("7", EPermissionLevel::kAdmin);
        Check(free == 1, "the next free slot is 1");

        Check(store.SelectSlot("7", free, EPermissionLevel::kAdmin).empty(),
              "creating points the account at the free slot first");

        // Exactly what the request handler now does: the save lands in the ACTIVE slot rather
        // than at a hardcoded zero.
        store.SaveCharacter("7", "cam", MakeCharacter(store.ActiveSlotFor("7"), "Newcomer"));

        Check(HasCharacterNamed(store, "7", 1, "Newcomer"),
              "the new character is in the slot it was aimed at");
        Check(HasCharacterNamed(store, "7", 0, "Original"),
              "AND THE ORIGINAL IS STILL THERE - creating adds, it does not replace");

        // Editing must keep its own slot too. The same hardcoded zero meant a ripperdoc visit
        // by the slot-1 character rewrote slot 0, silently destroying a character the player
        // was not even looking at.
        auto edited = MakeCharacter(store.ActiveSlotFor("7"), "Newcomer");
        edited.Appearance = "BBBB";
        store.SaveCharacter("7", "cam", edited);

        Check(HasCharacterNamed(store, "7", 0, "Original"),
              "an edit in slot 1 leaves slot 0 alone");

        const auto* pEdited = store.FindCharacter("7", 1);
        Check(pEdited && pEdited->Appearance == "BBBB", "and the edit applied to slot 1");

        // Reload from disk. Everything above went through Flush, so a slot that only exists
        // in memory would be a different bug wearing the same face.
        PlayerStore reloaded;
        reloaded.Load(fresh);

        Check(HasCharacterNamed(reloaded, "7", 0, "Original")
                  && HasCharacterNamed(reloaded, "7", 1, "Newcomer"),
              "both characters survive a reload from disk");
        Check(reloaded.ActiveSlotFor("7") == 1, "and the account is still pointed at slot 1");
    }

    // ---------------------------------------------------------------- a full account
    {
        const auto full = dir / "full.json";

        PlayerStore store;
        store.Load(full);

        for (int slot = 0; slot < PlayerStore::kStaffSlots; ++slot)
            store.SaveCharacter("9", "staff", MakeCharacter(slot, "Someone"));

        Check(store.FirstFreeSlot("9", EPermissionLevel::kOwner) == -1,
              "four characters fill a staff account");

        // -1 is what the client tests to say "every slot you have is full, delete one first"
        // rather than quietly replacing somebody - which is the behaviour being removed.
        Check(store.FirstFreeSlot("9", EPermissionLevel::kPlayer) == -1,
              "and it is still full if that account is demoted to the public entitlement");
    }

    std::filesystem::remove_all(dir);

    std::printf("\n%s\n", failures == 0 ? "characterslots_test: all checks passed"
                                        : "characterslots_test: FAILURES");
    return failures == 0 ? 0 : 1;
}
