# Read a Cyberpunk crash dump and say where the client died, without a debugger.
#
# The game writes a full minidump for every crash and produces NO Windows Error Reporting
# event, so Event Viewer looks empty and the process appears to die silently. The dumps
# live in %LOCALAPPDATA%\REDEngine\ReportQueue\Cyberpunk2077-<date>-<time>-<pid>-<tid>\,
# and the folder timestamp matches the client log's last line.
#
# Run with no arguments to analyse the newest crash:
#
#   .\tools\AnalyseCrashDump.ps1
#
# No dump - only a crash log line like "Cyberpunk2077.exe+0x3E7863"? Name the address(es)
# straight from it, against the build on disk (the play machine uploads logs, not dumps):
#
#   .\tools\AnalyseCrashDump.ps1 -Rva 'Cyberpunk2077.exe+0x3E7863'
#   .\tools\AnalyseCrashDump.ps1 -Rva 'CyberpunkMP.dll+0x1A2B3C', 0x22ED40
#
# What it prints, and why each part matters:
#
#   * the exception and the address it tried to touch. A SMALL value here (0x23, 0x25) is
#     not a null dereference - decode the instruction and you will usually find a register
#     holding something like 0x11, i.e. a small integer being used as a pointer. That means
#     freed and reused memory, and the faulting address tells you where the damage was
#     TOUCHED, never where it was WRITTEN. Three fixes reasoned backwards from it on
#     27 August were all wrong for that reason. If you see this, look for the corruption
#     (mimalloc is built with secure mode and reports through [Heap] lines in the client
#     log) rather than adding a null check where it crashed.
#
#   * which loaded module owns the faulting address, so "is this us or the game" is
#     answered immediately - and whether the file on disk is the build that crashed.
#
#   * the bytes at the fault, so the instruction can be decoded by hand.
#
#   * the function's bounds from .pdata when it has an entry, and any string literals it
#     references - which is how a function gets named here, because the PDB will not load
#     (dbghelp reports SymType None even with a matching GUID and age, from local disk).
#
#   * for the game exe, the function's address hash from cyberpunk2077_addresses.json (the
#     game ships it beside the exe). The hash is what code binds with - Core::RawFunc<hash>
#     survives patches, the RVA does not. Looked up at the .pdata START, never nearest-lower:
#     an RVA mid-function matches nothing, and nearest-lower confidently names a neighbour.
#
# Three traps worth knowing:
#   - MINIDUMP_THREAD's stack descriptor is at +24, size +32, rva +36. Reading it at +16
#     lands on Teb and yields a bogus multi-hundred-MB range.
#   - /OPT:ICF folds small identical functions, so "who calls this" can return completely
#     unrelated code. Do not trust caller analysis for small leaf helpers. The same folding
#     is why one address can carry two hashes; both are printed.
#   - The optimiser splits cold blocks into their own .pdata entries (CHAININFO unwind).
#     The fault's entry is then a fragment; it is followed back to the real function start.
[CmdletBinding()]
param(
    # Defaults to the newest report in the queue.
    [string]$DumpPath,
    # Defaults to the deployed plugin DLL.
    [string]$Dll = 'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\red4ext\plugins\zzzCyberpunkMP\CyberpunkMP.dll',
    # The game exe; cyberpunk2077_addresses.json is read from beside it.
    [string]$GameExe = 'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe',
    # Addresses to name without a dump: 'Cyberpunk2077.exe+0x3E7863', 'CyberpunkMP.dll+0x1A2B',
    # or a bare RVA (0x3E7863), which means the game exe. [object[]], not [string[]]: PowerShell
    # turns an unquoted 0x3E7863 into the DECIMAL text "4094051", which would then parse as hex.
    [object[]]$Rva
)

$ErrorActionPreference = 'Stop'

# One read per file, however many addresses are named.
$script:images = @{}
$script:table = $null

