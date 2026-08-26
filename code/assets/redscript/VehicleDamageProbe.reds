// Vehicle damage discovery probe. Solo, one session, no second player.
//
// It answers ONE question, and the whole vehicle damage design depends on the answer:
//
//   Does the damage number Cyberpunk REPORTS match the health it actually SPENDS?
//
// We have two candidate sources and no reason yet to prefer either. gameHitEvent carries
// attackComputed.GetTotalAttackValue(Health) - what the attack was worth. The Health stat
// pool carries what the vehicle actually lost, after armour, resistances and whatever else
// the engine applies that we cannot see. If those agree, the reported value is safe to send
// and the server can validate against it. If they disagree, only the pool delta is true and
// the reported value is a lie we would be broadcasting.
//
// Getting this wrong is not hypothetical: v0.3.104 shipped quickhack damage that the server
// added ON TOP of a figure the game had already applied, and every damaging hack did roughly
// double. That bug is exactly this question answered by assumption instead of measurement.
//
// Wrapping - rather than a stat pool listener - is what makes the comparison possible: the
// wrap can sample health on BOTH sides of the engine's own handler, in one place, for the
// same hit. A listener sees the result but never the cause.
//
// Logging goes through network.ScriptLog, NOT FTLog. FTLog reaches no file this project
// collects: an entire evening's script-side messages went into the void once, and "the code
// did not run" was indistinguishable from "I cannot see it run". ScriptLog lands in the
// mod's own log next to everything else.

module CyberpunkMP

import CyberpunkMP.World.*

@wrapMethod(VehicleObject)
protected func DamagePipelineFinalized(evt: ref<gameHitEvent>) -> Void {
  MpProbeVehicleDamage(this, evt);
  wrappedMethod(evt);
}

/**
 * Sample health either side of the engine's own damage handling and report both figures.
 *
 * Deliberately does nothing but observe. No health is set, no message is sent to the server,
 * nothing is synchronized - this build is for finding out what is true, and a probe that
 * also changes things cannot tell you what would have happened without it.
 */
public func MpProbeVehicleDamage(vehicle: ref<VehicleObject>, evt: ref<gameHitEvent>) -> Void {
  if !IsDefined(vehicle) || !IsDefined(evt) {
    return;
  }

  let network = GameInstance.GetNetworkWorldSystem();
  if !IsDefined(network) {
    return;
  }

  let game = vehicle.GetGame();
  let pools = GameInstance.GetStatPoolsSystem(game);
  if !IsDefined(pools) {
    network.ScriptLog("[VehProbe] no stat pools system - cannot measure");
    return;
  }

  let id = Cast<StatsObjectID>(vehicle.GetEntityID());

  // Absolute points, not percent: a percentage cannot be compared against an attack value,
  // and the third argument defaulting differently would silently change what we measured.
  let before = pools.GetStatPoolValue(id, gamedataStatPoolType.Health, false);

  // What the engine says the attack was worth. Same call Combat.reds already uses for
  // players, so a mismatch here is about vehicles specifically, not about the accessor.
  let reported: Float = 0.0;
  if IsDefined(evt.attackComputed) {
    reported = evt.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health);
  }

  // Who did it. Proven available on this path - vanilla VehicleObject calls
  // evt.attackData.GetInstigator() a few lines below where this wrap sits.
  let who: String = "unknown";
  let weapon: String = "none";
  if IsDefined(evt.attackData) {
    let instigator = evt.attackData.GetInstigator();
    if IsDefined(instigator) {
      who = instigator.IsPlayer() ? "PLAYER" : NameToString(instigator.GetClassName());
    }

    // GetWeapon() is on AttackData (attackData.script:219) and returns the WeaponObject;
    // GetWeaponRecord() lives on THAT, not on AttackData - a distinction that cost a
    // compile here. The class name is logged rather than the record name because
    // TDBID.ToStringDEBUG returns empty strings on release builds, which would read as
    // "no weapon" for every single hit.
    let weaponObject = evt.attackData.GetWeapon();
    if IsDefined(weaponObject) {
      weapon = NameToString(weaponObject.GetClassName());
    }
  }

  network.ScriptLog(s"[VehProbe] hit on \(NameToString(vehicle.GetClassName())) by \(who) weapon '\(weapon)' - reported \(reported), health before \(before)");

  // Read again on the next frame rather than immediately: this wrap runs BEFORE the engine's
  // own handler, so sampling twice in a row here would return the same number and prove
  // nothing. The delayed read is what catches the value the pipeline actually settled on.
  let after = new MpVehicleHealthReadback();
  after.Vehicle = vehicle;
  after.Before = before;
  after.Reported = reported;

  // Three arguments, matching the form already used in Combat.reds - the trailing flag is
  // not optional in the overload this project compiles against.
  GameInstance.GetDelaySystem(game).DelayCallback(after, 0.15, false);
}

/**
 * The second half of the measurement, one beat after the engine has finished.
 *
 * 0.15s rather than a frame: the damage pipeline is not guaranteed to have settled the pool
 * by the next tick, and a reading taken too early looks exactly like "the game reported a
 * number it never spent" - the false conclusion this probe exists to avoid.
 */
public class MpVehicleHealthReadback extends DelayCallback {
  public let Vehicle: wref<VehicleObject>;
  public let Before: Float;
  public let Reported: Float;

  public func Call() -> Void {
    if !IsDefined(this.Vehicle) {
      return;
    }

    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) {
      return;
    }

    let game = this.Vehicle.GetGame();
    let pools = GameInstance.GetStatPoolsSystem(game);
    if !IsDefined(pools) {
      return;
    }

    let id = Cast<StatsObjectID>(this.Vehicle.GetEntityID());
    let after = pools.GetStatPoolValue(id, gamedataStatPoolType.Health, false);
    let spent = this.Before - after;

    // ANSWERED 2026-08-26: the reported value is exact. A shotgun blast produced 14 hits
    // inside ONE millisecond, each reporting ~8 damage, and their SUM (117.434414) matched
    // the pool delta (117.434326) to float noise.
    //
    // The first version of this verdict compared a single hit against the whole burst's
    // delta and printed MISMATCH fourteen times - a wrong conclusion from correct data,
    // which is exactly the failure this probe exists to prevent. One trigger pull is many
    // damage events, so a single hit is EXPECTED to be smaller than the total.
    //
    // Without shared state across the callbacks this cannot sum them itself, so it says
    // what it can prove and names the arithmetic to do rather than guessing.
    let verdict: String = "MATCH - the reported value is usable";

    if spent == 0.0 {
      verdict = "NO POOL CHANGE - spent no health (armour, invulnerable, or not simulated here)";
    } else if AbsF(spent - this.Reported) >= 0.5 {
      if spent > this.Reported {
        verdict = s"BURST - this is one hit of several. Sum every reported value at this timestamp; it should equal \(spent)";
      } else {
        verdict = "REPORTED EXCEEDS SPENT - the pool delta is the only truth here, do NOT broadcast the reported value";
      }
    }

    network.ScriptLog(s"[VehProbe] settled: health \(this.Before) -> \(after), spent \(spent), reported \(this.Reported) => \(verdict)");
  }
}
