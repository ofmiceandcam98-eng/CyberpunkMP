# DoctorBuildEnv.ps1 - catches environment rot before it costs a shipping evening.
#
# WHY THIS EXISTS
#
# On 2026-09-10 the test.29 ship took SIX attempts (tags 23-28 burned) because the build
# environment had rotted invisibly while the code was fine:
#
#   - the xmake package cache (%LOCALAPPDATA%\.xmake) had been wiped mid-day, so every
#     C++ unit test failed on missing nlohmann/glm/spdlog headers;
#   - restoring it was a SILENT NO-OP twice, because stale fetch caches still claimed the
#     hollow package dirs were installed;
#   - and the surviving PCHs in build\.objs were baked against the OLD Windows SDK while
#     fresh compiles used the new one - C2011 corecrt redefinitions that survived even a
#     full `xmake f -c` reconfigure, because the .pch lives outside the config.
#
# Every one of those is a one-line check BEFORE a build and a forensic session after one.
# This script is that check. It runs first in Verify, read-only apart from one fingerprint
# file under build\, and every failure says what / where / fix per the repo rule.
#
#   .\tools\DoctorBuildEnv.ps1            # check this machine
#   .\tools\DoctorBuildEnv.ps1 -SelfTest  # exercise the detectors against a temp tree
#
# Exit 0 = environment sound. Exit 1 = at least one finding; details printed.
[CmdletBinding()]
param(
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Detectors - pure functions over paths, so the selftest can aim them at a temp tree.
# ---------------------------------------------------------------------------

# A package install that is a directory skeleton with NO files is a lie xmake will
# happily repeat: its fetch caches keep answering "installed" for a dir the wipe
# emptied, and nothing re-checks the bytes. Emptiness is checked with a first-entry
# enumeration, not a full count - this runs on every Verify.
function Get-HollowPackages {
    param([Parameter(Mandatory)][string]$PackagesRoot)

    $hollow = @()
    if (-not (Test-Path $PackagesRoot)) { return $hollow }

    foreach ($letter in [System.IO.Directory]::EnumerateDirectories($PackagesRoot)) {
        foreach ($pkg in [System.IO.Directory]::EnumerateDirectories($letter)) {
            foreach ($ver in [System.IO.Directory]::EnumerateDirectories($pkg)) {
                $files = [System.IO.Directory]::EnumerateFiles($ver, '*', 'AllDirectories')
                $first = $null
                foreach ($f in $files) { $first = $f; break }
                if (-not $first) {
                    $hollow += (Split-Path $pkg -Leaf) + '/' + (Split-Path $ver -Leaf)
                }
            }
        }
    }
    return $hollow
}

# The environment fingerprint: everything a compiled artifact silently bakes in. If any
# of it changes while build\.objs still holds artifacts from the old world, the next
# build MIXES worlds - the exact C2011 two-SDK failure - so the artifacts must go first.
function Get-EnvFingerprint {
    param(
        [Parameter(Mandatory)][string]$ConfPath,
        [Parameter(Mandatory)][string]$PackagesRoot,
        [Parameter(Mandatory)][string]$SdkIncludeRoot,
        [AllowEmptyCollection()][string[]]$MsvcToolsRoot = @()
    )

    $parts = @()

    if (Test-Path $ConfPath) {
        $conf = Get-Content $ConfPath -Raw
        foreach ($k in 'vs_sdkver', 'vs ', 'mode', 'arch') {
            if ($conf -match "$k\s*=\s*""?([^"",\r\n]+)") { $parts += "$($k.Trim())=$($Matches[1])" }
        }
    }

    foreach ($root in (@($SdkIncludeRoot) + @($MsvcToolsRoot))) {
        if (Test-Path $root) {
            $parts += (Get-ChildItem $root -Directory | ForEach-Object { $_.Name } | Sort-Object) -join ','
        }
    }

    if (Test-Path $PackagesRoot) {
        $dirs = @()
        foreach ($letter in [System.IO.Directory]::EnumerateDirectories($PackagesRoot)) {
            foreach ($pkg in [System.IO.Directory]::EnumerateDirectories($letter)) {
                foreach ($ver in [System.IO.Directory]::EnumerateDirectories($pkg)) {
                    foreach ($hash in [System.IO.Directory]::EnumerateDirectories($ver)) {
                        $dirs += (Split-Path $pkg -Leaf) + '/' + (Split-Path $ver -Leaf) + '/' + (Split-Path $hash -Leaf)
                    }
                }
            }
        }
        $parts += ($dirs | Sort-Object) -join ';'
    }

    $bytes = [System.Text.Encoding]::UTF8.GetBytes(($parts -join "`n"))
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes)) -replace '-', '').ToLower() }
    finally { $sha.Dispose() }
}

