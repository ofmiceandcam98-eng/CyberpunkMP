module CyberpunkMP

// Deliberately NOT `import CyberpunkMP.World.*` - see the note in Vehicles.reds. The
// wildcard shadows game classes with the mod's own, and the resulting errors name a method
// as missing rather than naming the collision.
import CyberpunkMP.World.NetworkWorldSystem

/*
 * The phone belongs to the server.
 *
 * Everyone starts from the same template save, which means everyone starts with the same
 * inbox: Takemura wanting to meet, Regina offering gigs, El Capitan renting apartments.
 * None of those people know the character reading them, and on a roleplay server a contact
 * list full of strangers from somebody else's story is worse than an empty one.
 *
 * So the vanilla list is reduced to Delamain - the one contact the server actually uses,
 * because vehicles are called through it - and the players a character has added by number
 * are what fills it back up.
 *
 * WHAT THIS DOES NOT DO
 *
 * It does not delete anything. The journal still holds every contact the save shipped with;
 * they are filtered out of what the phone DISPLAYS. Deleting would be irreversible, would
 * fight the save every load, and would break quests for anybody an admin later allows one -
 * hiding costs nothing and gives that back for free.
 */
/**
 * A player-to-player call, presented in the GAME'S OWN PHONE.
 *
 * WHY THIS IS SAFE, AND WHY IT DOES NOT REOPEN SONGBIRD
 *
 * The mod blocks every call the game makes, at PhoneSystem.OnTriggerCall in Quests.reds.
 * That gate takes a questTriggerCallRequest - the QUEST SYSTEM's own request type, which
 * only the quest system builds - so it is the door story calls come through, and it stays
 * shut.
 *
 * This is a different door. A player call never becomes a questTriggerCallRequest: the
 * server tells the client a call exists, and this writes the finished presentation
 * straight to the UI_ComDevice blackboard, which is the last thing the vanilla path does
 * anyway. Nothing here consults, relaxes or depends on the gate.
 *
 * The one place the two paths touch is IncomingCallLogicController.SetCallInfo, which
 * Quests.reds also suppresses. That wrap now asks the SERVER whether this player is on a
 * call, rather than checking a local flag.
 *
 * Server state, deliberately. A local "we are presenting" flag would be a claim the client
 * makes about itself; HasCall() is a fact the server established, arrived over the wire,
 * and cannot be set by anything running in this process. It is also the honest test: the
 * question is not "did we just write the blackboard", it is "is this player in a call".
 *
 * It is safe because story calls never get that far. PhoneSystem.OnTriggerCall refuses
 * every one of them while connected, and SetCallInfo is downstream of it - so the only
 * thing that can reach the widget during a session is the write below. The wrap is belt
 * and braces over a door that is already shut.
 *
 * WHAT WAS MEASURED RATHER THAN GUESSED
 *
 * Every name here was checked with tools\CheckScripts.ps1 against 2.31 before it was used.
 * The enums are quest-prefixed, which is the trap: `PhoneCallPhase` does not exist and
 * five guesses at its members all missed, while `questPhoneCallPhase` resolves and has
 * exactly StartCall, EndCall and RejectCall. `questPhoneCallVisuals` has only Default.
 * PhoneCallInformation carries callPhase, contactName, isAudioCall, isRejectable and
 * visuals - NOT contact, contactHash, contactId or isVideoCall.
 *
 * The widget's own ANSWER and DECLINE cannot be reached: OnAccept, OnReject, AcceptCall
 * and RejectCall are all absent from the script API on 2.31. So the phone SHOWS the call
 * and the answer comes from our own input, the same way voice push-to-talk already works.
 */
public class MpPhoneCall {

  /**
   * Is the SERVER holding a call for this player?
   *
   * The discriminator between our call and a story call. Asked of the network system
   * rather than tracked here: redscript has no mutable statics, and a local flag would in
   * any case be the client asserting something about itself where a server fact is
   * available.
   */
  public static func Active() -> Bool {
    let network = GameInstance.GetNetworkWorldSystem();

    return IsDefined(network) && network.IsConnected() && network.HasCall();
  }

