# Stage 6A — vanilla inventory source audit

**READ-ONLY. No behaviour changed, no hooks added, no production `.proto` touched, migration
inactive.** Completed 2026-09-05 (Cam stream).

Every claim below is traced to real code. Where something could not be established from the
repository, it says so rather than guessing.

---

## The finding, before the table

**There is exactly one channel by which the server learns anything about a player's
possessions, and it is the client's snapshot.**

Searching every redscript hook in the project:

```
$ grep -rn "@wrapMethod\|@replaceMethod" code/assets/redscript/*.reds
TimeDilationHelper · ScriptedPuppet · JournalNotificationQueue · PhoneHotkeyController
PauseMenuBackgroundGameController · SingleplayerMenuGameController · VehicleObject
TimeskipGameController · PlayerPuppet · Scanner/GameObject · PhoneSystem
IncomingCallLogicController · HudPhoneGameController · SocialPanelContactsList
NewHudPhoneGameController · DeathMenuGameController
```

**Not one hook on `TransactionSystem`, `EquipmentSystem`, any vendor, crafting, loot, stash, or
container system.** `Inventory.reds` contains no hooks at all — it is `Capture` and `Restore`,
called on demand. On the C++ side the only `ITransactionSystem` use is
`AppearanceSystem.cpp:236`, `GiveItem` for **remote puppets' clothing** — cosmetic, on other
players' bodies, not the local player's persistent inventory.

So every vanilla way of gaining or losing an item happens entirely inside the client's game,
unobserved, and reaches the server only when the next snapshot happens to include the result.

**And nothing is disabled.** No vendor, shop, crafting, loot, or stash system is blocked
anywhere in the project. `Quests.reds` suppresses quest *notifications* and phone calls; it does
not touch quest logic or rewards.

### How the snapshot reaches the server

`SaveCharacterRequest`, built at `NetworkWorldSystem.cpp:1168` and `:1622`, populated at
`:1201-1202` / `:1655-1656` after a `CallVirtual("CaptureInventory")` round trip into
`MpInventory.Capture`. Triggers:

| Trigger | Site |
|---|---|
| **90-second autosave** | `:3816` — `system("Possessions autosave").interval(90.f)` |
| disconnect | `:3716` |
| world detach (quit to desktop/menu) | `:2091` |
| character creator closing | `:3002` |
| first capture for an unknown character | `:2953` |
| server asks (`OpenCharacterCreator` capture_only) | `:2631` |

Guarded by `m_restorePending` and `MaySaveCharacter()` so a capture cannot run before the
server's items have landed.

---

## Two findings that were not on the brief

Both are load-bearing for Stages 7 and 8, and neither is about *which* sources exist.

### Finding 1 — the server's item model has no instance state

`Capture` stores `TDBID.ToNumber(tdbid)` and a quantity (`Inventory.reds:95-102`).
`Restore` gives items back with `GiveItemByTDBID(player, tdbid, owed)` (`:296`).

That is an item **type** and a **count**. It does not carry:

```
attachments / mods      quality tier        upgrade level
crafted stat rolls      iconic state        durability or per-instance data
```

A fully modded, upgraded weapon and a freshly bought one are the same `(id, quantity)` to this
server, and a restore hands back the **base item**.

**This is already a live data-loss path today**, independent of anything in Phase 5. Any
restore that has to re-grant an item returns a stripped version of it. It bears directly on
Cam's standing rule — *"make sure any new weapon, clothing, cyberware, money or any item a
person grabs or buys stays on them, it shouldnt disappear"* — because the item does not
disappear, but what made it valuable can.

Whether it bites often today depends on how often a restore actually re-grants rather than
finding the item already held; that needs a live session to measure, which this audit could not
do.

### Finding 2 — ~~restore can add, but it cannot take away~~ **CORRECTED 2026-09-05: WRONG**

**This finding was wrong and is retracted.** It is left here rather than deleted because it was
reported to review and acted on, and because the way it was wrong is worth not repeating.

What I wrote: that restore only applies a positive difference (`Inventory.reds:293-297`), so the
server could never reconcile a client downwards, and *"server possessions win" has no mechanism
today*.

**What is actually there.** I read lines 230-300 and drew a conclusion about a function that
runs to line 535. The removal pass is at `:374`, headed *"Take back what the server does NOT say
you own … This is what makes the restore AUTHORITATIVE instead of additive, and it is the half
that was missing."*

