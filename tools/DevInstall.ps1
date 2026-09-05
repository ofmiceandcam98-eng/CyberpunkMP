<#
    DevInstall.ps1 - put what you just built straight into your own game.

    This is the LIVE UPDATE path for a development machine. Build, run this, launch. It
    copies the freshly built DLL and every .reds from source into your game's mod folder
    and then proves both arrived.

        .\tools\DevInstall.ps1            # install what is already built
        .\tools\DevInstall.ps1 -Build     # build first, then install
        .\tools\DevInstall.ps1 -WhatIf    # say what it would do

    WHY THIS EXISTS, AND WHY IT COPIES RATHER THAN SYMLINKS

    Cam's game was set up with the mod DLL SYMLINKED into build\windows\x64\release, so a
    rebuild reached the game with no install step. On 2026-09-04 that quietly killed the
    whole mod: the main menu came up completely stock, no multiplayer entries, nothing
    crashed, nothing said why.

    The mechanism is worth writing down, because it will look like something else every
    time it happens. RED4ext loads the plugin, and the plugin resolves its OWN path to find
    assets/redscript beside itself. Through a symlink, "beside itself" is the LINK TARGET -
    the build tree - not the game folder. So the plugin threw during Load:

        canonical: The system cannot find the file specified.:
          "...\build\windows\x64\release\assets/redscript"
        CyberpunkMP did not initialize properly, unloading...

    and RED4ext unloaded it. The plugin is what registers the mod's scripts with redscript,
    so all 48 .reds sat on disk and never compiled, and the game started perfectly normally
    with none of the mod in it. A dead mod that leaves a working game is the nastiest shape
    a failure can take: there is no crash to investigate and the menu looks like a revert.

    It broke because code\server\admin\xmake.lua removes and recreates
    build\windows\x64\release\assets on EVERY build to stage the admin panel, and that is
    the same directory name the mod's assets used. The admin target wins; the mod's assets
    link got displaced to "assets$D" and the plugin had nowhere to look.

    So this copies. A real file in the game folder resolves its assets in the game folder,
    which is also exactly what every tester's install looks like - the failure above could
    only ever happen to a developer, and a dev setup that fails in ways nobody else can
    reproduce is not worth the seconds it saves.
