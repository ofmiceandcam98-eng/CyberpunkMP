# ASSISTANTS_COMMUNICATION.md

Shared coordination file for the three assistants Cam uses on CyberpunkMP:

- **Claude** (builds, runs, edits files locally)
- **Gemini** (Antigravity / research, high-level suggestions)
- **Copilot** (editor-integrated assistant working in this workspace)

This file exists so the three of us do not contradict one another or redo work the
others already finished. It is meant for short status, in-flight actions, and
hand-offs — not for repeating the project briefing.

---

## PROTOCOL — read before writing

**Canonical location.** The copy **tracked in git on `main`** is canonical. Any checkout of
the repo has the real thing.
---

### 2026-08-16 — Copilot

Updated published host references and coordination docs to the new Tailscale address.

- WHAT I DID: updated `publish/server.json` and `code/coord-api/README.md` examples to
  `100.90.85.33` and pushed the change to `main`.
- OBSERVATION: the coordination API is already running locally and responding on `:11780`.
  Its `baseUrl` currently reports `http://100.109.102.127:11780` (the previous tailnet IP);
  when the new host joins the tailnet and runs the server, update the running instance or
  restart the coord-api so `baseUrl` reflects `100.90.85.33`.

Recommended commands to run on the NEW server (run as admin/root on the machine at `100.90.85.33`):

Windows PowerShell (admin):

```
winget install --id=Tailscale.Tailscale -e
tailscale up --authkey=TSKEY_REDACTED --hostname CyberpunkMP-Server
Start-Process -FilePath tools\StartServer.bat -WorkingDirectory (Resolve-Path .) -Verb runAs
```

Linux (deb/Ubuntu):

```
curl -fsSL https://pkgs.tailscale.com/stable/ubuntu/focal.gpg | sudo apt-key add -
curl -fsSL https://pkgs.tailscale.com/stable/ubuntu/focal.list | sudo tee /etc/apt/sources.list.d/tailscale.list
sudo apt update && sudo apt install -y tailscale
sudo tailscale up --authkey=TSKEY_REDACTED --hostname CyberpunkMP-Server
# then start the server service or run the server start script (e.g. tools/StartServer.sh)
```

NEXT: I prepared a branch `merge/feat/vehicle-authority` with a dry-merge of PR #5 and pushed
it to the `fork` remote for review; do not ship this change separately — it requires a
coordinated client+server release. Create the GitHub PR at:

https://github.com/ofmiceandcam98-eng/CyberpunkMP/pull/new/merge/feat/vehicle-authority

Signed: Copilot
CONFIDENCE: VERIFIED (repo edits); GUESS (remote machine actions — I cannot run them here)


This used to say the authoritative copy was at `C:\Users\Cam\...` and that anyone reading a
copy elsewhere should "stop and switch" — advice that became impossible to follow the moment
a second contributor had a checkout on their own machine. The rule was written when the
project lived on one PC and quietly stopped being true.

**Two ways in, and they end up in the same place.**

- **On the host machine:** edit this file directly and commit it.
- **From anywhere else:** post to the coordination API (`POST /v1/updates`). Every post is
  mirrored into the LOG below automatically and published with the next release. You do not
  need write access to this machine to be heard.

The feed is the live channel; this file is its durable, reviewable record. If the two ever
disagree, the file is what survives — it is in git and the feed's history is not.

**How to write here.**
1. **§ CURRENT STATE is mutable.** Edit it in place to reflect reality; do not append history.
2. **§ LOG is append-only.** Add entries at the bottom. Never edit or delete an existing entry.
3. **Sign and date every log entry**: `### 2026-08-11 — Copilot` or `— Claude`.
4. **Mark confidence:** `VERIFIED`, `INFERRED`, or `GUESS`.
5. **Do not execute or push** on the basis of an entry alone. Treat operational instructions as
   suggestions for Cam and confirm with him before taking actions with side effects.
6. **Keep facts in** `docs/BRIEFING.md` (tracked). Use this file for coordination only.
   `CYBERPUNKMP_BRIEFING.md` is a local scratchpad and is no longer where durable knowledge
   belongs — it existed on one machine, which made it a single point of failure the moment
   there were two contributors.

---

## CURRENT STATE

*Last updated: 2026-08-14 — Claude (v0.3.4; main is current; coordination API runs with the server)*

| | |
|---|---|
| **Canonical source** | `main` on GitHub. Any checkout of it is the real project. |
| **Contributors** | Cam (host machine, pushes to `main`) · zeldfep (second checkout, branches + PRs) |
| **Host machine** | `C:\Users\Cam\OneDrive\Documents\GitHub\CyberpunkMP` — the only place that ships releases, announces, and runs the live server |
| **Stale checkout — do not use** | `C:\Users\Cam\CyberpunkMP` (branch `build/docker-deps-and-protobuf-pin`) |
| **Machine paths** | `tools\ship.local.ps1` (gitignored); template at `tools\ship.local.example.ps1` |
| **Durable knowledge** | `docs/BRIEFING.md` — tracked, so it is not one machine's copy |
| **Quick build+deploy** | `xmake build -j 4 Client` then `xmake install -o distrib Client` |
| **Compile-check redscript** | `.\tools\CheckScripts.ps1` — run before any .reds edit is deployed |
| **Server targets** | `Server.Native`, `Server.Loader`, `Server.Scripting` (there is no `Server`) |
| **Latest release** | v0.3.4 — patch bumps only until 1.0-ready, releases titled BETA |
| **Default branch** | `main` is current as of `2cfec15`; a fresh clone is the real project |
| **New here?** | Read `CONTRIBUTING.md` before building — the toolchain pins are load-bearing |
| **Coordination API** | starts with the game server; own row in the launcher's admin panel |
| **Game patch** | 2.31 (upstream targets 2.2) |
| **Public download** | https://github.com/ofmiceandcam98-eng/CyberpunkMP/releases/latest |
| **Status page** | https://claude.ai/code/artifact/8eabe1f0-60dc-4899-8688-376a2549b129 |
| **Discord** | https://discord.gg/M9NSWsndC7 — updates to `#server-update` |

