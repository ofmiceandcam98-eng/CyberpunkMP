# Verify.ps1 - the audit and the unit tests, in one command.
#
# WHY THIS EXISTS
#
# Cam, 2026-09-03: "make sure you do what we just did any time we build something new...
# we have to be SURE they work so thats less times i have to come back to you about an
# issue."
#
# An audit of one day's work found two bugs that compiled perfectly and that no build would
# ever have caught:
#
#   - /call had TWO dispatches. An older deprecation stub matched first and returned, so the
#     new player-to-player call command was dead code - "/call 555-014-372" answered "use
#     your phone" and rang nobody.
#   - CreateCharacterRequest was declared, took a oneof slot, and was never sent or handled.
#
# Both had been reported as working. Every check below maps to a failure that has actually
# happened on this project, not to a category of bug in the abstract.
#
#   .\tools\Verify.ps1            # everything
#   .\tools\Verify.ps1 -SkipTests # static checks only (fast, no compiler needed)
#
[CmdletBinding()]
param(
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot "Environment.ps1")
Set-Location $Repo

$problems = @()
function Fail($m) { $script:problems += $m; Write-Host "  FAIL  $m" -ForegroundColor Red }
function Pass($m) { Write-Host "  ok    $m" -ForegroundColor DarkGray }
function Head($m) { Write-Host "`n$m" -ForegroundColor Cyan }

# ---------------------------------------------------------------------------
Head "BOM"
# Set-Content -Encoding UTF8 on PowerShell 5.1 writes a BOM. In a .reds it breaks the whole
# compilation with "syntax error at 1:1", which names nothing and reads like a mystery.
$bom = @()
foreach ($root in @("code\assets\redscript", "distrib\launcher\mod\assets\redscript", "code\server\native", "code\client")) {
    if (-not (Test-Path $root)) { continue }
    Get-ChildItem $root -Recurse -Include *.reds,*.h,*.cpp -ErrorAction SilentlyContinue | ForEach-Object {
        $b = [System.IO.File]::ReadAllBytes($_.FullName)
        if ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF) { $bom += $_.FullName }
    }
}
if ($bom) { $bom | ForEach-Object { Fail "BOM in $_" } } else { Pass "no BOM in any source file" }

# ---------------------------------------------------------------------------
Head "redscript natives vs C++ RTTI"
# A native declared in redscript with no RTTI_METHOD behind it fails at LOAD, not compile,
# and takes every script in the mod down with it - the game then starts with no scripts at
# all while looking like the mod simply did nothing.
$reds = "code\assets\redscript\World\NetworkWorldSystem.reds"
$hdr  = "code\client\App\World\NetworkWorldSystem.h"
$declared = (Select-String -Path $reds -Pattern 'public native func (\w+)' -AllMatches).Matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$exposed  = (Select-String -Path $hdr  -Pattern 'RTTI_METHOD\((\w+)\)'     -AllMatches).Matches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$orphan = $declared | Where-Object { $exposed -notcontains $_ }
if ($orphan) { $orphan | ForEach-Object { Fail "native '$_' declared in redscript, not exposed from C++" } }
else { Pass "$($declared.Count) natives all resolve" }

# ---------------------------------------------------------------------------
Head "chat command dispatch"
# Two features wanting the same verb: the first match returns and the second is dead code.
# Compare by INDENTATION - a nested branch inside a compound `if` is not a duplicate.
$chat = "code\server\native\Systems\ChatSystem.cpp"
$top = @{}
$dupes = @()
Select-String -Path $chat -Pattern 'if \(command == "(/[a-z]+)"' | ForEach-Object {
    $indent = $_.Line.Length - $_.Line.TrimStart().Length
    if ($indent -le 4) {
        $c = $_.Matches[0].Groups[1].Value
        if ($top.ContainsKey($c)) { $dupes += "$c (lines $($top[$c]) and $($_.LineNumber))" }
        else { $top[$c] = $_.LineNumber }
    }
}
if ($dupes) { $dupes | ForEach-Object { Fail "duplicate dispatch: $_ - the second is unreachable" } }
else { Pass "$($top.Count) commands, each dispatching once" }

# ---------------------------------------------------------------------------
Head "protocol"
foreach ($p in @("client","server")) {
    $lines = Get-Content "code\protocol\$p.proto"
    $start = ($lines | Select-String -Pattern 'oneof content' | Select-Object -First 1).LineNumber
    $nums = @()
    for ($i = $start; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '=\s*(\d+);') { $nums += [int]$Matches[1] }
        if ($lines[$i] -match '^\s*\}') { break }
    }
    $d = $nums | Group-Object | Where-Object Count -gt 1
    if ($d) { Fail "$p.proto oneof has duplicate field numbers: $($d.Name -join ',')" }
    else { Pass "$p.proto oneof: $($nums.Count) entries, no duplicate numbers" }
}

