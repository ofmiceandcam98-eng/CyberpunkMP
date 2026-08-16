module CyberpunkMP

import CyberpunkMP.World.*

// No loading a save while you are on the server.
//
// Loading detaches and rebuilds the whole world. The server still holds the puppet from
// before, so everyone else is left looking at a frozen copy of someone who has already
// gone somewhere else - and the person who loaded is now in a singleplayer world from
// before any of the session happened. That is the desync Cam hit, and it takes both
// players out of the session, not just the one who pressed the button.
//
// The entry is removed rather than greyed out or refused on click. A disabled option
// still reads as something you are supposed to be able to use, and an option that shows
// an error when pressed is worse - it puts the explanation after the mistake.
//
// This hooks AddMenuItem rather than the pause menu itself, because that is the single
// point every menu entry passes through, and it means the item is never created at all
// instead of being created and then hunted down and deleted.
//
// Only 'OnSwitchToLoadGame' is affected - the pause menu's Load Game. The main menu uses
// 'OnLoadGame', which is untouched: you are not connected there, and it is how you get
// into the game in the first place.
// NOTE script_ref<String>, not String. The game declares this as
// `const label : ref< String >`, and that dialect's `ref` on a String is redscript's
// script_ref - getting it wrong fails the whole file with UNRESOLVED_METHOD, which takes
// every other script in the mod down with it.
// SAVING is removed for a different reason than loading.
//
// Loading is dangerous: it rebuilds the world and desyncs everyone. Saving is merely
// MISLEADING - a Cyberpunk save records a singleplayer game and knows nothing about the
// server, so a player who saves and expects that to preserve their session has been told
// something untrue by the menu. The server writes their position continuously and that is
// the only thing that survives.
//
// So the entry goes, for honesty rather than safety.
//
// Worth being clear about the limit: this removes the pause-menu entries. The QUICKSAVE
// hotkey and the game's own autosaves are native and cannot be blocked from redscript -
// checked against the 2.31 type hierarchy, which has no save system exposed to scripts at
// all. Those still write a file. That file is harmless: it only becomes a problem if it is
// LOADED, and loading is what is actually blocked above.
@wrapMethod(gameuiMenuItemListGameController)
protected func AddMenuItem(label: script_ref<String>, spawnEvent: CName) -> Void {
    let system = GameInstance.GetNetworkWorldSystem();
    let connected = IsDefined(system) && system.IsConnected();

    // Every menu item, everywhere, once per menu build.
    //
    // Menu event names are ink events and do not appear in the type dump, so the only way
    // to learn them is to watch them go past. This covers the NEW GAME menu too, which is
    // built before connecting and so was invisible to the connected-only logging below.
    FTLog(s"[Menu] item '\(NameToString(spawnEvent))'");

    if connected {
        // Every entry, once, so the exact names are on record rather than guessed at.
        // The save entry's event name is not in the type dump - it is an ink event - so if
        // the list below misses it, this line is what tells us what to add.
        FTLog(s"[PauseMenu] item: \(NameToString(spawnEvent))");

        if Equals(spawnEvent, n"OnSwitchToLoadGame") {
            FTLog(s"[PauseMenu] connected to a server - not offering Load Game");
            return;
        }

        // Several candidates, because the name could not be verified offline. Filtering a
        // CName that does not exist is harmless - nothing matches and the entry is left
        // alone - so guessing wide costs nothing and guessing narrow costs a round trip.
        if Equals(spawnEvent, n"OnSwitchToSaveGame")
            || Equals(spawnEvent, n"OnSaveGame")
            || Equals(spawnEvent, n"OnQuickSave")
            || Equals(spawnEvent, n"OnSwitchToSave") {
            FTLog(s"[PauseMenu] connected to a server - not offering Save Game");
            return;
        }
    }

    wrappedMethod(label, spawnEvent);
}

// REGULAR START is removed from the new-game screen.
//
// The first attempt at this filtered menu-item names on gameuiMenuItemListGameController,
// which was the wrong screen entirely. The choice in Cam's screenshot is not a menu list -
// it is ExpansionNewGame, a BaseCharacterCreationController with two bound buttons, and it
// never passes through AddMenuItem at all. The logging that replaced the guesses is what
// showed this: the only names that ever appeared were OnNewGame, OnLoadGame,
// OnSwitchToSettings, OnCreditsPicker and our own two. No standard-start event exists,
// because the standard start is a button on a later screen.
//
// Written against the game's own source, which is authoritative for the installed patch:
//   cyberpunk/UI/fullscreen/pregame/expansionNewGame.script
//     m_baseGameButton   -> REGULAR START            -> OnPressBaseGame
//     m_standaloneButton -> SKIP AHEAD TO PHANTOM LIBERTY -> OnPressExpansion
//
// A regular new game begins in the prologue - the whole of Act 1, which is exactly what
// the multiplayer start exists to skip. Phantom Liberty's start lands post-Act-1 at level
// 15, and the expansion is already required to play here at all, so nothing is lost.
@wrapMethod(ExpansionNewGame)
protected cb func OnInitialize() -> Bool {
    let result = wrappedMethod();

    FTLog(s"[Menu] new game screen - hiding REGULAR START");
    inkWidgetRef.SetVisible(this.m_baseGameButton, false);

    return result;
}

// Belt and braces: the button is hidden above, but a hidden widget can still be reachable
// by controller focus in some menus. If the press ever lands anyway, it is answered with
// the expansion start rather than being silently swallowed - a dead button is worse than
// no button, and this way the outcome is the one we want either way.
@replaceMethod(ExpansionNewGame)
protected cb func OnPressBaseGame(evt: ref<inkPointerEvent>) -> Bool {
    if evt.IsAction(n"click") && !this.m_isInputLocked {
        FTLog(s"[Menu] REGULAR START pressed - starting the Phantom Liberty one instead");
        this.m_characterCustomizationState.SetIsExpansionStandalone(true);
        this.PlaySound(n"Button", n"OnPress");
        this.NextMenu();
    }
}
