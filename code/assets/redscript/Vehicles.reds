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
   * Nothing is ever disabled. A model the server did not mention is left alone rather than
   * taken away: the list says what this account has earned, not an exhaustive statement of
   * what it may have. Taking cars off somebody because a lookup failed is not a
   * recoverable mistake.
   */
  public static func Restore(network: ref<NetworkWorldSystem>) -> Void {
    let count = network.GetRestoreVehicleCount();

    if count == 0u {
      return;
    }

    let vehicleSystem = GameInstance.GetVehicleSystem(GetGameInstance());

    if !IsDefined(vehicleSystem) {
      network.ScriptLog("restore: no vehicle system - vehicles not restored");
      return;
    }

    let index: Uint32 = 0u;
    let unlocked = 0;

    while index < count {
      if vehicleSystem.EnablePlayerVehicle(network.GetRestoreVehicle(index), true) {
        unlocked += 1;
      }

      index += 1u;
    }

    network.ScriptLog(s"restore: \(unlocked) vehicle(s) unlocked of \(count) stored");
  }
}
