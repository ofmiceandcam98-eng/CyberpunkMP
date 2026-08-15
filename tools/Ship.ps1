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

    # Which part of the version moves.
    #
    # PATCH ONLY until this is genuinely 1.0-ready - Cam's call, 2026-08-14. Version
    # numbers are a promise about maturity, and arriving at 1.0 while players are still
    # finding crashes spends that promise on nothing. Passing -Bump minor or major now
    # requires -IKnowThisIsNotOneOh, which exists to make it a decision rather than a
    # habit.
    [ValidateSet('patch','minor','major')]
    [string]$Bump = 'patch',

    # Required to move anything but the patch number. See above.
    [switch]$IKnowThisIsNotOneOh,

    # Re-publish into the SAME version instead of cutting a new one. For fixing a bad
    # upload, not for shipping changes - two different builds sharing a version number is
    # how you end up unable to tell what someone is running.
    [switch]$NoBump,

    # Announcing is the default. Every build that reaches people should reach the Discord
    # too, or #server-update quietly drifts out of step with what is actually live.
    [switch]$NoAnnounce,

    # Leads the Discord message. Without it the announcement is just the release notes,
    # which is fine for a fix and thin for a breakthrough.
    [string]$Highlights,

    # Also publish the portable build. It is another ~98MB copy of the same application
    # and everyone is pointed at the installer, so it is off by default.
    [switch]$Portable
)

$ErrorActionPreference = 'Stop'

# Repo, LauncherDir, XMake, GhRepo and GameDir all come from here. They used to be absolute
# paths to one person's PC, which was invisible while the project lived on one machine and
# became a wall the moment a second contributor had a checkout.
. (Join-Path $PSScriptRoot "Environment.ps1")

Assert-XMake
Assert-GameDir

# Nothing selected means everything.
$all = -not ($Launcher -or $Mod -or $Server)
if ($all) { $Launcher = $true; $Mod = $true }

function Step  { param($T) Write-Host "`n=== $T" -ForegroundColor Cyan }
function Ok    { param($T) Write-Host "  OK  $T" -ForegroundColor Green }
function Warn  { param($T) Write-Host "  !!  $T" -ForegroundColor Yellow }
function Die   { param($T) Write-Host "`nSTOPPED: $T" -ForegroundColor Red; exit 1 }

# Runs a native command without PowerShell treating its stderr as fatal.
#
# PowerShell 5.1 wraps a native program's stderr in ErrorRecords, and this script runs
# with $ErrorActionPreference='Stop', which turns those into terminating errors. Anything
# a tool prints to stderr - including perfectly normal progress and "not found" answers -
# therefore kills the script where it stands.
#
# This has now broken two ships. Once probing for a release that did not exist, where the
# expected answer was fatal. Once on a network timeout inside a RETRY LOOP, where the
# throw jumped clean over the retry and out of the script - so the loop that existed
# specifically to survive that failure never ran even once.
function Invoke-Native {
    param([scriptblock]$Command)

    # The odd name is deliberate. A scriptblock passed in here is EXECUTED here, so any
    # variable it reads resolves against this function's scope first - and this one used a
    # local called $previous. A caller that passed { & gh release download $previous ... }
    # got this function's saved preference string as the tag, and gh answered "release not
    # found" about a release that plainly existed.
    $__invokeNativeSavedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Command } finally { $ErrorActionPreference = $__invokeNativeSavedPreference }
}

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

# Patch only, until this is 1.0-ready. See the -Bump parameter.
if ($Bump -ne 'patch' -and -not $IKnowThisIsNotOneOh) {
    Die "-Bump $Bump is held back while this is in beta. Patch releases only until 1.0-ready; pass -IKnowThisIsNotOneOh if you really mean it."
}

