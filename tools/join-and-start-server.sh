#!/usr/bin/env bash
set -euo pipefail

# Usage: ./tools/join-and-start-server.sh <tailscale-authkey> [hostname]
# Provide the authkey as the first argument or set TS_AUTHKEY environment variable.

AUTHKEY=${1:-${TS_AUTHKEY:-}}
HOSTNAME=${2:-CyberpunkMP-Server}

if [ -z "$AUTHKEY" ]; then
  echo "Provide Tailscale auth key as first argument or set TS_AUTHKEY env var"
  exit 1
fi

echo "Bringing up Tailscale with hostname $HOSTNAME"

if ! command -v tailscale >/dev/null 2>&1; then
  echo "Tailscale not found; attempting install (Debian/Ubuntu)."
  if [ -f /etc/debian_version ]; then
    curl -fsSL https://pkgs.tailscale.com/stable/ubuntu/focal.gpg | sudo apt-key add -
    curl -fsSL https://pkgs.tailscale.com/stable/ubuntu/focal.list | sudo tee /etc/apt/sources.list.d/tailscale.list
    sudo apt update
    sudo apt install -y tailscale
  else
    echo "Non-debian distro: please install Tailscale manually and re-run this script.";
    exit 2
  fi
fi

sudo tailscale up --authkey="$AUTHKEY" --hostname="$HOSTNAME"

echo "Tailscale is up. Starting the game server..."

if [ -x tools/StartServer.sh ]; then
  ./tools/StartServer.sh &
  echo "Started tools/StartServer.sh (background)"
elif [ -f tools/StartServer.bat ]; then
  echo "Windows start script found: tools/StartServer.bat — run via PowerShell or cmd on Windows hosts."
else
  echo "No start script found under tools/. Start the server manually or add a start script named tools/StartServer.sh"
fi

echo "Done. Verify coordination API health on :11780 once the server is running."