```reds
let owned  = MpInventory.ServerWants(network, TDBID.ToNumber(heldTdbid));
let excess = transaction.GetItemQuantity(player, heldId) - owned;
if excess > 0 { /* collected, then RemoveItem after the loop */ }
```

Gated on `IsCharacterStatusKnown()` — the server having answered — not on first spawn. Excluded:
money (settled separately as a balance), a currently-empty `protectedIds` list, and body slots
(`RightArm`, `LeftArm`, `BaseFists`, after the strip took Cam's arms).

**So bidirectional reconciliation is substantially already built**, on quantity, in both
directions, at restore time. What is genuinely missing is narrower than I claimed: it runs only
at restore rather than continuously, and it cannot reconcile *instance state*, because Finding 1
means there is none to reconcile.

**Finding 1 is unaffected and stands.**

See `docs/PHASE5-STAGE6B-MONEY-AUDIT.md` §3 for the same correction alongside the money
enforcement path, which turns out to be stronger still: `ApplyServerMoney` sets the balance in
both directions, unconditionally, on every `NotifyMoney`.

---

## The map — every source, four answers

**B** = who decides the result. **C** = how the server learns. **D** = what is required before
Stage 8.

| # | Source | A. Reachable? | B. Decided by | C. Server learns via | D. Before Stage 8 | Class |
|---|---|---|---|---|---|---|
| 1 | world loot | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 2 | corpse / NPC loot | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 3 | containers | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 4 | pickups | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 5 | vendor purchase | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 6 | vendor sale | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 7 | weapon pickup | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 8 | clothing pickup | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 9 | consumable use | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 10 | drop / discard | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 11 | crafting | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 12 | dismantling | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 13 | stash / container movement | yes | vanilla client | snapshot only | authority path or accept | **C** |
| 14 | apartment stash | yes (same system) | vanilla client | snapshot only | authority path or accept | **C** |
| 15 | equipment changes | yes | vanilla client | snapshot only¹ | authority path or accept | **C** |
| 16 | cyberware (ripperdoc) | yes | vanilla client | snapshot only² | authority path or accept | **C** |
| 17 | quickhacks / programs | yes (inventory-backed) | vanilla client | snapshot only | authority path or accept | **C** |
| 18 | ammo | yes | vanilla client | snapshot only³ | authority path or accept | **C** |
| 19 | quest / script rewards | yes⁴ | vanilla client | snapshot only | authority path or accept | **C** |
| 20 | console / mod grants | yes⁵ | vanilla client | snapshot only | out of scope — see note | **C** |
| 21 | **starter kit** | yes | **server** | server-initiated | already authoritative | **A** |
| 22 | **trade** | yes | **server** | `/trade` → `ApplyTrade` | already authoritative | **A** |
| 23 | **`/pay`** (money) | yes | **server** | `/pay` → `Economy::Transfer` | already authoritative | **A** |
| 24 | **vehicle sale** (money) | yes | **server** | `/sellcar` → `/buycar` → `CompleteSale` | already authoritative | **A** |
| 25 | other Inventory API mutation | — | — | — | none found; see below | — |

¹ Equipped items remain in the `TransactionSystem` list, so they are captured as items. Their
*appearance* is synchronised separately (`Capture`'s own note: equipped clothing "is already
synchronised by the appearance path"). Equip state itself is not stored — cyberware is re-equipped
on restore by `EquipCyberware`, ordinary equipment is not.

² Cyberware is not a separate model: `GetItemListByTag(player, n"Cyberware", …)`
(`Inventory.reds:850`) reads it out of the same inventory. It captures, stores, and restores as
an ordinary item, then `EquipCyberware` re-slots it.

³ Ammo appears in the item list like any other item and is therefore captured, but consumption
during play is only reflected at the next snapshot.

⁴ `Quests.reds` suppresses notifications only. Quest logic and rewards are untouched. The
Phantom Liberty prologue was removed at the gamedef level (`docs/MAP.md`), which removes *those*
quests, not questing.

⁵ Reachable by anyone who can run console commands or install mods. Listed for completeness; it
is a client-integrity problem, not an inventory-architecture one, and it is not solvable by an
authority path — it is solved by the server not trusting the client, which is the whole point of
the cutover.

### Class summary

| Class | Count | Sources |
|---|---|---|
| **A** — already server authoritative | 4 | starter kit, trade, `/pay`, vehicle sale |
| **B** — client event the server can validate today | **0** | — |
| **C** — client event the server cannot currently validate | 16+ | everything in rows 1-20 |
| **D** — not reachable in NCO | **0** | nothing is disabled |

**Class B is empty, and that is the important result.** A source can only be Class B if the
server *observes* it, and no vanilla inventory event is observed at all. Vendor purchase is the
natural Class B candidate — the server could validate vendor, item, and price — but there is no
hook, no event, and no message. It would have to be built from nothing, not integrated.

**Search for "any other Inventory API mutation" (row 25):** the only `TransactionSystem` callers
in the project are `MpInventory.Capture`, `MpInventory.Restore`, `EquipCyberware`,
`MpSettleStarterLoadout`, and `AppearanceSystem.cpp` (remote puppets' clothing). Nothing else
mutates a player's inventory.

---

## What blocks Stage 8

**Every Class C source blocks Stage 8 in its current form**, for one shared reason: after
migration, `SaveCharacterRequest.inventory` stops being believed, and that is the *only* way any
of them currently reaches the server. A player who loots a weapon, buys a jacket, crafts a
grenade or installs cyberware would have the server's older record win at the next resync.

This is precisely the failure the review named: *"we would eliminate duplication while
accidentally making legitimate items disappear after reconnect."* The audit's answer is that
this would not be an edge case — **it would be every non-server-mediated item in the game.**

Three ways forward. They are not exclusive, and the choice is Cam's:

| Option | What it means | Cost |
|---|---|---|
| **1. Observe** | Hook the vanilla systems and report validated intents | Large. 16+ sources, each needing a hook, a message, and server validation. Multiple flag days |
| **2. Accept** | Keep believing the client's *inventory* for migrated characters; make only **money** authoritative | Small. Money is where the exploit value is, and it is already server-mediated in all four Class A paths |
| **3. Disable** | Turn off vanilla acquisition | Unacceptable — it is the game |

**Original recommendation: option 2, split the cutover** — on the grounds that money was
"already mutated only through `Economy::`" and so could be made authoritative for free.

> **CORRECTED 2026-09-05. That justification was wrong.** Money is an inventory item
> (`MarketSystem.Money()`), read and written through the same unobserved `TransactionSystem`.
> Vendor buy and sell move it — proven from the game's own source,
> `vendor.script:1180` — along with sixteen other vanilla paths.
> **Money is not server-authoritative today either.** See
> `docs/PHASE5-STAGE6B-MONEY-AUDIT.md`.
>
> **Splitting money from inventory is still the right architecture**, but for a different and
> smaller reason: money's unobserved sources are *bounded and enumerable* (10 game scripts,
> ~17 paths) where inventory's are effectively unbounded, money is a scalar with no instance-state
> problem, and its enforcement path (`ApplyServerMoney`) is already live and unconditional in both
> directions. Money is the tractable half — not the free one.

**This is a recommendation, not a decision, and it changes Phase 5's shape enough that it needs
explicit review before anything is built.**

---

## Consequence for the Stage 6 protocol surface

The review asked whether the audit discovers a legitimate vanilla path requiring a new typed
authoritative intent, and said to bring it back **before** the flag day.

**It does not — not yet, and deliberately not.**

Building intent messages for Class C sources requires first deciding *which* sources become
authoritative (the option-2 question above). Minting them now would guess at that answer, and a
guessed message is permanent protocol surface.

So the minimum surface stands unchanged and is still correct:

```
CLIENT -> SERVER
    SaveCharacterRequest.economy_revision

SERVER -> CLIENT
    SpawnCharacterResponse.economy_revision
    NotifyPossessions.economy_revision
    NotifyMoney.balance   int32 -> int64
    NotifyMoney.economy_revision
```

Every one of these is needed under **all three** options above, because all three keep money
authoritative and keep the revision meaningful. Nothing here is speculative surface.

**If option 2 is chosen, this surface is very likely final** — money enforcement needs no new
client request, because all four money paths are already Class A and chat-driven. If option 1 is
ever chosen for a specific source, that source's intent message is designed then, with the
observation it requires, as its own reviewed change.

---

## What this audit could not establish

Stated rather than glossed:

- **How often a restore actually re-grants** (Finding 1's blast radius). Needs a live session.
- **Whether `GetItemList` includes equipped items in every case.** The code treats it as the
  complete list and the equipped-appearance note implies it does, but this was not proven by
  observation.
- **Whether any vendor is unreachable for world-state reasons** in the template save — no code
  disables vendors, but reachability in the actual world was not walked in game.
- **Item instance state** is out of the model entirely; whether the game exposes enough through
  script to capture it was not investigated, because nothing today attempts to.

---

**Migration inactive. Production `.proto` untouched. No hooks added. Selector parked. Nothing
pushed.**