# The notes must mention the version being shipped.
#
# publish\release-notes.md is the body of EVERY release, and it is maintained by hand. It
# stopped being updated at v0.1.12 and nobody noticed for five releases - so v0.2.0 through
# v0.3.3 all published a page headed "What changed - v0.1.12", describing work from days
# earlier. Anyone reading the releases to find out what shipped was told the wrong thing,
# confidently, every time.
#
# Checked here rather than at the end, so the failure costs nothing but a moment's typing.
$notesPath = Join-Path $Repo "publish\release-notes.md"
if (-not $WhatIf -and -not $NoBump) {
    $nextVersion = $version
    if ($Launcher) {
        $peek = $version.Split('.')
        switch ($Bump) {
            'major' { $peek[0] = [int]$peek[0] + 1; $peek[1] = 0; $peek[2] = 0 }
            'minor' { $peek[1] = [int]$peek[1] + 1; $peek[2] = 0 }
            default { $peek[2] = [int]$peek[2] + 1 }
        }
        $nextVersion = $peek -join '.'
    }

    $notes = if (Test-Path $notesPath) { Get-Content $notesPath -Raw -Encoding UTF8 } else { "" }
    if ($notes -notmatch [regex]::Escape("v$nextVersion")) {
        Die "publish\release-notes.md does not mention v$nextVersion. Every release publishes this file as its body - shipping now would put stale notes on the release page. Add a 'What changed - v$nextVersion' section first."
    }
}

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

        # COMPILE FIRST, deploy second.
        #
        # distrib is the junction target - writing there IS writing to the game's plugin
        # folder. Deploying before the check meant broken scripts sat in their live
        # location for however long the check took, and launching the game in that window
        # produced a compilation-error dialog for a state that was fixed a minute later.
        # Nothing reaches the install until it is known to compile.
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
            # Compile a COPY, never the deployed scripts.
            #
            # distrib is the junction target - it IS the game's plugin folder. Checking
            # there meant the scripts had to be written to their live location before
            # anyone knew whether they compiled, leaving a window where launching the game
            # loaded a half-applied change. Cam hit exactly that: a compilation-error
            # dialog for a broken intermediate state that was fixed a minute later.
            #
            # Staging first means the install is only ever written to by the copy above,
            # after the code is known good.
            $staged = Join-Path $scratch "redscript"
            if (Test-Path $staged) { Remove-Item $staged -Recurse -Force }
            New-Item -ItemType Directory -Force -Path $staged | Out-Null
            Copy-Item (Join-Path $Repo "code\assets\redscript\*") $staged -Recurse -Force

            $scriptPaths = @($staged)
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
                Die "redscript does not compile - nothing was deployed, your install is untouched"
            }
            Ok "redscript compiles"
        }

        # NOW deploy. `xmake install` refreshes the DLL but was caught leaving edited .reds
        # files at their previous contents, which is the worst kind of failure: the build
        # succeeds, the version moves, the DLL is new, and the script half of the mod is
        # silently whatever it was last time. That looks exactly like a redscript change
        # that "did not work" and sends you debugging code the game never received.
        Copy-Item "code\assets\redscript\*" -Destination "distrib\launcher\mod\assets\redscript\" -Recurse -Force

        $stale = @()
        Get-ChildItem "code\assets\redscript" -Recurse -Filter *.reds | ForEach-Object {
            $relative = $_.FullName.Substring((Resolve-Path "code\assets\redscript").Path.Length + 1)
            $shipped = "distrib\launcher\mod\assets\redscript\$relative"
            if (-not (Test-Path $shipped)) { $stale += "$relative (missing)" }
            elseif ((Get-FileHash $_.FullName).Hash -ne (Get-FileHash $shipped).Hash) { $stale += $relative }
        }
        if ($stale.Count -gt 0) { Die "scripts did not ship: $($stale -join ', ')" }
        Ok "scripts deployed and match source"
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

        # SMOKE TEST: does the thing we just built actually start?
        #
        # `node --check` proves the file PARSES. It cannot catch anything that only fails
        # when the module is executed - and the one that got through was exactly that: a
        # const read before its declaration, which is valid syntax and throws on load.
        # v0.1.18 shipped a launcher that could not open at all, for everybody, and every
        # check passed.
        #
        # Starting it and seeing whether it is still alive a few seconds later is crude,
        # but it is the difference between shipping a broken launcher and not.
        $built = Join-Path $LauncherDir "dist\win-unpacked\Night City Online Launcher.exe"
        if (-not (Test-Path $built)) {
            Warn "no unpacked build to smoke test - skipping"
        } else {
            # Close any running launcher FIRST, immediately before the test.
            #
            # Pre-flight already does this, but the build between then and now takes
            # minutes and people reopen the launcher in the meantime. The app holds a
            # single-instance lock so nxm:// downloads reach the copy already open, so a
            # second instance quits cleanly with code 0 - which this check read as "the
            # launcher will not start for anyone" and refused to ship a perfectly good
            # build.
            Get-Process -ErrorAction SilentlyContinue |
                Where-Object { $_.ProcessName -like 'Night City Online*' -or $_.ProcessName -like 'NightCityOnline*' } |
                Stop-Process -Force -ErrorAction SilentlyContinue
            Start-Sleep -Seconds 2

            $proc = Start-Process -FilePath $built -PassThru -WindowStyle Minimized
            Start-Sleep -Seconds 8

            if ($proc.HasExited) {
                Pop-Location

                # Exit code 0 within seconds almost always means it lost the
                # single-instance race to a copy that was reopened mid-build, not that it
                # is broken. Say so, rather than sending someone hunting a startup crash
                # that does not exist.
                if ($proc.ExitCode -eq 0) {
                    Die "the launcher exited cleanly on startup - close any running copy and ship again"
                }

                Die "the packaged launcher crashed on startup (code $($proc.ExitCode)) - it would not start for anyone"
            }

            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            Ok "launcher starts and stays running"
        }
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

    # FullInstall.zip - what a NEW player gets. Rebuilt every ship.
    #
    # This is the launcher's "Install everything" download, and it went missing entirely
    # when releases became versioned: it lived only on one old tag, and the launcher was
    # still building its URL from a constant that had been deleted. First-time install
    # was broken outright and nothing surfaced it, because nobody does a fresh install
    # while they already have one.
    #
    # Assembled rather than copied forward, so the mod inside is the build being shipped.
    # The old zip carried a mod from days earlier, which meant a new player installed a
    # stale build and was immediately asked to update.
    $base = Join-Path $Repo "publish\fullinstall-base"
    if (-not (Test-Path $base)) {
        Warn "no publish\fullinstall-base - FullInstall.zip NOT shipped, new installs will fail"
    } else {
        $fullStage = Join-Path $env:TEMP ("fullinstall_" + (Get-Date -Format 'HHmmss'))
        New-Item -ItemType Directory -Path $fullStage -Force | Out-Null

        # The parts that do not change: prerequisites and their licence texts.
        Copy-Item (Join-Path $base "*") $fullStage -Recurse -Force

        # The part that does: this build's mod.
        $modOut = Join-Path $fullStage "mod"
        New-Item -ItemType Directory -Path $modOut -Force | Out-Null
        Copy-Item "distrib\launcher\mod\CyberpunkMP.dll" $modOut
        Copy-Item "distrib\launcher\mod\assets" (Join-Path $modOut "assets") -Recurse
        Copy-Item "distrib\launcher\mod\Rpc"    (Join-Path $modOut "Rpc")    -Recurse

        # Instructions ship INSIDE the package, where someone who downloaded a zip will
        # actually look. The Discord post has told people to read this file for days
        # while it was not there.
        if (Test-Path (Join-Path $Repo "publish\INSTALL.txt")) {
            Copy-Item (Join-Path $Repo "publish\INSTALL.txt") $fullStage
        }

        $fullZip = Join-Path $env:TEMP "FullInstall.zip"
        if (Test-Path $fullZip) { Remove-Item -LiteralPath $fullZip -Force }
        [System.IO.Compression.ZipFile]::CreateFromDirectory($fullStage, $fullZip,
            [System.IO.Compression.CompressionLevel]::Optimal, $false)

        $uploads += $fullZip
        Ok "full install package assembled ($([math]::Round((Get-Item $fullZip).Length/1MB,1)) MB)"
    }
}

