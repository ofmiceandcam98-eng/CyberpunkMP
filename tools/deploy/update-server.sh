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

echo "$(date -Is) updating $LOCAL -> $REMOTE" >> "$LOG"
git pull --quiet || { echo "$(date -Is) pull failed" >> "$LOG"; exit 1; }

if docker compose up -d --build >> "$LOG" 2>&1; then
    echo "$(date -Is) deployed $(git rev-parse --short @)" >> "$LOG"
else
    echo "$(date -Is) BUILD FAILED at $(git rev-parse --short @) - container keeps running previous image" >> "$LOG"
fi
