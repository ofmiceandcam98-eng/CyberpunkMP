# Addresses — the real values do NOT live in git

Copy this to `docs/deploy/ADDRESSES.local.md` and fill it in. That file is gitignored, the
same way `tools/ship.local.ps1` and the coordination keys are: **this repository is public**,
and infrastructure addresses in a public repo are a map of what to attack once somebody has a
foothold, plus a list of exactly which files to take.

Every address here is Tailscale CGNAT (`100.64.0.0/10`) or RFC1918 — **none of it is
routable from the internet**, so this is topology disclosure rather than an open door. That
is why it is worth tidying but was never worth a history rewrite; see the map.

**If you do not have these values:** ask zeldfep, or read them off the server —
`tailscale status` names every node, and `docker ps` plus `docker inspect` names the
deployments.

```
SERVER_HOST      <server-host>       # ssh <user>@<server-host> — the box both deployments run on
LIVE_SERVER      <live-server>       # game on :11778, coordination feed on :11780
TEST_SERVER      <test-server>       # game on :11778, compose project -p nco-authority
OLD_NAS          <old-nas>           # RETIRED 2026-09-06, not wiped, crontab disarmed
```

## Where each one is actually needed

| Use | Which |
|---|---|
| SSH to manage either deployment | `SERVER_HOST` |
| Post to / read the coordination feed | `LIVE_SERVER:11780` |
| Point a launcher's DEV override at test | `TEST_SERVER` |
| Reach the archived old build | `OLD_NAS` |

## What is deliberately NOT sanitised, and why

- **`publish/server.json`** carries the live address in the clear and must. It is fetched at
  runtime by every launcher and is how players find the server at all. Sanitising it breaks
  the product.
- **`docs/deploy/claude-settings.local.json`** matches the feed by pattern rather than by
  host, so a new instance can curl it without a prompt and without the file naming the box.
- **`docs/NODE-TO-NODE-VERDICT.md`** is a historical record of a rejected experiment. Its
  addresses are retired and rewriting history to hide dead values buys nothing.
- **The coordination feed redacts on publish, not on write.** `code/coord-api/redact.js`
  strips addresses from the slice uploaded to the release; `coord-data/updates.jsonl` on the
  server keeps them. A diagnosis without the address it applies to is useless to the other
  stream, so the record has to keep what the publication cannot.
