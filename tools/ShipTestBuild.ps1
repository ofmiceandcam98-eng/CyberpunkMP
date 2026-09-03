<#
    ShipTestBuild.ps1 - publish a pre-release the dev panel can install.

    A test build is NOT a release. It is a GitHub pre-release, which means:
      - players never see it (their launchers only auto-update from full releases)
      - it does not move the version, write release notes, or announce anything
      - only people with the dev role can install it, from Settings > DEV > Test builds

    Use it to put something risky in front of one or two people before it reaches
    everybody. That is the whole point: the selector changes the main menu flow, and the
    main menu is the one screen where a mistake means nobody can play at all.

    THE PAYLOAD, NOT JUST THE DLL

    Test builds used to be a bare CyberpunkMP.dll, which works only while the change under
    test is pure C++. Half this mod is redscript - menus, seat transitions, appearance -
    and a DLL-only build delivers none of it while reporting success. So this uploads
    ModPayload.zip (DLL + redscript + Rpc together) as well as the DLL, and the launcher
    prefers the payload. The DLL is still uploaded because the launcher's Test builds list
    only shows builds that have one.

    Usage:
        .\tools\ShipTestBuild.ps1 -Name "character selector"
        .\tools\ShipTestBuild.ps1 -Name "character selector" -WhatIf
#>

[CmdletBinding()]
param(
    # Shown in the dev panel next to the tag. Say what to LOOK for, not what changed
    # internally - "remote players MOVE" is the right shape.
    [Parameter(Mandatory = $true)]
    [string]$Name,

    # Override the tag. Defaults to <current version>-worldstate-test.<next number>.
    [string]$Tag,

    # Skip the Verify.ps1 gate. An escape hatch, not a habit - it exists so a genuine false
    # positive cannot block a ship at midnight, and every use of it is a bug in Verify that
    # should be fixed rather than routed around.
    [switch]$SkipVerify,

    [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot "Environment.ps1")

function Step($m) { Write-Host "`n=== $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "  OK  $m" -ForegroundColor Green }
function Warn($m) { Write-Host "  !!  $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host "`nSTOPPED: $m" -ForegroundColor Red; exit 1 }

Set-Location $Repo

# ---------------------------------------------------------------------------
# Tag
# ---------------------------------------------------------------------------

$version = (Get-Content (Join-Path $LauncherDir "package.json") -Raw | ConvertFrom-Json).version

if (-not $Tag) {
    # Next number in the sequence, read from what is actually published rather than
    # guessed - two test builds sharing a tag is a silent overwrite.
    # Asked of GitHub rather than of a remote name: $GhRepo is "owner/name", which
    # ls-remote does not accept, and which remote points where varies by checkout.
    $existing = & gh release list --repo $GhRepo --limit 60 2>$null

    $highest = 0
    foreach ($line in ($existing -split "`n")) {
        if ($line -match 'worldstate-test\.(\d+)') {
            $n = [int]$Matches[1]
            if ($n -gt $highest) { $highest = $n }
        }
    }

    $Tag = "v$version-worldstate-test.$($highest + 1)"
}

Step "Test build"
Write-Host "  tag  : $Tag"
Write-Host "  name : $Name"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

Step "Client mod"

# Redscript first. A test build whose scripts do not compile takes the game down entirely
# for whoever installs it - one bad file aborts ALL compilation and the game starts with
# no scripts at all, which looks exactly like the mod doing nothing.
& (Join-Path $PSScriptRoot "CheckScripts.ps1") | Out-Null
if ($LASTEXITCODE -ne 0) { Die "redscript does not compile - not publishing" }
Ok "redscript compiles"

# Then everything a compiler cannot see. Cam's rule, 2026-09-03: run this before shipping
# anything.
#
# GATED RATHER THAN REMEMBERED, because remembering is what failed. /call shipped as dead
# code - two dispatches, the older one matching first and returning - and it compiled
# perfectly, was reported as working, and would have gone out. Verify catches that class:
# duplicate dispatch, natives with no RTTI behind them (which fail at LOAD and take every
# script down), unhandled requests, BOMs, and the unit tests.
if ($SkipVerify) {
    Warn "verification SKIPPED by -SkipVerify"
} else {
    & (Join-Path $PSScriptRoot "Verify.ps1") | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        & (Join-Path $PSScriptRoot "Verify.ps1")   # re-run visibly, so the failure is readable
        Die "verification failed - not publishing. Fix it, or pass -SkipVerify if you are certain"
    }
    Ok "verified"
}

