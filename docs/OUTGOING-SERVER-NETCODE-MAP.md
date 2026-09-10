# Outgoing server netcode map

**Purpose: answer "where is the old netcode?" without grepping the repository again.**

Written 2026-09-05 (Cam stream). **Nothing was rolled back.** `feat/world-state` is intact and
is classified as the **OUTGOING SERVER REFERENCE IMPLEMENTATION** — not a deployment target, not
the base for the replacement server, and deliberately not cleaned up to resemble `fork/main`.

Companion: `NEW-SERVER-NETCODE-PORTING-HANDOFF.md` (what to rebuild and why).

---

## 0. Why this document exists instead of a rollback

A mechanical netcode rollback was audited and rejected. The numbers:

| | |
|---|---|
| branch total vs `fork/main` | 116 files, **+20,760 / −1,584** |
| files matching the "netcode" definition | 33 files, +6,046 / −1,235 |
| **actual outgoing-transport surface** | **~1,500–2,500 lines** |

The gap between those last two rows is the whole story: the transport is interleaved line-by-line
with persistence, permissions, economy and admin work that must be preserved. Removing it
mechanically produces a tree that does not build, and rebuilding it to compile is new work — which
a rollback is not allowed to do.

**Protocol identifiers, recorded and deliberately left alone:**

```
fork/main:        client 0x88b2f6b5cbefc91c   server 0xb2f2bf7363a7f337
feat/world-state: client 0xc67c52a1b6c5f096   server 0xa14513f4653e80f
```

---

## 1. The transport layer

| File | What it is | Classification |
|---|---|---|
| `code/protocol/client.proto` | 21 client→server messages in a `Protocol` oneof | **transport-specific** |
| `code/protocol/server.proto` | 33 server→client messages | **transport-specific** |
| `code/protocol/common.proto` | shared `Vector3` | transport-specific (trivial) |
| `code/netpack/main.cpp` | the code generator | **transport-specific** |
| `code/client/App/Network/NetworkService.cpp` | client transport, `kIdentifier` handshake | **transport-specific** |
| `code/client/App/Network/Rpc/RpcService.cpp/.h` | RPC dispatch | **transport-specific** |
| `code/server/native/Scripting/RpcScriptInstance.cpp/.h` | server RPC | **transport-specific** |

### 1.1 netpack framing — facts that constrain any successor

- `kIdentifier = FNV1a64(kProtocolString)`, and `kProtocolString` is built from **structure**:
  dependencies, package, enum names/values/numbers, message names, and per field
  `type-name_isOptional`. **Not** comments, whitespace, or field numbers.
- **Positional wire format with a presence bitfield.** Adding one optional field widens the prefix
  and shifts every field after it. A mismatched reader gets garbage, not an error.
- **Lockstep is enforced at the door**: `GameServer::HandleAuthentication` denies on either
  identifier mismatch before password, manifest or Discord checks. Old and new clients cannot
  coexist for one second.
- **Enums are not range-validated on the wire** — proven: `static_cast<TestEnum>(9999)`
  round-trips intact. The generated `_COUNT` sentinel is generator-only and not part of the
  contract.
- Two generator defects were found and fixed (`b6fc19c`): `GetType`'s `TYPE_ENUM` branch
  segfaulted (`message_type()` on an enum field), and a repeated enum generated a cast to the
  container type. **Both are generator-only; production protos are byte-identical after the fix.**

---

## 2. Handler inventory

### 2.1 Server handlers (20)

| Handler | Feature | Phase | Store dependency |
|---|---|---|---|
| `GameServer::HandleAuthentication` | auth, protocol lockstep, manifest/digest gates | 1 | `BanList`, `PlayerStore` |
| `ChatSystem::HandleChatMessageRequest` | chat **and every economy command** | 1, 5 | `PlayerStore`, `TradeStore`, `VehicleStore` |
| `ChatSystem::HandleSaveCharacterRequest` | possessions/appearance/progression save | 2, 4, 5 | `PlayerStore` |
| `ChatSystem::HandleSelectCharacterRequest` | character slots | 2 | `PlayerStore` |
| `ChatSystem::HandleDeleteCharacterRequest` | soft delete | 2 | `PlayerStore` |
| `ChatSystem::HandleRespawnRequest` | respawn | 7 | — |
| `ChatSystem::HandleCallRequest` | place a call | 9 | `CallStore` |
| `ChatSystem::HandleCallControlRequest` | answer / decline / hang up | 9 | `CallStore` |
| `Level::HandleSpawnCharacterRequest` | spawn + possessions push | 2, 3 | `PlayerStore` |
| `Level::HandleMoveEntityRequest` | movement (**unreliable**) | 3 | — |
| `Level::HandleUpdateAppearanceRequest` | appearance sync | 2 | `PlayerStore` |
| `Level::HandleEnterVehicleRequest` / `HandleExitVehicleRequest` | mount/dismount | 6 | `VehicleStore` |
| `Level::HandleCombatEventRequest` | damage | 7 | — |
| `Level::HandleWeaponEventRequest` | weapon fire | 7 | — |
| `Level::HandleStatusEffectRequest` | status effects | 7 | — |
| `Level::HandleQuickhackRequest` / `HandleQuickhackUploadRequest` | quickhacks | 7 | — |
| `Level::HandleVoiceFrameRequest` | voice relay (**unreliable**) | 9 | — |
| `RpcScriptInstance::HandleRpcCall` | scripted RPC | 1 | — |

