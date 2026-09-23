#!/bin/bash
# backup-gamedata.sh - nightly snapshot of the live server's game data.
#
# WHY THIS EXISTS
#
# The game data - config/players.json is every character and their progress, plus the
# world / respawn / startpoint / vehicle state - had NOTHING backing it up. The old
# Windows-side BackupServerData.ps1 pointed at the dead pre-migration NAS and only ran
# when a dev's PC happened to be on, so in practice a bad edit, a disk fault, or a wipe
# would have taken player progress with no recovery.
#
# This runs on the server itself, from cron, so the data is protected without any other
# machine being involved - the same reasoning as update-server.sh and watch-events.sh.
#
#   0 4 * * * /bin/bash /mnt/vol/projects/CyberpunkMP/tools/deploy/backup-gamedata.sh
#
# Run it from the LIVE checkout so update-server.sh keeps it current. Like the other
# crons, the cron LINE itself lives only in the crontab - a rebuilt box re-adds it by
# hand (noted on the Atlas: watch-events-cron-line-lives-only-in-the-crontab).
#
#   backup-gamedata.sh --dry-run   # say what it would copy/prune, touch nothing
set -u

SRC="${NCO_GAMEDATA_SRC:-/mnt/vol/projects/CyberpunkMP/config}"
DEST_ROOT="${NCO_BACKUP_DIR:-/mnt/vol/backups/gamedata}"
KEEP_DAYS="${NCO_BACKUP_KEEP_DAYS:-14}"
# server.example.json and _cache.json are deliberately excluded - a template and a cache,
# not state worth a snapshot.
FILES="players.json respawn.json startpoint.json worldstate.json worldfacts.json vehicles.json server.json mods.json"
DRY=0; [ "${1:-}" = "--dry-run" ] && DRY=1

stamp=$(date -u +%Y%m%d-%H%M%S)
dest="$DEST_ROOT/$stamp"

if [ "$DRY" = 1 ]; then
    echo "WOULD snapshot -> $dest"
    for f in $FILES; do [ -f "$SRC/$f" ] && echo "  + $f ($(stat -c %s "$SRC/$f") bytes)"; done
    echo "WOULD prune timestamped dirs older than $KEEP_DAYS days under $DEST_ROOT"
    exit 0
fi

mkdir -p "$dest" || exit 1

copied=0
for f in $FILES; do
    if [ -f "$SRC/$f" ]; then
        cp -p "$SRC/$f" "$dest/$f" && copied=$((copied + 1))
    fi
done

# A snapshot with no players.json is a snapshot of nothing worth keeping - refuse it
# loudly rather than rotate a good backup out behind an empty one.
if [ ! -s "$dest/players.json" ]; then
    rmdir "$dest" 2>/dev/null
    echo "$(date -Is) SKIPPED: players.json missing or empty at $SRC - no snapshot taken" >> "$DEST_ROOT/backup.log"
    exit 1
fi

echo "$(date -Is) snapshot $copied file(s) -> $stamp ($(du -sh "$dest" | cut -f1))" >> "$DEST_ROOT/backup.log"

# Prune our own timestamped snapshots older than KEEP_DAYS. Scoped to the 20YY-prefixed
# dirs this script makes, so nothing else under the backup root is ever touched.
find "$DEST_ROOT" -maxdepth 1 -type d -name '20*' -mtime "+$KEEP_DAYS" -exec rm -rf {} \; 2>/dev/null

exit 0
