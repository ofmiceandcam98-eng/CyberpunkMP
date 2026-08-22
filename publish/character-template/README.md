# The world template

This is the save every multiplayer player loads. It is **not anybody's character** — it is
the world, in a known-good state, with Dogtown already open.

Replaced on 2026-08-20. The previous template sat at `get_to_combat_zone` — the opening of
Phantom Liberty — which meant every player arrived with Songbird calling them, Dog Eat Dog
tracked, and the Dogtown gate shut in their face.

## What is in it

| | |
|---|---|
| Level / street cred | 34 / 44 — **overridden to 15/15 by the server**, see below |
| Body / lifepath | Female, Street Kid |
| Position | Dogtown, on a minor-quest objective |
| Game version | 2300 (patch 2.3) |
| Modded | **no** |
| Dogtown | **open, gate working both ways** |

## Why the level does not matter

The server clamps a new character to level 15 on its first capture. The template's own level
is an accident of which save happened to be usable, and letting it decide everyone's
starting power would mean that swapping the template for world-state reasons silently moves
the whole server's difficulty. Level and street cred are both proficiencies, so one clamp
covers both.

## Why patch 2300 rather than 2310

There was no choice. Every unmodded save past the Phantom Liberty intro is on 2300; the only
2310 saves are at `get_to_combat_zone`, which is the problem being fixed. 2.3 to 2.31 is a
single point release rather than the 2.1/2.12 gaps rejected previously, and the migration
was **tested** on 2026-08-20: the save loads, Dogtown streams, and the gate works.

## Why not just unlock the gate

Traced from the game files first. The obvious lever was `ep1_standalone`, and it is read by
**69 quest phases** — the root quest graph, Act 1, the metro, open-world activities,
apartments, the lifepath quests, and `ep1_community.questphase`, which is Dogtown's own NPC
population. Clearing it would have depopulated the district it was meant to open.

The crossing facts (`by_car_dogtown_crossed` and friends) are genuinely local to one file,
and setting them still did not work: while `q301_border` is active it owns the gate devices,
so the open-world checkpoint logic never gets a say. Forcing the doors with `ForceOpen`
would have produced a gate that opens without working — turrets hostile, guards alerted.

A save where the story already handed the gate back is the only route that leaves the
checkpoint functioning as a checkpoint.

## Tested 2026-08-20

Loads clean, drove out of Dogtown through the gate, drove back in. Both directions.

## The male template

`../character-template-male` is still on the old `get_to_combat_zone` state, so male players
still meet a locked Dogtown. Body gender cannot be changed in game, so the fix is a male
playthrough to the same point — not a conversion of this one.
