# Phase 5 — server-authoritative money and inventory

**Status: DESIGN ONLY. Nothing here is implemented.** Written 2026-09-04 (Cam stream) as the
map to review before anything touches the live economy.

Every claim below was read out of the code, not assumed. Where something is inferred rather
than verified, it says so.

---

## Executive summary

The economy is **not** broadly client-authoritative. It is server-authoritative almost
everywhere, with **exactly one client-authoritative door**, and that door is
`ChatSystem::HandleSaveCharacterRequest`:

```cpp
character.Money = aMessage.get_money();                        // ChatSystem.cpp:551
character.Inventory.push_back({stack.get_id(), stack.get_quantity()});   // :498
```

Trade, `/pay`, vehicle sales and starter kits are all already server-owned and validated.
That matters for scoping: **Phase 5 is closing one door, not rebuilding an economy.**

Three findings change how the work should be sequenced:

1. **The hole is a single entry point.** Eight money-write sites exist; seven are
   authoritative. Three inventory-write sites exist; two are authoritative.
2. **Persistence is not atomic.** `PlayerStore::Flush` truncates `players.json` and streams
   into it. A crash mid-write corrupts **every character on the server**. No transaction
   model is safer than the write beneath it, so this must be fixed *first*, and it is fixable
   independently and safely.
3. **There is no shop, vendor, crafting or admin-grant system.** The economy's entire surface
   is: starter kit → `/pay` → trade → vehicle sale. The migration is far smaller than the
   brief anticipates.

---

## Current authority model — verified

| State | Current authority | Desired | Notes |
|---|---|---|---|
| Money — trade | **Server** | Server | `PlayerStore::MoveAssets`, atomic on copies |
| Money — `/pay` | **Server** | Server | `ChatSystem.cpp:3615` |
| Money — vehicle sale | **Server** | Server | `:1747`, with rollback at `:1758` |
| Money — starter kit | **Server** | Server | `:716`, gated by `StarterKitGranted` |
| **Money — character save** | **CLIENT** | **Server** | `:551` — the hole |
| Inventory — trade | **Server** | Server | `PlayerStore.h:1114/1153` |
| Inventory — starter kit | **Server** | Server | `:710/714` |
| **Inventory — character save** | **CLIENT** | **Server** | `:487/498` — the hole |
| Ammo / magazine | **Server** | Server | `WeaponComponent` |
| Vehicle ownership | **Server** | Server | `VehicleStore` owns `OwnerId`/`Transfer` |
| Trade reservations | **Server** | Server | preserve as-is |

**No admin economy commands exist.** `/pay` is player-to-player; there is no `/givemoney`,
`/setmoney`, `/giveitem` or `/removeitem`. Nothing to preserve, and nothing to migrate.

---

## The item model, as it actually is

```cpp
struct ItemStack {
    uint64_t Id{0};        // TweakDBID
    uint32_t Quantity{1};
};
```

**Items are fungible.** There are no instance ids, no durability, no attachments, no
per-weapon state, no stack identity. Two stacks with the same `Id` are interchangeable.

`CharacterRecord` has **no separate Equipment field** — the persisted shape is `Slot, Name,
Appearance, IsMale, Level, …, Inventory, Money, Proficiencies, Attributes, Perks, Vehicles,
PhoneNumber, Contacts, …`. What the character is *wearing* rides inside the appearance blob,
not the inventory.

**Consequence for Phase 5:** a server ledger over fungible stacks is genuinely simple — it is
a map from `Id` to a count. The hard version of this problem (unique instances, durability,
mods) **does not exist yet**, and Phase 5 should not invent it. Adding `ItemInstanceID` now
would be building for a game we do not have.

---

## Persistence — the foundation problem

```cpp
std::ofstream file(m_path);                 // truncates players.json in place
file << nlohmann::json(m_records).dump(2);  // then streams into it
```

- **Not atomic.** A crash, power loss or full disk between those two lines leaves a truncated
  or half-written `players.json` — that is *every* character, not one.
- **No backup.** Nothing keeps the previous good copy.
- **Whole-file.** Every save rewrites all records; cost grows with the player base.

**This blocks the entire design.** An atomic transaction that commits into a non-atomic write
is not atomic. It is also the one part of Phase 5 that is **safe to fix immediately** and
cannot destroy data — see *Safe to implement now*.

---

## Server ledger design

The minimum viable model, given fungible items and an existing per-character record:

```
CharacterRecord (existing, extended)
    Money            int64      — already there, becomes server-owned
    Inventory        [ItemStack] — already there, becomes server-owned
    EconomyRevision  uint64     — NEW
    MigratedAt       int64      — NEW, 0 = never
```

