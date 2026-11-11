# tools\CheckGameDerived.ps1: does each failure still fire on the thing it exists to catch?
#
# A gate nobody has watched fail is a gate nobody knows works. This copies the parts of the tree
# the check reads into a throwaway folder, breaks the copy one way at a time - the patch moves, a
# file appears at one of the game's paths, a spawn record is pointed at the template one line
# above the right one - and requires the exact failure each time. The tree itself is never
# touched, so it is safe beside another session working in the same checkout.
#
# The copy mirrors the REAL files (the real tweak, the real Settings.h), so this also proves the
# check still reads the shapes those files have today. If the unbroken copy already fails, the
# tree is what is wrong: Verify prints the same failure, and the cases after it mean nothing
# until that is fixed.
#
# Needs nothing but PowerShell - no game, no compiler. Verify runs it; by hand:
#
#   powershell -NoProfile -File tools\tests\game_derived_test.ps1
#   powershell -NoProfile -File tools\tests\game_derived_test.ps1 -Show    # also print each failure as it reads
param(
    [string]$Repo = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),

    # Print what the check said for each broken copy - the text somebody reads on patch day.
    [switch]$Show
)

$ErrorActionPreference = 'Stop'

$check = Join-Path $Repo "tools\CheckGameDerived.ps1"
$registryPath = "tools\game-derived.json"
$manifestPath = "publish\manifest-source.json"
$settingsPath = "code\client\App\Settings.h"
$worldScriptPath = "code\assets\redscript\World\NetworkWorldSystem.reds"
$tweakPath = "code\assets\Tweaks\CyberpunkMP.tweak"

$registry = Get-Content -LiteralPath (Join-Path $Repo $registryPath) -Raw | ConvertFrom-Json
$archiveRoot = ([string]$registry.archiveRoot) -replace '/', '\'
$gamePatch = [string](Get-Content -LiteralPath (Join-Path $Repo $manifestPath) -Raw | ConvertFrom-Json).game.supportedVersion
$takenCount = @($registry.takenFromGame).Count

$tempRoot = [System.IO.Path]::GetTempPath()
$sandbox = Join-Path $tempRoot ("nco-game-derived-" + [guid]::NewGuid().ToString('N').Substring(0, 8))

function Remove-Sandbox {
    # Only ever a folder this script made, under the temp directory.
    if ($sandbox.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and
        $sandbox -like '*nco-game-derived-*' -and (Test-Path -LiteralPath $sandbox)) {
        Remove-Item -LiteralPath $sandbox -Recurse -Force
    }
}

function Add-EmptyFile([string]$Relative) {
    $target = Join-Path $sandbox $Relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    [System.IO.File]::WriteAllBytes($target, (New-Object byte[] 0))
}

function Copy-Real([string]$Relative) {
    $target = Join-Path $sandbox $Relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath (Join-Path $Repo $Relative) -Destination $target -Force
}

