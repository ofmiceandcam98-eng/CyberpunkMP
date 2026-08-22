module CyberpunkMP

import CyberpunkMP.World.*

/**
 * Stage 6 - hit synchronisation. The client half that was missing.
 *
 * Stages 1 to 5 built the protocol, the server validation, server-owned health and
 * server-owned ammunition, and nothing ever sent an event, because nothing detected one. A
 * remote player was not a target the engine would accept, so no hit could occur to report.
 * That is fixed (see Hackable.reds), and this is the piece that connects the two.
 *
 * INTERCEPT, DO NOT REPLACE. The brief is explicit and it is right: Cyberpunk already
 * computes attacker, target, weapon, damage, damage type, hit position, direction and
 * whether it crit. Rebuilding any of that server-side would be months of work that drifted
 * from the game every patch. gameHitEvent carries all of it, so the whole job here is to
 * notice and report.
 *
 * WHAT THIS DOES NOT DO. It does not apply damage, block the hit, or decide anything. The
 * local engine carries on exactly as it would have; what is sent is a CLAIM, and the
 * authoritative answer arrives separately as NotifyDamageResult. A client that applied its
 * own damage would be the "fake damage packet" architecture the brief warns against.
 *
 * ONLY OUR OWN ATTACKS, and only against our own puppets. Every NPC in Night City runs
 * through this hook; the two guards below are what keep it from becoming a firehose.
 */

// gamedataStatPoolType.Health is what a weapon spends. Named here so the intent survives.
@wrapMethod(ScriptedPuppet)
protected cb func OnHit(evt: ref<gameHitEvent>) -> Bool {
    MpReportHit(this, evt);

    // Always chain. Suppressing the game's own hit handling would cost the hit reaction,
    // the blood, the impact sound and the stagger - all the things that make a shot read as
    // a shot - and we would then have to rebuild every one of them.
    return wrappedMethod(evt);
}

/**
 * Tell the server that the local player just hit a remote player.
 *
 * Silent about everything else on purpose. This runs for every NPC hit in the game.
 */
public func MpReportHit(target: ref<ScriptedPuppet>, evt: ref<gameHitEvent>) -> Void {
    if !IsDefined(target) || !IsDefined(evt) {
        return;
    }

    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) || !network.IsConnected() {
        return;
    }

    // Is the thing that got hit one of ours? Zero means an ordinary NPC, which is the
    // common case rather than an error.
    let targetId = network.GetServerIdByEntity(target.GetEntityID());
    if Equals(targetId, 0ul) {
        return;
    }

    // Did WE do it? Every client sees the same puppets, and without this every observer
    // would report the same shot - the server would then have to work out which of five
    // identical claims was real.
    let instigator = evt.attackData.GetInstigator();
    let player = GetPlayer(GetGameInstance());

    if !IsDefined(instigator) || !IsDefined(player) {
        return;
    }

    if NotEquals(instigator.GetEntityID(), player.GetEntityID()) {
        return;
    }

    // What the engine computed. Health is the pool a weapon actually spends; armour and
    // resistances have already been applied by this point, which is precisely why taking
    // the game's number is better than recomputing it.
    let damage: Float = 0.0;
    let damageType: Int32 = 0;

    if IsDefined(evt.attackComputed) {
        damage = evt.attackComputed.GetTotalAttackValue(gamedataStatPoolType.Health);
        damageType = EnumInt(evt.attackComputed.GetDominatingDamageType());
    }

    // A hit that did nothing is not worth a packet. Grazes, blocked shots and the second
    // half of a piercing round all land here.
    if damage <= 0.0 {
        return;
    }

    // Ranged unless the attack data says otherwise. The distinction that matters to the
    // receiver is which reaction to play, and melee reads differently from a bullet.
    let sourceType: Uint32 = 0u;
    let attackType: Uint32 = 0u;

    if Equals(evt.attackData.GetAttackType(), gamedataAttackType.Melee) {
        sourceType = 1u;
        attackType = 1u;
    }

    // Which weapon, when there is one. Zero for fists and for anything that does not
    // present a record.
    let sourceId: Uint64 = 0ul;
    let weapon = evt.attackData.GetWeapon();

    if IsDefined(weapon) {
        sourceId = TDBID.ToNumber(weapon.GetWeaponRecord().GetID());
    }

    network.SendCombatEvent(
        targetId,
        sourceType,
        attackType,
        sourceId,
        Cast<Uint32>(damageType),
        0u,                      // hit zone - see the note below
        damage,
        evt.hitPosition,
        evt.hitDirection,
        false,                   // crit
        false);                  // headshot
}

// HIT ZONE, CRIT AND HEADSHOT are sent as zero/false deliberately, rather than guessed.
//
// gameHitEvent carries hitComponent and hitRepresentationResult, which is where the body
// part lives - but turning those into a zone id means knowing how the game names its
// targeting components (Targeting_Head, Targeting_Chest, Targeting_LeftArmLimbCyberAim and
// the rest, all confirmed present on our own puppets). Mapping them is a small job that
// needs the real component names read off a live hit, exactly like the quickhack actions
// did.
//
// Sending a wrong zone would be worse than sending none: the receiver picks a hit reaction
// from it, so a bad value produces somebody clutching their leg when they were shot in the
// head, and nothing in any log would say why.
