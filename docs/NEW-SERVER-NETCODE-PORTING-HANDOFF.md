# New-server netcode porting handoff

**Server-agnostic. Organised by master phase.** Written 2026-09-05 (Cam stream).

This document exists so the replacement server can rebuild these systems **without reading the
old code**. Where the outgoing implementation is named, it is named as *evidence* — never as a
design to copy.

Companions: `OUTGOING-SERVER-NETCODE-MAP.md` (where the old code is) ·
`NEW-SERVER-AUTHORITY-HANDOFF.md` (economy authority, game facts) ·
`PHASE5-STAGE6A/6B/6C/6D-*.md` (the audits).

**Two rules that apply to every entry below:**

> **Requirements survive. Transport does not.**
> **The client reports gameplay intent and context; the server decides the effect.**

---

## Phase 1 — Server / client architecture

### 1.1 Authentication and admission

**Outgoing:** `GameServer::HandleAuthentication`. Checks in order: client protocol id → server
protocol id → password → manifest version → install digest → ban → capacity. Denies with a typed
code and a player-facing sentence.

**Owner:** server, entirely.
**Client:** presents credentials and build identity; accepts refusal.
**Invariant worth carrying:** **the order is the design.** Refuse the cheapest, most certain
thing first. A wrong password costs nothing to reject; a Discord lookup costs a round trip.
**Also carry:** every denial has a *player-actionable* sentence. "Invalid protocol version" cost a
real debugging session until the build stamp was added to both sides of the log.
**Do not port:** `EDenialCode` numbering, GameNetworkingSockets specifics, the `kIdentifier`
comparison itself.
**Security:** identity comes from the authenticated session and never from message contents. This
is the root of every other authority rule here.
**Rebuild:** immediately — everything depends on it.

### 1.2 Protocol versioning

**Outgoing:** `FNV1a64` over a structural description; lockstep enforced at connect.
**Carry:** a version check that fails **loudly at the door**, not silently mid-stream. Lockstep is
acceptable and was never the problem.
**Do not port:** a positional wire format with a presence bitfield, where adding one optional
field shifts every later field and a mismatch yields garbage rather than an error. **Prefer a
format where an unknown or missing field is survivable.**
**Known bug solved:** generation must never be skipped on an mtime heuristic — switching branches
can leave a generated file newer than its source, and the build then links one branch's
serializers against another's headers. The outgoing build regenerates unconditionally, and the
comment explaining why should be carried as a warning.
**Rebuild:** immediately.

### 1.3 Request validation, rate and size limits

**Outgoing:** chat flood control (`RateLimiter`, 20 per 5s), voice frame caps.
**Carry, as ordering:** authenticate → cheap size/shape checks → rate limit → **then** expensive
work (record copies, store lookups, persistence, counterparty scans).
**Carry:** never log-spam on flood — the log is a resource an attacker can consume.
**Security:** any client-supplied string needs a length cap *before* it is used as a map key.
**Rebuild:** immediately, alongside the first mutating request.

### 1.4 RPC

**Outgoing:** `RpcService` / `RpcScriptInstance`, a scripted RPC layer.
**Carry:** the *idea* that server scripts can call into clients.
**Do not port:** the definition-exchange handshake — its shape is entirely transport-specific.
**Rebuild:** deferred; nothing critical depends on it.

---

## Phase 2 — Character identity and persistence

### 2.1 Character selector — PARKED, requirements preserved

**Intended flow, unchanged:**

```
Launch → Main Menu → Connect → Authenticate → Character List
      → Select / New → Play → server validates CharacterID
      → authoritative load → spawn
```

**Requirements:**
- **CharacterID, never a positional slot index.** The outgoing wire uses `slot` as an integer;
  it breaks on deletion and reorder. This is the single most important thing not to repeat.
- multiple characters per account; **server-owned** character list; **server-driven** slot count
- **exactly one active gameplay session per CharacterID** — still not built, still required
- single-player saves must not load into multiplayer
- multiplayer characters belong to the server/account
- reconnect loads the **same** authoritative character
- **selecting is not spawning**; "Play" commits the selection
- the selector must never become authoritative state

**Known bug solved:** loading as the template character ("Phantom Veronica") was
`LoadLastCheckpoint` taking save index 0, which the template often was. Identity must not come
from a position in a list — the same lesson as `slot`.
**Known bug solved:** a character panel rendered off-screen because of an anchor point; the fix
existed on an unmerged branch and was independently re-derived. **Check for an existing fix before
writing one.**
**Blocked on:** auth, identity, persistence, CharacterID, active-session lock, authoritative
load/spawn.
**Rebuild:** deferred until all six exist.

