# Drop-in brief for a new Claude instance

Written 2026-09-04 by zeldfep's stream for the weekend migration: *"clone yourself so I
can drop you in on the new claude instance."* Read this once and you are current — it is
the operational knowledge that lived in one machine's memory, promoted into git where it
belongs.

**Read in this order:** `CLAUDE.md` (the shared rulebook, and it does NOT auto-load —
see §1) → `docs/MAP.md` §1 (the ledger: what is open) → this file (how to actually do
things) → the coordination feed (what the other stream just did).

---

## 1. The setup that isn't obvious

- **The session's working directory is the PARENT of the repo** (`.../Projects`, not
  `.../Projects/CyberpunkMP`). Consequence: the repo's `CLAUDE.md` does not auto-load.
  Read it deliberately at session start, every session.
- **Permissions:** copy `docs/deploy/claude-settings.local.json` to
  `<parent>/.claude/settings.local.json`. It is a curated generalized set, not a copy of
  the old machine's 803 accumulated one-off entries. Ships, releases, force-push and
  destructive commands are deliberately left to prompt.
- **Two Claude streams work this repo** — zeldfep's (this one) and Cam's, from separate
  machines. Cam's runs inside the repo so `CLAUDE.md` auto-loads for it. A rule that
  lives only in one machine's memory binds nobody: promote it to `CLAUDE.md`.

## 2. Machines and addresses

| What | Where | Notes |
|---|---|---|
| Your workstation | the repo checkout | Builds C++ (MSVC) and the launcher. **Whether it can also compile REDSCRIPT depends on whether the game is installed here — check, do not assume (§2a)** |
| NAS (both servers) | `ssh truenas_admin@10.27.27.223` | LAN SSH, key auth, docker group. **`/home` is mounted noexec** — always `/bin/bash script.sh`, never direct execution (exit 126, silent) |
| Live/public server | `/mnt/vol/projects/CyberpunkMP` → tailnet `100.80.243.29:11778` | containers `cyberpunkmp-server` + `cyberpunkmp-tailscale`; cron auto-deploys |
| Test server | `/mnt/vol/projects/CyberpunkMP-authority` → `100.125.74.56:11778` | compose project `-p nco-authority`; manual rebuild |
| Coordination feed | `http://100.80.243.29:11780` | on the NAS. **Ignore any older note saying 100.109.102.127 — that was Cam's PC and is dead** |

