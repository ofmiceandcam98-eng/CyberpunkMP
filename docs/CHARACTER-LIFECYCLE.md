# Character lifecycle

**What this is:** the design for who a player *is* on this server — the difference between a
connection and a character, when it is safe to touch somebody's body, what a character id
looks like, and what happens when one is created or deleted.

Written 2026-08-30 after reading the OPX//77 framework docs (a Cyberpunk roleplay framework
for the Open77 runtime). Its platform is nothing like ours — Lua resources, a client export
bus, MySQL — so almost none of its *code* transfers. Several of its **decisions** do, because
it hit the same problems earlier and wrote down what it learned. Where a rule below came from
there, it says so; the shape it takes here is ours.

Read this before touching spawn, placement, the character selector, or `CharacterRecord`.

---

## 1. A connection is not a character

**The distinction we did not have, and paid for.**

| | Connection | Character |
|---|---|---|
| Exists from | the socket opening | the player choosing one |
| Dies when | they disconnect | they disconnect, or switch |
| Ours today | `PlayerComponent`, keyed on connection id | `CharacterRecord`, keyed on `CharacterId` |
| Durable id | Discord id (verified, survives) | `CharacterId` (ours, survives) |

Somebody sitting on the character selector **has a connection and no character.** Somebody
whose world just reloaded has a connection, and a character record, and a *body in the world
that is not that character*. Those are three different states and we have been treating them
as two.

That is the overwrite bug in one sentence. Cam connected, was restored correctly, pressed
join from the main menu — the world detached and rebuilt from his local save — and the
disconnect save wrote the singleplayer template over his character, because "connected and
restored once" was being read as "the body standing there is his character".

`m_characterLive` (`fbdff4a`) is the patch: set when a restore completes, cleared by any
world detach. It works, and it is one boolean standing in for a state machine. The states
we actually have:

```
CONNECTED            connection up, no character chosen
  -> SELECTED        a character is chosen, nothing placed yet
  -> RESTORING       possessions/appearance being applied
  -> LIVE            the body in the world IS this character   <- only state where saving is legal
  -> DETACHED        world torn down; the body is no longer ours
```

`DETACHED` is the state that did not exist before and cost a character. Every save path asks
`MaySaveCharacter()`, which is `LIVE` plus the deliberate-creation exception.

**Rule.** Anything that writes to a `CharacterRecord` must be able to say which state the
player is in. If it cannot, it must refuse rather than guess. Losing a session's progress is
recoverable; overwriting a stored character is not.

---

## 2. Never touch a body before it is ready

**Borrowed wholesale, because it describes our crash class.** OPX//77's rule:

> Do not teleport, spawn, kill or force a respawn on a player until their readiness gate has
> opened. Acting server-side on a client that is not incarnated crashes that client.

That is the entity-readiness theory `docs/CRASH-FIX-BRIEF.md` was built on, stated as a rule
instead of a suspicion. It is not the flecs race — that was separate and is fixed (`559828f`)
— but it is the *other* half of the same family, and the mount crash entry in the Map still
has an open case that fits it exactly.

What we have today is a **settle poll**: `Update()` waits for the player to exist, then waits
180 ticks (~3s), then restores. It works and it is implicit — nothing else in the codebase
can ask "is this player ready", and nothing announces when they become ready.

What their design has that ours does not:

- **A hold declared once at boot**, so every joiner is held before they exist. Not a race to
  grab a lock after the fact.
- **Two deadlines, ours shorter than the platform's.** If the platform's timeout wins, the
  gate opens with the player possibly still in the selector, holding no body at all. Ours
  must expire first so *we* get to say why.
- **A note on release.** Release carries a reason — `roster-failed`, `selection-timeout`,
  `done` — so anything waiting learns what happened instead of just being unblocked.
- **Release is idempotent**, and safe for a player who never had a hold.

**What to do here.** We have no host gate to plug into, so ours is our own: a per-player
readiness state on the server, set when placement completes, readable by anything that wants
to act on that player, and carrying a reason when it opens early. The 3-second settle poll
becomes the *implementation* of that gate rather than a private detail of the restore.

**Ordering, which we already get right and must keep:** log in, place, *then* release.
Releasing first lets everything else act on a player who is not where they belong yet.

---

## 3. Placement is not a teleport

**This one is a live finding about our code, not a hypothetical.**

We place players with a raw transform:

```
NetworkWorldSystem.cpp:488   Red::CallVirtual(this, "TeleportLocalPlayer", position, rotation)
```

OPX//77 refuses to do this, and gives the reason:

> The respawn transaction carries the fade, the streaming preload and the grace window that a
> direct teleport skips.

Their sequence is kill → respawn with the target position, the stored health and a grace
window, then armour applied afterwards because armour is not a respawn option and the body is
about to be replaced.

We cannot copy that call — it is their runtime's API. **What transfers is the question:**
does Cyberpunk 2.31 give us a placement path that preloads streaming and covers the transition,
and are we skipping it? Worth answering before the next spawn bug, because "we teleported into
a cell that had not streamed in" is consistent with more than one thing we have seen — Cam
underneath the map with an invisible character, most recently.

**Not a rule yet.** It is a verified difference between us and a framework that thought about
it, and an experiment worth running. Do not "fix" this speculatively.

---

## 4. Character ids people can read aloud

**Ours today:** 16 random hex characters, `GenerateCharacterId()` in `CharacterRecord.h`.

```
a3f9c2d18b4e7061
```

