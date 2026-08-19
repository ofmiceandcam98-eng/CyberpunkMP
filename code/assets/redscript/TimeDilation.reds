module CyberpunkMP

import CyberpunkMP.World.*

// No slowing time down on a shared server.
//
// Every slow-motion effect in the game - the weapon wheel, the phone, Sandevistan,
// Kerenzikov, deflect, the perk dilations - works by asking the TimeSystem to run the
// world slower. In singleplayer there is only one world and slowing it is the feature. On
// a server there are several people in one world, and one of them slowing time does not
// slow anything for anybody else: it desynchronises them from everyone, because their
// client keeps sending positions from a world running at a different rate to the one the
// server and every other player are simulating.
//
// It is also the leading suspect for the crash on 19 August, where opening the weapon
// wheel as a passenger in a moving car killed the client. Time dilation and vehicle
// synchronisation are exactly the two things that would collide there.
//
// WHY HERE, AND NOT AT THE SOURCE
//
// The game already refuses time dilation in multiplayer - TimeDilationHelper's own code
// reads:
//
//     if( !timeSystem || ... || IsMultiplayer() ) { return false; }
//
// So CDPR built this switch and it is sitting there unused, because IsMultiplayer() is a
// native global that returns false for us. Flipping it would be one hook rather than
// several - and it is tempting, because it is clearly the intended path.
//
// Not doing that, on purpose. IsMultiplayer() gates seventeen other files: damage
// replication, combat state, carried objects, equipment, grenades, and the muppet record
// remote players are built from. Turning it on globally changes all of that at once, in
// ways nobody here has tested, to fix time dilation. That is a much bigger change than it
// looks and it would be indistinguishable from a dozen new bugs.
//
// So this blocks the three helper entry points instead. They are ordinary script statics,
// they are what the weapon wheel and the radial menus call, and wrapping them changes
// nothing else.
//
// WHAT THIS DOES NOT COVER
//
// A few effects call the TimeSystem directly rather than through the helper - 'deflect'
// and 'meleeHit' in player.script, and the access-point minigame. Those need their own
// wraps and are deliberately left for a second pass, because each one is a separate
// gameplay path and doing them blind in the same change would make a failure impossible
// to attribute.

@wrapMethod(TimeDilationHelper)
public final static func SetTimeDilation(requester: wref<GameObject>, reason: CName, timeDilation: Float, opt duration: Float, easeInCurve: CName, easeOutCurve: CName, allowMultipleTimeDilationSimultaneously: Bool, opt listener: ref<TimeDilationListener>) -> Bool {
  if MpTimeDilationBlocked() {
    return false;
  }

  return wrappedMethod(requester, reason, timeDilation, duration, easeInCurve, easeOutCurve, allowMultipleTimeDilationSimultaneously, listener);
}

@wrapMethod(TimeDilationHelper)
public final static func SetTimeDilationOnPlayer(requester: wref<GameObject>, reason: CName, timeDilation: Float, opt duration: Float, easeInCurve: CName, easeOutCurve: CName, allowMultipleTimeDilationSimultaneously: Bool, opt listener: ref<TimeDilationListener>) -> Bool {
  if MpTimeDilationBlocked() {
    return false;
  }

  return wrappedMethod(requester, reason, timeDilation, duration, easeInCurve, easeOutCurve, allowMultipleTimeDilationSimultaneously, listener);
}

// ---------------------------------------------------------------------------
// The paths that do not go through the helper
// ---------------------------------------------------------------------------

// PlayerPuppet.SetSlowMo is the 'deflect' slow-motion - it calls the TimeSystem straight
// out and never touches TimeDilationHelper, so the wraps above do not see it. Listed as a
// known gap when those were written; this closes it.
@wrapMethod(PlayerPuppet)
protected func SetSlowMo(slowMoAmount: Float, slowMoDuration: Float) -> Void {
  if MpTimeDilationBlocked() {
    return;
  }

  wrappedMethod(slowMoAmount, slowMoDuration);
}

// ---------------------------------------------------------------------------
// Skipping time
// ---------------------------------------------------------------------------

/**
 * No skipping hours forward on a shared server.
 *
 * The clock belongs to the server: it is one shared, persistent time-of-day that survives
 * restarts, and /time is how it moves. Letting one player jump it six hours means either
 * their world silently disagrees with everyone else's about whether it is night, or one
 * person drags the sky for the entire server without meaning to. Neither is a thing to
 * offer by accident from a menu.
 *
 * Blocked at Apply rather than by removing the menu entry. The skip UI is reached from
 * several places and is bound up with sleeping and with quests, so hiding every route to
 * it is a bigger and more fragile change than refusing the one action at the end of them.
 *
 * The popup still opens and still closes cleanly - it simply does not move the clock.
 */
@wrapMethod(TimeskipGameController)
private func Apply() -> Void {
  if MpTimeDilationBlocked() {
    return;
  }

  wrappedMethod();
}

/**
 * Are we on a server right now?
 *
 * Checked per call rather than cached. A player connects and disconnects inside one game
 * session, and a cached answer would leave slow-motion switched off in singleplayer after
 * a disconnect - taking a feature away from somebody who is no longer on the server, which
 * is worse than the bug being fixed.
 *
 * Returns false when the system is missing, so a failure here leaves the game behaving
 * exactly as it does without the mod.
 */
public static func MpTimeDilationBlocked() -> Bool {
  let network = GameInstance.GetNetworkWorldSystem();

  return IsDefined(network) && network.IsConnected();
}
