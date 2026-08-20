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
