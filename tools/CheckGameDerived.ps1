# CheckGameDerived.ps1 - what was taken from the game, and what other players are built from.
#
# WHY THIS EXISTS
#
# Three ways the same work has been done twice, each one silent until somebody noticed in game:
#
#   1. A file of ours that is the game's own file with our change on top replaces the game's
#      WHOLE file for every player. When a patch changes the original, nothing says so: the HUD
#      layout was a 2024 copy for six days into 2.5 and hid three of the game's own HUD elements.
#   2. A new file dropped at one of the game's paths starts doing that the day it is added.
#   3. Other players are built from an entity template. One with meshes of its own puts those
#      same meshes on everybody, which is the opposite of each player being their own character.
#      The tweak keeps the wrong templates one line above the right ones, commented out.
#
# WHAT IT READS
#
# tools\game-derived.json is the list. Moving game.supportedVersion in the manifest fails every
# entry taken from the old patch until it has been redone, so on patch day the output of this
# script is the work order rather than something to remember.
#
# HOW IT REPORTS
#
# One object per finding - Kind is fail, pass or note - and nothing printed. Verify turns them
# into its own what / where / fix lines, and tools\tests\game_derived_test.ps1 breaks a copy of
# the tree one way at a time to prove each failure still fires.
#
#   .\tools\CheckGameDerived.ps1
#
[CmdletBinding()]
param(
    # The tree to check. The test points this at a throwaway copy.
    [string]$Root = (Split-Path -Parent $PSScriptRoot),

    # The game patch installed on this machine, when there is one. Verify reads it off the exe.
    [string]$InstalledPatch
)

$ErrorActionPreference = 'Stop'

function Finding {
    param([string]$Kind, [string]$Summary, [string]$What, [string]$Where, [string]$Fix)
    [pscustomobject]@{ Kind = $Kind; Summary = $Summary; What = $What; Where = $Where; Fix = $Fix }
}

