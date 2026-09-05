#!/bin/bash
#
# SessionStart hook - makes `tools/Verify.ps1` runnable in a Claude Code on the web
# session. That one command IS this project's gate (CLAUDE.md: "Verify.ps1 gates every
# ship"), so "the session can verify" and "the session can do anything shippable" are the
# same sentence here.
#
# WHAT A CLOUD SESSION IS MISSING WITHOUT THIS
#
#   pwsh                  every tool in tools/ is PowerShell; the container has none
#   nlohmann_json, glm,   the headers tools/tests/*.cpp include. On Windows they come from
#   spdlog                the xmake package cache; there is no xmake here, so they come
#                         from the distribution instead
#
# WHAT IT DELIBERATELY DOES NOT INSTALL
#
#   xmake / dotnet        the full server build. It is a 20+ minute dependency compile
#                         (protobuf, abseil, cryptopp, flecs, entt) that the Dockerfile
#                         already owns and that no verification step needs. If you want it,
#                         `docker build .` per CONTRIBUTING is the supported route.
#   electron              the launcher only ever builds `--win`. Nothing in a Linux
#                         container can produce or test that artifact.
#
# The redscript compiler is not installable at all here (CLAUDE-HANDOFF.md §2a: scc.exe
# arrives with the game), so a .reds change still cannot be compile-checked from a cloud
# session - ask Cam's stream, exactly as before.

set -euo pipefail

# Local checkouts already have MSVC, xmake and the game; installing Linux packages over the
# top of that is at best noise. Everything below is for the remote container only.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

log() { printf '[session-start] %s\n' "$1"; }

# --- headers for tools/tests/*.cpp -----------------------------------------------------
#
# Every test is one self-contained translation unit needing at most these three. The
# distribution's packages are used rather than a vendored copy: this is a throwaway
# container, and the alternative is carrying three upstreams in the repo for one platform.
MISSING=""
for pkg in nlohmann-json3-dev libglm-dev libspdlog-dev g++; do
  dpkg -s "$pkg" >/dev/null 2>&1 || MISSING="$MISSING $pkg"
done

if [ -n "$MISSING" ]; then
  log "installing:$MISSING"
  # apt-get update warns about third-party PPAs the proxy refuses; the Ubuntu archive
  # itself resolves, so do not let one 403 on an unrelated repo abort the whole hook.
  $SUDO apt-get update -qq || log "apt-get update reported errors - continuing, the archive is usually still usable"
  # stdout to /dev/null: dpkg's unpack chatter buries the hook's own lines. Errors go to
  # stderr and `set -e` still aborts on them, so nothing is being hidden.
  $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $MISSING >/dev/null
else
  log "C++ test headers already present"
fi

# --- PowerShell ------------------------------------------------------------------------
#
# Not in the Ubuntu archive and Microsoft publishes no noble apt repo, so this is the
# upstream tarball. Pinned: an unpinned "latest" download is a hook whose behaviour changes
# under you on a Tuesday.
PWSH_VERSION="7.4.6"
if command -v pwsh >/dev/null 2>&1; then
  log "pwsh $(pwsh --version 2>/dev/null | tr -d 'PowerShell ') already installed"
else
  case "$(uname -m)" in
    x86_64)         PWSH_ARCH="x64" ;;
    aarch64|arm64)  PWSH_ARCH="arm64" ;;
    *)              log "unknown architecture $(uname -m) - skipping pwsh; tools/*.ps1 will not run"; PWSH_ARCH="" ;;
  esac

  if [ -n "$PWSH_ARCH" ]; then
    PWSH_TGZ="$(mktemp -d)/powershell.tar.gz"
    PWSH_URL="https://github.com/PowerShell/PowerShell/releases/download/v${PWSH_VERSION}/powershell-${PWSH_VERSION}-linux-${PWSH_ARCH}.tar.gz"
    log "installing PowerShell ${PWSH_VERSION} (${PWSH_ARCH})"
    if curl -fsSL --retry 3 --retry-delay 2 -o "$PWSH_TGZ" "$PWSH_URL"; then
      $SUDO mkdir -p /opt/microsoft/powershell/7
      $SUDO tar -xzf "$PWSH_TGZ" -C /opt/microsoft/powershell/7
      $SUDO chmod +x /opt/microsoft/powershell/7/pwsh
      $SUDO ln -sf /opt/microsoft/powershell/7/pwsh /usr/local/bin/pwsh
      rm -f "$PWSH_TGZ"
    else
      log "could not download PowerShell - tools/*.ps1 will not run this session"
    fi
  fi
fi

# --- submodules ------------------------------------------------------------------------
#
# vendor/ is RED4ext.SDK, ArchiveXL, TweakXL and Codeware - client-side, and nothing the
# verifier touches. Fetched anyway so a session reading client code is not reading four
# empty directories, and non-fatal because none of the above depends on it.
if [ -f "${CLAUDE_PROJECT_DIR:-.}/.gitmodules" ]; then
  git -C "${CLAUDE_PROJECT_DIR:-.}" submodule update --init --depth 1 >/dev/null 2>&1 \
    && log "submodules initialised" \
    || log "submodule fetch failed - vendor/ is empty; nothing in tools/Verify.ps1 needs it"
fi

# --- prove it, do not assume it --------------------------------------------------------
#
# The gate is the reason this hook exists, so say plainly whether it can run. A session
# that starts believing it can verify and cannot is the expensive failure.
if command -v pwsh >/dev/null 2>&1; then
  log "ready - run the gate with:  pwsh -NoProfile -File tools/Verify.ps1"
else
  log "NOT ready - pwsh is missing, so tools/Verify.ps1 cannot run"
fi
