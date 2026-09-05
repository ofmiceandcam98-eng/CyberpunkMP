# New-server authority handoff

**Written 2026-09-05 (Cam stream) for the server swap.** NCO is moving to a different server
implementation. This document exists so the replacement does not have to rediscover what Phase 5
learned, and — just as important — so it does not inherit the outgoing server's *implementation*
as if it were a *requirement*.

**Netcode is frozen.** Nothing here proposes a change to the current server. No production
`.proto` has been modified; no runtime networking code has been touched.

**Read §4 before porting anything.**

---

## 1. Proven game facts

Established by tracing real code — this repository and the game's own shipped scripts at
`<game>\tools\redmod\scripts\`. These are facts about *Cyberpunk 2077 2.31*, so they survive the
server swap unchanged. They are the most valuable part of this document.

### 1.1 Money is an inventory item

`MarketSystem.Money()` returns an `ItemID`. Balances are read with
`TransactionSystem.GetItemQuantity(player, MarketSystem.Money())` and changed with
`GiveItem` / `RemoveItem` / `TransferItem` on that id.

**Consequence: money and items share one mutation surface.** Any design that treats "the
balance" as a separate scalar the client cannot reach is wrong about this game.

### 1.2 Vendor transactions move money client-side

```
vendor.script:1180
    transactionSystem.TransferItem( buyer, seller, MarketSystem.Money(), totalPrice, , , true );
vendor.script:1151
    buyerMoney = transactionSystem.GetItemQuantity( buyer, MarketSystem.Money() );
