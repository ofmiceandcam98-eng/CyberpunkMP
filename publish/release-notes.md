Unofficial build of [CyberpunkMP](https://github.com/tiltedphoques/CyberpunkMP) for **Cyberpunk 2077 patch 2.31**, plus the fixes needed to make it run on that patch. Upstream targets 2.2 and will not start on current game versions.

**Discord — Night City Online:** https://discord.gg/M9NSWsndC7

**Helping out?** Start with [CONTRIBUTING.md](https://github.com/ofmiceandcam98-eng/CyberpunkMP/blob/main/CONTRIBUTING.md) — the build toolchain has load-bearing version pins and a clean checkout of upstream does not compile.

## What changed — v0.3.70

- **Crash logs deliver themselves.** After every session — normal quit or crash — the launcher sends your mod log to the dev server automatically. No more finding files, no more dragging things into Discord (a copy still lands on your Desktop after a crash, in case someone asks for the file directly). The server keeps only each player's ten newest logs, so nothing piles up and nothing stale gets debugged.

## What changed — v0.3.69

- **Players who were online before you now actually appear.** Spawns that arrived while your game was still loading used to be silently thrown away — whoever joined first simply didn't exist for you. They're now held and placed the moment your world is ready.
- **No more frozen duplicate players.** A rejoin now replaces a player's old puppet instead of standing a second copy next to it.

## What changed — v0.3.68

- **Buttons that share a row share a size.** The Server panel's controls (and every other button row) are now uniform width — color still says what a button does, shape no longer says anything by accident.

## What changed — v0.3.67

- **"Test server connection" in Tools.** Checks every link between your PC and the server — internet, Tailscale, the server's network, the server itself — and names the first broken one with how to fix it. "Server offline" used to mean five different problems; now each one says its own name.
- **The network invite moved to the top of Tools**, where new players can find it — it was buried in the dev-only section. Accepting the invite AND switching Tailscale to the joined network are both needed; the connection test now catches the second half being missed.
- Fresh network invite published (the old one was used up).

## What changed — v0.3.66

- **Admins control the real server from the launcher.** The Server panel's Start, Restart and Stop now command the actual server everyone plays on — not a program on your own PC. Stop keeps it stopped (through reboots and redeploys) until an admin presses Start; Restart takes about 20 seconds. The server checks the admin login on every action; the launcher asks for it once and remembers it encrypted. Both destructive buttons confirm first, and the panel shows "stopped by an admin" as its own state instead of a generic offline.

## What changed — v0.3.65

- **Uninstalling actually uninstalls.** Removing the launcher now also clears everything it saved on your PC — signed-in Discord session, settings, keys. Updates never touch your data (only a real uninstall does), and your game folder and the mod stay untouched as before — "Remove mod" remains the explicit way to take that out first.

## What changed — v0.3.64

- **You can see each other move.** Every session on the new server had players frozen as statues: the mod was a mixed build — its network serializer came from one branch and its headers from another, off by exactly one bit, so the server read every entity ID you sent as double its real value and refused it. The build system flaw that let two branches fuse into one DLL is fixed, the mod is rebuilt from scratch, and real-time sync works. This was also the cause of seeing a copy of yourself, and of movement never saving.

## What changed — v0.3.63

- **Test builds get an Uninstall button.** The installed test build's row now offers Uninstall directly — it puts the shipped mod back (same as Restore) so builds can be checked one at a time without hunting for the way out. Dev panel only.

## What changed — v0.3.62

- **Body type moved fully in game.** The launcher's Body type toggle is gone — NEW CHARACTER runs the game's own creator, which asks, and since v0.3.61 that answer actually sticks. One place for the truth instead of two that could disagree.
- **The dev Server tool tells the truth about the servers.** Its hint still described the old world (the published server as one person's PC, the old test address). It now points at the vehicle-authority test server and describes the self-deploying main server as what it is.

## What changed — v0.3.61

- **NEW CHARACTER really means new.** Replacing your character now retires the old one properly, so the new person gets their own name prompt and starts at the arrivals point. Before this, the replacement quietly inherited the old character's name — and the once-per-character name lock along with it, so you could never name the person you'd just made. Old characters are retired, not deleted.
- Movement desync on the server now logs exactly what it rejected, so the remaining sync bug can be caught in the act.

## What changed — v0.3.60

- **The Server panel now shows the real server.** For admins, the panel used to control a server program on your own PC — a leftover from when the server was somebody's desktop. It now reports the actual game server everyone plays on (online state, player count, where it runs) and opens its web admin. The old local Start/Stop controls only appear on a machine that actually has a locally built server.

## What changed — v0.3.59

- **Multiplayer works again — sorry.** v0.3.58 shipped the wrong mod build: its release had been pre-created with test artifacts, and the launcher-only ship reused them instead of the real mod. In game that looked like multiplayer simply being gone — no chat, no other players, no map pins — and the server refusing every connection with a protocol mismatch. This build carries a mod compiled from exactly the code the server runs. The ship script now refuses to source mod files from anything but the latest full release, so this class of mistake can't repeat.

## What changed — v0.3.58

- **The server moved.** It now runs from a self-updating deployment on dedicated hardware instead of one person's PC, and it updates itself from GitHub within ten minutes of a push. Nothing to do on your side — the launcher reads the address from the release, so pressing Play lands you on the new server automatically. Character data starts fresh on the new machine.
- **`/name` is once per character.** Pick a name and it's yours until you retire that character with NEW CHARACTER — dying doesn't reset it, and renames are refused. The name prompt no longer reappears after a ripperdoc visit either: saving your look was wiping the fields that remembered you'd already named yourself (and where you'd already spawned).
- **Dev launcher: pick your server.** A Server tool in the Dev panel points Launch, the status pill and everything else at any server — the test box, localhost, or back to the published one with one click. Dev role required.
- **Dev launcher: one-click test builds.** Pre-releases from GitHub now list in the Dev panel with an Install button. The download is checksum-verified, your shipped mod DLL is backed up automatically, and Restore puts it back. Players never see any of this — their launchers only update from full releases like this one.

- **Every character now has a permanent ID of its own.** It's generated once, never reused, and stays the same through renames, ripperdoc visits and reconnects — while still filed under the Discord account that owns it. It's the identifier that will carry inventory, cyberware and RP profiles later.
- **Admin commands find people by any name they have.** `/tp`, `/kick`, `/ban` and the rest now accept a character name, a Discord name, a Discord ID, or a character ID. Character names win over account names, so the person you're standing in front of is the one you get.
- **`/whois`** — shows a player's character name, account, and character ID. Moderator and above.

## What changed — v0.3.56

- **NEW CHARACTER actually replaces your character now.** It never could. The server only saved an appearance for a player who had *no* character, so anyone who already had one went through the whole creator, connected, and was spawned as the character they had just replaced — the creation was thrown away silently. That is why hyliangenesis built a male V and stayed female. The client now tells the server that what is arriving is a replacement.
- Your character is still keyed to your Discord account, as it always has been — it follows you to any machine you sign in from.

## What changed — v0.3.55

- Diagnostic build for the "everyone looks like me" bug. Your clothes, name and position all sync correctly — only the face and body don't, and each player sees the other wearing a whole copy of their own character. This build records the one measurement that separates the two possible causes. Play for a minute with someone else and send the client log.

## What changed — v0.3.54

- **A leftover copy of the mod is moved aside automatically.** The game loads every mod folder it finds, so an old copy kept overriding the current one. The launcher now handles it on the way to launch — nothing to find, nothing to delete. The old copy is *moved*, not deleted, into `red4ext\disabled-by-launcher\`, so it can be dragged back if you wanted it. If you have deliberately pointed the launcher at your own build in Settings, that is the one it keeps.
- **You can't start the game twice.** Launch Game greys out and reads **Launching…**, then **Game running**, and only comes back once Cyberpunk has actually closed. It checks the real process list, so a game you started from Steam — or one still running from before you opened the launcher — counts too.

## What changed — v0.3.53

- **Verify files now catches a stale second copy of the mod.** The game loads *every* mod folder it finds, but the launcher only ever looked at the first — so a leftover copy kept running, its scripts overrode the current ones, and the launcher truthfully reported the copy it knew about as up to date. That is how you get an old main menu under a launcher saying v0.3.51. Verify now names every copy, and which release each came from.
- Each install stamps its version into its own folder, so the copies can be told apart.

## What changed — v0.3.52

- **The dev launcher appears for devs.** Access granted by a Discord role resolved *after* sign-in had already drawn the page, so the admin controls never showed up for anyone who was not on the hardcoded list. The page is now told when the role check finishes. Thanks to zeldfep for finding and fixing it.
- **New characters arrive where `/setstart` says.** Server-side fix, already live — it was deciding "brand-new" from the account rather than the character, so it never fired for anyone who had connected before.

## What changed — v0.3.51

- **Other players wear their clothes.** Equipping was nested inside the check for whether the puppet already *had* the item — so anything it was already carrying got skipped and left in the inventory instead of being put on. Remote players are built on an NPC record that ships with its own loadout, so that overlap was the normal case, not an edge case. Having an item and wearing it are now asked separately.

## What changed — v0.3.50

- **You are your own character again.** This was the big one. The launcher installs a starter save so new players skip Act 1 — and it was forcing that save to load on **every** launch, for everyone. Since it is one save built from one person's character, pressing MULTIPLAYER handed you *their* character, and it overwrote whatever you made through NEW CHARACTER. It is now only used until you have a character of your own, then your saves are left alone.
- **Chat appears straight away.** It was showing up late — usually once you got in a car — because the HUD finishes assembling after chat sets itself up and puts it back. Visibility is now re-asserted on every message, so the first line that arrives brings it back.

**Known and not fixed yet:** a crash when opening the weapon wheel as a passenger in a moving vehicle, and clothing that only partly applies to other players.

## What changed — v0.3.49

Most of this was already written and simply never reached your game. The build was only deploying the mod's code, not its data files — so two fixes sat finished in the repo while everyone kept hitting the bugs they fixed.

- **Players are no longer Panam.** Remote players are built on Panam's character record, and the fix that strips her identity off them was written days ago and never shipped. Nameplates and the scanner now show the player, not her name, her gang and her criminal record.
- **The chat box shows up.** Chat was never broken — messages arrived, typing worked, commands were sent — the box just wasn't being drawn. It's now made visible directly instead of relying on an animation that wasn't running.
- **Mouse wheel scrolls chat.** The bindings existed and had never been deployed either.
- **REGULAR START is gone from character creation.** The previous attempt filtered the wrong screen. It now removes the button on the actual one, so you can't accidentally start in the prologue.
- **The name box actually opens.** It could only ever trigger for a character with no name at all — which no character has, since names fall back to your Discord name. Everyone gets asked once now, on spawn if you already have a character.
- Builds now verify that tweaks and inputs reached the game, the same way scripts already were. That check is what would have caught all of this.

## What changed — v0.3.48

- **Your character gets a name, and the game asks for it.** Finish the creator and a box appears labelled **CHARACTER NAME**. Type one, press Enter, done — no command to know about. Escape skips it and `/name` still works whenever you change your mind.
- **That name is who you are to everyone else.** It shows over your head when someone scans you, and it is now what appears in chat instead of your Discord name. Your account name no longer leaks into the one place people read constantly.
- Everybody's name is their own — it lives on the character, not the account, so making a new character means picking a new name.

**This update changes how the client and server talk, so everyone needs it.** The launcher updates itself; if you were already in game, restart it.

## What changed — v0.3.47

- **No standard start when making a character.** A regular new game begins in the prologue — the whole of Act 1, which is exactly what the multiplayer start exists to skip. Only the Phantom Liberty start is offered, which lands you past Act 1 at level 15.
- Menu items are logged, so the remaining ones we want to hide can be named exactly rather than guessed at.

## What changed — v0.3.46

- **The character you make is kept.** The creator runs before you connect, so nothing was watching while you built a face — the server now asks for your appearance the moment you arrive and keeps that as your character. Previously it was only saved if you happened to visit a ripperdoc afterwards.
- **The menu says what it does.** Making a new character replaces the one the server holds, so the entry now reads **NEW CHARACTER (REPLACES YOURS)**.
- Half-built appearances are rejected. A 23-byte appearance was captured and used during testing — the customization state exists briefly before it is filled in, and the watcher caught it in that window. Both ends now require a plausible one.

## What changed — v0.3.45

- **MULTIPLAYER — NEW CHARACTER.** Runs the game's own New Game flow, so you get the real character creator — **including male or female**, which is the one thing no ripperdoc can change. Pick Phantom Liberty's start on the way through and you land past Act 1 at level 15, then connect straight to the server. **MULTIPLAYER** still drops you back in as you were.
- Your appearance still saves itself, and the server still remembers you on any machine.

## What changed — v0.3.44

- **Male or female.** Pick your body type in the launcher, under Settings → Character. It has to be chosen there rather than in game: ripperdocs change everything about how you look *except* body gender, and it is built into the world you load.
- Everything else about your appearance is still changed at any ripperdoc, and still saves itself.

**Phantom Liberty is required.** Both starting worlds are Phantom Liberty saves, so the expansion is needed to play on the server — not just to visit Dogtown.

## What changed — v0.3.43

- **Ripperdocs, not the apartment mirror.** They change appearance too — everything except body gender — and they are everywhere, so "go to a ripperdoc" is a far easier instruction than "go home". The prompts say so now.
- Diagnostics for the character creator. The mod writes what the game's customization system can actually do to the log on startup, so opening the creator automatically stops being guesswork.

**Note on everyone looking the same:** the shipped world template is one character, so until you change your appearance you and everyone else are the same V. Visit a ripperdoc once and the server remembers you from then on, on any machine.

## What changed — v0.3.42

- **Multiplayer no longer touches your own saves.** The mod ships a world template — Act 1 finished, level 15, standing free in Night City — and the launcher installs it and loads that instead. Your singleplayer saves are left alone entirely, and you no longer need a post-Act-1 save of your own to play.
- **Scanning someone shows their character's name**, not their Discord account name. Set yours with `/name`.
- **New players are told they need a character** when they join, instead of silently playing as whoever the template contains.

## What changed — v0.3.41

**Everyone must update.** The protocol changed, so older clients are refused at the handshake with "wrong client protocol identifier". The launcher will offer the update.

- **Your character now lives on the server, not in a save file.** Look in a mirror, change your face, close it — that's it. No command, no save picking. Rejoin from any save and you come back as yourself. Set what you're called with `/name`.
- **One MULTIPLAYER entry** on the main menu instead of two. Picking a save used to decide which character you played; it doesn't any more, so offering the choice implied a decision that no longer matters.
- **`/setstart`** — stand where new players should arrive and run it. Separate from `/setspawn`, which is where people wake up after being downed.
- **The pause menu no longer offers Save while connected.** A Cyberpunk save records a singleplayer game and knows nothing about the server, so offering it was telling you something untrue about what preserves your session. The server writes your position continuously.
- **Far-away players cost far less bandwidth.** Movement updates now degrade with distance instead of every player being sent every update, and nobody in a vehicle has their character position relayed at all — the client was already throwing those away on arrival.
- **Malformed packets are rejected.** A non-finite position used to silently switch off every system that measures distance: local chat stopped working for that player, jail stopped enforcing them, and the bad value was written to the persistent store where it survived a restart.

## What changed — v0.3.4

- **The coordination API starts with the game server.** It was started by hand, so it died on every reboot — and when it is down the far end gets a bare connection-refused with no way to tell whether the service is off, the machine is off, or their key is wrong. It had already died once unnoticed. It now has its own row in the admin panel showing how many keys are in use, and its own Start/Stop, because rebuilding the game server should not silence the channel.

## What changed — v0.3.3

- **The death screen is blocked at the engine level now, not by scripts.** Two previous attempts hooked `OnDeath` and put a floor under the health pool. Both are conventions the game's own scripts follow, and anything that sets the dead state directly — a scripted kill, falling out of the world, drowning — walked straight past them. The player is now flagged `Immortal`, which the engine checks *before* entering the dead state. Damage still lands and health still drops, so you still get downed and respawned; the death menu is simply never asked for.
- The backstop also lost a watchdog that put the menu **back** after three seconds if it had not closed — which turned a failed close into FLATLINED appearing slightly late, indistinguishable from the bug it was meant to prevent.
- **`/tp to <player>`** sends you to them, the counterpart to `/tp <player>` bringing them to you. `/return` works in both directions.
- **One person per seat.** Each client picks its own seat locally, so two people entering the same car from opposite doors could both claim `seat_front_left` and be replicated into each other. That is what "four people cannot fit in a four-door car" looked like from the inside.

## What changed — v0.3.2

- **A launcher-only release no longer breaks every launcher.** v0.3.1 published the installer and nothing else, and since `latest` had moved to it, every launcher 404'd on `server.json`, fell back to `127.0.0.1` and reported the server offline. Releases are now checked for completeness before being promoted.
- Anyone with the **dev** Discord role gets the dev key and the Tailscale invite in Settings.

## What changed — v0.3.0

- **Discord roles decide permissions, by name.** The role mapping existed but was keyed on role snowflakes, so nobody ever configured it and the `dev` role granted nothing. Roles named `dev`, `admin`, `moderator` and so on now resolve with no configuration at all.
- **The launcher reads the same roles the game does**, so the controls it shows match the commands you actually have.
- **`/tp` takes people out of the car properly.** It also stops them desynchronising: while a client believes it is driving it sends the *vehicle's* position as the player's, so teleporting out from under that left everyone else seeing them where the car was.

## What changed — v0.2.0

- **Remote players move smoothly.** The interpolation was chasing the target from the last drawn pose rather than interpolating between two network samples, so it accelerated into every update and snapped at the next. Update rate also went from 10 to 30 per second.
- **Cars stop duplicating.** Entering a car created a new server entity every single time and nothing ever destroyed it, so every entry told every other client to spawn another copy. One seven-minute session left seven of them stacked in the road — with full physics each, which is a large part of why frames collapsed while driving.
- **Cyber Engine Tweaks works.** The old instruction to disable it is withdrawn; the console is available again.
- A lock on the animation thread that every NPC in the city was queueing on is gone.
- Player cap raised from 4 to 16.

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

The **crash when a second player spawned** is fixed and confirmed with real players — a null component was being written to during the spawn handshake.

Still rough: vehicles are only loosely synchronised, so riding as a passenger in someone else's car is buggy, and other players' clothing does not always match what they are actually wearing.

## What's in the zip

- `mod/` — the mod itself: drop into `red4ext/plugins/` and rename to `zzzCyberpunkMP`
- `prerequisites/` — all six required mods, unmodified, so you can install in one pass
- `LICENSES/` — MIT license text for each bundled prerequisite
- `INSTALL.txt` — full instructions, including firewall and connection help

## Quick start

1. Back up `bin\x64`, `engine`, `r6` and `red4ext` from your game folder.
2. Extract all six prerequisite zips into your Cyberpunk 2077 folder.
3. Copy `mod` into `red4ext\plugins\` and rename it `zzzCyberpunkMP`.
4. Add launch options:

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
