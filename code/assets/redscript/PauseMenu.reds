module CyberpunkMP

// Not the wildcard - see the note in Vehicles.reds. `import CyberpunkMP.World.*` shadows
// game classes with the mod's own, and the resulting errors name a method as missing
// rather than naming the collision.
import CyberpunkMP.World.NetworkWorldSystem

/**
 * The in-game pause menu, with the singleplayer entries removed.
 *
 * Cam's rule, 2026-09-03: SAVE GAME and LOAD GAME go. On this server neither means what it
 * says - the character lives on the server, so a local save records a body without the
 * identity attached to it, and LOADING one drops you into a world the server is not in.
 * Both are ways to end up playing somebody who is not your character, which is the same
 * reason they came off the main menu.
 *
 * SAME SHAPE AS MainMenu.reds, deliberately. wrappedMethod() is the only thing that adds
 * those entries (pauseMenu.script:PopulateMenuItemList), so the multiplayer branch does not
 * call it and re-adds the ones that still make sense. That was verified against the game's
 * own shipped source rather than guessed - and it is also why Clear() is not needed here:
 * the engine calls ShowActionsList, which clears before populating.
 *
 * WHAT STAYS, and why each one earns it:
 *
 *   RESUME          - the reason the menu exists.
 *   SETTINGS        - resolution, controls, audio. Needed at least as much in multiplayer.
 *   CREDITS         - the game's own; taking it away is not ours to do.
 *   EXIT TO MENU    - the way out. Removing it would trap somebody in the session.
 *
 * QUIT GAME is added by ShowActionsList itself, after this returns, so it is untouched.
 */
@wrapMethod(PauseMenuGameController)
private func PopulateMenuItemList() -> Void {
  let network = GameInstance.GetNetworkWorldSystem();

  // Not launched through the launcher: the vanilla pause menu, with no trace of the mod.
  if !IsDefined(network) || !network.IsModEnabled() {
    wrappedMethod();
    return;
  }

  this.AddMenuItem(GetLocalizedText("UI-Labels-Resume"), n"OnClosePauseMenu");
  this.AddMenuItem(GetLocalizedText("UI-Labels-Settings"), n"OnSwitchToSettings");
  this.AddMenuItem(GetLocalizedText("UI-Labels-Credits"), n"OnCreditsPicker");
  this.AddMenuItem(GetLocalizedText("UI-Labels-ExitToMenu"), PauseMenuAction.ExitToMainMenu);

  this.m_menuListController.Refresh();
}