# ---------------------------------------------------------------------------
# EVERY release carries the whole runtime set, whatever was actually rebuilt.
#
# "releases/latest/download/<name>" is how the launcher finds all of this, and `latest`
# moves to whatever was published last. A launcher-only ship used to publish ONLY the
# installer - so the moment v0.3.1 went out, every launcher in existence got a 404 for
# server.json, fell back to 127.0.0.1, decided its own PC was the server and reported it
# offline. The mod update check broke the same way.
#
# Nothing about that ship was wrong except what it left out, and it looked completely
# clean: every check passed. So completeness cannot be a property of which switches were
# passed - it has to be checked here, every time.
# ---------------------------------------------------------------------------

# The small published files, which are decided by people rather than by a build.
$sideFiles = @(
    @{ Path = "publish\modlist.json";            Label = "mod list";       Required = $false },
    @{ Path = "publish\server.json";             Label = "server address"; Required = $true  },
    @{ Path = "publish\roles.json";              Label = "role map";       Required = $false },
    @{ Path = "publish\assistant-updates.json";  Label = "dev updates";    Required = $false }
)

foreach ($side in $sideFiles) {
    $full = Join-Path $Repo $side.Path
    if (Test-Path $full) {
        $uploads += $full
        Ok "$($side.Label) staged"
    } elseif ($side.Required) {
        Die "missing $($side.Path) - every launcher would fall back to 127.0.0.1 and report the server offline"
    } else {
        Warn "no $($side.Path) - that feature will be inert for everyone"
    }
}

