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
    [switch]$WhatIf,

    # Which part of the version moves. patch for a fix, minor for a feature, major when
    # something changes that people have to know about.
    [ValidateSet('patch','minor','major')]
    [string]$Bump = 'patch',

    # Re-publish into the SAME version instead of cutting a new one. For fixing a bad
    # upload, not for shipping changes - two different builds sharing a version number is
    # how you end up unable to tell what someone is running.
    [switch]$NoBump,

    # Announcing is the default. Every build that reaches people should reach the Discord
    # too, or #server-update quietly drifts out of step with what is actually live.
    [switch]$NoAnnounce,

    # Leads the Discord message. Without it the announcement is just the release notes,
    # which is fine for a fix and thin for a breakthrough.
    [string]$Highlights
)

$ErrorActionPreference = 'Stop'

$Repo      = "C:\Users\Cam\OneDrive\Documents\GitHub\CyberpunkMP"
$LauncherDir = Join-Path $Repo "code\launcher-lite"
$XMake     = "C:\Users\Cam\scoop\shims\xmake.exe"
$GhRepo    = "ofmiceandcam98-eng/CyberpunkMP"
$GameDir   = "C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077"

# Nothing selected means everything.
$all = -not ($Launcher -or $Mod -or $Server)
if ($all) { $Launcher = $true; $Mod = $true }

function Step  { param($T) Write-Host "`n=== $T" -ForegroundColor Cyan }
function Ok    { param($T) Write-Host "  OK  $T" -ForegroundColor Green }
function Warn  { param($T) Write-Host "  !!  $T" -ForegroundColor Yellow }
function Die   { param($T) Write-Host "`nSTOPPED: $T" -ForegroundColor Red; exit 1 }

Set-Location $Repo

# ---------------------------------------------------------------------------
# Version
#
# One number for the whole project. The launcher's package.json holds it because
# electron-builder must read it from there, and the release tag is that number with a v
# in front - so "which build is this?" has exactly one answer, whether you are looking at
# the launcher's title bar, a GitHub tag, or a Discord post.
#
# The bump happens HERE, before anything is built, because electron-builder stamps the
# version into the installer filename and into latest.yml at package time. Bumping
# afterwards would produce a release whose contents disagree with its name.
# ---------------------------------------------------------------------------

$pkgPath = Join-Path $LauncherDir "package.json"
$version = (Get-Content $pkgPath -Raw -Encoding UTF8 | ConvertFrom-Json).version

# Only a launcher build can move the version, because the version IS the launcher's
# version as far as the auto-updater is concerned. Bumping it on a mod-only ship would
# advertise a launcher release that was never built.
if ($Launcher -and -not $NoBump -and -not $WhatIf) {
    $parts = $version.Split('.')
    switch ($Bump) {
        'major' { $parts[0] = [int]$parts[0] + 1; $parts[1] = 0; $parts[2] = 0 }
        'minor' { $parts[1] = [int]$parts[1] + 1; $parts[2] = 0 }
        default { $parts[2] = [int]$parts[2] + 1 }
    }
    $version = $parts -join '.'

    # Rewrite just the version line. Round-tripping through ConvertTo-Json would reformat
    # the entire file and reorder the build config for no reason.
    $raw = Get-Content $pkgPath -Raw -Encoding UTF8
    $raw = $raw -replace '("version"\s*:\s*")[^"]+(")', "`${1}$version`${2}"
    [System.IO.File]::WriteAllText($pkgPath, $raw, (New-Object System.Text.UTF8Encoding($false)))
}

