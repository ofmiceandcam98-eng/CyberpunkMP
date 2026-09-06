# zeldfep — start here after the server swap

**From Cam's stream, 2026-09-05.** This is the "pick it up and finish it" note. Everything below
is either done and frozen, or waiting on you to swap the servers first.

**Branch: `wip/world-state`** (on `fork` = `ofmiceandcam98-eng/CyberpunkMP`), currently
`8b442b1`. Use that on both desktop and phone. `feat/world-state` is deliberately **not** pushed
during the migration — pushing it can trigger the production NAS cron to rebuild the live server
mid-migration. After the swap, fast-forward `feat/world-state` onto `wip/world-state` and push
normally.

---

## 1. Where things stand

**Phase 5 (economy authority) is 5 of 8 done and paused at the network boundary.**

```
Stage 1 ✅ atomic persistence          Stage 5 ✅ transaction revision semantics
Stage 2 ✅ revision/migration metadata  Stage 6 ⏸ BLOCKED on the swap
Stage 3 ✅ migration mechanism          Stage 7 ⛔ not started
Stage 4 ✅ server mutation boundary     Stage 8 ⛔ not started
```

**Nothing behaves differently for any live player.** Migration has never run; every character is
`(revision 0, migratedAt 0)`; the client is still the authority on money and inventory exactly as
before. Stages 1–5 built the machinery deliberately *before* switching anything on.

**`feat/world-state` / `wip/world-state` is now the OUTGOING SERVER REFERENCE IMPLEMENTATION.**
A full netcode rollback was proposed, audited and rejected — the transport is interleaved
line-by-line with persistence, permissions and economy work that has to survive. It stays whole
so you can read it. It is **not** the base for the new server.

**Netcode is frozen** until you say the swap is done: no production `.proto`, handlers,
transport, auth, replication, movement, vehicle, voice, phone, selector or economy networking.

---

## 2. The first thing to do after the swap — and it isn't code

**Do not start porting classes.** Establish what the replacement actually gives you, in these ten
areas:

```
1 server implementation        6 entity identity model
2 repository / baseline        7 persistence ownership
3 transport model              8 RPC / event architecture
4 protocol model               9 reliability + ordering guarantees
5 auth / session model        10 deployment architecture
```

Then read those capabilities against the three handoff documents and produce an implementation
roadmap. **Half the design work is already done** — the point of that comparison is to find which
half survives, not to redo it.

**Pick the baseline deliberately and report it before branching.** `fork/main` is what the live
boxes pull today, so it is the *deployed* baseline — that is a fact about today's deployment, not
an argument that it is the right base for a different server. New server work goes on a **new
branch from the chosen baseline**, treating `wip/world-state` as reference only.

---

## 3. Everything you need, in the order you'll need it

| Document | What it gives you |
|---|---|
| `docs/NEW-SERVER-NETCODE-PORTING-HANDOFF.md` | all ten master phases; rebuild each system **without reading the old code**. Authoritative owner, sequencing, idempotency, reconnect, limits, live tests, and what not to port |
| `docs/OUTGOING-SERVER-NETCODE-MAP.md` | where the old netcode is. All 20 server + 29 client handlers, the four unreliable messages, and every mixed file split into KEEP AS REFERENCE vs DO NOT PORT |
| `docs/NEW-SERVER-AUTHORITY-HANDOFF.md` | economy authority + proven Cyberpunk facts that survive any server |
| `docs/PHASE5-STAGE6A-INVENTORY-AUDIT.md` | every vanilla inventory source, classified |
| `docs/PHASE5-STAGE6B-MONEY-AUDIT.md` | every vanilla money source, classified |
| `docs/PHASE5-STAGE6C-REACHABILITY-AND-SPECS.md` | reward reachability, `RespecIntent` spec, vehicle-shop disable, protocol inventory |
| `docs/PHASE5-STAGE6D-REWARD-CONTENT-AUDIT.md` | reward record contents; the third money API |
| `docs/MAP.md` | the ledger. The **FOR ZELDFEP** block at the top is the short version of this |

**Read the requirements first. Only look at the old implementation afterwards, if it helps.**

---

## 4. Suggested order once the roadmap exists

**Everything depends on identity, so it goes first.**

1. **Identity + session lock.** AccountID / SessionID / CharacterID, and **one active gameplay
   session per CharacterID**. Still required, never built, and the selector is blocked on it.
   Persistent state attaches to **CharacterID** — never a slot index, save index, display name,
   connection id or Discord name.
2. **Persistence.** Port the invariants, not the C++: atomic write (temp → fsync → backup →
   rename), copy-then-commit, no clamping ever, one authoritative mutation boundary.
3. **Character system + selector.** The flow is written out in the porting handoff §2.1.
4. **Movement.** Reject stale/out-of-order, keep received and replicated sequences separate,
   reset only on lifecycle boundaries, position must never regress, coalesce newest per tick.
5. **Money authority.** Start with **perk respec**, not vendors —
   `cost = Price.RespecBase + Price.RespecSinglePerk × (spentPerks + spentTraits)`, a closed form
   the server can compute, and NCO already persists the inputs. It proves the whole
   intent → validate → mutate → persist → reconcile loop on something small. Vendors come after,
   and they need a real server model.
