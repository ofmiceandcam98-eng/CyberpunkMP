module CyberpunkMP

// Not the wildcard - see the note in Vehicles.reds. `import CyberpunkMP.World.*` shadows
// game classes with the mod's own, and the errors then name a method as missing rather
// than naming the collision.
import CyberpunkMP.World.NetworkWorldSystem

/**
 * The pause menu opens normally, and the world keeps running behind it.
 *
 * Cam, 2026-09-03: "i wanted you to stop the pause menu from pausing... just because
 * someone pauses their game doesnt mean the world around them should pause."
 * Cam, 2026-09-04: "pause menu still doesnt show up re enable it, while keeping the state
 * of the world moving, the world itself shouldnt pause, but players should be able to have
 * the pause menu."
 *
 * WHY IT MATTERS ON A SERVER. Pausing is a local act - it freezes this machine's simulation
 * and nothing else. The server keeps ticking, other players keep moving, the clock keeps
 * running. So a paused player is not "taking a break", they are standing still in a world
 * that is going on without them, and every second of it is desync they will be dragged
 * through when they close the menu.
 *
 * WHAT WENT WRONG THE FIRST TIME, and why this is now on a delay
 *
 * The first version called UnpauseGame() inline at the bottom of the background
 * controller's OnInitialize. That took the menu away entirely - Cam opened it and got
 * nothing. The order is the reason:
 *
 *   OnInitialize does  QueueBroadcastEvent(SetMenuModeEvent(PauseMenu, Enabled))
 *                then  GetSystemRequestsHandler().PauseGame()
 *
 * The menu-mode event is QUEUED - it is consumed on a later frame. PauseGame is immediate.
 * An unpause bolted onto the end of that method therefore lands BEFORE the layer has read
 * the event that puts it into pause-menu mode, and the layer comes up against an unpaused
 * game, decides no pause menu is wanted, and never shows one.
 *
 * So the unpause now waits. The menu gets its frames, the mode event is consumed, the menu
 * is on screen and interactive - and only then does time start again underneath it.
 *
 * A HONEST LIMIT. DelaySystem callbacks are driven by the game, and a paused game may not
 * drive them - in which case this never fires, the menu still works, and the world still
 * pauses. That is the safe way round: the failure mode is the stock game, not a missing
 * menu. If the world still freezes, the pause has to be attacked somewhere other than
 * here, and this file is not the place that changes.
 */
public class MpUnpauseBehindMenu extends DelayCallback {
  /*
   * The handler is CARRIED here, not looked up here.
   *
   * GetSystemRequestsHandler is declared in exactly two places in the shipped source -
   * widgetController.script:84 and menuDefinitions.script:28 - and both are METHODS on a
   * class. There is no GameInstance.GetSystemRequestsHandler() and no free function; a
   * DelayCallback is neither of those classes, so it cannot reach one on its own. Writing
   * it bare compiles to UNRESOLVED_FN, which reads as "the function does not exist" rather
   * than "you are not standing in a class that has it" - the same trap the note at the top
   * of MainMenu.reds records, and one this file has now hit twice.
   *
   * So the controller - which IS a widgetController - hands its handler over at schedule
   * time, when the method genuinely resolves.
   */
  public let handler: wref<inkISystemRequestsHandler>;

  public func Call() -> Void {
    let network = GameInstance.GetNetworkWorldSystem();

    // Re-checked here, not just at schedule time. The menu can outlive a disconnect.
    if !IsDefined(network) || !network.IsModEnabled() {
      return;
    }

    // A weak reference, and a quarter second has passed - the menu may already be gone.
    if !IsDefined(this.handler) {
      return;
    }

    this.handler.UnpauseGame();

    network.ScriptLog("pause menu: open, and the world is still running behind it");
  }
}

/**
 * Let the menu build itself exactly as the game intends, then lift the pause.
 *
 * The method is WRAPPED rather than replaced. A replacement would silently discard whatever
 * CDPR puts in it in a future patch, and this one already does something we need to keep -
 * the menu-mode broadcast is what makes the menu appear at all.
 */
@wrapMethod(PauseMenuBackgroundGameController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();

  let network = GameInstance.GetNetworkWorldSystem();

  // Only in a launcher session. Somebody's singleplayer game should pause exactly as the
  // game intends - taking that away outside multiplayer is not ours to do.
  if IsDefined(network) && network.IsModEnabled() {
    // `this` is a widgetController here, so GetSystemRequestsHandler resolves - which is
    // the whole reason the handler is captured at this point and carried into the callback.
    let unpause = new MpUnpauseBehindMenu();
    unpause.handler = this.GetSystemRequestsHandler();

    // Long enough for the queued menu-mode event to be consumed and the menu to be up.
    // Short enough that nobody watching the world behind the menu sees it stutter.
    GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(unpause, 0.25, false);
  }

  return result;
}

/**
 * And the matching side: the game's own teardown already calls UnpauseGame, so this is left
 * alone. Wrapped only so the pair is visible in one file - a future change that needs to
 * undo something on close has an obvious home, and the wrappedMethod call means the menu
 * still tears down normally today.
 */
@wrapMethod(PauseMenuBackgroundGameController)
protected cb func OnUninitialize() -> Bool {
  return wrappedMethod();
}
