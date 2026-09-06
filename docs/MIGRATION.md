# Moving a server (and a Claude) to new hardware

Written 2026-09-04 for the weekend's server + Claude migration, from a survey of what is
actually on the current box rather than from memory.

**The one-line rule:** git carries the CODE and the deploy machinery; it carries none of
the STATE and none of the SECRETS. Everything in the second list below has exactly one
copy in the world, and a migration that forgets it loses characters, money and access.

---

## 1. What git already carries (nothing to do)

`docker-compose.yml`, `Dockerfile`, the whole `code/` tree, `tools/deploy/*.sh`,
`tools/Ship.ps1`, `tools/Verify.ps1` + `tools/tests/`, `publish/` and these docs. A new
box clones the repo and can build and run — see §4.

## 2. What is machine-local and MUST be hand-carried

### 2a. Authoritative state — per deployment, under `<deploy>/config/`
Losing any of this loses player-visible history. None of it is in git, by design (it is
written by the running server).

| File | Holds | If lost |
|---|---|---|
| `players.json` | **every character**: appearance, name, IsMale, money, inventory, position, contacts, phone number, SpawnedBefore, jail | everyone starts over — the big one |
| `vehicles.json` | owned cars + plates | purchased cars vanish |
| `worldfacts.json` | opened doors / world flags | re-opened by hand |
| `worldstate.json`, `_cache.json` | clock/day, caches | cosmetic |
| `startpoint.json`, `respawn.json` | arrivals + respawn points | re-set with `/setstart`, `/setspawn` |
| `npcs.json` | `/npc` declarations | re-declared (test box only) |
| `calls.json`, `messages.json` | call + message history | history gone |
| `audit.log` | admin action record | the record is the point |
| `server.json` | port, tick rate, Discord ids, **admin password** | rebuilt from `server.example.json` |
| `discord-bot-token` | **SECRET** — the bot token | roles stop resolving |

`*.bak*` / `*.pre-*` / `*.ghosts-*` copies exist beside several of these (deliberate
backups from live incidents). Carry the directory wholesale rather than picking files.

### 2b. The coordination feed — `<live deploy>/coord-data/`
Untracked, and the REAL record: `updates.jsonl` (every post both Claude streams have
made) and `participants.json` (**SECRET — it contains every participant's bearer key**).
`publish/ASSISTANT_UPDATES.md` + `assistant-updates.json` are a regenerated slice of it
and are expected to show as locally modified; the deploy discards them on purpose.

### 2c. The test deployment's identity — `docker-compose.override.yml`
**UNTRACKED, and it is the only thing that makes the test box a separate deployment.**
It is three lines (container names + hostname + image name) and it is not reproducible
from git — copy it, or the second deployment collides with the first. *Fixed for the
future: a committed template now lives at `docs/deploy/docker-compose.authority.yml`.*

### 2d. Secrets and keys, per machine
- NAS: `~/.nco-deploy-coord-key` (the deploy's feed identity), `<deploy>/.env`
  (`TS_AUTHKEY`, `CYBERPUNKMP_ADMIN_PASSWORD`) plus its `.env.bak-*` copies.
- zeldfep's workstation: `~/.ncoa-coord-key` (feed), `~/.nco-manifest-key` (**the
  release signing key, keyid `882c415a`, pinned in every launcher since v0.3.97 — lose
  it and signed releases stop until a new key is pinned and shipped two releases apart**;
  see the manifest entry in the map), `~/.tailscale-api-key`.
- Cam's workstation: his own feed key; his manifest key stays PAUSED (map entry).

**None of these ever go in git or in a feed post** — the feed publishes a slice into
`publish/`, which ships as a public release asset.

### 2e. Cron
```
*/10 * * * * /bin/bash <live deploy>/tools/deploy/update-server.sh <live deploy>
0    * * * * /bin/bash <live deploy>/tools/deploy/update-wolvenkit.sh
```
Always via `/bin/bash` — TrueNAS mounts `/home` noexec, where direct execution fails
silently with exit 126.

### 2f. Tailscale
Each deployment's sidecar is a tailnet NODE. A new box gets new node identities, so the
published address changes: update `publish/server.json` (fetched by every launcher from
`releases/latest/download/`) and re-issue the invite. The launcher's checkup surfaces
which server it will use, which is the fastest way to confirm players followed.

## 3. The Claude migration

Both streams' durable context is IN GIT and needs nothing: `CLAUDE.md` (the shared
rulebook), `docs/MAP.md` (the ledger), `docs/MANIFEST-ARCHITECTURE.md`,
`docs/CRASH-FIX-BRIEF.md`, this file.