# ---------------------------------------------------------------------------
# Selftest - the detectors against a temp tree, per the tests-live-in-the-repo rule.
# ---------------------------------------------------------------------------

if ($SelfTest) {
    $failures = 0
    $t = Join-Path ([System.IO.Path]::GetTempPath()) ("doctor-selftest-" + [guid]::NewGuid().ToString('N').Substring(0, 8))

    try {
        # One hollow package, one real one.
        New-Item -ItemType Directory -Force (Join-Path $t 'packages\g\glm\1.0.3\aaaa') | Out-Null
        New-Item -ItemType Directory -Force (Join-Path $t 'packages\n\nlohmann_json\v3.12.0\bbbb\include') | Out-Null
        Set-Content (Join-Path $t 'packages\n\nlohmann_json\v3.12.0\bbbb\include\json.hpp') 'x'

        # @() re-wrap first: a one-element array returned from a function unwraps to a
        # scalar string, and [0] on a string is its first CHARACTER.
        $hollow = @(Get-HollowPackages -PackagesRoot (Join-Path $t 'packages'))
        if ($hollow.Count -eq 1 -and $hollow[0] -eq 'glm/1.0.3') { Write-Host "  ok    hollow detector finds exactly the empty package" }
        else { Write-Host "  FAIL  hollow detector: expected ['glm/1.0.3'], got [$($hollow -join ', ')]"; $failures++ }

        # Fingerprint changes when the SDK set changes, is stable when nothing moves.
        New-Item -ItemType Directory -Force (Join-Path $t 'sdk\10.0.22621.0'), (Join-Path $t 'msvc\14.44.1') | Out-Null
        Set-Content (Join-Path $t 'xmake.conf') 'vs_sdkver = "10.0.22621.0",'
        $args1 = @{ ConfPath = (Join-Path $t 'xmake.conf'); PackagesRoot = (Join-Path $t 'packages'); SdkIncludeRoot = (Join-Path $t 'sdk'); MsvcToolsRoot = (Join-Path $t 'msvc') }
        $fp1 = Get-EnvFingerprint @args1
        $fp2 = Get-EnvFingerprint @args1
        New-Item -ItemType Directory -Force (Join-Path $t 'sdk\10.0.26100.0') | Out-Null
        $fp3 = Get-EnvFingerprint @args1

        if ($fp1 -eq $fp2) { Write-Host "  ok    fingerprint is stable when nothing changes" }
        else { Write-Host "  FAIL  fingerprint differs across identical runs"; $failures++ }
        if ($fp1 -ne $fp3) { Write-Host "  ok    fingerprint moves when an SDK appears" }
        else { Write-Host "  FAIL  fingerprint blind to a new SDK"; $failures++ }
    }
    finally {
        Remove-Item $t -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Host "doctor selftest: $(3 - $failures)/3 passed"
    exit $(if ($failures) { 1 } else { 0 })
}

# ---------------------------------------------------------------------------
# The checks.
# ---------------------------------------------------------------------------

. (Join-Path $PSScriptRoot "Environment.ps1")

$packagesRoot  = Join-Path $env:LOCALAPPDATA '.xmake\packages'
$confPath      = Join-Path $Repo '.xmake\windows\x64\xmake.conf'
$sdkRoot       = 'C:\Program Files (x86)\Windows Kits\10\include'
# EVERY edition's toolset root, under BOTH Program Files - x86 is where BuildTools
# lands, plain Program Files is the installer default for full VS. Picking one edition
# (or falling back to the VS root, whose children are edition names and never change)
# left the fingerprint blind to a toolset update on the layouts it skipped.
$msvcRoots     = @(Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Tools\MSVC',
                                 'C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\MSVC' `
                                 -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName } | Sort-Object)
$objsDir       = Join-Path $Repo 'build\.objs'
$gensDir       = Join-Path $Repo 'build\.gens'
$fpFile        = Join-Path $Repo 'build\.env-fingerprint'

$findings = 0
function Finding {
    param([string]$Summary, [string]$What, [string]$Where, [string]$Fix)
    $script:findings++
    Write-Host "  FAIL  $Summary" -ForegroundColor Red
    Write-Host "        what   $What" -ForegroundColor DarkYellow
    if ($Where) { Write-Host "        where  $Where" -ForegroundColor DarkYellow }
    Write-Host "        fix    $Fix" -ForegroundColor DarkYellow
}
function Ok($m) { Write-Host "  ok    $m" -ForegroundColor DarkGray }

# 1. The package cache exists at all. Its total absence is what started the 2026-09-10
#    cascade - and plain `xmake require -y` will NOT cure it (stale fetch caches).
if (-not (Test-Path $packagesRoot)) {
    Finding -Summary "the xmake package cache is GONE" `
            -What "every C++ unit test fails on missing vendored headers, and a client build dies mid-compile. Plain 'xmake require -y' is a SILENT NO-OP here - stale fetch caches still claim the packages are installed" `
            -Where $packagesRoot `
            -Fix "delete $(Join-Path $Repo '.xmake\windows\x64\cache\package') if present, then 'xmake require --force -y' the missing packages - and force the PINNED versions separately (mimalloc 2.1.7, flecs v4.0.3, catch2 2.13.9) or bare names pull latest. Full recipe: Atlas branch the-xmake-package-cache-vanished-from-zeldfep-s-box-mid-day"
} else {
    # 2. No hollow packages - dirs the wipe emptied but the caches still vouch for.
    $hollow = Get-HollowPackages -PackagesRoot $packagesRoot
    if ($hollow) {
        Finding -Summary "$(@($hollow).Count) hollow xmake package(s): $(($hollow | Select-Object -First 6) -join ', ')$(if (@($hollow).Count -gt 6) { ', and more' })" `
                -What "each is an empty directory skeleton xmake's caches still report as installed, so builds fail on missing headers while every restore attempt silently no-ops" `
                -Where $packagesRoot `
                -Fix "delete the hollow dirs and $(Join-Path $Repo '.xmake\windows\x64\cache\package'), then 'xmake require --force -y <names>' - pinned versions by their exact strings (mimalloc 2.1.7, flecs v4.0.3, catch2 2.13.9)"
    } else {
        Ok "package cache present, no hollow packages"
    }
}

# 3. The SDK is pinned, and the pin is installed. Unpinned + two installed SDKs is how
#    detection drift starts; a pin that points at a missing SDK fails immediately instead.
$pinned = $null
if (Test-Path $confPath) {
    $conf = Get-Content $confPath -Raw
    if ($conf -match 'vs_sdkver\s*=\s*"([^"]+)"') { $pinned = $Matches[1] }

    if (-not $pinned) {
        $installed = @(Get-ChildItem $sdkRoot -Directory -ErrorAction SilentlyContinue)
        if ($installed.Count -gt 1) {
            Finding -Summary "no vs_sdkver pin with $($installed.Count) Windows SDKs installed" `
                    -What "toolchain detection picks an SDK per detect-cache lifetime; after any cache reset it can pick a DIFFERENT one than the artifacts in build\.objs were baked with - two SDKs on one compile line, C2011 corecrt redefinitions" `
                    -Where $confPath `
                    -Fix "pin it: xmake f --vs_sdkver=$(($installed | Sort-Object Name | Select-Object -Last 1).Name) --game=""<game exe path>"" (a partial 'xmake f' RESETS --game to its default - always re-pass it)"
        } else {
            Ok "single SDK installed; pin optional"
        }
    } elseif (-not (Test-Path (Join-Path $sdkRoot $pinned))) {
        Finding -Summary "pinned SDK $pinned is not installed" `
                -What "every compile fails at the first system header" `
                -Where (Join-Path $sdkRoot $pinned) `
                -Fix "install that SDK, or re-pin to an installed one: xmake f --vs_sdkver=<ver> --game=""<game exe path>"""
    } else {
        Ok "SDK pinned to $pinned and installed"
    }

    # 4. The game path survived the last reconfigure ('xmake f' with partial flags resets it).
    if ($conf -match 'game\s*=\s*(?:\[\[)?([^\]",\r\n]+\.exe)') {
        if (Test-Path $Matches[1]) { Ok "game path set and exists" }
        else {
            Finding -Summary "configured game path does not exist" `
                    -What "the redscript compile check and the mod build both need it; CheckScripts will SKIP and Verify loses its script gate" `
                    -Where $Matches[1] `
                    -Fix "xmake f --vs_sdkver=$pinned --game=""<real Cyberpunk2077.exe path>"""
        }
    } else {
        Ok "no game path configured (fine on a box without the game - CheckScripts skips loudly)"
    }
} else {
    Ok "no xmake project config yet (first build will create it)"
}

