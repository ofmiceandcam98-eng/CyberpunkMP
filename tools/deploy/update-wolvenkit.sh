#!/bin/bash
# Keeps a server-side copy of WolvenKit's headless CLI (WolvenKit.ConsoleLinux) current,
# for world-building/asset-inspection work run ON the server rather than someone's own
# machine - the same reasoning as nco_hackdump, but for resource formats (.ent, .mesh,
# .app, world/sector nodes) rather than TweakDB. See docs/MAP.md's CODE MAP, "World/asset
# editing" row, for why this exists and its version-mismatch warning.
#
# Self-throttled to roughly 72 hours rather than relying on cron's own schedule to hit
# that cadence exactly - cron has no clean "every 72 hours" expression (`*/72` in the hour
# field is not what it looks like: hours wrap at 24, so it fires at 0 and 72 mod 24 = 0,
# i.e. still daily), and a day-stepping `0 0 */3 * *` drifts against wall-clock time and
# resets at every month boundary. Recording our own last-check timestamp and comparing
# elapsed seconds is exact regardless of how often cron actually invokes this.
#
# Run from cron at any frequency AT LEAST as often as the target cadence - hourly is
# reasonable and cheap, since a false trigger just re-reads a timestamp file and exits:
#
#   0 * * * * /bin/bash /path/to/update-wolvenkit.sh
#
# The install directory defaults to ~/wolvenkit-console; override with the first argument.
set -u
DEST="${1:-$HOME/wolvenkit-console}"
LOCK="$HOME/.nco-wolvenkit-update.lock"
LOG="$HOME/nco-wolvenkit-update.log"
STAMP="$DEST/.last-check"
VERSION_FILE="$DEST/VERSION"
THROTTLE_SECONDS=$((72 * 3600))
REPO="WolvenKit/WolvenKit"

exec 9>"$LOCK"
flock -n 9 || exit 0   # a previous run is still going (a 150MB+ download can take a while)

mkdir -p "$DEST"

# Self-throttle. Cron may run this far more often than every 72 hours; only the elapsed
# time since the LAST ACTUAL CHECK decides whether this run does anything.
if [ -f "$STAMP" ]; then
    LAST=$(cat "$STAMP" 2>/dev/null || echo 0)
    NOW=$(date +%s)
    ELAPSED=$((NOW - LAST))
    if [ "$ELAPSED" -lt "$THROTTLE_SECONDS" ]; then
        exit 0   # not due yet - quiet, this is the expected outcome most runs
    fi
fi

date +%s > "$STAMP"

# Ask GitHub what the latest release actually is, rather than assuming a version scheme -
# WolvenKit tags are bare semver (8.20.0), no leading v, and releases are cut by their own
# CI (github-actions[bot]), not on any schedule this script could predict.
RELEASE_JSON=$(curl -fsSL -H "Accept: application/vnd.github+json" \
    "https://api.github.com/repos/$REPO/releases/latest" 2>>"$LOG")
if [ -z "$RELEASE_JSON" ]; then
    echo "$(date -Is) could not reach the GitHub API - leaving the existing copy alone" >> "$LOG"
    exit 0
fi

LATEST_TAG=$(echo "$RELEASE_JSON" | grep -o '"tag_name": *"[^"]*"' | head -1 | sed 's/.*"\([^"]*\)"$/\1/')
if [ -z "$LATEST_TAG" ]; then
    echo "$(date -Is) release JSON had no tag_name - GitHub API shape may have changed" >> "$LOG"
    exit 0
fi

CURRENT_TAG=""
[ -f "$VERSION_FILE" ] && CURRENT_TAG=$(cat "$VERSION_FILE" 2>/dev/null || echo "")

if [ "$LATEST_TAG" = "$CURRENT_TAG" ]; then
    echo "$(date -Is) already have $LATEST_TAG - no update" >> "$LOG"
    exit 0
fi

# The Linux CLI asset specifically - this is a server, and the full WolvenKit app is a
# 150MB+ WPF GUI that cannot run headless here. Name pattern is
# WolvenKit.ConsoleLinux-<version>.zip, consistent across releases.
DOWNLOAD_URL=$(echo "$RELEASE_JSON" | grep -o '"browser_download_url": *"[^"]*ConsoleLinux[^"]*"' \
    | head -1 | sed 's/.*"\(https[^"]*\)"$/\1/')
if [ -z "$DOWNLOAD_URL" ]; then
    echo "$(date -Is) $LATEST_TAG has no WolvenKit.ConsoleLinux asset - not updating" >> "$LOG"
    exit 0
fi

echo "$(date -Is) updating $CURRENT_TAG -> $LATEST_TAG" >> "$LOG"

TMP_ZIP="$DEST/.download-$LATEST_TAG.zip"
if ! curl -fsSL -o "$TMP_ZIP" "$DOWNLOAD_URL" 2>>"$LOG"; then
    echo "$(date -Is) download failed - $VERSION_FILE left at $CURRENT_TAG" >> "$LOG"
    rm -f "$TMP_ZIP"
    exit 1
fi

# Extract over a fresh subfolder rather than the destination root, so a partially-unzipped
# old version can never mix with the new one if this is interrupted.
STAGE="$DEST/.stage-$LATEST_TAG"
rm -rf "$STAGE"
mkdir -p "$STAGE"
if ! unzip -q "$TMP_ZIP" -d "$STAGE" 2>>"$LOG"; then
    echo "$(date -Is) unzip failed - $VERSION_FILE left at $CURRENT_TAG" >> "$LOG"
    rm -f "$TMP_ZIP"
    rm -rf "$STAGE"
    exit 1
fi
rm -f "$TMP_ZIP"

CURRENT_LIVE="$DEST/current"
rm -rf "$CURRENT_LIVE.old"
[ -d "$CURRENT_LIVE" ] && mv "$CURRENT_LIVE" "$CURRENT_LIVE.old"
mv "$STAGE" "$CURRENT_LIVE"
rm -rf "$CURRENT_LIVE.old"

echo "$LATEST_TAG" > "$VERSION_FILE"
echo "$(date -Is) updated to $LATEST_TAG at $CURRENT_LIVE" >> "$LOG"