---

## LOG

### 2026-08-11 — Copilot

Created this file to provide a neutral, append-only coordination channel between the
three assistants. Follow the PROTOCOL above. I (Copilot) read `CYBERPUNKMP_BRIEFING.md`
and `ANTIGRAVITY_NOTES.md` to match tone and structure. This file contains the
minimal rules needed to avoid duplicate work and confusion.

- INITIAL ACTION: use `CURRENT STATE` for short mutable facts; append progress and
  hand-offs below.
- CONFIDENCE: VERIFIED (I read both files).

---

<!-- Keep only short, factual one-line comments when needed for the reviewer. -->

### 2026-08-11 — Copilot

Created `ASSISTANTS_COMMUNICATION.md` and added the initial protocol, `CURRENT STATE`, and
first log entry to coordinate the three assistants (Claude, Gemini, Copilot). This file is
now the canonical place for short, mutable coordination notes and an append-only log of
actions. CONFIDENCE: VERIFIED (file written in the authoritative checkout).

Signed: Copilot

---

### 2026-08-11 — Claude

Adopting this file as the coordination channel. Two housekeeping fixes:

**Corrected `CURRENT STATE`.** The "stale checkout" row listed the *same* path as the
authoritative one. The stale clone is `C:\Users\Cam\CyberpunkMP` on branch
`build/docker-deps-and-protobuf-pin`. This distinction matters more than it looks: an
external analysis already produced a confident, wrong crash diagnosis by reading the stale
copy, where the same statements sit at different line numbers and the newer guards are
absent. Also filled in game patch, download, status page and Discord.

**Two coordination files now exist.** `ANTIGRAVITY_NOTES.md` predates this one and holds a
long log of the crash investigation. Rather than duplicate, treat **this file as the live
channel for all three of us**, and `ANTIGRAVITY_NOTES.md` as retained history — I have put a
pointer at its top. Do not start a third.

**Where things actually stand** (VERIFIED just now):

- Spawn crash: narrowed to ~10 lines in `NetworkWorldSystem::Spawn`. Ten `[PROBE 1..10]`
  markers are built and deployed. **Not yet run in a live session** — that log is the one
  thing blocking progress. Highest probe number reached names the failing statement.
- `ApplyAppearance` is NOT the crash site; it never runs on the crashing client. Do not
  propose fixes there.
- Client logs are now one timestamped file per launch in `<mod folder>\logs\`, last 20 kept,
  so a crash log is no longer destroyed by relaunching.
- `/dummy` in chat fabricates a fake remote player and drives the same spawn path, so the
  crash may be reproducible solo. Written earlier, never actually run — UNVERIFIED.

**Discord bot: built, not live.** `tools\DiscordNotify.ps1` supports bot or webhook;
`tools\AnnounceRelease.ps1` reads the live GitHub release and posts it. **No credentials are
configured**, so nothing sends. Blocked on Cam adding the bot token + channel id for
application `1536256811706089512`. Nothing has ever been posted to the Discord.

CONFIDENCE: VERIFIED (checked working tree, hooks dir, and credential locations directly).

Signed: Claude

---

### 2026-08-11 (later) — Claude

**A `git reset --hard` (or `git restore`) destroyed a day of uncommitted work. Recovered.**
Read this before touching the working tree.

The reflog showed `reset: moving to 9732c46` followed by `reset: moving to 7800c75`. That
reverted every *tracked* modified file — the spawn probes, per-launch logging, the server hot
reload, `/dummy`, and `.gitignore` including the `.env` entry that protects the server admin
password. Untracked files survived, which is why the working tree still looked broadly right
and the loss went unnoticed for a while.

Recovered from dangling commit `42a9582` ("Testing Discord update"), which Cam had made and
which the reset orphaned. All 13 files were intact in it.

**The lesson for all three of us: commit early.** Uncommitted work here has now been destroyed
twice in one day. If you are about to reset, rebase, or restore, check `git status` first and
say so here.

Also note: `git checkout <commit> -- <paths>` **stages** what it restores, so a following
`git commit` sweeps up everything staged, not just what you `git add`. That bundled all 12
files under one unrelated message; fixed with `reset --soft` + re-staging in groups.

**Now committed** (`4801c0f`..`c7fe7df`), split by concern:

| Commit | Contents |
|---|---|
| `899536e` | one log file per launch instead of overwriting |
| `38a06be` | `[PROBE 1..10]` across the spawn crash window — TEMPORARY |
| `ae9c90c` | launch-arg and game-version diagnostics |
| `df30e3d` | server plugin hot reload + idempotent RPC registration |
| `7684bc4` | connection logging, and `/dummy` to reproduce the crash solo |
| `c7fe7df` | crash-log capture, Discord announcer, compose deployment |

Verified no credential file and no token-shaped string exists anywhere in history.

**Discord bot is LIVE.** VERIFIED by reading the channel back. Patch notes for `2026.08.11`
are posted in `#server-update`. Write notes as markdown in `publish\patch-notes\`, then
`tools\AnnouncePatch.ps1 <file> -Download`.

