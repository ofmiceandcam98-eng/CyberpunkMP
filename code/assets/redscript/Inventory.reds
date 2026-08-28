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

    // The lifepath, read here because this already runs on every save and needs no new
    // hook of its own.
    //
    // PlayerDevelopmentSystem.GetLifePath(owner) is the game's own accessor - see
    // tools/redmod/scripts/cyberpunk/systems/playerDevelopmentSystem.script - and it
    // returns gamedataLifePath { Corporate, Nomad, StreetKid, Count, Invalid }. Note the
    // order: Nomad is 1 and StreetKid is 2, and the game says Corporate, not Corpo. The
    // raw enum value goes over the wire so the server maps it once and nobody translates
    // a name back and forth.
    //
    // If the system is missing the call is simply skipped, which leaves the native at its
    // unknown sentinel and grants no kit - better than sending a plausible wrong number.
    let devSystem = GameInstance.GetScriptableSystemsContainer(GetGameInstance())
        .Get(n"PlayerDevelopmentSystem") as PlayerDevelopmentSystem;

    if IsDefined(devSystem) {
        let lifePath = devSystem.GetLifePath(player);
        network.SetLifepath(Cast<Uint32>(EnumInt(lifePath)));
        network.ScriptLog(s"capture: lifepath \(EnumInt(lifePath))");
    } else {
        network.ScriptLog("capture: PlayerDevelopmentSystem missing - no lifepath reported");
    }

    network.ScriptLog(s"capture DONE: \(counted) stack(s), \(ArraySize(chrome)) cyberware, \(money) eddies");

    // Say so on screen.
    //
    // The autosave has been running every ninety seconds and telling nobody. The chat
    // message was removed on purpose - it was noise - but nothing replaced it, so a working
    // save and a broken one looked identical from the player's side, and the save got
    // reported as broken while the log showed sixteen of them ninety seconds apart.
    //
    // Shown from here rather than from the C++ timer because this function already runs on
    // every save, automatic or not. Doing it in script needs no new native, and a native
    // declared on one side and not the other takes the whole mod down - which has already
    // happened once.
    MpShowSaved();

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

    // Take back what the server does NOT say you own.
    //
    // This is what makes the restore AUTHORITATIVE instead of additive, and it is the half
    // that was missing. The loop above only ever hands items out, so anything the shared
    // world template happened to carry stayed with the player forever - every account
    // inherited the template's weapons, clothing, ammo and consumables, and the server's
    // "130 stacks" arrived as "0 stacks given" because the template had already supplied
    // them. The template is meant to be the technical shell the game boots into, not a
    // source of anybody's belongings.
    //
    // Money already worked this way (it gives OR removes the difference, just below), so
    // this brings items into line with the rule money has followed all along.
    //
    // GUARDED ON THE SERVER HAVING ANSWERED, not on the list being non-empty.
    //
    // The distinction matters and the first version got it wrong. Guarding on "the server
    // sent at least one item" protects against a failed read, but it also means a BRAND NEW
    // character - whose record is legitimately empty - keeps everything the template gave
    // them. That is precisely the player this whole change is for: a fresh character should
    // start with what they chose in creation and nothing else, not the template's weapons
    // and ammo.
    //
    // IsCharacterStatusKnown() answers the real question - did the server tell us who this
    // character is - and it is true for an empty inventory as readily as a full one. So a
    // fresh character is stripped to their own belongings, while a client that never heard
    // back still touches nothing.
    let removed = 0;

    if network.IsCharacterStatusKnown() {
      let moneyTdbid = ItemID.GetTDBID(MarketSystem.Money());

      // DECIDE FIRST, REMOVE AFTER. Never both in the same loop.
      //
      // `existing` holds wref<gameItemData> - weak references INTO the inventory. Calling
      // RemoveItem while walking them destroys the very objects being dereferenced, and
      // the next iteration reads freed memory. That crashed the game immediately after the
      // restore on 2026-08-26, with the log ending right after "restore DONE" and no TDR
      // to blame it on.
      //
      // The server already had this lesson written down - RemoveOwnedVehicles collects into
      // a vector before destroying, "destroying entities from inside the iteration would
      // invalidate it" - and this is the same mistake on the script side.
      // AND RE-READ THE LIST FIRST. The half the note above missed.
      //
      // `existing` was filled in at the top of this function, and by the time we get here
      // it is stale: the give pass above has called GiveItemByTDBID, and the money pass has
      // called GiveItem or RemoveItem on the eddies stack. Both mutate the very inventory
      // these weak references point into, so walking the old array afterwards dereferences
      // entries the item system has already moved or destroyed.
      //
      // Not deferring the removals - that part was right and stays. This is the earlier
      // mistake in the same function: deciding not to touch the list WHILE removing, but
      // still trusting a snapshot taken before two other passes rearranged it.
      //
      // It fits what the crashes actually show. The client dies two to four seconds after
      // "restore DONE" reading a tiny bogus pointer (0x11, 0x13) inside our own DLL, which
      // is freed-and-reused memory rather than a missing null check, and the 02:25 run went
      // through the money path - "money 300 -> 0" - so a mutation definitely happened
      // between the snapshot and this loop.
      let current: array<wref<gameItemData>>;
      transaction.GetItemList(player, current);

      // CYBERWARE IS STRIPPED TOO. A new character starts with none and earns it.
      //
      // Zeldfep's call on 27 August, and it overrides the previous reading of this. It had
      // briefly been kept, because stripping it coincided with Cam coming out of the
      // creator with no head and no arms - but that link was never actually confirmed, and
      // "you start with nothing and buy your own chrome" is the design decision, not an
      // inference for me to make.
      //
      // If the head and arms go missing again, this is the first suspect and the strip log
      // below is how to check it: it prints the equipment AREAS the removal touched, and
      // ArmsCW appearing there while the arms are gone would settle it. Do NOT quietly
      // re-protect cyberware to fix that - narrow it to the specific body-critical pieces
      // and say so, because "no cyberware at start" is a rule about the game, not a bug.
      let chrome: array<wref<gameItemData>>;
      transaction.GetItemListByTag(player, n"Cyberware", chrome);

      // Nothing is protected. Kept as an empty list rather than deleting the mechanism,
      // so re-protecting a specific piece later is one ArrayPush rather than a rewrite.
      let protectedIds: array<ItemID>;

      let doomedIds: array<ItemID>;
      let doomedCounts: array<Int32>;

      // What the strip actually takes, by equipment area.
      //
      // Names cannot be logged - TDBID.ToStringDEBUG returns empty strings on 2.31 - and
      // the raw ids are unreadable, but the equipment AREA is an enum and says plainly
      // whether something being removed belongs on the character rather than in a pocket.
      // Two guesses at what took the head and arms have already been wrong; this stops the
      // third from being a guess.
      let removedAreas: array<Int32>;

      for item in current {
        if IsDefined(item) {
          let heldId = item.GetID();
          let heldTdbid = ItemID.GetTDBID(heldId);

          // Nested rather than an early `continue` - redscript has no continue statement,
          // and using one fails the whole compilation with UNRESOLVED_REF, which takes
          // every script in the mod down with it rather than just this file.
          let isProtected = ArrayContains(protectedIds, heldId);

          // BODY SLOTS ARE NOT LOOT.
          //
          // The strip was taking whatever sat in the RightArm slot, which the area log
          // caught red-handed: "removed from equip areas: ... 33" with 33 = RightArm.
          // That is a body slot, not an inventory item and not cyberware - and it fits
          // exactly what Cam saw, arms gone and a pistol floating where his hands were.
          //
          // Deliberately NOT the cyberware areas. Zeldfep's rule stands: a new character
          // starts with no chrome, so ArmsCW, HandsCW, EyesCW, LegsCW and the rest are
          // still stripped. RightArm and LeftArm are separate equipment areas from those,
          // so protecting them hands nobody an implant.
          //
          // Head is NOT on this list on purpose. It holds headgear - hats and the like -
          // which is ordinary clothing and should be stripped, and the area log shows it
          // was never removed anyway, so whatever happened to the head is a different
          // question from whatever happened to the arms.
          let bodySlot = false;
          let itemRecord = RPGManager.GetItemRecord(heldId);
          if IsDefined(itemRecord) && IsDefined(itemRecord.EquipArea()) {
            let itemArea = itemRecord.EquipArea().Type();
            bodySlot = Equals(itemArea, gamedataEquipmentArea.RightArm)
                    || Equals(itemArea, gamedataEquipmentArea.LeftArm)
                    || Equals(itemArea, gamedataEquipmentArea.BaseFists);
          }

          // Money is settled separately below as a balance. Removing it here would fight
          // that and double-count the difference.
          if heldTdbid != moneyTdbid && !isProtected && !bodySlot {
            let owned = MpInventory.ServerWants(network, TDBID.ToNumber(heldTdbid));
            let excess = transaction.GetItemQuantity(player, heldId) - owned;

            if excess > 0 {
              ArrayPush(doomedIds, heldId);
              ArrayPush(doomedCounts, excess);

              let record = RPGManager.GetItemRecord(heldId);
              if IsDefined(record) && IsDefined(record.EquipArea()) {
                let area = EnumInt(record.EquipArea().Type());
                if !ArrayContains(removedAreas, area) {
                  ArrayPush(removedAreas, area);
                }
              }
            }
          }
        }
      }

      // The iteration is over; `existing` is no longer touched, so removing is safe.
      let doomed = 0;
      while doomed < ArraySize(doomedIds) {
        transaction.RemoveItem(player, doomedIds[doomed], doomedCounts[doomed]);
        removed += 1;
        doomed += 1;
      }

      let areaList = "";
      let a = 0;
      while a < ArraySize(removedAreas) {
        areaList += s"\(removedAreas[a]) ";
        a += 1;
      }

      network.ScriptLog(s"strip: body slots kept; \(ArraySize(chrome)) cyberware piece(s) present, \(ArraySize(protectedIds)) kept; removed from equip areas: \(areaList)");

      // NOW THE EQUIPPED HALF. Everything above only touched the backpack.
      //
      // GetItemList enumerates carried items. Equipped weapons and INSTALLED cyberware
      // live in equipment slots and are invisible to it, which is why Cam finished a
      // "stripped" character still holding an assault rifle, a shotgun, an SMG and three
      // pieces of chrome - and why the log cheerfully reported "0 cyberware piece(s)
      // present" while he was wearing three. The strip was doing exactly what it was
      // written to do and only half of what it needed to.
      //
      // Read through the paperdoll, the same way AppearanceSystem.reds already reads what
      // a player is wearing, then unequip before removing: RemoveItem on something still
      // in a slot leaves the slot pointing at an item that no longer exists.
      let equipData = EquipmentSystem.GetData(player);
      let equipment = EquipmentSystem.GetInstance(player);

      if IsDefined(equipData) && IsDefined(equipment) {
        let areas: array<gamedataEquipmentArea> = [gamedataEquipmentArea.Outfit];
        let paperdoll: array<SEquipArea> = equipData.GetPaperDollEquipAreas();

        let p = 0;
        while p < ArraySize(paperdoll) {
          ArrayPush(areas, paperdoll[p].areaType);
          p += 1;
        }

        let stripped = 0;
        let q = 0;

        while q < ArraySize(areas) {
          let area = areas[q];

          // The body is not loot - same three slots the backpack pass protects. Taking
          // what sits in RightArm is what left Cam with no arms and a floating pistol.
          let isBody = Equals(area, gamedataEquipmentArea.RightArm)
                    || Equals(area, gamedataEquipmentArea.LeftArm)
                    || Equals(area, gamedataEquipmentArea.BaseFists);

          if !isBody {
            let equipped = equipData.GetVisualItemInSlot(area);
            let equippedTdbid = ItemID.GetTDBID(equipped);

            // ServerWants covers the starter kit, so the clothes and gun just handed over
            // are not stripped straight back off again.
            if TDBID.IsValid(equippedTdbid)
               && MpInventory.ServerWants(network, TDBID.ToNumber(equippedTdbid)) <= 0 {
              let unequip = new UnequipRequest();
              unequip.owner = player;
              unequip.areaType = area;
              unequip.slotIndex = 0;
              equipment.QueueRequest(unequip);

              let held = transaction.GetItemQuantity(player, equipped);
              if held > 0 {
                transaction.RemoveItem(player, equipped, held);
              }

              stripped += 1;
            }
          }

          q += 1;
        }

        network.ScriptLog(s"strip: \(stripped) equipped item(s) removed from slots");
      } else {
        network.ScriptLog("strip: no equipment system - equipped items left alone");
      }
    }

    // A starter kit is meant to be WORN.
    //
    // GiveItemByTDBID puts things in the inventory and stops, so a new character arrived
    // with the right clothes and gun in a bag and had to dress themselves first. Only the
    // kit does this - a returning player's restore must not re-equip a hundred items and
    // overwrite whatever they chose to wear.
    //
    // Same EquipRequest/QueueRequest shape as EquipCyberware below, which is how this
    // project already asks the equipment system for something.
    if network.ShouldEquipRestored() {
      let equipment = EquipmentSystem.GetInstance(player);
      if IsDefined(equipment) {
        let wearable: array<wref<gameItemData>>;
        transaction.GetItemList(player, wearable);

        let dressed = 0;
        for candidate in wearable {
          if IsDefined(candidate) {
            let candidateId = candidate.GetID();
            if MpInventory.ServerWants(network, TDBID.ToNumber(ItemID.GetTDBID(candidateId))) > 0 {
              let request = new EquipRequest();
              request.itemID = candidateId;
              request.owner = player;
              equipment.QueueRequest(request);
              dressed += 1;
            }
          }
        }

        network.ScriptLog(s"starter kit: queued \(dressed) item(s) to equip");
      } else {
        network.ScriptLog("starter kit: no equipment system - items left in the inventory");
      }

      // Arm the one-shot settlement.
      //
      // HERE and nowhere else. ShouldEquipRestored() is true only for a starter kit, which
      // means only for a character being created - so this cannot arm on a reconnect, on an
      // ordinary restore, or on an admin grant. That is the whole safety property: a
      // cleanup that could arm twice would eventually delete something a player bought.
      //
      // The engine grants its own vanilla loadout up to a minute and a half after this
      // point (see MpStarterSettlement). Nothing here can see it yet, which is exactly why
      // the strip above keeps missing it.
      let settle = new MpStarterSettlement();
      let settleList: array<wref<gameItemData>>;
      transaction.GetItemList(player, settleList);
      settle.baseline = ArraySize(settleList);
      settle.stable = 0;
      settle.sawGrant = false;
      settle.attempts = 0;
      GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(settle, 2.0, false);

      network.ScriptLog(s"settlement: armed, watching for the engine's starting loadout (baseline \(ArraySize(settleList)) stack(s))");

      network.ClearEquipRestored();
    }

    MpInventory.RestoreAttributes(network, player);
    MpInventory.RestorePerks(network, player);
    MpVehicles.Restore(network);
    MpInventory.EquipCyberware(network, player, transaction);

    network.ScriptLog(s"restore DONE: \(restored) stack(s) given, \(removed) stack(s) taken back, money \(held) -> \(stored)");
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

    // NOT `if count == 0 then return`. A character with no perks on record is the exact
    // case that needs the wipe most: a brand new player should face a BLANK perk tree and
    // spend their points on what they want, not inherit whatever build the shared template
    // was saved with. Returning early here left them with the template's perks and no way
    // to refund them.
    //
    // The wipe below runs whenever the server has answered; the buy-back loop after it
    // simply has nothing to do when the list is empty, which is the correct outcome.
    if !network.IsCharacterStatusKnown() {
      return;
    }

    let development = PlayerDevelopmentSystem.GetInstance(player);

    if !IsDefined(development) {
      network.ScriptLog("restore: no development system - perks not restored");
      return;
    }

    let index: Uint32 = 0u;
    let asked = 0;

    // Clear first, so what the server holds REPLACES what the save happened to carry.
    //
    // Two problems, one fix. The shared world template comes with its own perks, and
    // nothing took them away - so every account inherited them on top of their own. Worse,
    // the buy loop below starts at level 0 and buys `wanted` times no matter what the
    // player already has, so a perk the template had ALREADY bought got bought again: the
    // template did not merely persist, it compounded.
    //
    // RemoveAllPerks.Set(owner, unequip, free) - verified against
    // playerDevelopmentSystemRequests.script:150. Free, because this is a restore and not
    // a respec the player paid for; unequipping too, so perk-granted items go with it
    // rather than lingering as orphans.
    //
    // Guarded above on the server having ANSWERED, not on the list being non-empty - a
    // character with no perks on record is exactly the one who most needs the blank tree.
    let wipe = new RemoveAllPerks();
    wipe.Set(player, true, true);
    development.QueueRequest(wipe);

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
  /**
   * How many of this item the SERVER says you own. Zero means it does not own it at all.
   *
   * The inverse of HeldCount, and the thing that makes the restore authoritative rather
   * than additive: without it the restore could only ever hand items OUT, so anything the
   * template save happened to include stayed forever. Same linear scan for the same
   * reason - the restore list is the server's whole inventory for this character, walked
   * once per held stack, which is the same order of comparisons the give loop already does.
   */
  public static func ServerWants(network: ref<NetworkWorldSystem>, wanted: Uint64) -> Int32 {
    let count = network.GetRestoreCount();
    let index: Uint32 = 0u;

    while index < count {
      if Equals(network.GetRestoreId(index), wanted) {
        return Cast<Int32>(network.GetRestoreQuantity(index));
      }

      index += 1u;
    }

    return 0;
  }

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