$Tag = "v$version"

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
    # Both names: the app was renamed to "Night City Online Launcher", and an old install
    # still running under the previous name would block electron-builder just the same.
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -like 'NightCityOnline*' -or $_.ProcessName -like 'Night City Online*' } |
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

        # Actually COMPILE the redscript, using the game's own compiler.
        #
        # This exists because a single bad .reds file does not fail quietly and locally -
        # redscript aborts the whole compilation, so the game starts with NO scripts at
        # all. No menu entry, no chat, no HUD, and nothing that points at the real cause.
        # It looks exactly like the mod doing nothing.
        #
        # The mistake that earned this check: copying a method signature straight out of
        # the game's own .script source. That dialect writes `data : PauseMenuListItemData`;
        # redscript requires `ref<PauseMenuListItemData>`. The two languages look alike
        # enough that the difference is invisible until the game refuses to run any script.
        #
        # Output goes to a scratch file. Pointing this at the real cache would let a ship
        # check overwrite the compiled scripts the game actually loads.
        $scc = Join-Path $GameDir "engine\tools\scc.exe"
        if (-not (Test-Path $scc)) {
            Warn "no scc.exe - skipping the redscript compile check"
        } else {
            $scratch  = Join-Path $env:TEMP "ship_scc"
            New-Item -ItemType Directory -Force -Path $scratch | Out-Null
            # Every script path the game compiles, or the check is meaningless.
            #
            # `r6\scripts` is EMPTY on this install - nothing comes from there. RED4ext
            # registers each plugin's Scripts folder instead, and when -compilePathsFile
            # is supplied scc compiles ONLY the listed paths and ignores the -compile
            # directory entirely. Leaving the other plugins out makes our own files fail
            # on `import Codeware.*`, which is a fake error about real code.
            #
            # zzzCyberpunkMP is excluded because it is a junction to distrib: including it
            # would compile our mod twice and collide on every definition.
            $scriptPaths = @(Join-Path $Repo "distrib\launcher\mod\assets\redscript")
            $scriptPaths += Get-ChildItem (Join-Path $GameDir "red4ext\plugins") -Directory |
                Where-Object { $_.Name -ne 'zzzCyberpunkMP' -and (Test-Path (Join-Path $_.FullName 'Scripts')) } |
                ForEach-Object { Join-Path $_.FullName 'Scripts' }

            # NO BOM. PowerShell 5.1's -Encoding UTF8 writes one, scc reads the first path
            # as garbage, silently compiles nothing, and reports success. That false pass
            # is worse than no check at all - it printed "OK" over a build that disabled
            # every script in the mod.
            $pathsFile = Join-Path $scratch "paths.txt"
            [System.IO.File]::WriteAllLines($pathsFile, $scriptPaths, (New-Object System.Text.UTF8Encoding($false)))

            $sccOut = & $scc -compile (Join-Path $GameDir "r6\scripts") `
                             (Join-Path $GameDir "r6\cache\final.redscripts") `
                             -compilePathsFile $pathsFile `
                             -outputCacheFile (Join-Path $scratch "final.redscripts") 2>&1

            if ($LASTEXITCODE -ne 0) {
                $sccOut | Where-Object { $_ -match 'ERROR|error' } | Select-Object -First 12 | ForEach-Object { Write-Host "      $_" -ForegroundColor Red }
                Die "redscript does not compile - shipping this would disable every script in the mod"
            }
            Ok "redscript compiles"
        }
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
    # Take the version from package.json rather than repeating it here. electron-builder
    # stamps the installer filename with it, so a hardcoded number breaks this copy the
    # first time the version moves - silently shipping nothing, or the previous build.
    $built = Join-Path $LauncherDir "dist\NightCityOnline-Setup-$version.exe"
    if (-not (Test-Path $built)) { Die "no installer at '$built' - version changed without a rebuild?" }

    # The versioned filename goes up UNCHANGED, because latest.yml names it exactly and
    # the auto-updater downloads it by that name. Rename it and self-update breaks with a
    # 404 that looks like a network problem.
    $uploads += $built

    # A second copy under a stable name, for humans. Every link already shared - status
    # page, Discord, INSTALL.txt - points at this, and a version bump must not break them.
    $setup = Join-Path $env:TEMP "NightCityOnline-Setup.exe"
    Copy-Item $built $setup -Force
    $uploads += $setup

    $portableBuilt = Join-Path $LauncherDir "dist\NightCityOnline-Portable.exe"
    if (Test-Path $portableBuilt) {
        $portable = Join-Path $env:TEMP "NightCityOnline-Launcher.exe"
        Copy-Item $portableBuilt $portable -Force
        $uploads += $portable
    }

    # latest.yml IS the auto-update mechanism: version, filename, and hash. Without it
    # electron-updater has nothing to compare against and every launcher silently stays
    # on the version it was installed at, which is the exact problem this replaced.
    $latestYml = Join-Path $LauncherDir "dist\latest.yml"
    if (-not (Test-Path $latestYml)) { Die "no latest.yml - is the github publish config still in package.json?" }
    $uploads += $latestYml

    # Kept for launchers too old to contain the auto-updater. They read the version out of
    # this filename and tell their owner to download manually. Harmless once everyone is
    # past 0.1.3, and the only thing those builds can still see.
    $marker = Join-Path $env:TEMP "launcher-version-$version.txt"
    Set-Content -Path $marker -Value $version -Encoding UTF8
    $uploads += $marker

    Ok "launcher $version staged"
}

if ($uploads.Count -eq 0) { Warn "nothing to publish"; exit 0 }

# Create the release if this version has never been published.
#
# Marked --latest deliberately. Everything downstream resolves through /releases/latest -
# the auto-updater, the status page download button, the launcher's own links - so a
# release that is not flagged latest is a release nobody receives. That already happened
# once: a build sat published-but-not-latest while every download link quietly served an
# older one.
# Asked as a LIST rather than "view this tag". A missing release makes `gh release view`
# print to stderr, and PowerShell 5.1 turns native stderr into an ErrorRecord that
# $ErrorActionPreference='Stop' treats as fatal - so probing for absence killed the whole
# ship, after the version had already been bumped and everything built.
$existingTags = (gh release list --repo $GhRepo --limit 100 --json tagName | ConvertFrom-Json).tagName

if ($existingTags -notcontains $Tag) {
    Step "Create release $Tag"
    $branch = git rev-parse --abbrev-ref HEAD
    gh release create $Tag --repo $GhRepo `
        --target $branch `
        --title "Night City Online $Tag" `
        --notes-file (Join-Path $Repo "publish\release-notes.md") `
        --latest 2>&1 | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) { Die "could not create release $Tag" }
    Ok "release created"
} else {
    # Existing release: refresh the notes so the launcher's patch-notes panel and the
    # Discord post cannot disagree with what was actually written.
    gh release edit $Tag --repo $GhRepo --notes-file (Join-Path $Repo "publish\release-notes.md") --latest 2>&1 | Out-Null
}

gh release upload $Tag @uploads --repo $GhRepo --clobber 2>&1 | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { Die "upload failed" }

# Confirm what is actually on the release, rather than assuming the upload worked.
Step "Verify"
$release = gh api "repos/$GhRepo/releases/tags/$Tag" | ConvertFrom-Json
foreach ($asset in $release.assets) {
    "  {0,-34} {1,7} MB  {2}" -f $asset.name, [math]::Round($asset.size/1MB,1), $asset.updated_at
}

# The auto-updater cannot work without these two, and a release missing them fails in the
# worst way: silently, on everyone else's machine, days later.
if ($Launcher) {
    $names = $release.assets.name
    foreach ($required in @("latest.yml", "NightCityOnline-Setup-$version.exe")) {
        if ($names -notcontains $required) { Die "release is missing $required - auto-update would be broken" }
    }
    Ok "auto-update assets present"
}

# ---------------------------------------------------------------------------
# Announce
# ---------------------------------------------------------------------------

if (-not $NoAnnounce) {
    Step "Announce"
    $announce = @{ Tag = $Tag }
    if ($Highlights) { $announce.Highlights = $Highlights }

    & (Join-Path $Repo "tools\AnnounceRelease.ps1") @announce

    if ($LASTEXITCODE -ne 0) {
        # Not fatal. The build is already published and working; a failed Discord post is
        # worth knowing about but is not a reason to treat the ship as failed.
        Warn "announcement failed - post it by hand: tools\AnnounceRelease.ps1 -Tag $Tag"
    }
}

Write-Host "`nShipped $Tag." -ForegroundColor Green