### 2.2 Character save

**Outgoing:** one `SaveCharacterRequest` carrying appearance + inventory + money + progression +
lifepath, on a 90-second autosave plus disconnect, world detach, creator close, first capture, and
server request.

**Do not port the shape.** One message that is simultaneously an identity write, a possessions
declaration and a progression report is *why* client authority is a single un-pickable line.
**Split by authority, not by convenience.**

**Carry these hard-won guards:**
- `MaySaveCharacter()` — "is this still the server's character?" After a world reload the mirror
  holds the local save's V; storing that puts a stranger's head on someone's character.
- Never capture while a restore is outstanding (`m_restorePending`) — a capture that runs before
  the server's items land stores the pre-restore inventory *over* the server's copy, every 90
  seconds, with healthy-looking numbers in the log.
- Absence is not a value: an empty inventory means "could not read", not "owns nothing". Treating
  them alike empties pockets.
- **Save on quit.** Not every exit goes through disconnect; quitting to desktop or menu tears the
  world down on the engine's schedule.

**Known bug solved (27 Aug):** starter kit granted at 15:01:27, overwritten by the possessions
autosave at 15:02:57. The fix was to push possessions immediately so they join the *pending*
restore rather than competing with it.
**Live test:** two clients, reconnect, and quit-to-desktop mid-session.
**Rebuild:** immediately, but as *separate* authority-scoped messages.

---

## Phase 3 — Movement and entity sync

**Outgoing:** `MoveEntityRequest` / `NotifyEntityMove`, both **unreliable**; `InterpolationSystem`
client-side; per-packet relevance in `Level.cpp`.

**Requirements — all proven, carry every one:**
- **Unreliable packets arrive out of order.** Assume it; do not hope.
- **Reject stale/out-of-order movement.** An older packet must never rewind authoritative
  position.
- **Keep the received sequence separate from the replicated sequence.** Conflating them was a real
  bug: interest management silently stopped working when the components were replaced wholesale
  and `ReplicatedSequence` / `ReplicationPending` were not carried across.
- **Reset sequence state only on authoritative lifecycle boundaries** (spawn, authority handoff) —
  **never because a lower number arrived.**
- **Server-authoritative position must not regress.**
- **Coalesce: replicate the newest movement per entity once per server tick**, not per packet.
- **Relevance should scale O(players), not O(packets × players).**

**Not a session epoch.** `AuthorityComponent::Epoch` is a *vehicle handoff* counter — which grant
of simulation rights is current. Player puppets never transfer, so it does not apply to them.
Do not mistake it for a session/reconnect epoch; there is no session epoch, and the outgoing
server does not need one because stale sessions are prevented structurally (connection-oriented
transport, the connection→player map erased on disconnect, and a character switch refused while a
live puppet exists).

**Unresolved:** movement coalescing is implemented behind a flag that is **OFF** and has **never
been tested with two clients**. That test is outstanding and belongs to whoever rebuilds this.
**Do not port:** the replication loop, the cell/grid queries, `InterpolationSystem` internals.
**Rebuild:** immediately — nothing looks more broken faster than movement.

---

## Phase 4 — Inventory

**Outgoing:** the client declares its entire inventory in `SaveCharacterRequest`; the server
stores it verbatim. **This is the client-authority hole and must not be recreated.**

**Facts established (see `PHASE5-STAGE6A`):**
- **No vanilla inventory operation is observed.** Not one hook on `TransactionSystem`,
  `EquipmentSystem`, or any vendor, crafting, loot, stash or container system.
- 16+ unobserved sources; **nothing is disabled**.
- **The item model cannot represent a real item.** `(TweakDBID, quantity)` in, `GiveItemByTDBID`
  out — no mods, attachments, tier, upgrades, crafted state, generated rolls, or iconic state.
  **A restore hands back a base item.** This is a live data-loss path today.
- **Reconciliation already works in both directions** (quantity): the restore has a give pass and
  a "take back what the server does not say you own" pass. What is missing is *continuous*
  reconciliation and *instance* awareness.
- Duplicate stacks are reachable only via the first match; canonicalize **at migration**,
  preserving exact total, and **block the character if a sum would exceed `uint32` — never clamp,
  wrap, or discard.**

