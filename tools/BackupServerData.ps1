<#
    BackupServerData.ps1 - snapshot the server's character data, locally and to the NAS.

    Usage:
        .\tools\BackupServerData.ps1              # take a snapshot now
        .\tools\BackupServerData.ps1 -Install     # and run it nightly at 04:00

    WHAT THIS PROTECTS

    Not the code - git already does that, and the code is reproducible anyway. This is for
    players.json, which holds every character's appearance, inventory, money, skills and
    permanent id, keyed to a Discord account. Nothing regenerates it. A bad save, a
    mistaken wipe, or a failing disk costs every character on the server.

    The other three files are small and cheap to include: where new arrivals spawn, where
    the dead reappear, and the world clock. Losing them is an afternoon of re-setting
    points by hand rather than a catastrophe, but there is no reason to lose them either.

    WHY TWO COPIES

    A local snapshot covers the common case - a bad edit, a wipe, wanting yesterday's
    state back - and is instant. It does not cover the disk dying, which is the case that
    actually loses everything. The NAS copy is the one that matters for that, and it is
    already where the servers live.

    Copies are NOT committed. backups/ is gitignored: these hold player records tied to
    Discord accounts and have no business in a public repository.
#>

[CmdletBinding()]
param(
    [switch]$Install,
    [int]$KeepDays = 30
)

$ErrorActionPreference = 'Stop'

$repo    = Split-Path $PSScriptRoot -Parent
$config  = Join-Path $repo 'build\windows\x64\release\config'
$backups = Join-Path $repo 'backups'

# Files worth keeping, in the order they matter.
$files = @('players.json', 'respawn.json', 'startpoint.json', 'worldstate.json', 'server.json')

# The account and host are NOT in this file, because the repository is public and naming
# them hands anyone with a foothold a free step. They come from ship.local.ps1 (gitignored,
# beside the other machine-local settings) or from the environment.
#
# Failing loudly beats defaulting: a backup script that silently targets the wrong box
# writes nothing anywhere useful and says it succeeded.
. (Join-Path $PSScriptRoot 'Environment.ps1')

$nasUser = if ($env:NCO_NAS_USER) { $env:NCO_NAS_USER } else { $script:NasUser }
$nasHost = if ($env:NCO_NAS_HOST) { $env:NCO_NAS_HOST } else { $script:NasHost }

if (-not $nasUser -or -not $nasHost) {
    Write-Host "No backup target configured." -ForegroundColor Red
    Write-Host "  Set `$NasUser and `$NasHost in tools\ship.local.ps1, or NCO_NAS_USER and" -ForegroundColor Yellow
    Write-Host "  NCO_NAS_HOST in the environment. Real values are NOT in this repo - see" -ForegroundColor Yellow
    Write-Host "  docs\deploy\ADDRESSES.example.md." -ForegroundColor Yellow
    exit 1
}

$nasPath = '/mnt/vol/projects/nco-backups'
$sshKey  = Join-Path $env:USERPROFILE '.ssh\nco_nas'

# ---------------------------------------------------------------------------
# Install as a nightly task
# ---------------------------------------------------------------------------

if ($Install) {
    $action  = New-ScheduledTaskAction -Execute 'powershell.exe' `
                 -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    $trigger = New-ScheduledTaskTrigger -Daily -At 4am

    # Wake is deliberately NOT requested. A backup is not worth starting a sleeping PC
    # for; the point is that it happens on the nights the machine is on, which is most of
    # them, and the NAS copy covers the rest.
    $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -DontStopOnIdleEnd `
                  -ExecutionTimeLimit (New-TimeSpan -Minutes 10)

    Register-ScheduledTask -TaskName 'NightCityOnline-BackupServerData' `
        -Action $action -Trigger $trigger -Settings $settings -Force | Out-Null

    "Installed: nightly at 04:00. Runs on the next start if the PC was off."
    return
}

# ---------------------------------------------------------------------------
# Take the snapshot
# ---------------------------------------------------------------------------

if (-not (Test-Path $config)) {
    throw "No server config at $config - has the server ever run?"
}

$stamp = Get-Date -Format 'yyyy-MM-dd_HHmm'
$dest  = Join-Path $backups $stamp
New-Item -ItemType Directory -Path $dest -Force | Out-Null

$copied = 0
foreach ($f in $files) {
    $src = Join-Path $config $f
    if (Test-Path $src) {
        Copy-Item $src $dest -Force
        $copied++
    }
}

if ($copied -eq 0) {
    Remove-Item $dest -Recurse -Force
    throw "Nothing to back up - $config held none of the expected files."
}

"Snapshot: $copied file(s) -> $dest"

# ---------------------------------------------------------------------------
# Off this machine
# ---------------------------------------------------------------------------

if (Test-Path $sshKey) {
    try {
        # -BatchMode so a missing or changed key fails immediately rather than sitting at
        # a prompt forever in a scheduled task nobody is watching.
        & scp -i $sshKey -o BatchMode=yes -o StrictHostKeyChecking=no -o LogLevel=ERROR `
              -r $dest "${nasUser}@${nasHost}:$nasPath/" 2>&1 | Out-Null

        if ($LASTEXITCODE -eq 0) { "Copied to the NAS at $nasPath" }
        else { "NAS copy FAILED (exit $LASTEXITCODE) - the local snapshot is still good" }
    }
    catch {
        "NAS copy failed: $($_.Exception.Message) - the local snapshot is still good"
    }
}
else {
    "No SSH key at $sshKey - local snapshot only"
}

# ---------------------------------------------------------------------------
# Prune
# ---------------------------------------------------------------------------

# Local only. The NAS copies are left alone deliberately: it has the space, and the whole
# point of the off-machine copy is that this script cannot reach in and delete it if
# something here goes wrong.
$cutoff = (Get-Date).AddDays(-$KeepDays)
$old = Get-ChildItem $backups -Directory -ErrorAction SilentlyContinue |
       Where-Object { $_.CreationTime -lt $cutoff }

if ($old) {
    $old | Remove-Item -Recurse -Force
    "Pruned $($old.Count) local snapshot(s) older than $KeepDays days"
}
