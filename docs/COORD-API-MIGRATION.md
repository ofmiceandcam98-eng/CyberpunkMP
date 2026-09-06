# Moving the coordination feed off Cam's PC

The build is already portable. This is the one service that was not.

## Why this exists

Everything else moved to the NAS during the cutover — the game servers, the build, the
release pipeline (v0.3.58 shipped from zeldfep's VM, proving it). The coordination API did
not, because nothing ever described how to run it anywhere else. It only existed while one
particular desktop was switched on.

That is not a theoretical cost. **The feed has a two-day hole in it**, from 17 Aug 02:34 to
19 Aug, covering seventeen releases from v0.3.59 to v0.3.75 and the test.5 pre-release.
Posts made while the host was off were not queued — they were never made at all. Anyone
catching up from the feed would have believed the project was still on v0.3.58.

## What has to travel

Two files, both in `code/coord-api/data/` on the old host:

| file | what it is | size |
|---|---|---|
| `participants.json` | the five participant keys | ~570 B |
| `updates.jsonl` | the append-only history | ~23 KB |

On the old host they are at:

```
<repo>/code/coord-api/data/participants.json
<repo>/code/coord-api/data/updates.jsonl
```

They belong in `<repo>/coord-data/` on the new host — the compose service mounts that at
`/data` and points `NCO_COORD_DATA` there, so state lives outside the source mount and is
one directory to back up.

**Copy them directly, machine to machine.** The keys are name badges rather than passwords,
but there is no reason to put them through a chat window, a commit, or the feed itself.
There are only five slots; reissuing them is a nuisance, not a disaster, so if in any doubt
about how a copy travelled, reissue instead.

Carrying `updates.jsonl` matters more than it looks. It is the only record of what each
side did and why, and it is append-only by construction — starting fresh loses the
reasoning behind decisions that are still load-bearing.

## Running it

Nothing to build. The service imports only Node built-ins: no `package.json`, no
`npm install`, no Dockerfile. A stock `node:22-alpine` runs it as-is.

```bash
# on the host that will own the feed, in the repo root
mkdir -p coord-data
cp /path/from/old/host/participants.json coord-data/
cp /path/from/old/host/updates.jsonl     coord-data/

# .env - the tailnet address of THIS deployment
# .env - the SIDECAR node address, NOT the host machine.
#
# The service runs inside the tailscale sidecar network namespace, so what it should
# advertise is the deployment own node: nco-server-1 (100.109.52.23) if it lives with
# the main deployment. truenas-scale (100.90.85.33) is the machine underneath and is
# a separate node - handing that out would point people at the wrong host.
echo "NCO_COORD_HOST=100.109.52.23" >> .env

docker compose --profile coord up -d
```

Then check it answers, from another machine on the tailnet:

```bash
curl -s -o /dev/null -w '%{http_code}\n' http://100.109.52.23:11780/v1/updates
```

**401 is the correct answer.** It means the service is up and enforcing auth. A connection
refused means it is not running; a 200 without a key would mean something is wrong.

## Two things that are easy to get wrong

**It must run in exactly one place.** The service owns the keys and the history, so a
second copy on the test deployment is a second set of both, and they diverge silently. That
is why it sits behind a compose profile and is off by default — a plain `docker compose up
-d` will not start it. Start it deliberately, on one host.

**`NCO_COORD_HOST` has to be set here, even though it is optional elsewhere.** The service
discovers its own address by scanning local interfaces for a `100.64/10` one. That works on
a normal machine and cannot work behind this deployment's sidecar, which runs userspace
networking (`TS_USERSPACE`) and so creates no tun device carrying a tailnet address.
Inbound traffic still arrives - it reaches sockets in the shared namespace - but nothing in
that namespace can see what address it came in on.

Left unset, it starts happily and advertises loopback. It warns loudly in its own startup
output when that happens; compose cannot enforce it, because compose interpolates every
service's environment before applying profiles, so a required variable there would break
deployments that never run this service at all.

## Turning off the old one

Once the new one answers, stop the copy on Cam's PC. Two of them accepting posts means the
history splits in two, and neither is complete.

The launcher's dev panel has Start/Stop controls for the local service (`coord:start` /
`coord:stop`), which is the tidiest way to leave it off.
