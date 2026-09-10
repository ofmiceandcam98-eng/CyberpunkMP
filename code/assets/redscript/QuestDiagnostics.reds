module CyberpunkMP

import CyberpunkMP.World.*

/*
 * PROOF, NOT A FIX. Nothing here changes behaviour.
 *
 * ANSWERED 2026-08-29 - the Dogtown half of this, at least. The gate is the quest fact
 * ep1_side_content, and it has nothing to do with q301. See the fact list below and
 * ANTIGRAVITY_NOTES.md for the full trace through the shipped quest graphs. The server
 * carries it with the existing /fact command, so there is no code change to make - this
 * file exists to show whether it actually arrived. The phone half is still open.
 *
 * The question: what actually drags a fresh multiplayer character into the Phantom Liberty
 * prologue - the Songbird call, then Dog Eat Dog. Cam's requirement is the world without
 * the story: Dogtown reachable, no Songbird, no forced dialogue, no quest markers, and
 * Delamain still allowed to call.
 *
 * We are explicitly NOT solving this by completing q301_crash. That would keep the
 * singleplayer quest as the thing that grants Dogtown, which is the coupling we want gone.
 *
 * WHAT IS ALREADY KNOWN, from reading the game's scripts rather than guessing:
 *
 *   - Quest facts are readable AND writable from here:
 *     GameInstance.GetQuestsSystem(gi).GetFact(name) / .SetFact(name, value).
 *     delamainTaxiComponent.script:180 calls SetFact, so it is not theoretical.
 *
 *   - The prologue's own facts follow the q301_ prefix - q301_holocall_blackwall,
 *     q301_WayToCrashsite_CarHitImmunity, q301_02a_stadium_stopUIInCutscene.
 *
 *   - ep1_standalone is NOT a story gate. It only drives the ripperdoc/cyberware tutorial
 *     (ripperdoc.script:333, vendorHubMenuGameController.script:401). Setting it would
 *     change nothing about the prologue.
 *
 *   - Every phone call is presented through HudPhoneGameController.OnTriggerCall, which
 *     receives a PhoneCallInformation carrying contactName, callPhase and isAudioCall.
 *     That is the one place an allowlist can live - Delamain in, story calls out.
 *
 *   - Quests.reds already suppresses IncomingCallLogicController.SetCallInfo for EVERY
 *     caller while connected. That is a blanket block, so it would silence Delamain too
 *     once he is meant to work - and it evidently does not stop the Songbird prologue,
 *     because Cam still gets it. Both facts point at the same conclusion: the block is in
 *     the wrong place and too broad.
 *
 * WHAT THIS FILE ANSWERS
 *
 *   1. Which q301/ep1 facts are actually set in the template world at spawn.
 *   2. Who is calling when the prologue fires - contactName, phase, audio or holo.
 *
 * Read the [QuestDebug] block after a fresh spawn. The fact values say whether the
 * template has the prologue ACTIVE, merely eligible, or already done, and the [PhoneDebug]
 * line names the contact to allowlist against. Only then is it worth changing anything.
 */

// Probed on connect. Not an exhaustive list of Phantom Liberty facts - it is every q301_
// and ep1_ fact the game's own scripts reference, which is what can be justified without
// guessing at names that may not exist.
public func MpQuestDebugDump(network: ref<NetworkWorldSystem>) -> Void {
  let quests = GameInstance.GetQuestsSystem(GetGameInstance());

  if !IsDefined(quests) {
    network.ScriptLog("[QuestDebug] no quests system");
    return;
  }

  let names = [
    // ---- Dogtown access. Proven 2026-08-29 by decoding the shipped EP1 quest graphs.
    //
    // ep1_side_content is THE switch. combat_zone_gate.questphase holds the gate's
    // passage branches behind a pause condition on ep1_side_content >= 1, and never
    // reads a single q301 fact - completing Dog Eat Dog was never the requirement. The
    // same fact is the root gate on every Dogtown community, the vendors, the world
    // encounters and the mini world stories, so it is also what decides whether the
    // district is populated or an empty shell.
    //
    // It is written = 1 in thirteen places across EP1 and never written 0, and in the
    // main quests it only ever appears as a setter - no story branch reads it. Setting
    // it cannot start Songbird or anything else.
    n"ep1_side_content",
    n"ep1_installed",
    n"ep1_active",

    // Must both be 0 or the gate stays shut regardless. Zero by default; listed so a
    // failed test can be told apart from a fact that was never the problem.
    n"q304_block_dogtown_gate",
    n"dogtown_gate_combat_on",

    // Written by the gate itself the first time you get through, on foot and by car.
    // These turning 1 is the proof the crossing actually happened.
    n"on_foot_dogtown_crossed",
    n"by_car_dogtown_crossed",

    // Sub-switches for the rest of the district's content.
    n"sts_ep1_tier_1",
    n"ow_combat_zone_mini_world_stories",

    // The holocall interlock. 1 means every Phantom Liberty story call is held shut at
    // the quest level - see MpSilenceStoryHolocalls in Quests.reds. This is the one that
    // matters for a character whose save still has the prologue running, so read it here
    // rather than assuming the write took.
    n"holo_setup_active",
    n"holo_setup_started",
    n"holo_songbird_calls_v_start_activate",

    // ---- Prologue state, from the original investigation.
    n"q301_active",
    n"q301_done",
    n"q301_holocall_blackwall",
    n"q301_WayToCrashsite_CarHitImmunity",
    n"q301_02a_stadium_stopUIInCutscene",
    n"ep1_standalone",
    n"ep1_tree_unlocked",
    n"ep1_ripperdoc_tutorial_seen",
    n"ep1_ripperdoc_tutorial_started",
    n"ep1_relic_intro_button"
  ];

  network.ScriptLog("[QuestDebug] ---- world quest facts at spawn ----");

  for name in names {
    network.ScriptLog(s"[QuestDebug]   \(NameToString(name)) = \(quests.GetFact(name))");
  }

  network.ScriptLog("[QuestDebug] ---- end ----");
}

/*
 * Logs every phone call the game tries to present, and suppresses nothing.
 *
 * This is what names the Songbird trigger. contactName is the CName an allowlist would key
 * on - Delamain allowed, story callers refused - and callPhase says whether we are seeing
 * the incoming ring, the start of the conversation, or its end.
 *
 * Deliberately NOT a filter yet. Cam's brief is explicit that the trigger gets proved
 * before the smallest possible change is made, and a filter written against a guessed
 * contact name would silence the wrong things.
 */
@wrapMethod(HudPhoneGameController)
protected cb func OnTriggerCall(data: Variant) -> Bool {
  let network = GameInstance.GetNetworkWorldSystem();

  if IsDefined(network) && network.IsConnected() {
    let info = FromVariant<PhoneCallInformation>(data);
    network.ScriptLog(s"[PhoneDebug] call presented: contact='\(NameToString(info.contactName))' phase=\(EnumInt(info.callPhase)) audio=\(info.isAudioCall) playerCalling=\(info.isPlayerCalling) rejectable=\(info.isRejectable)");
  }

  return wrappedMethod(data);
}
