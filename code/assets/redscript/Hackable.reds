module CyberpunkMP

import CyberpunkMP.World.*

/**
 * HALF of what a remote player needs to be a legitimate quickhack target.
 *
 * WHAT THE GAME ACTUALLY REQUIRES, read from its own source rather than guessed.
 * ScriptedPuppet.IsQuickHackAble() (cyberpunk/puppet/scriptedPuppet.script:4045) is the
 * only gate, and it is seven conditions:
 *
 *   1. IsActive()                              - alive, not defeated/unconscious/off
 *   2. not (IsCrowd() and not IsPrevention())  - crowd NPCs are excluded
 *   3. IsAggressive()                          <- THIS FILE
 *   4. not blocked by a scene
 *   5. GetRecord().GetObjectActionsCount() > 0 <- still missing, see below
 *   6. the ATTACKER has a cyberdeck equipped   - their problem, not ours
 *   7. at least one action of type PuppetQuickHack <- still missing, see below
 *
 * IsAggressive() (same file, :2008) has four routes to true, and only one of them belongs
 * on a roleplay server:
 *
 *   - the GameplayRestriction.FistFight status effect   <- what this uses
 *   - the reaction preset being aggressive              - ours is NoReaction, deliberately
 *   - ReactionSystem.IsRegisteredAsAggressive()         - registers them with the AI
 *   - attitude toward the local player being Hostile    - makes everyone hostile to everyone
 *
 * The status effect is the only one that does not change how the puppet behaves or how the
 * world feels about it. It is a flag the aggression check happens to read first.
 *
 * WHAT THIS DOES NOT DO. Conditions 5 and 7 need `objectActions` on the Character record
 * carrying at least one PuppetQuickHack, and those record names live in TweakDB, which is
 * not readable as text. The obvious-looking candidates in the game's scripts
 * (QuickHack.BlindHack and friends) appear there in a DEVICE context - ScriptableDeviceAction
 * on a screen - so shipping them as puppet actions would be a guess wearing evidence's
 * clothes. They have to come from a TweakDB browser looking at a real hackable NPC.
 *
 * So this alone will NOT make anybody hackable yet. It removes one of the two remaining
 * obstacles, and it is the one that can be verified from source.
 *
 * OFF BY DEFAULT. FistFight is the brawl restriction and the game reads it in melee stim
 * handling too - see scriptedPuppet.script:3106 and :3743. Applied to a server-driven
 * puppet that never swings at anything it should be inert, but "should be" is why this is
 * behind a flag rather than simply on: --hackable-puppets turns it on, and turning it off
 * is a relaunch rather than a rebuild.
 */
public func MpTryMakeHackable(entity: ref<GameObject>) -> Void {
    if !IsDefined(entity) {
        return;
    }

    let network = GameInstance.GetNetworkWorldSystem();
    if !IsDefined(network) || !network.HackablePuppetsEnabled() {
        return;
    }

    // Only puppets. The tag is what the spawn path sets, and anything else reaching here
    // would be a mistake worth not compounding.
    if !entity.HasTag(n"CyberpunkMP.Puppet") {
        return;
    }

    StatusEffectHelper.ApplyStatusEffect(entity, t"GameplayRestriction.FistFight");

    // Says what it did and what is still missing, so a test that shows nothing has an
    // explanation on screen rather than requiring this comment to be found.
    FTLog(s"[Hackable] aggression flag applied to \(EntityID.GetHash(entity.GetEntityID())) - " +
          s"still needs objectActions/PuppetQuickHack on the record before it is hackable");
}
