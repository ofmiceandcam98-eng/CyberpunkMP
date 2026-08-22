module CyberpunkMP

import CyberpunkMP.World.*

/**
 * Makes a remote player a legitimate Cyberpunk combatant - shootable and hackable.
 *
 * THE TWO GATES, both read from the game's own source and both since confirmed live.
 *
 * Weapon targeting, TSF_EnemyNPC (core/gameplay/targetingSearchFilter.script:79):
 *
 *     All(Obj_Puppet | Att_Hostile | St_Alive) AND Not(Obj_Player)
 *
 * Quickhacking, ScriptedPuppet.IsQuickHackAble (cyberpunk/puppet/scriptedPuppet.script:4045):
 *
 *     IsActive, not crowd, IsAggressive, not scene-blocked,
 *     record has objectActions, one of them PuppetQuickHack,
 *     and the attacker holds a cyberdeck
 *
 * They look like separate problems and turn out to share one answer.
 *
 * WHAT ACTUALLY DOES IT: a hostile attitude toward the other player.
 *
 * Hostility satisfies Att_Hostile for the weapon filter directly, and it is also the fourth
 * route to IsAggressive() - so it clears the quickhack gate at the same time. Measured
 * before and after on a live muppet:
 *
 *     before hostile:  IsQuickHackAble false   IsAggressive false   cannot aim
 *     after  hostile:  IsQuickHackAble true    IsAggressive true    can aim
 *
 * The other half is the record: MaMuppet and WaMuppet carry objectActions now (see
 * CyberpunkMP.tweak). Attitude alone is not enough - a puppet with no object actions fails
 * condition 5 whatever its attitude.
 *
 * WHAT WAS TRIED AND DISCARDED. The GameplayRestriction.FistFight status effect is the
 * first branch of IsAggressive() and looked like the least invasive route - no AI change,
 * no hostility. It did not work: IsAggressive stayed false after applying it, and it does
 * nothing for weapon targeting either, which needs Att_Hostile specifically rather than
 * aggression. Attitude replaced it.
 *
 * THE COST, and it is a real one. Hostility is what police, prevention and NPC AI react to.
 * Every player becoming an enemy NPC in every other player's game is what makes native
 * combat work, and it may also make bystanders and the prevention system treat them as
 * threats. Scoped as narrowly as the API allows - hostility is set toward the LOCAL PLAYER
 * only, per puppet, not by changing anyone's faction - but that is a narrowing, not an
 * elimination. This stays behind a flag until it has been played with rather than tested.
 */
public func MpTryMakeHackable(entity: ref<GameObject>) -> Void {
    if !IsDefined(entity) {
        return;
    }

    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) || !network.HackablePuppetsEnabled() {
        return;
    }

    // Only our puppets. Anything else reaching here is a mistake not worth compounding by
    // making a random NPC hostile.
    if !entity.HasTag(n"CyberpunkMP.Puppet") {
        return;
    }

    let player = GetPlayer(GetGameInstance());
    if !IsDefined(player) {
        return;
    }

    let theirs = entity.GetAttitudeAgent();
    let mine = player.GetAttitudeAgent();

    if !IsDefined(theirs) || !IsDefined(mine) {
        FTLogWarning(s"[Hackable] no attitude agent on \(EntityID.GetHash(entity.GetEntityID())) - it will not be targetable");
        return;
    }

    // Toward the local player specifically, rather than a faction or group change. This is
    // the narrowest form the API offers: it makes this one puppet a valid target for this
    // one player, and says nothing about how the rest of the world should feel about them.
    theirs.SetAttitudeTowards(mine, EAIAttitude.AIA_Hostile);

    FTLog(s"[Hackable] \(EntityID.GetHash(entity.GetEntityID())) is now a valid combat and quickhack target");
}
