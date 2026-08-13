<#
    Ship.ps1 - build, package and publish in one command.

    Replaces the build -> package -> upload -> verify sequence that was being done by
    hand every time, which is where most of the wasted effort went: three or four
    round trips for a one-line change, and a real chance of publishing something that
    was never checked.

    Usage:
        .\Ship.ps1                 # everything that changed
        .\Ship.ps1 -Launcher       # launcher only
        .\Ship.ps1 -Mod            # client mod only
        .\Ship.ps1 -Server         # server only, no publish
        .\Ship.ps1 -WhatIf         # say what it would do, change nothing

    It refuses to publish anything that failed a check. A broken build reaching the
    release is worse than no build.
#>

[CmdletBinding()]
param(
    [switch]$Launcher,
    [switch]$Mod,
    [switch]$Server,
    [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'

$Repo      = "C:\Users\Cam\OneDrive\Documents\GitHub\CyberpunkMP"
$LauncherDir = Join-Path $Repo "code\launcher-lite"
$XMake     = "C:\Users\Cam\scoop\shims\xmake.exe"
$Tag       = "test-2026.08.12-probes"
$GhRepo    = "ofmiceandcam98-eng/CyberpunkMP"

# Nothing selected means everything.
$all = -not ($Launcher -or $Mod -or $Server)
if ($all) { $Launcher = $true; $Mod = $true }

function Step  { param($T) Write-Host "`n=== $T" -ForegroundColor Cyan }
function Ok    { param($T) Write-Host "  OK  $T" -ForegroundColor Green }
function Warn  { param($T) Write-Host "  !!  $T" -ForegroundColor Yellow }
function Die   { param($T) Write-Host "`nSTOPPED: $T" -ForegroundColor Red; exit 1 }

Set-Location $Repo

# ---------------------------------------------------------------------------
# Pre-flight. Cheap checks that catch the mistakes actually made before.
# ---------------------------------------------------------------------------

Step "Pre-flight"

# Publishing a credential would be unrecoverable - the file is public the moment it
# uploads. Worth the two seconds.
foreach ($secret in @("tools\.discord-bot", "tools\.discord-webhook", ".env")) {
    if (Test-Path $secret) {
        $ignored = git check-ignore $secret 2>$null
        if (-not $ignored) { Die "$secret exists and is NOT gitignored." }
    }
}
Ok "secrets are ignored"

if ($Server) {
    if (Get-Process -Name "Server.Loader" -ErrorAction SilentlyContinue) {
        Die "The server is running - close it, or the DLL cannot be relinked."
    }
    Ok "server not running"
}

if ($Launcher) {
    Get-Process NightCityOnline* -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    Ok "launcher closed"
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

if ($Mod) {
    Step "Client mod"
    if ($WhatIf) { Warn "would build + install Client" }
    else {
        & $XMake build -j 4 Client 2>&1 | Select-Object -Last 3
        if ($LASTEXITCODE -ne 0) { Die "client build failed" }
        & $XMake install -o distrib Client 2>&1 | Out-Null
        $dll = Get-Item "distrib\launcher\mod\CyberpunkMP.dll"
        Ok "built $([math]::Round($dll.Length/1MB,2)) MB at $($dll.LastWriteTime.ToString('HH:mm:ss'))"

        # Force the scripts across. `xmake install` refreshes the DLL but was caught
        # leaving edited .reds files behind at their previous contents, which is the worst
        # possible failure: the build succeeds, the version number moves, the DLL is new,
        # and the script half of the mod is silently whatever it was last time. That looks
        # exactly like a redscript change that "did not work" and sends you debugging code
        # the game never received. Copying is cheap; guessing is not.
        Copy-Item "code\assets\redscript\*" -Destination "distrib\launcher\mod\assets\redscript\" -Recurse -Force

        $stale = @()
        Get-ChildItem "code\assets\redscript" -Recurse -Filter *.reds | ForEach-Object {
            $relative = $_.FullName.Substring((Resolve-Path "code\assets\redscript").Path.Length + 1)
            $shipped = "distrib\launcher\mod\assets\redscript\$relative"
            if (-not (Test-Path $shipped)) { $stale += "$relative (missing)" }
            elseif ((Get-FileHash $_.FullName).Hash -ne (Get-FileHash $shipped).Hash) { $stale += $relative }
        }
        if ($stale.Count -gt 0) { Die "scripts did not ship: $($stale -join ', ')" }
        Ok "scripts match source"
    }
}

if ($Server) {
    Step "Server"
    if ($WhatIf) { Warn "would build Server.Loader" }
    else {
        & $XMake build -j 4 Server.Loader 2>&1 | Select-Object -Last 3
        if ($LASTEXITCODE -ne 0) { Die "server build failed" }
        Ok "server built"
    }
}

if ($Launcher) {
    Step "Launcher"

    Push-Location $LauncherDir

    # Syntax-check BEFORE packaging. electron-builder happily packages a file with a
    # syntax error, and you only find out when the window comes up blank.
    foreach ($file in @("main.js", "preload.mjs")) {
        node --check $file 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { Pop-Location; Die "$file has a syntax error" }
    }
    Ok "javascript parses"

    # Every id the script reaches for must exist in the markup. Getting this wrong
    # produces a dead button and no error at all.
    $html = Get-Content "index.html" -Raw
    $missing = @()
    foreach ($m in [regex]::Matches($html, "\`$\('([A-Za-z0-9_]+)'\)")) {
        $id = $m.Groups[1].Value
        if ($html -notmatch "id=`"$id`"") { $missing += $id }
    }
    if ($missing.Count -gt 0) { Pop-Location; Die "script references missing ids: $($missing -join ', ')" }
    Ok "every referenced element exists"

    $open  = ([regex]::Matches($html, '<div')).Count
    $close = ([regex]::Matches($html, '</div>')).Count
    if ($open -ne $close) { Pop-Location; Die "unbalanced divs: $open open, $close close" }
    Ok "markup balanced"

    if ($WhatIf) { Warn "would package launcher" }
    else {
        pnpm run dist 2>&1 | Select-Object -Last 2
        if ($LASTEXITCODE -ne 0) { Pop-Location; Die "packaging failed" }
        Ok "packaged"
    }

    Pop-Location
}

# ---------------------------------------------------------------------------
# Publish
# ---------------------------------------------------------------------------

if ($WhatIf) { Write-Host "`n(dry run - nothing published)" -ForegroundColor Yellow; exit 0 }

Step "Publish"

$uploads = @()

if ($Mod) {
    # Full payload, so redscript fixes reach people too - shipping only the DLL meant
    # script changes silently never arrived.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stage = Join-Path $env:TEMP ("ship_" + (Get-Date -Format 'HHmmss'))
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    Copy-Item "distrib\launcher\mod\CyberpunkMP.dll" $stage
    Copy-Item "distrib\launcher\mod\assets" (Join-Path $stage "assets") -Recurse
    Copy-Item "distrib\launcher\mod\Rpc"    (Join-Path $stage "Rpc")    -Recurse

    $payload = Join-Path $env:TEMP "ModPayload.zip"
    if (Test-Path $payload) { Remove-Item -LiteralPath $payload -Force }
    [System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $payload,
        [System.IO.Compression.CompressionLevel]::Optimal, $false)

    $uploads += $payload
    $uploads += (Join-Path $Repo "distrib\launcher\mod\CyberpunkMP.dll")
    Ok "mod payload staged"
}

if ($Launcher) {
    $setup = Join-Path $env:TEMP "NightCityOnline-Setup.exe"
    Copy-Item (Join-Path $LauncherDir "dist\NightCityOnline Setup 0.1.0.exe") $setup -Force
    $uploads += $setup
    $uploads += (Join-Path $LauncherDir "dist\NightCityOnline-Launcher.exe")
    Ok "launcher staged"
}

if ($uploads.Count -eq 0) { Warn "nothing to publish"; exit 0 }

gh release upload $Tag @uploads --repo $GhRepo --clobber 2>&1 | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { Die "upload failed" }

# Confirm what is actually on the release, rather than assuming the upload worked.
Step "Verify"
$release = gh api "repos/$GhRepo/releases/tags/$Tag" | ConvertFrom-Json
foreach ($asset in $release.assets) {
    "  {0,-34} {1,7} MB  {2}" -f $asset.name, [math]::Round($asset.size/1MB,1), $asset.updated_at
}

Write-Host "`nShipped." -ForegroundColor Green
