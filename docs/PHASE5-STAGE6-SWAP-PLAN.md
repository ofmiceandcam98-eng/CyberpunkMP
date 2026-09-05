# Phase 5 — Stage 6 / server swap design map

**Status: DESIGN ONLY for the protocol. No production `.proto` has been touched.**

Revised 2026-09-05 (Cam stream) to incorporate the Stage 6 design review. The review's
decisions are treated as binding; where this revision goes further than the review asked, it
says so and gives the evidence.

**One thing in here is no longer design: the netpack enum prerequisite (§1) was approved as a
prerequisite, and it is now built, fixed, and verified.** Two defects, not one. Details below.

**The objective is ONE CONTROLLED ECONOMY CUTOVER**, not a protocol rewrite. §17 lists what was
considered and excluded.

---

## 0. What actually makes a flag day

`kIdentifier` is `FNV1a64(kProtocolString)`, and `kProtocolString` is built by `HashProtocol`
(`code/netpack/main.cpp:92`). It walks **structure**, not file text:

| Hashed | Not hashed |
|---|---|
| file dependencies | comments |
| package name | whitespace, ordering in the file |
| enum names, value names, value numbers | **field numbers** (`= 4`, `= 5` …) |
| message class names | field documentation |
| per field: type, **name**, `is_optional` | the oneof entry's *name* |
| the `Protocol` oneof, as `field_<index>_<Type>` | |

Verified directly against a generated header — the scratch protocol in §1 emitted:

```
kProtocolString = "package_enumtest.enum_TestEnum.element_TEST_ZERO_0.element_TEST_ONE_1.
element_TEST_TWO_2.struct_EnumTest.field_TestEnum-value_1. …"
```

So documenting a `.proto` is free. Renaming a field is a flag day even though renaming is
wire-invisible in real protobuf. Adding one optional field widens the presence bitfield and
shifts every field after it (`netpack/main.cpp:338-362` — `SpawnCharacterResponse` went 6 bits
→ 8 during the appearance work and would have misaligned every un-rebuilt client).

**The cost is fixed.** One field or six, it is the same identifier change. That argues for
landing the economy set together, and equally against letting anything unrelated ride along —
each addition is permanent surface, not free surface.

---

## 1. PREREQUISITE — netpack enum support: DONE

Approved as a prerequisite and completed as an isolated change. **No production `.proto` was
modified.**

### 1.1 Defect one — the generator segfaulted

`GetType()` (`code/netpack/main.cpp:5`). The `TYPE_ENUM` branch was a copy of the
`TYPE_MESSAGE` branch and kept its accessor:

```cpp
else if (type == FieldDescriptor::TYPE_ENUM)
{
    name = ...ClassName(field->enum_type());                 // correct
    if (field->message_type()->file()->package() != ...)     // WRONG - null on an enum
}
```

`FieldDescriptor::message_type()` is only defined for `TYPE_MESSAGE`/`TYPE_GROUP`.

**Proven, not inferred.** Running the unfixed generator on the scratch proto:

```
Segmentation fault
EXIT CODE: 139
output: (empty)
```

It died inside `HashProtocol`, before emitting anything. **Fix:** `field->message_type()` →
`field->enum_type()` on both lines.

### 1.2 Defect two — a repeated enum did not compile

Found only because the scratch proto included `repeated TestEnum history`. The deserializer
(`main.cpp:429`) cast each element using `GetType(field)`, which wraps a repeated field in
`Vector<>`:

```cpp
internal_history_itor_data = static_cast<Vector<TestEnum>>(Serialization::ReadVarInt(aReader) ...);
//                                        ^ the container, not the element
```

```
error C2440: '=': cannot convert from 'std::vector<enumtest::TestEnum,...>' to '_Ty'
```

**Fix:** `GetType` → `GetInternalType`, which returns the element type. The generic primitive
branch immediately below has always used `GetInternalType` for exactly this reason; the enum
branch was written separately and did not.

