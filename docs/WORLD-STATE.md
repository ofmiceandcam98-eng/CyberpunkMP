# World state: one map, one truth, many renderers

A design for review, not a change. Companion to `NETCODE-AUTHORITY.md` — that document
settles *who simulates an entity*; this one settles *what the world itself is* and where
its state lives. It answers the requirement as stated: **the map is the same for all,
rendering happens on each player's machine, and all shared assets and interactions are
server-side.**

Confidence markers as before: VERIFIED means read in the code on main, INFERRED means
deduced from architecture, GUESS is labelled.

---

## What the requirement means in this engine

Cyberpunk 2077 ships the entire map — geometry, textures, sector streaming — inside every
client install. Rendering has therefore *always* been per-machine; no bytes of Night City
ever cross the wire, and none should. What the server must own is not the map but the
**state layered on top of it**: the time of day, the weather, which shared objects exist
and what condition they are in, and the outcome of every interaction that more than one
player can observe.

So "all assets and interactions server-side" decomposes into four concrete guarantees:

1. **Same world loaded** — every connected client is running the same map baseline
   (parity gate).
2. **Same world texture** — clock and weather come from the server, not each client's
   local simulation.
3. **Same objects** — anything shared exists as a server entity with server-owned state;
   clients hold render proxies.
4. **Same outcomes** — interactions are requested by clients, validated and decided by
   the server, and the result is broadcast. A client never tells the world what happened;
   it asks.

## What exists today

| Guarantee | Current state | Where |
|---|---|---|
| Same world loaded | Partially. `AuthenticationRequest` carries `client_protocol`/`server_protocol`, so mismatched *code* is refused at the door (VERIFIED, `client.proto`). But nothing identifies the *world template* — a client on the female starting world and one on the male world would both authenticate. The launcher already treats body type as world-defining, so the hole is real, not theoretical. | `client.proto`, launcher body-type tool |
| Same world texture | Absent. Clock and weather are each client's private singleplayer simulation (VERIFIED: no time/weather message exists in either proto file). | — |
| Same objects | Only player-spawned entities (characters, summoned vehicles) exist server-side, parented to their spawner (VERIFIED, `Level.cpp`). Doors, containers, props, parked cars: per-client fiction. | `Level.cpp` |
| Same outcomes | Absent, with one honourable exception: chat and `/name` flow through the server. Everything else a player does to the world is invisible to everyone else. | `ChatSystem.cpp` |

The good news mirrors the authority document: the substrate (flecs server world,
observer-driven replication, protocol versioning that auto-bumps on any surface change)
already supports all four guarantees. Nothing below invents new machinery; it extends
what merges from the authority work.

## Design

### 1. Parity gate — refuse divergent worlds at the door

Add to `AuthenticationRequest`:

```
uint64 world_hash = 5;   // FNV1a64 over (world template id, mod data version)
```

