#!/bin/bash
# watch-events.selftest.sh - the watcher against a stand-in docker that behaves the way a log
# damaged by an unclean death really behaves. Touches no container and posts nothing.
#
#   /bin/bash tools/deploy/watch-events.selftest.sh      (needs flock, so run it on the server)
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "${T:?}"' EXIT
mkdir -p "$T/bin" "$T/home/.nco-event-watcher"
: > "$T/home/.nco-deploy-coord-key"

# FAKE_POST lines exist after the damage and FAKE_PRE before it. As measured on the real servers:
# --tail N returns the newest N while N fits after the damage; past that it restarts from the top
# of the file and stops at the damage, so it returns only the old lines. --since reads forward and
# stops at the damage too.
cat > "$T/bin/docker" <<'STUB'
#!/bin/bash
case "$1" in
  inspect)
    [ "${2:-}" = "-f" ] && echo "$FAKE_STARTED"
    exit 0 ;;
  logs)
    tail_n=0
    while [ $# -gt 0 ]; do [ "$1" = "--tail" ] && tail_n="$2"; shift; done
    if [ -n "${FAKE_FAIL:-}" ]; then exit 1; fi
    if [ "$tail_n" -gt 0 ] && [ "$tail_n" -le "$(wc -l < "$FAKE_POST")" ]; then
      tail -n "$tail_n" "$FAKE_POST"
    else
      cat "$FAKE_PRE"
    fi ;;
esac
STUB
chmod +x "$T/bin/docker"

: > "$T/pre"
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do echo "2026-11-03T09:05:0$((i % 10)).000Z old line $i" >> "$T/pre"; done
export FAKE_PRE="$T/pre" FAKE_POST="$T/post" FAKE_STARTED="2026-11-05T04:52:46.000Z"

failed=0
check () { # $1 what, $2 output, $3 regex it must match
    if printf '%s' "$2" | grep -Eq "$3"; then echo "ok    $1"; else echo "FAIL  $1"; printf '%s\n' "$2" | sed 's/^/      /'; failed=1; fi
}
refute () {
    if printf '%s' "$2" | grep -Eq "$3"; then echo "FAIL  $1"; printf '%s\n' "$2" | sed 's/^/      /'; failed=1; else echo "ok    $1"; fi
}
run () { # $1 since stamp
    rm -f "$T/home/.nco-event-watcher/"*
    echo "$1" > "$T/home/.nco-event-watcher/since-fake"
    HOME="$T/home" PATH="$T/bin:$PATH" NCO_WATCH_CONTAINERS=fake /bin/bash "$HERE/watch-events.sh" --dry-run 2>&1
}

{
  for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do echo "2026-11-05T04:52:46.000Z [info] startup line $i"; done
  echo "2026-11-05T05:10:00.000Z [error] unhandled exception in the tick loop"
  for i in 1 2 3 4 5 6 7 8 9 10; do echo "2026-11-05T05:10:01.000Z [info] after line $i"; done
} > "$T/post"

check "a crash after the damage is seen" "$(run 2026-11-05T05:00:00Z)" 'WOULD POST.*fake|CRASH-CLASS lines: 1'
refute "lines before the window are not rescanned" "$(run 2026-11-05T05:20:00Z)" 'CRASH-CLASS'

: > "$T/post"
for i in 1 2 3; do echo "2026-11-05T04:52:46.000Z [info] startup line $i" >> "$T/post"; done
out="$(run 2026-11-05T04:50:00Z)"
check "fewer lines than the first read still reaches past the restart, or says it cannot" "$out" 'NOTHING|startup'

: > "$T/post"
out="$(FAKE_STARTED=2026-11-05T06:00:00.000Z run 2026-11-05T05:00:00Z)"
check "reading nothing across a restart says so out loud" "$out" 'read NOTHING from fake across a restart'

: > "$T/post"
for i in 1 2 3 4 5 6 7 8 9 10; do echo "2026-11-05T05:00:00.000Z [info] quiet $i" >> "$T/post"; done
out="$(FAKE_STARTED=2026-11-05T04:00:00.000Z run 2026-11-05T05:30:00Z)"
refute "a quiet window with no restart stays quiet" "$out" 'WOULD POST'

# A healthy log, for contrast: everything fits, nothing to fall back to.
for i in $(seq 1 400); do echo "2026-11-05T05:00:00.000Z [info] line $i"; done > "$T/post"
echo "2026-11-05T05:40:00.000Z [info] Server started on port 11778" >> "$T/post"
echo "2026-11-05T05:41:00.000Z [info] Server started on port 11778" >> "$T/post"
out="$(run 2026-11-05T05:30:00Z)"
check "a restart loop on a healthy log still fires" "$out" 'RESTART LOOP: server started 2 times'

# A read that fails must not advance the window, or that stretch is skipped forever.
rm -f "$T/home/.nco-event-watcher/"*
echo "2026-11-05T05:30:00Z" > "$T/home/.nco-event-watcher/since-fake"
FAKE_FAIL=1 HOME="$T/home" PATH="$T/bin:$PATH" NCO_WATCH_CONTAINERS=fake /bin/bash "$HERE/watch-events.sh" --dry-run > /dev/null 2>&1
check "a failed read keeps the window where it was" "$(cat "$T/home/.nco-event-watcher/since-fake")" '^2026-11-05T05:30:00Z$'

exit "$failed"
