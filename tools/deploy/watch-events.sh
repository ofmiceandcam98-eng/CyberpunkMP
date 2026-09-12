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
# connection refusals, identity-save refusals and sustained combat refusals are
# signal. Each event class has a threshold and a cooldown, and one run posts at most
# ONE feed entry per container - a watcher that floods the feed trains everyone to
# ignore both.
set -u -o pipefail

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

# NO RAW LOG LINES IN THE BODY. The feed publishes a slice into publish/, which ships
# as a public release asset, and the matched lines carry player usernames verbatim
# (ChatSystem "REFUSED a save from <name>", Level "Refused a shot from <name>") -
# redact.js scrubs addresses, not names. The rule is "logs record presence only":
# the post carries class + count, and the docker-logs pull command in the body is
# how a human gets the detail, on the host, off the record.

for c in $CONTAINERS; do
    docker inspect "$c" >/dev/null 2>&1 || continue

    since_file="$STATE_DIR/since-$c"
    since=$(cat "$since_file" 2>/dev/null || echo "10m")
    now_iso=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    logfile=$(mktemp)
    # stderr stays OUT of the scanned text (docker's own errors are not server events),
    # and pipefail makes the guard see a failed read - the window must not advance past
    # logs that were never scanned, or a crash during a container restart is skipped
    # forever. A failed read keeps $since; the next tick retries the same window.
    if ! docker logs --since "$since" "$c" 2>/dev/null | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\x1b[=><]//g' > "$logfile"; then
        rm -f "$logfile"; continue
    fi
    echo "$now_iso" > "$since_file"   # advance only after a successful read

    if [ ! -s "$logfile" ]; then rm -f "$logfile"; continue; fi

    findings=""

    # Crashes and the watchdog: always signal, shortest cooldown.
    n=$(grep -aciE 'watchdog|\[Crash\]|fatal|unhandled exception|terminate' "$logfile" || true)
    if [ "${n:-0}" -gt 0 ] && ! cooled "$c" crash $((COOLDOWN_S / 2)); then
        findings="$findings\nCRASH-CLASS lines: $n"
    fi

    # Protocol / identifier / manifest refusals at the door: a build mismatch reads as
    # "the button does nothing" on the client - the server is the only place it is
    # visible. Patterns quote the ACTUAL emitters (Server.cpp:325/336, GameServer.cpp
    # ~912/947); a regex that paraphrases a C++ string watches nothing.
    n=$(grep -aciE 'was refused:|wrong server protocol|Connection attempt with client identifier|Refused connection' "$logfile" || true)
    if [ "${n:-0}" -gt 0 ] && ! cooled "$c" proto "$COOLDOWN_S"; then
        findings="$findings\nCONNECTION refusals (protocol/identifier/manifest): $n"
    fi

    # Identity-save refusals: the guard that stops a template capture overwriting a real
    # character. Every firing is a live wrong-character-identity data point.
    n=$(grep -acE 'REFUSED a save' "$logfile" || true)
    if [ "${n:-0}" -gt 0 ] && ! cooled "$c" save "$COOLDOWN_S"; then
        findings="$findings\nIDENTITY save refusals: $n"
    fi

    # Combat refusals: one or two is the rate limiter doing its job; a sustained run is
    # the fire-rate bug class back again.
    n=$(grep -acE 'Refused a (shot|reload)' "$logfile" || true)
    if [ "${n:-0}" -ge 10 ] && ! cooled "$c" combat "$COOLDOWN_S"; then
        findings="$findings\nSUSTAINED combat refusals: $n"
    fi

    # (No OrphanVehicle detector: that string is emitted by the CLIENT only -
    # InterpolationSystem.cpp - and this watcher reads server containers. Watching for
    # it here can never fire; the runaway is visible in client logs via PullLogs.ps1.
    # The real fix is the server emitting vehicle-churn events itself.)

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