# The world template every player loads.
#
# Zipped here rather than stored zipped, so the save stays inspectable in the repo - being
# able to read its metadata is how we know what state it is in. The launcher extracts it
# into the player's saves folder and stamps it newest; see ensureTemplateSave.
# One per body type. Body gender cannot be changed in game - not at a ripperdoc, not at a
# mirror - so the only way to offer both is to ship both worlds and let the launcher
# install whichever was chosen.
#
# The male one is optional: while it does not exist the launcher shows that choice as
# unavailable rather than pretending to honour it.
Add-Type -AssemblyName System.IO.Compression.FileSystem

$templates = @(
    @{ Dir = "publish\character-template";      Zip = "character-template.zip";      Label = "female" },
    @{ Dir = "publish\character-template-male"; Zip = "character-template-male.zip"; Label = "male"   }
)

foreach ($template in $templates) {
    $templateDir = Join-Path $Repo $template.Dir

    if (-not (Test-Path (Join-Path $templateDir "sav.dat"))) {
        if ($template.Label -eq "female") {
            Warn "no $($template.Dir)\sav.dat - players will load their own saves instead"
        } else {
            Warn "no $($template.Label) template yet - that body type stays unavailable in the launcher"
        }
        continue
    }

    $templateZip = Join-Path $env:TEMP $template.Zip
    if (Test-Path $templateZip) { Remove-Item -LiteralPath $templateZip -Force }

    # Staged WITHOUT the README. That folder becomes a save directory on the player's
    # machine and Cyberpunk reads it looking for saves - a stray markdown file there is at
    # best noise and at worst something the game tries to parse.
    $templateStage = Join-Path $env:TEMP ("template_" + $template.Label + "_" + (Get-Date -Format 'HHmmss'))
    if (Test-Path $templateStage) { Remove-Item -LiteralPath $templateStage -Recurse -Force }
    New-Item -ItemType Directory -Path $templateStage -Force | Out-Null

    foreach ($part in @("sav.dat", "metadata.9.json", "screenshot.png")) {
        $from = Join-Path $templateDir $part
        if (Test-Path $from) { Copy-Item $from $templateStage }
    }

    [System.IO.Compression.ZipFile]::CreateFromDirectory($templateStage, $templateZip,
        [System.IO.Compression.CompressionLevel]::Optimal, $false)

    $uploads += $templateZip
    Ok "$($template.Label) world template staged ($([math]::Round((Get-Item $templateZip).Length/1MB,1)) MB)"
}

# The mod artifacts. Rebuilt above when -Mod, carried forward from the previous release
# otherwise, so a launcher-only ship never leaves players unable to update the mod.
if (-not $Mod) {
    $carry = Join-Path $env:TEMP ("ship_carry_" + (Get-Date -Format 'HHmmss'))
    New-Item -ItemType Directory -Path $carry -Force | Out-Null

    $carryFrom = Invoke-Native { & gh release view --repo $GhRepo --json tagName -q .tagName }

    if (-not $carryFrom) {
        Warn "no previous release to carry the mod forward from - this release will have no mod payload"
    } else {
        $carried = 0

        foreach ($name in @("ModPayload.zip", "CyberpunkMP.dll", "FullInstall.zip")) {
            Invoke-Native { & gh release download $carryFrom --repo $GhRepo --dir $carry --pattern $name } | Out-Null

            $got = Join-Path $carry $name
            if (Test-Path $got) { $uploads += $got; $carried++ }
            else { Warn "could not carry $name forward from $carryFrom" }
        }

        if ($carried -gt 0) { Ok "$carried mod artifact(s) carried forward from $carryFrom" }
        else { Warn "nothing could be carried forward from $carryFrom" }
    }
}