**This is why the review's instruction to prove compilation rather than assume integral
promotion was correct.** The serialize direction *did* work by promotion. The deserialize
direction never compiled, and nothing but a real compile would have found it.

### 1.3 Evidence

Scratch protocol: `tools/netpack-scratch/enumtest.proto` — deliberately outside
`code/protocol/`, because a file the Protocol target never sees cannot affect any protocol
identifier. Runner: `tools/netpack-scratch/Run.ps1`. Test: `netpack_enum_test.cpp`.

```
== codegen ==   ok - enumtest.gen.h and enumtest.gen.cpp generated
                scratch protocol identifier: 0xefaf15472518d95f
== compile ==   ok - generated enum code compiles
== round trip == 18 checks, all passed
```

Covered: all three declared values; **zero-as-a-value vs absent** (the presence-bitfield trap
an enum whose first member is 0 would hide); unset stays unset; an enum **beside** other
fields, asserting the `uint64` and `string` after it are not shifted; a repeated enum's length
and element order; partial population; the generated `COUNT` sentinel.

### 1.4 The finding that changes a Stage 6 handler requirement

```
ok  AN UNDECLARED ENUM VALUE ROUND-TRIPS INTACT - netpack does NOT validate range
```

`static_cast<TestEnum>(9999)` serialises and deserialises to `9999`. The wire layer preserves
whatever integer arrives.

**Therefore every enum field read from a client is untrusted input.** A handler must switch on
known members with an explicit refusing `default` — never assume the value is declared, and
never index an array by it. The generated `TestEnum_COUNT` sentinel is *not* part of
`kProtocolString` and is not a receivable value.

### 1.5 The existing protocol is provably unchanged

The requirement was no production `.proto` modification. Stronger evidence than "I didn't edit
them": regenerating all three production protos with the **fixed** generator and diffing
against the pre-fix output —

```
client.gen.h: IDENTICAL      kIdentifier = 0xc67c52a1b6c5f096  (unchanged)
server.gen.h: IDENTICAL      kIdentifier = 0xa14513f4653e80f   (unchanged)
common.gen.h: IDENTICAL
```

Byte-for-byte. The fix only touches a path no production proto reaches.

---

## 2. A finding that changes Stage 6's scope

The review said: *"If the existing vehicle flow does not need a new network request, do not
create one merely because Stage 6 exists."* Applying that test honestly answers a larger
question than it was asked.

**Every economy action on this server is already a chat command:**

| Action | Path | Client sends |
|---|---|---|
| `/pay` | `ChatSystem.cpp:3654` | `ChatMessageRequest` |
| `/trade` | `:4433` | `ChatMessageRequest` |
| vehicle sale | `/sellcar` → `m_pendingSales` (`:3183`) → `/buycar` → `CompleteSale` (`:3211`) | `ChatMessageRequest` |
| starter kit | server-initiated | nothing |

`ChatMessageRequest` is already authenticated (the player is resolved from the connection,
never from the message), already server-authoritative, and already validated server-side. The
vehicle flow is the clearest case: `/sellcar` builds a `PendingSale` holding token, vehicle id,
seller, buyer and **price** server-side; `/buycar` carries no payload at all and is matched by
the authenticated Discord id. The client cannot name a price or an item.

**Consequently there is no justified `VehiclePurchaseRequest`** — the review's own test
excludes it — **and by the same test there is no justified `MoneyTransferRequest` either.**
`/pay` already works this way and closing the client-authority door does not change that.

**The only economy-bearing typed client request is `SaveCharacterRequest`**, which is the door
Stage 7 closes.

### 2.1 What this means for sequencing

Stage 6 was scoped as "the authoritative economy request + RequestLedger". The above says the
requests it would carry already exist and are already authoritative. The *real* need for an
authoritative intent path is whatever legitimate inventory changes cannot survive Stage 7 —
loot, vendors, crafting, equipment — **and that is precisely the audit in §12, which has not
been done.**

