module CyberpunkMP

// Deliberately NOT `import CyberpunkMP.World.*`.
//
// The mod defines its own VehicleSystem for networked vehicles, and a wildcard import of
// that namespace shadows the GAME's VehicleSystem - the one that owns
// GetPlayerUnlockedVehicles and EnablePlayerVehicle. The result is UNRESOLVED_METHOD on
// two calls whose signatures are provably correct, which reads like the game API being
// missing rather than a name collision.
//
// Importing the single class needed keeps our NetworkWorldSystem reachable while leaving
// VehicleSystem meaning the game's.
import CyberpunkMP.World.NetworkWorldSystem

/**
 * Vehicles a character owns, held by the server.
 *
 * Stored by NAME rather than by record id, which is the opposite of everything else in
 * this mod. EnablePlayerVehicle takes a String, and a TweakDBID cannot be turned back into
 * its string on 2.31 - release builds ship an empty debug name table, the same trap that
 * left remote players naked for a week. The name is the only field that survives the round
 * trip, so the name is what is kept.
 */
public class MpVehicles {

  /**
   * Deliberately does nothing.
   *
   * This used to report every unlocked vehicle to the server, back when the garage WAS
   * the ownership record. It is not any more: the server owns vehicle instances, and a
   * player's phone contents are derived from them.
   *
   * Reporting the garage now would be worse than pointless - it would be the client
   * telling the server which cars it has, which is precisely the claim ownership exists to
   * stop anyone making. Left as an empty function rather than deleted so the call site
   * stays honest about what happens: nothing, on purpose.
   */
  public static func Capture(network: ref<NetworkWorldSystem>) -> Void {
  }

  /**
   * Puts the models this player owns into their phone.
   *
   * This is the whole interface now. Cyberpunk already has a vehicle summon - the phone,
   * the animation, the arrival, the spawn positioning - and players already know it, so
   * ownership decides what appears there and the game does the summoning. A custom call
   * command was built first and was a worse version of something the game ships with.
   *
   * Idempotent - EnablePlayerVehicle either unlocks a model or finds it already unlocked -
   * so unlike items and money this needs no difference calculation.
   *
   * The server's list IS exhaustive now, and anything else is locked.
   *
   * This used to leave unmentioned models alone, for a reason that was right at the time:
   * taking cars off somebody because a lookup failed is not a recoverable mistake. But
   * every player loads the same world template, and that template comes with its own
   * garage - so "leave it alone" meant every account inherited the template's cars and saw
   * them in their phone despite the server considering none of them owned.
   *
   * The failed-lookup worry is answered properly now rather than avoided:
   * IsCharacterStatusKnown() says whether the server actually replied, so a client that
   * never heard back changes nothing at all.
   *
   * ORDER MATTERS - locking runs BEFORE unlocking, deliberately. The server stores names
   * as strings and the game hands them back as CNames; if those ever disagree, locking
   * first means the enable pass immediately puts back anything wrongly taken, and the
   * mistake lasts microseconds. The other order would leave a car the player genuinely
   * owns locked until their next join.
   */
  /**
   * Does the server say this account owns this model?
   *
   * A linear scan of the restore list, which is the whole of what the server holds for
   * this character - a handful of entries against a handful of unlocked models, so the
   * cost is nothing and the alternative is a lookup structure that would have to be built
   * every join anyway.
   */
  public static func ServerOwns(network: ref<NetworkWorldSystem>, name: String) -> Bool {
    let count = network.GetRestoreVehicleCount();
    let index: Uint32 = 0u;

    while index < count {
      if Equals(network.GetRestoreVehicle(index), name) {
        return true;
      }

      index += 1u;
    }

    return false;
  }

  public static func Restore(network: ref<NetworkWorldSystem>) -> Void {
    let count = network.GetRestoreVehicleCount();

    // NOT `if count == 0 then return`. A character who owns no cars is exactly the one who
    // must not keep the template's - returning early here left a brand new player with a
    // phone full of vehicles the server has never heard of.
    if !network.IsCharacterStatusKnown() {
      return;
    }

    let vehicleSystem = GameInstance.GetVehicleSystem(GetGameInstance());

    if !IsDefined(vehicleSystem) {
      network.ScriptLog("restore: no vehicle system - vehicles not restored");
      return;
    }

    // Take the template's garage away first (see the note above on why this order).
    let held: array<PlayerVehicle>;
    vehicleSystem.GetPlayerUnlockedVehicles(held);

    let locked = 0;

    for entry in held {
      let name = NameToString(entry.name);

      if !MpVehicles.ServerOwns(network, name) {
        // despawnIfDisabling: a car already sitting in the world is not left standing
        // there unowned once its unlock is gone.
        if vehicleSystem.EnablePlayerVehicle(name, false, true) {
          locked += 1;
        }
      }
    }

    let index: Uint32 = 0u;
    let unlocked = 0;

    while index < count {
      if vehicleSystem.EnablePlayerVehicle(network.GetRestoreVehicle(index), true) {
        unlocked += 1;
      }

      index += 1u;
    }

    network.ScriptLog(s"restore: \(unlocked) vehicle(s) unlocked of \(count) stored, \(locked) not-owned locked");
  }
}