  /**
   * Show an incoming or outgoing call on the phone.
   *
   * contactName is the name the SERVER already resolved through this character's phone
   * book, so somebody saved as "Ripper - Watson" shows that and an unsaved number shows
   * whoever holds it. Nothing here looks a name up.
   */
  public static func Present(contactName: String, rejectable: Bool) -> Void {
    let bb = GameInstance.GetBlackboardSystem(GetGameInstance())
      .Get(GetAllBlackboardDefs().UI_ComDevice);

    if !IsDefined(bb) {
      return;
    }

    let info: PhoneCallInformation;

    /*
     * contactName is a CName, NOT a String - QuestDiagnostics reads it back through
     * NameToString. StringToName goes the other way and compiles, but a CName built at
     * runtime from a player's name is not one the game has ever seen, so whether the phone
     * RENDERS it is the one thing here that cannot be settled by compiling. If it comes up
     * blank in game, the number is the fallback and the name belongs in a widget of ours
     * rather than in this field.
     */
    info.contactName = StringToName(contactName);

    /*
     * IncomingCall for a call that is RINGING, StartCall for one already under way.
     *
     * This was StartCall for both, and that was the bug behind "the phone shows the call
     * but I cannot answer it". The vanilla answer and decline are not on the call widget -
     * IncomingCallLogicController has only OnInitialize, SetCallInfo, OnRingAnimFinished
     * and SetCallingPaused, which is what the earlier note here concluded and stopped at.
     * They are in NewHudPhoneGameController.OnAction, and BOTH branches begin by testing
     *
     *     m_CurrentCallInformation.callPhase == questPhoneCallPhase.IncomingCall
     *
     * so a call presented as StartCall can never be answered or declined by the player, no
     * matter what they press. The phone was showing a call the game did not believe was
     * ringing.
     *
     * The earlier note also recorded the enum as "exactly StartCall, EndCall and
     * RejectCall". It is not: phoneSystem.script:9 declares
     * { Undefined, IncomingCall, StartCall, EndCall } - IncomingCall exists and RejectCall
     * does not. Read from the game's own source this time rather than probed.
     */
    info.callPhase = rejectable ? questPhoneCallPhase.IncomingCall : questPhoneCallPhase.StartCall;
    info.isAudioCall = true;
    info.isRejectable = rejectable;

    // Whether WE are the one dialling. Set from the same fact the server sent, rather than
    // left at its default - the phone presents an outgoing call differently, and a call we
    // started that claims to be incoming would offer to decline itself.
    info.isPlayerCalling = !rejectable;

    info.visuals = questPhoneCallVisuals.Default;

    bb.SetVariant(GetAllBlackboardDefs().UI_ComDevice.PhoneCallInformation, ToVariant(info), true);

    // The game's own ringtone, so an incoming call sounds like one. Only for a call we are
    // RECEIVING - playing it at the person dialling would be wrong and confusing.
    if rejectable {
      let player = GetPlayer(GetGameInstance());
      if IsDefined(player) {
        GameObject.PlaySoundEvent(player, n"ui_phone_incoming_call");
      }
    }
  }

  /**
   * Take the call down.
   *
   * Written as an explicit EndCall rather than by clearing the blackboard. The phone reacts
   * to a phase, and an empty variant is not a phase - it would leave the call on screen
   * with nothing left to dismiss it.
   */
  public static func Dismiss() -> Void {
    let bb = GameInstance.GetBlackboardSystem(GetGameInstance())
      .Get(GetAllBlackboardDefs().UI_ComDevice);

    if !IsDefined(bb) {
      return;
    }

    let info: PhoneCallInformation;
    info.callPhase = questPhoneCallPhase.EndCall;
    info.isAudioCall = true;
    info.visuals = questPhoneCallVisuals.Default;

    bb.SetVariant(GetAllBlackboardDefs().UI_ComDevice.PhoneCallInformation, ToVariant(info), true);
  }
}

/**
 * ANSWER, DECLINE and HANG UP, through the game's own phone controls.
 *
 * Cam / the phone brief, 2026-09-04: "Answer/Decline should happen through the vanilla call
 * UI... The chat menu is NOT the phone."
 *
 * WHERE THE VANILLA CONTROLS ACTUALLY ARE. The note above records the earlier search giving
 * up: OnAccept, OnReject, AcceptCall and RejectCall are genuinely absent from
 * IncomingCallLogicController, which has only OnInitialize, SetCallInfo, OnRingAnimFinished
 * and SetCallingPaused. That was true and it was the wrong place to look. The controls are
 * input actions on the HUD phone controller
 * (newHudPhoneGameController.script:512 OnAction):
 *
 *     'PhoneInteract', BUTTON_RELEASED       -> answer
 *     'PhoneReject',   BUTTON_HOLD_COMPLETE  -> decline
 *
 * Both are already bound, already on screen as button hints, and both already fire while a
 * call is ringing - the only reason they did nothing for a player call is that they test
 * for the IncomingCall phase and we were presenting StartCall. See Present().
 *
 * WHY WRAPPING OnAction RATHER THAN REPLACING IT. Vanilla queues a PickupPhoneRequest on
 * these actions, which stops the ringtone and sets the talking fact. All of that is wanted -
 * it is the phone behaving normally - so the game's handler runs untouched and this only
 * adds the network half beside it.
 *
 * THE SERVER STILL DECIDES. Pressing answer sends AnswerCall(); it does not put this client
 * into a connected call. The call becomes connected when the SERVER says so and
 * OnCallStateChanged draws that - so a decline that races an approach, or an answer arriving
 * after the caller hung up, resolves the same way every other disagreement does.
 *
 * Guarded on IsCallIncoming() - the server's fact, not a widget's - so pressing the phone
 * button during a story call, or with no call at all, does nothing here.
 */