The server compares against its configured value and refuses with a reason string the
launcher can show ("Your launcher is set to a different starting world than this
server"). Editing the proto bumps the netpack protocol identifier automatically
(VERIFIED mechanism), so old clients that don't send the field are already refused by
the existing check — the gate composes with what's there.

Cost: one field, one config entry, one launcher string. This is the cheapest guarantee
in the document and the precondition for the other three: no amount of state sync helps
if two players aren't standing in the same world to begin with.

### 2. Clock and weather — the server's metronome

One new server→client message, sent on join and on change:

```
message NotifyWorldState {
    uint64 game_time_seconds = 1;  // canonical server game-clock
    float  time_scale = 2;         // config: how fast the day turns
    uint64 weather_id = 3;         // TweakDBID, same convention as appearance fields
    float  transition_seconds = 4; // blend, so weather rolls in rather than pops
}
```

Client-side, a small `WorldStateSystem` applies it through the game's own time/weather
facilities and then *re-asserts it* on an interval, because singleplayer systems will
drift the clock and roll their own weather given the chance (INFERRED — needs the same
suppress-the-singleplayer-brain treatment vehicles needed).

The server ticks game time itself — it is authoritative even when empty, so the city
has a continuous timeline rather than resetting to noon when the first player joins.
Persistence: one row (current game time), written on the existing save cadence.

This is Phase 3 of the authority document, unchanged, and it is deliberately first
here too: highest shared-world payoff per line of code, zero dependence on the
authority machinery.

### 3. Server-declared objects — two kinds, one rule

The rule: **if two players can both see it change, its state is a server entity.**

**Static world objects** (doors, containers, parked cars, vendor terminals — things the
map itself places): every client already has an identical copy with an identical static
`EntityID` (VERIFIED for parked cars in the Phase-2 adoption design; INFERRED as the
general case since IDs are baked into the shipped map sectors). These do *not* need
spawning or position sync — they need a **state overlay**: server entity keyed by the
static ID, holding only the mutable bits (open/closed, looted/full, destroyed/intact).
Replication is the existing flecs-observer broadcast. A client joining late receives the
current overlay for its interest area and applies it — the same catch-up shape the
movement code already uses, minus interpolation.

**Dynamic server objects** (props an admin or server plugin spawns: event decorations, a
street market, a crashed AV for tonight's RP scene): full server entities, owner =
server (parent none, epoch machinery from the authority model applies unchanged).
Clients spawn a local proxy on notify, exactly as they do for other players' vehicles
today. Plugins get a spawn/despawn API — this is what makes the server *programmable*
as an RP platform rather than just a relay.

What stays per-client fiction, deliberately: ambient crowd and traffic (Phase 4's
sync-or-suppress question, unchanged), litter physics, birds, rain particles — anything
whose divergence between machines no player can ever notice.

### 4. Interaction sync — ask, don't tell

One request/response pair generalises every shared-object interaction:

```
message InteractRequest  { uint64 object_id = 1; uint64 interaction_id = 2; }
message NotifyInteraction {
    uint64 object_id = 1;
    uint64 interaction_id = 2;
    uint64 actor_id = 3;      // who did it — so clients can play the right animation
    bool   accepted = 4;      // request path only; broadcasts are always accepted=true
}
```

Client presses the button → `InteractRequest` → server validates (does the object
exist, is the actor close enough, is the door locked, is the container already looted)
→ mutates the overlay entity → broadcast `NotifyInteraction` to the interest area,
including the actor. The actor's client plays the interaction on *receipt*, not on
key-press — a rejected request costs the presser ~an RTT of unresponsiveness, which at
RP-server scale is invisible and buys the property that matters: **no client can
assert a world change; it can only request one.** Distance and state checks server-side
mean a hacked client can spam requests but cannot open a locked door, double-loot a
container, or interact across the map.

### Persistence — the question that shapes the rest

Characters already persist (VERIFIED, the character DB). The new state divides into:

| State | Proposal |
|---|---|
| Game clock | Persist. One value; the city keeps its timeline across restarts. |
| Weather | Don't persist; re-roll on boot. Nobody remembers yesterday's rain. |
| Static-object overlay | **Open question.** Persisting looted/opened forever makes the world permanently strip-mined; never persisting resets on every restart. Proposal: persist with per-type decay (doors relock on restart, containers refill on a config timer) — RP servers want a world that heals. |
| Dynamic objects | Persist spawn records (a plugin-built market survives restart), owner-server, no decay. |

## Ordering

1. **Parity gate** — one field; do it inside the Phase-1 protocol bump so the version
   only breaks once.
2. **Clock/weather** — independent, small, visible to everyone in the first minute.
3. **Static overlay + interactions** — doors and containers first (binary state, easy
   validation), vendors later (they touch money, which touches persistence policy).
4. **Dynamic server objects + plugin API** — after the overlay proves the replication
   path, because it reuses all of it.

## Open questions for Cam / the host-side Claude

1. Persistence policy for the static overlay — decay timers, and who tunes them?
2. Should dynamic-object spawning be admin-command only at first, or plugin API from
   day one? (The API is more work but is the actual product.)
3. Vendor/money interactions imply a server-side currency ledger eventually. In scope
   for this arc, or its own design doc?

Signed: Claude (zeldfep's machine) — current-state table VERIFIED against main at
`40172b6`; static-EntityID generality INFERRED pending a check on door/container IDs
across two installs.