So the honest recommendation, which goes beyond what the review asked:

> **Do the §12 Stage 7 inventory-source audit BEFORE finalising Stage 6's protocol surface.**

Otherwise we spend a flag day on messages we do not need, discover in the audit which messages
we *do* need, and pay a second flag day for them. The audit is read-only investigation; it
blocks nothing and it is the only thing that can tell us what the new surface must be.

**What remains justified for the swap regardless of the audit** is the observation and
synchronisation half — §3 — because those fields are needed under every possible audit outcome.

---

## 3. Proposed protocol surface (revised — minimal)

Everything the review struck is gone: no `EconomyRequest` bag, no `EconomyVerb`, no trade
verbs, no phone `request_id`, no `detail` string, no `REPLAY` result. What is left is the set
that is needed under any audit outcome.

### 3.1 `client.proto` — the observed revision

```
message SaveCharacterRequest {
    ...
    // The EconomyRevision the client believes it is working from. OBSERVATION ONLY:
    // the server classifies and records it, and refuses nothing on it in stage 6.
    uint64 economy_revision = 12;
}
```

Field 12 is free (existing: 1-11, with 7 declared last). `Economy::ClassifyClientRevision` is
already built and tested and currently has nothing to classify.

### 3.2 `server.proto` — the revision on **every** authoritative economy path

Per the review: after Stage 5, Money and EconomyRevision describe **one snapshot**, so a path
that carries new money with a stale or absent revision makes the client silently stale.

```
// added to ALL THREE
uint64 economy_revision = N;
```

| Message | Today | Why it must carry the revision |
|---|---|---|
| `SpawnCharacterResponse` (`server.proto:174`) | possessions, no revision | the client's first authoritative state |
| `NotifyPossessions` (`:166`) | inventory, money, reason | the full-inventory resync path |
| `NotifyMoney` (`:116`) | balance, reason | balance-only changes |

**No exceptions.** If a path can change what the client believes about its economy, it carries
the revision that state belongs to.

### 3.3 `server.proto` — the `NotifyMoney` truncation

```
message NotifyMoney {
    int64 balance = 1;      // was int32
    string reason = 2;
    uint64 economy_revision = 3;
}
```

`CharacterRecord::Money`, `SaveCharacterRequest.money` and every `Economy::` primitive are
`int64`. `ChatSystem::PushMoney` takes `int32_t`, fed by **five explicit narrowing casts** —
`ChatSystem.cpp:1931, 1948, 3833, 3843, 4795`. All five are removed with the widening.

It does not truncate today only because the save path refuses balances above
`kMaxPlausibleMoney` (1e9) and int32 holds ~2.147e9 — a 2.1x margin between a documented
ceiling and silent corruption, on the only channel by which the server tells a client its
authoritative balance.

### 3.4 What is NOT in the surface, and why

| Struck | Reason |
|---|---|
| `EconomyRequest` / `EconomyVerb` | review: no generic economy bag; and §2 — nothing needs it |
| `ECONOMY_VERB_TRADE_OFFER` / `_ACCEPT` | review decision 1: trade stays on its existing path |
| `VehiclePurchaseRequest` | §2 — `/buycar` carries no payload; the server already holds price and identity |
| `MoneyTransferRequest` | §2 — same test, same answer, for `/pay` |
| phone `request_id` | review decision 2: the economy flag day is not a coupon |
| `EconomyResponse.detail` | review: anything sent to a client is player-accessible; reasons belong in server and audit logs |
| `ECONOMY_RESULT_REPLAY` | review: replay returns the ORIGINAL logical result |

**`EconomyResult` is not minted at all in this revision**, because with no new request there is
no response to carry it. If §12's audit produces a justified request, the result set is defined
in §3.5 and comes with it — as one addition, on that flag day.

### 3.5 Held in reserve: the response shape, if the audit justifies a request

