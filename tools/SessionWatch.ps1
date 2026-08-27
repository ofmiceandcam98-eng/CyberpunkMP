# Standing session watcher. Leave it running; it writes a report nobody has to tail.
#
# Every diagnosis in this project has cost a round trip - "did it crash?", "was that a
# TDR?", "did the restore run?" - and the answers were always sitting in four different
# logs. This watches them all and writes ONE line per event, so the whole history of a
# play session can be read at a glance afterwards.
#
# Crucially it separates OUR failures from the machine's. A GPU driver timeout and a mod
# crash look identical from inside the game, and this project spent a full day chasing mod
# bugs that were actually amdkmdag.sys resetting the card. Every crash line here says which
# it was.
#
#   powershell -ExecutionPolicy Bypass -File tools\SessionWatch.ps1
#
# Report: %USERPROFILE%\Desktop\nco-session-report.txt
[CmdletBinding()]
param(
    [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077',
    [string]$Report  = "$env:USERPROFILE\Desktop\nco-session-report.txt",
    [int]$PollSeconds = 5
)

$ErrorActionPreference = 'Continue'

$modLogs  = Join-Path $GameDir 'red4ext\plugins\zzzCyberpunkMP\logs'
$r4Logs   = Join-Path $GameDir 'red4ext\logs'
$redsLog  = Join-Path $GameDir 'r6\logs\redscript_rCURRENT.log'
$crashCfg = "$env:LOCALAPPDATA\CD Projekt Red\Cyberpunk 2077\CrashInfo.json"

function Say([string]$text) {
    $line = "[{0:HH:mm:ss}] {1}" -f (Get-Date), $text
    Add-Content -Path $Report -Value $line -Encoding utf8
    Write-Host $line
}

# TDR count is only meaningful against a baseline, and the baseline is per-boot.
function TdrCount {
    try {
        $b = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
        @(Get-WinEvent -FilterHashtable @{LogName='Application';Id=1001;StartTime=$b} -EA SilentlyContinue |
          Where-Object { $_.Message -match 'Tdr|amdkmdag' }).Count
    } catch { -1 }
}

Set-Content -Path $Report -Value "" -Encoding utf8
Say "session watch started - reporting to $Report"
Say "watching: crashes, GPU timeouts, character restores, damage probe, script aborts"

$seenSession = $null
$reportedLines = 0
$tdrBase = TdrCount
Say "GPU timeout baseline for this boot: $tdrBase"

while ($true) {
    try {
        # --- a GPU timeout is the single most important thing to name correctly ---
        $tdr = TdrCount
        if ($tdr -ge 0 -and $tdr -gt $tdrBase) {
            Say "*** GPU DRIVER TIMEOUT (amdkmdag.sys) - $($tdr - $tdrBase) new. NOT a mod bug. ***"
            $tdrBase = $tdr
        }

        $current = Get-ChildItem $modLogs -Filter '*.log' -EA SilentlyContinue |
                   Sort-Object LastWriteTime -Descending | Select-Object -First 1

        if ($current) {
            if ($current.Name -ne $seenSession) {
                # The PREVIOUS session ended - say how, before moving on.
                if ($seenSession) {
                    $prevR4 = Get-ChildItem $r4Logs -Filter 'red4ext-*.log' -EA SilentlyContinue |
                              Sort-Object LastWriteTime -Descending | Select-Object -Skip 1 -First 1
                    if ($prevR4 -and -not (Select-String -Path $prevR4.FullName -Pattern 'has been terminated' -Quiet -EA SilentlyContinue)) {
                        Say "previous session ENDED WITHOUT CLEAN EXIT (crash)"
                    }
                }
                $seenSession = $current.Name
                $reportedLines = 0
                Say "--- new session: $($current.Name) ---"
            }

            # Only the lines that mean something, and each one only once.
            $interesting = Select-String -Path $current.FullName -EA SilentlyContinue `
                -Pattern 'Connected to server|Disconnected from server|restore DONE|restore: |cyberware:|VehProbe|OnVehicleExit: not mounted|no entity stub|OrphanVehicle|Detach\] done'
            if ($interesting.Count -gt $reportedLines) {
                $interesting | Select-Object -Skip $reportedLines | ForEach-Object {
                    $t = $_.Line -replace '^\[[^\]]*\] \[[^\]]*\] ', '' -replace '^\[script\] ', ''
                    Say "   $t"
                }
                $reportedLines = $interesting.Count
            }
        }

        # --- a script abort kills EVERY script, and looks like the mod doing nothing ---
        if (Test-Path $redsLog) {
            $errs = @(Select-String -Path $redsLog -Pattern '\[ERROR' -EA SilentlyContinue)
            if ($errs.Count -gt 0) {
                Say "*** REDSCRIPT ABORTED - the mod has NO scripts this session ***"
                $errs | Select-Object -First 3 | ForEach-Object { Say "    $($_.Line.Trim())" }
                Start-Sleep -Seconds 60   # said once, not every poll
            }
        }
    } catch {
        # A watcher that dies on a transient file lock is worse than no watcher.
    }

    Start-Sleep -Seconds $PollSeconds
}