6. **Vehicles, combat, world** — requirements are all in the porting handoff.
7. **Inventory authority.** Its own project. Do not attempt it alongside money.

---

## 5. Things that will bite you if nobody tells you

**Money is an inventory item.** `MarketSystem.Money()`. Vendors move eddies client-side —
`vendor.script:1180`, proven from the game's own source. **Money is not server-authoritative
today**; 17 vanilla paths bypass the mutation boundary. There are **three** money APIs, not one:
`TransferItem`, `GiveItem`/`RemoveItem`, and `GiveMoney()`. A search for `MarketSystem.Money()`
alone misses the third — that mistake is already in this project's history.

**No vanilla inventory operation is observed.** Zero hooks on `TransactionSystem`,
`EquipmentSystem`, or any vendor/crafting/loot/stash system. Everything reaches the server through
one 90-second snapshot. Nothing is disabled.

**The item model cannot represent a real item.** `(TweakDBID, quantity)` in, `GiveItemByTDBID`
out. Mods, attachments, tier, upgrades, crafted state and iconic status are all lost — **a restore
hands back a base item.** That is a live data-loss path today, not a future risk. Map what item
instance data the game actually exposes **before** designing anything around an ItemInstanceID.

**Wire enums are not range-validated.** An undeclared value round-trips intact, and the generated
`_COUNT` sentinel is generator-only. Every client-controlled enum needs an explicit known-value
switch with a refusing default. `value < COUNT` is not a check.

**The redscript layer is game integration, not transport.** It talks to the game, not the network,
so a server swap is not by itself a reason to rewrite it. Audit it as
`DIRECTLY REUSABLE / NEEDS NEW BRIDGE / OBSOLETE`. The knowledge in inventory capture/restore,
money reconciliation, the vanilla phone, quest suppression and vehicle mounting cost days and is
written down nowhere else.

**The client can already be reconciled in both directions.** `ApplyServerMoney` sets the balance
up or down unconditionally; the inventory restore has a give pass and a take-back pass. Enforcement
is not the missing piece — knowing *why* a value changed is.

---

## 6. Seven regressions that must not come back

Each one actually happened. They are acceptance criteria, not history — full detail in the porting
handoff.

1. **Vehicle seat transition** — a seat swap is exit+enter within a millisecond; applying the
   enter first let the trailing exit empty a moving car, which was then destroyed under its driver.
2. **Inventory restore duplication** — reconcile on `difference = desired − current`, never add
   the total. Doing it wrong doubled 124 stacks.
3. **Body slot protection** — the strip took `RightArm`; a player lost their arms.
4. **Starter-kit autosave race** — granted 15:01:27, erased by the autosave at 15:02:57.
5. **Character identity** — never a save index, array position or slot number.
6. **Phone call state** — never leave a player in an invisible call. Block at
   `PhoneSystem.OnTriggerCall`; `SetCallInfo` is too late.
7. **Codegen contamination** — never regenerate protocol on an mtime heuristic; a build from one
   checkout must never link serializers generated from another.

---

## 7. Open blockers — documented is not solved

- **Movement coalescing**: built, flag **OFF**, **never live-tested with two clients.** This one
  needs you specifically — it is the oldest outstanding item and it cannot be tested alone.
- **Remote vehicle mount crash**: dominates current crash reports; distinct from the older join
  crash. Match the signature before assuming which.
- **Cell-grid relevance**: currently culls nothing. Fix in the new relevance design.
- **Money authority**: 17 unobserved vanilla paths.
- **Item-instance fidelity**: unresolved, and it gates inventory authority.
- **Character session lock**: required, never built.
- **Optional, read-only**: five TweakDB reward records (`ExtractParts*`, `ProgramParts*`) whose
  currency contents are unresolved. WolvenKit's GUI TweakDB browser answers it in minutes; it
  would shrink the money matrix. Not blocking anything.

---

## 8. Rules that stay in force

- **Migration stays inactive.** Do not stamp metadata, canonicalize inventories, or remove client
  authority until Stage 8 is explicitly reached on the new server.
- **The metadata rename is paused, not applied.** `EconomyRevision`/`MigratedAt` are known-wrong
  names (they claim money *and* inventory). The new server starts with
  `MoneyRevision`/`MoneyMigratedAt`, and later `InventoryRevision`/`InventoryMigratedAt`. The
  patch at `nco-backup\paused-work\stage6-metadata-rename-PAUSED.patch` is **reference only** —
  do not apply it to the outgoing server.
- **Selector stays parked** until auth, identity, CharacterID, persistence, session lock and
  authoritative load/spawn exist.
- **The governing principle**: the server owns truth; the client owns presentation and intent. A
  client may request an action and must never be trusted to declare authoritative state.

---

## 9. Backups

Verified `git bundle` snapshots in `C:\Users\Cam\nco-backup\`, including a marked
`PRE-ROLLBACK-` one. Each was verified with `git bundle verify` and one was proven by cloning
from it. Refresh after meaningful milestones. Submodules are gitlinks — all four pinned commits
were confirmed published upstream, so none needs separate backup.