#>
[CmdletBinding()]
param(
    [string]$GameDir,

    # Run xmake first. Without it, this installs whatever is already in the build output.
    [switch]$Build,

    [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot "Environment.ps1")

if ($GameDir) { $script:GameDir = $GameDir }
Assert-GameDir
$GameDir = $script:GameDir

function Step($m) { Write-Host "`n=== $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "  OK  $m" -ForegroundColor Green }
function Die($m)  { Write-Host "  FAIL  $m" -ForegroundColor Red; exit 1 }

# ---------------------------------------------------------------------------
# A running game holds the DLL open, and a copy onto a locked file is how the build
# output got truncated in the first place. Refuse rather than half-write.
$proc = Get-Process -Name "Cyberpunk2077" -ErrorAction SilentlyContinue
if ($proc) { Die "Cyberpunk 2077 is running (pid $($proc.Id)). Close it first - the DLL cannot be replaced while the game holds it." }

# ---------------------------------------------------------------------------
Step "Mod folder"

# zzzCyberpunkMP is the current name - the launcher moved to it so the mod sorts last and
# loads after Codeware/ArchiveXL/TweakXL. The old name is still accepted so an install
# made before that change keeps working.
$candidates = @(
    (Join-Path $GameDir "red4ext\plugins\zzzCyberpunkMP"),
    (Join-Path $GameDir "red4ext\plugins\CyberpunkMP")
)

$modDir = $null
foreach ($c in $candidates) { if (Test-Path $c) { $modDir = $c; break } }

if (-not $modDir) {
    Die "no mod folder in $GameDir\red4ext\plugins. Install the mod once through the Night City Online launcher, then this can keep it current."
}

Ok "using $modDir"

# ---------------------------------------------------------------------------
if ($Build) {
    Step "Build"
    if ($WhatIf) {
        Write-Host "  would run: xmake build" -ForegroundColor Yellow
    }
    else {
        Assert-XMake
        Push-Location $Repo
        try {
            & xmake build
            if ($LASTEXITCODE -ne 0) { Die "xmake build failed - nothing installed" }
        }
        finally { Pop-Location }
        Ok "built"
    }
}

# ---------------------------------------------------------------------------
Step "DLL"

$builtDll = Join-Path $Repo "build\windows\x64\release\CyberpunkMP.dll"
if (-not (Test-Path $builtDll)) { Die "no built DLL at $builtDll - run with -Build first" }

# Length through the link, not the link's own metadata. PowerShell reports a reparse point
# as 0 bytes, which reads exactly like a truncated file and sent this investigation down a
# wrong path once already.
$stream = [System.IO.File]::OpenRead($builtDll)
$builtSize = $stream.Length
$stream.Close()

if ($builtSize -lt 1MB) { Die "the built DLL is only $builtSize bytes - that is a failed link step, not a build. Rebuild before installing." }

$destDll = Join-Path $modDir "CyberpunkMP.dll"

if ($WhatIf) {
    Write-Host "  would copy $builtSize bytes -> $destDll" -ForegroundColor Yellow
}
else {
    # Remove FIRST if it is a link. Copying onto a symlink writes through it into the link
    # target - which here is the build output, so the copy would appear to succeed while
    # the game folder still had no real DLL.
    if (Test-Path $destDll) {
        $existing = Get-Item $destDll -Force
        if (($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            Write-Host "  replacing a symlink that pointed at $($existing.Target)" -ForegroundColor DarkGray
        }
        Remove-Item -LiteralPath $destDll -Force
    }

    Copy-Item -LiteralPath $builtDll -Destination $destDll -Force
    Ok "DLL copied ($([math]::Round($builtSize/1MB,1)) MB)"
}

# ---------------------------------------------------------------------------
Step "Redscript"

# From SOURCE, not from distrib. distrib is a staging copy that only ShipTestBuild
# refreshes, so installing from it on a dev machine ships whatever the last test build
# happened to contain - which is the stale-scripts trap this repo has already been bitten
# by once ("xmake install reports success and leaves them stale").
$srcReds = Join-Path $Repo "code\assets\redscript"
if (-not (Test-Path $srcReds)) { Die "no redscript source at $srcReds" }

$destReds = Join-Path $modDir "assets\redscript"

$srcFiles = Get-ChildItem $srcReds -Recurse -Filter *.reds

if ($WhatIf) {
    Write-Host "  would mirror $($srcFiles.Count) .reds -> $destReds" -ForegroundColor Yellow
}
else {
    # DELETE the destination first, so a file removed from source cannot linger. All 45+
    # files compile as ONE unit, so a stale leftover does not merely do nothing - it can
    # reference something that no longer exists and abort the whole compilation, taking
    # every script in the mod down with it.
    if (Test-Path $destReds) { Remove-Item -LiteralPath $destReds -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $destReds | Out-Null

    Copy-Item -Path (Join-Path $srcReds "*") -Destination $destReds -Recurse -Force

    $installed = (Get-ChildItem $destReds -Recurse -Filter *.reds).Count
    if ($installed -ne $srcFiles.Count) {
        Die "copied $installed .reds but source has $($srcFiles.Count) - the install is incomplete, do not launch"
    }

    Ok "$installed .reds mirrored from source"
}

# ---------------------------------------------------------------------------
Step "Rpc bindings"

# CHECKED, NOT COPIED - deliberately.
#
# ShipTestBuild bundles Rpc alongside the DLL and redscript, so these do reach testers and
# they can go stale here. But the build output's copy is NOT the good one: RedTypes.cs is a
# 218-byte stub there against 2366 bytes in the staged payload, so this file is generated in
# more than one step and installing the build tree's version would quietly downgrade it.
#
# Rather than guess at a generation pipeline this script does not understand, it compares
# and says so. These only change when somebody adds or changes an RPC, which is rare enough
# that a warning is cheaper than a wrong copy - and a wrong copy here would be another
# silent breakage of exactly the kind this whole script exists to stop.
$stagedRpc = Join-Path $Repo "distrib\launcher\mod\Rpc"
$destRpc = Join-Path $modDir "Rpc"

if ((Test-Path $stagedRpc) -and (Test-Path $destRpc)) {
    $drift = @()
    foreach ($sf in Get-ChildItem $stagedRpc -Recurse -File) {
        $rel = $sf.FullName.Substring($stagedRpc.Length).TrimStart('\')
        $df = Join-Path $destRpc $rel
        if (-not (Test-Path $df)) { $drift += "$rel (missing in game)"; continue }
        if ((Get-Item $df).Length -ne $sf.Length) { $drift += "$rel (size differs)" }
    }

    if ($drift.Count -eq 0) {
        Ok "in sync with the staged payload"
    }
    else {
        Write-Host "  !! Rpc bindings differ from distrib\launcher\mod\Rpc:" -ForegroundColor Yellow
        $drift | ForEach-Object { Write-Host "       $_" -ForegroundColor Yellow }
        Write-Host "     Run tools\ShipTestBuild.ps1 to regenerate them, then re-run this." -ForegroundColor Yellow
    }
}
else {
    Write-Host "  (no Rpc folder to compare)" -ForegroundColor DarkGray
}

if ($WhatIf) { Write-Host "`n(nothing was written)" -ForegroundColor Yellow; exit 0 }

# ---------------------------------------------------------------------------
Step "Verify"

# The three things that were silently wrong last time, each checked rather than assumed.
$f = Get-Item $destDll -Force
if (($f.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    Die "the installed DLL is still a symlink - it will resolve its assets outside the game folder and the mod will not load"
}

$s = [System.IO.File]::OpenRead($destDll); $liveSize = $s.Length; $s.Close()
if ($liveSize -ne $builtSize) { Die "installed DLL is $liveSize bytes but the build is $builtSize - the copy did not land" }

if (-not (Test-Path (Join-Path $modDir "assets\redscript"))) {
    Die "assets\redscript is missing beside the DLL - this is the exact condition that makes the plugin unload on Load"
}

Ok "real DLL, $([math]::Round($liveSize/1MB,1)) MB, with assets\redscript beside it"

Write-Host "`nInstalled. Launch through the Night City Online launcher." -ForegroundColor Green
Write-Host "If the main menu comes up stock, read red4ext\logs\red4ext-*.log - a plugin that" -ForegroundColor DarkGray
Write-Host "fails during Load unloads itself and takes every mod script with it, silently." -ForegroundColor DarkGray