**Before any authoritative inventory: map what item-instance data the game actually exposes.**
**Do not invent an `ItemInstanceID` before that mapping exists.**
**Known bug solved (19 Aug):** connecting from inside the world handed back all 124 stacks on top
of the 124 already held and doubled everything. **Always reconcile to a difference, never a
total.**
**Known bug solved:** the strip took whatever sat in the `RightArm` slot — body slots are not
loot. Protect `RightArm`, `LeftArm`, `BaseFists`.
**Rebuild:** **deferred.** Its own workstream, gated on item fidelity.

---

## Phase 5 — Economy

**Largely reusable and NOT the reason for the swap.** Full detail in
`NEW-SERVER-AUTHORITY-HANDOFF.md`; the invariants are listed in the netcode map §5.

**Networking removed from Phase 5: essentially none.** Stages 1–5 produced persistence, a mutation
boundary, transaction semantics, migration design and revision semantics — all server-agnostic.
The only transport-facing pieces are the observation log in `HandleSaveCharacterRequest` and the
`NotifyMoney` / `NotifyPossessions` push shapes.

**Do not port:** `NotifyMoney.balance` as `int32` (money is `int64` everywhere else; five
narrowing casts feed it) · `EconomyRevision`/`MigratedAt` as shared metadata — start with
`MoneyRevision`/`MoneyMigratedAt`.
**Carry:** `ApplyServerMoney`'s behaviour — the server states a balance and the client's game is
set to it, **up or down, unconditionally**. That is the enforcement half and it already works.
**Unresolved:** money is **not** server-authoritative today either — 17 vanilla paths bypass the
mutation boundary (vendors, access points, bounties, taxi, respec, vending, casino, shards…).
Vendors are `NEEDS SERVER MODEL`. Perk respec is the approved first target: its cost is a
closed-form function the server can compute.
**Rebuild:** the invariants immediately; the authority cutover after vendors.

---

## Phase 6 — Vehicles

**Outgoing:** `VehicleSystem` client-side; enter/exit handlers in `Level.cpp`; `VehicleStore`,
`VehicleRecord`, `VehicleSeats` server-side; `/sellcar` + `/buycar` with server-held price and a
lock token.

**Requirements:**
- ownership is **server-authoritative** and vehicle identity is **persistent**
- enter/exit validation; driver authority; **passengers and rear seats; four-seat support**
- **a vehicle must not be destroyed merely because it becomes empty**
- no duplication on exit
- authority handoff has explicit semantics — this is what `AuthorityComponent::Epoch` counts
- **sale/purchase atomic; the server determines the price; one transaction cannot pay twice**
- **the vanilla vehicle shop conflicts with NCO ownership and should be disabled**
  (`vehicleShopGameController`, `computerYaibaShowroom` — decided; do not reproduce the vanilla
  shop to preserve it, and do not confiscate vehicles players already own)

**Known bug solved:** a seat swap is exit+enter within a millisecond; applying the enter first let
the trailing exit remove the *new* seat, emptying a car whose driver believed he was driving it —
whereupon it was destroyed under him. **Name the vehicle and seat in the exit** so a stale one can
be recognised and dropped.
**Known bug solved:** destroying entities from inside an iteration invalidates it — collect, then
destroy.
**Unresolved:** a remote-vehicle-mount crash dominates the current crash reports and is distinct
from the older remote-player-join crash; match the signature before assuming which.
**Rebuild:** after movement.

---

## Phase 7 — Combat, health, medical

**Outgoing:** combat/weapon/status/quickhack request handlers; `HealthComponent`, `Medical.h`.

**Requirements:** server-authoritative health · PvP validation · weapon-fire validation · ammo
ownership · **cannot fire an empty magazine** · rate-of-fire and reload validation · damage bounds
· target and range validation · quickhack RAM and cooldown authority · downed state · revive and
medic RP · status effects · respawn lifecycle · **anti-replay on every damage event**.

**Carry:** `/kill` must actually down/flatline rather than only reporting it — the visible state
and the authoritative state must agree.
**Do not port:** the message shapes; combat is the area most likely to need a different tick and
reconciliation model on a new server.
**Rebuild:** after movement and vehicles.

---

## Phase 8 — World interactables

**Requirements:** doors, gates, containers, properties and shared world objects converge on the
server's state. World facts are the mechanism the outgoing server used to open areas
(`WorldFacts.cpp`), and it is a good one: a fact is how Night City decides whether a door opens,
and setting it does not fake quest completion.

