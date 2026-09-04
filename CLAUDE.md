# Working rules — both Claude streams

Two assistant streams work this codebase from separate machines (zeldfep's and Cam's).
This file is the SHARED rulebook both load; machine-local memory is where it goes to die.
A rule that matters to both streams lives here or in the map — nowhere else.

## New instance? Start here

**`docs/CLAUDE-HANDOFF.md`** is the drop-in brief: the setup that is not obvious (this
session's cwd is the repo's PARENT, so this file does not auto-load), machines and
addresses, where every credential lives, the recipes that took a while to get right, the
gotchas that cost evenings, and the current state. A curated permission set to start from
is at `docs/deploy/claude-settings.local.json`, and the channel you use to talk to the
other Claude is documented in `docs/LLM-COMMS.md`.

## Read first, every session

1. **`docs/MAP.md`** — the ledger. Open items, standing decrees, code geography, the
   gotcha that bites in each area. If something feels missed, it should already be there.
2. **The coordination feed** — where both streams announce ships, flag-days, pulls,
   diagnoses, and map changes. Check it before shipping or deploying; post to it when you
   do any of those.
   - **Address**: `http://100.80.243.29:11780` (tailnet) OR `http://10.27.27.223:11780`
     (the NAS's LAN). **Try both before concluding it is down - they fail
     independently** (measured 2026-09-04: tailnet timed out, `tx 1560 rx 0` via a relay,
     while LAN answered instantly and the service was healthy throughout). From the NAS
     over SSH, `127.0.0.1:11780` separates "my route is broken" from "the service is
     down". `GET /v1/updates?limit=N` to read, `POST /v1/updates` with
     `Authorization: Bearer <key>` to post.
   - **Keys are machine-local files** (this stream: `~/.ncoa-coord-key`), never in the
     repo and NEVER in a feed body — the feed publishes a slice into `publish/`, which
     ships as a public release asset. Lost your key? Get it from the other human
     privately (Discord DM); each stream's participant identity already exists
     server-side.
   - **Full contract, etiquette and who-is-who: `docs/LLM-COMMS.md`.** Read it before
     your first post. The short version: post on every ship, deploy, map change and
     diagnosis; mark confidence; never put a secret in a body; and ask Cam's stream for
     anything needing the game (only that machine can compile redscript).
   - `publish/ASSISTANT_UPDATES.md` is an ARCHIVE, not a live mirror - the deploy
     discards local modifications to it, so the committed copy lags (12 days behind as
     of 2026-09-04). The API is the source of truth.

zeldfep's stream: your session cwd is the PARENT directory, so this file does NOT
auto-load — read it deliberately. Cam's stream: it auto-loads; keep it current.

## The decrees (full text in the map's Standing Decrees — do not paraphrase from memory)

- **Boot policy**: straight to menu, both halves stay.
- **The footprint rule**: uninstall leaves NOTHING; every write location in the footprint,
  both layers.
- **The helper rule**: content mods are never load-bearing — no feature depends on one,
  none gates Play/join/digest.
- **The map convention**: any commit touching `docs/MAP.md` gets a "map updated" post on
  the feed; the other stream re-reads before acting. Landing removes, finding adds, SAME
  commit. Write map entries in the ledger voice: tight categorized bullets, one home per
  fact.

## How we work (either stream — violating these has already cost us evenings)

- **Verify before you ship**: `.\tools\Verify.ps1` gates every ship. `-SkipVerify` exists
  for FALSE POSITIVES only — every use of it is a bug in Verify to fix, never a route
  around the gate.
- **Tests live in the repo** (`tools/tests/`, selftests beside their module), never in a
  scratchpad. A wiped temp dir once turned "the tests passed" into somebody's word.
- **Failure messages print what / where / fix.** The other stream picks up your failure
  with none of your context; a bare FAIL costs them a fresh investigation of something
  the check already knew.
- **Respect the do-not-undo blocks.** Deliberate decisions carry their rationale in the
  map and in code comments (the Songbird gate, seat identity, the /npc confirm guard, the
  digest predicate...). Read the rationale before "fixing" one; if you still disagree,
  raise it on the feed, don't revert it quietly.
- **Protocol lockstep**: changing `client/server.proto` is a self-enforcing flag-day;
  a `common.proto` content change is a flag-day BY CONVENTION — both sides ship together.
- **Keep probes and experiments out of `distrib/`** — the ship copies its assets
  wholesale, and anything left there ships to every player.
- **Don't pause the pipeline for a test session** — deploy and keep building; validation
  rides the next live session.
- **Missing tooling**: ask to install it rather than shipping "not compiled / not tested"
  caveats.
- **Secrets never enter the repo or the feed** — no keys, tokens, or credentials; logs
  record presence only. Key files stay machine-local; each stream keeps its own.
- **Never force-push shared history.** Pull --rebase and fold; the other stream is
  always mid-flight.

## When the streams disagree

The map is the tiebreaker for facts; the feed is the venue for decisions. A rule that
exists only in one machine's memory does not bind the other stream — promote it here
first.