### 2.2 Client handlers (29)

`NetworkWorldSystem` (21): character load/list/creator/name, spawn response, possessions, money,
appearance, teleport, world state, interaction, damage result, combat state, status effect,
quickhack upload, voice frame, call, vehicle control assigned, entity unload.
`VehicleSystem` (5): authority assigned/revoked, enter, exit, load, unload.
`InterpolationSystem` (1): `HandleNotifyEntityMove`.
`ChatSystem` (1), `RpcService` (2).

### 2.3 Unreliable messages — exactly four

```
MoveEntityRequest      NotifyEntityMove      VoiceFrameRequest      NotifyVoiceFrame
```

**Everything that mutates persistent state is reliable and ordered within a connection.** That is
a load-bearing fact: it is why the outgoing server needs no session epoch (see §4.3).

---

## 3. Mixed files — the reason the rollback was rejected

For each: what to keep as design reference, and what not to port.

### `Systems/ChatSystem.cpp` — +4,136 lines, the worst offender

| KEEP AS DESIGN/LOGIC REFERENCE | DO NOT PORT TRANSPORT |
|---|---|
| 47 chat commands; the dispatch and alias model | `RegisterHandler<&…>` wiring |
| permission model (`PermissionLevel.h`), staff gating | `PacketEvent<T>` handler signatures |
| `/pay`, `/trade`, `/sellcar`+`/buycar` transaction boundaries | `GServer->Send(connection, msg)` calls |
| starter-kit atomicity (rebuilt on a candidate, `StarterKitGranted` false on failure) | `NotifyPossessions` / `NotifyMoney` push shapes |
| impossible-balance refusal (`< 0`, `> kMaxPlausibleMoney`) keeping the STORED balance | |
| Stage 5 revision observation and `AuditLog` records | |
| `PushMoney` reconciliation intent (**not** its `int32`) | |

**The blocking example**: `HandleSaveCharacterRequest` is one function that is *simultaneously* a
packet handler and the money guard, the starter kit, the Stage 5 observation and the audit
logging. It cannot be split without a rewrite.

### `PlayerStore.h` — +788

**Keep:** `ApplyTrade` (headroom → copy → `MoveAssets` both ways → advance → commit); atomic
`Flush`; migration inspect/commit; character/slot model.
**Do not port:** nothing transport-specific — it is called *by* handlers, not *of* them.
**Classification: server-agnostic**, and one of the most directly reusable files on the branch.

### `Game/Level.cpp` — +290

**Keep:** spawn sequencing, possessions-on-spawn ordering, vehicle mount/dismount validation,
seat model.
**Do not port:** replication loop, `NotifyEntityMove` broadcast, cell/grid interest queries,
per-packet relevance.
**Classification: mixed, transport-heavy.**

### `GameServer.cpp/.h` — +200

**Keep:** the *order* of admission checks (protocol → password → manifest → digest → ban →
capacity), `AuditLog` ownership, store lifecycle.
**Do not port:** `EDenialCode`, GameNetworkingSockets connection handling, `kIdentifier`
comparison, connection-id mapping.
**Classification: mixed.**

### `client/App/World/NetworkWorldSystem.cpp/.h` — +622

**Keep:** `MaySaveCharacter()` ("is this still the server's character" — the guard that stopped
the template overwriting real characters); save triggers (90s autosave, disconnect, world detach,
creator close, first capture, server request); `m_restorePending` race avoidance.
**Do not port:** every `Handle*` packet method, `CallVirtual` bridges shaped by message layout.
**Classification: mixed.**

### `MessageStore.h` (+642) / `CallStore.h` (+540)