```

The vendor reads the buyer's *local* balance and moves eddies locally. Both directions.

### 1.3 Ten game scripts mutate money

```
cyberpunk/systems/marketSystem/vendor.script            buy / sell
cyberpunk/systems/marketSystem/marketSystem.script      price / money helpers
cyberpunk/systems/playerDevelopmentSystem.script:1969   perk respec cost
cyberpunk/managers/bountyManager.script:222             bounty reward (GiveItem)
cyberpunk/devices/masters/accessPointController.script  hacking reward
cyberpunk/devices/vendingMachines/iceMachineController.script:115
cyberpunk/devices/UI/internet/vehicleShopGameController.script:433
cyberpunk/devices/UI/computer/computerYaibaShowroom.script
cyberpunk/devices/recreation/casinoTable.script
core/systems/delamainTaxiSystem.script:146              taxi fare
```

### 1.4 There is a second, vanilla vehicle purchase path

`vehicleShopGameController.script` sells vehicles for eddies through the in-game internet,
entirely separately from NCO's `/sellcar` + `/buycar` flow. **Two unrelated vehicle ownership
and economy systems currently coexist by accident.** See §5.6.

### 1.5 The item model cannot represent a real item

Capture stores `(TweakDBID, quantity)`; restore calls `GiveItemByTDBID`. That cannot carry:

```
modifications   attachments   upgrades   quality / tier
crafted state   generated rolls   iconic / unique state   per-instance data
```

**A restore hands back a base item.** This is a live data-loss path in the *current* server,
independent of any authority work, and it is the single largest reason inventory authority is
its own project.

### 1.6 Cyberware is not a separate model

`GetItemListByTag(player, n"Cyberware", chrome)` — cyberware lives in the same inventory,
captures and restores as an ordinary item, and is re-equipped afterwards.

### 1.7 The client CAN be reconciled, in both directions

- **Money:** `ApplyServerMoney` (`World/NetworkWorldSystem.reds:370`) sets the local balance to
  the server's value, **unconditionally, up or down**.
- **Inventory:** the restore has both a give pass and a *take back* pass
  (`Inventory.reds:374`, "Take back what the server does NOT say you own"), reconciling
  quantity in both directions, gated on the server having answered.

**Enforcement is not the missing piece.** Knowing *why* a balance changed is.

### 1.8 Vanilla inventory and money changes are completely unobserved

There is not one hook on `TransactionSystem`, `EquipmentSystem`, or any vendor, crafting, loot,
stash or container system. Every vanilla mutation reaches the server only through the
`SaveCharacterRequest` snapshot (90-second autosave + five event triggers). Nothing is disabled.

### 1.9 Enum values on the wire are not range-validated

Proven with a scratch protocol: `static_cast<TestEnum>(9999)` round-trips intact. The generated
`_COUNT` sentinel is generator-only and is **not** part of the protocol contract.

**Rule for any server: every client-controlled enum needs an explicit known-value switch with a
refusing default.** `value < COUNT` is not a check.

---

## 2. Current old-server implementation

What exists today, so the replacement can decide deliberately what to keep.

| Piece | What it does | File |
|---|---|---|
| `AtomicWrite` | temp + fsync + `.bak` + rename, for all six stores | `AtomicWrite.h` |
| `Economy::` | the one boundary for server money/inventory mutation | `EconomyMutator.h` |
| `EconomyMigration` | trust-once migration, **inert — nothing calls it** | `EconomyMigration.h` |
| `RequestLedger` | idempotency by owner + request id, **inert — no wire field** | `RequestLedger.h` |
| `EconomyRevision` / `MigratedAt` | authority metadata, **all zero on every record** | `CharacterRecord.h` |
| `AuditLog` | append-only ledger of authoritative changes | `AuditLog.h` |
| four money paths | starter kit, `/pay`, trade, NCO vehicle sale | `ChatSystem.cpp`, `PlayerStore.h` |

**Every economy action is a chat command.** `/pay`, `/trade`, `/sellcar` + `/buycar` all arrive
as `ChatMessageRequest` — already authenticated from the connection, never from the message. The
only economy-bearing typed client request is `SaveCharacterRequest`.

**Client authority is one line**, and it is still there deliberately:

```cpp
character.Money = aMessage.get_money();      // ChatSystem.cpp
character.Inventory = <the client's list>;
```

---

## 3. Required invariants

These are conclusions, not code. **Carry them.**

1. **Atomic persistence.** No store may truncate its live file. Temp → fsync → backup → rename.
   A crash mid-write must not be able to corrupt every character on the server.
2. **One mutation boundary.** All authoritative money/inventory changes go through a single
   narrow layer that knows arithmetic and invariants and nothing about gameplay.
3. **All-or-nothing, never clamped.** Over-debit and over-credit *fail*. A partial or clamped
   transaction silently changes what two parties agreed to.
4. **Copy, then commit.** Mutate candidates; swap only when every leg succeeded.
5. **Revision the transaction, not the primitive.** One advance per participant per committed
   change — not one per `Credit`/`AddItem` call.
6. **A revision advances only when the state it versions changes.** An item-only trade must not
   advance a *money* revision.
7. **Validate headroom before mutating.** Discovering a second participant can't advance after
   the first was changed means unwinding a transaction that should not have started.
8. **`UINT64_MAX` refuses and sticks — never wraps.** A wrapped revision makes every stale
   client compare as *current*, inverting the check from protection to endorsement.
9. **Version match is not value trust.** A client at the server's revision can still send a
   forged balance. Treating `Match` as permission is client authority wearing a version number.
10. **Migration is the only 0 → 1 transition.** Ordinary play must never migrate anybody.
11. **Migration and authority are one boundary.** No window where a record is marked migrated
    while the client still decides its values — that makes the mark a lie.
12. **Idempotency keyed on authenticated identity**, never on anything the client supplied.
13. **Bound before expensive work.** Length caps, rate limits, and size caps come before record
    copies, serialization, persistence, and counterparty scans.
14. **Never record a success before it is persisted.**
15. **Explicit enum validation** (§1.9).
16. **One CharacterID = one active gameplay session.** Not built; still required.

---

## 4. Old-server code that should NOT be ported blindly

The section this document exists for.

| Do not port | Why |
|---|---|
| **`SaveCharacterRequest` as an identity write** | One message carrying appearance + inventory + money + progression is why client authority is a single un-pickable line. Split by authority, not by convenience |
| **Client-declared possessions** | The thing Phase 5 exists to remove. Do not reproduce it as a starting point |
| **`(TweakDBID, quantity)` as the item model** | §1.5 — cannot represent a real item. Design the item model *before* any inventory persistence |
| **`NotifyMoney.balance` as `int32`** | Money is `int64` everywhere else; five narrowing casts feed it. A latent truncation on the only authoritative-balance channel |
| **Economy as chat commands** | `/pay`, `/trade`, `/sellcar` work, but a text parser is not an authority surface. Re-derive them as intents |
| **First-match `FindStack`** | Duplicate stacks make later ones unreachable. Decide a canonical representation up front |
| **Spawn-time-only reconciliation** | Correct as far as it goes; a real authority model reconciles when state changes, not only at spawn |
| **`EconomyRevision` / `MigratedAt` names** | They claim money *and* inventory. See §5.1 |
| **Positional wire format with a presence bitfield** | netpack's framing: adding one optional field shifts every field after it, and a mismatch is garbage rather than an error. Whatever replaces it, prefer a format where an unknown field is survivable |

---

## 5. Required new-server features

### 5.1 Truthful authority metadata — from the start

The rename was designed, reviewed, approved, and then **deliberately paused** rather than
applied to the outgoing server (which is about to be replaced). Build the replacement with it
already correct:

```
MoneyRevision        MoneyMigratedAt
InventoryRevision    InventoryMigratedAt      (when inventory authority arrives)
```

**Do not use one field for both.** Money and inventory will cross the authority boundary at
different times — possibly years apart — and one mark cannot honestly describe both.

If any legacy record ever carries a nonzero `EconomyRevision` / `MigratedAt`: **detect, report,
refuse to reinterpret.** The old mark claimed both, and nothing in the number says which was
meant. (Production is expected to contain none — every live value is 0 — but the assumption
should be testable rather than assumed.)

### 5.2 The money authority matrix

Semantic **intents**, not messages. The client reports *context*; the server decides the
*effect*.

Legend — **Strategy**: `AUTHORITY-READY` (server can validate today) · `NEEDS SERVER MODEL` ·
`NEEDS WORLD SYNC` · `DISABLE` · `ADMIN ONLY`.

| # | Source | Dir | Intent | Server must calculate | Server must validate | Strategy |
|---|---|---|---|---|---|---|
| 1 | vendor purchase | debit | `VendorPurchaseIntent` | price, total | vendor identity, item, qty, stock, player balance, proximity, transaction exists & not replayed | **NEEDS SERVER MODEL** |
| 2 | vendor sale | credit | `VendorSaleIntent` | sale value | vendor identity, ownership of item, qty, proximity | **NEEDS SERVER MODEL** |
| 3 | ripperdoc / cyberware | debit | `VendorPurchaseIntent` | price | as #1, plus install eligibility | **NEEDS SERVER MODEL** |
| 4 | weapon / clothing shops | debit | `VendorPurchaseIntent` | price | as #1 | **NEEDS SERVER MODEL** |
| 5 | consumable purchase | debit | `VendorPurchaseIntent` | price | as #1 | **NEEDS SERVER MODEL** |
| 6 | junk / item sale | credit | `VendorSaleIntent` | value | as #2 | **NEEDS SERVER MODEL** |
| 7 | access-point reward | credit | `AccessPointRewardIntent` | reward amount (difficulty-derived) | AP identity, not already awarded (`m_moneyAwarded` is persistent per AP), proximity | **NEEDS WORLD SYNC** |
| 8 | bounty reward | credit | `BountyRewardIntent` | bounty value | target identity, kill attribution, one-shot | **NEEDS WORLD SYNC** |
| 9 | Delamain fare | debit | `TravelFareIntent` | fare | route validity, that travel happened | **NEEDS SERVER MODEL** |
| 10 | perk respec | debit | `RespecIntent` | respec cost | eligibility; server already models progression | **AUTHORITY-READY**¹ |
| 11 | vending machine | debit | `VendingPurchaseIntent` | price | machine identity, proximity | **NEEDS WORLD SYNC** |
| 12 | vanilla vehicle shop | debit | — | — | — | **DISABLE** — see §5.6 |
| 13 | Yaiba showroom | debit | — | — | — | **DISABLE** — see §5.6 |
| 14 | casino | both | `GambleIntent` | outcome **and** payout | server must own the RNG | **NEEDS SERVER MODEL**² |
| 15 | world eddie pickup | credit | `WorldPickupIntent` | amount | pickup identity, one-shot per instance, proximity | **NEEDS WORLD SYNC** |
| 16 | quest / script reward | credit | `QuestRewardIntent` | reward | quest reachability under NCO, one-shot | **NEEDS WORLD SYNC**³ |
| 17 | console / mod grant | credit | — | — | — | **DISABLE** — never a legitimate source |
| 18 | starter kit | credit | server-initiated | amount | eligibility, once | **AUTHORITY-READY** |
| 19 | `/pay` | both | `TransferIntent` | — | balance, target, limits | **AUTHORITY-READY** |
| 20 | trade | both | `TradeIntent` | — | ownership, distance, both balances | **AUTHORITY-READY** |
| 21 | NCO vehicle economy | both | `VehicleSaleIntent` | price (server-held) | ownership, lock, both parties | **AUTHORITY-READY** |

¹ Respec is the best *first* target, not vendors: the cost is deterministic, the server already
models progression, and it needs no world sync. It proves the whole intent → validate → mutate →
reconcile loop end to end on something small.

² A casino whose outcome the client decides is a money printer. Either the server owns the RNG
or the feature is disabled.

³ **Do not build authority for every vanilla quest reward.** NCO suppresses much of vanilla
progression already. Determine per reward whether it is reachable at all — unreachable ones
reclassify to **D** and disappear from this table. This can cut the work substantially and it
has not been done.

**The rule for every row:** the client may claim *which vendor, which item, which access point* —
context that identifies a gameplay event. The client must **never** decide the amount, the price,
the payout, or the resulting balance.

### 5.3 What every money intent needs

```
authenticated character   (from the session, never the message)
idempotency key           (server-scoped; replay returns the original result)
source category           (vendor / reward / travel / …)
source entity identity    (vendor id, access point id, machine id)
claimed context           (item, quantity, route — never price or amount)
client's observed revision (diagnostic only; NEVER trust)
```

And per §3.12–14: bound sizes and rates before any work; record only after persistence.

### 5.4 The observation trap

A redscript hook reporting "the player bought X for 500" is **observation, not authority**. The
player controls the client; a modified one forges the same event.

Where the server cannot independently validate, say so plainly:

```
SERVER MODEL REQUIRED       or      FEATURE MUST BE DISABLED FOR AUTHORITATIVE MODE
```

A shared internal observation format is fine as plumbing. **`observed_delta` must never become
"server applies delta".**

### 5.5 Vendors need a server model

Vendors are the dominant money path in both directions, and they are *not* a quick hook. To
validate a purchase the server needs vendor identity, the item, the quantity, the **real price**
(vendor prices vary by reputation, stock and modifiers), stock levels, the player's
authoritative balance, and proximity — plus one-shot semantics.

**Classify vendors as NEEDS SERVER MODEL.** Do not fake authority by trusting a more detailed
client message.

### 5.6 Vanilla vehicle shop — recommend disabling

NCO has its own vehicle ownership and sale economy. `vehicleShopGameController` is a second,
unrelated one that spends vanilla eddies and grants vanilla vehicles the server does not model.

**Recommendation: disable the vanilla path** rather than reproduce the entire vanilla shop to
preserve it. Two unrelated ownership systems coexisting by accident is a duplication surface and
a support problem. **Needs Cam's decision.**

### 5.7 Sources recommended for disabling

| Source | Why |
|---|---|
| console / mod grants | never a legitimate player money source |
| vanilla vehicle shop + Yaiba showroom | competes with NCO's own vehicle economy (§5.6) |
| casino | unless the server owns the RNG |
| unreachable quest rewards | reclassify to D with evidence; do not build authority for them |

---

## 6. Inventory authority — deferred, and why

**Do not attempt this in the same phase as money.**

**A. Mutation observation.** No vanilla inventory operation is observed. Loot, vendors, crafting,
dismantling, stash, consumables, pickups, equipment, cyberware, quickhacks all need staged
authoritative handling.

**B. Item-instance fidelity — the blocker.** §1.5. Before any authoritative inventory, audit what
instance data the game actually exposes through script and decide what NCO must persist. **Do not
invent an `ItemInstanceID` before mapping the game APIs.**

**C. Reconciliation.** Quantity reconciliation in both directions already works (§1.7). What is
missing: continuous rather than spawn-time-only, and instance-aware.

**D. Equipped-item safety.** The current strip already had to protect `RightArm`, `LeftArm`,
`BaseFists` after removing a player's arms. Equipped state is not stored; cyberware is re-equipped
on restore, ordinary equipment is not.

**E. Duplicate stacks.** Client saves can produce two stacks of one id; only the first is
reachable. **Policy: canonicalize at migration** — inspect, report, sum, review, then merge
deterministically preserving exact total. If a sum would exceed `uint32`: **block that character
and report — never clamp, wrap, or discard.**

**F–J.** Source validation per §5.4; persistence must carry whatever the item model decides;
reconnect must converge without duplicating (the 19 Aug incident doubled 124 stacks by adding
totals instead of differences — **always reconcile to a difference**); the duplication threat
model is the reconnect and trade paths.

---

## 7. Security requirements

1. Authority derives from the **authenticated session**, never from message contents.
2. The client supplies **intent and context**; the server determines the **effect**.
3. Idempotency on every state-changing intent; replay returns the original result and mutates
   nothing.
4. Rate and size limits **before** expensive work.
5. Explicit enum validation (§1.9).
6. One active session per CharacterID.
7. Audit every authoritative change with both before and after values.
8. Migration is irreversible in the direction that matters — inspect and report before
   committing, and refuse unless every record is clean.

---

## 8. Live test requirements

Cannot be covered by `tools/Verify.ps1`; all need a running game, most need two clients.

- two-client duplication attempts on every supported money path
- disconnect / reconnect convergence (no duplication, no loss)
- forged balance declarations refused, authoritative value restored
- replayed intents mutate once
- stale saves after an authoritative change
- vendor buy/sell under authority
- reward one-shot semantics (access points, bounties, world pickups)
- client balance always returns to the authoritative value
- movement coalescing (built, flag OFF, never tested with two clients)

---

## 9. Deferred systems

| System | State |
|---|---|
| Full inventory authority | own workstream (§6) |
| Character selector | **parked** — needs auth, identity, CharacterID, persistence, session lock, authoritative loading on the new server first |
| Character session lock | required, not built |
| Duplicate-stack canonicalization | policy decided (§6E), not implemented |
| Movement coalescing | built, flag OFF, needs a 2-client test |
| Phone `request_id` | `RequestLedger` is built and inert, waiting for a wire field |
| Vanilla vehicle shop decision | needs Cam (§5.6) |
| Quest-reward reachability audit | not done; would shrink §5.2 (§5.2 note 3) |

---

## 10. Paused work, preserved

The Stage 6 metadata rename was implemented and then **reverted unapplied** when the freeze
landed. It is not lost:

```
C:\Users\Cam\nco-backup\paused-work\stage6-metadata-rename-PAUSED.patch
C:\Users\Cam\nco-backup\paused-work\legacymetadata_test.cpp
```

It contains the field rename, the money-gate semantics (item-only trades do not advance a money
revision), legacy-metadata detection, and the tests. **It targets the outgoing server.** Use it
as a reference for §5.1, not as a patch to apply.

---

## Companion documents

| Document | What |
|---|---|
| `PHASE5-ECONOMY-AUTHORITY.md` | the original design and stage table |
| `PHASE5-STAGE6-SWAP-PLAN.md` | protocol/flag-day analysis, netpack findings |
| `PHASE5-STAGE6A-INVENTORY-AUDIT.md` | inventory source audit |
| `PHASE5-STAGE6B-MONEY-AUDIT.md` | money source audit |
| `PHASE5-STAGE6C-REACHABILITY-AND-SPECS.md` | reward reachability, `RespecIntent`, vehicle-shop disable, protocol inventory |

### Amendments from Stage 6C

- **§5.2 row 16 ("quest/script reward") is superseded.** Reward money is overwhelmingly
  **device-driven**: 12 of 14 `GiveReward` callers are world devices or NPCs. It splits into
  *world device scrap/hack rewards* (**REACHABLE**, `NEEDS WORLD SYNC`) and *quest-graph rewards*
  (**UNKNOWN**, blocker).
- **NCO's quest suppression does not stop quest rewards.** `Quests.reds` says so itself — the
  engine still runs; only presentation is silenced. Nothing may assume otherwise.
- **§5.2 note 1 is now proven**: the respec cost is a closed-form function of two TweakDB
  constants and the character's spent points, and NCO already persists those inputs.
- **One open TweakDB question would shrink the matrix**: do `ExtractParts*` reward records
  contain currency, or only crafting components? Unanswered; see 6C §A.5.
- **Protocol divergence recorded**: `feat/world-state` already differs from published
  `fork/main` (character slots, phone calls, `/call` fix) from older commits. Treat both as
  historical input, never as the target protocol.

---

**Netcode frozen. Production `.proto` untouched. Migration inactive. Selector parked. Nothing
pushed.**