Recorded so the decisions are not re-derived later.

- **Results kept:** `SUCCESS`, `INSUFFICIENT_FUNDS`, `INSUFFICIENT_ITEMS`, `OVERFLOW`,
  `STALE_REVISION`, `REVISION_EXHAUSTED`, `REFUSED`. Maps 1:1 onto `Economy::Result`
  (`EconomyMutator.h:47`) plus the transport's own. The mapping must be an exhaustive switch
  with no fallthrough to `REFUSED`.
- **`STALE_REVISION` may exist but Stage 6 must never return it as a refusal.** It exists so
  Stage 7 can begin using it without a second response redesign.
- **Replay returns the original logical result**, not a distinct one. Same `request_id` →
  the stored response, whatever it was, and **zero economy mutations**. That the ledger served
  it is server-side telemetry, not a client-visible semantic.
- **Smallest authoritative response.** For a money-only transaction: `request_id`, `result`,
  authoritative money, authoritative revision. **Not the full inventory** — a rejected `/pay`
  must not serialise hundreds of stacks back to the requester. `NotifyPossessions` remains the
  full-inventory resync path. If a future request genuinely changes inventory, its resync is
  designed deliberately then.
- **Authoritative state on failure as well as success**, so a rejection is a resync rather than
  a desync.

---

## 4. Protocol impact summary

| Change | Hashes? | Wire? |
|---|---|---|
| `SaveCharacterRequest.economy_revision` | yes | widens presence bitfield |
| `economy_revision` on `SpawnCharacterResponse` | yes | widens presence bitfield |
| `economy_revision` on `NotifyPossessions` | yes | widens presence bitfield |
| `NotifyMoney`: `int32`→`int64` + `economy_revision` | yes | field width + bitfield |
| comments / documentation | no | no |
| **the netpack enum fix (§1)** | **no** | **no** — generator only, proven identical (§1.5) |

Four field additions and one widening, across three messages. **One identifier change covers
all of it**; there is no partial credit inside a swap.

Next free oneof numbers if ever needed: client **22** (19 is burned — `CreateCharacterRequest`,
explicitly not reused), server **34**.

---

## 5. Client send paths

`SaveCharacterRequest` is constructed in exactly two places, both in
`code/client/App/World/NetworkWorldSystem.cpp`:

| Site | Trigger | Economy fields |
|---|---|---|
| `:1168` | appearance change in world, gated by `MaySaveCharacter()` | `set_inventory`/`set_money` at `:1201-1202` |
| `:1622` | the character creator closing | at `:1655-1656` |

Both capture possessions via `Red::CallVirtual(…, "CaptureInventory")` (`:1177`, `:1631`) —
native cannot read the item API, so it round-trips through
`code/assets/redscript/Inventory.reds`.

**Both must echo the last authoritative revision the server supplied.** Per the review, the
client-side revision state:

- is **scoped to the selected multiplayer character** — never carry character A's revision
  into character B;
- **resets** on disconnect, authentication reset, character switch, new character selection,
  and entering a different character/session;
- is **replaced from authoritative server state** on `SpawnCharacterResponse`,
  `NotifyPossessions`, and `NotifyMoney`;
- is **never client authority** — it is an echo of what the server said, and a client that
  fabricates it only classifies itself as `Future`, which §10 records.

`MaySaveCharacter()` already encodes a closely related "is this still the server's character"
judgement and is the right thing to model the scoping on.

---

## 6. Server handlers and request flow

`ChatSystem::HandleSaveCharacterRequest` gains one observation, in the same
record-do-not-refuse spirit as the Stage 5 observation already there:

```
classify aMessage.get_economy_revision() via Economy::ClassifyClientRevision
  -> audit "economy.revision_view" with the classification
  -> refuse nothing
```

**If §12's audit justifies a new request**, it follows this order exactly — the review's flow,
and it is not stylistic:

