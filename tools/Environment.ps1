# Where this machine keeps things.
#
# Dot-sourced by the other tools. Everything here is either DERIVED from the checkout, or
# read from a gitignored tools\ship.local.ps1 that each machine provides for itself.
#
# Why this exists: Ship.ps1 and CheckScripts.ps1 had absolute paths to one person's PC
# baked into them. That was invisible while the project lived on one machine and became a
# wall the moment a second contributor had a checkout - the scripts did not fail with
# "tell me where your game is", they failed pretending to know.
#
# A script that needs something from the machine should SAY SO. Deriving what can be
# derived and naming the rest is the difference between "clone and go" and "read the source
# to find out why it is looking in someone else's home directory".
#
#   Repo        derived from git
#   XMake       found on PATH
#   GhRepo      derived from the git remote
#   GameDir     must be provided - there is no reliable way to guess it
#
# See tools\ship.local.example.ps1.

Set-StrictMode -Off

# --- derived ----------------------------------------------------------------

# git, not $PSScriptRoot\.. - so this is correct when called from a worktree or a
# subdirectory, and fails loudly outside a checkout rather than guessing.
#
# BUT ASKED ABOUT THE SCRIPT'S OWN LOCATION, NOT THE SHELL'S.
#
# `git rev-parse --show-toplevel` answers for the CURRENT DIRECTORY. With one checkout that
# is the same answer either way, which is why this went unnoticed. With two checkouts of
# this project on one machine - and there are two - it means the tooling audits whichever
# repository the shell happens to be standing in, not the one the tooling belongs to.
#
# That is not theoretical. Verify.ps1 was run from this checkout while the shell sat in the
# other one, and it reported SEVEN portability failures in files nobody had touched: it was
# faithfully auditing the stale checkout, which does not have the 34 GCC include fixes from
# 75d6ece. A false failure that reads as a real regression is expensive at any time and
# especially so during a deployment window.
#
# `git -C` keeps every property the original had - worktrees resolve correctly, a
# non-checkout still throws - and only changes WHICH directory the question is about.
$script:Repo = (& git -C $PSScriptRoot rev-parse --show-toplevel 2>$null)
if (-not $script:Repo) {
    throw "Not inside a git checkout: $PSScriptRoot is not in one. The tooling resolves the repository from its OWN location, not the current directory."
}
$script:Repo = (Resolve-Path $script:Repo).Path

$script:LauncherDir = Join-Path $script:Repo "code\launcher-lite"

$script:XMake = (Get-Command xmake -ErrorAction SilentlyContinue).Source

# Whichever remote points at a fork of this project. Falls back to `origin` so a plain
# clone still works; publishing targets are checked before use in Ship.ps1 anyway.
#
# The existing remotes are LISTED first rather than asking for each by name. `git remote
# get-url` writes to stderr when the remote does not exist, and PowerShell 5.1 wraps a
# native command's stderr in an ErrorRecord - which under $ErrorActionPreference='Stop'
# (which every caller sets) is a TERMINATING error.
#
# That failed on a fresh clone and nowhere else: this machine has a `fork` remote so the
# call never wrote to stderr here. It would have hit a second contributor on their first
# command and nobody else. Found by actually cloning rather than by reasoning about it.
$script:GhRepo = $null

$remotes = @()
try { $remotes = @(& git remote 2>$null) } catch { }

foreach ($remote in @('fork', 'origin')) {
    if ($remotes -notcontains $remote) { continue }

    $url = $null
    try { $url = (& git remote get-url $remote 2>$null) } catch { }

    if ($url -and $url -match 'github\.com[:/](?<owner>[^/]+)/(?<name>[^/.]+)') {
        $script:GhRepo = "$($Matches.owner)/$($Matches.name)"
        break
    }
}

# --- machine-specific -------------------------------------------------------

# Backup target. Deliberately EMPTY here and set in ship.local.ps1, which is gitignored:
# this repository is public, and naming the account and host of the box that holds every
# secret is the one step an attacker with a foothold would otherwise have to work for.
# See docs/deploy/ADDRESSES.example.md.
$script:NasUser = $null
$script:NasHost = $null


$script:GameDir = $null

$localConfig = Join-Path $PSScriptRoot "ship.local.ps1"
if (Test-Path $localConfig) {
    # Dot-sourced so it can set any of the above. Values it sets win over the derived
    # ones - a machine with two game installs or a non-PATH xmake needs to be able to say
    # which, and the derivation is a convenience rather than a rule.
    . $localConfig
}

# Environment wins over everything, for CI and one-off overrides.
if ($env:CYBERPUNKMP_GAME_DIR) { $script:GameDir = $env:CYBERPUNKMP_GAME_DIR }
if ($env:CYBERPUNKMP_REPO)     { $script:Repo    = $env:CYBERPUNKMP_REPO }

# A last-resort look in the usual place. Deliberately only ONE guess, and only if nothing
# else answered: a script that silently finds the wrong game install is worse than one that
# asks.
if (-not $script:GameDir) {
    $usual = "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077"
    if (Test-Path -LiteralPath "$usual\bin\x64\Cyberpunk2077.exe" -ErrorAction SilentlyContinue) {
        $script:GameDir = $usual
    }
}

function Assert-GameDir {
    <#
        Called by anything that actually needs the game. Kept separate so tools that do
        not touch it still run on a machine with no game installed at all.
    #>
    if (-not $script:GameDir) {
        throw @"
No Cyberpunk 2077 install configured.

Create tools\ship.local.ps1 (gitignored) with:

    `$GameDir = "D:\SteamLibrary\steamapps\common\Cyberpunk 2077"

There is a documented template at tools\ship.local.example.ps1.
Or set CYBERPUNKMP_GAME_DIR in the environment.
"@
    }

    # Built by string concatenation, not Join-Path.
    #
    # PowerShell 5.1's Join-Path VALIDATES the drive qualifier and throws "Cannot find
    # drive. A drive with the name 'D' does not exist." - so a mistyped drive letter in
    # ship.local.ps1 reported a PowerShell internal instead of the message explaining what
    # to configure. Test-Path's -ErrorAction does not help, because the throw happens
    # before Test-Path is ever reached.
    $exe = $script:GameDir.TrimEnd('\', '/') + '\bin\x64\Cyberpunk2077.exe'

    if (-not (Test-Path -LiteralPath $exe -ErrorAction SilentlyContinue)) {
        throw "GameDir is set to '$($script:GameDir)' but there is no bin\x64\Cyberpunk2077.exe there. Check tools\ship.local.ps1."
    }
}

function Assert-XMake {
    if (-not $script:XMake) {
        throw "xmake was not found on PATH. Install it, or set `$XMake in tools\ship.local.ps1."
    }
}
