# tools\CheckOneGrid.ps1: does each failure still fire on the thing it exists to catch?
#
# A gate nobody has watched fail is a gate nobody knows works. This copies the source files the
# check reads into a throwaway folder, breaks the copy one way at a time - the 2026-11-11 private
# 60000 put back, a cell worked out by hand, the advertised size turned into a literal, the
# client insisting on a number - and requires the exact failure each time. The tree itself is
# never touched, so it is safe beside another session working in the same checkout.
#
# The copy is made from the REAL files, so this also proves the check still reads the shapes
# those files have today. A case whose edit no longer finds the line it breaks FAILS saying so,
# rather than passing for the wrong reason. If the unbroken copy already fails, the tree is what
# is wrong: Verify prints the same failure, and the cases after it mean nothing until it is fixed.
#
# Needs nothing but PowerShell - no game, no compiler. Verify runs it; by hand:
#
#   powershell -NoProfile -File tools\tests\one_grid_test.ps1
#   powershell -NoProfile -File tools\tests\one_grid_test.ps1 -Show    # also print each failure as it reads
param(
    [string]$Repo = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),

    # Print what the check said for each broken copy - the text somebody reads when it fires.
    [switch]$Show
)

$ErrorActionPreference = 'Stop'

$check = Join-Path $Repo "tools\CheckOneGrid.ps1"
$serverDir = "code\server\native"
$clientDir = "code\client"
$movementPath = "code\server\native\Components\MovementComponent.cpp"
$vehiclePath = "code\server\native\Components\VehicleComponent.cpp"
$levelHeaderPath = "code\server\native\Game\Level.h"
$gameServerPath = "code\server\native\GameServer.cpp"
$clientLoginPath = "code\client\App\Network\NetworkService.cpp"
$clientVehiclePath = "code\client\App\World\VehicleSystem.cpp"