**Why only two new fields.**

- **`EconomyRevision`** — increments on every server-side money/inventory mutation. It gives
  three things at once: the client can be told what it is holding is stale; a rejected
  transaction can be answered with a correction the client cannot argue with; and a save
  request carrying an old revision is identifiable as pre-dating a change it did not see.
- **`MigratedAt`** — marks the moment a character's possessions stopped being client-declared.
  Without it there is no way to tell a migrated record from an unmigrated one, and no way to
  run the migration exactly once.

**Deliberately NOT added:** `ItemInstanceID` (items are fungible — no use for it yet),
separate `MoneyRevision`/`InventoryRevision` (they always change together under a
transaction, so one counter is honest and two would drift), and a transaction journal (see
*Failure model* — the atomic-write fix removes the need).

---

## Transaction model

Trade already implements the right pattern and it should be the template rather than replaced:

```
copy both records
mutate the copies
if every step succeeded → assign the copies over the originals
else → discard, originals untouched
```

That is `PlayerStore::ApplyTrade`, and it is genuinely atomic **in memory**. What it lacks is
an atomic *write*. With write-to-temp-then-rename underneath it, the pair is a complete
transaction for this architecture — **no journal or database is required**, and proposing one
would be designing for an architecture this project does not have.

### How the identifiers relate

| Concept | Answers | Needed? |
|---|---|---|
| `RequestID` (exists) | "have I already performed this request?" | Yes — retry safety |
| `EconomyRevision` (new) | "is the client's view current?" | Yes — stale-save rejection |
| `TransactionID` | "which multi-step operation is this?" | **No** — the copy-and-commit pattern makes a transaction a single function call; an id would name something that never outlives its stack frame |

`RequestLedger` handles **retries**. `EconomyRevision` handles **staleness**. They are not
substitutes: a retry is the same request twice, a stale save is a *different* request built on
an out-of-date view.

---

## What `HandleSaveCharacterRequest` should become

The client still has information the server genuinely does not — it is the only thing that can
see what Cyberpunk's own inventory did when the player looted, crafted or spent at a vendor
the server does not model.

So the target is **not** "ignore the client". It is:

```
today     client: "my balance is X"          → server writes X
target    client: "these things happened"    → server validates, applies, and replies with
                                                the authoritative result
```

Concretely, the save should carry **appearance, progression and position** — which are
genuinely client-observed and harmless — and **stop carrying money and inventory as
declarations**. Anything the client needs to report about possessions becomes an explicit
economy request (see *Proposed protocol changes*), so it is validated rather than persisted.

---

## Migration strategy

The risk is not technical. Every existing character's possessions were created under
client authority, so a strict cutover would have to decide which of them are legitimate — and
it cannot, because there is no history to check them against.

**Proposed policy: trust once, then own.**

1. On first load after Phase 5 ships, a record with `MigratedAt == 0` has its **current**
   money and inventory accepted as the opening balance, subject only to the impossibility
   bounds already in place (negative, or above 1e9).
2. `MigratedAt` is stamped and `EconomyRevision` starts at 1.
3. From that moment the server owns it, and client declarations are refused.

**Why trust-once rather than validate or reset.** Validation is impossible — there is no
ledger of how anybody earned anything, so "plausible" cannot be computed. Resetting punishes
every honest player for the design, and would be the single most damaging thing this project
could do to its players. Trust-once accepts that whatever was obtained before the line is
grandfathered, and guarantees nothing after it.

**Consequence, stated plainly:** anyone who exploited before migration keeps what they took.
That is the price of not destroying legitimate possessions, and it should be a conscious
decision rather than a side effect. The `[MONEY]` audit trail already running can be reviewed
before the line is drawn, and specific accounts corrected by hand if wanted.

---

## Threat model

| Attack | Protection after Phase 5 |
|---|---|
| Save replay | `RequestLedger` on economy requests; a save carrying a stale `EconomyRevision` is refused |
| Old inventory snapshot replayed to restore spent items | Refused — the save no longer declares inventory at all |
| Money replay | Same |
| Concurrent buy/buy | Copy-and-commit is a single call on the game thread; the second sees the first's result |
| Trade race, same item | Already handled — reservations |
| Disconnect mid-transaction | Copy-and-commit either completed before the disconnect or did nothing |
| Reconnect and retry | `RequestLedger` returns the original result |
| Two sessions, one character | **Open** — see below |

**One genuine gap:** nothing currently prevents the same character being active in two
sessions. The character-switch guard (refused while a puppet is alive) protects switching
*within* a connection, not two connections holding the same character. That is a character
session lock, it belongs with the selector work, and it is listed there rather than here.

---

## Character switching and reconnect

