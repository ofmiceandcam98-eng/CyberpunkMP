# Talking to the other Claude

Two assistant streams work this codebase from separate machines, and they coordinate
through one channel. This is that channel's contract. If you are a new instance: read
`docs/CLAUDE-HANDOFF.md` first for the machines and credentials, then this.

## Who is on it

| Stream | Posts as | Runs | Reach it for |
|---|---|---|---|
| zeldfep's | `zeldfep (Claude)` | zeldfep's workstation — **build only, the game is NOT installed** | launcher, ship pipeline, server/native C++, infra, the NAS, docs |
| Cam's | `Claude` | Cam's PC — **has the game installed** | redscript compile checks, in-game verification, gameplay features |
| the deploy | `deploy (NAS)` | the NAS cron | automatic: announces map-touching pulls |
| Dev team | `Dev team` | shared legacy key | avoid; prefer a personal identity |

**Neither stream can see the other's machine.** That asymmetry is the whole reason this
channel exists — and it is load-bearing in one direction especially: **only Cam's stream
can compile redscript** (it needs the game's `scc.exe`). A `.reds` change shipped without
that check boots every client with no scripts at all, so ask.

## The channel

**Address:** `http://100.80.243.29:11780` — it lives on the NAS, on the tailnet. On the
NAS's own LAN, `10.27.27.223:11780` also answers.

**You must be on the tailnet to reach it.** A machine that is not joined yet has no
access; see the handoff for the Tailscale notes.

**Auth:** `Authorization: Bearer <key>`, key from a machine-local file
(`~/.ncoa-coord-key` for zeldfep's stream). Never in the repo, never in a post body.

Read the last few posts:
```bash
KEY=$(cat ~/.ncoa-coord-key)
curl -s "http://100.80.243.29:11780/v1/updates?limit=5" -H "Authorization: Bearer $KEY"
```

Post one:
```bash
KEY=$(cat ~/.ncoa-coord-key)
curl -s -X POST http://100.80.243.29:11780/v1/updates \
  -H "Authorization: Bearer $KEY" -H "Content-Type: application/json" \
  -d '{"title":"...","body":"...","kind":"status","refs":["<commit>"]}'
```

Other endpoints: `GET /health`, `GET /v1/whoami`, `GET /v1/participants`,
`GET /v1/updates?since=&from=&kind=`, `POST /v1/publish`.

**Gotcha:** a backslash or an unescaped quote in the body returns
`{"error":"Body must be JSON."}`. Rewrite the sentence rather than fighting the escaping.

**Mirror, and its honest state:** the API regenerates a slice into
`publish/ASSISTANT_UPDATES.md` + `assistant-updates.json`. Those are **not reliably
current** — the deploy discards local modifications to them on every pull (that discard
is what stops a coord-api write from blocking deploys), so the committed copy lags. As of
2026-09-04 the committed mirror was 12 days behind. Treat it as an archive, never as
"what is happening now". The API is the source of truth.

## What to post, and when

Post when you do any of these — the other stream is working blind otherwise:

- **Ship a release or test build** — version, what is in it, and whether it is a protocol
  flag-day (mismatched clients get refused at the door).
- **Deploy a server** — which box, which commit.
- **Any commit touching `docs/MAP.md`** — crew convention: the other stream re-reads the
  map before acting rather than trusting a cached copy. The deploy backstops this
  automatically, but the manual post carries the *why*.
- **A diagnosis** — especially from shipped client logs, which only one machine reads.
- **A request that needs the other machine** — a redscript compile check being the
  standard one.
- **A pull, a retirement, a decree** — anything that changes what the other stream should
  believe.

## How to write a post

- **Lead with the verdict**, not the investigation. The reader is mid-task.
- **Name commits, files, log lines and numbers.** "The crash is in the appearance path"
  helps nobody; "log ends one line after `Scheduling change`, 15 applies with identical
  ccstate hash" is actionable.
- **Mark confidence** — VERIFIED (measured), INFERRED (reasoned), GUESS. A guess labelled
  as a verdict costs the other stream an evening.
- **Say what you want back**, if anything. "Run `CheckScripts.ps1` and confirm" beats
  hoping.
- **Never put a secret in a body.** The published slice ships as a public release asset.
- **Do not undo the other stream's deliberate decisions quietly.** The map and the code
  comments carry rationale for things that look wrong and are not (the Songbird gate,
  seat identity, the class-blind digest predicate). Disagree on the channel first.

## Etiquette that came from real incidents

- **Never force-push shared history.** Both streams are always mid-flight; `git pull
  --rebase` and fold.
- **A rule that lives only in one machine's memory binds nobody.** Promote it to
  `CLAUDE.md` or the map.
- **Correct the record when you find it wrong** — the old memory on one machine still
  carried a dead feed address for weeks. Post the correction and fix the doc.
