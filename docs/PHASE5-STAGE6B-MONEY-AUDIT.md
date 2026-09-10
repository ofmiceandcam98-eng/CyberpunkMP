# Stage 6B — money mutation source audit

**READ-ONLY. No authority change, no protocol change, no migration, no hooks added.**
Completed 2026-09-05 (Cam stream).

---

## The headline

**Money-only authority is NOT safe today, for the same reason inventory is not.** The review
was right to demand this audit before approving the split, and right to distrust the claim.

**I wrote "money is already mutated only through `Economy::`". That was wrong**, and the way it
was wrong matters: it is true of the *server's* mutation paths, and I let it stand as though it
described every path. Money in Cyberpunk is an **inventory item** —
`transaction.GetItemQuantity(player, MarketSystem.Money())` — read and written through the same
`TransactionSystem` I had just finished proving is completely unobserved.

Proven from the game's own shipped source, not inferred:

```
vendor.script:1180
    transactionSystem.TransferItem( buyer, seller, MarketSystem.Money(), totalPrice, , , true );
```

Vendor buy and sell move eddies through the same item the mod reads and nothing hooks.

**But money is still meaningfully different from inventory**, and the difference is what makes
the split worth keeping — see §5.

---

## 1. The money mutation map

Sources marked **game source** were traced in
`<game>\tools\redmod\scripts\`. Sources marked **repo** were traced in this repository.

| # | Source | Reachable | Value decided by | Server observes? | Learns only via `.money`? | Class |
|---|---|---|---|---|---|---|
| 1 | **vendor purchase** | yes | vanilla client | **no** | **yes** | **C** |
| 2 | **vendor sale** | yes | vanilla client | **no** | **yes** | **C** |
| 3 | ripperdoc / cyberware purchase | yes (vendor path) | vanilla client | no | yes | **C** |
| 4 | weapon / clothing shop | yes (vendor path) | vanilla client | no | yes | **C** |
| 5 | consumable purchase | yes (vendor path) | vanilla client | no | yes | **C** |
| 6 | selling junk / items | yes (vendor path) | vanilla client | no | yes | **C** |
| 7 | **access point / hacking reward** | yes | vanilla client | no | yes | **C** |
| 8 | **bounty reward** | yes | vanilla client | no | yes | **C** |
| 9 | **Delamain taxi fare** | yes¹ | vanilla client | no | yes | **C** |
| 10 | **perk respec cost** | yes | vanilla client | no | yes | **C** |
| 11 | **vending machines** | yes | vanilla client | no | yes | **C** |
| 12 | **vanilla vehicle shop** (in-game internet) | yes² | vanilla client | no | yes | **C** |
| 13 | Yaiba showroom computer | yes² | vanilla client | no | yes | **C** |
| 14 | casino table | yes | vanilla client | no | yes | **C** |
| 15 | world eddie pickups / loot | yes³ | vanilla client | no | yes | **C** |
| 16 | quest / scripted rewards | yes | vanilla client | no | yes | **C** |
| 17 | console / mod grants | yes | client | no | yes | **C** |
| 18 | **starter kit money** | yes | **server** | yes — server-initiated | no | **A** |
| 19 | **`/pay`** | yes | **server** | yes — `Economy::Transfer` | no | **A** |
| 20 | **trade money** | yes | **server** | yes — `ApplyTrade` → `MoveAssets` | no | **A** |
| 21 | **NCO vehicle sale** (`/sellcar`+`/buycar`) | yes | **server** | yes — `CompleteSale` | no | **A** |
| 22 | crafting cost | n/a — crafting consumes components, not eddies | — | — | — | — |

¹ Delamain taxi exists in vanilla; whether it is reachable in NCO's world state was not walked
in game. Marked reachable because nothing disables it.
² **This is a second, vanilla vehicle purchase path entirely separate from NCO's `/sellcar`
flow** — `vehicleShopGameController.script:433` does
`transactionSystem.RemoveItem(player, MarketSystem.Money(), amount)`. Worth knowing on its own.
³ Direct evidence in our own code: the `[MONEY]` boundary comments in `Inventory.reds:63-79`
were written to chase "I found 84 eddies and they did not save".

### Class counts

| Class | Count |
|---|---|
| **A** — already server authoritative | **4** |
| **B** — client event the server can validate today | **0** |
| **C** — vanilla mutation, currently unobservable | **17** |
| **D** — unreachable / disabled | **0** |

### Every money-mutating script in the game (repository search, item 25)

```
cyberpunk/systems/marketSystem/vendor.script          buy / sell
cyberpunk/systems/marketSystem/marketSystem.script    price / money helpers
cyberpunk/systems/playerDevelopmentSystem.script      respec cost
cyberpunk/managers/bountyManager.script               bounty reward
cyberpunk/devices/masters/accessPointController.script  hacking reward
cyberpunk/devices/vendingMachines/iceMachineController.script
cyberpunk/devices/UI/internet/vehicleShopGameController.script
cyberpunk/devices/UI/computer/computerYaibaShowroom.script
cyberpunk/devices/recreation/casinoTable.script
core/systems/delamainTaxiSystem.script                taxi fare
```

**In this repository, the only money mutations are the four Class A paths plus
`ApplyServerMoney`.** Nothing else in NCO touches eddies.

---

## 2. Direct answers to the review's questions

**4. Does vendor buy change local money?** **Yes.** `vendor.script:1180`, `TransferItem` on
`MarketSystem.Money()`. Proven from game source.

**5. Does vendor sale change local money?** **Yes.** Same call, buyer/seller roles reversed.

**6. Can loot/rewards change money?** **Yes.** Access point hacking rewards
(`accessPointController.script`), bounty rewards (`bountyManager.script:222` —
`GiveItem(player, MarketSystem.Money(), bounty.m_moneyAmount)`), and world eddie pickups.

**7. Does any reachable vanilla system bypass `Economy::`?** **Yes — seventeen of them.**
`Economy::` governs the server's own mutations. It has no reach into the client's game at all.

**8. Is money-only authority actually safe?** **No, not today.** If the server ignores
`SaveCharacterRequest.money` for a migrated character, a player who sells loot to a vendor,
completes a bounty, or hacks an access point would have those eddies erased at the next
authoritative resync. That is the identical failure mode the inventory audit found, and it is
not a smaller one — vendors are the primary money sink and source in this game.

---

## 3. Two corrections to the Stage 6A audit

Both are mine, and both matter.

### 3.1 Inventory reconciliation IS bidirectional — my "additive only" finding was wrong

I reported that restore "can add but never take away", from reading `Inventory.reds:230-300` and
stopping before the rest of the function. **There is a removal pass at `:374-535`**, headed *"Take
back what the server does NOT say you own … This is what makes the restore AUTHORITATIVE instead
of additive."*

```reds
let owned  = MpInventory.ServerWants(network, TDBID.ToNumber(heldTdbid));
let excess = transaction.GetItemQuantity(player, heldId) - owned;
if excess > 0 { /* deferred, then RemoveItem */ }
```

Gated on `IsCharacterStatusKnown()` — the server having answered — not on first spawn. Excluded
from removal: money (settled separately), a currently-empty `protectedIds` list, and body slots
(`RightArm`, `LeftArm`, `BaseFists`).

**So the review's requirement C — bidirectional reconciliation — is substantially already built
for inventory.** It reconciles both directions on quantity, at restore time. What it does *not*
do is run continuously, and it still cannot reconcile instance state, because there is none.

I got this wrong by not reading to the end of a 300-line function before drawing a conclusion
from it. The Stage 6A document has been corrected.

### 3.2 Money has *better* enforcement than inventory, not worse

`ApplyServerMoney` (`World/NetworkWorldSystem.reds:370`), called from every `NotifyMoney`:

```reds
let owed = target - held;
if owed > 0 { GiveItem(player, Money(), owed); }
else if owed < 0 { RemoveItem(player, Money(), -owed); }
```

**Unconditional, both directions, live** — not gated on spawn, first-spawn, or anything else.
Whenever the server says a balance, the client's game is set to exactly that, up or down. The
four Class A paths already do this through the five `PushMoney` call sites.

The spawn-time money restore in `Inventory.reds:333-370` is separately gated: it only *removes*
on `firstSpawn`, deliberately, under Cam's rule that money is only ever taken at creation.

**Finding 1 (no item instance state) stands unchanged**, and is untouched by any of this.

---

## 4. Metadata audit — every current use

### `EconomyRevision`

| Site | Use |
|---|---|
| `CharacterRecord.h:423`, `:457` | declaration + JSON serialization |
| `EconomyMigration.h:111,116,164` | migration state classification; `Apply` sets it to 1 |
| `EconomyMutator.h:284` | `IsMigrated` |
| `EconomyMutator.h:304,329,332` | `CanAdvanceRevision` / `AdvanceRevision` |
| `EconomyMutator.h:360,363` | `ClassifyClientRevision` |
| `ChatSystem.cpp:525,529` | the Stage 5 observation — one log line and one audit field |
| 4 transaction boundaries | advanced once per participant (trade, `/pay`, starter kit, vehicle sale) |

### `MigratedAt`

| Site | Use |
|---|---|
| `CharacterRecord.h:441`, `:457` | declaration + serialization |
| `EconomyMigration.h:110,163` | classification; `Apply` stamps it |
| `EconomyMutator.h:284` | `IsMigrated` |

### `ClassifyClientRevision`

**Zero production uses.** One comment in `ChatSystem.cpp:517` and the tests. Entirely dormant,
by design — it has nothing to classify until a wire field exists.

### The conclusion that makes this cheap

**No production code makes an authority decision on `EconomyRevision` or `MigratedAt`.** Nothing
gates behaviour on them. Their only consumers are migration classification, revision
advance/headroom, and one observation log line.

**And no production migration has ever occurred** — every live record is `(0, 0)`, and both
fields default to 0 when absent from JSON. So the meaning of these fields can still be corrected
at essentially zero cost. That window closes the moment migration runs.

---

## 5. Recommended metadata model

**Recommendation: Option B — separate the metadata now, before the flag day.**

```
MoneyRevision       (was EconomyRevision)
MoneyMigratedAt     (was MigratedAt)

