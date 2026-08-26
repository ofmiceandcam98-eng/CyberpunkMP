# Collect everything needed to diagnose one crash, into a single zip.
#
# Run it IMMEDIATELY after a crash and BEFORE relaunching. RED4ext rotates its logs and
# the mod truncates its own on start, so a second launch destroys the evidence from the
# first - which is exactly how three real launcher crashes ended up with no readable trace.
#
#   .\tools\CollectCrash.ps1
#
[CmdletBinding()]
param(
    [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077',
    [string]$OutDir  = "$env:USERPROFILE\Desktop"
)

$ErrorActionPreference = 'Continue'

$stamp   = Get-Date -Format 'yyyyMMdd-HHmmss'
$staging = Join-Path $env:TEMP "nco-crash-$stamp"
New-Item -ItemType Directory -Force -Path $staging | Out-Null

function Grab {
    param([string]$Path, [string]$As, [int]$Newest = 3)
    if (-not (Test-Path $Path)) { return }
    $dest = Join-Path $staging $As
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Get-ChildItem $Path -File -EA SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First $Newest |
        ForEach-Object { Copy-Item $_.FullName -Destination $dest -Force -EA SilentlyContinue }
}

# The mod's own log is the one that says how far it got before dying.
Grab "$GameDir\red4ext\plugins\zzzCyberpunkMP\logs" 'mod'          4
Grab "$GameDir\red4ext\logs"                        'red4ext'      4
Grab "$GameDir\r6\logs"                             'redscript'    2
Grab "$GameDir\red4ext\plugins\ArchiveXL"           'archivexl'    1
Grab "$GameDir\bin\x64\plugins\cyber_engine_tweaks" 'cet'          2

# The game's own post-mortem: district, quest, session length, out-of-memory flag.
$ci = "$env:LOCALAPPDATA\CD Projekt Red\Cyberpunk 2077\CrashInfo.json"
if (Test-Path $ci) { Copy-Item $ci -Destination (Join-Path $staging 'CrashInfo.json') -Force }

# Which server it was pointed at, and the exit code the launcher saw. NOTE: settings.json
# is deliberately NOT collected - it holds the Discord auth token.
$trail = "$env:APPDATA\Night City Online\launcher-trail.log"
if (Test-Path $trail) {
    Get-Content $trail -Tail 120 | Set-Content (Join-Path $staging 'launcher-trail-tail.log') -Encoding utf8
}

# Was this a process crash or did the whole machine go down? Different problems entirely.
$since = (Get-Date).AddHours(-6)
$sys = Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=$since} -EA SilentlyContinue |
       Where-Object { $_.Id -in 41,1001,4101 }
if ($sys) {
    $sys | Select-Object TimeCreated, Id, ProviderName, Message |
        Format-List | Out-String |
        Set-Content (Join-Path $staging 'system-events.txt') -Encoding utf8
} else {
    'No Kernel-Power 41 / bugcheck / display-reset events in the last 6 hours.' |
        Set-Content (Join-Path $staging 'system-events.txt') -Encoding utf8
}

# Installed versions, so nobody has to ask what you were running.
$verFile = Join-Path $staging 'versions.txt'
$lines = @()
$nco = "$GameDir\red4ext\plugins\zzzCyberpunkMP\.nco-version"
$lines += "mod tag      : " + $(if (Test-Path $nco) { (Get-Content $nco).Trim() } else { 'absent' })
$dll = "$GameDir\red4ext\plugins\zzzCyberpunkMP\CyberpunkMP.dll"
if (Test-Path $dll) {
    $lines += "mod dll      : $((Get-FileHash $dll -Algorithm SHA256).Hash.Substring(0,32))  $((Get-Item $dll).LastWriteTime)"
}
$exe = "$GameDir\bin\x64\Cyberpunk2077.exe"
if (Test-Path $exe) {
    $vi = (Get-Item $exe).VersionInfo
    $lines += "game         : product $($vi.ProductVersion)  build $($vi.FileVersionRaw)"
}
Get-CimInstance Win32_VideoController -EA SilentlyContinue |
    ForEach-Object { $lines += "gpu          : $($_.Name)  driver $($_.DriverVersion)  ($($_.DriverDate))" }
$lines | Set-Content $verFile -Encoding utf8

$zip = Join-Path $OutDir "nco-crash-$stamp.zip"
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip -Force

Remove-Item $staging -Recurse -Force -EA SilentlyContinue

Write-Host ""
Write-Host "Collected: $zip" -ForegroundColor Green
Write-Host "No credentials are included (settings.json is deliberately skipped)." -ForegroundColor DarkGray
Write-Host ""