The dangerous sequence the brief names —

```
Character A inventory → select Character B → submit A's inventory → overwrite B
```

— is **already structurally impossible**, and for a reason worth keeping: a switch is refused
while the player has a live puppet, and a save requires one. After Phase 5 it becomes
impossible for a second reason as well, since the save no longer carries possessions.

---

## System dependencies

Every consumer of money and inventory was checked. **None of them read the client's
declaration** — they all read the persisted record:

- **Trade** — `AvailableMoney`/`HeldQuantity`, reservation-aware. No change needed.
- **`/pay`** — reads and writes the record directly. No change needed.
- **Vehicle sale** — same, with an explicit rollback path. No change needed.
- **Starter kit** — writes the record once, gated by `StarterKitGranted`. No change needed.

**This is the most important result in the document.** Phase 5 does not have to rewrite trade,
vehicles or payments. They are already correct, and they become *more* correct the moment
their input stops being client-declared.

---

## Proposed protocol changes

Only two, and both are justified by something that cannot be done without them:

1. **Remove `money` and `inventory` from `SaveCharacterRequest`.** Required — while the fields
   exist, the client can declare possessions, which is the entire problem.
2. **Add an economy request carrying an intent** (`spend`, `acquire`, `consume`) **with a
   `RequestID` and the client's `EconomyRevision`.** Required — without it the server cannot
   learn about legitimate single-player-side changes (loot, vendors it does not model) at all,
   and the practical result would be possessions that silently never persist.

Both are protocol changes and therefore flag-days. **Neither should be attempted before the
server migration is verified**, per the standing constraint.

---

## Implementation stages

Ordered so each stage is independently shippable and none can destroy data:

| Stage | What | Risk |
|---|---|---|
| **1** | **Atomic persistence** — temp file + rename, keep one backup | **None. Do this now** |
| 2 | Add `EconomyRevision` + `MigratedAt`, written but not yet enforced | None — additive fields |
| 3 | Migration on load: trust-once, stamp `MigratedAt` | Low — read path only |
| 4 | Server increments `EconomyRevision` on every existing authoritative mutation | Low |
| 5 | Refuse saves carrying a stale revision; log, do not enforce, for one build | Low — measure first |
| 6 | New economy request + `RequestLedger` integration | **Flag day** |
| 7 | Remove `money`/`inventory` from the save | **Flag day**, after 6 is proven |
| 8 | Exploit testing with two clients | Requires live test |

Stage 5 deliberately mirrors the decision already recorded in `HandleSaveCharacterRequest`
("Recorded, not refused… measure first"). That instinct was right, and this design keeps it.

---

## Safe to implement now

**Stage 1 only — atomic persistence.** Write to `players.json.tmp`, `fsync`, rename over the
target, keep the previous file as `.bak`. It cannot lose data (it strictly reduces the window
in which data can be lost), it changes no behaviour, and it is the foundation everything else
commits into.

Nothing else in this document should be built before the server migration is verified.

---

## Requires live test

- Stage 6 and 7 (both flag days)
- Stage 8 exploit testing
- Any character session lock, which belongs with the selector

---

## High-risk migration areas

- **The trust-once line.** Once drawn it cannot be re-drawn without taking possessions from
  players. Review the `[MONEY]` audit trail before drawing it.
- **Stage 7.** The moment the client stops declaring possessions, anything the server does not
  model stops persisting. Stage 6 must be proven to carry those changes *before* 7 removes the
  old path, or players will quietly lose loot.

---

## Definition of done

Phase 5 is complete when:

- [ ] `players.json` cannot be corrupted by a crash mid-write
- [ ] Every character has `MigratedAt != 0`
- [ ] No client message can set a balance or an inventory
- [ ] Every money and inventory mutation happens in a server-side function that validates first
- [ ] Every such mutation increments `EconomyRevision`
- [ ] Economy requests are idempotent under retry
- [ ] A stale-revision save is refused, and the client is corrected
- [ ] Trade, `/pay`, vehicle sale and starter kits are unchanged and still pass their tests
- [ ] Two-client exploit testing finds no duplication or loss
- [ ] No legitimate player lost possessions in migration

---

## What this document deliberately does not propose

- **A database.** The copy-and-commit pattern plus an atomic file write is a complete
  transaction for this architecture. Introducing SQLite or similar would be a larger change
  than the problem justifies, and would collide with the replicable-instances rule, which
  wants shared storage rather than a second local file format.
- **Item instances.** Items are fungible today. Adding instance identity would be building for
  durability, attachments and unique items that do not exist.
- **A transaction journal.** With an atomic write there is nothing to replay after a crash:
  the file is either the old state or the new one.
