# The world template

This is the save every multiplayer player loads. It is **not anybody's character** — it is
the world, in a known-good state, with the story already out of the way.

Rescued from `AutoSave-0` on 2026-08-14. It was an autosave, which the game overwrites, so
the original is already gone or will be shortly. This copy is the one that matters.

## What is in it

| | |
|---|---|
| Level / street cred | 15 / 15 |
| Act 1 | finished — 63 quests done |
| Position | Night City, told to head for Dogtown |
| Lifepath | Corporate, female body |
| Game version | 2310 (patch 2.31) |
| Modded | **no** |

## Why this one

Three other saves matched "level 15 and past Act 1" and all of them were unusable:

- **`isModded: true`.** A save made with mods can refuse to load, or load wrong, on a
  machine that does not have them. This one goes to *every* player, so it has to be clean.
- **Older game versions** (2.1, 2.12). The game migrates an older save on load, which is a
  risk taken once per player for no benefit when a matching-version save exists.

This one is unmodded and built on the exact patch the mod targets.

## Why the tracked quest does not matter much

`get_to_combat_zone` is an open-world objective — "go to Dogtown" — not a scripted scene.
V is standing free in Night City and can walk away from it. That is the important property:
saves taken mid-mission drop players into an elevator or a cutscene, which is how a template
breaks the game's own systems.

## What still has to happen to it

Nothing here removes the singleplayer story or grants a loadout — that is the
initialisation step, and it runs per character on first spawn rather than being baked in.
Baking it in would mean every change to the starting loadout needed a new template.

## Replacing it

Any save works as long as it is: **unmodded**, on the **current game patch**, **past Act 1**,
and **not mid-mission**. Drop the three files in and keep the folder name.
