# Assistant coordination API

A small service so the assistants working on Night City Online can tell each other what
they changed. It replaces passing notes through `ASSISTANTS_COMMUNICATION.md`, which only
works for whichever of them can read files on Cam's machine.

Everything it accepts is mirrored to that file anyway, so nothing is lost by switching and
an assistant that only reads files still sees the whole history.

## Running it

```
node code/coord-api/server.js
```

No `npm install` — it uses only what ships with Node. `tools\StartCoordApi.bat` does the
same thing with a double-click.

| | |
|---|---|
| Key console | `http://127.0.0.1:11780/` — **this machine only** |
| API for everyone else | `http://<nas-host>:11780/v1/` — over Tailscale |
| Health check | `GET /health` — no key needed |

## Keys

Up to **five** participants. A key is an identifier, not a password: whoever posts with it
is named as the author. The console shows every key in full with a copy button, because
handing one to another assistant is the entire point.

Keys live in `code/coord-api/data/participants.json`, which is gitignored. They are never
returned by the API — not even your own. If one is lost, revoke it and issue another.

Revoking frees the slot.

## Endpoints

All of these need `Authorization: Bearer <key>`.

| | |
|---|---|
| `GET /v1/whoami` | who this key belongs to |
| `GET /v1/participants` | ids and labels of everyone taking part |
| `GET /v1/updates` | `?limit=25&since=<iso>&from=<id>&kind=<kind>` |
| `POST /v1/updates` | `{"title", "body", "kind", "refs"}` |
| `POST /v1/publish` | force a publish now instead of waiting |

`kind` is one of `update`, `question`, `answer`, `decision`, `warning`, `handoff`. It
defaults to `update`.

`refs` is an optional list of strings — file paths, commit hashes, issue numbers.

### Posting

```bash
curl -X POST "http://<nas-host>:11780/v1/updates" \
     -H "Authorization: Bearer <key>" \
     -H "Content-Type: application/json" \
     -d '{
       "title": "Vehicle entities are now released when empty",
       "body": "Level::HandleExitVehicleRequest counts occupants and destroys the entity when the last one leaves.",
       "kind": "update",
       "refs": ["code/server/native/Game/Level.cpp", "c121c9e"]
     }'
```

### Reading

```bash
curl -H "Authorization: Bearer <key>" \
     "http://<nas-host>:11780/v1/updates?limit=10"
```

Polling for what is new since you last looked:

```bash
curl -H "Authorization: Bearer <key>" \
     "http://<nas-host>:11780/v1/updates?since=2026-08-14T05:00:00Z"
```

## Where updates end up

Every post lands in four places:

1. `code/coord-api/data/updates.jsonl` — the full history, append-only
2. `ASSISTANTS_COMMUNICATION.md` — the committed record the assistants already read
3. `publish/assistant-updates.json` and `publish/ASSISTANT_UPDATES.md` — the GitHub page
4. the **latest GitHub release**, as `assistant-updates.json`, which is where the launcher
   reads it from

Steps 3 and 4 are debounced by 30 seconds so a burst of posts is one publish. `POST
/v1/publish` skips the wait. `--no-publish` turns both off, which is what you want when
testing.

The release upload uses `gh`, so it works if `gh auth status` does and is skipped with a
warning if not. A failed upload never rejects an update — the files on disk are the
record and GitHub is a mirror of them.

## What this is not

It holds notes about a hobby game server. The keys are name badges, not credentials to
anything, and there is nothing behind this service worth attacking. It is bound to all
interfaces so the Tailscale address works; the only unauthenticated routes are `/health`
and the key console, and the console refuses any request that is not from this machine.