What is machine-local: each stream's memory directory (zeldfep's at
`~/.claude/projects/<project>/memory/`). It is a CACHE of what the map and rulebook
already say — a new machine reads `CLAUDE.md` at session start and is current. Carry it
if convenient; do not treat it as a source of truth. The rule stands: a fact that lives
only in one machine's memory does not bind the other stream — promote it to `CLAUDE.md`.

## 4. Standing a deployment up on new hardware

### 4a. Disk layout on the new box (decided 2026-09-06, zeldfep)

Two disks: a 238G NVMe and a 1.09T SSD. **OS on the NVMe, everything CyberpunkMP-related on
the 1.09T.** Ubuntu Server, no LVM - plain partitions are easier to recover and to resize.

| Path | Disk | Why |
|---|---|---|
| `/` | NVMe 238G | OS only. Guided "Use an entire disk" creates the ESP + root correctly |
| `/mnt/vol` | SSD 1.09T | **Deployments AND Docker.** Same path as today, deliberately |

- **`/mnt/vol` is not a cosmetic choice.** The deployments already live at
  `/mnt/vol/projects/CyberpunkMP` and `/mnt/vol/projects/CyberpunkMP-authority`, so keeping the mount
  point identical means the cron lines in 2e, every compose path, and every path in this
  document transfer UNCHANGED. Mount it anywhere else and all of them need editing - and one
  of them gets missed at 2am.
- **MOVE THE DOCKER DATA ROOT OR THE NVMe FILLS ANYWAY.** `/var/lib/docker` sits on `/` by
  default, and the server image is a full native compile: layers plus the xmake build cache
  run to tens of GB. Putting only the repo on the big disk leaves the actual bulk on the OS
  disk. Do this before pulling anything:

```
sudo systemctl stop docker
sudo mkdir -p /mnt/vol/docker
sudo rsync -aP /var/lib/docker/ /mnt/vol/docker/    # skip on a fresh Docker install
echo '{ "data-root": "/mnt/vol/docker" }' | sudo tee /etc/docker/daemon.json
sudo systemctl start docker
docker info | grep -i "docker root dir"             # must say /mnt/vol/docker
```

- **Partitioning trap, cost real time on the install:** subiquity will not offer **Add GPT
  Partition** on a disk already marked "to be formatted as ext4". A whole-disk format leaves
  no free space and no room for an ESP, which is why "Select a boot disk" then cannot be
  satisfied - the two errors are one problem. **Reformat first, then add partitions**, or skip
  manual mode and use guided "Use an entire disk" on the OS disk.
- **The USB installer shows up as a local disk** with an `iso9660` partition, and it carries
  the only ESP the installer can see. Do not touch it, and do not let its ESP stand in for the
  target disk.



1. `git clone` the repo; `git checkout feat/world-state` (what deploys today — see the
   live-vs-main entry in the map).
2. Copy `<old deploy>/config/` wholesale, `coord-data/` (live only), `.env`, and
   `docker-compose.override.yml` (second deployment only).
3. `docker compose up -d --build` (first build is a full native compile: ~10-15 min on
   4 cores; `--build-arg BUILD_JOBS=2` on a box that is also serving).
4. Install the cron lines from §2e.
5. Verify: `docker exec <tailscale-sidecar> wget -qO- http://localhost:11778/api/v1/status/`
   → expect `State: running`. Then check `players.json` came across: a character should
   log as `has character '<name>' (played)` on the owner's first join, not `never spawned`.
6. Publish the new address in `publish/server.json` and tell people to relaunch.

**The deploy is then self-serving:** push to the tracked branch and the cron pulls,
rebuilds only when server-relevant paths changed, defers while players are online, and
announces map changes on the feed. That is the "quick deployment from git" requirement —
the only manual steps are the ones above, because they are the ones git must not hold.

---

## 5. THE 2026-09-06 CUTOVER — live checklist

Tick as you go. Anything unticked is not done, however sure anyone feels.
**Old box:** TrueNAS, deployments stopped. **New box:** `officialcutstudios01`, Ubuntu 26.04.