# 5. The build tree matches the environment it was compiled under. PCHs bake the SDK's
#    headers in; artifacts from an older environment under a newer one mix worlds, and
#    NO reconfigure clears them - only deleting them does.
$current = Get-EnvFingerprint -ConfPath $confPath -PackagesRoot $packagesRoot -SdkIncludeRoot $sdkRoot -MsvcToolsRoot $msvcRoots
if (Test-Path $objsDir) {
    $stored = if (Test-Path $fpFile) { (Get-Content $fpFile -Raw).Trim() } else { $null }
    if ($stored -and $stored -ne $current) {
        Finding -Summary "the environment changed under an existing build tree" `
                -What "build\.objs holds artifacts (PCHs included) baked against the OLD environment; the next build mixes old and new - the C2011 two-SDK corecrt failure that survived even 'xmake f -c' on 2026-09-10" `
                -Where $objsDir `
                -Fix "Remove-Item '$objsDir','$gensDir' -Recurse -Force  - then rebuild clean; this file re-baselines automatically"
    } elseif ($stored) {
        Ok "build tree consistent with the current environment"
        if ($findings -eq 0) { New-Item -ItemType Directory -Force (Split-Path $fpFile) | Out-Null; Set-Content $fpFile $current -NoNewline }
    } else {
        # First run over a PRE-EXISTING build tree: no baseline exists, so consistency
        # is UNKNOWN - claiming "consistent" here is exactly how the 2026-09-10 poisoned
        # tree would have sailed through its first doctor run. Say so, baseline anyway
        # (the check has to start somewhere), and leave the fix on screen.
        Write-Host "  warn  existing build tree but no baseline yet - cannot vouch for artifacts already in build\.objs" -ForegroundColor DarkYellow
        Write-Host "        if the next build fails C2011/corecrt or PCH-shaped: Remove-Item '$objsDir','$gensDir' -Recurse -Force and rebuild clean" -ForegroundColor DarkYellow
        if ($findings -eq 0) { New-Item -ItemType Directory -Force (Split-Path $fpFile) | Out-Null; Set-Content $fpFile $current -NoNewline }
    }
} else {
    # No artifacts to poison - record the baseline for the build about to happen.
    if ($findings -eq 0) { New-Item -ItemType Directory -Force (Split-Path $fpFile) | Out-Null; Set-Content $fpFile $current -NoNewline }
    Ok "no build artifacts yet; environment baseline recorded"
}

exit $(if ($findings) { 1 } else { 0 })
