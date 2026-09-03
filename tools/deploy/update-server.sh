#!/bin/bash
# Self-updater for a CyberpunkMP server deployment. Pulls the deployed branch from
# GitHub and rebuilds the container ONLY when new commits landed - so pushing to the
# branch is the entire deploy pipeline, no other machine involved.
#
# Run from cron every 10 minutes, ALWAYS via bash (some hosts - TrueNAS among them -
# mount /home noexec, where direct execution fails silently with exit 126):
#
#   */10 * * * * /bin/bash /path/to/update-server.sh
#
# The repo directory defaults to ~/CyberpunkMP; override with the first argument.
set -u
REPO="${1:-$HOME/CyberpunkMP}"
LOCK="$HOME/.nco-update.lock"
LOG="$HOME/nco-update.log"

exec 9>"$LOCK"
flock -n 9 || exit 0   # a previous run (or the initial build) is still going

cd "$REPO" || exit 1

# A coordination-API instance runs against this same checkout and writes its published
# slice INTO publish/, so those two files carry local modifications between deploys.
# git pull then refuses ("local changes would be overwritten by merge"), which this
# script records as "pull failed" in a log nobody reads - so deploys stop happening and
# nothing says so. That is not theoretical: on 2026-08-23 it held a live server fix off
# the box entirely, and the checkout had been stuck several commits back.
#
# Discarding them loses nothing. They are a regenerated slice of coord-data/updates.jsonl,
# which is untracked, is the actual record, and is never touched by git here. Scoped to
# exactly these two paths - a blanket reset would throw away real work.
git checkout --quiet -- publish/ASSISTANT_UPDATES.md publish/assistant-updates.json 2>/dev/null || true

git fetch --quiet origin || { echo "$(date -Is) fetch failed" >> "$LOG"; exit 1; }

LOCAL=$(git rev-parse @)
REMOTE=$(git rev-parse @{u})

[ "$LOCAL" = "$REMOTE" ] && exit 0   # nothing new

# The third file to silently kill every deploy (2026-09-03): a hand-seeded copy of a
# script that later landed in the repo (update-wolvenkit.sh). Fetch worked, pull
# refused ("untracked working tree files would be overwritten"), and "pull failed"
# scrolled unread for hours while every updated client was refused at the door. The
# two-path checkout above cures only the coord-api flavour; this cures the CLASS:
# any UNTRACKED file the incoming commits are about to CREATE gets shelved aside with
# a loud log line. Untracked means the repo never owned it, so nothing of the repo's
# is lost - and a hand-seeded copy of a soon-to-be-committed file is exactly what
# this box keeps growing. Tracked local modifications are deliberately NOT touched:
# those are real divergence and deserve a failed pull someone investigates.
for f in $(git diff --name-only --diff-filter=A "$LOCAL" "$REMOTE"); do
    if [ -e "$f" ] && ! git ls-files --error-unmatch "$f" >/dev/null 2>&1; then
        mv "$f" "$f.deploy-shelved-$(date +%Y%m%d%H%M%S)" 2>/dev/null || continue
        echo "$(date -Is) shelved untracked '$f' - it collided with an incoming file of the same name" >> "$LOG"
    fi
done

# Never rebuild under a live session. A deploy is a full native compile on the same
# 4-core box that is serving the tick loop, followed by a container restart that kicks
# everyone - which players experienced as the game "getting worse" in windows that
# correlated with pushes, not code. The status endpoint is queried through the
# tailscale sidecar because it shares the server's network namespace (the host only
# publishes the UDP game port). If the count cannot be read, deploy anyway: a server
# whose status endpoint is down needs the update more than it needs the courtesy.
SIDECAR="${SIDECAR:-cyberpunkmp-tailscale}"
PLAYERS=$(docker exec "$SIDECAR" wget -qO- -T 5 http://localhost:11778/api/v1/status/ 2>/dev/null \
          | grep -o '"Players": *[0-9]*' | grep -o '[0-9]*$')
if [ -n "$PLAYERS" ] && [ "$PLAYERS" -gt 0 ]; then
    echo "$(date -Is) deferring $LOCAL -> $REMOTE: $PLAYERS player(s) online" >> "$LOG"
    exit 0   # retry on the next cron tick; the fetch already happened, pull comes later
fi

# Rebuild only when the commits touch something the server image actually uses.
# Launcher releases and their bookkeeping (version bumps, notes, publish/ assets)
# land on this branch constantly, and every one used to rebuild the whole native
# image and RESTART the server - players saw "server not reachable" for each
# no-op deploy. A pull with no relevant changes is recorded and left running.
if ! git diff --name-only "$LOCAL" "$REMOTE" | grep -qE '^(code/(server|common|protocol|assets|client)|docker|Dockerfile|xmake)'; then
    git pull --quiet || { echo "$(date -Is) pull failed" >> "$LOG"; exit 1; }
    echo "$(date -Is) pulled $(git rev-parse --short @) - nothing the server uses changed, no restart" >> "$LOG"
    exit 0
fi

echo "$(date -Is) updating $LOCAL -> $REMOTE" >> "$LOG"
git pull --quiet || { echo "$(date -Is) pull failed" >> "$LOG"; exit 1; }

if docker compose up -d --build >> "$LOG" 2>&1; then
    echo "$(date -Is) deployed $(git rev-parse --short @)" >> "$LOG"
else
    echo "$(date -Is) BUILD FAILED at $(git rev-parse --short @) - container keeps running previous image" >> "$LOG"
fi
