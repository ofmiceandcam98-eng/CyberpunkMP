module CyberpunkMP

// Not the wildcard - see the note in Vehicles.reds. `import CyberpunkMP.World.*` shadows
// game classes with the mod's own, and the errors then name a method as missing rather
// than naming the collision.
import CyberpunkMP.World.NetworkWorldSystem

/**
 * No saving on the server.
 *
 * Cam, 2026-09-03: "im also still able to save my game, load a game in the pause menu and i
 * see quicksave using f5 in the pause menu."
 *
 * WHY IT MATTERS. A character on this server lives on the server - possessions, money,
 * position and progression are all written there, and the client is told what it holds. A
 * local save is a SECOND copy of that character, taken at a moment of the player's choosing
 * and owned by nobody. Loading one later hands the game a version of you the server has
 * never agreed to, and the server's own record is then either overwritten by a stale
 * snapshot or silently disagreed with for the rest of the session. Neither is recoverable
 * by anything the player can see.
 *
 * It is also the obvious duplication exploit: save with the money, spend it, load, spend it
 * again. The inventory rules Cam set out - what you pick up stays yours, nobody shares an
 * inventory - do not survive a mechanism that lets one character exist twice.
 *
 * HOW, and why this is not the pause menu again
 *
 * The game has a first-class facility for exactly this: SaveLocksManager holds a set of
 * named reasons, and IsSavingLocked is true while any is present. Quicksave, manual save
 * and autosave all consult it, so ONE lock covers F5, the menu entry and the game saving on
 * its own - which is the whole point of using the engine's own gate rather than hiding
 * buttons.
 *
 * That distinction matters here, because the last attempt at this removed entries from the
 * pause menu and Cam rejected it twice ("you removed the pause menu, i didnt want that").
 * Nothing here touches a menu. The menu stays exactly as it is; the game declines to save
 * and says so in its own words, the same as it does during a quest that forbids saving.
 *
 * LOADING IS NOT COVERED and is deliberately left alone - see the note at the bottom.
 *
 * ONLY IN A LAUNCHER SESSION. Somebody's singleplayer game is theirs. Gated on
 * IsModEnabled() rather than IsConnected() for the same reason the difficulty lock is: the
 * lock has to already be in place before anything can offer to save, and that includes the
 * whole character-creation flow, which runs before a socket exists. The lock is lifted the
 * moment a session stops being a launcher session.
 */

// One name, used to add and to remove. AddSaveLock refuses a duplicate
// (saveLocksManager.script:29 checks Contains), so re-adding on every poll costs nothing
// and cannot grow the array - which is what makes a poll the right shape here.
public func MpSaveLockReason() -> CName = n"NightCityOnline"

public class MpSaveLock extends DelayCallback {
  public func Call() -> Void {
    let game = GetGameInstance();
    let network = GameInstance.GetNetworkWorldSystem();

    if IsDefined(network) && network.IsModEnabled() {
      SaveLocksManager.RequestSaveLockAdd(game, MpSaveLockReason());
    } else {
      // Not a launcher session any more. Give saving back and stop polling - leaving the
      // lock on would follow them into their own singleplayer game and quietly break
      // saving there, which is the same overstep the difficulty lock takes care to undo.
      SaveLocksManager.RequestSaveLockRemove(game, MpSaveLockReason());
      return;
    }

    // Ten seconds, matching the difficulty lock. The lock is not something a player can
    // clear, so this is only here to survive anything that clears the manager's own state -
    // a load, a session change - rather than to police a value.
    GameInstance.GetDelaySystem(game).DelayCallback(new MpSaveLock(), 10.0, false);
  }
}

/**
 * Armed from the main menu, before anything can offer to save.
 *
 * Same entry point and same reasoning as the difficulty lock: this is the one controller
 * every session passes through, and it runs before the world exists. Armed once per
 * controller rather than per press, because the poll reschedules itself and a second one
 * would double it forever.
 */
@addField(SingleplayerMenuGameController)
private let m_mpSaveLockArmed: Bool;

@wrapMethod(SingleplayerMenuGameController)
protected cb func OnInitialize() -> Bool {
  let result = wrappedMethod();

  let network = GameInstance.GetNetworkWorldSystem();

  if IsDefined(network) && network.IsModEnabled() && !this.m_mpSaveLockArmed {
    this.m_mpSaveLockArmed = true;

    // Immediately, not in ten seconds. The gap would be a window in which saving works.
    SaveLocksManager.RequestSaveLockAdd(GetGameInstance(), MpSaveLockReason());

    GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(new MpSaveLock(), 10.0, false);

    network.ScriptLog("saving: locked for this session - characters live on the server");
  }

  return result;
}

/*
 * WHY LOADING IS NOT BLOCKED HERE.
 *
 * Cam reported saving and loading together, and only saving is handled. That is deliberate
 * rather than forgotten.
 *
 * SaveLocksManager gates SAVING only - IsSavingLocked is what quicksave, manual save and
 * autosave consult, and nothing in it is consulted by a load. Blocking loads needs a
 * different lever, and the obvious one is dangerous: the mod LOADS A SAVE ITSELF. The
 * world template is how a session starts, and LoadLastCheckpoint is on that path - a
 * blanket load block would break the mod's own start before it inconvenienced anybody.
 *
 * Blocking saving already removes the reason loading is a problem: with no local save being
 * written, the only thing left to load is whatever singleplayer saves existed before, and
 * loading one drops the player out to a singleplayer game rather than corrupting their
 * server character. Worth closing properly later - it belongs with the menu work, not here.
 */
