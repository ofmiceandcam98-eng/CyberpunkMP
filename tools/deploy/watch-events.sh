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

# The event classes, one row per detector. Collapsed from five hand-copied blocks so a
# new class is a line, not a paste. Fields are '@'-delimited (no pattern uses '@'):
#
#   class @ grep-flags @ min-count @ cooldown-seconds @ regex @ message (printf, %s=count)
#
# min-count is the count the window must REACH to fire: crash/proto/save trip on the first
# matching line, a combat run needs 10, a restart LOOP needs 2+. Crashes get half the
# cooldown. Case-insensitive (-i) only where the emitter's casing varies - crash and proto.
#
# Patterns quote the ACTUAL emitters (Server.cpp:325/336, GameServer.cpp ~912/947,
# ChatSystem, Level), never a paraphrase - a regex that reworded a C++ string would watch
# nothing. NO RAW LOG LINES REACH THE BODY: the matched lines carry player usernames
# verbatim and redact.js scrubs addresses, not names, so only class + count is posted.
#
# No OrphanVehicle row: that string is emitted by the CLIENT only (InterpolationSystem.cpp)
# and this watcher reads server containers, so a detector for it here could never fire. The
# runaway shows up in client logs via PullLogs.ps1; the real fix is the server emitting
# vehicle-churn events itself.
DETECTORS=(
    "crash@-aciE@1@$((COOLDOWN_S / 2))@watchdog|\[Crash\]|fatal|unhandled exception|terminate@CRASH-CLASS lines: %s"
    "proto@-aciE@1@$COOLDOWN_S@was refused:|wrong server protocol|Connection attempt with client identifier|Refused connection@CONNECTION refusals (protocol/identifier/manifest): %s"
    "save@-acE@1@$COOLDOWN_S@REFUSED a save@IDENTITY save refusals: %s"
    "combat@-acE@10@$COOLDOWN_S@Refused a (shot|reload)@SUSTAINED combat refusals: %s"
    "restart@-acE@2@$COOLDOWN_S@Server started on port@RESTART LOOP: server started %s times in one window"
)

mkdir -p "$STATE_DIR"

# One run at a time. A slow docker-logs read could otherwise overlap the next cron tick
# and double-post the same window - every other cron script in tools/deploy takes this
# lock (update-server.sh, update-wolvenkit.sh). A previous run still going: quietly yield.
LOCK="$STATE_DIR/watch-events.lock"
exec 9>"$LOCK"
flock -n 9 || exit 0

# Soft-skip without the key, same as update-server.sh: watching is a courtesy that
# must never page anyone about its own configuration.
[ -f "$KEYFILE" ] || { [ "$DRY" = 1 ] && echo "no $KEYFILE - would post nothing"; exit 0; }

# jq builds the JSON payloads. If it is absent, say so loudly and stop rather than posting
# nothing on the quiet - a watcher that cannot report is worse than one that admits it.
# (--dry-run prints instead of posting, so it does not need jq.)
if [ "$DRY" != 1 ] && ! command -v jq >/dev/null 2>&1; then
    echo "watch-events: jq not found on PATH - required to build feed payloads; install jq" >&2
    exit 1
fi

post_feed() { # $1 title, $2 body
    if [ "$DRY" = 1 ]; then
        echo "WOULD POST: $1"
        echo "$2" | sed 's/^/    /'
        return 0
    fi
    # -S surfaces the transport error, -f turns an HTTP 4xx/5xx (a bad key, a down feed)
    # into a non-zero exit; `2>&1 >/dev/null` keeps curl's stderr and drops the response
    # body. A failed post is LOGGED with what and where, never swallowed - the whole point
    # of this watcher is to not lose events, and a silent POST failure loses them twice.
    local err
    if ! err=$(jq -n --arg t "$1" --arg b "$2" '{title: $t, body: $b, kind: "warning"}' \
      | curl -s -S -f -m 10 -X POST "$FEED" \
          -H "Authorization: Bearer $(cat "$KEYFILE")" -H 'Content-Type: application/json' \
          -d @- 2>&1 >/dev/null); then
        echo "watch-events: POST to $FEED failed: ${err:-unknown error}" >&2
    fi
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

    # One pass over the detector table. Each row carries its own threshold, cooldown and
    # message, so the combat run-of-10 and the restart-loop count-of-2 stay exactly what
    # they were - the only thing that changed is that the five blocks are now one loop.
    for row in "${DETECTORS[@]}"; do
        IFS='@' read -r class flags min cooldown pattern message <<< "$row"
        n=$(grep "$flags" -- "$pattern" "$logfile" || true)
        if [ "${n:-0}" -ge "$min" ] && ! cooled "$c" "$class" "$cooldown"; then
            findings="$findings\n$(printf -- "$message" "$n")"
        fi
    done

    if [ -n "$findings" ]; then
        body="Window since $since (UTC). One post per container per run; per-class cooldown $((COOLDOWN_S / 60))min. Pull full logs: docker logs --since $since $c$(printf '%b' "$findings")"
        post_feed "server events: $c" "$(printf '%b' "$body")"
    fi

    rm -f "$logfile"
done

exit 0