1. resolve the authenticated connection
2. cheap size / rate validation (§9) — **before** any expensive work
3. obtain the owner key from **authenticated server identity only**
4. validate the `request_id` shape
5. `RequestLedger` lookup
6. on hit: **return the stored response, execute nothing**
7. validate the gameplay intent
8. validate authoritative state and revision headroom (`Economy::CanAdvanceRevision`) for
   every migrated participant
9. candidate mutation via `Economy::`
10. commit / persist
11. **record the completed result** — never before persistence has succeeded
12. respond

Step 8 before step 9 is the invariant Stage 5 locked. Step 11 after step 10 is the invariant
that stops a crash between them turning a failed write into a remembered success.

Registration goes through `RegisterHandler<&ChatSystem::…>` like the other client requests;
`tools/Verify.ps1` already fails the audit if a client request has no handler.

---

## 7. Server → client resync paths

| Path | When | After the swap |
|---|---|---|
| `SpawnCharacterResponse` | join / character select | possessions **+ revision** |
| `NotifyPossessions` | server changed possessions | inventory, money **+ revision** |
| `NotifyMoney` | balance-only change | int64 balance **+ revision** |

`NotifyPossessions` stays the authoritative **full-inventory** path. Nothing else should
duplicate it.

**Not proposed: a periodic authoritative push.** No evidence of drift the event paths miss, and
a timer that rewrites client possessions on a schedule is a much larger behavioural change than
this swap should carry.

---

## 8. RequestLedger

`code/server/native/RequestLedger.h` is built, tested (21 checks), and **reaches nothing**.
`MessageStore::Send` has taken `const std::string& acRequestId = {}` since it was written
(`MessageStore.h:242`) and every caller uses the default, because **no wire message carries a
request id** — grep for `request_id` across `code/protocol/` returns nothing.

`Find` and `Record` both no-op on an empty owner or id (`RequestLedger.h:84`, `:113`), so the
ledger is inert in exactly the way migration is inert.

**It stays inert through this swap.** Phone integration is parked (review decision 2), and §2
found no justified new economy request to attach it to. It activates when §12's audit produces
one — which is the first moment it has a caller that needs it.

**Owner key comes only from authenticated server identity** — never a `character_id`,
`DiscordId`, or target supplied in the message. `RequestLedger.h` says so explicitly, and it is
what stops one player replaying another's id.

---

## 9. Request limits — bound before expensive work

Any new client request must be bounded **before** it can cost the server anything. Enforced in
step 2 of §6, ahead of `CharacterRecord` copies, inventory serialisation, persistence, and
counterparty scans — while still leaving connection/auth validation first, because that is
cheaper still.

At minimum: `request_id` maximum length, counterparty identifier maximum length, request rate,
amount range, and total message size.

**A ledger keyed on client-supplied strings is a memory-exhaustion surface**, which is why
`RequestLedger` already carries a TTL, a per-owner cap, and a global cap with oldest-first
eviction. Those bound what is *stored*; a length cap on `request_id` bounds what can be
*allocated on the way in*, and that check has to come first.

Use existing project conventions rather than inventing limits: `RateLimiter` already exists
(`ratelimit_test`, 17 checks) and the chat flood control sets the precedent for per-player
rates. **Do not declare an unbounded string.**

---

## 10. Revision expectations, and the Stage 6 stale policy

Unchanged from Stage 5 — Stage 6 adds observers, not rules:

- primitives never advance; **transaction boundaries advance once per participant**
- **headroom prevalidated for every migrated participant before any mutation**
- `UINT64_MAX` refuses and sticks; never wraps, never saturates while committing
- **migration remains the only 0 → 1 transition**
- **`Match` never validates values**

Stage 6 stale policy — **observe all four, refuse none**:

| View | Stage 6 |
|---|---|
| `Match` | observe |
| `Stale` | observe |
| `Future` | observe **strongly** — the client claims a version that has never existed |
| `Legacy` | observe |

