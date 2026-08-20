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
   * The one vanilla contact worth keeping.
   *
   * Delamain stays because the server drives vehicle calls through it - removing it would
   * take away the interface players already know in exchange for nothing.
   */
  public static func IsKept(journal: wref<JournalManager>, hash: Int32) -> Bool {
    if !IsDefined(journal) {
      // No journal is not a reason to hide somebody's whole phone. Failing open leaves the
      // game exactly as it is without the mod, which is the right direction to fail in.
      return true;
    }

    return hash == MpPhone.KeptHash(journal);
  }

  /**
   * Delamain's entry hash, found by walking the contact list.
   *
   * By name rather than by a hardcoded hash. A hash copied out of a log is opaque, silently
   * wrong after a patch moves the entry, and impossible to check by eye - whereas a name
   * that stops matching leaves every contact visible, which is a fault somebody notices
   * immediately rather than one that hides the whole phone.
   *
   * Returns 0 when not found, which no real entry hashes to, so the caller keeps everything.
   */
  public static func KeptHash(journal: wref<JournalManager>) -> Int32 {
    // GetContacts takes a request context, not a bare array - the journal's getters are all
    // shaped this way. A default context asks for everything, which is what we want: the
    // point is to find Delamain wherever it is, not to reproduce the phone's own filtering.
    let request: JournalRequestContext;
    let contacts: array<wref<JournalEntry>>;
    journal.GetContacts(request, contacts);

    let index = 0;

    while index < ArraySize(contacts) {
      let contact = contacts[index] as JournalContact;

      if IsDefined(contact) {
        let name = StrLower(contact.GetLocalizedName(journal));

        if StrContains(name, "delamain") {
          return journal.GetEntryHash(contact);
        }
      }

      index += 1;
    }

    return 0;
  }

  /**
   * Says what the journal actually returned.
   *
   * Written instead of a fifth guess at JournalRequestStateFilter's values. A default
   * request context compiles, but whether it asks for EVERY contact or a narrow subset is
   * not something the compiler can answer - and if it returns nothing, KeptHash returns 0,
   * every contact stays visible, and the result is indistinguishable from a filter that
   * simply does not work.
   *
   * One log line settles it with data. Guessing enum names had a hit rate of zero across
   * five attempts; measuring has a hit rate of one.
   */
  public static func Report(network: ref<NetworkWorldSystem>, journal: wref<JournalManager>) -> Void {
    let request: JournalRequestContext;
    let contacts: array<wref<JournalEntry>>;
    journal.GetContacts(request, contacts);

    network.ScriptLog(s"[Phone] journal returned \(ArraySize(contacts)) contact(s), delamain hash \(MpPhone.KeptHash(journal))");

    let index = 0;

    while index < ArraySize(contacts) {
      let contact = contacts[index] as JournalContact;

      if IsDefined(contact) {
        network.ScriptLog(s"[Phone]   \(contact.GetLocalizedName(journal)) hash \(journal.GetEntryHash(contact))");
      }

      index += 1;
    }
  }
}

/**
 * Where the contact list is built.
 *
 * Confirmed by the compiler rather than assumed - the RTTI dump gave the name and the
 * parameters but not the modifiers, and a @wrapMethod with the wrong ones does not fail
 * politely. It aborts every script in the game, and the mod then loads doing nothing at all.
 *
 * Reports before it filters. The filtering needs to know what the journal hands back, and
 * that is the one thing five rounds of guessing could not establish.
 */
@wrapMethod(MessengerGameController)
private final func PopulateData() -> Void {
  wrappedMethod();

  if !MpPhone.Active() {
    return;
  }

  let network = GameInstance.GetNetworkWorldSystem();

  // The controller holds a journalManager of its own, but it is private and unreachable
  // from a wrapper. Asking GameInstance gets the same system without depending on another
  // class's internals - which would break on any patch that renames the field.
  if IsDefined(network) {
    MpPhone.Report(network, GameInstance.GetJournalManager(GetGameInstance()));
  }
}