function Read-Image([string]$path) {
    if ($script:images.ContainsKey($path)) { return $script:images[$path] }
    $d = [System.IO.File]::ReadAllBytes($path)
    $pe = [BitConverter]::ToUInt32($d,0x3C)
    $nSec = [BitConverter]::ToUInt16($d,$pe+6); $optSz = [BitConverter]::ToUInt16($d,$pe+20)
    $opt = $pe+24; $secStart = $opt + $optSz
    $secs = @()
    for ($i=0; $i -lt $nSec; $i++) {
        $s = $secStart + $i*40
        $secs += [pscustomobject]@{
            Index=$i+1
            Name=[System.Text.Encoding]::ASCII.GetString($d,$s,8).Trim([char]0)
            VA=[BitConverter]::ToUInt32($d,$s+12); VSz=[BitConverter]::ToUInt32($d,$s+8); Raw=[BitConverter]::ToUInt32($d,$s+20) }
    }
    $img = [pscustomobject]@{ Bytes=$d; Stamp=[BitConverter]::ToUInt32($d,$pe+8); DirOff=$opt+112; Secs=$secs }
    $script:images[$path] = $img
    $img
}

function Read-AddressTable([string]$path) {
    if ($null -eq $script:table) { $script:table = [System.IO.File]::ReadAllText($path) }
    $script:table
}

