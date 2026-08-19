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

git fetch --quiet origin || { echo "$(date -Is) fetch failed" >> "$LOG"; exit 1; }

LOCAL=$(git rev-parse @)
REMOTE=$(git rev-parse @{u})

[ "$LOCAL" = "$REMOTE" ] && exit 0   # nothing new

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

echo "$(date -Is) updating $LOCAL -> $REMOTE" >> "$LOG"
git pull --quiet || { echo "$(date -Is) pull failed" >> "$LOG"; exit 1; }

if docker compose up -d --build >> "$LOG" 2>&1; then
    echo "$(date -Is) deployed $(git rev-parse --short @)" >> "$LOG"
else
    echo "$(date -Is) BUILD FAILED at $(git rev-parse --short @) - container keeps running previous image" >> "$LOG"
fi
