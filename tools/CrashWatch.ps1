<#
    CrashWatch.ps1 - CyberpunkMP crash log catcher

    Leave this running before you start Cyberpunk. When the game exits it decides whether
    that was a crash, saves a timestamped copy of CyberpunkMP.log, and shows you the part
    that matters. Then it re-arms for the next launch.

    Why it exists: the mod opens its log with truncate=true, so the next launch WIPES the
    log from the session that crashed. If you relaunch before saving it, that evidence is
    gone for good.

    Usage - just run it, no arguments, no admin:
        powershell -ExecutionPolicy Bypass -File CrashWatch.ps1

    Saved logs go to:  Documents\CyberpunkMP-crashlogs\
#>

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms

$ProcName  = 'Cyberpunk2077'
$SaveDir   = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'CyberpunkMP-crashlogs'

if (-not (Test-Path $SaveDir)) {
    New-Item -ItemType Directory -Path $SaveDir | Out-Null
}

function Write-Status {
    param([string]$Text, [string]$Color = 'Gray')
    Write-Host ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $Text) -ForegroundColor $Color
}

# The log sits next to the mod DLL. The plugin folder is normally 'zzzCyberpunkMP', but
# don't rely on the name - RED4ext loads any subfolder, so search for the log itself.
#
# The mod writes one timestamped file per launch into a logs\ subfolder, so take the most
# recently written one. Older builds wrote a single CyberpunkMP.log; fall back to that.
#
# Call this AFTER the game exits - the file does not exist until the mod initialises.
function Find-ModLog {
    param([string]$GameExePath)

    $gameRoot = Split-Path (Split-Path (Split-Path $GameExePath -Parent) -Parent) -Parent
    $pluginRoot = Join-Path $gameRoot 'red4ext\plugins'

    if (-not (Test-Path $pluginRoot)) { return $null }

    $hit = Get-ChildItem $pluginRoot -Recurse -Filter 'CyberpunkMP_*.log' -ErrorAction SilentlyContinue |
           Sort-Object LastWriteTime -Descending |
           Select-Object -First 1

    if ($hit) { return $hit.FullName }

    $legacy = Get-ChildItem $pluginRoot -Recurse -Filter 'CyberpunkMP.log' -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending |
              Select-Object -First 1

    if ($legacy) { return $legacy.FullName }
    return $null
}

# Pull the interesting bits out of a log so nobody has to read the whole thing.
function Get-LogSummary {
    param([string]$LogPath)

    $lines = Get-Content $LogPath -ErrorAction SilentlyContinue
    if (-not $lines) { return "(log was empty)" }

    $probeNums = @()
    foreach ($m in ([regex]'\[PROBE (\d+)\]').Matches(($lines -join "`n"))) {
        $probeNums += [int]$m.Groups[1].Value
    }

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("Lines in log: $($lines.Count)")
    [void]$sb.AppendLine("")

    if ($probeNums.Count -gt 0) {
        $highest = ($probeNums | Measure-Object -Maximum).Maximum
        [void]$sb.AppendLine("HIGHEST PROBE REACHED: [PROBE $highest]")
        [void]$sb.AppendLine("  -> this names the last statement that ran before the crash.")
        [void]$sb.AppendLine("")
    } else {
        [void]$sb.AppendLine("No [PROBE n] lines found (either an older build, or the")
        [void]$sb.AppendLine("crash happened before the spawn path was reached).")
        [void]$sb.AppendLine("")
    }

    [void]$sb.AppendLine("--- last 25 lines ---")
    foreach ($l in ($lines | Select-Object -Last 25)) {
        [void]$sb.AppendLine($l)
    }

    return $sb.ToString()
}

function Save-Snapshot {
    param([string]$LogPath, [string]$Tag)

    if (-not (Test-Path $LogPath)) {
        Write-Status "No log at $LogPath - nothing to save." 'Yellow'
        return $null
    }

    $stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
    $dest  = Join-Path $SaveDir ("CyberpunkMP_{0}_{1}.log" -f $stamp, $Tag)

    Copy-Item $LogPath $dest -Force
    return $dest
}

