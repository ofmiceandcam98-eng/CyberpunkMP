module CyberpunkMP

import CyberpunkMP.World.*

/*
 * Quests do not intrude on a roleplay server.
 *
 * The world is the setting; the story is whatever the people in it decide to do. Being
 * pulled into Act 2 by a phone call from a fixer nobody in the session knows is the
 * opposite of that - it interrupts a scene players are running with a scene the game
 * wrote, and it does it to one person while everyone else stands there.
 *
 * WHAT THIS DOES AND DOES NOT DO
 *
 * It silences everything quests use to REACH a player: the "new quest" popup, objective
 * updates, and the incoming calls that start most gigs. It does not disable the quest
 * engine, and cannot from here - quest phase graphs are native, and the journal's own
 * ChangeEntryState is an imported function with no script body to wrap.
 *
 * So quests still technically progress in the background. Nothing announces them, nothing
 * tracks them on screen, and nobody is telephoned into one. For a server where players
 * write their own stories that is the whole of the problem; if the engine itself ever
 * needs stopping, that is native work and a different piece.
 *
 * Singleplayer is untouched. Every check here asks whether we are connected, per call, so
 * disconnecting gives the game back exactly as it was.
 */

// The "NEW QUEST" / quest-updated popup.
@wrapMethod(JournalNotificationQueue)
private final func PushQuestNotification(questEntry: wref<JournalQuest>, state: gameJournalEntryState) -> Void {
  if MpQuestsSilenced() {
    return;
  }

  wrappedMethod(questEntry, state);
}

// Objective updates - "objective completed", "new objective". Same reasoning: it is the
// game narrating a story at somebody in the middle of theirs.
@wrapMethod(JournalNotificationQueue)
private final func PushObjectiveQuestNotification(entry: wref<JournalEntry>) -> Void {
  if MpQuestsSilenced() {
    return;
  }

  wrappedMethod(entry);
}

/*
 * Incoming calls.
 *
 * This is the one that actually matters. Most gigs and main-story beats begin with the
 * phone ringing, and a call takes over the screen and the audio whether or not the player
 * wanted it. Suppressing the popup while still letting the call arrive would be worse than
 * doing nothing - the player would be in a conversation they could neither see nor end.
 *
 * Refused at SetCallInfo, before the controller is told who is calling, so the call is
 * never presented rather than being presented and then dismissed.
 */
@wrapMethod(IncomingCallLogicController)
public final func SetCallInfo(contactName: script_ref<String>, contactEntry: wref<JournalContact>, journalMgr: wref<JournalManager>, isRejectable: Bool) -> Void {
  if MpQuestsSilenced() {
    return;
  }

  wrappedMethod(contactName, contactEntry, journalMgr, isRejectable);
}

/*
 * The phone itself. THIS is the one that actually stops calls.
 *
 * SetCallInfo above only refuses the incoming-call WIDGET. By the time it runs,
 * PhoneSystem.OnTriggerCall has already played 'ui_phone_incoming_call', written
 * PhoneCallInformation to the UI_ComDevice blackboard, and - for a player-triggered
 * call - asked PhoneManager for ApplyPhoneCallRestriction(true). So the call is live,
 * the player is restricted, and the only thing missing is the part that would have let
 * them see it. That is precisely the "in a conversation they can neither see nor end"
 * failure the comment above warns about, and it is why Cam still got the Songbird
 * prologue with that wrap already shipping.
 *
 * PhoneSystem.OnTriggerCall (phoneSystem.script:155) is the single entry point the quest
 * system uses to reach the phone. Its private TriggerCall - which is what sets the
 * blackboard, the talking state and the restriction - is called from nowhere else.
 * Refusing the request here means no sound, no blackboard entry, no restriction, no HUD:
 * the call never happens rather than happening invisibly.
 *
 * This is deliberately EVERY call, not just incoming ones. Player-initiated calls are
 * how gigs are accepted and how fixers are called back, which is the same singleplayer
 * story arriving by a different door. Nothing on the server needs the phone - chat is a
 * separate widget entirely, and the contacts list is already filtered by Phone.reds.
 *
 * Per-call connected check, like everything else here, so disconnecting restores the
 * phone completely.
 */
@wrapMethod(PhoneSystem)
private final func OnTriggerCall(request: ref<questTriggerCallRequest>) -> Void {
  if MpQuestsSilenced() {
    return;
  }

  wrappedMethod(request);
}

/**
 * Are we on a server right now?
 *
 * Asked per call rather than cached. Somebody can connect and disconnect inside one
 * session, and a cached answer would leave quests silenced in singleplayer afterwards -
 * taking the game away from somebody who is no longer on the server, which is worse than
 * the thing being fixed.
 *
 * False when the system is missing, so a failure here leaves the game behaving exactly as
 * it does without the mod.
 */
public static func MpQuestsSilenced() -> Bool {
  let network = GameInstance.GetNetworkWorldSystem();

  return IsDefined(network) && network.IsConnected();
}