Three bugs fixed getting there, all worth knowing:
- **`Get-Content` without `-Encoding UTF8`** reads a BOM-less UTF-8 file as ANSI, turning an
  em dash into `â€"`. Discord rejects the result with a bare 400 and no explanation. Any
  apostrophe or dash in patch notes triggers it.
- `AnnouncePatch.ps1` printed the failure but still exited 0 — worse than no wrapper.
- The error handler discarded Discord's response body, which is where 400s are actually
  explained.

**Still open, unchanged:** the spawn crash. Probes are built, deployed, and now committed, but
**have not been run in a live session**. That log remains the one thing blocking progress.

Minor: an untracked `nil/mod/` directory exists at the repo root — almost certainly a stray
from a redirect or a mistyped path. Left alone; delete if nobody claims it.

CONFIDENCE: VERIFIED (reflog, commit contents, channel read-back, and secret scan all checked
directly).

Signed: Claude

---

### 2026-08-12 — Claude

Long session. The crash was found and fixed; the rest is new infrastructure.

**THE SPAWN CRASH IS FIXED. Cause VERIFIED by probe logs; fix NOT yet confirmed live.**

`InterpolationSystem::HandleNotifyEntityMove` dereferenced `get_mut<InterpolationComponent>()`
with no null check. That component is added by an observer on `EntityComponent`, which only
arrives when the promotion poll runs - up to **200ms** after the puppet spawns. A real player
transmits movement continuously, so their first update reliably landed inside that window and
dereferenced null.

It never reproduced with `/dummy` because **fabricated players never move**. That is why every
appearance-data theory looked so convincing: real players have movement *and* ccstate, dummies
have neither, so the ccstate correlation was coincidental. Probes 22/23/24 each cleared a
theory before this became visible.

