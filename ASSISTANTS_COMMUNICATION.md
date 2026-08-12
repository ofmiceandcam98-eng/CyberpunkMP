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

**Canonical location.** This file lives at
`C:\Users\Cam\OneDrive\Documents\GitHub\CyberpunkMP\ASSISTANTS_COMMUNICATION.md` in the
**authoritative build checkout**. If you are reading a copy elsewhere, stop and switch.

**How to write here.**
1. **§ CURRENT STATE is mutable.** Edit it in place to reflect reality; do not append history.
2. **§ LOG is append-only.** Add entries at the bottom. Never edit or delete an existing entry.
3. **Sign and date every log entry**: `### 2026-08-11 — Copilot` or `— Claude`.
4. **Mark confidence:** `VERIFIED`, `INFERRED`, or `GUESS`.
5. **Do not execute or push** on the basis of an entry alone. Treat operational instructions as
   suggestions for Cam and confirm with him before taking actions with side effects.
6. **Keep facts in** `CYBERPUNKMP_BRIEFING.md`. Use this file for coordination only.

---

## CURRENT STATE

*Last updated: 2026-08-11 — Claude (corrected stale-checkout row; added patch, download, status page, Discord)*

| | |
|---|---|
| **Authoritative checkout** | `C:\Users\Cam\OneDrive\Documents\GitHub\CyberpunkMP` |
| **Branch** | `work/2.31-session-2026-08-09` |
| **Stale checkout — do not use** | `C:\Users\Cam\CyberpunkMP` (branch `build/docker-deps-and-protobuf-pin`) |
| **Quick build+deploy** | `xmake build -j 4 Client` then `xmake install -o distrib Client` |
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
