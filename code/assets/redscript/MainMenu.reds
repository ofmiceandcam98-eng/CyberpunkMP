// NetworkWorldSystem lives in module CyberpunkMP.World. This file has no module of its
// own, so without this import the type is simply not in scope and
// GameInstance.GetNetworkWorldSystem() fails to resolve - taking every other script in
// the mod down with it, because redscript aborts all compilation on one bad file.
import CyberpunkMP.World.*

// Adds a MULTIPLAYER entry to the main menu, beside Continue / New Game / Load Game.
//
// Written against the game's own script source, which ships at
// <game>\tools\redmod\scripts\ and is authoritative for the installed patch:
//
//   cyberpunk/UI/fullscreen/pregame/singleplayerMenu.script:843
//       SingleplayerMenuGameController.PopulateMenuItemList()
//   cyberpunk/UI/fullscreen/pregame/singleplayerMenu.script:1001
//       HandleMenuItemActivate() - where Continue actually loads a save
//   cyberpunk/UI/menus/menuItemListGameController.script:56
//       AddMenuItem(label, spawnEvent: CName) -> pushes PauseMenuListItemData
//
// Everything lives on the CONTROLLER on purpose. The first attempt put the handler on
// MenuScenario_SingleplayerMenu and failed to compile with UNRESOLVED_FN on
// GetSystemRequestsHandler - that is a method declared on widgetController
// (core/ui/baseControllers/widgetController.script:84), not a free function, so it only
// resolves on classes that inherit it. The controller does; the scenario does not.

@wrapMethod(SingleplayerMenuGameController)
private func PopulateMenuItemList() -> Void {
    wrappedMethod();

    // Two ways in, because they answer different questions.
    //
    // CONTINUE is the one people want almost every time: the server already remembers
    // where you were standing, so which save loads underneath barely matters - it gets
    // overwritten by your real position on arrival. Making people pick a save first was
    // an extra decision that changes nothing.
    //
    // LOAD GAME still exists for choosing WHICH character to bring, which is a real
    // choice until proper character slots land.
    this.AddMenuItem("MULTIPLAYER - CONTINUE", n"OnMultiplayerContinue");
    this.AddMenuItem("MULTIPLAYER - LOAD GAME", n"OnMultiplayerJoin");

    // PopulateMenuItemList refreshes at its end, before our item existed. Without
    // refreshing again the entry is in the data but never drawn, which looks exactly
    // like the hook silently doing nothing.
    this.m_menuListController.Refresh();
}

// NOTE THE ref<>. The game's own source declares this as `data : PauseMenuListItemData`,
// and copying that signature across breaks the ENTIRE mod: redscript requires class types
// to be reached through ref or wref, and it aborts all compilation on one bad file. The
// game then starts with no scripts at all - no menu entry, no chat, no HUD - while looking
// like the mod simply did nothing. The .script dialect the game ships is not redscript.
@wrapMethod(SingleplayerMenuGameController)
protected func HandleMenuItemActivate(data: ref<PauseMenuListItemData>) -> Bool {
    // Straight back in - no save picker.
    //
    // The most recent save is loaded purely as a vehicle to get into the world; the
    // server replaces the position on arrival with wherever you actually were. That is
    // what makes this "continue from the server" rather than "continue singleplayer".
    if Equals(data.eventName, n"OnMultiplayerContinue") {
        FTLog(s"[CyberpunkMP] Multiplayer CONTINUE selected from the main menu");

        let network = GameInstance.GetNetworkWorldSystem();
        if IsDefined(network) {
            network.RequestJoin();
        } else {
            FTLogError(s"[CyberpunkMP] No NetworkWorldSystem in the menu - cannot arm the join");
        }

        this.GetSystemRequestsHandler().LoadLastCheckpoint(false);
        return true;
    }

    if Equals(data.eventName, n"OnMultiplayerJoin") {
        FTLog(s"[CyberpunkMP] Multiplayer selected from the main menu");

        // Record the decision BEFORE the load starts.
        //
        // Nothing can connect from here: a menu has no world and no player, so there is
        // nowhere for anyone to be put. All this does is remember that the player asked
        // to join, on the one object that survives the load that follows - see
        // NetworkWorldSystem::RequestJoin. MultiplayerGameController picks it back up on
        // the other side, once there is a real world to arrive in.
        //
        // An earlier version connected automatically on world attach instead. The main
        // menu is itself a world, so it connected there, and the game died the moment the
        // server tried to stream players into it.
        let network = GameInstance.GetNetworkWorldSystem();
        if IsDefined(network) {
            network.RequestJoin();
        } else {
            // Not fatal on its own - the save still loads, the player simply arrives in
            // singleplayer and can connect by hand. Worth saying out loud, because it
            // would otherwise look like the menu entry did nothing at all.
            FTLogError(s"[CyberpunkMP] No NetworkWorldSystem in the menu - cannot arm the join");
        }

        // Hand over to the game's own Load Game screen rather than picking a save for
        // them. This is the same event the Load Game entry spawns, so it is the real
        // save list, with the real character on each slot.
        this.m_menuEventDispatcher.SpawnEvent(n"OnLoadGame");

        return true;
    }

    return wrappedMethod(data);
}
