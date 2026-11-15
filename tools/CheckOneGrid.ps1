# CheckOneGrid.ps1 - the cell grid is one number, declared once.
#
# WHY THIS EXISTS
#
# The server stamps a cell on what it sends and tells the client how big a cell is. The client
# works the cell out again from the position and DROPS the message when the two differ. So every
# copy of the cell size has to be the same number, and three times one was not:
#
#   2026-08-24  the size advertised to clients was a literal Level did not use. Nobody could see
#               anybody, for a week.
#   2026-08-30  the client demanded cell_size == 60000 exactly after the server moved to 6000.
#               Every login was refused.
#   2026-11-11  movement snapshots and vehicle loads were stamped from a private 60000 in two
#               component files. It agreed with 6000 only within 6000 units of the origin, and
#               every snapshot would have been dropped the day Level::kCellSize changed.
#
# Each time, a comment already said "one place". A comment did not hold, so this is a check.
#
# WHAT IT REQUIRES
#
#   1. code\server\native declares a cell size in Game\Level.h (kCellSize) and nowhere else,
#      apart from Level.cpp's alias of that same constant.
#   2. Every set_cell_x / set_cell_y there passes the .x / .y of a value that came from ToCell.
#   3. The size clients are told (set_cell_size) is Level::kCellSize.
#   4. The client reads the size it is told, never compares it against a number of its own, and
#      declares no size of its own beyond the sanity bound.
#
# It reads code, not comments: prose about an old number is not a copy of it.
#
# HOW IT REPORTS
#
# One object per finding - Kind is fail or pass - and nothing printed. Verify turns them into its
# own what / where / fix lines, and tools\tests\one_grid_test.ps1 breaks a copy of the tree one
# way at a time to prove each failure still fires.
#
#   .\tools\CheckOneGrid.ps1
#
[CmdletBinding()]
param(
    # The tree to check. The test points this at a throwaway copy.
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

function Finding {
    param([string]$Kind, [string]$Summary, [string]$What, [string]$Where, [string]$Fix)
    [pscustomobject]@{ Kind = $Kind; Summary = $Summary; What = $What; Where = $Where; Fix = $Fix }
}

$serverDir = "code\server\native"
$clientDir = "code\client"
$levelHeader = "code\server\native\Game\Level.h"
$levelSource = "code\server\native\Game\Level.cpp"

# The one name the client may give a number of its own: the bound past which an advertised size
# is treated as a typo. It rejects nonsense; it is never used to work out a cell.
$clientSanityBound = 'kMaxSaneCellSize'

$rootFull = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\')

# Every line of code under a folder that mentions a cell, comments blanked. Everything this
# check looks for has "cell" in its name, so a line without the word cannot matter to it.
function Read-CellLines([string]$Relative) {
    $folder = Join-Path $rootFull $Relative
    if (-not (Test-Path -LiteralPath $folder -PathType Container)) { return }

    $files = Get-ChildItem -Path $folder -Recurse -File |
        Where-Object { $_.Extension -eq '.cpp' -or $_.Extension -eq '.h' -or $_.Extension -eq '.hpp' } |
        Sort-Object FullName

    foreach ($file in $files) {
        $text = [System.IO.File]::ReadAllText($file.FullName)
        if ($text.IndexOf('cell', [System.StringComparison]::OrdinalIgnoreCase) -lt 0) { continue }

        $relativeFile = $file.FullName.Substring($rootFull.Length + 1)
        $inBlock = $false
        $number = 0

        foreach ($raw in ($text -split "`n")) {
            $number++
            $rest = $raw.TrimEnd("`r")
            $code = ''

            while ($rest.Length -gt 0) {
                if ($inBlock) {
                    $end = $rest.IndexOf('*/')
                    if ($end -lt 0) { $rest = '' }
                    else { $rest = $rest.Substring($end + 2); $inBlock = $false }
                }
                else {
                    $block = $rest.IndexOf('/*')
                    $slash = $rest.IndexOf('//')
                    if ($slash -ge 0 -and ($block -lt 0 -or $slash -lt $block)) {
                        $code += $rest.Substring(0, $slash)
                        $rest = ''
                    }
                    elseif ($block -ge 0) {
                        $code += $rest.Substring(0, $block)
                        $rest = $rest.Substring($block + 2)
                        $inBlock = $true
                    }
                    else {
                        $code += $rest
                        $rest = ''
                    }
                }
            }

            if ($code.IndexOf('cell', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                [pscustomobject]@{ File = $relativeFile; Number = $number; Code = $code }
            }
        }
    }
}

$sizeName = '\w*[Cc]ell_?[Ss]ize\w*'
$serverLines = @(Read-CellLines $serverDir)
$clientLines = @(Read-CellLines $clientDir)

# --- 1. one declaration -------------------------------------------------------------------------
$declared = $false
$extraDeclaration = $false

foreach ($line in $serverLines) {
    foreach ($match in [regex]::Matches($line.Code, "\b($sizeName)\s*=(?!=)")) {
        $name = $match.Groups[1].Value

        if ($line.File -eq $levelHeader -and $name -ceq 'kCellSize') { $declared = $true; continue }
        if ($line.File -eq $levelSource -and $name -ceq 'sCellSize' -and $line.Code -match 'Level::kCellSize') { continue }

        $extraDeclaration = $true
        Finding fail "$name is a second copy of the cell size" `
            "the client drops everything stamped with a cell it cannot work out from the size it was told, so a copy that drifts from Level::kCellSize takes other players or their cars out of the world with nothing in the server log." `
            "$($line.File):$($line.Number)" `
            "delete it and use Level::kCellSize, or Level::ToCell for a cell. Game\Level.h is the one declaration"
    }
}

if (-not $declared) {
    Finding fail "could not find Level::kCellSize" `
        "this check starts from that declaration. Without it, it cannot tell the grid from a copy of it, and a pass here would mean nothing." `
        $levelHeader `
        "if the constant was renamed or moved, name the new home at the top of tools\CheckOneGrid.ps1"
}
elseif (-not $extraDeclaration) { Finding pass "the cell size is declared once, as Level::kCellSize" }

# --- 2. every stamp comes from the grid ---------------------------------------------------------
$fromGrid = @{}
foreach ($line in $serverLines) {
    foreach ($match in [regex]::Matches($line.Code, '\b(\w+)\s*=\s*(?:Level::)?ToCell\s*\(')) {
        $fromGrid["$($line.File)|$($match.Groups[1].Value)"] = $true
    }
}

$stamps = 0
$stampFiles = @{}
$badStamp = $false

foreach ($line in $serverLines) {
    if ($line.Code -notmatch '\bset_cell_[xy]\s*\(') { continue }

    $stamps++
    $stampFiles[$line.File] = $true

    $shape = [regex]::Match($line.Code, '\bset_cell_([xy])\s*\(\s*(\w+)\.([xy])\s*\)')
    $ok = $shape.Success -and
          $shape.Groups[1].Value -ceq $shape.Groups[3].Value -and
          $fromGrid.ContainsKey("$($line.File)|$($shape.Groups[2].Value)")

    if (-not $ok) {
        $badStamp = $true
        Finding fail "a cell is stamped from something other than Level::ToCell" `
            "the client works this cell out again at the advertised size and drops the message when its answer differs - so this entity stops moving, or never appears, wherever the two disagree." `
            "$($line.File):$($line.Number)  $($line.Code.Trim())" `
            "take the cell from the grid: const auto cell = Level::ToCell(position); then set_cell_x(cell.x) and set_cell_y(cell.y)"
    }
}

if ($stamps -eq 0) {
    Finding fail "could not find where the server stamps a cell" `
        "this check looks for set_cell_x / set_cell_y under $serverDir. Finding none means it is checking nothing, not that nothing is wrong." `
        $serverDir `
        "if the fields were renamed, name the new ones in tools\CheckOneGrid.ps1"
}
elseif (-not $badStamp) { Finding pass "$stamps cell stamp(s) in $($stampFiles.Count) file(s), each taken from Level::ToCell" }

# --- 3. the size clients are told ---------------------------------------------------------------
$advertised = 0
$badAdvert = $false

foreach ($line in $serverLines) {
    if ($line.Code -notmatch '\bset_cell_size\s*\(') { continue }

    $advertised++
    if ($line.Code -notmatch 'Level::kCellSize') {
        $badAdvert = $true
        Finding fail "clients are told a cell size that is not Level::kCellSize" `
            "the server stamps cells at one size and tells clients another, so every client drops what it is sent: on 2026-08-24 that was nobody able to see anybody." `
            "$($line.File):$($line.Number)  $($line.Code.Trim())" `
            "advertise the constant: set_cell_size(static_cast<uint32_t>(Level::kCellSize))"
    }
}

if ($advertised -eq 0) {
    Finding fail "could not find where the server tells clients the cell size" `
        "this check looks for set_cell_size under $serverDir. Finding none means it is checking nothing." `
        $serverDir `
        "if the field was renamed, name the new one in tools\CheckOneGrid.ps1"
}
elseif (-not $badAdvert) { Finding pass "clients are told Level::kCellSize" }

# --- 4. the client uses what it is told ---------------------------------------------------------
$reads = 0
$badClient = $false
$told = "(?:get_cell_size\s*\(\s*\)|\b$sizeName)"
$number = "([0-9][0-9']*)"

foreach ($line in $clientLines) {
    if ($line.Code -match '\bget_cell_size\s*\(') { $reads++ }

    $own = @()
    foreach ($match in [regex]::Matches($line.Code, "$told\s*(?:==|!=)\s*$number")) { $own += $match.Groups[1].Value }
    foreach ($match in [regex]::Matches($line.Code, "$number\s*(?:==|!=)\s*(?:\w+(?:\.|->))*$told")) { $own += $match.Groups[1].Value }
    $own = @($own | Where-Object { [decimal]($_ -replace "'", '') -ne 0 })

    foreach ($value in $own) {
        $badClient = $true
        Finding fail "the client compares the cell size against $value" `
            "the server is the authority on its own grid. A client that insists on a number refuses every server that changes it: on 2026-08-30 that was every login." `
            "$($line.File):$($line.Number)  $($line.Code.Trim())" `
            "compare against nothing but zero; work from settings.get_cell_size()"
    }

    foreach ($match in [regex]::Matches($line.Code, "\b($sizeName)\s*=(?!=)\s*[0-9]")) {
        $name = $match.Groups[1].Value
        if ($name -ceq $clientSanityBound) { continue }

        $badClient = $true
        Finding fail "the client declares a cell size of its own, $name" `
            "a cell worked out from this instead of the advertised size disagrees with the server's stamp the day the server's number moves, and the message is dropped." `
            "$($line.File):$($line.Number)  $($line.Code.Trim())" `
            "read settings.get_cell_size() instead"
    }
}

if ($reads -eq 0) {
    Finding fail "could not find the client reading the advertised cell size" `
        "this check looks for get_cell_size() under $clientDir. Finding none means the client is not using what it is told, or this check is looking in the wrong place." `
        $clientDir `
        "if the field was renamed, name the new one in tools\CheckOneGrid.ps1"
}
elseif (-not $badClient) { Finding pass "the client works every cell out from the size it is told ($reads read(s))" }