@wrapMethod(NewHudPhoneGameController)
protected cb func OnAction(action: ListenerAction, consumer: ListenerActionConsumer) -> Bool {
  let network = GameInstance.GetNetworkWorldSystem();

  if IsDefined(network) && network.IsConnected() && network.HasCall() {
    let actionName = ListenerAction.GetName(action);
    let actionType = ListenerAction.GetType(action);

    if network.IsCallIncoming() {
      // Answer. Released rather than pressed, matching vanilla - a press that turns into a
      // hold is the "open the phone" gesture, and answering on press would eat it.
      if Equals(actionName, n"PhoneInteract") && Equals(actionType, gameinputActionType.BUTTON_RELEASED) {
        network.ScriptLog("phone: answer pressed on the vanilla phone");
        network.AnswerCall();
      }
      else {
        if Equals(actionName, n"PhoneReject") && Equals(actionType, gameinputActionType.BUTTON_HOLD_COMPLETE) {
          network.ScriptLog("phone: decline held on the vanilla phone");
          network.DeclineCall();
        }
      }
    }
    else {
      /*
       * Hang up: the same hold that declines a ringing call, once it is connected.
       *
       * Vanilla has no hang-up control because story calls end themselves - the quest that
       * started one also finishes it. A player call has nobody to end it, so the decline
       * gesture is reused for the state where there is nothing to decline. It is the same
       * button hint already on screen and the meaning is the one people expect.
       */
      if Equals(actionName, n"PhoneReject") && Equals(actionType, gameinputActionType.BUTTON_HOLD_COMPLETE) {
        network.ScriptLog("phone: hang up held on the vanilla phone");
        network.HangUpCall();
      }
    }
  }

  return wrappedMethod(action, consumer);
}

public class MpPhone {

  /**
   * Only while connected.
   *
   * Asked per call rather than cached, for the same reason the quest silencer asks: someone
   * can connect and disconnect inside one session, and a cached answer would leave a
   * singleplayer phone permanently stripped of contacts belonging to a game we no longer
   * have anything to do with.
   */
  public static func Active() -> Bool {
    let network = GameInstance.GetNetworkWorldSystem();

    return IsDefined(network) && network.IsConnected();
  }

  /**
   * Is this a contact the server keeps?
   *
   * Reads the name off the row's own contact handle. An earlier version asked the journal
   * for every contact and matched hashes, which needed a JournalRequestStateFilter value
   * that five guesses failed to find - and none of that was necessary, because the row
   * being filtered already carries the contact it represents.
   *
   * Matched by name rather than by a hardcoded hash. A pasted hash is opaque, unverifiable
   * by eye, and goes silently wrong when a patch moves the entry - and its failure mode is
   * hiding the WHOLE phone. A name that stops matching leaves everything visible instead,
   * which somebody notices in a second.
   */
  public static func IsKept(journal: wref<JournalManager>, info: SocialPanelContactInfo) -> Bool {
    let contact = info.Contact;

    // Failing open, deliberately. A row we cannot identify stays visible: showing one
    // contact too many is a blemish, hiding somebody's whole phone is a bug report.
    if !IsDefined(contact) || !IsDefined(journal) {
      return true;
    }

    return StrContains(StrLower(contact.GetLocalizedName(journal)), "delamain");
  }
}

/**
 * The contacts list on the phone overlay.
 *
 * Wraps AddContactItem - the PER-ITEM call - rather than RefreshContactsList, which takes
 * the whole array by script_ref.
 *
 * The array version crashed the game on opening the phone. Handing wrappedMethod a LOCAL
 * array for a script_ref parameter means the callee may hold a reference to storage that
 * dies with this function; the caller then reads freed memory. Skipping an item never
 * transfers ownership of anything, so there is nothing to outlive the call.
 *
 * Not returning early on the whole list matters too: the game still builds every row and
 * still owns them. We decline to add the ones the server hides.
 */
@wrapMethod(SocialPanelContactsList)
public final func AddContactItem(info: SocialPanelContactInfo, index: Int32) -> Void {
  if !MpPhone.Active() {
    wrappedMethod(info, index);
    return;
  }

  if MpPhone.IsKept(GameInstance.GetJournalManager(GetGameInstance()), info) {
    wrappedMethod(info, index);
  }
}
