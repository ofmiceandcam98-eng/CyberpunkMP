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

  /** Every unlocked vehicle, handed to native one name at a time. */
  public static func Capture(network: ref<NetworkWorldSystem>) -> Void {
    let vehicleSystem = GameInstance.GetVehicleSystem(GetGameInstance());

    if !IsDefined(vehicleSystem) {
      return;
    }

    let owned: array<PlayerVehicle>;
    vehicleSystem.GetPlayerUnlockedVehicles(owned);

    let counted = 0;
    for vehicle in owned {
      if vehicle.isUnlocked {
        network.AddVehicle(NameToString(vehicle.name));
        counted += 1;
      }
    }

    network.ScriptLog(s"capture: \(counted) vehicle(s)");
  }

  /**
   * Re-unlocks everything the server says this character owns.
   *
   * Idempotent by nature - EnablePlayerVehicle either unlocks it or finds it already
   * unlocked - so unlike items and money this needs no difference calculation.
   *
   * Nothing is ever disabled. A vehicle the server has not heard of is left alone rather
   * than taken away: the stored list is what somebody has earned, not an exhaustive
   * statement of what they are allowed to have.
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
