# Netcode: entity authority, and the road to whole-game sync

A design for review, not a change. Written from the second checkout after reading the
netcode end to end; every claim about current behaviour below is VERIFIED against main at
`5de83c9` unless marked otherwise. Nothing here is implemented yet — the point of this
document is to agree on the model *before* protocol-touching code gets written, because
this is the item the TODO already calls out as needing an ownership model rather than a
patch.

---

## What exists today (the part we should keep)

The framework this fork inherits is the TiltedPhoques lineage — the same architecture
that runs Skyrim Together — and most of the hard substrate is already here and working:

| Layer | What it is | Where |
|---|---|---|
| Transport | GameNetworkingSockets; reliable + unreliable channels (`MoveEntityRequest` is tagged unreliable) | `vendor/`, `code/protocol/client.proto` |
| Serialization | protobuf via the netpack codegen | `code/protocol/` |
| Server world | flecs ECS; replication is flecs observers (`OnSet` → broadcast) | `code/server/native/Components/*` |
| **Authority** | **Entity parentage.** Everything a client simulates is `child_of(player)` on the server, and `HandleMoveEntityRequest` refuses movement from anyone but the parent's connection. This is a real authority check, enforced on every packet. | `Level.cpp` |
| Authority transfer (one case) | Taking `seat_front_left` reparents the vehicle to the driver and sends `NotifyVehicleControlAssigned` | `AttachmentComponent.cpp` |
| Interest management | Distance-banded send rates: full ≤200m, 1/4 ≤600m, 1/16 beyond; mounted entities not replicated at all | `MovementComponent.cpp` |
| Client remote-side | Interpolation between two samples at 30/s; network vehicles kinematic and driven by `ForceMoveTo` (PR #1) | `InterpolationSystem.cpp`, `VehicleSystem.cpp` |
| Versioning | `AuthenticationRequest` already carries `client_protocol` / `server_protocol`, so a protocol bump rejects old clients cleanly at the door | `client.proto` |

The design below deliberately builds on parentage-as-authority instead of inventing a
parallel ownership system. It is proven, it is already enforced, and every gap we have is
a *transition* problem, not a steady-state one.

## The gaps (why passengers bounce and control strands)

1. **Authority is assigned but never revoked.** When the vehicle reparents to a new
   driver, the old owner's client is never told. It keeps simulating locally — its
   `MoveEntityRequest`s now fail the parent check (as warn-spam), but its local physics
   still fights the interpolation. This is the "passenger vehicle physics is simulated
   independently on both machines" bug on the TODO.
2. **No client-side reaction to gaining authority.** `NotifyVehicleControlAssigned` sets a
   variable (`m_vehicleRemoteId`) and nothing else. A network-spawned vehicle stays
   kinematic even for the player who now controls it (documented in PR #1) — you can be
   assigned a car you cannot actually drive.
3. **Stale movement after transfer.** `MoveEntityRequest` is unreliable and unordered
   across a transfer: packets from the previous owner can arrive after the handoff. The
   parent check happens to reject them today, but only because transfer = reparent; any
   authority change that isn't a reparent would silently accept old-owner movement.
4. **No handoff rules.** Driver exits with a passenger still inside → the vehicle stays
   parented to the ex-driver, nobody simulates it authoritatively. Driver *disconnects* →
   `RemoveOwnedVehicles` destroys the vehicle outright — with the passenger in it.
5. **Nothing outside player-spawned entities has an owner at all.** NPCs, traffic, and
   world state are per-client fiction; each machine sees its own city.

## The model: explicit, epoch-guarded, transferable authority

Keep parentage as the source of truth for *who* owns an entity. Add three things: a
generation counter, two messages, and defined transfer rules.

### Server

```
struct AuthorityComponent   // on every server entity a client simulates
{
    uint32_t Epoch{0};      // bumped on every transfer; parentage stays the owner
};

void TransferAuthority(flecs::entity aEntity, flecs::entity aNewOwnerPlayer)
{
    // 1. bump Epoch
    // 2. reparent: aEntity.child_of(aNewOwnerPlayer)   (nullptr → server-owned)
    // 3. to the NEW owner:  NotifyAuthorityAssigned { entity, epoch }
    // 4. to the OLD owner:  NotifyAuthorityRevoked { entity, epoch }
    // 5. everyone else: nothing — they were interpolating and keep interpolating
}
```

`MoveEntityRequest` gains `uint32 epoch`. The server drops any movement whose epoch is not
current for that entity — silently, no warn: a stale packet after a handoff is the
expected race, not an attack. (The parent check stays; epoch is the ordering guard,
parentage is the authorisation.)

### Client

Two handlers with exactly two jobs:

- `NotifyAuthorityAssigned` → become the simulator: physics on, `SetIsPlayerControlled`
  as appropriate, start sending `MoveEntityRequest` at the normal rate with the new epoch.
  For vehicles this is the inverse of PR #1's `MakeRemoteDriven` and closes its
  documented control-handoff gap.
- `NotifyAuthorityRevoked` → stop being the simulator: `MakeRemoteDriven` (kinematic,
  interpolated), stop sending movement for that entity.

`NotifyVehicleControlAssigned` is subsumed: it becomes `NotifyAuthorityAssigned` for the
vehicle entity, and the vehicle-specific message is retired at the same protocol bump.

### Vehicle transfer rules (phase 1 policy)

| Event | New owner |
|---|---|
| Player takes `seat_front_left` | That player (existing behaviour, now epoch-guarded) |
| Driver exits, occupants remain | Next occupant by seat priority: front_right → back_left → back_right. They cannot drive from the back seat, but their machine simulates the parked car coherently — one simulator at all times is the invariant. |
| Driver disconnects, occupants remain | Same as exit. `RemoveOwnedVehicles` transfers-then-checks instead of destroying: only a vehicle with **no** occupants is destroyed (existing `ReleaseVehicleIfEmpty` behaviour, kept). |
| Last occupant leaves | Destroyed (existing behaviour, kept) |

## Phases

**Phase 1 — vehicles (the agreed first step).** Everything above. Fixes on the TODO it
closes: passenger physics bounce (single-simulator invariant), stranded control after
driver exit/disconnect, PR #1's control-handoff gap. Protocol bump: +1 field on
`MoveEntityRequest`, +2 messages, −1 message (`NotifyVehicleControlAssigned`). Testable
with two players and one car: drive, swap seats, exit with a passenger aboard,
disconnect with a passenger aboard.

**Phase 2 — parked-car adoption + traffic hygiene (already scoped, now framed by the
model).** Parked world cars have identical static EntityIDs on every client, so remote
clients mount the character into *their own copy* instead of spawning a duplicate —
object permanence with zero new sync. Local traffic clearing in a small radius around
network vehicles stops remote cars clipping through per-client traffic fiction.

**Phase 3 — shared world texture: clock and weather.** Server-authoritative game time
and weather, broadcast on join and on change. Small, cheap, no authority model needed —
but it is the highest RP-value-per-line item in this document: everyone stands in the
same rain at the same hour. Could ship before Phase 2 if wanted.

**Phase 4 — NPCs and crowd (design-gated, not committed).** The same
`AuthorityComponent`/epoch machinery, applied per grid cell: the nearest player's client
simulates the NPCs around them and streams movement; others render interpolated puppets.
Caps per client, hysteresis on handoff, despawn-for-remote beyond interest range. This is
the genuinely hard one — it multiplies entity counts by an order of magnitude and needs
the GridCell system finished (it exists but `TransferCell` is stubbed and broadcasts
still go to every player). It should not start until Phases 1–3 have proven the model
live. An honest alternative for an RP server is to *reduce* ambient NPC density near
other players instead of syncing it, which is drastically cheaper and may read better.

**Non-goals.** Quest/mission sync. Singleplayer quest state is deeply engine-coupled,
and an RP server's life is emergent rather than quest-driven. Revisit only if the RP
design demands a specific scripted scene.

## Open questions for Cam / the host-side Claude

1. Seat-priority order for driver handoff — or should a driverless occupied car transfer
   to the *server* (owner = none) and simply freeze until someone takes the wheel?
2. Is Phase 3 (clock/weather) wanted early? It is independent and small.
3. Phase 4: sync the crowd, or suppress it near remote players? The second is cheap and
   might be the better RP call.
4. Protocol bump cadence — one bump for all of Phase 1, or per-PR?

Signed: Claude (zeldfep's machine) — VERIFIED for the current-state table, INFERRED for
the failure mechanics in "gaps" until the two-player tests run.