And the rule that must survive into Stage 7:

> **`Match` != trusted possessions.** A correct revision beside a forged balance is still a
> forged balance. Matching revisions mean only "the base version you claim is not obviously
> stale". Stage 7 closes the authority door; the revision never opens it.

---

## 11. Duplicate stacks — Option A, at migration, NOT in Stage 6

**Decision recorded: Option A — canonicalize at migration.** Not implemented in Stage 6, and
not implemented silently anywhere.

Current behaviour, asserted deliberately in `economymutator_test.cpp:168-180`:

```
Inventory: [{0x1111, 10}, {0x1111, 90}]
Held(0x1111)           -> 10        (first stack only, not 100)
RemoveItem(0x1111, 20) -> InsufficientQuantity
```

Duplicates can only arrive from a client save: `SaveCharacterRequest.inventory` is a repeated
list the server stores verbatim, with no merge step. Under-reporting is the safe direction
today, but at Stage 7 the server's view becomes the only view, and the hidden 90 becomes
permanently unreachable value the player legitimately owns.

**Required order when it is implemented:**

1. read-only inspection
2. report every duplicate item id
3. calculate total quantities
4. review
5. deterministic canonicalization, as part of the controlled migration
6. audit every changed record
7. commit only through the migration transaction

Canonicalization preserves total legitimate quantity: `[{X,10},{X,90}]` → `[{X,100}]` — not 10,
not 90, not 110.

### 11.1 Overflow policy — define before implementing

`ItemStack.quantity` is `uint32`. If summed duplicates exceed what it can represent:

```
DO NOT clamp        DO NOT wrap        DO NOT discard items
```

**Block that record from migration and report it for review.** This matches the existing
inspect → refuse-unless-clean → commit shape of `InspectEconomyMigration` /
`CommitEconomyMigration`, and it matches `Economy::AddItem`, which already returns `Overflow`
rather than clamping.

### 11.2 This amends a Stage 3 invariant — explicitly, later

Stage 3's migration is currently **byte-for-byte on inventory**: it stamps `MigratedAt` and
`EconomyRevision` and changes nothing else. Canonicalization would change inventory contents
during migration.

**That amendment must be made explicitly when we reach the cutover step, and reviewed then —
not silently now.** Recording it here so it cannot be missed: `playerstore_migration_test`
(35 checks) currently encodes the byte-for-byte expectation, and those tests are the thing that
will notice.

---

## 12. HARD GATE — the Stage 7 vanilla inventory-source audit

**Stage 7 must not remove `SaveCharacterRequest.inventory` until this audit is complete.**

The premise to be tested is one this very document asserted in an earlier revision: that the
economy's whole surface is starter kit → `/pay` → trade → vehicle sale. **That is the
SERVER-CODED surface. It is not proof that those are the only ways a Cyberpunk character can
gain or lose persistent items**, and the two are not the same claim.

The failure mode is precise and severe: remove client inventory declarations while legitimate
vanilla inventory changes still depend on them, and we would "fix duplication" by making
players' real possessions disappear after reconnect.

Every source must be investigated explicitly:

```
world loot                NPC/vendor purchases      NPC/vendor sales
drops                     pickups                   containers / stashes
crafting                  dismantling               consumables
clothing / equipment       weapon acquisition        quest or script grants
any other vanilla inventory API
```

For each, determine:

| Question |
|---|
| Does the server already model it? |
| Can the server validate it? |
| Must it be disabled? |
| Must an authoritative intent path be built **before** Stage 7? |

**§2 raises the priority of this audit above its position in the stage order.** It is the only
thing that can say what new protocol surface Stage 6 actually needs — and it is read-only
investigation, so it blocks nothing and can start immediately.

---

## 13. Compatibility and deployment

### 13.1 Coexistence is impossible — verified

`GameServer::HandleAuthentication` (`GameServer.cpp:906`) denies on either identifier mismatch
**before** password, manifest, or Discord checks:

