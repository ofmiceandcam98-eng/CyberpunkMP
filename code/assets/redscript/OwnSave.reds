module CyberpunkMP

import CyberpunkMP.World.*

/*
 * Load the player's OWN character, not the world template.
 *
 * THE BUG. MULTIPLAYER called LoadLastCheckpoint(false), which loads save index 0 - the
 * most recent save, whatever it happens to be. The game's own QuickLoad proves the
 * equivalence (singleplayerMenu.script:1012):
 *
 *     if (m_isModded) LoadModdedSave(0) else LoadLastCheckpoint(false)
 *
 * The launcher installs the world template into the saves folder as MultiplayerStart. Any
 * time that file is the newest, index 0 IS the template, and the player loads as Phantom
 * Veronica instead of themselves. When one of their own autosaves is newer, they load as
 * themselves. That is why this has been maddeningly inconsistent rather than simply broken
 * - on 28 August the template was written at 17:12 and an autosave at 17:22, and which one
 * you got depended purely on file order.
 *
 * Cam remembered this working before the template existed. He was right: it did, because
 * back then index 0 was always one of his own saves.
 *
 * THE FIX. Ask for the save list, skip the template by name, and load the newest save that
 * is actually the player's. The template is loaded only when they have nothing of their own
 * - a first-time player with no singleplayer save at all, who genuinely needs a world to
 * stand in.
 *
 * WHY NOT DELETE OR HIDE THE TEMPLATE. It is a legitimate fallback and it is what the
 * launcher installs; making the load path choose correctly is smaller and reversible, and
 * it does not touch anybody's saves. Nothing here writes or deletes a save.
 */

// The launcher's own name for the template - see TEMPLATE_SAVE_NAME in launcher-lite/main.js.
// Matched by name because that is what the save list gives us.
public func MpTemplateSaveName() -> String = "MultiplayerStart"

@addField(SingleplayerMenuGameController)
public let m_mpOwnSaveArmed: Bool;

/*
 * Asks for the save list, then loads the player's own newest save.
 *
 * The list arrives asynchronously through OnSavesForLoadReady, so this cannot simply return
 * an answer; it arms the callback and lets that finish the job.
 */
@addMethod(SingleplayerMenuGameController)
public func MpLoadOwnCharacterSave() -> Void {
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
        network.ScriptLog("[OwnSave] asked for the save list - looking for this player's own character");
    }
}

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

    // The list is ordered newest first - index 0 is what LoadLastCheckpoint would have
    // taken. So the first entry that is not the template IS the player's most recent
    // character, and no timestamps need comparing.
    while i < ArraySize(saves) {
        if IsDefined(network) {
            network.ScriptLog(s"[OwnSave]   save[\(i)] = '\(saves[i])'");
        }

        if chosen < 0 && !StrContains(saves[i], template) {
            chosen = i;
        }

        i += 1;
    }

    if chosen < 0 {
        // Nothing but the template. A player with no save of their own genuinely needs it,
        // so this is the one case where loading it is correct.
        if IsDefined(network) {
            network.ScriptLog("[OwnSave] no save of their own - loading the template, which is what it is for");
        }

        this.GetSystemRequestsHandler().LoadLastCheckpoint(false);
        return false;
    }

    if IsDefined(network) {
        network.ScriptLog(s"[OwnSave] loading '\(saves[chosen])' at index \(chosen) - NOT the template");
    }

    // LoadModdedSave, not LoadSaveInGame. With this mod installed every save the player
    // makes is a modded save, and the game's own QuickLoad takes exactly this branch
    // (singleplayerMenu.script:1012).
    this.LoadModdedSave(chosen);
    return false;
}
