#!/bin/bash
# install-crons.sh - install (idempotently) the Night City Online server cron jobs.
#
# WHY THIS EXISTS
#
# The cron LINES used to live only in the crontab. A rebuilt box got every script back
# through git but nothing invoked them - and a watcher that never runs is indistinguishable
# from a quiet one, an updater that never fires just looks like nothing changed. Two of
# those (update-wolvenkit, watch-events) were flagged on the Atlas for exactly this. The
# schedule belongs in the repo, next to the scripts it schedules.
#
# Run once after a fresh checkout or a box rebuild, from the repo root or anywhere:
#
#   /bin/bash tools/deploy/install-crons.sh            # install what is missing
#   /bin/bash tools/deploy/install-crons.sh --dry-run  # say what it would do, touch nothing
#
# Idempotent: re-running never duplicates a line. Presence is decided by the SCRIPT PATH,
# not the exact schedule, so a schedule you hand-tuned in the crontab is left ALONE rather
# than reset to the default here - change one deliberately, and remove the old line by hand.
#
# Keep this list and docs/MIGRATION.md in step; both name the same four jobs.
set -u

# Where the checkout lives. Derived from this script's own location so the cron lines point
# at wherever it was run from; override with NCO_REPO_DIR when the script is piped over SSH
# (no on-disk path for $0). Falls back to the production path.
REPO_DIR="${NCO_REPO_DIR:-$(cd "$(dirname "$0")/../.." 2>/dev/null && pwd)}"
REPO_DIR="${REPO_DIR:-/mnt/vol/projects/CyberpunkMP}"

# The schedule. One line per NCO cron. Absolute paths, because cron runs with a bare PATH
# and no working directory - a relative path silently resolves to nothing.
CRONS=(
  "*/10 * * * * /bin/bash $REPO_DIR/tools/deploy/update-server.sh $REPO_DIR"
  "0 * * * * /bin/bash $REPO_DIR/tools/deploy/update-wolvenkit.sh"
  "*/5 * * * * /bin/bash $REPO_DIR/tools/deploy/watch-events.sh"
  "0 4 * * * /bin/bash $REPO_DIR/tools/deploy/backup-gamedata.sh"
)

DRY=0
[ "${1:-}" = "--dry-run" ] && DRY=1

echo "repo: $REPO_DIR"

current=$(crontab -l 2>/dev/null || true)
newcron="$current"
added=0
present=0

for line in "${CRONS[@]}"; do
    # Match on the .sh path only, so a present-but-rescheduled line counts as present.
    script=$(printf '%s' "$line" | grep -oE '/[^ ]+/tools/deploy/[a-z-]+\.sh')
    if printf '%s\n' "$current" | grep -Fq "$script"; then
        present=$((present + 1))
        echo "  present: ${script##*/}"
    else
        added=$((added + 1))
        echo "  ADD:     $line"
        newcron="$newcron
$line"
    fi
done

echo "-- $present present, $added to add --"

if [ "$added" -eq 0 ]; then
    echo "crontab already complete - nothing to do."
    exit 0
fi

if [ "$DRY" -eq 1 ]; then
    echo "(dry-run: crontab not modified)"
    exit 0
fi

# Drop blank lines the append may have introduced, then load.
printf '%s\n' "$newcron" | grep -v '^[[:space:]]*$' | crontab -
echo "installed. active NCO crons now:"
crontab -l | grep -E 'tools/deploy/' | sed 's/^/  /'
