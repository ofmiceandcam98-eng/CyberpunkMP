# Stage 6D — reward content audit

**READ-ONLY. No game files, server code, redscript, protocol, netcode, TweakDB, saves or
migration state modified.** Completed 2026-09-05 (Cam stream) under the netcode freeze.

---

## The headline: the audit found a gap in my own money map, and closed the biggest unknown

Two results, and the first is a correction.

### 1. There is a THIRD money API, and my Stage 6B search would have missed every use of it

Stage 6B mapped money by searching for `MarketSystem.Money()` — the item id — and found ten
scripts. **That pattern does not match `TransactionSystem.GiveMoney()`**, a dedicated money API
that takes an amount and a currency name:

```
accessPointController.script:863
    GameInstance.GetTransactionSystem( player.GetGame() )
        .GiveMoney( GetPlayerMainObject(), (Int32)(moneyReward * moneyModifier), 'money' );

player.script:2681
    transSystem.GiveMoney( this, price, 'money' );     // money shard pickup
```

An exhaustive search for `.GiveMoney(` / `.RemoveMoney(` across the game scripts returns
**exactly these two call sites** — so the gap is now closed, and the complete money-mutation
API surface in this game is:

| API | Used by |
|---|---|
| `TransferItem(…, MarketSystem.Money(), …)` | vendor buy/sell |
| `GiveItem` / `RemoveItem(…, MarketSystem.Money(), …)` | respec, taxi, vending, vanilla vehicle shop, bounty, `GiveReward` currency packages |
| **`GiveMoney(target, amount, 'money')`** | **access points, money shards** |

**Carry this to the new server:** any future audit of money movement must cover all three, not
just the item-id pattern. This is exactly the class of miss that makes an authority model leak.

### 2. Access point money does NOT come from its reward record — so the TweakDB question never applied to it

`accessPointController.script:489` calls `GiveReward(…, T"RPGActionRewards.Hacking", …)`, which
is what put it on the TweakDB list. But the **money** arrives from a completely separate script
path in the same file:

```
RewardMoney(baseMoney):                                          :808-865
    moneyModifier = (hackingSlotDifficulty * 0.025) + baseMoney

    moneyReward = f(equipped cyberdeck QUALITY):
        Common 100 · CommonPlus 150 · Uncommon 250 · UncommonPlus 350
        Rare 500 · RarePlus 800 · Epic 1200 · EpicPlus 1500
        Legendary 2000 · LegendaryPlusPlus 2500 · default 100

    if the cyberdeck is ICONIC:  moneyReward *= 1.04999995

    GiveMoney(player, (Int32)(moneyReward * moneyModifier), 'money')
    -> QueuePSEvent(NetworkMoneySiphoned) -> m_moneyAwarded = true
```

`GenerateMaterialDrops` (`:874`) is a separate function granting crafting materials. So
`RPGActionRewards.Hacking` is the **materials** reward; the eddies are script math.

**This is the single most valuable result of the audit.** The largest reward-money source in the
game is now fully specified from readable script, with no TweakDB dependency, and it is
**server-computable** given three inputs the server would need to know:

```
the access point's hacking-slot difficulty     (world state)
the player's equipped cyberdeck quality        (progression / equipment state)
whether that cyberdeck is iconic               (item state)
m_moneyAwarded, persistent per access point    (one-shot semantics, already in the game)
```

The game already tracks one-shot per access point (`m_moneyAwarded` is `persistent`), which is
exactly the anti-replay property an authoritative version needs.

**Caveat, stated rather than buried:** the cyberdeck quality and iconic flag are *item instance
state* — the thing the current item model cannot represent (handoff §1.5). So authoritative
access-point rewards depend on the item-fidelity work, or on the server modelling cyberdecks
specifically.

### 3. Money shards — also script-computed

```
player.script:2678-2683
    itemType == gamedataItemType.Gen_MoneyShard:
        price = RPGManager.CalculateSellPrice(game, this, evt.itemID) * quantity
        GiveMoney(this, price, 'money')
        RemoveItem(this, evt.itemID, <all of them>)
```

Picking up a money shard converts it to eddies at its sell price. This is the concrete mechanism
behind Stage 6B's "world eddie pickup" row, and it is **item-driven**: the amount comes from the
shard's own record via `CalculateSellPrice`.

---

## Records inspected, and the honest limit

### What could not be resolved, and exactly why

The remaining reward records could **not** be resolved, and I am not going to guess at them.

`TweakDB` stores record names as **hashes, not strings** — consistent with the codebase's own
note that `TDBID.ToStringDEBUG` returns empty on 2.31. Grepping `tweakdb.bin` (42 MB) for
`RPGActionRewards.ExtractPartsDoor` and the rest returns nothing, because the literal is not
there.

Resolving `CurrencyPackage` contents needs a TweakDB parser: locate a record by hashed id, walk
its flats, follow an array-of-ids to the currency records, resolve those. The tooling on this
machine does not do it:

| Tool | Result |
|---|---|
| `WolvenKit.CLI.exe` 8.20.0 (extracted from the Console zip) | commands cover archives, CR2W, hashes, oodle, wwise — **no TweakDB command** |
| WolvenKit GUI | has a TweakDB browser, but it is interactive; not driven here |
| TweakXL (installed) | applies tweaks; no read-out used here |
| grep on `tweakdb.bin` | defeated by hashed names |