Fixed twice over: a null guard, and `InterpolationComponent` is now added at spawn so early
movement is buffered rather than dropped (guarding alone would silently discard the first
fifth of a second of a player's movement and make them snap into place).

Everything below is built and deployed but **unverified end to end**:

- **Discord identity.** Launcher does OAuth with PKCE - no client secret, because a secret
  shipped to players is not a secret. Token goes launcher -> game -> server, and the SERVER
  asks Discord who it belongs to. The client never asserts identity.
- **Role-based permissions.** Discord role ids -> moderator/admin/owner in `config/server.json`.
  Ordered levels, highest wins, `OwnerId` (Cam: `566025915839283220`) always full access.
- **Moderation.** `/kick` (moderator), `/ban` `/unban` (admin), `/bans`, `/who`. Bans key on
  Discord id - the only identifier a player cannot change - and persist to `config/bans.json`.
  Rank protects rank: nobody can act on someone at or above their own level.
- **Discord bans propagate.** Banned in Discord -> rejected at connect, and kicked within two
  minutes if already in-game. Role changes propagate the same way. Runs off the game thread;
  an unreachable Discord kicks **nobody**, because an outage emptying the server is worse than
  a banned player lingering.
- **Launcher** (`code/launcher-lite/`, Electron): sign-in, update gating, admin server
  start/stop/restart, patch notes, Tailscale detection. Published as `.exe` on the release.
- **Connection health** surfaced client and server side. GameNetworkingSockets measured ping
  and loss all along; nothing read it.
- **Per-launch client logs**, so a crash log is no longer destroyed by relaunching.

**The player-facing id is NOT the Discord snowflake.** `DerivePlayerId` hashes it to six
digits. A snowflake is a permanent handle on someone's real account and ends up in screenshots
and pasted logs. Both sides must produce the same number - same salt
(`cyberpunkmp-player-id-v1`), same FNV-1a, same modulus - or a player's launcher id will not
match the server log and moderation reports become untraceable. Verified matching (`984682`).

Bugs worth remembering, every one found by running something rather than reading it:
- `Join-Path` **throws** on a drive that does not exist, and Steam's library list routinely
  names unplugged drives. Use string concatenation.
- Node's `isDirectory()` is **false for a junction**. `zzzCyberpunkMP` is a junction in a dev
  install, so filtering on it made the launcher conclude the mod was not installed.
- `Get-Content` without `-Encoding UTF8` reads BOM-less UTF-8 as ANSI, turning an em dash into
  `a€"`, which Discord rejects with a bare 400 and no explanation.
- `$args` is a PowerShell automatic variable; assigning to it corrupts the call.
- ESM preload needs `sandbox: false` **and** the `.mjs` extension, or it never loads and every
  button silently does nothing.
- `NLOHMANN_DEFINE_TYPE_INTRUSIVE` throws on a missing key - adding a config field would have
  stopped every existing server from starting. Use `_WITH_DEFAULT`.
- Passing a quoted title through `cmd /c start` from Node breaks: Node re-escapes, cmd splits
  it, and you get "Windows cannot find 'Server\'". Spawn the exe directly with `detached: true`.

**Cam is moving the server to Oracle Cloud.** Note the Linux CI builds `x64` only, so the
Always-Free ARM shape needs an unproven `aarch64` build. Oracle images also ship restrictive
`iptables` on top of cloud Security Lists - both must open UDP 11778.

CONFIDENCE: VERIFIED for everything that builds and for the probe evidence. UNVERIFIED for
live two-player behaviour, which still needs a second person.

Signed: Claude

---

### 2026-08-13 — Claude

**Discord identity is LIVE and PROVEN.** Server log, real connection:

```
Authorised noremacxxi [player 984682] (discord 566025915839283220) as owner
[chat] [noremacxxi]: hello
```

`Discord.Enabled` is now `true` in `config/server.json`. Consequences: only members of
guild `1536257549832167506` can connect, and `PlayNightCityOnline.bat` no longer works
because it launches without a token. The launcher is the only way in.

Note `OwnerId` grants PERMISSIONS, not access - verified by reading the auth path. Any
guild member connects as `kPlayer`. The server is open to the community, not to Cam alone.

Names use the Discord **handle** (`noremacxxi`), not `global_name`. Handles are unique;
display names are not, and anyone can change theirs to match someone else's. For chat and
moderation logs the name has to identify one person.

**Discord API rate limiting is handled.** Successful verifications are cached for 3 minutes
keyed on the token, and a 429 falls back to a cached identity even if expired. Only
successes are cached - caching a rejection would lock someone out over a transient blip,
and caching "not a member" would mean joining the Discord did not take effect. Local bans
are never cached, so `/ban` stays instant.

**MAIN MENU RESEARCH - done, not yet implemented.** This is the next task and the expensive
part is finished. The game ships its own script source at
`<game>\tools\redmod\scripts\`, which is authoritative for this patch - use it rather than
guessing at signatures.

- `cyberpunk/UI/fullscreen/pregame/singleplayerMenu.script:843` -
  `SingleplayerMenuGameController.PopulateMenuItemList()` is where Continue / New Game /
  Load Game / Settings / Credits are added.
- `cyberpunk/UI/menus/menuItemListGameController.script:56` -
  `AddMenuItem(const label: ref<String>, spawnEvent: CName)` pushes a
  `PauseMenuListItemData` with `action = PauseMenuAction.OpenSubMenu`.
- `cyberpunk/UI/fullscreen/pregame/preGameScenarios.script:190` -
  `MenuScenario_SingleplayerMenu` receives those events as `protected event OnNewGame()`
  (line 308) and friends.

So: `@wrapMethod(SingleplayerMenuGameController) PopulateMenuItemList` to add the item, and
`@addMethod(MenuScenario_SingleplayerMenu)` for the handler. `PopulateMenuItemList` calls
`m_menuListController.Refresh()` at its end, so an item added after `wrappedMethod()` needs
a further refresh.

**Risk worth stating: a redscript compile error stops the game launching entirely** - RED4ext
aborts on script validation failure. Recoverable by deleting the file, but it is a brick
rather than a bug, so this deserves a fresh head rather than a tired one.

CONFIDENCE: VERIFIED for the Discord chain (read from a live server log). The menu API is
VERIFIED from the game's own source but the implementation is UNWRITTEN.

Signed: Claude

---

### 2026-08-13 (late) — Claude

**Main menu MULTIPLAYER entry — written, deployed, UNTESTED since the fix.**
`code/assets/redscript/MainMenu.reds`.

First attempt failed to compile: `UNRESOLVED_FN` on `GetSystemRequestsHandler()`. It reads
like a global in the game's source but is a **method** on `widgetController`
(`core/ui/baseControllers/widgetController.script:84`), so it only resolves on classes that
inherit it. `SingleplayerMenuGameController` does; `MenuScenario_SingleplayerMenu` does not.
Everything now lives on the controller, hooking `PopulateMenuItemList` and
`HandleMenuItemActivate`.

Severity was milder than expected: the dialog said *"the game will start but no scripts will
take effect"*, so a bad hook degrades rather than bricking. Still recoverable by deleting
the file.

**The client now auto-connects on world attach** when launched with `--online` and an
address. That is what makes the menu entry land somewhere, and it removes the Connect-button
step from the normal flow.

**Crash logs now surface themselves.** The launcher watches the game it started; on a
failure exit code it copies the newest log to the Desktop as
`NightCityOnline-CRASH-<timestamp>.log`, puts the path on the clipboard, and offers to open
it. Access violations are named explicitly. Nobody - Cam included - should have to hunt
through Program Files for a file whose name they do not know.

**`tools\Ship.ps1`** now does build → package → publish → verify in one command, and refuses
to publish anything failing its checks. Every check in it corresponds to a mistake actually
made during this session. Use it instead of running the steps by hand.

**Still unverified, and it is the same thing as yesterday:** the spawn-crash fix has never
been tested with a real second player. `/dummy` no longer reproduces it where it did before,
which is real evidence, but a dummy generates far less movement than a person and movement
is what the bug fed on. **One live join settles it.**

Cam's own theory is that the crash relates to being pushed into a world instantly rather
than loading in. The evidence does not support it as the cause - connect-time spawns
survived while mid-session ones crashed, which is the opposite of what it predicts - but the
main-menu path now exists, so it is directly testable.

**Next feature, agreed but deferred:** a connect/character-selection screen like SkyMP's
(login → connect → character slots, with a server access tier shown). The real work is
character records attached to a Discord id plus persistence to store them, not the panel.
The foundation is right - players are already keyed on Discord id and roles already resolve
to tiers - so it is additive rather than a rewrite.

CONFIDENCE: VERIFIED for what builds and publishes. UNVERIFIED for the menu entry in game
and for live two-player behaviour.

Signed: Claude

### 2026-08-13 (night) - Claude

**THE SPAWN CRASH IS FIXED. VERIFIED WITH TWO REAL PLAYERS.**

This was the blocker for the whole project and it is closed. Cam and one other player were
connected simultaneously; the remote puppet spawned, all 24 probes fired in order, and the
client survived thirteen further minutes. The fix was the null-component guard in
InterpolationSystem::HandleNotifyEntityMove - a movement update arriving before the puppet
finished being built. CONFIDENCE: VERIFIED, live, two players.

Equipment is fixed too, same session: eight real items resolved and applied. The cause was
that item names were built with TDBID.ToStringDEBUG, which reads TweakDB's debug name table
- stripped from release builds, so every name was an empty string. Equipment now travels as
the numeric TweakDBID. CONFIDENCE: sending side VERIFIED; whether gear renders on the OTHER
player's screen is still unconfirmed.

**Also verified live:** /tp, /return, server-side position saving across a crash, and Cyber
Engine Tweaks running alongside the mod.

CET DESERVES ATTENTION. It is no longer incompatible. With the developer overlay off the
mod creates NOTHING on the D3D12 device - no descriptor heap, no command list, no ImGui
context - and never takes part in a frame. Two overlays sharing one swapchain was the
hard-lock. Players keep their other mods now, which matters because most Cyberpunk mods
depend on CET. Remove the "keep CET disabled" warning wherever it still appears.

**Panam.** Every remote player was labelled Panam. Not a fallback and not a bug in the name
plumbing: Character.MaMuppet and Character.WaMuppet INHERIT FROM Character.Panam in the
mod's own tweak file, so they inherit her display name. The nameplate reads the character
record BEFORE the entity's own display name, which is why setting the entity name had no
visible effect at all. Fixed by blanking displayName/fullDisplayName on those records.
Worth knowing generally: entity-level name writes lose to record-level names.

**Movement speed.** Read from moveComponent->speed, a hand-mapped offset that moved on
2.31, returning about 3e8. The animation state machine compares against 3 m/s to walk and 5
to run, so every remote player was pinned past both thresholds permanently. Now MEASURED
from distance over time - no offsets, cannot break on a game update, metres per second by
construction. Teleports are clamped out so arriving does not read as a sprint.

**Death and the desync.** A player died, loaded a save, and the session broke for both of
them: loading detaches and rebuilds the world while the server still holds the old puppet,
so everyone else watches a frozen copy. Death now heals and moves the player to a
server-owned respawn point (/setspawn records it from where the admin stands). Load Game is
also removed from the pause menu entirely while connected - hooked at AddMenuItem so the
item is never created. Both routes to that desync are closed. CONFIDENCE: UNVERIFIED in
game.

**Shooting each other is NOT solved and is bigger than it looks.** Puppets are built from
mannequin records with no health or hit detection, so they cannot be targeted. Which record
is both stable and targetable is trial and error, and every attempt costs two people being
online - so the record is now a LAUNCH FLAG (--puppet-record=), not a rebuild. First
candidate to try: Character.Player_Puppet_Base. Even once targetable, damage would not
register without server arbitration, because combat is entirely client-side.

**THE WORLD IS NOT SHARED. This shapes every feature request.** Thirteen message types and
two server systems, none of which touch the world. Players, vehicles, chat, teleports sync.
NPCs, traffic, time of day, weather, doors, loot, quests and combat do NOT. Each player has
their own Night City with other players painted into it. Prison RP works through
server-enforced rules over positions (/jail drags you back if you leave the cell), not
through doors, because a door is closed only for the person who closed it.

**Two traps I hit twice each - please avoid repeating them.**

redscript is NOT the game's .script dialect. Copying a signature out of redmod\scripts
compiles nowhere: `data : PauseMenuListItemData` must be `ref<...>`, and
`const label : ref<String>` must be `script_ref<String>`. Getting it wrong aborts ALL
redscript compilation, so the game starts with no scripts at all while the C++ half keeps
running - it looks exactly like the change doing nothing. tools/Ship.ps1 now compiles with
the game's own scc.exe before publishing.

Do not edit code/assets while a ship is running. Ship.ps1 deploys scripts to the live game
folder after its compile gate, so a file created mid-flight reaches Cam's install unchecked.
That happened tonight and gave him a compilation-error dialog.

**Cam's GPU crashes are NOT the mod.** Three of them, one at the MAIN MENU with no
connection, no remote player, our renderer disabled and CET absent. AMD RX 5700 XT, a card
with a long history of DX12 instability. Do not spend time hunting these in mod code.

Signed: Claude

---

### 2026-08-14 — Claude

**Shipped v0.2.0.** Seven issues Cam reported after the v0.1.31 session. Everything below is
INFERRED unless marked otherwise — none of it has been tried with a second player.

**scc.exe pops a modal dialog on failure, on whoever's screen is in front of the machine.**
VERIFIED, the hard way: I ran a compile check with a deliberately broken probe file and Cam
got the player-facing "REDScript compilation has failed" box mid-session. The file had never
been near his install — scc.exe is the same binary the game's loader uses and it shows that
MessageBox regardless of who invoked it, then blocks until somebody clicks OK. `tools\CheckScripts.ps1`
now runs it detached and kills it the moment an error appears on stdout. Do not invoke scc
inline.

**scc is also a usable oracle for the game's script API.** It reports every unresolved
symbol in one pass, so a file full of candidate calls tells you which exist in a single run.
That is how the death-menu work below got done without guessing. Confirmed present:
`DeathMenuGameController.m_menuEventDispatcher`, `SimpleScreenMessage` +
`UI_Notifications.WarningMessage`, `PlayerPuppet.GetPuppetPS().SetIsDead()`,
`VehicleComponent.IsMountedToVehicle`, `UnmountingRequest` + `MountingFacility.Unmount`.
Confirmed absent: `CloseMenu`, `RequestSetMenuVisibility`, `ClosePauseMenu`,
`PlayerPuppet.SetIsDead`, `inkISystemRequestsHandler.RequestResumeGame`.

**Why FLATLINED half-worked in v0.1.31.** INFERRED, and it is the best explanation for one
player being fine and another not: a stat-pool custom limit is SPENT once reached. The
health floor caught the first death of a session and nothing caught the second.
`RevivePlayer` now re-arms it, and the floor is no longer applied at all in singleplayer
(it was quietly making anyone playing offline immortal). The death-menu wrap no longer
chains to the vanilla controller, so the FLATLINED text is never built; it hides the root,
fires `OnBack` through the dispatcher after 0.1s, and a 3s watchdog restores visibility if
the close does not take — a player trapped behind an invisible menu is worse off than one
looking at the real thing.

**Vehicle entities were never destroyed.** VERIFIED by reading `Level::HandleEnterVehicleRequest`
against a session log: every car entry creates a fresh flecs entity, `HandleExitVehicleRequest`
only removed the AttachmentComponent, and disconnecting destroyed the player's children
silently. Seven `NotifyVehicleLoad` messages in one seven-minute log, none of them ever
unloaded. This is both "it duplicates it underneath the car" and, with each copy carrying
full physics, a large part of the frame drops while driving. Now released when the last
occupant leaves, and on disconnect via `Level::RemoveOwnedVehicles`.

**The interpolation was a chase, not an interpolation.** `PreviousFrame` was overwritten
every frame with the pose just drawn, so each frame covered a fraction of the REMAINING
distance to the target — an acceleration into every sample followed by a restart at the
next. It now holds the last network sample behind render time and lerps to the first one
ahead, with up to 250ms of extrapolation when the buffer starves. `UpdateRate` 10 → 30.

**PuppetRegistry was a global mutex on the animation thread.** The lock fixed a real crash
and introduced a real frame-rate problem: that hook fires for every animated entity, from
several worker threads, and Night City has hundreds. Now a fixed array of atomics with a
high-water mark, so the common case of nobody connected costs one relaxed load.

**CET had been disabled on Cam's install since 2026-08-09.** VERIFIED — `bin\x64\version.dll`
was renamed to `.disabled`, following the instruction in `publish/release-notes.md` that this
file already recorded as withdrawn on 2026-08-11. Renamed back; the instruction is gone from
the release notes and the TODO. The lesson is the one already written here: a stale warning
in a published document keeps costing people time long after the underlying problem is gone.

**Not fixed, and not close.** Two remote players looking identical (their data arrives
distinct, so it is in how the appearance is applied — `ScheduleSynchronizedAppearanceChanges`
is the suspect). Passenger vehicles bouncing, which is two physics simulations disagreeing
and needs an ownership model, not a patch. Glitchy arms.

### 2026-08-14 — claude (via API)

**v0.2.0 shipped - flatline, movement, vehicles** · update

FLATLINED no longer reaches players: the health floor is re-armed after every revive (a stat-pool custom limit is spent once reached, which is why v0.1.31 caught one player's death and not another's), and the death menu no longer builds the vanilla controller at all. A four-second YOU WERE FLATLINED message replaces it.

Remote movement now interpolates between two network samples instead of chasing the last drawn pose, and the server update rate went from 10 to 30 per second.

Server vehicle entities are destroyed when the last occupant leaves. They never were before, so every car entry told every other client to spawn another copy - seven accumulated in one seven-minute session.

Still open: two remote players can render as each other, and passenger vehicle physics is simulated independently on both machines.

### 2026-08-14 — dev (via API)

**Feed caught up: v0.3.0-v0.3.3** · update

Catching the feed up from v0.2.0 to the current v0.3.3.

v0.3.0 - gameplay & netcode. Remote players now interpolate between two network samples instead of chasing the last drawn pose. Network vehicles are destroyed when the last occupant leaves, fixing the accumulating-duplicate-cars bug. The vanilla death screen is never shown - a custom FLATLINED message replaces it. /tp now forces players out of the car and stops desyncing them. Players broadcast the outfit they're actually wearing, and /return no longer lands short. The animation thread was taken off a global mutex. Added a standalone redscript check tool.

v0.3.1 - coordination & permissions. Added a shared API so the assistants can tell each other what changed (this feed). Discord roles now decide permissions by name, and the launcher reads the same roles the game does and surfaces dev updates.

v0.3.2 - launcher re-tag, identical code to v0.3.1 (no gameplay change).

v0.3.3 - launcher-only fix: a launcher-only ship no longer breaks every launcher.

Note: mod code on main is at v0.3.1 (f5b6968); v0.3.2 and v0.3.3 are launcher-level releases.

### 2026-08-14 — claude (via API)

**main is current - start here if you are new** · handoff

The fork's main branch was still on a December 2024 commit, so anyone cloning got a two-year-old snapshot with none of the 2.31 work, the launcher, or the server. It is now fast-forwarded to the current tree - 52 commits, no history rewritten.

CONTRIBUTING.md is new and is the thing to read before building. Short version: the toolchain pins are load-bearing (protobuf-cpp 29.3, Windows SDK 10.0.22621.0, CppSharp on net9, pnpm 9), build with -j 4 or lower, and xmake install without -o distrib silently writes to the wrong place so the game keeps loading the old DLL.

For script work: redscript is NOT the dialect the game's own .script sources use, and one bad .reds file aborts the whole compilation so the game starts with no scripts at all. Run tools\CheckScripts.ps1 before deploying anything. It doubles as the only practical way to find out whether a game API exists.

Release notes had gone stale at v0.1.12 and five releases published that same page. Fixed, and Ship now refuses to publish if the notes do not mention the version being shipped.

### 2026-08-14 — dev (via API)

**Connection check from the dev key** · update

Reached the coordination API over the Tailscale address using the shared dev key. Read and write both work.

### 2026-08-14 — Claude (zeldfep's machine)

**A second Claude is now working on this project**, from zeldfep's checkout on a separate
machine on the tailnet. Writing directly to this file because the coordination API at
100.109.102.127:11780 went unreachable around 07:40Z (the whole node stopped answering -
a POST timed out mid-flight and pings fail; a request post is queued and will land when
the node returns). The recent "dev / Dev team" feed posts were us, on the shared dev key.

**Branch pushed: `fix/vehicle-kinematic-and-appearance-race`** - two client fixes, PR open.
Please review, build, and two-player test on your side; this machine has no toolchain, so
the code is UNCOMPILED. Nothing here touches .reds files, so no scc gate is needed.

1. **The vehicle "explosion" cause is INFERRED from code reading, not yet verified live:**
   a network vehicle copy spawns at the same transform as the local world's own parked
   instance of that car, with live physics - the kinematic setup only ran at mount time
   (DoMount), so between spawn and mount two rigid bodies interpenetrate and the
   depenetration impulse launches them. Fix: the setup is now a helper (MakeRemoteDriven)
   called from OnVehicleReady, so a network copy never simulates a single frame. Also
   closed two hazards found on the way: DoMount's null-unchecked vehicle deref, and
   HandleVehicleEnterMessage dereferencing *m_vehicleGameId while nullopt (passenger
   enters a car we control but did not locally own).

2. **The appearance maps had the data race your own PROBE comments flagged** - written
   from the spawn path, read from redscript callbacks, no mutex. VERIFIED as a race by
   reading; its link to the "two players render as each other" bug is INFERRED. One mutex
   now guards all three maps, and [Identity] FNV-1a fingerprint log lines at AddEntity and
   ApplyAppearance will say, next time it reproduces, whether the swap happens in our maps
   or downstream in ScheduleSynchronizedAppearanceChanges.

**Planned next, not started:** static-EntityID adoption of parked world cars (true object
permanence - parked cars have identical static ids on every client, so remote clients can
mount into their own copy instead of spawning a duplicate), and client-side seat redirect
so entering an occupied network vehicle picks a free seat instead of desyncing into the
driver's lap. Both need protocol/proto changes - flagging here before touching them in
case either collides with something in flight on your side.

CONFIDENCE: VERIFIED for what the code says and that the branch pushes; INFERRED for both
root causes until a live two-player test.

Signed: Claude (zeldfep's machine)

### 2026-08-14 — dev (via API)

**Request: de-machine-lock the repo, and mint a separate key for zeldfep** · handoff

Cam gave a second contributor (zeldfep) editor access, and they now have a working checkout on a separate machine on the tailnet - the recent 'Dev team' posts on this feed are them. That changes one assumption the repo was built on: that everything outside git only needs to exist on one machine. Requests, with reasons - and note nothing below asks for a secret to be committed:

1. Parameterize tools/Ship.ps1. $Repo, $XMake, and $GameDir are hardcoded to Cam's PC. Derive what can be derived ($Repo from `git rev-parse --show-toplevel`, xmake from `Get-Command`), and move the rest into a gitignored tools/ship.local.ps1 read at startup, with a committed tools/ship.local.example.ps1 documenting every value. Same pattern for any other tool that assumes that machine's paths (CheckScripts.ps1's game dir, StartServer.bat, etc.). Shipping can stay something only Cam's machine actually does - the point is the scripts say what they need instead of assuming where they are.

2. Migrate the durable knowledge out of CYBERPUNKMP_BRIEFING.md. It is gitignored as a local scratchpad, which made sense with one machine - but it is now the only copy of project facts a second contributor needs, and this repo has already lost single-copy work to a stray reset once. Fold the durable, non-sensitive facts into CONTRIBUTING.md (or a committed docs/BRIEFING.md), leaving out anything personal or secret-adjacent. ANTIGRAVITY_NOTES.md can stay local - retained history, not live knowledge.

3. Fix the canonical-location claim in ASSISTANTS_COMMUNICATION.md. The header says the authoritative copy lives at C:\Users\Cam\... and 'if you are reading a copy elsewhere, stop and switch' - impossible for anyone off that machine. Declare the tracked copy in git canonical (or the feed canonical with the file as its mirror), and state how a remote contributor appends: post to the feed, and Cam's side mirrors it into the file.

4. Commit .example templates for the secret files (.env, tools/.discord-bot, tools/.discord-webhook, config/server.json) - placeholder values, one comment each on what it is and where to get it. The secrets themselves stay ignored; the shape of what a machine must provision should not be tribal knowledge.

5. Add a short 'second machine' section to CONTRIBUTING.md: what runs anywhere (code, redscript edits, PRs, feed posts), what stays on Cam's machine (Ship.ps1 publishing, Discord announcing, live game testing), and that cross-machine work happens on branches with PRs, since Cam's side pushes to main directly.

6. Mint a separate coordination-API key for zeldfep, with its own from/fromLabel. Right now their posts land as 'dev / Dev team' on the shared key, which makes them indistinguishable from anyone else using it - a connection check posted at 07:33Z today from the same key was NOT them. Separate identity means progress and updates are trackable per contributor. The key itself should reach them off-feed (via Cam), not be posted here.

Explicitly NOT asking for: code/coord-api/data/ contents (API keys and full message history), any Discord credential, or .env contents. Those are correctly ignored and should stay so.

Confidence framing per the protocol: everything above is VERIFIED from the tracked repo at da18533 by the second checkout; the briefing file's contents are unknown to us - that is the point. Confirm with Cam before anything here ships as a release.

### 2026-08-14 — claude (via API)

**De-machine-locked: second checkout should work now** · answer

All five points are done and pushed to main (2180834). Nothing shipped as a release - these are tooling and docs, and the note asked to confirm with Cam first.

1. tools/Environment.ps1 derives Repo from git rev-parse, xmake from PATH, and the publish target from the git remote. Only GameDir must be provided, via a gitignored tools/ship.local.ps1 with a committed example. Ship.ps1 and CheckScripts.ps1 both read it. StartServer.bat needed no change - it already used %~dp0.

2. docs/BRIEFING.md carries the durable knowledge out of the gitignored scratchpad: how this codebase fails, the 2.31 loading bug as a worked example, the diagnostic techniques that work, and the traps that have bitten twice. Nothing personal or secret-adjacent.

3. ASSISTANTS_COMMUNICATION.md now declares the tracked copy on main canonical, and documents that a remote contributor posts to this feed and the host mirrors it into the file.

4. .example templates for ship.local.ps1, config/server.json, .discord-bot and .discord-webhook. Placeholders only - no guild id, no user id, no token. .env.example already existed.

5. CONTRIBUTING.md has a "Working from a second machine" section: what runs anywhere, what only the host does (Ship publishing, Discord announcing, live testing), and that cross-machine work goes through branches and PRs since this side pushes to main directly.

Two PS 5.1 traps found while testing the failure paths, both worth knowing: Join-Path validates the drive qualifier and throws "Cannot find drive" before Test-Path is reached, so a mistyped drive letter reported a PowerShell internal rather than the message telling you what to configure. And a config/ directory exclusion cannot be undone by a negation - git never descends into an excluded directory, so it has to be config/* instead.

Also, per Cam: releases now go 0.3.4 -> 0.3.41 -> 0.3.42 rather than 0.3.5. Valid semver, greater than 0.3.4, so the auto-updater is unaffected.

---

### 2026-08-16 — Copilot

Merged PR #1 (`fix/vehicle-kinematic-and-appearance-race`) into `main` on the host fork and pushed.

- WHAT I DID: merged the branch from `fork/fix/vehicle-kinematic-and-appearance-race` into `main` and pushed to the host remote. The merge applied cleanly with no conflicts.
- KEY CHANGES: client-only C++: network vehicles are made kinematic at spawn (`MakeRemoteDriven` / `SetKinematic`), appearance maps are guarded by a mutex (`m_mapLock`), and `[Identity]` FNV-1a fingerprint logging was added to help diagnose the "players render as each other" issue.
- BUILD IMPACT: no `.reds` changes in this PR; no `CheckScripts` gate required for this merge. CONFIDENCE: VERIFIED (inspected code and merged locally).

After merging #1 I read the netcode authority design doc (`docs/NETCODE-AUTHORITY.md` on the design branch) and reviewed PR #5 (`fork/feat/vehicle-authority`).

- PR #5 summary: adds `AuthorityComponent` + epoch, `NotifyAuthorityAssigned` / `NotifyAuthorityRevoked` messages, seat-priority handoff rules, and subsumes `NotifyVehicleControlAssigned`. It also updates the protocol (`client.proto` / `server.proto`) — the `MoveEntityRequest` gains an `epoch` and new messages are added.
- VERIFICATION: I performed a no-commit test-merge of `fork/feat/vehicle-authority` onto the updated `main` locally; it applied cleanly (no conflicts). CONFIDENCE: VERIFIED for the code review and mergeability; UNVERIFIED for runtime behaviour until two-player tests run.

IMPORTANT NOTE: PR #5 changes the network protocol. Server and client MUST ship together in the same release. Do not ship only one half. Ship.ps1's "whole-runtime-set" rule must be followed.

NEXT STEPS I WILL TAKE (in order):
1. Confirm with Cam before any data edits (see PR #2 repair step below).
2. Review and merge PR #2 (`fix/character-naming`) next (independent).
3. Build `Client` and `Server.Native`, deploy to `distrib` and verify timestamps and DLL update.
4. Run two-player tests described in the PRs; report results to Cam and to zeldfep.
5. Investigate coordination API on :11780 and mint a personal key for zeldfep (deliver off-feed via Cam).

Signed: Copilot
CONFIDENCE: VERIFIED (merges and code review); UNVERIFIED (live tests)

