<#
    UpdateMod.ps1 - grab the latest CyberpunkMP test build

    Run this whenever you are told there is a new build. It finds your game, backs up the
    DLL you have, downloads the current one, and puts it in place. Takes a few seconds.

    Usage - close Cyberpunk first, then:
        powershell -ExecutionPolicy Bypass -File UpdateMod.ps1

    You only need this if you already installed the mod normally once. It updates one
    file; it does not install prerequisites.
#>

[CmdletBinding()]
param(
    [string]$GamePath,
    # The whole mod payload - DLL, redscript and Rpc together. Updating only the DLL was a
    # trap: fixes that live in redscript silently never reached anyone, so two people could
    # be on "the same build" and behave differently.
    # releases/latest, not a pinned tag: the default sat pointed at a dead test tag from
    # August 12th, so anyone still using this legacy tool was "updating" to a build older
    # than the one they had.
    [string]$Url = "https://github.com/ofmiceandcam98-eng/CyberpunkMP/releases/latest/download/ModPayload.zip",
    # Start the game once the update is done. This is how Play.bat uses it, so that
    # updating is something that happens TO you rather than a chore you remember.
    [switch]$Launch,
    [string]$Server,
    [int]$Port = 11778,
    # Skip the "press Enter" pauses, for the launch path.
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Say { param([string]$T, [string]$C = 'Gray') Write-Host $T -ForegroundColor $C }

function Pause-Unless-Quiet {
    if (-not $Quiet) { Read-Host "Press Enter to exit" }
}

# Starts the game with the right flags when -Launch was passed. Doing it here means the
# player never has to set Steam launch options - which is the single most common way this
# mod silently does nothing, because "--ip 1.2.3.4" with a space is quietly ignored.
function Start-GameIfAsked {
    if (-not $Launch) { return }

    $exe = Join-PathSafe $script:game "bin\x64\Cyberpunk2077.exe"
    if (-not (Test-Path $exe)) {
        Say "Could not find the game executable to launch." 'Red'
        return
    }

    $gameArgs = @("--online")

    if ($script:Server) {
        $gameArgs += "--ip=$($script:Server)"
        $gameArgs += "--port=$($script:Port)"
    }
    else {
        # Without an address the client falls back to 127.0.0.1 - your own PC. If you are
        # hosting, that is correct. If you are joining someone, it means you connect to
        # nothing and sit at "offline" with no explanation, which is exactly what happened
        # to the first person who tried this.
        Write-Host ""
        Say "No server address is set, so this will look for a server on THIS PC." 'Yellow'
        Say "If you are joining someone else, close the game, open the .bat in Notepad," 'Yellow'
        Say "and put their address on the SERVER= line." 'Yellow'
    }

    Write-Host ""
    Say "Starting Cyberpunk 2077..." 'Cyan'
    Say "  $($gameArgs -join ' ')" 'DarkGray'

    Start-Process -FilePath $exe -ArgumentList $gameArgs -WorkingDirectory (Split-Path $exe -Parent)
}


Write-Host ""
Say "  CyberpunkMP - update to the latest test build" 'Cyan'
Write-Host ""

# --- game must be closed -------------------------------------------------------
if (Get-Process -Name "Cyberpunk2077" -ErrorAction SilentlyContinue) {
    Say "Cyberpunk is running. Close it first, then run this again." 'Red'
    Write-Host ""
    Pause-Unless-Quiet
    exit 1
}

# --- find the game -------------------------------------------------------------
# Join-Path THROWS on a drive that does not exist - and Steam's library list routinely
# names drives that are unplugged or gone. Plain string joining cannot throw, so use it
# everywhere a path might reference a missing drive.
function Join-PathSafe {
    param([string]$Base, [string]$Leaf)
    if (-not $Base) { return $null }
    return ($Base.TrimEnd('\', '/') + '\' + $Leaf.TrimStart('\', '/'))
}

function Test-GameFolder {
    param([string]$Path)
    if (-not $Path) { return $false }
    try {
        return (Test-Path (Join-PathSafe $Path "bin\x64\Cyberpunk2077.exe"))
    } catch {
        return $false
    }
}

function Find-Game {
    param([string]$Explicit)

    if ($Explicit) {
        if (Test-GameFolder $Explicit) { return $Explicit }
        Say "No Cyberpunk2077.exe under: $Explicit" 'Red'
        return $null
    }

    $candidates = New-Object System.Collections.Generic.List[string]

    # --- 1. Ask Steam where it lives, instead of assuming C: --------------------
    # Steam records its own install path and every library folder it uses, including
    # ones on other drives. Far more reliable than guessing.
    $steamRoots = @()
    foreach ($key in @("HKCU:\Software\Valve\Steam",
                       "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam",
                       "HKLM:\SOFTWARE\Valve\Steam")) {
        try {
            $p = Get-ItemProperty -Path $key -ErrorAction Stop
            foreach ($v in @($p.SteamPath, $p.InstallPath)) {
                if ($v) { $steamRoots += ($v -replace '/', '\') }
            }
        } catch { }
    }
    $steamRoots += @("C:\Program Files (x86)\Steam", "C:\Program Files\Steam")

    foreach ($root in ($steamRoots | Select-Object -Unique)) {
        $candidates.Add((Join-PathSafe $root "steamapps\common\Cyberpunk 2077"))

        foreach ($vdf in @((Join-PathSafe $root "steamapps\libraryfolders.vdf"),
                           (Join-PathSafe $root "config\libraryfolders.vdf"))) {
            if (Test-Path $vdf) {
                foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                    $lib = $m.Groups[1].Value -replace '\\\\', '\'
                    $candidates.Add((Join-PathSafe $lib "steamapps\common\Cyberpunk 2077"))
                }
            }
        }
    }

    # --- 2. GOG registers the game's path as well ------------------------------
    foreach ($key in @("HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games\1423049311",
                       "HKLM:\SOFTWARE\GOG.com\Games\1423049311")) {
        try {
            $p = Get-ItemProperty -Path $key -ErrorAction Stop
            if ($p.path) { $candidates.Add($p.path) }
        } catch { }
    }

    # --- 3. Common layouts on EVERY fixed drive, not just C: -------------------
    $patterns = @(
        "SteamLibrary\steamapps\common\Cyberpunk 2077"
        "Steam\steamapps\common\Cyberpunk 2077"
        "Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077"
        "Program Files\Steam\steamapps\common\Cyberpunk 2077"
        "Games\Cyberpunk 2077"
        "GOG Games\Cyberpunk 2077"
        "GOG Galaxy\Games\Cyberpunk 2077"
        "Program Files (x86)\GOG Galaxy\Games\Cyberpunk 2077"
        "Epic Games\Cyberpunk2077"
        "Program Files\Epic Games\Cyberpunk2077"
        "Cyberpunk 2077"
    )

    # Checking a path is nearly free, so cast the widest net here: every ready drive,
    # whatever Windows calls it. People do run games from external SSDs, and unusual
    # letters like B: are not always reported as "Fixed".
    $allDrives = [System.IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady }

    # Searching a drive is expensive, so the deep scan later stays on internal disks.
    $drives = $allDrives | Where-Object { $_.DriveType -eq 'Fixed' }

    foreach ($d in $allDrives) {
        foreach ($pat in $patterns) {
            $candidates.Add((Join-PathSafe $d.RootDirectory.FullName $pat))
        }
    }

    foreach ($c in ($candidates | Select-Object -Unique)) {
        if (Test-GameFolder $c) { return $c }
    }

    # --- 4. Last resort: actually go looking ------------------------------------
    # Depth-limited so this takes seconds rather than minutes.
    Write-Host ""
    Say "Not in any usual location - searching your drives..." 'Yellow'

    foreach ($d in $drives) {
        $root = $d.RootDirectory.FullName
        Say "  scanning $root" 'DarkGray'
        try {
            $hit = Get-ChildItem -Path $root -Filter "Cyberpunk2077.exe" -Recurse -Depth 6 `
                                 -File -Force -ErrorAction SilentlyContinue |
                   Select-Object -First 1
            if ($hit) {
                # <game>\bin\x64\Cyberpunk2077.exe -> up three levels
                $guess = Split-Path (Split-Path (Split-Path $hit.FullName -Parent) -Parent) -Parent
                if (Test-GameFolder $guess) { return $guess }
            }
        } catch { }
    }

    return $null
}

$game = Find-Game -Explicit $GamePath

if (-not $game) {
    Say "Could not find Cyberpunk 2077 automatically." 'Yellow'
    Say "Re-run it with your game folder, for example:" 'DarkGray'
    Say '  powershell -ExecutionPolicy Bypass -File UpdateMod.ps1 -GamePath "D:\Games\Cyberpunk 2077"' 'DarkGray'
    Write-Host ""
    Pause-Unless-Quiet
    exit 1
}

Say "Game    : $game" 'DarkGray'

# --- find the installed mod folder ---------------------------------------------
$pluginRoot = Join-PathSafe $game "red4ext\plugins"

if (-not (Test-Path $pluginRoot)) {
    Say "No red4ext\plugins folder - install the mod normally first." 'Red'
    Write-Host ""
    Pause-Unless-Quiet
    exit 1
}

$modDir = Get-ChildItem $pluginRoot -Directory -ErrorAction SilentlyContinue |
          Where-Object { Test-Path (Join-Path $_.FullName "CyberpunkMP.dll") } |
          Select-Object -First 1

if (-not $modDir) {
    Say "CyberpunkMP is not installed yet - no folder under red4ext\plugins contains" 'Red'
    Say "CyberpunkMP.dll. Install the normal release first, then run this." 'Red'
    Write-Host ""
    Pause-Unless-Quiet
    exit 1
}

$target = Join-PathSafe $modDir.FullName "CyberpunkMP.dll"
Say "Mod     : $($modDir.Name)" 'DarkGray'

$old = Get-Item $target
Say "Current : $($old.LastWriteTime)" 'DarkGray'
Write-Host ""

# --- download to a temp file first ---------------------------------------------
# Never touch a working install until the whole download has landed.
$tmp = Join-PathSafe $env:TEMP ("CyberpunkMP_payload_{0}.zip" -f (Get-Date -Format 'HHmmss'))

Say "Downloading..." 'Cyan'
try {
    Invoke-WebRequest -Uri $Url -OutFile $tmp -UseBasicParsing
} catch {
    Say "Download failed: $($_.Exception.Message)" 'Red'
    Write-Host ""
    Pause-Unless-Quiet
    exit 1
}

$new = Get-Item $tmp
if ($new.Length -lt 1MB) {
    Say "That download looks wrong (only $($new.Length) bytes) - leaving your install alone." 'Red'
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    Write-Host ""
    Pause-Unless-Quiet
    exit 1
}

# A marker recording exactly which payload is installed. Comparing DLL file sizes was a
# poor proxy - two different builds can be the same size, and it said nothing at all about
# the redscript files.
$stampFile = Join-PathSafe $modDir.FullName ".installed-build"
$stamp = "{0}:{1}" -f $new.Length, ((Get-FileHash $tmp -Algorithm MD5).Hash)
$installed = ""
if (Test-Path $stampFile) { $installed = (Get-Content $stampFile -Raw).Trim() }

if ($installed -eq $stamp) {
    Say "Already up to date." 'Green'
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    Write-Host ""
    Start-GameIfAsked
    Pause-Unless-Quiet
    exit 0
}

# --- back up, then extract over the install --------------------------------------
$backup = Join-PathSafe $modDir.FullName "CyberpunkMP.dll.backup"
Copy-Item $target $backup -Force
Say "Backed up your old DLL to CyberpunkMP.dll.backup" 'DarkGray'

try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tmp)
    foreach ($entry in $zip.Entries) {
        # Directory entries have empty names; the folder is implied by the path.
        if (-not $entry.Name) { continue }

        $dest = Join-PathSafe $modDir.FullName ($entry.FullName -replace '/', '\')
        $destDir = Split-Path $dest -Parent
        if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }

        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
    }
    $zip.Dispose()
} catch {
    Say "Could not apply the update: $($_.Exception.Message)" 'Red'
    Say "Your previous DLL is still at CyberpunkMP.dll.backup" 'DarkGray'
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    Write-Host ""
    Pause-Unless-Quiet
    exit 1
}

Set-Content -Path $stampFile -Value $stamp -Encoding utf8
Remove-Item $tmp -Force -ErrorAction SilentlyContinue

$final = Get-Item $target
Write-Host ""
Say "Updated - mod, scripts and all. $($final.LastWriteTime)" 'Green'
Write-Host ""
Say "If it crashes, your log is in:" 'DarkGray'
Say "  $($modDir.FullName)\logs\" 'DarkGray'
Say "Post the newest file in the Discord - the whole file, not a screenshot." 'DarkGray'
Write-Host ""
Start-GameIfAsked
Pause-Unless-Quiet