Write-Host ""
Write-Host "  CyberpunkMP Crash Watch" -ForegroundColor Cyan
Write-Host "  Saving to: $SaveDir" -ForegroundColor DarkGray
Write-Host "  Leave this window open. Ctrl+C to stop." -ForegroundColor DarkGray
Write-Host ""

while ($true) {

    # --- wait for the game -------------------------------------------------------
    $proc = Get-Process -Name $ProcName -ErrorAction SilentlyContinue | Select-Object -First 1

    if (-not $proc) {
        Write-Status "Waiting for Cyberpunk 2077 to start..."
        while (-not $proc) {
            Start-Sleep -Seconds 3
            $proc = Get-Process -Name $ProcName -ErrorAction SilentlyContinue | Select-Object -First 1
        }
    }

    $exePath = $null
    try { $exePath = $proc.Path } catch { }

    if (-not $exePath) {
        Write-Status "Game is running but its path is unreadable. Retrying..." 'Yellow'
        Start-Sleep -Seconds 5
        continue
    }

    Write-Status "Game running (PID $($proc.Id))." 'Green'
    Write-Status "Waiting for it to exit..." 'DarkGray'

    # --- wait for it to end ------------------------------------------------------
    try {
        $proc.WaitForExit()
    } catch {
        # Fall back to polling if we can't get a wait handle.
        while (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 2 }
    }

    Start-Sleep -Milliseconds 800   # let the last flush land

    $exitCode = $null
    try { $exitCode = $proc.ExitCode } catch { }

    # 0xC0000005 = access violation, the signature of this crash.
    # Anything non-zero is treated as a crash; 0 or unknown is treated as a clean quit.
    $isCrash = $false
    $reason  = "clean exit"

    if ($null -ne $exitCode -and $exitCode -ne 0) {
        $isCrash = $true
        $hex = "0x{0:X8}" -f $exitCode
        if ($exitCode -eq -1073741819) {
            $reason = "ACCESS VIOLATION ($hex)"
        } else {
            $reason = "exit code $exitCode ($hex)"
        }
    } elseif ($null -eq $exitCode) {
        $reason = "exit code unavailable - saving log just in case"
        $isCrash = $true
    }

    # Resolve the log only now - the mod creates it during startup, so looking earlier
    # would either miss it or find the previous session's file.
    $logPath = Find-ModLog -GameExePath $exePath

    if (-not $logPath) {
        Write-Status "Game exited ($reason), but no CyberpunkMP log was found." 'Yellow'
        Write-Status "Is the mod installed for this game copy?" 'Yellow'
        continue
    }

    Write-Status "Session log: $logPath" 'DarkGray'

    if (-not $isCrash) {
        # Still snapshot it. A "clean" exit can follow a bad session, and the next
        # launch will wipe this file.
        $saved = Save-Snapshot -LogPath $logPath -Tag 'clean'
        Write-Status "Game exited cleanly. Log kept at $saved" 'Green'
        continue
    }

    # --- crash path --------------------------------------------------------------
    Write-Host ""
    Write-Status "CRASH DETECTED - $reason" 'Red'

    $saved = Save-Snapshot -LogPath $logPath -Tag 'CRASH'
    if (-not $saved) { continue }

    $summary = Get-LogSummary -LogPath $saved

    Write-Host ""
    Write-Host $summary -ForegroundColor White
    Write-Host ""
    Write-Status "Saved to $saved" 'Cyan'
    Write-Host ""

    # Open it so it is literally in front of you, and select it in Explorer.
    Start-Process notepad.exe $saved
    Start-Process explorer.exe "/select,`"$saved`""

    $msg = "CyberpunkMP crashed.`r`n`r`n$reason`r`n`r`n" +
           "The log has been saved and opened in Notepad:`r`n$saved`r`n`r`n" +
           "Send that file to Cam.`r`n`r`n" +
           "--- summary ---`r`n$summary"

    [System.Windows.Forms.MessageBox]::Show(
        $msg, 'CyberpunkMP - crash captured',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Warning) | Out-Null

    Write-Status "Re-arming for the next launch..." 'DarkGray'
}