/**
 * A brief on-screen confirmation that the character was saved.
 *
 * The same SimpleScreenMessage path Death.reds uses for FLATLINED - proven to work on 2.31,
 * which matters more here than finding something prettier.
 *
 * Short and quiet on purpose. This fires every ninety seconds for as long as somebody is
 * playing, so anything longer or louder becomes the thing they notice instead of the game.
 * Two seconds is enough to catch out of the corner of an eye and short enough to ignore.
 */
public func MpShowSaved() -> Void {
  let message: SimpleScreenMessage;
  message.isShown = true;
  message.duration = 2.0;
  message.message = "SAVED";

  GameInstance.GetBlackboardSystem(GetGameInstance())
    .Get(GetAllBlackboardDefs().UI_Notifications)
    .SetVariant(GetAllBlackboardDefs().UI_Notifications.WarningMessage, ToVariant(message), true);
}

/**
 * Removes the loadout the ENGINE hands a brand new character, once, and never again.
 *
 * The problem this exists for, measured on 2026-08-28:
 *
 *   04:51:08  strip: 0 cyberware present; starter kit: queued 5 item(s) to equip
 *   04:51:08  restore DONE: money 121994 -> 20000     <- correct: 5 items, no chrome
 *   04:52:36  capture DONE: 124 stack(s), 14 cyberware, 20000 eddies
 *
 * Our strip runs, the kit lands correctly, and then EIGHTY-EIGHT SECONDS LATER the game
 * grants its own vanilla Phantom Liberty starting loadout - three guns, fourteen pieces of
 * chrome, a wardrobe. The possessions autosave then captures that and writes it to the
 * server as the character's real inventory, which is why it survives every reconnect.
 *
 * The strip cannot catch it by running earlier or more often. It has to WAIT for it.
 *
 * WHY THIS IS ONE-SHOT, AND WHY THAT MATTERS MORE THAN THE FEATURE
 *
 * Cam's rule, verbatim: "make sure any new weapon, clothing, cyberware, money or any item a
 * person grabs or buys stays on them, it shouldnt disappear."
 *
 * So the obvious implementation - "remove anything the server does not know about" on a
 * timer, or on every inventory change, or on every reconnect - is FORBIDDEN. It would
 * delete the gun somebody just bought, every time, and it would look exactly like the
 * server eating their money. This runs during the creating session only, stops the moment
 * it has acted, and is never armed again for that character. After it, the server follows
 * the player; it never leads.
 *
 * WHY IT DETECTS RATHER THAN SLEEPS
 *
 * Eighty-eight seconds is an observation, not a contract - it is whatever that machine did
 * once. So this watches the stack count instead: it waits for the count to GROW past what
 * the kit left, then waits for it to stop moving, and only then acts. A slower machine gets
 * the same result, and a grant that never comes costs nothing but a few polls.
 */
