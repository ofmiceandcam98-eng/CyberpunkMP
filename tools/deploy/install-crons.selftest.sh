#!/bin/bash
# install-crons.selftest.sh - what install-crons.sh adds, and that it never duplicates, against
# a stand-in crontab in a temp folder. Touches no real crontab.
#
#   /bin/bash tools/deploy/install-crons.selftest.sh
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "${T:?}"' EXIT

mkdir -p "$T/bin" "$T/both/live" "$T/both/live-authority/config" "$T/single/live"
cat > "$T/bin/crontab" <<'STUB'
#!/bin/bash
if [ "${1:-}" = "-l" ]; then cat "$FAKE_CRON" 2>/dev/null; else cat > "$FAKE_CRON"; fi
STUB
chmod +x "$T/bin/crontab"
export FAKE_CRON="$T/cron"

run () { PATH="$T/bin:$PATH" NCO_REPO_DIR="$1" /bin/bash "$HERE/install-crons.sh" ${2:-} > /dev/null; }
lines () { grep -c . "$FAKE_CRON"; }

failed=0
check () {
    if [ "$2" = "$3" ]; then echo "ok    $1"; else echo "FAIL  $1: expected $3, got $2"; failed=1; fi
}

: > "$FAKE_CRON"
run "$T/both/live"
check "empty crontab, two servers: every job" "$(lines)" 6
check "the test server's snapshot is scheduled" "$(grep -c '^0 4 .*NCO_BACKUP_DIR=' "$FAKE_CRON")" 1
check "every boot snapshots both servers" "$(grep -c '^@reboot.*NCO_BACKUP_DIR=' "$FAKE_CRON")" 1

run "$T/both/live"
check "running it again duplicates nothing" "$(lines)" 6

grep -v 'backup-gamedata' "$FAKE_CRON" > "$T/old"
echo "0 4 * * * /bin/bash $T/both/live/tools/deploy/backup-gamedata.sh" >> "$T/old"
cp "$T/old" "$FAKE_CRON"
run "$T/both/live"
check "a box with the old four jobs gets only the two new lines" "$(lines)" 6

sed -i 's/^0 4 \* \* \* \/bin\/bash/30 5 * * * \/bin\/bash/' "$FAKE_CRON"
run "$T/both/live"
check "a hand-tuned schedule is left alone" "$(grep -c '^30 5 ' "$FAKE_CRON")" 1
check "and not added a second time" "$(lines)" 6

grep -v 'NCO_BACKUP_DIR=' "$FAKE_CRON" > "$T/partial"
cp "$T/partial" "$FAKE_CRON"
run "$T/both/live" --dry-run
check "a dry run changes nothing" "$(lines)" 4

grep -v 'backup-gamedata' "$FAKE_CRON" > "$T/none"
echo "0 4 * * * NCO_GAMEDATA_SRC=x NCO_BACKUP_DIR=y /bin/bash $T/both/live/tools/deploy/backup-gamedata.sh" >> "$T/none"
cp "$T/none" "$FAKE_CRON"
run "$T/both/live"
check "another line using the script does not hide a missing live snapshot" "$(grep -c "^0 4 \* \* \* /bin/bash" "$FAKE_CRON")" 1

: > "$FAKE_CRON"
run "$T/single/live"
check "one server: no test-server lines" "$(grep -c 'NCO_BACKUP_DIR=' "$FAKE_CRON")" 0
check "one server: still snapshots at boot" "$(grep -c '^@reboot.*backup-gamedata' "$FAKE_CRON")" 1
check "one server: five lines" "$(lines)" 5

exit "$failed"
