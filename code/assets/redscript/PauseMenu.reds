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
@wrapMethod(gameuiMenuItemListGameController)
protected func AddMenuItem(label: script_ref<String>, spawnEvent: CName) -> Void {
    if Equals(spawnEvent, n"OnSwitchToLoadGame") {
        let system = GameInstance.GetNetworkWorldSystem();

        if IsDefined(system) && system.IsConnected() {
            FTLog(s"[PauseMenu] connected to a server - not offering Load Game");
            return;
        }
    }

    wrappedMethod(label, spawnEvent);
}
