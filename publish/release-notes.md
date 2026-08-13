Unofficial build of [CyberpunkMP](https://github.com/tiltedphoques/CyberpunkMP) for **Cyberpunk 2077 patch 2.31**, plus the fixes needed to make it run on that patch. Upstream targets 2.2 and will not start on current game versions.

**Discord — Night City Online:** https://discord.gg/M9NSWsndC7

## What changed — v0.1.12

- **Cyber Engine Tweaks may now work alongside this mod — worth testing.** With the developer overlay off, the mod no longer creates *anything* on the graphics device and never takes part in a frame. CET draws its own overlay through the same path, and two overlays sharing one swapchain is what caused the GPU hard-lock. A mod that renders nothing cannot fight another renderer. **This is untested — if it hard-locks, tell us and turn CET back off.**

## What changed — v0.1.11

- **The mouse wheel actually scrolls chat now.** The previous attempt listened for an action called `mouse_wheel`, which does not exist in Cyberpunk — the game exposes the wheel as two separate buttons. Real bindings are now declared through Input Loader.
- All six bundled mods ship their licence texts. Input Loader's was the last one missing.

## What changed — v0.1.10

- **The mod no longer touches the GPU unless the developer overlay is on.** It was building and submitting a DirectX frame every single frame to draw an empty overlay. That work is now skipped entirely for normal players. A GPU crash was reported with no other explanation, and while this is not proven to be the cause, it removes the mod's renderer from the picture for anyone not running the overlay.

## What changed — v0.1.9

- **Chat reads as one line per message** — `name: what they said` — instead of the name sitting on its own line above. Halves how fast the chat box grows.
- **Mouse wheel scrolls the chat** while the input is open.
- **First-time install works again.** The launcher's "Install everything" was building its download URL from a constant that had been deleted, so it failed before downloading anything. The install package is now rebuilt and published with every release, with the current mod inside it rather than a days-old one.
- The install package now includes `INSTALL.txt` and the licence texts for the bundled mods, neither of which were actually in it.
- Release announcements name the installer explicitly instead of whichever file happened to be first, which is why the reported download size jumped around.

## What changed — v0.1.8

- The launcher shows **its own version** at the bottom of the window, and the update line now says **"Mod up to date"**. Those are two different version numbers and only one of them was ever on screen.
- The "Diagnostic build" link pointed at a fixed old release and went stale the moment releases became versioned. It now follows the current build.

## What changed — v0.1.7

- **`/return <player>`** puts someone back where `/tp` took them from. Staff pulling a player out of what they were doing can now undo it. Moderators and above.

## What changed — v0.1.6

- **Chat is colour-coded.** Yells are red, whispers pink-purple, adverts yellow. Local chat and server notices keep their normal colour, since those are most of what you see.
- **`/tp <player>`** brings someone to you, five metres in front, facing you. Admins only.
- **The debug overlay is off by default.** The in-game menu bar no longer appears for ordinary players. Devs and admins get a switch for it in launcher Settings.

## What changed — v0.1.5

- **Chat has range now.** Just typing is local and carries 30m. `/yell` carries 60m, `/whisper` 5m, and `/advert` reaches the whole server (admins only). `/help` lists what you personally can use. Press `;` to open chat.
- **Remote players should have their clothes and weapons.** Equipment was being sent as *names* produced by a debug helper that returns empty strings in release builds of the game — so every item arrived blank. It now travels as the item's actual numeric ID.
- Server messages addressed to you — usage text, refusals, `/who`, `/bans` — go to you alone instead of the whole server.

> **This build changes the network protocol.** Client and server must both be on v0.1.5; older clients will be turned away rather than half-working.

## What changed — 2026-08-13

- **Join from the main menu.** A `MULTIPLAYER` entry now sits beside Continue / New Game / Load Game. Picking it opens the game's own Load Game screen; choose a save and you arrive already connected.
- **Removed the automatic connect.** The previous build connected as soon as a world came up. Cyberpunk's main menu is itself a world, so it connected from the menu — before any save existed — and the game crashed as soon as the server sent player data. Joining is now something you choose.
- **The launcher updates itself.** It downloads new versions in the background and installs them when you restart it. No more running the installer by hand.
- **Proper install.** The launcher installs as *Night City Online Launcher* with its own icon, adds Desktop and Start Menu shortcuts, and has an uninstaller reachable from Settings.
- **Fixed a stray Windows cursor** sitting on top of the crosshair and the in-game cursor. The mod's debug overlay was forcing it on screen every frame.
- **Releases are versioned now** — `v0.1.4` rather than a dated test tag — so it is clear what you are running.
- Connection quality reads `n/a` instead of `-100%` before the network layer has measured it.
- In-game chat shows your Discord username.

## This is a work in progress

There is a **known crash when a second player spawns.** A likely cause has been found and fixed — a null component was being written to during the spawn handshake — but **this has not yet been confirmed with two real players**, so treat it as unproven. If you crash, your log is genuinely useful — see below.

## What's in the zip

- `mod/` — the mod itself: drop into `red4ext/plugins/` and rename to `zzzCyberpunkMP`
- `prerequisites/` — all six required mods, unmodified, so you can install in one pass
- `LICENSES/` — MIT license text for each bundled prerequisite
- `INSTALL.txt` — full instructions, including firewall and connection help

## Quick start

1. Back up `bin\x64`, `engine`, `r6` and `red4ext` from your game folder.
2. **Disable Cyber Engine Tweaks** — CET alongside this mod has caused a full GPU hard-lock. Rename `bin\x64\version.dll` to `version.dll.off`.
3. Extract all six prerequisite zips into your Cyberpunk 2077 folder.
4. Copy `mod` into `red4ext\plugins\` and rename it `zzzCyberpunkMP`.
5. Add launch options:

```
--online --ip=SERVER_ADDRESS --port=11778
```

The `=` signs are **not optional**. Writing `--ip 1.2.3.4` with a space silently fails — you connect to localhost and time out with no error explaining why. Without `--online` the mod stays dormant and the game runs completely normally.

## If it crashes, please send the log

This is the single most useful thing you can do right now.

The mod writes one log per launch to `red4ext\plugins\zzzCyberpunkMP\logs\`. Crashes no longer overwrite each other, so the file from the session that crashed is still there. Post the **whole file** in the Discord — not a screenshot, and not just the tail. The ordering across the entire log is what we read.

## Known issues

- **Crash when a remote player spawns.** A fix is in this build but is unconfirmed — it needs two people online at once to prove. Numbered checkpoints remain in the log to identify the exact failing statement if it happens again.
- Remote players have no clothes or weapons — item names come back empty on 2.31. Cosmetic.
- Remote player movement speed is reported as nonsense, so their animations look wrong.
- The `/` key does not open chat; a 2.2-era interface file is what registers it. Debug menu bar works as a fallback.

## Credits

CyberpunkMP is the work of **Tilted Phoques and contributors**. This is an unofficial community build and is not released by them.

Bundled prerequisites, all MIT licensed, all credit to their authors: [RED4ext](https://github.com/WopsS/RED4ext) (Octavian Dima), [redscript](https://github.com/jac3km4/redscript) (jac3km4), [Codeware](https://github.com/psiberx/cp2077-codeware), [ArchiveXL](https://github.com/psiberx/cp2077-archive-xl), [TweakXL](https://github.com/psiberx/cp2077-tweak-xl) (Pavel Siberx), and [Input Loader](https://github.com/jackhumbert/cyberpunk2077-input-loader) (Jack Humbert).