```
client_protocol != client::kIdentifier  -> Deny(kProtocolMismatch)
server_protocol != server::kIdentifier  -> Deny(kProtocolMismatch)
```

with *"Your mod is built against a different protocol than this server. Open the launcher and
update."* Two further gates follow: `manifest_version` equality and `install_digest`
(`:942-969`).

**Protocol lockstep is mandatory and already enforced.** No negotiation, no version range, no
partial compatibility. This is a good property — it fails loudly at the door rather than
misparsing a presence bitfield.

### 13.2 Deployment

**Both artifacts must already exist and have been tested together before the window opens.**
"Server first" is about installation order, not build order.

1. build server **and** client
2. test that exact pair together
3. stage both artifacts
4. **verify backups** (§14, and `players.json`)
5. take the server down
6. install the new server
7. activate the matching client/manifest update immediately
8. start the server
9. execute smoke tests
10. reopen normally **only after** smoke tests pass

Server-before-client at install time because the failure is legible: old clients are denied
with an accurate message telling them to update. Client-first would deny updated clients with a
message that is now *misleading*, generating reinstalls and reports against a working launcher.

### 13.3 Blocked on zeldfep — do not schedule a date until answered

- how the launcher detects a new manifest/build
- whether the update is mandatory
- whether the client can be pre-staged
- how quickly clients receive it
- what the user sees
- whether rollback can force the old client build again

The manifest/digest machinery (`docs/MANIFEST-ARCHITECTURE.md`) is the lever. Note that
`m_manifestVersion` being empty means "no manifest configured", which reads as every old client
at once — worth confirming the deploy actually sets it.

### 13.4 Why Stage 7 is a separate flag day

Stage 6 exists to **measure**. Removing client authority in the same swap that builds the
instrument means the instrument never reports on the world it was built to measure — we would
cut over on prediction rather than evidence, and the migration is irreversible in the direction
that matters. Two flag days is the price of finding out we were wrong while it is still cheap.

---

## 14. Rollback

**All-or-nothing, for the same reason coexistence is impossible.** Binaries roll back cleanly;
nothing about the protocol is persisted.

**Migration must NOT be activated in the same deploy as the protocol swap.** A rolled-back
server would not know what `MigratedAt` and a non-zero `EconomyRevision` mean — not corrupt,
but no longer a state we have reasoned about. The swap must be reversible **without** restoring
economy data.

**`players.json` backup before the window is mandatory.** Stage 1's atomic writes keep one
`.bak`, which protects against a crash mid-write — it is not a restore point for a bad deploy
and must not be treated as one.

### 14.1 Rollback triggers — into the runbook before the window

Immediate rollback for any of:

- a known-good **updated** client still receives a protocol mismatch
- server crash or assert in authentication or any new economy handler
- the same `request_id` executes an economy mutation more than once
- one account can replay another owner's `request_id`
- authoritative server `Money` differs from what the matching new client receives
- authoritative `EconomyRevision` differs from the revision the matching client receives
- a freshly resynced controlled client immediately reports an unexplained stale revision
- `NotifyMoney` corrupts or narrows a balance above `INT32` in the dedicated test
- a persistence failure leaves live transaction state committed inconsistently
- **any unmigrated character becomes `MigratedAt > 0`**
- **any ordinary unmigrated character becomes `EconomyRevision > 0`**
- `players.json` fails validation or reload
- unexplained loss or creation of value during controlled two-client smoke testing

**Not a rollback trigger:** old clients being denied before they update. That is expected
lockstep behaviour.

---

## 15. Tests

Existing coverage carries forward — **17 files, 491 checks**, green after the netpack fix.
Two are load-bearing during the swap:

- `trade_real_test` — **ordinary play never migrates anybody**
- `revision_test` — the transaction/primitive rule and exhaustion behaviour