**Known fact:** Dogtown access is gated by the fact `ep1_side_content`, **not** q301.
**Rebuild:** deferred.

---

## Phase 9 — RP systems (phone, voice, chat)

### 9.1 Phone

**Requirements:** the **vanilla Cyberpunk phone UI**, not a chat-menu replacement · contacts ·
ten-digit numbers · call / answer / decline / hang up · player-to-player texting ·
**server-owned call state** · stale phone state must be recoverable · **one authoritative
transition per state change**.

**Carry:** `MessageStore` / `CallStore` almost wholesale — they are state models, not transport.
**Carry:** the per-conversation disk layout. One text used to rewrite every conversation on the
server; that is a disk-amplification bug waiting to recur.
**Do not port:** `NotifyCall`'s bare `uint32` state/action. **Use validated enums with a refusing
default** — the wire does not range-check them (proven).
**Do not port:** the packet shapes generally.
**Carry, unbuilt:** RequestID idempotency for text and call mutations. `RequestLedger` exists,
is tested, and has **never had a wire field to key on** — `MessageStore::Send` has taken
`acRequestId` since it was written and every caller passes empty.
**Known bug solved:** blocking calls at `SetCallInfo` is too late and locks the player in an
invisible call — block at `PhoneSystem.OnTriggerCall`.
**Known bug solved:** the phone's answer/decline phase enum is
`{Undefined, IncomingCall, StartCall, EndCall}` — a comment claiming otherwise cost real time.
**Cam's standing rule:** no vanilla NPC may call or be called. Judy, Panam and Songbird stay out.
**Rebuild:** deferred; requirements preserved.

### 9.2 Voice

**Requirements:** maximum frame size · inbound frame-rate cap · **cheap rejection before expensive
lookup** · no log spam on flood · **proximity decided server-side** · call partner and channel
authority server-side.
**Threat model:** voice is a **relay amplification** surface — one sender, many receivers. Cap
inbound before deciding recipients.
**Unresolved live test:** two clients, one flooding.
**Rebuild:** deferred.

### 9.3 Chat

**Carry:** channels (`/yell`, `/whisper`, `/advert`), flood control, and the **audit rule** that
every `/command` appearing in a `Tell()` must have a dispatch block — it found three advertised
commands that did not exist (`/respawn`, `/inventory`, one alias chain).
**Do not port:** chat as the economy transport. `/pay` and `/trade` work, but a text parser is not
an authority surface.

---

## Phase 10 — NPCs, traffic, world persistence

**Requirements:** NPC identity, position, state · NPCs inside vehicles · traffic · animation
state · downed / dead · combat target and state · persistent death where required · merchant
inventory synchronization · police events · world facts.

**The core rule for the replacement:**

> **If the server says an NPC is dead, downed or removed, every client must converge on that
> state.**

**Carry:** the world clock and world-facts model.
**Unresolved:** the cell grid does not actually cull anything (measured 2026-09-04) — relevance is
effectively broadcast. Fix in the new relevance design rather than porting the grid.
**Rebuild:** deferred.

---

## Cross-cutting: live-test requirements

None of these can be covered by `tools/Verify.ps1`; all need a running game, most need two
clients.

```
movement coalescing with two clients      (flag OFF, never tested)
voice flood from a second client
remote-vehicle-mount crash signature
seat swap / rear seats / four-seat vehicles
reconnect convergence: no duplication, no loss
quit-to-desktop mid-session save
two-client economy duplication attempts
forged balance refused and authoritative value restored
replayed intents mutate once
NPC death convergence across clients
```

---

## Cross-cutting: what NOT to port, in one list

1. A single `SaveCharacterRequest` carrying every kind of state.
2. Client-declared money or inventory, in any form.
3. `(TweakDBID, quantity)` as the long-term item model.
4. `int32` money anywhere.
5. `slot` as positional character identity.
6. Bare `uint32` enums on the wire.
7. Economy as chat commands.
8. First-match `FindStack` with unreachable duplicates.
9. Spawn-time-only reconciliation.
10. `EconomyRevision` / `MigratedAt` as shared money+inventory metadata.
11. A positional wire format whose presence bitfield shifts on every added field.
12. The cell grid as relevance (it culls nothing).

---

**No runtime code changed. No protocol changed. Migration inactive. Selector parked. Nothing
pushed. `feat/world-state` remains intact as the outgoing reference implementation.**
