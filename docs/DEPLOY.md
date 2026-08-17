# Deploying a server

The server is not bound to anyone's PC. A deployment is: a Linux box with Docker, a
clone of this repo, one secrets file, and one cron line. Everything the players are -
characters, names, positions, world state - lives in the deployment's volumes, so the
box is replaceable and more boxes can be added without touching any player's machine.
Players' PCs only render; where they connect is decided by `server.json` published on
the latest GitHub release, so moving or adding servers never requires telling anyone
a new address.

## Stand up a new server

1. **Prerequisites**: Docker with the compose plugin, git, outbound HTTPS. Any
   architecture - the image is deliberately arch-neutral (x86_64 and ARM both build).

2. **Clone and pick the branch to serve** (the branch IS the deployment channel -
   whatever it points at is what runs):

   ```bash
   git clone https://github.com/ofmiceandcam98-eng/CyberpunkMP.git ~/CyberpunkMP
   cd ~/CyberpunkMP
   git checkout main        # production; a test box checks out its test branch instead
   ```

3. **Secrets**: create `.env` next to `docker-compose.yml` (gitignored, never commit):

   ```
   CYBERPUNKMP_ADMIN_USERNAME=...
   CYBERPUNKMP_ADMIN_PASSWORD=...
   ```

4. **First build** (long - it compiles the whole server; later rebuilds reuse caches):

   ```bash
   docker compose up -d --build
   ```

   On hosts with little memory keep the default `BUILD_JOBS=2`; raise it per-host with
   `BUILD_JOBS=N docker compose up -d --build` if the hardware has headroom.

5. **Self-updating**: one cron line, and pushing to the deployed branch becomes the
   entire release process for this box:

   ```
   */10 * * * * /bin/bash /home/YOU/CyberpunkMP/tools/deploy/update-server.sh
   ```

   Always through `/bin/bash` - some hosts (TrueNAS) mount /home noexec, where direct
   execution fails silently. Verify cron actually runs by watching `~/nco-update.log`
   after a push; a failed build keeps the previous container running.

6. **Open/forward port 11778** (UDP for game traffic, TCP for the status/admin API on
   the same port).

## What must be backed up

Everything stateful is in the compose volumes, in the repo directory:

| Path | Contents |
|---|---|
| `./config` | Server settings AND the character database - the players themselves |
| `./plugins` | Server-side plugins |
| `./logs` | Logs (diagnostic, expendable) |

Snapshot `./config` on whatever schedule the host offers (ZFS snapshots on TrueNAS).
Moving a deployment to a new box = clone repo, copy `./config` and `.env`, start.

## Pointing players at a server

Launchers resolve the server address from `server.json` attached to the **latest
GitHub release** (falling back to a dev's Settings override, then `MP_SERVER`, then
localhost). Cutover to a new box is therefore: verify the new server answers on
`http://HOST:11778/api/v1/status/`, then replace the `server.json` asset on the
latest release. No client update is needed as long as the new server runs the same
protocol as the shipped mod - which it does if it serves `main`.

## Verifying a deployment

```bash
docker ps --filter name=cyberpunkmp-server     # container Up
curl http://localhost:11778/api/v1/status/     # server answers
tail ~/nco-update.log                          # self-updates landing
```