function Rooted([string]$Relative) { Join-Path $Root ($Relative -replace '/', '\') }

$derivedPath = "tools\game-derived.json"
$manifestPath = "publish\manifest-source.json"

$gamePatch = [string](Get-Content -LiteralPath (Rooted $manifestPath) -Raw | ConvertFrom-Json).game.supportedVersion
$derived = Get-Content -LiteralPath (Rooted $derivedPath) -Raw | ConvertFrom-Json

if (-not $gamePatch) {
    Finding fail "$manifestPath names no game.supportedVersion" `
        "nothing here can say which patch the tree is matched to, so nothing taken from the game can be checked against it." `
        "$manifestPath, game.supportedVersion" `
        "set it to the patch this tree is built for"
    return
}

# --- 1. everything taken from the game is matched to the patch the manifest names ---------------
$takenOk = $true
foreach ($entry in $derived.takenFromGame) {
    $entryPath = ([string]$entry.path) -replace '/', '\'

    if (-not (Test-Path -LiteralPath (Rooted $entryPath))) {
        $takenOk = $false
        Finding fail "$derivedPath lists $entryPath, which is not there" `
            "the list no longer describes the tree, so a patch can move past a copy of a game file that nothing is watching." `
            "$derivedPath, takenFromGame" `
            "if it moved, correct the path; if it was removed on purpose, remove its entry in the same commit"
        continue
    }

    # stale and refresh ARE the failure message on patch day; an entry without them fails now,
    # while whoever added it still remembers what they would have said.
    $unwritten = @('takenFrom', 'stale', 'refresh' | Where-Object { -not [string]$entry.$_ })
    if ($unwritten) {
        $takenOk = $false
        Finding fail "$derivedPath, the entry for $($entry.path), has no $($unwritten -join ' / ')" `
            "when the game moves, this entry is the instruction for redoing the file. Without it the failure can only say that something is old, and the work of finding out what and how is done again." `
            "$derivedPath, takenFromGame, the entry for $($entry.path)" `
            "write them: takenFrom is the patch it was matched to, stale is what a player sees when it is old, refresh is how to redo it"
        continue
    }

    if ([string]$entry.takenFrom -ne $gamePatch) {
        $takenOk = $false
        Finding fail "$entryPath was matched to game patch $($entry.takenFrom), the manifest says $gamePatch" `
            ([string]$entry.stale) `
            "$derivedPath, takenFromGame, the entry for $($entry.path)" `
            "$($entry.refresh) Then set its takenFrom to $gamePatch, in the same commit as the redone file."
    }
}
if ($takenOk) { Finding pass "$(@($derived.takenFromGame).Count) thing(s) taken from the game are all matched to patch $gamePatch" }

# --- 2. fail closed: an archive source file is ours by folder, or listed, or it stops the ship --
$archiveRoot = ([string]$derived.archiveRoot) -replace '/', '\'
$archiveRootFull = (Resolve-Path -LiteralPath (Rooted $archiveRoot)).Path.TrimEnd('\')
$oursPaths = @($derived.ours | ForEach-Object { [string]$_.path })
$takenPaths = @($derived.takenFromGame | ForEach-Object { [string]$_.path })
$archiveFiles = @(Get-ChildItem -LiteralPath $archiveRootFull -Recurse -File)

$coverageOk = $true
foreach ($file in $archiveFiles) {
    $rel = $file.FullName.Substring($archiveRootFull.Length + 1) -replace '\\', '/'

    $isOurs = $false
    foreach ($ours in $oursPaths) {
        if ($rel -eq $ours) { $isOurs = $true }
        elseif ($ours.EndsWith('/') -and $rel.StartsWith($ours, [System.StringComparison]::OrdinalIgnoreCase)) { $isOurs = $true }
    }

    if (-not $isOurs -and ($takenPaths -notcontains "$($derived.archiveRoot)/$rel")) {
        $coverageOk = $false
        Finding fail "$archiveRoot\$($rel -replace '/', '\') is outside our own folders and nothing says where it came from" `
            "a file packed at one of the game's own paths replaces the game's whole file for every player, and nothing notices when a patch changes the original underneath it." `
            $derivedPath `
            "if it was taken from the game, add it to takenFromGame with the patch and how to redo it; if the game has no file at that path, add it to ours and say how that was checked"
    }
}
if ($coverageOk) { Finding pass "all $($archiveFiles.Count) archive source file(s) are ours by folder or listed as taken from the game" }

# --- 3. other players are built only from templates measured to carry no body of their own ------
# The records are read from where the client picks them: the two defaults, and the fallback the
# script uses when a record does not resolve. (-puppet-record on the command line can name any
# record for one launch; that is a developer's own experiment and not what ships.)
$settingsPath = "code\client\App\Settings.h"
$worldScriptPath = "code\assets\redscript\World\NetworkWorldSystem.reds"
$tweakPath = "code\assets\Tweaks\CyberpunkMP.tweak"

$spawnRecords = @()
foreach ($m in [regex]::Matches((Get-Content -LiteralPath (Rooted $settingsPath) -Raw), 'puppetRecord(?:Male|Female)\s*=\s*"([^"]+)"')) { $spawnRecords += $m.Groups[1].Value }
foreach ($m in [regex]::Matches((Get-Content -LiteralPath (Rooted $worldScriptPath) -Raw), 't"(Character\.\w+)"')) { $spawnRecords += $m.Groups[1].Value }
$spawnRecords = @($spawnRecords | Sort-Object -Unique)

$measuredTemplates = @($derived.playerTemplates.measured | ForEach-Object { [string]$_.path })
$tweakText = Get-Content -LiteralPath (Rooted $tweakPath) -Raw
$templatesOk = $true

if ($spawnRecords.Count -lt 2) {
    $templatesOk = $false
    Finding fail "could not find the records other players are spawned from" `
        "the check that every player is built from a template with no body of its own is checking nothing, and would pass whatever the records point at." `
        "$settingsPath (puppetRecordMale / puppetRecordFemale) and $worldScriptPath (the t`"Character...`" fallback)" `
        "the names moved: point section 3 of tools\CheckGameDerived.ps1 at wherever the spawned record is chosen now"
}

foreach ($record in $spawnRecords) {
    $block = [regex]::Match($tweakText, '(?ms)^' + [regex]::Escape($record) + '\s*:.*?^\}')

    if (-not $block.Success) {
        $templatesOk = $false
        Finding fail "$record is what other players are spawned from, and $tweakPath does not define it" `
            "the spawn asks the game for a record that does not exist; the other player does not appear." `
            "$settingsPath or $worldScriptPath names it" `
            "define the record in $tweakPath, or name one that exists"
        continue
    }

    # Live lines only: the commented-out alternatives are exactly what this exists to keep out.
    $templates = @()
    foreach ($line in ($block.Value -split "`n")) {
        $live = $line -replace '//.*$', ''
        foreach ($m in [regex]::Matches($live, '"([^"]+\.ent)"')) { $templates += ($m.Groups[1].Value -replace '\\', '/') }
    }
    $templates = @($templates | Sort-Object -Unique)

    if ($templates.Count -eq 0) {
        $templatesOk = $false
        Finding fail "$record names no entity template of its own" `
            "it is built from whatever its parent record uses, which nobody has measured - and a template with meshes of its own puts them on every player." `
            "$tweakPath, the $record record" `
            "give it an entityTemplatePath that is listed under playerTemplates in $derivedPath"
    }

    foreach ($template in $templates) {
        if ($measuredTemplates -notcontains $template) {
            $templatesOk = $false
            Finding fail "$record builds other players from $template, which has not been measured" `
                "any mesh the template carries itself is on EVERY player built from it, over or instead of that player's own character. That follows from how a template works and has not been watched in game; the templates in use were measured at zero such meshes, and each player's own look has only been seen on those." `
                "$tweakPath, the $record record; the measured list is playerTemplates in $derivedPath" `
                "WolvenKit.CLI convert serialize the template and count the chunks whose `$type is entSkinnedMeshComponent. Zero: add it to playerTemplates with today's date. Not zero: it is the wrong template - docs\MAP.md, CODE MAP, row 'Player templates'"
        }
    }
}
if ($templatesOk) { Finding pass "other players ($($spawnRecords -join ', ')) are built only from templates measured to have no body of their own" }

# --- 4. the game on this machine ----------------------------------------------------------------
# A note, not a failure: the build box follows Steam, and a hotfix for the patch players are
# still on has to be shippable the morning the game moves here.
if ($InstalledPatch) {
    if ($InstalledPatch -ne $gamePatch) {
        Finding note "the game installed here is $InstalledPatch; this tree is matched to $gamePatch" `
            "everything was checked against $gamePatch. Scripts compile against $InstalledPatch on this machine, and the launcher refuses players whose game has moved." `
            "$manifestPath, game.supportedVersion" `
            "the port starts by setting game.supportedVersion to $InstalledPatch. This check then fails once for each thing taken from $gamePatch, with how to redo it."
    }
    else { Finding pass "the game installed here is $InstalledPatch, the patch this tree is matched to" }
}