if ($WhatIf) {
    Warn "would build and install Client"
} else {
    & $XMake build Client
    if ($LASTEXITCODE -ne 0) { Die "client build failed" }

    & $XMake install -o distrib Client
    if ($LASTEXITCODE -ne 0) { Die "client install failed" }
    Ok "client built and installed to distrib"
}

# ---------------------------------------------------------------------------
# Payload
# ---------------------------------------------------------------------------

Step "Payload"

$modDir = Join-Path $Repo "distrib\launcher\mod"
$dll    = Join-Path $modDir "CyberpunkMP.dll"

if (-not $WhatIf) {
    if (-not (Test-Path $dll)) { Die "no CyberpunkMP.dll at $dll" }

    # The scripts that go INTO the payload come from source, force-copied, because
    # xmake install has been observed leaving edited .reds at their previous contents -
    # a build that succeeds with a silently stale script half.
    $assetsDst = Join-Path $modDir "assets\redscript"
    New-Item -ItemType Directory -Force -Path $assetsDst | Out-Null
    Copy-Item (Join-Path $Repo "code\assets\redscript\*") $assetsDst -Recurse -Force
    Ok "redscript force-copied from source"

    $stage = Join-Path $env:TEMP "nco-testbuild"
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null

    Copy-Item $dll $stage
    Copy-Item (Join-Path $modDir "assets") (Join-Path $stage "assets") -Recurse
    Copy-Item (Join-Path $modDir "Rpc")    (Join-Path $stage "Rpc")    -Recurse

    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $payload = Join-Path $env:TEMP "ModPayload.zip"
    if (Test-Path $payload) { Remove-Item -LiteralPath $payload -Force }

    [System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $payload,
        [System.IO.Compression.CompressionLevel]::Optimal, $false)

    $size = [math]::Round((Get-Item $payload).Length / 1MB, 1)
    Ok "ModPayload.zip staged ($size MB)"
} else {
    Warn "would stage ModPayload.zip"
}

# ---------------------------------------------------------------------------
# Publish
# ---------------------------------------------------------------------------

Step "Publish"

if ($WhatIf) {
    Warn "would publish pre-release $Tag"
    Write-Host "`n(dry run - nothing published)" -ForegroundColor Yellow
    exit 0
}

$notes = @"
Test build - **not a release**. Players never receive this; it only appears under
Settings > DEV > Test builds for people with the dev role.

$Name

Install swaps in this build's mod payload (DLL, redscript and Rpc together) and keeps
the shipped one. Restore puts the current release back.
"@

# The build NUMBER goes in the title, not just the tag.
#
# The launcher's Test builds list shows only the title, so a build called
# "appearance rollback + commit fallbacks" was impossible to match against a
# conversation about "test.18" - Cam had to ask which row to click. The tag was always
# right there in the release; it just never reached the one screen where the choice is
# actually made.
$shortNum = if ($Tag -match 'test\.(\d+)') { $Matches[1] } else { '?' }
$title = "test.$shortNum - $Name"

& gh release create $Tag --repo $GhRepo --prerelease --title $title --notes $notes $payload $dll
if ($LASTEXITCODE -ne 0) { Die "publishing failed" }

Ok "published $Tag"
Write-Host "`nInstall it from Settings > DEV > Test builds." -ForegroundColor Green
