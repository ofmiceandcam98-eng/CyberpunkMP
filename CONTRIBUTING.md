# Working on Night City Online

This is a fork of [CyberpunkMP](https://github.com/tiltedphoques/CyberpunkMP) carrying the
changes needed to run on **Cyberpunk 2077 patch 2.31**. Upstream targets 2.2 and will not
start on current game versions.

Read this before building. The toolchain has version pins that are load-bearing, and a
clean checkout of upstream does not compile — you will lose an evening finding that out on
your own.

---

## First, tell the scripts about your machine

```
copy tools\ship.local.example.ps1 tools\ship.local.ps1
```

Then set `$GameDir` in it. That file is gitignored; the example documents every value.

Everything else is derived — the repo root from git, xmake from PATH, the publish target
from your git remote. The game install is the one thing that cannot be guessed reliably,
and a script that silently finds the *wrong* Cyberpunk install is worse than one that asks.

The same file also has commented-out overrides for xmake and the repo root, if a derivation
guesses wrong on your setup.

## The build

```
git submodule update --init
xmake f --vs_sdkver=10.0.22621.0
xmake -y -j 4
```

### The pins, and why each one exists

Upstream's CI passes only because it restores a *cached* package set. From a clean machine
the unpinned dependencies resolve to versions that no longer work.

| | |
|---|---|
| **protobuf-cpp 29.3** | 3.19.4 is too old — it lacks `RecordError` / `absl::string_view` in `MultiFileErrorCollector`, which `code/netpack/ErrorHandler.cpp` overrides. 35.1 is too new — it removed `FieldDescriptor::is_optional()`. |
| **Windows SDK 10.0.22621.0** | 10.0.26100.0 breaks the build. Installing a second SDK requires clearing **both** `xmake global --clean` and the local `.xmake/` and `build/` directories, or the two include paths collide with type-redefinition errors. |
| **CppSharp 1.1.84.17100 on net9.0** | For `SdkGenerator` only. The pinned 1.1.5.3168 bundles a Clang too old for MSVC 14.44's STL. The SDK and plugins stay on net8.0. |
| **pnpm 9** | pnpm 10+ blocks package build scripts by default, which breaks esbuild. |

Build with **`-j 4` or lower**. Higher parallelism has exhausted memory and produced
`C3859: Failed to create virtual memory for PCH`.

### A silent failure worth knowing about

`code/server/scripting/SdkGenerator/Program.cs` wraps its body in `catch { Console.WriteLine(e); }`,
so codegen failures **exit 0**. The build then fails much later with a confusing
`namespace 'Internal' does not exist`. If you see that, run the generator by hand to see
the real error.

---

## Building is not deploying

`xmake build Client` only links into `build/windows/x64/release/`. The copy into the game
happens in an `on_install` hook that derives its path from `installdir("launcher")`, so the
output prefix has to be given explicitly:

```
xmake build -j 4 Client
xmake install -o distrib Client
```

Running `xmake install Client` **without `-o distrib`** prints `install ok!` and writes to
xmake's default prefix instead — the game keeps loading the previous DLL. Confirm the
timestamp on `distrib/launcher/mod/CyberpunkMP.dll` actually changed.

The same applies to `Archives`, `Inputs`, `Tweaks` and `redscript`.

On the development machine, the game's `red4ext/plugins/zzzCyberpunkMP` is a **junction** to
`<repo>/distrib/launcher/mod`, so a deployed build is live without copying. Note the `zzz`
prefix — the folder is not called `CyberpunkMP`.

---

## Redscript is not the game's script dialect

The two look alike and are not the same language. This has broken the mod outright more
than once.

- Class parameters need `ref<T>`: the game's sources write `data: PauseMenuListItemData`,
  redscript requires `ref<PauseMenuListItemData>`.
- `const label: String` becomes `label: script_ref<String>`.

**A single bad `.reds` file aborts the entire compilation**, so the game starts with *no*
scripts at all — no menu entry, no chat, no HUD, and nothing pointing at the cause. It
looks exactly like the mod doing nothing.

Before you deploy any script change:

```
.\tools\CheckScripts.ps1
```

That is also the only practical way to find out whether a game API exists. There is no RTTI
dump; `scc` reports every unresolved symbol in one pass, so a file full of candidate calls
answers a batch of "does this method exist" questions in one run. Guessing and shipping is
what the gate exists to catch.

**Do not invoke `scc.exe` directly.** It is the same binary the game's loader uses, so on
failure it pops the player-facing "REDScript compilation has failed" dialog and blocks until
somebody clicks OK — on whoever is at the machine. `CheckScripts.ps1` runs it detached and
kills it as soon as an error appears.

---

## Running it

**Server**

```
tools\StartServer.bat
```

Config lives at `build/windows/x64/release/config/server.json`. Discord roles decide
permissions — see `Discord.Roles`, `Discord.BotTokenFile` and `Discord.RolesFile`.

**Client**

Launch flags must use the **`=` form**:

```
--online --ip=<addr> --port=11778
```

The space-separated form (`--ip 1.2.3.4`) silently does nothing — `Settings::Load` only
assigns when the parsed value is non-empty, so it keeps the `127.0.0.1` default. This has
cost hours already.

**Networking** is over Tailscale. Windows blocks inbound ICMP by default, so `ping` failing
proves nothing — use `tailscale ping`. Firewall rules auto-created for `server.loader.exe`
default to the **Public** profile while Tailscale is **Private**; the rule must be created
with `-Profile Any`.

---

## Shipping

```
.\tools\Ship.ps1                 # everything that changed
.\tools\Ship.ps1 -Mod            # client mod only
.\tools\Ship.ps1 -WhatIf         # say what it would do
```

It builds, compile-checks the scripts, packages, publishes and announces to Discord, and it
refuses to publish anything that failed a check.

Three rules it enforces, each of which exists because it was broken once:

- **Patch bumps only** until this is genuinely 1.0-ready.
- **`publish/release-notes.md` must mention the version being shipped.** That file is the
  body of *every* release; it went stale at v0.1.12 and five subsequent releases published a
  page describing work from days earlier.
- **A release must carry the whole runtime set** — `server.json`, `modlist.json`,
  `roles.json`, `ModPayload.zip` — before it is promoted to `latest`. A launcher-only ship
  once published the installer alone, and since `latest` had moved to it, every launcher in
  existence 404'd on `server.json` and reported the server offline.

---

## Working from a second machine

Most of this project runs anywhere. A few things are tied to the machine that hosts the
server, and it is worth knowing which is which before you go looking for a bug that is
really just "you are not the host".

**Runs on any checkout**

- Building the client, server and launcher
- Editing redscript, and `tools\CheckScripts.ps1` to verify it
- Running a server locally for your own testing
- Branches, pull requests, code review
- Posting to the coordination feed (needs the tailnet — see below)

**Only on the host machine**

- `tools\Ship.ps1` publishing a release. It uploads to the GitHub release everyone's
  launcher reads from, so two people shipping would fight over `latest`.
- Discord announcements, which need the bot token.
- The live game server and the coordination API.
- Testing with real players, since they connect to the host's address.

**Getting on the tailnet.** The server and the coordination API are only reachable over
Tailscale. Ask Cam for an invite, or use the button in the launcher once you are verified in
the Discord. `tailscale status` hides shared devices — use `tailscale status --json` if you
want to check whether you are actually connected to the right network.

**Branches and PRs.** The host machine pushes to `main` directly, because shipping a release
and updating `main` happen together there. From a second checkout, work on a branch and open
a PR. That is not ceremony — it is the only way two people can work without one of them
force-pushing over the other's `main`.

**Secrets.** Nothing secret is in the repo, and the files that hold secrets are ignored.
What each machine has to provide is documented by the committed `.example` templates:

| Ignored file | Template | What it is |
|---|---|---|
| `tools\ship.local.ps1` | `tools\ship.local.example.ps1` | your game install and tool paths |
| `config\server.json` | `config\server.example.json` | the game server's config |
| `tools\.discord-bot` | `tools\.discord-bot.example` | bot token and announcement channel |
| `tools\.discord-webhook` | `tools\.discord-webhook.example` | fallback announcement route |
| `.env` | `.env.example` | admin credentials for docker compose |
| `code\coord-api\data\` | — | coordination keys and message history, host only |

## Where things are

| | |
|---|---|
| `code/client/` | the C++ mod — networking, interpolation, appearance, vehicles |
| `code/assets/redscript/` | the script half — menus, chat UI, death handling |
| `code/server/native/` | the C++ server — world, chat commands, Discord auth |
| `code/launcher-lite/` | the Electron launcher |
| `code/coord-api/` | small HTTP service the assistants use to post updates to each other |
| `publish/` | release notes, the to-do list, the published JSON the launcher reads |
| `tools/` | Ship, CheckScripts, the Discord bots |

`ASSISTANTS_COMMUNICATION.md` is an append-only log of what changed and why, including the
hypotheses that turned out to be wrong. It is usually the fastest way to find out whether
something has already been tried.

---

## What is currently broken

Kept current in [`publish/TODO.md`](publish/TODO.md), which is also posted to the Discord.

The two that need real work rather than a patch:

- **Vehicle physics is simulated independently on every machine**, so passenger rides bounce.
  Fixing it properly means one machine owning each vehicle and the others following.
- **Two remote players can render as each other.** Their appearance data arrives distinct,
  so the fault is in how it gets applied — `ScheduleSynchronizedAppearanceChanges` is the
  suspect.

---

## Licensing

Upstream is GPL-3.0 and so is this. The six bundled prerequisite mods ship their own licence
texts in `publish/fullinstall-base/LICENSES/` — that inclusion is the condition that makes
redistributing them legitimate, so do not drop it when changing the install package.
