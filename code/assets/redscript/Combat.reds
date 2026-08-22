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

/**
 * Stage 10 - quickhack status effects, in both directions.
 *
 * Quickhack DAMAGE needs nothing extra: an Overheat burns health through Cyberpunk's
 * ordinary hit pipeline, so the hook above already reports it. What does not travel is the
 * EFFECT - blindness, a weapon glitch, crippled movement. Those are applied to the PUPPET
 * standing on the attacker's machine, and the person actually being hacked never finds out.
 *
 * OUTBOUND: notice an effect landing on one of our puppets and tell the server.
 */
@wrapMethod(ScriptedPuppet)
protected cb func OnStatusEffectApplied(evt: ref<ApplyStatusEffectEvent>) -> Bool {
    MpReportStatusEffect(this, evt);
    return wrappedMethod(evt);
}

public func MpReportStatusEffect(target: ref<ScriptedPuppet>, evt: ref<ApplyStatusEffectEvent>) -> Void {
    if !IsDefined(target) || !IsDefined(evt) || !IsDefined(evt.staticData) {
        return;
    }

    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) || !network.IsConnected() {
        return;
    }

    let targetId = network.GetServerIdByEntity(target.GetEntityID());
    if Equals(targetId, 0ul) {
        return;
    }

    // Only what WE caused. Every client has these puppets, and without this every observer
    // would report the same hack - and the victim would receive it several times over.
    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) {
        return;
    }

    if NotEquals(evt.instigatorEntityID, player.GetEntityID()) {
        return;
    }

    network.SendStatusEffect(
        targetId,
        TDBID.ToNumber(evt.staticData.GetID()),
        evt.stackCount,
        0ul);
}

/**
 * INBOUND: apply what the server says landed on us.
 *
 * On the local PLAYER, not on a puppet - a status effect on a puppet is a status effect on
 * a puppet. This is the only place the hack can actually do anything to the person it was
 * aimed at.
 *
 * Drained on a timer rather than an event because the arrival happens on the network
 * thread; the queue is native, and this is script visiting it.
 */
public class MpStatusEffectPoll extends DelayCallback {
    public func Call() -> Void {
        let network = GameInstance.GetNetworkWorldSystem();

        if !IsDefined(network) {
            return;
        }

        if network.IsConnected() {
            let player = GetPlayer(GetGameInstance());

            if IsDefined(player) {
                // Everything waiting, not just one - two hacks can land inside a frame, and
                // draining one per poll would spread them over half a second.
                let effect = network.ConsumeIncomingStatusEffect();

                while NotEquals(effect, 0ul) {
                    StatusEffectHelper.ApplyStatusEffect(player, network.TdbidFromNumber(effect));
                    FTLog(s"[Combat] a quickhack landed on us: \(effect)");

                    effect = network.ConsumeIncomingStatusEffect();
                }

                MpApplyServerHealth(player, network);
            }
        }

        // Rearmed unconditionally, including while disconnected - the poll has to survive a
        // reconnect, and a callback that stops on the first quiet tick never comes back.
        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(new MpStatusEffectPoll(), 0.25, false);
    }
}

/**
 * Put the server's verdict on our own health pool.
 *
 * THE HALF THAT MAKES DAMAGE REAL. Everything before this detected hits, validated them,
 * and broadcast a result that nothing applied - the pipeline ran end to end and nobody ever
 * lost a hit point.
 *
 * SET, never subtract. The server sends the health it says we should be on, and applying it
 * absolutely means a dropped packet cannot leave two machines permanently disagreeing.
 * Subtracting a damage figure would also mean recomputing armour locally, which is the
 * thing the whole architecture exists to avoid.
 *
 * Interacts deliberately with Death.reds. The player is Immortal with a floor under their
 * health so the death menu can never open, and that stays true: this writes a value, it
 * does not kill anybody. When the server says we are down, the existing revive machinery is
 * what handles it - the server owns the fact, the client owns the presentation.
 */
public func MpApplyServerHealth(player: ref<PlayerPuppet>, network: ref<NetworkWorldSystem>) -> Void {
    if !IsDefined(player) || !IsDefined(network) {
        return;
    }

    // -1 means the server has said nothing new. Health genuinely reaching zero arrives as
    // a downed state, not as a value we would confuse with "no news".
    let health = network.ConsumeIncomingHealth();
    if health < 0.0 {
        return;
    }

    let pools = GameInstance.GetStatPoolsSystem(player.GetGame());
    let id = Cast<StatsObjectID>(player.GetEntityID());

    // The floor from Death.reds, so a server figure of zero does not fight the immortality
    // that keeps the death menu shut. Being downed is a STATE the server owns; it is not
    // expressed by driving a health pool to nothing.
    let target = MaxF(health, MpDeathFloor());

    pools.RequestSettingStatPoolValue(id, gamedataStatPoolType.Health, target, player, false);

    FTLog(s"[Combat] server set our health to \(target)");
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
