module CyberpunkMP

import CyberpunkMP.World.*

/*
 * PHASE 1 EXPERIMENT - can a mod-spawned puppet become the LOCAL player?
 *
 * Off unless the game was launched with -mod-local-puppet. Nothing here runs otherwise,
 * and the vanilla V path is untouched either way: the shipping player system must never be
 * the thing under test.
 *
 * WHY. Every attempt to change the local player's body in-world failed. InitializeState,
 * ReFinalizeState and FinalizeState are all refused during gameplay;
 * InitializeOptionsFromFinalizedState is accepted and does not rebuild the body - proved
 * by applying a female appearance and watching Cam stay male. The appearance lives in the
 * save and cannot be overwritten on a live V.
 *
 * Remote players never had this problem, because the gendered record is chosen BEFORE the
 * puppet exists. This asks whether the local player can be built the same way, using
 * PlayerSystem.LocalPlayerControlExistingObject - a public import the mod has never used,
 * on the system that 542 call sites in the game's own scripts consult to ask who the local
 * player is.
 *
 * IT DOES NOT DELETE V. Whether V and a controlled puppet can coexist is one of the
 * questions, not an assumption, so V is left exactly where it is and what happens to it is
 * reported rather than pre-empted.
 *
 * The log IS the deliverable. Every question asked of this phase is answered by a
 * [LocalPuppet] line, and "the call returned true" is reported separately from "the game
 * agrees the puppet is the player", because those came apart once already today.
 */

public func MpLocalPuppetDescribe(network: ref<NetworkWorldSystem>, when: String) -> Void {
  let game = GetGameInstance();
  let ps = GameInstance.GetPlayerSystem(game);

  if !IsDefined(ps) {
    network.ScriptLog(s"[LocalPuppet] \(when): no PlayerSystem");
    return;
  }

  let controlled = ps.GetLocalPlayerControlledGameObject();
  let main = ps.GetLocalPlayerMainGameObject();

  let controlledId = "none";
  if IsDefined(controlled) {
    controlledId = s"\(EntityID.GetHash(controlled.GetEntityID()))";
  }

  let mainId = "none";
  if IsDefined(main) {
    mainId = s"\(EntityID.GetHash(main.GetEntityID()))";
  }

  // Reported separately because they are allowed to differ - "controlled" is what input
  // and camera should follow, "main" is what most gameplay asks for. If a retarget moves
  // one and not the other, that difference IS the finding.
  network.ScriptLog(s"[LocalPuppet] \(when): controlled=\(controlledId) main=\(mainId)");
}

/*
 * Hands control to the puppet once it actually exists.
 *
 * CreateEntity returns an id immediately and the entity arrives later, so the handover
 * cannot happen in the same breath as the spawn. This polls for the entity and gives up
 * rather than retrying forever - an experiment that hangs is worse than one that fails.
 */
public class MpLocalPuppetHandover extends DelayCallback {
  public let puppetId: EntityID;
  public let attempts: Int32;

  public func Call() -> Void {
    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) {
      return;
    }

    let game = GetGameInstance();
    let entity = GameInstance.FindEntityByID(game, this.puppetId) as GameObject;

    this.attempts += 1;

    if !IsDefined(entity) {
      if this.attempts > 40 {
        network.ScriptLog(s"[LocalPuppet] FAILED: puppet \(EntityID.GetHash(this.puppetId)) never appeared after \(this.attempts) polls");
        return;
      }

      GameInstance.GetDelaySystem(game).DelayCallback(this, 0.25, false);
      return;
    }

    network.ScriptLog(s"[LocalPuppet] puppet entity exists: \(EntityID.GetHash(this.puppetId)) after \(this.attempts) poll(s)");

    MpLocalPuppetDescribe(network, "BEFORE handover");

    let ps = GameInstance.GetPlayerSystem(game);
    if !IsDefined(ps) {
      network.ScriptLog("[LocalPuppet] FAILED: no PlayerSystem to hand control to");
      return;
    }

    // THE CALL THIS WHOLE EXPERIMENT EXISTS TO MAKE.
    ps.LocalPlayerControlExistingObject(this.puppetId);

    network.ScriptLog("[LocalPuppet] LocalPlayerControlExistingObject returned");

    // Asked again a beat later as well as immediately: a retarget may not be visible in
    // the same frame, and reporting only the immediate answer would call a working
    // handover a failure.
    MpLocalPuppetDescribe(network, "AFTER handover (immediate)");

    let check = new MpLocalPuppetVerify();
    check.puppetId = this.puppetId;
    GameInstance.GetDelaySystem(game).DelayCallback(check, 1.0, false);
  }
}

public class MpLocalPuppetVerify extends DelayCallback {
  public let puppetId: EntityID;

  public func Call() -> Void {
    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) {
      return;
    }

    MpLocalPuppetDescribe(network, "AFTER handover (+1s)");

    // What became of the vanilla V. Deliberately reported rather than acted on - whether
    // to hide it, despawn it or leave it is a decision for after this run, not before.
    let v = GetPlayer(GetGameInstance());
    if IsDefined(v) {
      let vId = EntityID.GetHash(v.GetEntityID());
      let isPuppet = vId == EntityID.GetHash(this.puppetId);
      network.ScriptLog(s"[LocalPuppet] GetPlayer() now returns \(vId) - \(isPuppet ? "the PUPPET" : "still the vanilla V")");
    } else {
      network.ScriptLog("[LocalPuppet] GetPlayer() returns nothing");
    }

    network.ScriptLog("[LocalPuppet] Phase 1 observation complete - now check by hand: WASD, mouse look, third person, first person, animations");
  }
}

/*
 * Entry point. Spawns the puppet beside the player and starts the handover.
 *
 * The record comes from the SAME setting remote players use, so a pass here is
 * transferable rather than a special case built for the experiment.
 */
public func MpLocalPuppetExperiment(network: ref<NetworkWorldSystem>) -> Void {
  if !network.IsModLocalPuppetEnabled() {
    return;
  }

  let game = GetGameInstance();
  let player = GetPlayer(game);

  if !IsDefined(player) {
    network.ScriptLog("[LocalPuppet] no player yet - experiment not started");
    return;
  }

  let female = network.IsModLocalPuppetFemale();
  let record = network.GetLocalPuppetRecord();

  network.ScriptLog(s"[LocalPuppet] ===== PHASE 1 EXPERIMENT: \(female ? "FEMALE" : "MALE") puppet from record '\(record)' =====");

  MpLocalPuppetDescribe(network, "BEFORE spawn");

  // Beside the player, not on top of them. Two bodies in one spot is its own confusion,
  // and being able to SEE the puppet standing there is half the observation.
  let where = player.GetWorldPosition();
  where.X += 2.0;

  let puppetId = network.CreatePuppet(where, player.GetWorldOrientation(), !female, record);

  if !EntityID.IsDefined(puppetId) {
    network.ScriptLog("[LocalPuppet] FAILED: CreatePuppet returned no entity id");
    return;
  }

  network.ScriptLog(s"[LocalPuppet] spawn requested: id=\(EntityID.GetHash(puppetId)) - waiting for the entity");

  let handover = new MpLocalPuppetHandover();
  handover.puppetId = puppetId;
  handover.attempts = 0;
  GameInstance.GetDelaySystem(game).DelayCallback(handover, 0.25, false);
}