**Player-count probe** (the host publishes only UDP, so go through the sidecar's netns):
```
docker exec cyberpunkmp-tailscale wget -qO- http://localhost:11778/api/v1/status/
```

### 2a. Does THIS machine have the game? Check before you ship

It decides what you can verify yourself, and it has differed per machine — the box this
was first written on had no game; the machine it was written FOR does.

```powershell
Test-Path "<GameDir>\bin\x64\Cyberpunk2077.exe"   # is the GAME here?
Test-Path "<GameDir>\engine\tools\scc.exe"        # is the redscript COMPILER here?
```
**These are two separate questions and the second is the one that matters.** `scc.exe` does
NOT ship with the game - a stock Steam install of 2.31 does not contain it anywhere. It
arrives with **redscript**, one of the prerequisites the Night City Online launcher installs,
which puts it at `engine\tools\scc.exe` (the path `CheckScripts.ps1` looks at); the REDmod
DLC is the other source, at `tools\redmod\bin\scc.exe`. **Both halves were observed on one
box on 2026-09-04, hours apart, which is the whole argument for checking rather than
assuming:** before the mod was installed — game present and reporting 2.31, no `scc.exe`
anywhere, no `engine\tools`, no `r6\scripts`,
no `red4ext\plugins`. After the launcher installed the mod: `engine\tools\scc.exe` present,
`red4ext\` populated, and `CheckScripts.ps1` answering `OK - redscript compiles` against the
real 2.31 scripts. **So "the game is installed" does not mean you can compile redscript, and
"it could not compile an hour ago" does not mean it cannot now** — run the check, do not
carry the answer forward. Installing the mod once through the launcher brings the
prerequisites and creates the mod folder, and both `CheckScripts.ps1` and `DevInstall.ps1`
work from that point on.
Point the tooling at it once and everything below follows:
```
copy tools\ship.local.example.ps1 tools\ship.local.ps1   # then set $GameDir
```
(or set `CYBERPUNKMP_GAME_DIR` in the environment).

**With the game installed** — the good case: `tools\CheckScripts.ps1` compiles the
redscript against the real game, `Ship.ps1 -Mod` runs that gate for real, no stub game
dir and no hand-assembled `Rpc` folder, and you can test in-game yourself. **A `.reds`
change should never leave this machine uncompiled again** — that was the ship pipeline's
one real blind spot.

**Without it:** C++ and launcher work still build and ship, but redscript is unverifiable
here — `Ship.ps1 -Mod` needs `CYBERPUNKMP_GAME_DIR` pointed at a stub containing only
`bin\x64\Cyberpunk2077.exe` (the scc check then soft-skips) and
`distrib\launcher\mod\Rpc` assembled by hand from the previous release's
`ModPayload.zip`. Then **ask Cam's stream to run `CheckScripts.ps1`** before anyone
installs it: one bad `.reds` boots every client with no scripts at all.

## 3. Credentials — locations only, never values

Never in git, never in a feed post (the feed publishes a slice into `publish/`, which
ships as a **public** release asset). Logs record credential *presence*, never values.

| Key | Path | Loses you |
|---|---|---|
| Feed identity | `~/.ncoa-coord-key` (workstation) | posting as "zeldfep (Claude)" |
| Feed identity (deploy) | `~/.nco-deploy-coord-key` (NAS only) | the deploy's auto-announce |
| **Release signing** | `~/.nco-manifest-key` (keyid `882c415a`) | **signed releases** — pinned in every launcher since v0.3.97; a replacement must be pinned and shipped two releases apart |
| Tailscale API | `~/.tailscale-api-key` (mode 600) | minting auth keys + player invites |
| Discord bot | `<deploy>/config/discord-bot-token` (NAS) | role resolution |
| GitHub | Git Credential Manager | see §4 — `gh auth login` does NOT work here |

## 4. Recipes that took a while to get right

**GitHub token (bash only — PowerShell has no stdin for this and fails):**
```bash
export GH_TOKEN=$(printf 'protocol=https\nhost=github.com\n\n' | git credential fill | grep '^password=' | cut -d= -f2-)
export PATH="$PATH:/c/Program Files/GitHub CLI"
```

**Build** (from this workstation): `xmake build -j 4 Client` / `Server.Native`. Configure
once: `xmake f -p windows -a x64 --vs_sdkver=10.0.22621.0`. Pins are load-bearing (SDK
10.0.22621, protobuf-cpp 29.3, pnpm 9) — see CONTRIBUTING.md.

**Verify before shipping** (crew rule): `.\tools\Verify.ps1` — 116 checks incl.
`tools/tests/*.cpp` (auto-discovered; a new test file is picked up with no wiring).
`-SkipTests` for static checks only. `-SkipVerify` is for a FALSE POSITIVE only; every
use is a bug in Verify to fix.

**Ship a launcher** (`tools/Ship.ps1 -Launcher`): needs the GH_TOKEN dance above, run
from bash. It bumps `package.json` EARLY — a killed ship leaves the bump, so
`git checkout -- code/launcher-lite/package.json` before retrying. Notes gate requires a
`## What changed — v<next>` section in `publish/release-notes.md` first.

**Ship the mod too** (`-Mod`): needs a game dir — see §2a for the two cases. With the
game installed, the redscript compile gate runs for real and you are done. Without it,
§2a has the stub-and-hand-assembled-`Rpc` route, and the build must be compile-checked by
Cam's stream before anyone installs it.

**Deploy** = push to `feat/world-state`. The NAS cron (10 min) pulls, rebuilds only when
server-relevant paths changed, **defers while players are online**, auto-shelves untracked
collisions, and announces `docs/MAP.md` changes on the feed. Force it when empty:
`cd <deploy> && git pull --ff-only && docker compose up -d --build`.

**Read the field:** every launcher POSTs session logs to the server →
`/mnt/vol/projects/CyberpunkMP/logs/clients/<player>/` (newest 10 + `launcher-trail.log`).
**First stop for any "it broke on my machine" — never ask a player for files.**
Client-side redscript must log via `ScriptLog` to reach these; `FTLog` reaches nothing
anyone collects.

**Post to the feed** — full contract and etiquette in `docs/LLM-COMMS.md`; do this for ships, deploys, diagnoses, and every map change:
```bash
KEY=$(cat ~/.ncoa-coord-key)
curl -s -X POST http://100.80.243.29:11780/v1/updates -H "Authorization: Bearer $KEY" \
  -H "Content-Type: application/json" -d '{"title":"...","body":"...","kind":"status"}'
```
Backslashes and unescaped quotes in the body break the JSON — the API says
`Body must be JSON.`

## 5. Gotchas that cost real evenings

- **Protocol lockstep:** changing `client/server.proto` is a self-enforcing flag-day
  (mismatched clients are refused at the door with a popup). A `common.proto` content
  change is a flag-day BY CONVENTION — nothing enforces it. **Never memorize the
  identifier pair; read it from the build.** A server deployed ahead of the newest
  *release* locks every player out — that happened 2026-09-04 and needed an emergency
  ship.
- **Electron packaging:** a new launcher source file MUST be added to `package.json`
  `build.files` or the release is dead on arrival (v0.3.97).
- **CSS specificity in the launcher:** base `button.action` (0,1,1) beats bare class
  rules; overrides must be `button.action.x`. Two shipped "fixes" never rendered.
- **PowerShell 5.1 `-Encoding UTF8` writes a BOM**, which makes redscript fail with
  "syntax error at 1:1" naming nothing.
- **Python with Windows paths:** use heredocs or the Write tool, never `python -c` with
  backslashes. Console is cp1252 — `python -X utf8` for anything with arrows/dashes.
- **Restarting a server orphans a `docker logs -f` monitor.** Re-arm it after any
  rebuild, or use a reconnect loop.
- **Do not re-litigate the node-to-node era** (`docs/NODE-TO-NODE-VERDICT.md`): it was
  not better; what changed was topology, not code.

## 6. Where the durable knowledge lives

`CLAUDE.md` (rules) · `docs/MAP.md` (the ledger — landing removes, finding adds, SAME
commit, and any change is announced on the feed) · `docs/MANIFEST-ARCHITECTURE.md` ·
`docs/CRASH-FIX-BRIEF.md` · `docs/MIGRATION.md` (server + Claude migration) · this file.

A stream's memory directory is a CACHE of these. Carry it if convenient; never treat it
as a source of truth, and correct it against the map when they disagree — the old
machine's memory still contains a dead feed address, which is exactly the failure mode.

## 7. State as of this handoff (2026-09-04)

- **Current release v0.3.114**, shipped with the first **signed** manifest. Test channel
  is one build: `v0.3.114-worldstate-test.19` (byte-identical payload; its notes carry
  the two-human checklist).
- **Landed tonight, server-side and already on the public server:** arrivals point fires
  once per character and announces itself, `/revive` finds people by their picked name
  (prefix-matched) and messages use it, and an identity guard refuses a save that would
  flip an established character's body type.
- **Landed tonight, client-side, awaiting the next ship:** the appearance re-schedule
  crash fix, a spawn/respawn fall-through recovery guard, the Settings section rail
  (scrollbar replaced by arrows), and periodic update checks so an open launcher learns
  about releases.
- **Open, in the map:** whether a character's FIRST save captures the creator's choice
  (the wrong-gender question — a 60-second experiment settles it), per-connection
  interpolation delay for far players, and the two-human checklist that has never run.