$repoFull = (Resolve-Path -LiteralPath $Repo).Path.TrimEnd('\')

# The same files the check reads, found the same way: C++ sources that mention a cell.
function Find-CellFiles([string]$Relative) {
    $folder = Join-Path $repoFull $Relative
    Get-ChildItem -Path $folder -Recurse -File |
        Where-Object { $_.Extension -eq '.cpp' -or $_.Extension -eq '.h' -or $_.Extension -eq '.hpp' } |
        Where-Object { [System.IO.File]::ReadAllText($_.FullName).IndexOf('cell', [System.StringComparison]::OrdinalIgnoreCase) -ge 0 } |
        ForEach-Object { $_.FullName.Substring($repoFull.Length + 1) }
}

$serverFiles = @(Find-CellFiles $serverDir)
$clientFiles = @(Find-CellFiles $clientDir)

$tempRoot = [System.IO.Path]::GetTempPath()
$sandbox = Join-Path $tempRoot ("nco-one-grid-" + [guid]::NewGuid().ToString('N').Substring(0, 8))

function Remove-Sandbox {
    # Only ever a folder this script made, under the temp directory.
    if ($sandbox.StartsWith($tempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and
        $sandbox -like '*nco-one-grid-*' -and (Test-Path -LiteralPath $sandbox)) {
        Remove-Item -LiteralPath $sandbox -Recurse -Force
    }
}

# A fresh copy for every case, so no case inherits the last one's damage.
function New-Sandbox {
    Remove-Sandbox
    foreach ($relative in (@($serverFiles) + @($clientFiles))) {
        $target = Join-Path $sandbox $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath (Join-Path $repoFull $relative) -Destination $target -Force
    }
}

# A mutation that changes nothing would let its case pass for the wrong reason, so it is recorded
# and the case fails saying so.
$script:mutationProblem = $null

# Replaces text that MUST be there. A file that has changed shape is reported, never skipped.
function Swap([string]$Text, [string]$Old, [string]$New) {
    if ($Text.IndexOf($Old, [System.StringComparison]::Ordinal) -lt 0) {
        $script:mutationProblem = "the copy no longer contains '$Old' - the file has changed shape and this case needs rewriting"
        return $Text
    }
    $Text.Replace($Old, $New)
}

function Edit-Text {
    param([string]$Relative, [scriptblock]$Change)
    $path = Join-Path $sandbox $Relative
    $before = [System.IO.File]::ReadAllText($path)
    $after = [string](& $Change $before)
    if ($after -ceq $before -and -not $script:mutationProblem) {
        $script:mutationProblem = "the edit to the copy of $Relative changed nothing - the file no longer has the shape this case expects"
    }
    [System.IO.File]::WriteAllText($path, $after)
}

# Renames a word in every copied file under a folder that has it; reports when none had it.
function Rename-Everywhere([string[]]$Files, [string]$Old, [string]$New) {
    $touched = 0
    foreach ($relative in $Files) {
        $path = Join-Path $sandbox $relative
        $before = [System.IO.File]::ReadAllText($path)
        if ($before.IndexOf($Old, [System.StringComparison]::Ordinal) -lt 0) { continue }
        [System.IO.File]::WriteAllText($path, $before.Replace($Old, $New))
        $touched++
    }
    if ($touched -eq 0) { $script:mutationProblem = "no copied file contains '$Old' - this case no longer breaks anything" }
}

function Invoke-Check { @(& $check -Root $sandbox) }

$script:cases = 0
$script:failing = 0

function Expect {
    param(
        [Parameter(Mandatory)][string]$Case,
        [object[]]$Findings = @(),
        [int]$Fails = 0,
        [string[]]$Naming = @(),      # each must appear in the summary of a failure
        [string[]]$Passing = @(),     # each must appear in an ok line
        [string[]]$NotPassing = @()   # none may appear in an ok line
    )

    $script:cases++
    $failed = @($Findings | Where-Object { $_.Kind -eq 'fail' })
    $passed = @($Findings | Where-Object { $_.Kind -eq 'pass' })
    $reasons = @()

    if ($script:mutationProblem) { $reasons += $script:mutationProblem; $script:mutationProblem = $null }
    if ($failed.Count -ne $Fails) { $reasons += "expected $Fails failure(s), the check reported $($failed.Count)" }

    foreach ($text in $Naming) {
        if (-not ($failed | Where-Object { $_.Summary.IndexOf($text, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 })) {
            $reasons += "no failure names '$text'"
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
    foreach ($finding in $failed) {
        if (-not $finding.What -or -not $finding.Where -or -not $finding.Fix) {
            $reasons += "'$($finding.Summary)' is missing its what, where or fix"
        }
    }

    if (-not $reasons) {
        Write-Output "  ok    $Case"
        if ($Show) {
            foreach ($finding in $failed) {
                Write-Output "          [fail] $($finding.Summary)"
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
    Write-Output "        where  tools\CheckOneGrid.ps1, against a copy of the tree broken as the case says"
    Write-Output "        fix    the check reported what follows; make it report what the case expects, or correct the case in tools\tests\one_grid_test.ps1"
    if (-not $Findings) { Write-Output "               (the check reported nothing at all)" }
    foreach ($finding in $Findings) { Write-Output "               [$($finding.Kind)] $($finding.Summary)" }
}

$declaredOnce = "declared once, as Level::kCellSize"
$stampsFromGrid = "each taken from Level::ToCell"
$toldTheConstant = "clients are told Level::kCellSize"
$clientUsesIt = "from the size it is told"

Write-Output "CheckOneGrid, against a copy of the tree broken one way at a time ($($serverFiles.Count) server and $($clientFiles.Count) client file(s) mention a cell)"

try {
    # --- the copy as it is ---------------------------------------------------------------------
    New-Sandbox
    Expect "an unbroken copy is clean, the client's sanity bound included" (Invoke-Check) `
        -Passing $declaredOnce, $stampsFromGrid, $toldTheConstant, $clientUsesIt

    New-Sandbox
    Edit-Text $movementPath { param($text) "/* constexpr float kWorldCellSize = 60000.f;`n   message.set_cell_x(ToSnapshotCell(x)); */`n" + $text }
    Edit-Text $clientLoginPath { param($text) "// the client used to demand settings.get_cell_size() == 60000`n" + $text }
    Expect "the old numbers written in COMMENTS are prose, not copies" (Invoke-Check) `
        -Passing $declaredOnce, $stampsFromGrid, $toldTheConstant, $clientUsesIt

    # --- the server: a second copy of the size -------------------------------------------------
    New-Sandbox
    Edit-Text $movementPath {
        param($text)
        $text = Swap $text '#include <cmath>' ("#include <cmath>`n" +
            "constexpr float kWorldCellSize = 60000.f;`n" +
            "int32_t ToSnapshotCell(const float aCoordinate) noexcept { return static_cast<int32_t>(std::floor(aCoordinate / kWorldCellSize)); }")
        $text = Swap $text 'const auto cell = Level::ToCell(aComponent.Position);' ''
        $text = Swap $text 'message.set_cell_x(cell.x);' 'message.set_cell_x(ToSnapshotCell(aComponent.Position.x));'
        $text = Swap $text 'message.set_cell_y(cell.y);' 'message.set_cell_y(ToSnapshotCell(aComponent.Position.y));'
        $text
    }
    Expect "the 2026-11-11 mistake put back: a private 60000 and both stamps worked out from it" (Invoke-Check) `
        -Fails 3 -Naming "kWorldCellSize is a second copy of the cell size", "stamped from something other than Level::ToCell" `
        -NotPassing $declaredOnce, $stampsFromGrid

    New-Sandbox
    Edit-Text $gameServerPath { param($text) "constexpr float kGridCellSize = 6000.f;`n" + $text }
    Expect "a new cell size constant in another server file, even with today's number" (Invoke-Check) `
        -Fails 1 -Naming "kGridCellSize is a second copy of the cell size" -NotPassing $declaredOnce

    # --- the server: a stamp that does not come from the grid -----------------------------------
    New-Sandbox
    Edit-Text $vehiclePath {
        param($text)
        Swap $text 'load.set_cell_x(cell.x);' 'load.set_cell_x(static_cast<int32_t>(std::floor(aMovementComponent.Position.x / 6000.f)));'
    }
    Expect "a cell worked out by hand from an unnamed 6000" (Invoke-Check) `
        -Fails 1 -Naming "stamped from something other than Level::ToCell" -Passing $declaredOnce -NotPassing $stampsFromGrid

    New-Sandbox
    Edit-Text $movementPath {
        param($text)
        Swap $text 'const auto cell = Level::ToCell(aComponent.Position);' 'const glm::ivec2 cell{0, 0};'
    }
    Expect "a variable called cell that never came from ToCell" (Invoke-Check) `
        -Fails 2 -Naming "stamped from something other than Level::ToCell" -NotPassing $stampsFromGrid

    New-Sandbox
    Edit-Text $vehiclePath { param($text) Swap $text 'load.set_cell_x(cell.x);' 'load.set_cell_x(cell.y);' }
    Expect "x stamped from y" (Invoke-Check) `
        -Fails 1 -Naming "stamped from something other than Level::ToCell" -NotPassing $stampsFromGrid

    # --- the server: what clients are told ------------------------------------------------------
    New-Sandbox
    Edit-Text $gameServerPath {
        param($text)
        Swap $text 'set_cell_size(static_cast<uint32_t>(Level::kCellSize))' 'set_cell_size(6000)'
    }
    Expect "the 2026-08-24 mistake: the advertised size is a literal" (Invoke-Check) `
        -Fails 1 -Naming "clients are told a cell size that is not Level::kCellSize" -NotPassing $toldTheConstant

    # --- the client -----------------------------------------------------------------------------
    New-Sandbox
    Edit-Text $clientLoginPath {
        param($text)
        Swap $text 'if (settings.get_cell_size() == 0 ||' 'if (settings.get_cell_size() != 60000 ||'
    }
    Expect "the 2026-08-30 mistake: the client insists on one number" (Invoke-Check) `
        -Fails 1 -Naming "the client compares the cell size against 60000" -NotPassing $clientUsesIt

    New-Sandbox
    Edit-Text $clientVehiclePath { param($text) "constexpr float kCellSize = 6000.f;`n" + $text }
    Expect "the client keeps a cell size of its own" (Invoke-Check) `
        -Fails 1 -Naming "the client declares a cell size of its own, kCellSize" -NotPassing $clientUsesIt

    # --- the check going blind ------------------------------------------------------------------
    New-Sandbox
    Edit-Text $levelHeaderPath { param($text) Swap $text 'kCellSize' 'kGridUnit' }
    Expect "Level.h no longer declares kCellSize: it says it has nothing to start from" (Invoke-Check) `
        -Fails 1 -Naming "could not find Level::kCellSize" -NotPassing $declaredOnce

    New-Sandbox
    Rename-Everywhere $serverFiles 'set_cell_x(' 'set_grid_x('
    Rename-Everywhere $serverFiles 'set_cell_y(' 'set_grid_y('
    Expect "the stamp fields are renamed: it says it is checking nothing" (Invoke-Check) `
        -Fails 1 -Naming "could not find where the server stamps a cell" -NotPassing $stampsFromGrid

    New-Sandbox
    Rename-Everywhere $serverFiles 'set_cell_size(' 'set_grid_size('
    Expect "the advertised field is renamed: it says it is checking nothing" (Invoke-Check) `
        -Fails 1 -Naming "could not find where the server tells clients the cell size" -NotPassing $toldTheConstant

    New-Sandbox
    Rename-Everywhere $clientFiles 'get_cell_size' 'get_grid_size'
    Expect "the client stops reading the advertised size where the check looks: it says so" (Invoke-Check) `
        -Fails 1 -Naming "could not find the client reading the advertised cell size" -NotPassing $clientUsesIt
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