# Everything known about $rva inside the image at $path. $crashedStamp is the PE timestamp the
# dump recorded for the module, or $null when there is no dump to compare against.
function Describe-Image([string]$name, [string]$path, [uint64]$rva, $crashedStamp) {
    $img = Read-Image $path
    $d = $img.Bytes; $secs = $img.Secs; $dirOff = $img.DirOff
    function R2O([int64]$x) { foreach ($s in $secs) { if ($x -ge $s.VA -and $x -lt ($s.VA+$s.VSz)) { return [int]($s.Raw + ($x - $s.VA)) } } return -1 }
    function SecOf([int64]$x) { foreach ($s in $secs) { if ($x -ge $s.VA -and $x -lt ($s.VA+$s.VSz)) { return $s } } return $null }

    Write-Output ""
    Write-Output ("=== {0}+0x{1:X}  (on disk: {2}) ===" -f $name, $rva, $path)
    if ($null -ne $crashedStamp -and $crashedStamp -ne $img.Stamp) {
        Write-Output ("  WARNING: the file on disk is NOT the build that crashed (crashed 0x{0:X8}, on disk 0x{1:X8})." -f $crashedStamp, $img.Stamp)
        Write-Output "           Every byte, bound and name below describes a different build. Fix: analyse on the"
        Write-Output "           machine that crashed, or copy that build's file here and pass -Dll / -GameExe."
    } elseif ($null -eq $crashedStamp) {
        Write-Output ("  (no dump to compare: this assumes build 0x{0:X8} on disk is the one that crashed)" -f $img.Stamp)
    }

    $fo = R2O $rva
    if ($fo -lt 0) {
        Write-Output ("  0x{0:X} is outside every section of {1} - wrong module, or an address that was never code." -f $rva, $name)
        return
    }
    Write-Output ""
    Write-Output "--- bytes at the fault (decode by hand) ---"
    Write-Output ("  " + (($d[$fo..($fo+23)] | ForEach-Object { $_.ToString('X2') }) -join ' '))

    # .pdata is sorted by start address; the game exe has ~1M entries, so binary-search it.
    $pdRva = [BitConverter]::ToUInt32($d,$dirOff+3*8); $pdSz = [BitConverter]::ToUInt32($d,$dirOff+3*8+4)
    $pdOff = R2O $pdRva; $lo = 0; $hi = [int][Math]::Floor($pdSz/12) - 1
    $bg = 0; $en = 0; $unw = 0
    while ($lo -le $hi) {
        $mid = [int][Math]::Floor(($lo + $hi) / 2)
        $x = $pdOff + $mid*12
        $s1 = [BitConverter]::ToUInt32($d,$x); $s2 = [BitConverter]::ToUInt32($d,$x+4)
        if ($rva -lt $s1) { $hi = $mid - 1 }
        elseif ($rva -ge $s2) { $lo = $mid + 1 }
        else { $bg = $s1; $en = $s2; $unw = [BitConverter]::ToUInt32($d,$x+8); break }
    }

    # A split-off cold block has UNW_FLAG_CHAININFO (flags are the top 5 bits of byte 0); the
    # RUNTIME_FUNCTION it continues sits after the unwind codes, which are padded to an even count.
    $start = $bg
    for ($hop = 0; $bg -ne 0 -and $hop -lt 16; $hop++) {
        $u = R2O $unw
        if ($u -lt 0 -or (($d[$u] -shr 3) -band 0x4) -eq 0) { break }
        $codes = [int]$d[$u+2]; if ($codes % 2) { $codes++ }
        $c = $u + 4 + 2*$codes
        $start = [BitConverter]::ToUInt32($d,$c); $unw = [BitConverter]::ToUInt32($d,$c+8)
    }

    Write-Output ""
    if ($bg -eq 0) {
        Write-Output "--- function: no .pdata entry (a leaf function with no unwind data) ---"
        Write-Output "    Find its start by scanning back to the CC/C3 padding before it."
    } else {
        Write-Output ("--- function 0x{0:X}..0x{1:X} ({2} bytes), fault at +0x{3:X} ---" -f $bg,$en,($en-$bg),($rva-$bg))
        if ($start -ne $bg) {
            Write-Output ("    this is a split-off part of the function that starts at 0x{0:X} (chained unwind info)" -f $start)
        }
        # One section holds the whole range, so its file offsets run in step with the RVAs.
        $o0 = (R2O $bg) - $bg
        $seen = @{}
        for ($x = $bg; $x -lt $en; $x++) {
            $o = $o0 + $x
            if ($o -lt 0 -or $o+7 -ge $d.Length) { continue }
            if (($d[$o] -eq 0x48 -or $d[$o] -eq 0x4C) -and $d[$o+1] -eq 0x8D -and (($d[$o+2] -band 0xC7) -eq 0x05)) {
                $t = $x + 7 + [BitConverter]::ToInt32($d,$o+3)
                $ts = SecOf $t
                if ($ts -and $ts.Name -match 'rdata|data') {
                    $so = R2O $t
                    if ($so -ge 0) {
                        $end = $so; $ok = $true
                        while ($end -lt $d.Length -and ($end-$so) -lt 200 -and $d[$end] -ne 0) {
                            if ($d[$end] -lt 9 -or ($d[$end] -gt 13 -and $d[$end] -lt 32) -or $d[$end] -gt 126) { $ok = $false; break }
                            $end++
                        }
                        if ($ok -and ($end-$so) -ge 5) {
                            $str = [System.Text.Encoding]::ASCII.GetString($d,$so,$end-$so)
                            if (-not $seen.ContainsKey($str)) { $seen[$str] = $true; Write-Output ("    `"$str`"") }
                        }
                    }
                }
            }
        }
        if ($seen.Count -eq 0) { Write-Output "    (references no string literals - template or inline-heavy code)" }
    }

    if ($name -ne 'Cyberpunk2077.exe') { return }

    # --- the game's address hash for the function start ---
    Write-Output ""
    $tbl = Join-Path (Split-Path -Parent $path) 'cyberpunk2077_addresses.json'
    if ($bg -eq 0) { Write-Output "--- address hash: none without a function start (no .pdata entry) ---"; return }
    if (-not (Test-Path $tbl)) {
        Write-Output "--- address hash: no table at $tbl ---"
        Write-Output "    The game ships it beside the exe. Fix: verify the game files in Steam."
        return
    }
    $txt = Read-AddressTable $tbl
    $head = $txt.Substring(0, [Math]::Min(200, $txt.Length))
    if ($head -match '"Linker map timestamp": "([0-9a-fA-F]{8})' -and [Convert]::ToUInt32($Matches[1],16) -ne $img.Stamp) {
        Write-Output ("  WARNING: the address table is for build 0x{0}, the exe beside it is 0x{1:X8}: hashes below may" -f $Matches[1], $img.Stamp)
        Write-Output "           name the wrong function. Fix: verify the game files in Steam so the two match."
    }
    $sec = SecOf $start
    $key = '"offset": "{0:D4}:{1:x8}"' -f $sec.Index, ($start - $sec.VA)
    Write-Output ("--- address hash at the function start ({0}) ---" -f $key.Substring(11).Trim('"'))
    $hits = 0
    $i = $txt.IndexOf($key, [StringComparison]::Ordinal)
    while ($i -ge 0) {
        $o0 = $txt.LastIndexOf([char]'{', $i); $o1 = $txt.IndexOf([char]'}', $i)
        $entry = $txt.Substring($o0, $o1 - $o0 + 1)
        $h = if ($entry -match '"hash": "(\d+)"') { $Matches[1] } else { '?' }
        $sy = if ($entry -match '"symbol": "([^"]+)"') { $Matches[1] } else { '(unnamed - the table names only a few dozen functions)' }
        Write-Output ("    hash {0}  {1}" -f $h, $sy)
        $hits++
        $i = $txt.IndexOf($key, $o1, [StringComparison]::Ordinal)
    }
    if ($hits -eq 0) {
        Write-Output "    no entry at the function start - the table does not cover every function."
    } else {
        Write-Output "    Bind it by the hash (Core::RawFunc<hash, ...>), never by the RVA - the RVA moves every patch."
    }
}

if ($Rva) {
    foreach ($a in $Rva) {
        $mod = $null
        if ($a -isnot [string]) { $x = [uint64]$a }
        elseif ($a -match '^\s*(?:(?<mod>[^+\s]+)\+)?(?:0x)?(?<hex>[0-9a-fA-F]+)\s*$') {
            $mod = $Matches['mod']; $x = [Convert]::ToUInt64($Matches['hex'], 16)
        } else {
            throw "-Rva '$a' is not an address. Use 'Cyberpunk2077.exe+0x3E7863', 'CyberpunkMP.dll+0x1A2B' or a bare hex RVA."
        }
        if (-not $mod -or $mod -eq 'Cyberpunk2077.exe') { $nm = 'Cyberpunk2077.exe'; $img = $GameExe }
        elseif ($mod -eq 'CyberpunkMP.dll') { $nm = 'CyberpunkMP.dll'; $img = $Dll }
        else { throw "-Rva '$a': only Cyberpunk2077.exe and CyberpunkMP.dll can be named; $mod is somebody else's module." }
        if (-not (Test-Path $img)) { throw "-Rva '$a': $img not found. Pass -GameExe / -Dll with the build that crashed." }
        Describe-Image $nm $img $x $null
    }
    return
}

if (-not $DumpPath) {
    $q = Join-Path $env:LOCALAPPDATA 'REDEngine\ReportQueue'
    $newest = Get-ChildItem $q -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $newest) { throw "no crash reports under $q - crashes land on the machine that crashed; for a crash log line use -Rva" }
    $DumpPath = Join-Path $newest.FullName 'Cyberpunk2077.dmp'
    Write-Output ("report : " + $newest.Name)
}

$st = Join-Path (Split-Path -Parent $DumpPath) 'stacktrace.txt'
if (Test-Path $st) {
    Write-Output "--- stacktrace.txt (read this first) ---"
    Get-Content $st | ForEach-Object { "  $_" }
}

$b = [System.IO.File]::ReadAllBytes($DumpPath)
if ([System.Text.Encoding]::ASCII.GetString($b,0,4) -ne 'MDMP') { throw 'not a minidump' }

$n = [BitConverter]::ToUInt32($b,8); $dir = [BitConverter]::ToUInt32($b,12)
$streams = @{}
for ($i=0; $i -lt $n; $i++) {
    $o = $dir + $i*12
    # [int] key: the field is UInt32 and a UInt32 key never matches an Int32 literal.
    $streams[[int][BitConverter]::ToUInt32($b,$o)] = @([BitConverter]::ToUInt32($b,$o+4), [BitConverter]::ToUInt32($b,$o+8))
}
function WStr([int]$rva) {
    $l = [BitConverter]::ToUInt32($b,$rva)
    if ($l -le 0 -or $l -gt 2000) { return '' }
    [System.Text.Encoding]::Unicode.GetString($b,$rva+4,$l)
}

# MINIDUMP_MODULE is 108 bytes; TimeDateStamp is at +16 and ModuleNameRva at +20, not +32.
$mods = @()
$r = $streams[4][1]; $c = [BitConverter]::ToUInt32($b,$r); $off = $r+4
for ($i=0; $i -lt $c; $i++) {
    $full = WStr ([BitConverter]::ToUInt32($b,$off+20))
    $nm = if ($full) { $full.Substring($full.LastIndexOfAny([char[]]('\','/'))+1) } else { '<?>' }
    $mods += [pscustomobject]@{ Base=[BitConverter]::ToUInt64($b,$off); Size=[BitConverter]::ToUInt32($b,$off+8); Stamp=[BitConverter]::ToUInt32($b,$off+16); Name=$nm }
    $off += 108
}

$e = $streams[6][1]
$code    = [BitConverter]::ToUInt32($b,$e+8)
$addr    = [BitConverter]::ToUInt64($b,$e+8+16)
$nparams = [BitConverter]::ToUInt32($b,$e+8+24)
$p0      = [BitConverter]::ToUInt64($b,$e+8+32)
$p1      = [BitConverter]::ToUInt64($b,$e+8+40)
$ctxRva  = [BitConverter]::ToUInt32($b,$e+8+152+4)

Write-Output ""
Write-Output "--- exception ---"
Write-Output ("  code    : 0x{0:X8}" -f $code)
Write-Output ("  address : 0x{0:X16}" -f $addr)
if ($nparams -ge 2) {
    $rw = switch ($p0) { 0 {'read'} 1 {'write'} 8 {'execute'} default {"op$p0"} }
    Write-Output ("  tried to {0} 0x{1:X}" -f $rw, $p1)
    if ($p1 -lt 0x10000 -and $p1 -ne 0) {
        Write-Output "  NOTE: a small non-zero address means a garbage integer was used as a pointer."
        Write-Output "        That is freed/reused memory. Hunt the corruption, not this address."
    }
}

$owner = $null
foreach ($m in $mods) { if ($addr -ge $m.Base -and $addr -lt ($m.Base + $m.Size)) { $owner = $m } }
if ($owner) {
    $rva = $addr - $owner.Base
    Write-Output ("  module  : {0}+0x{1:X}" -f $owner.Name, $rva)
} else {
    Write-Output "  module  : <not inside any loaded module>"
}

# CONTEXT_AMD64: Rax 0x78, Rcx 0x80, Rdx 0x88, Rsp 0x98, Rip 0xF8
Write-Output ("  RAX=0x{0:X}  RCX=0x{1:X}  RDX=0x{2:X}  RSP=0x{3:X}" -f `
    ([BitConverter]::ToUInt64($b,$ctxRva+0x78)), ([BitConverter]::ToUInt64($b,$ctxRva+0x80)),
    ([BitConverter]::ToUInt64($b,$ctxRva+0x88)), ([BitConverter]::ToUInt64($b,$ctxRva+0x98)))

if (-not $owner) { return }
$img = switch ($owner.Name) { 'CyberpunkMP.dll' { $Dll } 'Cyberpunk2077.exe' { $GameExe } default { $null } }
if ($img -and (Test-Path $img)) { Describe-Image $owner.Name $img $rva $owner.Stamp }
