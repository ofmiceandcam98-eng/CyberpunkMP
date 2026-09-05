module CyberpunkMP

import CyberpunkMP.World.*

/*
 * Load the WORLD TEMPLATE, always. The server is the only source of who you are.
 *
 * WHAT A SAVE IS FOR HERE, and the sentence that settles it. MainMenu.reds calls the load
 * "purely as a vehicle into the world" - the mod needs somewhere to stand, nothing more.
 * But a save does not only carry a world; it carries an IDENTITY, and that identity is the
 * part this mod then has to fight: strip the template's inventory, override the appearance,
 * refuse a save that would flip an established character's body. Every one of those guards
 * exists to undo something a save supplied and nobody asked for.
 *
 * THE BUG THIS REPLACES (ledger fault A). This file used to hunt for "the player's own
 * save" by taking the newest entry that was not the template. That is not an identity, it
 * is an accident of file order, and it picked whatever happened to be newest: another test
 * character, a singleplayer session, a probe run. On 2026-09-01 it loaded `AutoSave-12` - a
 * throwaway female Corpo from a probe two days earlier - which is the whole of "the
 * character we created would not be the character we play as, it is also not phantom
 * veronica". A third person entirely, because the newest file happened to be theirs.
 *
 * WHY THE FIX IS NOT "NAME THE SAVE AFTER THE CHARACTER". That was the plan of record, and
 * it needs the mod to WRITE saves. `343b912` closed that door hours before this was
 * written, deliberately: SaveLocksManager is held for the whole launcher session because a
 * local save is a second copy of a server-owned character, and "save with the money, spend
 * it, load, spend it again" is the exploit that follows. Naming saves per character would
 * have punched a hole in that rule to solve a problem that has a cheaper answer.
 *
 * THE ANSWER: REMOVE THE CHOICE. Every session loads the same known world. There is no file
 * order to be at the mercy of, no newest-save race, and nothing to name. Identity arrives
 * from the server, which has been the design the whole time - `HasCharacter()`,
 * `GetCharacterName()` and the appearance restore already run before this does. The
 * template stops being a fallback for people with no save and becomes what it always
 * should have been: the world everyone stands in.
 *
 * WHAT THIS COSTS, honestly. A returning player's own singleplayer world progress no longer
 * comes into a multiplayer session. That progress was never consulted by anything - the
 * server owns position, possessions, money and world facts - and it could not have advanced
 * during a session anyway, because saving is locked. What it buys is that every player is
 * standing in the same world, which is what a shared server wanted in the first place.
 *
 * WHAT IS STILL NOT SOLVED HERE. The template carries Phantom Veronica's identity, and that
 * bleed is real - her inventory arriving ~88s in, her appearance competing with the stored
 * one. This file does not fix that and does not pretend to; `MpStarterSettlement`
 * (Inventory.reds) and the appearance restore are the two fixes already in flight for it.
 * What changes here is that the bleed now comes from ONE known source on every machine
 * instead of from whichever save a player happened to have, which is the difference between
 * a bug you can reproduce and a bug that looks like a coin flip.
 *
 * NOTHING HERE WRITES OR DELETES A SAVE.
 */

// The launcher's own name for the template - see TEMPLATE_SAVE_NAME in launcher-lite/main.js.
// Matched by name because that is what the save list gives us.
public func MpTemplateSaveName() -> String = "MultiplayerStart"

// Field name predates the rename below and is left alone on purpose: it is only ever set
// and read in this file, and a rename is a compile risk that buys nothing.
@addField(SingleplayerMenuGameController)
public let m_mpOwnSaveArmed: Bool;

/*
 * Asks for the save list, then loads the world template out of it.
 *
 * The list arrives asynchronously through OnSavesForLoadReady, so this cannot simply return
 * an answer; it arms the callback and lets that finish the job.
 */
@addMethod(SingleplayerMenuGameController)
public func MpLoadMultiplayerWorld() -> Void {
    let network = GameInstance.GetNetworkWorldSystem();
    let handler = this.GetSystemRequestsHandler();

    if !IsDefined(handler) {
        if IsDefined(network) {
            network.ScriptLog("[OwnSave] no system requests handler - falling back to LoadLastCheckpoint");
        }
        this.GetSystemRequestsHandler().LoadLastCheckpoint(false);
        return;
    }

    this.m_mpOwnSaveArmed = true;
    handler.RegisterToCallback(n"OnSavesForLoadReady", this, n"MpOnSavesReady");
    handler.RequestSavesForLoad();

    if IsDefined(network) {
        network.ScriptLog("[OwnSave] asked for the save list - loading the multiplayer world template");
    }
}

// Callback name is bound as a string in RegisterToCallback above - it must keep matching
// this method exactly, so this one does not get renamed.
@addMethod(SingleplayerMenuGameController)
protected cb func MpOnSavesReady(saves: array<String>) -> Bool {
    let network = GameInstance.GetNetworkWorldSystem();

    // Fires for the game's own load menu too. Without this guard, opening Load Game would
    // be hijacked into a multiplayer load.
    if !this.m_mpOwnSaveArmed {
        return false;
    }

    this.m_mpOwnSaveArmed = false;

    let template = MpTemplateSaveName();
    let chosen = -1;
    let i = 0;

    // Newest first, so the FIRST template entry is the current one. Matching by name rather
    // than by position is the entire point: position is what made this a coin flip.
    while i < ArraySize(saves) {
        if IsDefined(network) {
            network.ScriptLog(s"[OwnSave]   save[\(i)] = '\(saves[i])'");
        }

        if chosen < 0 && StrContains(saves[i], template) {
            chosen = i;
        }

        i += 1;
    }

    if chosen < 0 {
        // The launcher installs the template, so this means it was deleted, renamed, or
        // never written. Loud, because the fallback is the exact arbitrary behaviour this
        // file exists to remove - but the player still needs a world, and refusing to load
        // anything would mean refusing to let them play at all.
        if IsDefined(network) {
            network.ScriptLog("[OwnSave] NO TEMPLATE IN THE SAVE LIST - falling back to newest save, which may be any character. Reinstall through the launcher to restore it.");
        }

        this.GetSystemRequestsHandler().LoadLastCheckpoint(false);
        return false;
    }

    if IsDefined(network) {
        network.ScriptLog(s"[OwnSave] loading the world template '\(saves[chosen])' at index \(chosen) - identity comes from the server");
    }

    // LoadModdedSave, not LoadSaveInGame. With this mod installed every save the player
    // makes is a modded save, and the game's own QuickLoad takes exactly this branch
    // (singleplayerMenu.script:1012).
    this.LoadModdedSave(chosen);
    return false;
}
