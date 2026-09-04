module CyberpunkMP

// Not the wildcard - see the note in Vehicles.reds. `import CyberpunkMP.World.*` shadows
// game classes with the mod's own, and the errors then name a method as missing rather
// than naming the collision.
import CyberpunkMP.World.NetworkWorldSystem

/**
 * The pause menu stops pausing the world.
 *
 * Cam, 2026-09-03: "i wanted you to stop the pause menu from pausing."
 *
 * WHY IT MATTERS ON A SERVER. Pausing is a local act - it freezes this machine's simulation
 * and nothing else. The server keeps ticking, other players keep moving, the clock keeps
 * running. So a paused player is not "taking a break", they are standing still in a world
 * that is going on without them, and every second of it is desync they will be dragged
 * through when they close the menu. Worse, it is a free advantage: pause mid-firefight,
 * think, and come back to a world that politely waited on your screen but did not wait on
 * anybody else's.
 *
 * The menu still opens, still works, and still looks the same. Only time keeps running.
 *
 * HOW, and why this shape rather than replacing the method
 *
 * PauseMenuBackgroundGameController.OnInitialize broadcasts the menu-mode event and then
 * calls GetSystemRequestsHandler().PauseGame(). PauseGame is a native import
 * (systemRequestsHandler.script:71) and cannot be wrapped, so the pause cannot be
 * intercepted where it happens.
 *
 * The method could be REPLACED to omit that one line, and that was the first instinct. It
 * is the worse option: a replacement silently discards whatever CDPR puts in that method in
 * a future patch, and this one already does something we want to keep - the menu-mode
 * broadcast is what makes the menu appear at all.
 *
 * So the game's own code runs untouched and the pause is undone immediately after. One
 * frame of pause nobody can perceive, and if a patch changes that method we inherit the
 * change instead of overwriting it.
 */
@wrapMethod(PauseMenuBackgroundGameController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();

  let network = GameInstance.GetNetworkWorldSystem();

  // Only in a launcher session. Somebody's singleplayer game should pause exactly as the
  // game intends - taking that away outside multiplayer is not ours to do.
  // NOTE this.  - GetSystemRequestsHandler is a method on widgetController
  // (core/ui/baseControllers/widgetController.script:84), not a free function, so it
  // resolves only through an instance that inherits it. Written bare it fails with
  // UNRESOLVED_FN, which reads like the function does not exist. Same trap the note at the
  // top of MainMenu.reds records.
  if IsDefined(network) && network.IsModEnabled() {
    this.GetSystemRequestsHandler().UnpauseGame();
  }

  return result;
}

/**
 * And the matching side: do not unpause something we never paused.
 *
 * OnUninitialize calls UnpauseGame unconditionally. Left alone that is a second, unmatched
 * unpause - harmless today, because the handler treats it as a state rather than a counter,
 * but it is the kind of asymmetry that becomes a bug the moment the engine starts counting.
 * Leaving the wrappedMethod call in place means the menu still tears down normally.
 */
@wrapMethod(PauseMenuBackgroundGameController)
protected cb func OnUninitialize() -> Bool {
  return wrappedMethod();
}
