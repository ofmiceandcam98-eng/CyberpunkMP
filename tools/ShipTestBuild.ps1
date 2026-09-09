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
    # Shown in the dev panel next to the tag, and it is the ONLY thing shown there. Say what
    # to LOOK for, not what changed internally - "remote players MOVE" is the right shape,
    # "worldstate" is not. Four builds shipped as "test.N - worldstate" before anybody could
    # tell them apart; there is a warning below that catches that shape now.
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
    # THE SEQUENCE MUST NEVER GO BACKWARDS, and reading only what is published lets it.
    #
    # This asked GitHub for the highest published number and added one. Correct while builds
    # accumulate, and wrong the moment any are deleted: on 2026-09-08 a cleanup removed every
    # prerelease, so the next ship found none and restarted at 1. The sequence went test.29,
    # test.30, test.1 - and two different builds can now share a name, which makes "which
    # build was that" unanswerable for anything before that point.
    #
    # So the number is the MAXIMUM of three sources, and the counter is written back. Any one
    # of them surviving is enough to stop the sequence rewinding:
    #
    #   published releases   authoritative while they exist, and the only cross-machine source
    #   local git tags       survive a release being deleted on GitHub
    #   a counter file       survives both, and is machine-local (gitignored) rather than
    #                        committed, because a tracked counter is a merge conflict on
    #                        every parallel ship
    $highest = 0

    $existing = & gh release list --repo $GhRepo --limit 100 2>$null
    foreach ($line in ($existing -split "`n")) {
        if ($line -match 'worldstate-test\.(\d+)') {
            $n = [int]$Matches[1]
            if ($n -gt $highest) { $highest = $n }
        }
    }

    foreach ($line in (& git tag --list "*worldstate-test*" 2>$null)) {
        if ($line -match 'worldstate-test\.(\d+)') {
            $n = [int]$Matches[1]
            if ($n -gt $highest) { $highest = $n }
        }
    }

    $counterFile = Join-Path $PSScriptRoot ".test-build-counter"
    if (Test-Path $counterFile) {
        $saved = 0
        if ([int]::TryParse((Get-Content $counterFile -Raw).Trim(), [ref]$saved)) {
            if ($saved -gt $highest) { $highest = $saved }
        }
    }

    $Tag = "v$version-worldstate-test.$($highest + 1)"

    # Written before the build, not after: a ship that dies half way must still burn its
    # number, or the next attempt reuses it and clobbers whatever the first one managed to
    # publish.
    Set-Content -Path $counterFile -Value ($highest + 1) -Encoding ascii
}

Step "Test build"
Write-Host "  tag  : $Tag"
Write-Host "  name : $Name"

# THE NAME IS THE ONLY THING THE LAUNCHER SHOWS, so a bad one costs a person a guess.
#
# The parameter has always said "say what to LOOK for, not what changed internally". It was
# still passed as "worldstate" - the BRANCH name - on four consecutive builds, which put four
# rows called "test.N - worldstate" in the dev panel with nothing to tell them apart.
# zeldfep, 2026-09-08: "can we make it into 1 fill test, the test should say what we are
# working on".
#
# A warning rather than a refusal: a bad label is a bad label, not a reason to lose a build at
# the end of a long ship. It is loud enough to fix next time.
$branch = (& git rev-parse --abbrev-ref HEAD 2>$null)

if ($Name -eq $branch -or $Name -eq ($branch -replace '.*/', '') -or $Name -notmatch '\s') {
    Write-Host ""
    Write-Host "  !!  '$Name' reads like a branch or a keyword, not something to look for." -ForegroundColor Yellow
    Write-Host "      The dev panel shows ONLY this. Prefer 'remote players MOVE' or" -ForegroundColor DarkYellow
    Write-Host "      'character selector: click to pick, confirm by name'." -ForegroundColor DarkYellow
    Write-Host ""
}

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
# THE COMMIT IS IN THE TITLE, so a build stays identifiable even if a number is ever
# reused. Numbering is now monotonic, but that depends on a counter file that a fresh
# checkout does not have - the sha does not depend on anything.
#
# A dirty tree is marked. On 2026-09-08 test.18 was published from uncommitted changes, so
# the artifact matched no commit for several minutes; saying so on the release is cheaper
# than refusing to ship and is honest about what was built.
$sha = (& git rev-parse --short HEAD 2>$null)
$dirty = if ((& git status --porcelain 2>$null)) { "+dirty" } else { "" }

$title = "test.$shortNum - $Name ($sha$dirty)"

# UPDATE an existing tag rather than failing on it.
#
# Iterating on one test build is the normal case, not the exception: a tester finds
# something, it gets fixed, and the SAME build number should carry the fix so nobody has to
# be told which of four rows to click. `gh release create` refuses an existing tag, and the
# refusal came after a full verify-and-build - so every iteration ended in a dead script and
# a hand-run `gh release upload --clobber`, three times in one evening before this was
# written. The recovery was always identical, which is the sign it belonged in the tool.
#
# Explicitly NOT deleting and recreating the release: that would break the download URLs the
# launcher may already be holding, and briefly leave the dev panel with no build at all.
# Asked as a LIST, not as "view this tag" - the same fix Ship.ps1 carries at line 1072, and
# for the same reason. A missing release makes `gh release view` write to stderr, PowerShell
# 5.1 turns native stderr into an ErrorRecord, and $ErrorActionPreference='Stop' makes that
# fatal. So PROBING FOR ABSENCE killed the script - after the full verify, the client build
# and the payload staging had all succeeded.
#
# It failed silently too: the abort happened inside a background run that exited 0, so the
# only symptom was a test build that never appeared. 2026-09-07.
$existingTags = (gh release list --repo $GhRepo --limit 100 --json tagName | ConvertFrom-Json).tagName

if ($existingTags -contains $Tag) {
    Write-Host "  $Tag exists - updating it in place" -ForegroundColor DarkGray

    & gh release upload $Tag --repo $GhRepo --clobber $payload $dll
    if ($LASTEXITCODE -ne 0) { Die "uploading the new assets to $Tag failed" }

    # The title carries what to LOOK for, so it has to move with the payload - a refreshed
    # build under last iteration's description is how a tester tests the wrong thing.
    & gh release edit $Tag --repo $GhRepo --title $title --notes $notes | Out-Null
    if ($LASTEXITCODE -ne 0) { Die "updating the title and notes on $Tag failed" }

    Ok "updated $Tag"
}
else {
    & gh release create $Tag --repo $GhRepo --prerelease --title $title --notes $notes $payload $dll
    if ($LASTEXITCODE -ne 0) { Die "publishing failed" }

    Ok "published $Tag"
}
Write-Host "`nInstall it from Settings > DEV > Test builds." -ForegroundColor Green
