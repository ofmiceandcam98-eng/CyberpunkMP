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
      network.ScriptLog("capture: no transaction system - sending nothing");
      return false;
    }

    let items: array<wref<gameItemData>>;
    if !transaction.GetItemList(player, items) {
      network.ScriptLog("capture: could not read the inventory");
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

    // Skills, street cred and level.
    //
    // All three are gamedataProficiencyType entries, so one loop covers what would
    // otherwise be three separate features. Walked by index up to Count rather than by a
    // hand-written list, so a proficiency added in a game patch is picked up without
    // anybody remembering to add it here.
    let development = PlayerDevelopmentSystem.GetData(player);

    if IsDefined(development) {
      let profType = 0;

      while profType < EnumInt(gamedataProficiencyType.Count) {
        let level = development.GetProficiencyLevel(IntEnum<gamedataProficiencyType>(profType));

        if level > 0 {
          network.AddProficiency(Cast<Uint32>(profType), level);
        }

        profType += 1;
      }
    } else {
      network.ScriptLog("capture: no development data - skills not captured");
    }

    // Attributes: the five that everything else is built on.
    //
    // Walked as an explicit list rather than by enum index. gamedataStatType holds
    // hundreds of entries - every stat in the game - and only these five are attributes;
    // sweeping the whole enum would store armour and carry-capacity as if a player had
    // chosen them.
    if IsDefined(development) {
      let attrs = [gamedataStatType.Strength, gamedataStatType.Reflexes,
                   gamedataStatType.TechnicalAbility, gamedataStatType.Intelligence,
                   gamedataStatType.Cool];

      for attr in attrs {
        let value = development.GetAttributeValue(attr);
        network.AddAttribute(Cast<Uint32>(EnumInt(attr)), Cast<Int32>(value));
      }

      // Perks. Stored now, handed back later - see Restore.
      let perkType = 0;

      while perkType < EnumInt(gamedataNewPerkType.Count) {
        let level = development.IsNewPerkBought(IntEnum<gamedataNewPerkType>(perkType));

        if level > 0 {
          network.AddPerk(Cast<Uint32>(perkType), level);
        }

        perkType += 1;
      }
    }

    MpVehicles.Capture(network);

    network.EndInventoryCapture(Cast<Int64>(money));

    // Counted separately and only for the log.
    //
    // Cyberware is not a separate system - it is ordinary items carrying the 'Cyberware'
    // tag, which is how the game itself finds it (player.script reads it with
    // GetItemListByTag). So GetItemList above already returns it and it is already being
    // stored. Reporting the number proves that rather than assuming it, because "is
    // cyberware included" is exactly the sort of thing that reads as obviously-yes and
    // turns out to be no.
    //
    // Worth being precise about what this does and does not buy: the chrome is STORED and
    // will be given back, but given back into the inventory, not re-installed into its
    // slots. Installing is the equipment system's job and is a separate piece of work.
    let chrome: array<wref<gameItemData>>;
    transaction.GetItemListByTag(player, n"Cyberware", chrome);

    network.ScriptLog(s"capture DONE: \(counted) stack(s), \(ArraySize(chrome)) cyberware, \(money) eddies");
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
  public static func Restore(network: ref<NetworkWorldSystem>) -> Void {
    let game = GetGameInstance();
    let player = GetPlayer(game);

    // Both of these used to return in silence, which is how a restore that never ran
    // looked identical to one that had nothing to do.
    if !IsDefined(player) {
      network.ScriptLog("restore: no player");
      return;
    }

    if !IsDefined(network) {
      FTLogError(s"[MpInventory] restore asked for with no network system");
      return;
    }

    let transaction = GameInstance.GetTransactionSystem(game);
    if !IsDefined(transaction) {
      network.ScriptLog("restore: no transaction system");
      return;
    }

    // What the player already holds, read ONCE and indexed by TweakDBID.
    //
    // GetItemQuantity takes an ItemID and there is no way to build one from a TweakDBID,
    // so the only way to answer "how many of this do they have" is to enumerate what is
    // there and look it up. Read once rather than per stack: 124 stacks against 124 held
    // items is fifteen thousand comparisons either way, but one enumeration instead of
    // 124 of them.
    let heldIds: array<Uint64>;
    let heldCounts: array<Int32>;

    let existing: array<wref<gameItemData>>;
    transaction.GetItemList(player, existing);

    for item in existing {
      if IsDefined(item) {
        ArrayPush(heldIds, TDBID.ToNumber(ItemID.GetTDBID(item.GetID())));
        ArrayPush(heldCounts, transaction.GetItemQuantity(player, item.GetID()));
      }
    }

    let count = network.GetRestoreCount();
    let restored = 0;
    let index: Uint32 = 0u;

    while index < count {
      // Rebuilt in native: TDBID has ToNumber and no inverse.
      let tdbid = network.TdbidFromNumber(network.GetRestoreId(index));
      let quantity = network.GetRestoreQuantity(index);

      if TDBID.IsValid(tdbid) && quantity > 0u {
        // Give the DIFFERENCE, never the total.
        //
        // Adding the stored amount to whatever is already held duplicates everything the
        // player still has - which is exactly what happened on 19 Aug: connecting from
        // inside the world (where the puppet already exists, so this actually ran) handed
        // back all 124 stacks on top of the 124 already there, and doubled the lot.
        //
        // Making it a difference also makes it idempotent, which matters more than the
        // duplication: this can now run twice, or run after a partial restore, and the
        // result is the same. The server's number is the answer rather than an increment.
        let want = Cast<Int32>(quantity);
        let have = MpInventory.HeldCount(heldIds, heldCounts, network.GetRestoreId(index));
        let owed = want - have;

        if owed > 0 {
          transaction.GiveItemByTDBID(player, tdbid, owed);
          restored += 1;
        }
      }

      index += 1u;
    }

    // Money, which the first version of this logged and never actually granted - the
    // figure appeared in the log and nothing reached the player, which reads exactly like
    // it worked.
    //
    // Granted as the difference, not the total. Money is an item the player already holds
    // some of, so giving the stored amount on top of what they have doubles it every
    // single spawn.
    let stored = network.GetRestoreMoney();
    let held = transaction.GetItemQuantity(player, MarketSystem.Money());
    let owed = stored - held;

    if owed > 0 {
      transaction.GiveItem(player, MarketSystem.Money(), owed);
    } else {
      if owed < 0 {
        transaction.RemoveItem(player, MarketSystem.Money(), -owed);
      }
    }

    MpInventory.RestoreAttributes(network, player);
    MpInventory.RestorePerks(network, player);
    MpVehicles.Restore(network);
    MpInventory.EquipCyberware(network, player, transaction);

    network.ScriptLog(s"restore DONE: \(restored) stack(s) given, money \(held) -> \(stored)");
  }

  /**
   * Puts the five attributes back to what the server holds.
   *
   * SetAttribute rather than BuyAttribute: this is restoring a build that was already
   * paid for, not spending points again. Buying would fail the moment the player had no
   * points left, which is exactly the case for anybody who had finished spending them.
   *
   * PERKS ARE DELIBERATELY NOT RESTORED HERE, only stored. Handing them back means
   * UnlockNewPerk and BuyNewPerk in dependency order while points allow - and getting
   * that order wrong does not fail cleanly, it rebuilds somebody's character wrongly and
   * then saves that as the truth. Storing them costs nothing and loses nothing; the
   * restore side is worth doing carefully, on its own, with a character nobody minds
   * breaking.
   */
  public static func RestoreAttributes(network: ref<NetworkWorldSystem>, player: ref<GameObject>) -> Void {
    let count = network.GetRestoreAttributeCount();

    if count == 0u {
      return;
    }

    let index: Uint32 = 0u;
    let applied = 0;

    while index < count {
      let request = new SetAttribute();
      request.Set(player, Cast<Float>(network.GetRestoreAttributeValue(index)),
                  IntEnum<gamedataStatType>(Cast<Int32>(network.GetRestoreAttributeType(index))));

      PlayerDevelopmentSystem.GetInstance(player).QueueRequest(request);

      applied += 1;
      index += 1u;
    }

    network.ScriptLog(s"restore: \(applied) attribute(s) set from the server");
  }

  /**
   * Buys back every perk the server holds.
   *
   * Attributes go first, deliberately - see the call order in Restore. A perk usually
   * requires an attribute level to be available at all, so buying perks before the
   * attributes are back means every gated one is refused. Restoring in the order the
   * player originally built in is the only order that works.
   *
   * UnlockNewPerk then BuyNewPerk, once per level. Unlock makes it available regardless of
   * whether the prerequisite tree is satisfied yet; Buy is what actually applies it, and a
   * multi-level perk needs buying once per level rather than being set to a level.
   *
   * Requests are queued, not executed - the development system processes them in order, so
   * this cannot check whether each one succeeded. That is the honest limit here: it asks
   * for the build back and does not verify it arrived. Worth watching the first few times
   * with a character nobody minds losing.
   */
  public static func RestorePerks(network: ref<NetworkWorldSystem>, player: ref<GameObject>) -> Void {
    let count = network.GetRestorePerkCount();

    if count == 0u {
      return;
    }

    let development = PlayerDevelopmentSystem.GetInstance(player);

    if !IsDefined(development) {
      network.ScriptLog("restore: no development system - perks not restored");
      return;
    }

    let index: Uint32 = 0u;
    let asked = 0;

    while index < count {
      let perkType = IntEnum<gamedataNewPerkType>(Cast<Int32>(network.GetRestorePerkType(index)));
      let wanted = network.GetRestorePerkLevel(index);

      let unlock = new UnlockNewPerk();
      unlock.Set(player, perkType);
      development.QueueRequest(unlock);

      // Once per level. There is no "set to level N" request - the game models a perk as
      // something bought repeatedly, so restoring level three means asking three times.
      let level = 0;
      while level < wanted {
        let buy = new BuyNewPerk();
        buy.Set(player, perkType);
        development.QueueRequest(buy);
        level += 1;
      }

      asked += 1;
      index += 1u;
    }

    network.ScriptLog(s"restore: asked for \(asked) perk(s) back");
  }

  /** How many of this TweakDBID the player already holds, from the snapshot above. */
  public static func HeldCount(ids: array<Uint64>, counts: array<Int32>, wanted: Uint64) -> Int32 {
    let i = 0;

    while i < ArraySize(ids) {
      if Equals(ids[i], wanted) {
        return counts[i];
      }

      i += 1;
    }

    return 0;
  }

  /**
   * Puts every held piece of cyberware back into its slot.
   *
   * The equipment system decides which slot each piece belongs in - EquipRequest carries
   * an optional slotIndex and leaving it unset means "wherever this goes". That is what we
   * want: the server stores what you own, and the game decides where it fits, so a game
   * patch moving slots around costs nothing here.
   */
  public static func EquipCyberware(network: ref<NetworkWorldSystem>, player: ref<GameObject>, transaction: ref<TransactionSystem>) -> Void {
    let chrome: array<wref<gameItemData>>;
    transaction.GetItemListByTag(player, n"Cyberware", chrome);

    let equipment = EquipmentSystem.GetInstance(player);
    if !IsDefined(equipment) {
      network.ScriptLog("cyberware: no equipment system");
      return;
    }

    let slotted = 0;
    for item in chrome {
      if IsDefined(item) {
        let request = new EquipRequest();
        request.itemID = item.GetID();
        request.owner = player;

        equipment.QueueRequest(request);
        slotted += 1;
      }
    }

    network.ScriptLog(s"cyberware: queued \(slotted) piece(s) for installation");
  }
}

/** One item and how many of it. Mirrors ItemStack in the protocol. */
public struct MpItemStack {
  public let id: Uint64;
  public let quantity: Uint32;
}