**The problem, now that Cam has asked for `/rename` and four slots:** these get typed. An
admin repairing a name types one. A player reporting a problem reads one out. Sixteen
undifferentiated hex characters have no structure to hold on to, and — the part that matters
— **a typo produces another valid id.** There is no check. The rename lands on a stranger's
character and nothing on screen reports an error.

OPX//77's answer, which is worth taking:

- **An alphabet with no ambiguous glyph.** No `0` against `O`, no `1` against `I` or `L`, no
  `5` against `S`. They use 23 symbols: `34679ACDEFGHJKMNPRTWXYZ`.
- **A check symbol**, weighted sum modulo a prime. Distinct weights per position, modulus 23.
  That catches **every single-symbol substitution and every adjacent transposition** — which
  is exactly what mis-hearing and mis-typing produce.
- **Grouped for reading**: `H7K-M4X3`.
- **Forgiving parse, strict content.** Upper-case the input, strip spaces and hyphens, then
  reject an unknown symbol rather than dropping it. Dropping turns one player's id into
  another's.

Their note on why the modulus must stay prime and the weights distinct is the important part:
change either and the check silently stops catching what it exists for.

**For us.** Migrating existing ids is not free — `CharacterId` is a stored key. Options, in
order of preference:

1. New characters get the new format; old ids keep working. The parser accepts both, and
   `/rename` and any future lookup take either.
2. Add the check symbol only to what is *displayed and typed*, keeping the stored id as-is.
   Cheaper, but two representations of one identity is exactly the confusion the format is
   meant to remove.

Do 1. Do not renumber anything that exists.

---

## 5. Slots, and deleting a character

Cam has asked for **four character slots for admins** and **delete a character**. OPX//77 has
both, and its design carries two traps we would otherwise walk into.

**Deletion is soft.** The row stays; the slot is freed. Three consequences:

- A character id is **never reissued**, so a stale reference can never resolve to somebody
  else's character.
- A deletion is undoable, which matters the first time somebody deletes the wrong one.
- **Create-and-delete writes a new row every time.** Hence a *lifetime row ceiling* per
  account, separate from the slot count. Without it, one account can grow the table forever.

**Lowering the slot limit never deletes anything.** An account over the limit keeps every
character and simply cannot make another. This has to be written down before the limit is
ever lowered, or somebody's character disappears because a config number changed.

**The slot number is not an identity.** Deleting character 2 of 3 leaves the third as slot 3.
Anything keyed on slot rather than id is a bug waiting for a deletion.

Our `CharacterRecord` already has `Slot` (always 0 today) and `CharacterId`. The wire already
carries a **list** of `CharacterSummary` — a deliberate choice recorded in `server.proto`:
*"a list of length one costs nothing today and is the whole difference later."* That day is
now. The missing piece is the client selector panel, which is also what blocks delete
(`MainMenu.reds:331-342`).

---

## 6. Cross-cutting rules worth stealing

Smaller than the above, each earning its place.

**Make a silent limit loud.** Their event payload budget refuses at 64 nodes because the host
silently drops anything past 1024 — *"a budget that refuses at 64 turns a payload that would
have vanished without a word into an error code you can read."* We hit the identical class of
problem in netpack: `SpawnCharacterResponse` reads a **6-bit** presence bitfield, and adding
two optional fields makes it 8, misaligning every field after them on any client not rebuilt
in lockstep. Nothing warns. That is a silent limit we should make loud.

**Refusals are stable codes, not sentences.** Every refusal answers a key that the caller can
branch on and a UI can render. We `Tell()` the player an English sentence, which cannot be
branched on and cannot be translated. Codes first, wording at the edge.

**Read a tunable at the point of use.** Capturing a live value into a file-scope local freezes
it for the life of the process. Their proxy makes this explicit; ours would too, if we had
operator-changeable numbers at all. We do not — every config change needs a server restart.

**A total order, or the list reshuffles itself.** Their status strip sorts on three keys, the
third being a stable tie-break on owner and id, *because the first two are not a total order
and the registry is walked with `pairs`*. Without it chips swap places on nothing but
iteration order. Anything we sort for display needs the same treatment.

**Fail closed, but keep the lobby open.** Their elevator gate closes every job-gated floor
when the core is unreachable — and a floor with no job requirement stays open through all four
failure modes, because *"a lobby that stops working while the core restarts is worse than
anything the gate protects."* Good shape for anything we gate on server state.

**Say which checks are hints.** Their elevator docs state plainly that the job check is a
client-side hint, that no setting makes it authoritative, and that gating money or contraband
on it is wrong. We should be equally explicit about ours — `MpInventory.ServerWants` is a
client-side read, and the settlement's decision about what to strip is made on the client.

**Nothing secret in the mirrored state.** Their `PlayerData` is mirrored to the client whole,
so nothing secret may go in it. Our `CharacterRecord` is not mirrored whole today, and should
not start being.

---

## What does not transfer

Recorded so nobody tries.

- **Their whole resource/export model.** Lua resources, a client-side export bus,
  `GetInvokingResource`, generations. We are a C++ plugin and redscript in one process; there
  is no resource boundary to police.
- **MySQL and migrations.** We persist to JSON on the server. Their append-only migration
  ledger is good practice, but for a different storage model.
- **Money types as a JSON column.** We have one balance and Cam has explicitly ruled out a
  banking system for now.
- **Their permission manifest.** Ours is Discord roles, already built.