**Keep:** both, nearly wholesale — server-owned call state machine
(`Undefined/IncomingCall/StartCall/EndCall`), one authoritative transition per change, conversation
model, per-conversation disk layout that stopped one text rewriting every conversation,
`RequestLedger` integration point (`acRequestId`, currently always empty).
**Do not port:** nothing — these are state models, not transport.
**Classification: server-agnostic.**

### `CharacterRecord.h` (+322), `TradeStore.h` (+362), `EconomyMutator.h` (+398), `EconomyMigration.h` (+248), `RequestLedger.h` (+215), `AtomicWrite.h` (+236)

**Keep: all of it, as design.** These are Phase 5 Stages 1–5 and contain no transport whatsoever.
**Do not port:** the C++ types themselves if the new server's language/storage differs — carry the
*invariants* (§5), not the structs.
**Classification: server-agnostic.**

**One caveat on `CharacterRecord`:** the field names `EconomyRevision` / `MigratedAt` are known
wrong — they claim money *and* inventory. The new server starts with `MoneyRevision` /
`MoneyMigratedAt`. The paused patch at
`C:\Users\Cam\nco-backup\paused-work\stage6-metadata-rename-PAUSED.patch` is **reference only**.

---

## 4. Branch-only protocol features

Three commits, 134 lines, deliberately **not** reverted.

### 4.1 `9af9e8d` — character slots / selector

Wire: `SelectCharacterRequest{slot}`, `DeleteCharacterRequest`, and `slot` / `is_active` /
`character_slots` on the character list.
**Requirement survives. `slot` as an integer index does NOT** — positional identity breaks on
deletion and reorder. Use CharacterID.

### 4.2 `1d5aec2` — phone calls through the game's phone

Wire: `CallRequest{number}`, `CallControlRequest{call_id, action}`,
`NotifyCall{call_id, state, display_name, number, incoming}`.
**Requirement survives. Bare `uint32` `state`/`action` do NOT** — untyped, and §1.1 proves the
wire does not range-validate them.

### 4.3 `8156ebb` — `/call` reachability fix

Removed a declared message nothing sent and nothing handled. **The lesson survives as a CI rule:**
`tools/Verify.ps1` now fails when an advertised command has no dispatch, and when a client request
has no handler.

---

## 5. Phase 5 — why it was not the reason for the swap

**Stages 1–5 are largely reusable architecture and are not transport.** Invariants to carry:

atomic persistence · copy-then-commit · no clamping, ever · one authoritative mutation boundary ·
revision the transaction, not the primitive · a revision advances only when the state it versions
changes · `UINT64_MAX` refuses and sticks · migration is the only 0 → 1 transition · migration and
authority cross together · RequestID idempotency keyed on authenticated identity · never record
success before persistence · **a matching client revision never means the client's values are
trusted**.

**Do not assume the new server reuses the same C++ types or stores.**

---

## 6. Where everything lives

| Area | Transport | Server-agnostic | Mixed |
|---|---|---|---|
| Protocol | `code/protocol/*`, `code/netpack/` | — | — |
| Transport | `NetworkService`, `RpcService`, `RpcScriptInstance` | — | — |
| Auth / lifecycle | — | — | `GameServer.cpp/.h` |
| Characters | — | `PlayerStore.h`, `CharacterRecord.h` | `ChatSystem.cpp`, `NetworkWorldSystem.*` |
| Movement | `InterpolationSystem.cpp`, `MovementComponent` | — | `Level.cpp` |
| Economy | — | `EconomyMutator/Migration.h`, `TradeStore.h`, `AtomicWrite.h`, `RequestLedger.h` | `ChatSystem.cpp` |
| Phone | — | `MessageStore.h`, `CallStore.h` | `ChatSystem.cpp` |
| Voice | `VoiceClient.cpp`, `VoiceAudioManager.cpp` | — | `Level.cpp` (relay + cap) |
| Vehicles | `VehicleSystem` (client) | `VehicleStore.cpp`, `VehicleRecord.h`, `VehicleSeats.h` | `Level.cpp`, `ChatSystem.cpp` |
| Combat / health | — | `Medical.h`, `HealthComponent.h` | `Level.cpp` |
| World | — | `WorldFacts.cpp`, `WorldClock.h` | `Level.cpp` |
| Client scripts | — | all `code/assets/redscript/*.reds` | — |

**The redscript layer is almost entirely server-agnostic.** It talks to *the game*, not to the
network — `Inventory.reds` (capture/restore/reconcile), `Phone.reds`, `Death.reds`,
`Quests.reds`, `PauseMenu.reds`, `MainMenu.reds`. It carries forward with the least change of
anything on the branch, and it contains the hardest-won game knowledge.

---

**No runtime code changed. No protocol changed. Migration inactive. Selector parked. Nothing
pushed.**
