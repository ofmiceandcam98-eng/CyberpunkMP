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
    // Quest state just moved, which is when something becomes tracked. Untracking here
    // catches the marker at the moment it would appear.
    MpUntrackQuest();
    return;
  }

  wrappedMethod(questEntry, state);
}

// Objective updates - "objective completed", "new objective". Same reasoning: it is the
// game narrating a story at somebody in the middle of theirs.
@wrapMethod(JournalNotificationQueue)
private final func PushObjectiveQuestNotification(entry: wref<JournalEntry>) -> Void {
  if MpQuestsSilenced() {
    MpUntrackQuest();
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

/*
 * Songbird's holocall.
 *
 * The wrap on IncomingCallLogicController below never caught this one, and the reason is
 * that they are different systems: the trace of q301_hook names the scene
 * `q301_00_holocall_hook`, and a HOLOcall is delivered by HoloAudioCallLogicController.
 * A phone-call hook cannot see it. Confirmed by the compiler, since the RTTI dump gives a
 * class name but not the modifiers a wrapper has to match.
 *
 * Refused at initialise, so the call is never constructed rather than being constructed and
 * then dismissed - which is the difference between a player seeing nothing and a player
 * seeing a call flash up and vanish.
 */
@wrapMethod(HoloAudioCallLogicController)
protected cb func OnInitialize() -> Bool {
  if MpQuestsSilenced() {
    return false;
  }

  return wrappedMethod();
}

/**
 * Stop the game tracking a quest at the player.
 *
 * The tracked objective is the "455M - GO TO THE DOGTOWN BORDER" marker: an arrow, a
 * distance, and a line of somebody else's story on screen at all times. Untracking removes
 * the presentation and touches no quest state, which is the whole point - the quest keeps
 * running for whatever world systems depend on it.
 *
 * Driven from the notification queue rather than a timer. The game notifies whenever quest
 * state moves, which is exactly when something would have become tracked, so this catches
 * it at the moment it happens rather than a second later.
 */
public static func MpUntrackQuest() -> Void {
  let journal = GameInstance.GetJournalManager(GetGameInstance());

  if IsDefined(journal) {
    journal.UntrackEntry();
  }
}