// reserved, not implemented:
InventoryRevision
InventoryMigratedAt
```

**Why B over A (redefine the existing fields as money-only):**

Option A leaves a name that says "economy" meaning "money", and there is a concrete way it
becomes a lie. `ApplyTrade` advances the revision for a trade — and `trade_real_test` covers
*item-only* trades explicitly (*"an item-only trade succeeds - zero money is not an error"*). A
field called `MoneyRevision` advancing on a trade where **no money moved** is wrong. That has to
be decided deliberately, and the honest answer is that a money revision advances when money
changes.

The rename is safe precisely because of §4: records missing the new keys default to 0, every
live value is already 0, so old and new agree exactly. The one thing to update alongside is
`economyfields_test`, which asserts the JSON key names.

**Wire naming follows the same rule.** Field names are hashed, so this is the moment:

```
money_revision      not      economy_revision
```

And **`NotifyPossessions.money_revision` versions the money field in that message, not the
inventory payload.** The name has to make that unambiguous, because that message carries both
and only one of them is authoritative versioned state. The review's warning — do not ship one
revision number that falsely implies the inventory payload is versioned authoritative state — is
exactly right, and naming is how it is avoided.

**Not implemented. Recommendation only.**

---

## 6. Revised Stage 6 protocol surface

Unchanged in shape, renamed for truth:

```
CLIENT -> SERVER
    SaveCharacterRequest.money_revision          uint64