# A fresh copy for every case, so no case inherits the last one's damage.
function New-Sandbox {
    Remove-Sandbox

    # The archive source by NAME only: the check never opens these, and the real tree is 14 MB.
    $archiveFull = (Resolve-Path -LiteralPath (Join-Path $Repo $archiveRoot)).Path.TrimEnd('\')
    foreach ($file in Get-ChildItem -LiteralPath $archiveFull -Recurse -File) {
        Add-EmptyFile (Join-Path $archiveRoot $file.FullName.Substring($archiveFull.Length + 1))
    }

    # Everything else the list names: a folder becomes an empty folder, a file is copied.
    foreach ($entry in $registry.takenFromGame) {
        $relative = ([string]$entry.path) -replace '/', '\'
        $real = Join-Path $Repo $relative
        if (Test-Path -LiteralPath $real -PathType Container) {
            New-Item -ItemType Directory -Force -Path (Join-Path $sandbox $relative) | Out-Null
        }
        elseif ((Test-Path -LiteralPath $real) -and -not (Test-Path -LiteralPath (Join-Path $sandbox $relative))) {
            Copy-Real $relative
        }
    }

    # What the check READS, for real.
    foreach ($relative in @($registryPath, $manifestPath, $settingsPath, $worldScriptPath, $tweakPath)) { Copy-Real $relative }
}

# A mutation that changes nothing would let its case pass for the wrong reason, so it is recorded
# and the case fails saying so.
$script:mutationProblem = $null

function Edit-Text {
    param([string]$Relative, [scriptblock]$Change)
    $path = Join-Path $sandbox $Relative
    $before = [System.IO.File]::ReadAllText($path)
    $after = [string](& $Change $before)
    if ($after -ceq $before) { $script:mutationProblem = "the edit to the copy of $Relative changed nothing - the file no longer has the shape this case expects" }
    [System.IO.File]::WriteAllText($path, $after)
}

# Rewrites the lines of one tweak record; the record ends at the first } in column 0.
function Edit-Record {
    param([string]$Record, [scriptblock]$ChangeLine)
    $path = Join-Path $sandbox $tweakPath
    $before = [System.IO.File]::ReadAllText($path)
    $inside = $false
    $lines = foreach ($line in ($before -split "`n")) {
        if ($line -match ('^' + [regex]::Escape($Record) + '\s*:')) { $inside = $true }
        elseif ($inside -and $line -match '^\}') { $inside = $false }
        if ($inside) { [string](& $ChangeLine $line) } else { $line }
    }
    $after = $lines -join "`n"
    if ($after -ceq $before) { $script:mutationProblem = "the edit to the $Record record in the copy of $tweakPath changed nothing - the record no longer has the shape this case expects" }
    [System.IO.File]::WriteAllText($path, $after)
}

function Invoke-Check {
    param([string]$InstalledPatch)
    @(& $check -Root $sandbox -InstalledPatch $InstalledPatch)
}

$script:cases = 0
$script:failing = 0

function Expect {
    param(
        [Parameter(Mandatory)][string]$Case,
        [object[]]$Findings = @(),
        [int]$Fails = 0,
        [int]$Notes = 0,
        [string[]]$Naming = @(),      # each must appear in the summary of a failure or a note
        [string[]]$Passing = @(),     # each must appear in an ok line
        [string[]]$NotPassing = @()   # none may appear in an ok line
    )

    $script:cases++
    $failed = @($Findings | Where-Object { $_.Kind -eq 'fail' })
    $noted = @($Findings | Where-Object { $_.Kind -eq 'note' })
    $passed = @($Findings | Where-Object { $_.Kind -eq 'pass' })
    $reasons = @()

    if ($script:mutationProblem) { $reasons += $script:mutationProblem; $script:mutationProblem = $null }
    if ($failed.Count -ne $Fails) { $reasons += "expected $Fails failure(s), the check reported $($failed.Count)" }
    if ($noted.Count -ne $Notes) { $reasons += "expected $Notes note(s), the check reported $($noted.Count)" }

    foreach ($text in $Naming) {
        if (-not (@($failed) + @($noted) | Where-Object { $_.Summary.IndexOf($text, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 })) {
            $reasons += "no failure or note names '$text'"
        }
    }
    foreach ($text in $Passing) {
        if (-not ($passed | Where-Object { $_.Summary.IndexOf($text, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 })) {
            $reasons += "no ok line says '$text'"
        }
    }
    foreach ($text in $NotPassing) {
        if ($passed | Where-Object { $_.Summary.IndexOf($text, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 }) {
            $reasons += "an ok line still says '$text' while the thing it vouches for is broken"
        }
    }
    foreach ($finding in (@($failed) + @($noted))) {
        if (-not $finding.What -or -not $finding.Where -or -not $finding.Fix) {
            $reasons += "'$($finding.Summary)' is missing its what, where or fix"
        }
    }

    if (-not $reasons) {
        Write-Output "  ok    $Case"
        if ($Show) {
            foreach ($finding in (@($failed) + @($noted))) {
                Write-Output "          [$($finding.Kind)] $($finding.Summary)"
                Write-Output "                 what   $($finding.What)"
                Write-Output "                 where  $($finding.Where)"
                Write-Output "                 fix    $($finding.Fix)"
            }
        }
        return
    }

    $script:failing++
    Write-Output "  FAIL  $Case"
    Write-Output "        what   $($reasons -join '; ')"
    Write-Output "        where  tools\CheckGameDerived.ps1, against a copy of the tree broken as the case says"
    Write-Output "        fix    the check reported what follows; make it report what the case expects, or correct the case in tools\tests\game_derived_test.ps1"
    if (-not $Findings) { Write-Output "               (the check reported nothing at all)" }
    foreach ($finding in $Findings) { Write-Output "               [$($finding.Kind)] $($finding.Summary)" }
}

Write-Output "CheckGameDerived, against a copy of the tree broken one way at a time (manifest patch: $gamePatch)"

try {
    # --- the copy as it is ---------------------------------------------------------------------
    New-Sandbox
    Expect "an unbroken copy is clean, and the tweak's commented-out templates are not read" (Invoke-Check) `
        -Passing "matched to patch $gamePatch", "ours by folder or listed", "built only from templates measured"

    # --- the patch moves ------------------------------------------------------------------------
    New-Sandbox
    Edit-Text $manifestPath { param($text) [regex]::Replace($text, '("supportedVersion"\s*:\s*")[^"]*(")', '${1}9.9${2}') }
    $naming = @($registry.takenFromGame | ForEach-Object { ([string]$_.path) -replace '/', '\' }) + @("the manifest says 9.9")
    Expect "the manifest moves to 9.9: one failure for each thing taken from $gamePatch" (Invoke-Check) `
        -Fails $takenCount -Naming $naming -NotPassing "matched to patch"

    New-Sandbox
    Edit-Text $manifestPath { param($text) [regex]::Replace($text, '("supportedVersion"\s*:\s*")[^"]*(")', '${1}${2}') }
    Expect "the manifest names no patch: one failure, and nothing is vouched for" (Invoke-Check) `
        -Fails 1 -Naming "names no game.supportedVersion" -NotPassing "matched to patch", "built only from"

    # --- the list and the tree drift apart ------------------------------------------------------
    New-Sandbox
    $firstTaken = ([string]@($registry.takenFromGame)[0].path) -replace '/', '\'
    Remove-Item -LiteralPath (Join-Path $sandbox $firstTaken) -Force
    Expect "a listed file is deleted: the list says so" (Invoke-Check) `
        -Fails 1 -Naming "$firstTaken, which is not there"

    New-Sandbox
    Edit-Text $registryPath { param($text) ([regex]'"refresh"\s*:\s*"(?:[^"\\]|\\.)*"').Replace($text, '"refresh": ""', 1) }
    Expect "an entry that does not say how to redo the file fails now, not on patch day" (Invoke-Check) `
        -Fails 1 -Naming "has no refresh" -NotPassing "matched to patch"

    New-Sandbox
    Add-EmptyFile (Join-Path $archiveRoot "base\gameplay\gui\widgets\added_by_the_test.inkwidget")
    Expect "a new file appears at one of the game's paths: it stops the ship" (Invoke-Check) `
        -Fails 1 -Naming "added_by_the_test.inkwidget is outside our own folders" -NotPassing "ours by folder or listed"

    New-Sandbox
    Add-EmptyFile (Join-Path $archiveRoot "multi\gameplay\gui\widgets\minimap\added_by_the_test.inkwidget")
    Expect "a single file listed as ours does not vouch for its neighbours" (Invoke-Check) `
        -Fails 1 -Naming "added_by_the_test.inkwidget is outside our own folders"

    New-Sandbox
    Add-EmptyFile (Join-Path $archiveRoot "mods\cyberpunkmp\added_by_the_test.ent")
    Expect "a new file under our own folder is nobody's business" (Invoke-Check) `
        -Passing "ours by folder or listed"

    # --- what other players are built from ------------------------------------------------------
    New-Sandbox
    Edit-Record "Character.MaMuppet" {
        param($line)
        if ($line -match '^\s*//') { $line } else { $line.Replace('player_ma_tpp_cutscene.ent', 'player_ma_fpp.ent') }
    }
    Expect "the male record is pointed at the template commented out one line above it" (Invoke-Check) `
        -Fails 1 -Naming "Character.MaMuppet builds other players from mods/cyberpunkmp/player_ma_fpp.ent, which has not been measured" `
        -NotPassing "built only from"

    New-Sandbox
    Edit-Record "Character.WaMuppet" {
        param($line)
        if ($line -match '^\s*//' -or $line -notmatch '\.ent"') { $line } else { "// $line" }
    }
    Expect "the female record loses every template line and falls back to its parent's" (Invoke-Check) `
        -Fails 1 -Naming "Character.WaMuppet names no entity template of its own" -NotPassing "built only from"

    New-Sandbox
    Edit-Text $tweakPath { param($text) [regex]::Replace($text, '(?m)^Character\.MaMuppet(\s*:)', 'Character.MaMuppetRenamed$1') }
    Expect "a spawned record is no longer defined in the tweak" (Invoke-Check) `
        -Fails 1 -Naming "Character.MaMuppet is what other players are spawned from" -NotPassing "built only from"

    New-Sandbox
    Edit-Text $settingsPath { param($text) "// emptied by the test`n" }
    Edit-Text $worldScriptPath { param($text) "// emptied by the test`n" }
    Expect "the client stops naming its spawn records where the check looks: it says it is blind" (Invoke-Check) `
        -Fails 1 -Naming "could not find the records other players are spawned from" -NotPassing "built only from"

    # --- the game on this machine ---------------------------------------------------------------
    New-Sandbox
    Expect "a newer game installed here is a note, never a failure" (Invoke-Check -InstalledPatch "9.9") `
        -Notes 1 -Naming "the game installed here is 9.9"
    Expect "the same patch installed here is an ok line" (Invoke-Check -InstalledPatch $gamePatch) `
        -Passing "the game installed here is $gamePatch"
    Expect "no game installed here says nothing about it" (Invoke-Check) `
        -NotPassing "installed here"
}
finally {
    Remove-Sandbox
}

Write-Output ""
if ($script:failing) {
    Write-Output "$($script:failing) of $($script:cases) case(s) FAILING"
    exit 1
}
Write-Output "$($script:cases) case(s): every failure fires on the thing it exists to catch"
exit 0