public class MpStarterSettlement extends DelayCallback {
    // What the kit left behind. Growth past this is the engine's grant arriving.
    public let baseline: Int32;

    // Consecutive polls with no change. Two in a row means the grant has finished landing
    // rather than still being written.
    public let stable: Int32;

    public let sawGrant: Bool;

    // Hard stop. Three minutes of polling, then give up quietly - an armed cleanup that
    // never fires is a bug; one that fires an hour into a session is a disaster.
    public let attempts: Int32;

    public func Call() -> Void {
        let network = GameInstance.GetNetworkWorldSystem();

        if !IsDefined(network) || !network.IsConnected() {
            return; // session gone - do not reschedule, do not clean
        }

        let player = GetPlayer(GetGameInstance());
        let transaction = GameInstance.GetTransactionSystem(GetGameInstance());

        if !IsDefined(player) || !IsDefined(transaction) {
            this.Reschedule();
            return;
        }

        let items: array<wref<gameItemData>>;
        transaction.GetItemList(player, items);
        let count = ArraySize(items);

        this.attempts += 1;

        if this.attempts > 90 {
            network.ScriptLog(s"settlement: gave up after \(this.attempts) polls - the engine never granted a starting loadout (count stayed \(count))");
            return;
        }

        if count > this.baseline {
            this.sawGrant = true;
        }

        // Still moving? Wait. The grant arrives over several frames and cleaning halfway
        // through would leave part of it behind for the autosave to store.
        if count != this.baseline {
            this.stable = 0;
            this.baseline = count;
            this.Reschedule();
            return;
        }

        this.stable += 1;

        if !this.sawGrant || this.stable < 2 {
            this.Reschedule();
            return;
        }

        MpSettleStarterLoadout(network, player, transaction);
    }