### Hardware and OS
- [x] Ubuntu Server 26.04.1 installed, hostname `officialcutstudios01`
- [x] `/` on the 238G NVMe (LVM, extended to 232G), `/boot/efi` + `/boot` present
- [x] 1.09T SSD at `/mnt/vol`, **fstab by UUID** (not `/dev/sdX` - letters move)
- [x] Docker installed from the official repo (not `docker.io` - BuildKit needed for the xmake cache mount)
- [x] **`Docker Root Dir: /mnt/vol/docker`** and `/var/lib/docker` absent
- [x] `zeldfep` in the `docker` group
- [x] Tailscale up, host node `100.74.122.79`
- [x] SSH key authorised for the assistant

### Code and state
- [x] `/mnt/vol/projects/CyberpunkMP` cloned, `feat/world-state`, 4 submodules
- [x] `/mnt/vol/projects/CyberpunkMP-authority` cloned, same
- [x] Both old deployments stopped (**0 players at the moment of stop**)
- [x] `config/` carried, both deployments
- [x] `coord-data/` carried (live only)
- [x] `.env` carried, both
- [x] `docker-compose.override.yml` carried (test only - UNTRACKED, not reproducible from git)
- [x] `logs/` carried, both
- [x] **Checksum verified**: `players.json` md5 identical (74 characters), `updates.jsonl` identical (95 posts)
- [x] Fresh `TS_AUTHKEY` minted (`k4DKQhY33d11CNTRL`, expires 2026-12-05, reusable + preauthorised), written to both `.env`, mode 600, backups kept
- [x] Docs repointed `/mnt/vol/NASa` -> `/mnt/vol/projects` (`99d1551`)

### Bring-up — IN PROGRESS
- [x] Live: `docker compose up -d --build` completes (full native compile, 10-15 min)
- [x] Live: status endpoint answers `State: running` via the sidecar netns
- [x] Live: **new tailnet address recorded** (replaces `100.80.243.29`)
- [ ] Live: a returning character logs `has character '<name>' (played)`, NOT `never spawned`
- [x] coord-api container up; `GET /health` answers; posting works again
- [x] Test: built and started with `-p nco-authority`
- [x] Test: **new tailnet address recorded** (replaces `100.125.74.56`)
- [x] Cron installed, both lines, repointed at `/mnt/vol/projects/CyberpunkMP`

### Player-facing — do LAST, only after the above is green
- [x] `publish/server.json`: `host` + `coordHost` -> new address. **MUST be committed on `main`** - the publish workflow triggers on push to `main` only, and `workflow_dispatch` also checks out `main`. An edit on `feat/world-state` reaches nobody.
- [x] Confirm the release asset actually updated (`releases/latest/download/server.json`)
- [ ] **Invite distributed via Discord, NOT via `server.json`** - see the decision below
- [ ] Players told to relaunch

### Decisions taken during this cutover, recorded so they are not re-litigated
- **`tailscaleInvite` comes OUT of `server.json`.** That file is fetched from
  `releases/latest/download/server.json` with **no authentication** - verified, `http 200`
  to an anonymous request - so the invite was a tailnet join link on a public URL. The
  launcher's `tailscale:invite` handler has **no Discord or role check** either; it opens
  the link for anyone who clicks. The old invite is already consumed by an unidentifiable
  party. Omitting the field degrades gracefully ("No invite link is published yet").
  Fresh invite handed out in Discord instead.
  **Proper fix, later:** serve it from the coord-api behind the dev-role gate, exactly how
  coord keys are already handed out - `server.json` states that principle for the coord key
  and then violates it three lines further down.
- **`/mnt/vol/projects`, not `/mnt/vol/NASa`.** `NASa` was a TrueNAS *pool* name and means
  nothing on Ubuntu. Cheap to change: the cron takes the deployment dir as an argument and
  compose paths are relative, so only documentation referenced it.

### After the cutover — still open
- [ ] Launcher `index.html` dev panel hardcodes the OLD test-server address. **Needs a ship** - `server.json` is fetched at runtime, `index.html` is baked into the launcher.
- [ ] Sanitise internal addresses out of the public repo (18 tracked files; `publish/ASSISTANT_UPDATES.md` + `assistant-updates.json` are the ones that actually ship). Full copy to live off-git on the server. **Migration retires the old addresses anyway** - no history rewrite, force-push is forbidden.
- [ ] `CLAUDE-HANDOFF.md` §2 claims `100.109.102.127` "is dead". It was **seen on the tailnet 2026-09-06**. Correct it.
- [ ] Old NAS deployments: decide archive vs delete. Do not delete until the new box has served a real session.
- [ ] `rmdir /mnt/vol/NASa` on the new box once confirmed empty.
