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
#     answered immediately.
#
#   * the bytes at the fault, so the instruction can be decoded by hand.
#
#   * the function's bounds from .pdata when it has an entry, and any string literals it
#     references - which is how a function gets named here, because the PDB will not load
#     (dbghelp reports SymType None even with a matching GUID and age, from local disk).
#
# Two traps worth knowing:
#   - MINIDUMP_THREAD's stack descriptor is at +24, size +32, rva +36. Reading it at +16
#     lands on Teb and yields a bogus multi-hundred-MB range.
#   - /OPT:ICF folds small identical functions, so "who calls this" can return completely
#     unrelated code. Do not trust caller analysis for small leaf helpers.
[CmdletBinding()]
param(
    # Defaults to the newest report in the queue.
    [string]$DumpPath,
    # Defaults to the deployed plugin DLL.
    [string]$Dll = 'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\red4ext\plugins\zzzCyberpunkMP\CyberpunkMP.dll'
)

$ErrorActionPreference = 'Stop'

if (-not $DumpPath) {
    $q = Join-Path $env:LOCALAPPDATA 'REDEngine\ReportQueue'
    $newest = Get-ChildItem $q -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $newest) { throw "no crash reports under $q" }
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

# MINIDUMP_MODULE is 108 bytes; ModuleNameRva is at +20, not +32.
$mods = @()
$r = $streams[4][1]; $c = [BitConverter]::ToUInt32($b,$r); $off = $r+4
for ($i=0; $i -lt $c; $i++) {
    $full = WStr ([BitConverter]::ToUInt32($b,$off+20))
    $nm = if ($full) { $full.Substring($full.LastIndexOfAny([char[]]('\','/'))+1) } else { '<?>' }
    $mods += [pscustomobject]@{ Base=[BitConverter]::ToUInt64($b,$off); Size=[BitConverter]::ToUInt32($b,$off+8); Name=$nm }
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

if (-not $owner -or $owner.Name -ne 'CyberpunkMP.dll' -or -not (Test-Path $Dll)) { return }

# --- our DLL: bytes at the fault, function bounds, and referenced strings ---
$d = [System.IO.File]::ReadAllBytes($Dll)
$pe = [BitConverter]::ToUInt32($d,0x3C)
$nSec = [BitConverter]::ToUInt16($d,$pe+6); $optSz = [BitConverter]::ToUInt16($d,$pe+20)
$opt = $pe+24; $dirOff = $opt + 112; $secStart = $opt + $optSz
$secs = @()
for ($i=0; $i -lt $nSec; $i++) {
    $s = $secStart + $i*40
    $secs += [pscustomobject]@{
        Name=[System.Text.Encoding]::ASCII.GetString($d,$s,8).Trim([char]0)
        VA=[BitConverter]::ToUInt32($d,$s+12); VSz=[BitConverter]::ToUInt32($d,$s+8); Raw=[BitConverter]::ToUInt32($d,$s+20) }
}
function R2O([int64]$x) { foreach ($s in $secs) { if ($x -ge $s.VA -and $x -lt ($s.VA+$s.VSz)) { return [int]($s.Raw + ($x - $s.VA)) } } return -1 }
function SecOf([int64]$x) { foreach ($s in $secs) { if ($x -ge $s.VA -and $x -lt ($s.VA+$s.VSz)) { return $s.Name } } return '?' }

$fo = R2O $rva
if ($fo -ge 0) {
    Write-Output ""
    Write-Output "--- bytes at the fault (decode by hand) ---"
    Write-Output ("  " + (($d[$fo..($fo+23)] | ForEach-Object { $_.ToString('X2') }) -join ' '))
}

$pdRva = [BitConverter]::ToUInt32($d,$dirOff+3*8); $pdSz = [BitConverter]::ToUInt32($d,$dirOff+3*8+4)
$pdOff = R2O $pdRva; $cnt = [int]($pdSz/12)
$bg = 0; $en = 0
for ($i=0; $i -lt $cnt; $i++) {
    $x = $pdOff + $i*12
    $s1 = [BitConverter]::ToUInt32($d,$x); $s2 = [BitConverter]::ToUInt32($d,$x+4)
    if ($s1 -le $rva -and $rva -lt $s2) { $bg = $s1; $en = $s2; break }
    if ($s1 -gt $rva) { break }
}
Write-Output ""
if ($bg -eq 0) {
    Write-Output "--- function: no .pdata entry (a leaf function with no unwind data) ---"
    Write-Output "    Find its start by scanning back to the CC/C3 padding before it."
} else {
    Write-Output ("--- function 0x{0:X}..0x{1:X} ({2} bytes), fault at +0x{3:X} ---" -f $bg,$en,($en-$bg),($rva-$bg))
    $seen = @{}
    for ($x = $bg; $x -lt $en; $x++) {
        $o = R2O $x
        if ($o -lt 0 -or $o+7 -ge $d.Length) { continue }
        if (($d[$o] -eq 0x48 -or $d[$o] -eq 0x4C) -and $d[$o+1] -eq 0x8D -and (($d[$o+2] -band 0xC7) -eq 0x05)) {
            $t = $x + 7 + [BitConverter]::ToInt32($d,$o+3)
            if ((SecOf $t) -match 'rdata|data') {
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