if ($Launcher) {
    # Take the version from package.json rather than repeating it here. electron-builder
    # stamps the installer filename with it, so a hardcoded number breaks this copy the
    # first time the version moves - silently shipping nothing, or the previous build.
    # ONE installer, under a stable name.
    #
    # This used to upload the same ~99MB file twice - once versioned, because latest.yml
    # names the file the auto-updater fetches, and once stable-named because every shared
    # link points at it. Plus a ~98MB portable build. Nearly 300MB per ship, of which two
    # thirds was the same bytes twice, pushed over Wi-Fi.
    #
    # That was the actual cause of three failed ships in one night: not a broken network,
    # just a multi-minute upload with plenty of time to hit a blip. electron-builder does
    # not require a version in the filename - latest.yml records whatever name is used -
    # so one file satisfies both the updater and the links.
    $built = Join-Path $LauncherDir "dist\NightCityOnline-Setup.exe"
    if (-not (Test-Path $built)) { Die "no installer at '$built' - did the build actually run?" }
    $uploads += $built

    # The portable build is a third copy of the same application, and the installer is
    # what everyone is pointed at. Uploaded only when explicitly asked for.
    if ($Portable) {
        $portableBuilt = Join-Path $LauncherDir "dist\NightCityOnline-Portable.exe"
        if (Test-Path $portableBuilt) {
            $portable = Join-Path $env:TEMP "NightCityOnline-Launcher.exe"
            Copy-Item $portableBuilt $portable -Force
            $uploads += $portable
        }
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

# Created as a PRERELEASE, and only promoted to latest once the files are verified.
#
# Everything downstream resolves through /releases/latest - the auto-updater, the status
# page, the launcher's links. Marking a release latest before its assets are up means any
# interruption leaves the current release pointing at files that are not there. That is
# not hypothetical: a 98MB upload timed out mid-flight on v0.1.6, leaving latest.yml
# published and the installer it names missing, so every launcher checking for updates
# would have got a 404. Nothing is "current" until it is complete.
if ($existingTags -notcontains $Tag) {
    Step "Create release $Tag"
    $branch = git rev-parse --abbrev-ref HEAD
    gh release create $Tag --repo $GhRepo `
        --target $branch `
        --title "Night City Online BETA $Tag" `
        --notes-file (Join-Path $Repo "publish\release-notes.md") `
        --prerelease 2>&1 | Select-Object -Last 1
    if ($LASTEXITCODE -ne 0) { Die "could not create release $Tag" }
    Ok "release created (held back until its files are verified)"
} else {
    # Refresh the notes so the launcher's patch-notes panel and the Discord post cannot
    # disagree with what was actually written.
    gh release edit $Tag --repo $GhRepo --notes-file (Join-Path $Repo "publish\release-notes.md") 2>&1 | Out-Null
}

# Uploads are retried. These are ~100MB over a home connection, and a transient timeout
# should cost a retry rather than the whole ship.
$uploaded = $false
foreach ($attempt in 1..4) {
    Invoke-Native { gh release upload $Tag @uploads --repo $GhRepo --clobber }

    if ($LASTEXITCODE -eq 0) { $uploaded = $true; break }

    if ($attempt -lt 4) {
        # Long enough for a DNS hiccup or a dropped route to recover. These are ~100MB
        # uploads over a home connection; failing fast helps nobody.
        Warn "upload attempt $attempt failed (exit $LASTEXITCODE) - retrying in 20s"
        Start-Sleep -Seconds 20
    }
}
if (-not $uploaded) { Die "upload failed after 4 attempts - $Tag is still a prerelease, so nothing is pointing at it" }

# Confirm what is actually on the release, rather than assuming the upload worked.
Step "Verify"
$release = gh api "repos/$GhRepo/releases/tags/$Tag" | ConvertFrom-Json
foreach ($asset in $release.assets) {
    "  {0,-34} {1,7} MB  {2}" -f $asset.name, [math]::Round($asset.size/1MB,1), $asset.updated_at
}

# The auto-updater cannot work without these two, and a release missing them fails in the
# worst way: silently, on everyone else's machine, days later.
$names = $release.assets.name

if ($Launcher) {
    foreach ($required in @("latest.yml", "NightCityOnline-Setup.exe")) {
        if ($names -notcontains $required) { Die "release is missing $required - auto-update would be broken" }
    }
    Ok "auto-update assets present"
}

if ($Mod -and $names -notcontains "ModPayload.zip") { Die "release is missing ModPayload.zip" }

# The runtime set, checked BEFORE this tag becomes `latest`.
#
# Every one of these is fetched by the launcher from releases/latest/download/, so a
# release without them does not fail here - it fails on everybody else's machine, quietly,
# as "server offline" or a mod that will not update. That is exactly what v0.3.1 did, and
# every check in this script passed while it happened.
#
# Checked against what GitHub actually has, not against what we meant to upload.
foreach ($required in @("server.json", "modlist.json", "ModPayload.zip", "CyberpunkMP.dll")) {
    if ($names -notcontains $required) {
        Die "release is missing $required - promoting it would break every launcher. Nothing was promoted; $Tag is still a prerelease."
    }
}
Ok "runtime assets complete"

# Only NOW does this become the release everyone receives.
gh release edit $Tag --repo $GhRepo --prerelease=false --latest 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Die "uploaded fine but could not mark $Tag as latest - nobody will be offered it" }
Ok "promoted to latest"

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