    private func Reschedule() -> Void {
        GameInstance.GetDelaySystem(GetGameInstance()).DelayCallback(this, 2.0, false);
    }
}

/**
 * The one cleanup. Called by MpStarterSettlement once the engine's grant has landed and
 * stopped moving; never called from anywhere else, and never twice.
 *
 * Removes exactly what the server does not know this character owns. At this instant the
 * server's record IS the starter kit, so that is a precise statement rather than a guess -
 * and it is only true for this moment, which is why nothing else may reuse it.
 *
 * Collect-then-remove, in two passes, for the reason the strip documents: RemoveItem
 * mutates the very list being walked, and destroying the objects being dereferenced
 * mid-iteration is how that goes wrong.
 */
public func MpSettleStarterLoadout(network: ref<NetworkWorldSystem>, player: ref<PlayerPuppet>, transaction: ref<TransactionSystem>) -> Void {
  let items: array<wref<gameItemData>>;
  transaction.GetItemList(player, items);

  let moneyTdbid = ItemID.GetTDBID(MarketSystem.Money());

  let doomedIds: array<ItemID>;
  let doomedCounts: array<Int32>;

  for item in items {
    if IsDefined(item) {
      let heldId = item.GetID();
      let heldTdbid = ItemID.GetTDBID(heldId);

      // Money is never taken here. The restore has already set the balance the server
      // wants, and removing eddies as "unrecognised" would read as the server robbing them.
      if heldTdbid != moneyTdbid {
        // Body slots stay. Arms and fists are the player's own limbs, not equipment - the
        // strip learned this the hard way when Cam ended up a floating torso with a pistol.
        let bodySlot = false;
        let itemRecord = RPGManager.GetItemRecord(heldId);
        if IsDefined(itemRecord) && IsDefined(itemRecord.EquipArea()) {
          let itemArea = itemRecord.EquipArea().Type();
          bodySlot = Equals(itemArea, gamedataEquipmentArea.RightArm)
                  || Equals(itemArea, gamedataEquipmentArea.LeftArm)
                  || Equals(itemArea, gamedataEquipmentArea.BaseFists);
        }

        if !bodySlot {
          let owned = MpInventory.ServerWants(network, TDBID.ToNumber(heldTdbid));
          let excess = transaction.GetItemQuantity(player, heldId) - owned;

          if excess > 0 {
            ArrayPush(doomedIds, heldId);
            ArrayPush(doomedCounts, excess);
          }
        }
      }
    }
  }

  let doomed = 0;
  let removed = 0;
  while doomed < ArraySize(doomedIds) {
    transaction.RemoveItem(player, doomedIds[doomed], doomedCounts[doomed]);
    removed += 1;
    doomed += 1;
  }

  // Chrome the engine installed with its loadout. Cam's rule is that nobody starts with
  // cyberware - "they have to get it on their own" - and the vanilla corpo start hands over
  // fourteen pieces.
  let chrome: array<wref<gameItemData>>;
  transaction.GetItemListByTag(player, n"Cyberware", chrome);

  let chromeIds: array<ItemID>;
  for piece in chrome {
    if IsDefined(piece) {
      let pieceId = piece.GetID();
      if MpInventory.ServerWants(network, TDBID.ToNumber(ItemID.GetTDBID(pieceId))) <= 0 {
        ArrayPush(chromeIds, pieceId);
      }
    }
  }

  let c = 0;
  let pulled = 0;
  while c < ArraySize(chromeIds) {
    transaction.RemoveItem(player, chromeIds[c], 1);
    pulled += 1;
    c += 1;
  }

  network.ScriptLog(s"settlement: removed \(removed) engine-granted stack(s) and \(pulled) cyberware piece(s) - this character is now INITIALIZED and will never be cleaned again");
}