Writing a TweakDB parser is a real piece of work, and a subtly wrong one would remove rows from a
**security** workload on bad evidence. Not worth it against the value: §2 above already removed
the biggest item from the list by reading script instead.

### Record status

| Record | Money-bearing? | Basis |
|---|---|---|
| `RPGActionRewards.Hacking` | **NOT MONEY** (materials) | proven — access point money comes from `GiveMoney`, and `GenerateMaterialDrops` is separate |
| `RPGActionRewards.ProgramPartsAccessPoint` | **UNKNOWN** | TweakDB; name suggests components, not proven |
| `RPGActionRewards.ExtractPartsDoor` | **UNKNOWN** | TweakDB |
| `RPGActionRewards.ExtractPartsTerminal` | **UNKNOWN** | TweakDB |
| `RPGActionRewards.ExtractPartsSecurityTurret` | **UNKNOWN** | TweakDB (used by turret, camera, and both explosive controllers) |
| `disposalDeviceController` rewards | **UNKNOWN** | record-driven: `rewards[i].GetID()` from device data — the id is not a literal in script |
| `baseDeviceActions.script:543` rewards | **UNKNOWN** | same — `rewards[i].GetID()`, per-device data |
| `bountyManager.script:257` reward | **UNKNOWN** | `rewardID` from bounty data |

**The two record-driven callers cannot be resolved from script at all**, even with a TweakDB
reader: the reward id comes from each device's own data, so the answer is per-device, not
per-record. Any future check has to enumerate device records, not five reward records.

---

## Revised Class C money matrix

**A. TRUE MONEY SOURCES — confirmed, remain in the money-authority workload**

| Source | How the amount is decided | Server-computable? |
|---|---|---|
| vendor purchase | `TransferItem` on money; vendor pricing | needs vendor model |
| vendor sale | as above | needs vendor model |
| ripperdoc / shops / consumables / junk sale | vendor path | needs vendor model |
| **access point hack** | **script formula (§2)** | **yes — given deck quality, difficulty, iconic flag** |
| **money shard pickup** | **`CalculateSellPrice` × quantity** | **yes — given the shard record** |
| perk respec | `Price.RespecBase + perPerk × spent` | **yes — already specified (6C §B)** |
| Delamain fare | `m_currentTravelCost` | likely — needs the route model |
| vending machine | `evt.GetPrice()` | likely — needs the machine model |
| vanilla vehicle shop | `RemoveItem` on money | **DISABLE** (decided) |
| Yaiba showroom | as above | **DISABLE** (decided) |
| casino | table logic | **DISABLE unless server owns the RNG** |
| bounty reward | bounty record | unknown amount source |
| console / mod grants | — | **DISABLE** |

**B. INVENTORY-ONLY REWARD SOURCES — moved out of the money workload**

| Source | Evidence |
|---|---|
| `RPGActionRewards.Hacking` (access point materials) | proven: money is `GiveMoney`, materials are `GenerateMaterialDrops` |

**C. MIXED** — none confirmed.

**D. STILL UNKNOWN** — the six records/paths in the table above: `ProgramPartsAccessPoint`, the
three `ExtractParts*`, disposal and generic device-action rewards, and the bounty reward amount.

### The real count

Before this audit, "device scrap/hack rewards" was one **REACHABLE** row of unknown content.

- **1 row proven NOT money** and removed (`RPGActionRewards.Hacking` materials).
- **2 rows proven money AND fully specified from script** — access point eddies, money shards.
  Both moved from "unknown content" to "designable now".
- **6 remain unknown**, all of them device-scrap or bounty *amounts*, and all of them small
  relative to vendors.
- **1 new API surface** added to the audit rules (`GiveMoney`).

**Vendors remain the dominant unsolved money problem**, and nothing here changes that. What
changed is that the second-largest source — access points — is now fully understood and needs no
TweakDB work at all.

---

## New TweakDB dependencies discovered

The server would need these values, and they are **data, not code** — so the replacement server
must snapshot them rather than ask a client:

```
Price.RespecBase                     Price.RespecSinglePerk          (respec, 6C §B)
per-cyberdeck Quality()                                              (access point reward)
per-shard sell price via CalculateSellPrice                          (money shards)
per-device reward record ids                                         (disposal, baseDeviceActions)
```

---

## Respec — unresolved dependency recorded

Added to the `RespecIntent` design as an explicit blocker:

> **PROGRESSION TRANSACTION OWNERSHIP.** The perk reset executes in the client's game while the
> debit is authoritative. Neither of these may be a valid final state:
>
> ```
> money debit succeeds  +  perk reset fails
> perk reset succeeds   +  money debit fails
> ```
>
> Before respec ships, decide whether the perk reset becomes server-authoritative, or whether the
> server can otherwise guarantee the reset and the debit commit as one logical operation.

---

## Confirmations

| | |
|---|---|
| no game data modified | ✅ — every game-side action was a read (`grep`, `sed`, `stat`) |
| TweakDB unmodified | ✅ — `tweakdb.bin` read only, never written |
| outgoing runtime code unchanged | ✅ — documentation only |
| production `.proto` untouched | ✅ |
| no netcode touched | ✅ |
| migration inactive | ✅ |
| selector parked | ✅ |
| nothing pushed | ✅ |

The only thing written outside the repo was extracting `WolvenKit.Console-8.20.0.zip` (a file
Cam already had) into the session scratchpad to check its command list.