Done already (§1): netpack enum codegen, compile, and round trip — 18 checks, plus the
byte-for-byte proof that production identifiers are unchanged.

Before the swap ships:

1. **Revision propagation, end to end** — a client that applies `SpawnCharacterResponse` /
   `NotifyPossessions` / `NotifyMoney` and echoes the revision back classifies as `Match`.
   Proves §3.2 and §5 close the loop. **Needs two processes.**
2. **Stale classification** — each of Match/Stale/Future/Legacy is classified correctly and
   **refused in none of them**.
3. **`NotifyMoney` above int32** — the truncation is gone, not just widened on paper.
4. **Revision scoping on the client** — character A's revision never travels to character B,
   and it resets on each event in §5.
5. **Unknown enum refusal** — *if* §12 produces an enum-bearing request. §1.4 proved the wire
   layer will hand the handler an undeclared value.
6. If a new request lands: exhaustive result mapping; idempotency through the real ledger
   (same id → work once, same response; different id → twice; cross-owner replay refused);
   empty `request_id` stays safe as non-idempotent; response carries authoritative state on
   failure.

`tools/Verify.ps1` cannot reach items 1 and the Stage 8 exploit testing — those need two
clients and a live server, and Verify's own closing line says so.

---

## 16. Repository integrity — DONE

Approved and completed. No push, no history rewrite.

```
bundle:  C:\Users\Cam\nco-backup\cyberpunkmp-20260904-2240.bundle
size:    22,157,755 bytes (21.1 MB)
verify:  "The bundle records a complete history."
refs:    12 local branches + tags; feat/world-state = 17d6d0a (Stage 5)
```

Proven by restoring, not just by verifying: a clone from the bundle came back with **867
commits**, HEAD at the Stage 5 commit, with `EconomyMutator.h` and `revision_test.cpp` intact.

**Caveats worth keeping:**

- A bundle carries **committed history only**. This document was untracked when the bundle was
  made and was therefore not in it — which is exactly why it is committed now. Re-bundle after
  landing work.
- Submodules (`ArchiveXL`, `Codeware`, `RED4ext.SDK`, `TweakXL`) are gitlinks; their contents
  come from their own upstreams and are not inside the bundle.
- It is a point-in-time snapshot on the same physical machine. It removes the
  one-branch-one-folder risk; it is not offsite.

---

## 17. Deliberately NOT in this swap

| Excluded | Why |
|---|---|
| Removing `money`/`inventory` from the save | Stage 7 — §13.4, and gated on §12 |
| Activating migration | Stage 8 — §14; must not share a deploy with the swap |
| Duplicate-stack canonicalization | §11 — Option A, at migration, reviewed then |
| Generic `EconomyRequest` / verbs | review decision; and §2 found nothing needing them |
| `VehiclePurchaseRequest` / `MoneyTransferRequest` | §2 — already chat-driven and authoritative |
| Phone `request_id` | review decision 2 |
| `EconomyResponse.detail` | review — client-visible internal reasons |
| Periodic authoritative possession push | no evidence of drift the event paths miss |
| Movement coalescing wire changes | unrelated; the flag day is not a coupon |
| Character session lock | still required, belongs with the selector |
| Selector protocol | parked |

---

## 18. Open questions

1. **Sequencing (§2.1)** — run the §12 audit *before* finalising the Stage 6 surface? My
   recommendation is yes: it is read-only, it blocks nothing, and it is the only thing that can
   say what the new surface must be.
2. **Launcher behaviour (§13.3)** — six questions for zeldfep. No swap date until answered.
3. **Stage 3 amendment (§11.2)** — confirm the byte-for-byte inventory invariant is amended
   explicitly at the migration cutover, with `playerstore_migration_test` updated in the same
   change.
4. **Bundle cadence (§16)** — one-off, or a step in the deploy runbook? Recommend the runbook.

---

**Migration inactive. Selector parked. Nothing pushed. No production `.proto` modified.**