SERVER -> CLIENT
    SpawnCharacterResponse.money_revision        uint64
    NotifyPossessions.money_revision             uint64   (versions the money field only)
    NotifyMoney.balance                          int32 -> int64
    NotifyMoney.money_revision                   uint64
```

Still four field additions and one widening. Still one identifier change.

**But it should not be built yet**, because §2.8 says money authority is not safe today, and the
protocol surface exists to serve that authority. Building it now is not harmful — every field is
needed under any outcome — but it would be a flag day spent ahead of the decision that gives it
purpose. See §8.

---

## 7. Revised Stage 7 and 8

**Stage 7 — dormant, migration-gated, and now money-scoped:**

```
if character is UNMIGRATED:      legacy behaviour, unchanged
if character is MIGRATED:        client Money ignored; server Money retained;
                                 revision classified; authoritative Money resynced
                                 INVENTORY still accepted under the legacy model
```

`SaveCharacterRequest.money` and `.inventory` both **stay on the wire**. Field deletion is not
the security boundary.

**Stage 8 — money-only cutover**, migration and authority as one atomic boundary, enforcement
activating at the instant the record migrates. Duplicate-stack canonicalization is **not** part
of it: if only money crosses, inventory metadata must not be stamped as though it had.

**Blocked on the Class C money problem.** Stage 8 cannot run until the 17 Class C sources are
each hooked, accepted, or disabled.

---

## 8. What the money problem actually needs

Money's Class C list is **bounded and enumerable** — 10 game scripts, ~17 player-facing paths —
where inventory's is effectively unbounded. That is the real difference, and it is why the split
still makes sense even though neither half is ready.

Three ways forward, for review:

| Option | What | Cost |
|---|---|---|
| **1. Hook the money sources** | Wrap the ~10 game scripts that mutate `MarketSystem.Money()`, report each as a validated intent | Bounded, but real. Vendors alone are a substantial feature |
| **2. Reconcile rather than refuse** | Keep accepting the client's balance, but bound *how much* it may move per interval and audit the rest | Cheap, catches bulk duplication, does not stop patient cheating |
| **3. Accept money as client-declared** | Keep today's behaviour; rely on the impossible-value refusal already shipped | Free, no security gain |

**My recommendation is option 1, scoped to vendors first** — they are the dominant money path in
both directions, they are a single script, and `ApplyServerMoney` already provides the
enforcement half. But it is a feature-sized piece of work, not a Phase 5 finishing move.

**Phase 5's honest end state:** stages 1-5 built the machinery — atomic persistence, the mutation
boundary, revision semantics, migration mechanism. Stages 6-8 as originally imagined require
observation work that does not exist for either money or inventory. **The machinery is done; the
authority cutover is a new phase, and it needs vendors before it needs a protocol.**

---

## 9. Inventory authority — recorded as a separate workstream

Three problems, none solvable inside Phase 5:

**A. Mutation observation.** No vanilla inventory operation is observed. Needs staged
authoritative handling for loot, vendors, crafting, dismantling, stash, consumables, pickups,
equipment, cyberware, quickhacks. **Do not build these now.**

**B. Item fidelity.** `ItemStack` is `(TweakDBID, quantity)`; `Capture` → that, `Restore` →
`GiveItemByTDBID`. Cannot represent modifications, attachments, upgrades, quality/tier, crafted
state, generated rolls, or iconic/unique state. **Phase 5 must not pretend the current
serialization is lossless — it is not.** Before authoritative inventory, audit what item-instance
data the game actually exposes and decide what NCO needs to persist. **Do not invent an
ItemInstanceID before mapping the game APIs.**

**C. Bidirectional reconciliation.** **Substantially already built** (§3.1) — quantity in both
directions, at restore. What is missing: continuous reconciliation rather than spawn-time only,
protection of equipped items and legitimate instance state, and any notion of *which* instance to
remove when several share an id.

---

**Migration inactive. Production `.proto` untouched. No hooks added. Selector parked. Nothing
pushed.**
