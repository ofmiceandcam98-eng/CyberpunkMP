#!/bin/bash
# watch-events.sh - posts significant game-server events to the coordination feed, so
# they survive the death of whatever Claude session was watching.
#
# Claude-session Monitors die with their session (and should - the no-stale-workloads
# decree). Events during unwatched hours were simply lost unless somebody grepped logs
# later. This runs ON the host from cron, so it outlives every session; the feed is
# where every stream already looks.
#
#   */5 * * * * /bin/bash /mnt/vol/projects/CyberpunkMP/tools/deploy/watch-events.sh
#
# Run it from the LIVE checkout: update-server.sh pulls that every 10 minutes, so the
# watcher self-updates. (Like update-wolvenkit.sh, the cron LINE itself lives only in
# the crontab - a rebuilt box needs it re-added by hand.)
#
#   watch-events.sh --dry-run     # print what would be posted, post nothing
#
# NOISE IS THE FAILURE MODE. The 2026-09-10 live session showed exactly which lines
# matter and which flood: connects, spawns, pings and saves are chatter; crashes,
# protocol refusals, identity-save refusals, sustained combat refusals and an
# OrphanVehicle runaway are signal. Each event class has a threshold and a cooldown,
# and one run posts at most ONE feed entry per container - a watcher that floods the
# feed trains everyone to ignore both.
set -u

STATE_DIR="$HOME/.nco-event-watcher"
KEYFILE="$HOME/.nco-deploy-coord-key"
FEED="http://127.0.0.1:11780/v1/updates"   # 127.0.0.1, never localhost - the feed binds IPv4-only

# Overridable so the detectors can be exercised against a throwaway container instead
# of waiting for a real crash - and so a future server joins by env, not by edit.
CONTAINERS="${NCO_WATCH_CONTAINERS:-nco-authority-server cyberpunkmp-server}"
COOLDOWN_S=3600        # per class+container; crashes use half of it
DRY=0; [ "${1:-}" = "--dry-run" ] && DRY=1

mkdir -p "$STATE_DIR"

# Soft-skip without the key, same as update-server.sh: watching is a courtesy that
# must never page anyone about its own configuration.
[ -f "$KEYFILE" ] || { [ "$DRY" = 1 ] && echo "no $KEYFILE - would post nothing"; exit 0; }

post_feed() { # $1 title, $2 body
    if [ "$DRY" = 1 ]; then
        echo "WOULD POST: $1"
        echo "$2" | sed 's/^/    /'
        return 0
    fi
    jq -n --arg t "$1" --arg b "$2" '{title: $t, body: $b, kind: "warning"}' \
      | curl -s -m 10 -X POST "$FEED" \
          -H "Authorization: Bearer $(cat "$KEYFILE")" -H 'Content-Type: application/json' \
          -d @- >/dev/null 2>&1 || true
}

# Cooldown: report a class once per window per container, not once per cron tick - an
# ongoing storm is one fact, not twelve posts. Crashes get half the window.
cooled() { # $1 container, $2 class, $3 window-seconds -> 0 if still cooling
    local f="$STATE_DIR/cool-$1-$2" now last
    now=$(date +%s)
    last=$(cat "$f" 2>/dev/null || echo 0)
    [ $((now - last)) -lt "$3" ] && return 0
    echo "$now" > "$f"
    return 1
}

# Up to 3 sample lines for a pattern, ANSI stripped, flattened for a JSON-safe body.
samples() { # $1 logs-file, $2 pattern
    grep -aiE "$2" "$1" | head -3 | cut -c1-220 | tr '\n' '\f' | sed 's/\f/  |  /g'
}

for c in $CONTAINERS; do
    docker inspect "$c" >/dev/null 2>&1 || continue

    since_file="$STATE_DIR/since-$c"
    since=$(cat "$since_file" 2>/dev/null || echo "10m")
    now_iso=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    logfile=$(mktemp)
    docker logs --since "$since" "$c" 2>&1 | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1b[=><]//g' > "$logfile"
    echo "$now_iso" > "$since_file"   # advance only after a successful read

    if [ ! -s "$logfile" ]; then rm -f "$logfile"; continue; fi

    findings=""

    # Crashes and the watchdog: always signal, shortest cooldown.
    n=$(grep -aciE 'watchdog|\[Crash\]|fatal|unhandled exception|terminate' "$logfile" || true)
    if [ "${n:-0}" -gt 0 ] && ! cooled "$c" crash $((COOLDOWN_S / 2)); then
        findings="$findings\nCRASH-CLASS lines: $n\n  $(samples "$logfile" 'watchdog|\[Crash\]|fatal|unhandled exception|terminate')"
    fi

    # Protocol / identifier refusals: a build mismatch at the door reads as "the button
    # does nothing" on the client - the server is the only place it is visible.
    n=$(grep -aciE 'refus[a-z]* .*(protocol|identifier)|protocol mismatch|denial' "$logfile" || true)
    if [ "${n:-0}" -gt 0 ] && ! cooled "$c" proto "$COOLDOWN_S"; then
        findings="$findings\nPROTOCOL refusals: $n\n  $(samples "$logfile" 'refus[a-z]* .*(protocol|identifier)|protocol mismatch|denial')"
    fi

    # Identity-save refusals: the guard that stops a template capture overwriting a real
    # character. Every firing is a live wrong-character-identity data point.
    n=$(grep -acE 'REFUSED a save' "$logfile" || true)
    if [ "${n:-0}" -gt 0 ] && ! cooled "$c" save "$COOLDOWN_S"; then
        findings="$findings\nIDENTITY save refusals: $n\n  $(samples "$logfile" 'REFUSED a save')"
    fi

    # Combat refusals: one or two is the rate limiter doing its job; a sustained run is
    # the fire-rate bug class back again.
    n=$(grep -acE 'Refused a (shot|reload)' "$logfile" || true)
    if [ "${n:-0}" -ge 10 ] && ! cooled "$c" combat "$COOLDOWN_S"; then
        findings="$findings\nSUSTAINED combat refusals: $n\n  $(samples "$logfile" 'Refused a (shot|reload)')"
    fi

    # OrphanVehicle: a handful is churn; a runaway (hundreds before the 0xC0000005) is
    # the two-player vehicle crash in progress.
    n=$(grep -acE 'OrphanVehicle' "$logfile" || true)
    if [ "${n:-0}" -ge 20 ] && ! cooled "$c" orphan "$COOLDOWN_S"; then
        findings="$findings\nOrphanVehicle runaway: $n lines\n  $(samples "$logfile" 'OrphanVehicle')"
    fi

    # More than one start in a window = crash-looping. Exactly one is usually a deploy,
    # which update-server.sh already logs - stay quiet for that.
    n=$(grep -acE 'Server started on port' "$logfile" || true)
    if [ "${n:-0}" -ge 2 ] && ! cooled "$c" restart "$COOLDOWN_S"; then
        findings="$findings\nRESTART LOOP: server started $n times in one window"
    fi

    if [ -n "$findings" ]; then
        body="Window since $since (UTC). One post per container per run; per-class cooldown $((COOLDOWN_S / 60))min. Pull full logs: docker logs --since $since $c$(printf '%b' "$findings")"
        post_feed "server events: $c" "$(printf '%b' "$body")"
    fi

    rm -f "$logfile"
done

exit 0
