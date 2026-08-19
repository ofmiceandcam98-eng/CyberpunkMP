module CyberpunkMP

import CyberpunkMP.World.*

/**
 * Reads what the player is carrying and hands it to the server.
 *
 * Identity was already server-owned - name, face, body, progression. Possessions were not,
 * and that is the half that decides whether a character really belongs to the server.
 * Everything a player carried came off their own disk: their guns, their eddies, their
 * cyberware. Two people with the same character name walked around with whatever their
 * singleplayer file happened to contain, and nothing the server knew could contradict it.
 *
 * WHY THE READING HAPPENS HERE
 *
 * The item API is script-side - GetItemList, GetItemQuantity, GiveItemByTDBID are all
 * declared on TransactionSystem in the game's own scripts. C++ can reach them only through
 * RTTI, by hand, with no compiler checking any of it. Doing it here means the game's own
 * type system verifies every call, and the only thing crossing into native is a pair of
 * numbers at a time.
 *
 * WHAT IS DELIBERATELY NOT SENT
 *
 * Equipped clothing is already synchronised by the appearance path, and money is kept
 * separate because the game models it as an item and a balance is not a stack - counting
 * it as one is how somebody duplicates eddies.
 */
public class MpInventory {

  /**
   * Everything the player is carrying, as TweakDBIDs and counts.
   *
   * Returns false when it could not read - a missing player or transaction system - and
   * the caller must then send nothing rather than sending an empty list. Empty means
   * "owns nothing" to the server, and telling it that because a lookup failed would empty
   * somebody's pockets on their next login.
   */
  public static func Capture(network: ref<NetworkWorldSystem>) -> Bool {
    let game = GetGameInstance();
    let player = GetPlayer(game);

    if !IsDefined(player) || !IsDefined(network) {
      return false;
    }

    let transaction = GameInstance.GetTransactionSystem(game);
    if !IsDefined(transaction) {
      FTLogError(s"[MpInventory] no transaction system - not sending an empty inventory");
      return false;
    }

    let items: array<wref<gameItemData>>;
    if !transaction.GetItemList(player, items) {
      FTLogError(s"[MpInventory] could not read the inventory - sending nothing rather than nothing-owned");
      return false;
    }

    network.BeginInventoryCapture();

    let money = transaction.GetItemQuantity(player, MarketSystem.Money());
    let counted = 0;

    // Written with nested ifs rather than `continue`, which redscript does not support in
    // a for-loop - it fails with UNRESOLVED_REF, which reads like a missing symbol rather
    // than an unsupported keyword.
    let moneyId = ItemID.GetTDBID(MarketSystem.Money());

    for item in items {
      if IsDefined(item) {
        let id = item.GetID();
        let tdbid = ItemID.GetTDBID(id);

        // Money is read separately above. Left in here it would be stored twice - once as
        // a balance and once as a stack of eddies - and restored twice with it.
        if tdbid != moneyId {
          let quantity = transaction.GetItemQuantity(player, id);

          if quantity > 0 {
            network.AddInventoryItem(TDBID.ToNumber(tdbid), Cast<Uint32>(quantity));
            counted += 1;
          }
        }
      }
    }

    network.EndInventoryCapture(Cast<Int64>(money));

    FTLog(s"[MpInventory] captured \(counted) stack(s) and \(money) eddies for the server");
    return true;
  }

  /**
   * Puts back what the server says this character owns.
   *
   * Called on spawn, and only when the server actually has a record - see has_possessions
   * on the spawn response. A character created before any of this existed has an empty
   * record, and applying it would take everything they own.
   *
   * Additive rather than replacing. Clearing the inventory first is the "correct" version
   * and it is not worth the risk yet: RemoveItem on the wrong thing destroys somebody's
   * gear with no undo, and until the capture side has been watched for a while, the
   * failure mode of giving too much is far kinder than the failure mode of deleting.
   */
  public static func Restore(network: ref<NetworkWorldSystem>, items: array<MpItemStack>, money: Int64) -> Void {
    let game = GetGameInstance();
    let player = GetPlayer(game);

    if !IsDefined(player) || !IsDefined(network) {
      return;
    }

    let transaction = GameInstance.GetTransactionSystem(game);
    if !IsDefined(transaction) {
      return;
    }

    let restored = 0;
    for stack in items {
      // Rebuilt in native: TDBID has ToNumber and no inverse.
      let tdbid = network.TdbidFromNumber(stack.id);

      if TDBID.IsValid(tdbid) {
        transaction.GiveItemByTDBID(player, tdbid, Cast<Int32>(stack.quantity));
        restored += 1;
      }
    }

    FTLog(s"[MpInventory] restored \(restored) stack(s) and \(money) eddies from the server");
  }
}

/** One item and how many of it. Mirrors ItemStack in the protocol. */
public struct MpItemStack {
  public let id: Uint64;
  public let quantity: Uint32;
}