# Every client request needs a handler, or it is silently ignored on arrival.
$requests = (Select-String -Path "code\protocol\client.proto" -Pattern '^message (\w+Request) \{' -AllMatches).Matches | ForEach-Object { $_.Groups[1].Value }
$handlers = (Get-ChildItem "code\server\native" -Recurse -Include *.cpp | Select-String -Pattern 'RegisterHandler<&\w+::Handle(\w+)>' -AllMatches).Matches | ForEach-Object { $_.Groups[1].Value }
# Handlers are not always named after the whole message - GameServer::HandleAuthentication
# takes an AuthenticationRequest. Match on the stem with "Request" optional, or this reports
# working code as broken, which is the fastest way to make a check nobody trusts.
$unhandled = $requests | Where-Object {
    $stem = $_ -replace 'Request$',''
    ($handlers -notcontains $_) -and ($handlers -notcontains $stem)
}
if ($unhandled) { $unhandled | ForEach-Object { Fail "$_ is declared but nothing handles it" } }
else { Pass "$($requests.Count) client requests, all handled" }

# ---------------------------------------------------------------------------
Head "server wiring"
$gs = Get-Content "code\server\native\GameServer.cpp" -Raw
foreach ($s in @('m_players.Load','m_bans.Load','m_vehicles.Load','m_worldFacts.Load','m_messages.Load','m_calls.Load')) {
    if ($gs -notmatch [regex]::Escape($s)) { Fail "$s is never called - that store loads nothing" }
}
foreach ($t in @('SavePlayerPositions','EnforceJail','ExpireCalls')) {
    if ($gs -notmatch "$t\(now\)") { Fail "$t is not driven from OnUpdate" }
}
$cs = Get-Content $chat -Raw
foreach ($t in @('TickCalls','TickTrades','TickMedical')) {
    if ($gs -notmatch "$t\(\)") { Fail "$t is not driven from OnUpdate" }
}
if (-not $problems) { Pass "stores load and ticks are wired" }

# ---------------------------------------------------------------------------
if (-not $SkipTests) {
    Head "unit tests"

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars) -and (Test-Path $vswhere)) {
        $vs = & $vswhere -latest -property installationPath
        if ($vs) { $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat" }
    }

    if (-not (Test-Path $vcvars)) {
        Write-Host "  !!    no MSVC found - skipping tests" -ForegroundColor Yellow
    }
    else {
        $out = Join-Path $env:TEMP "nco-tests"
        New-Item -ItemType Directory -Force $out | Out-Null
        $src = Join-Path $Repo "code\server\native"
        $json = (Get-ChildItem "$env:USERPROFILE\AppData\Local\.xmake\packages\n\nlohmann_json" -Recurse -Filter "json.hpp" -ErrorAction SilentlyContinue |
                 Select-Object -First 1).Directory.Parent.FullName

        foreach ($t in (Get-ChildItem (Join-Path $PSScriptRoot "tests") -Filter *.cpp)) {
            $exe = Join-Path $out "$($t.BaseName).exe"
            $inc = if ($json) { "/I `"$json`"" } else { "" }
            # NOTE the doubled backslash before the closing quote. cmd treats \" as an
            # escaped quote, so /Fo:"$out\" swallows the rest of the line and cl reports
            # "D8003: missing source filename" - which looks like the test is at fault.
            $r = cmd /c "`"$vcvars`" >nul 2>&1 && cl /nologo /std:c++17 /EHsc $inc /I `"$src`" /Fe:`"$exe`" /Fo:`"$out\\`" `"$($t.FullName)`" 2>&1"
            if ($LASTEXITCODE -ne 0) { Fail "$($t.BaseName) did not compile"; $r | Select-Object -Last 4 | ForEach-Object { Write-Host "        $_" -ForegroundColor DarkRed }; continue }

            $r = & $exe
            $ok = ($r | Select-String -Pattern "^ok").Count
            $no = ($r | Select-String -Pattern "^FAIL").Count
            if ($no -gt 0 -or $LASTEXITCODE -ne 0) {
                Fail "$($t.BaseName): $no failing"
                $r | Select-String -Pattern "^FAIL" | ForEach-Object { Write-Host "        $_" -ForegroundColor DarkRed }
            }
            else { Pass "$($t.BaseName): $ok checks" }
        }
    }
}

# ---------------------------------------------------------------------------
Write-Host ""
if ($problems) {
    Write-Host "$($problems.Count) problem(s)." -ForegroundColor Red
    exit 1
}
Write-Host "VERIFIED - nothing above needs a human." -ForegroundColor Green
Write-Host "Still not covered: anything that needs the game running or two players." -ForegroundColor DarkGray
exit 0
