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

    // Added after the base items, so it sits below Load Game rather than displacing
    // Continue - the one people reach for without reading.
    this.AddMenuItem("MULTIPLAYER", n"OnMultiplayerJoin");

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
