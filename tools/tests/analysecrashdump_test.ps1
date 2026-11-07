# tools\AnalyseCrashDump.ps1 -Rva: does an address INSIDE a game function name that function's
# address hash?
#
# Build-independent: the samples are drawn from the cyberpunk2077_addresses.json beside the
# installed exe, so a game patch refreshes them instead of breaking them. Needs the game
# installed and skips (exit 0) without it. Not wired into Verify - it reads ~200 MB - so run it
# after touching AnalyseCrashDump.ps1:
#
#   powershell -NoProfile -File tools\tests\analysecrashdump_test.ps1
param(
    [string]$GameExe = 'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe'
)

$ErrorActionPreference = 'Stop'
$tool = Join-Path $PSScriptRoot '..\AnalyseCrashDump.ps1'
$tbl = Join-Path (Split-Path -Parent $GameExe) 'cyberpunk2077_addresses.json'
if (-not (Test-Path $GameExe) -or -not (Test-Path $tbl)) {
    Write-Output "SKIP: no game at $GameExe (needs the exe and cyberpunk2077_addresses.json beside it)"
    exit 0
}

# Section 1's VA, from the exe header: table offsets are '0001:<rva from that VA>'.
$fs = [System.IO.File]::OpenRead($GameExe); $h = New-Object byte[] 4096; [void]$fs.Read($h, 0, 4096); $fs.Close()
$pe = [BitConverter]::ToUInt32($h, 0x3C)
$textVA = [BitConverter]::ToUInt32($h, $pe + 24 + [BitConverter]::ToUInt16($h, $pe + 20) + 12)

# 24 entries spread across the table; the lazy match stays inside one {...} entry.
$txt = [System.IO.File]::ReadAllText($tbl)
$re = [regex]'"hash": "(\d+)"[^}]*?"offset": "0001:([0-9a-f]{8})"'
$samples = @()
for ($k = 1; $k -le 24; $k++) {
    $m = $re.Match($txt, [int]($txt.Length * $k / 25))
    if ($m.Success) {
        $samples += [pscustomobject]@{ Hash = $m.Groups[1].Value; Offset = $m.Groups[2].Value
                                       Start = [uint64]$textVA + [Convert]::ToUInt32($m.Groups[2].Value, 16) }
    }
}
$txt = $null

# One call for all of them, one byte past each start. Numbers on purpose: an unquoted 0x... once
# arrived as decimal text and was misread as hex. The first goes in as 'module+0x' text instead.
$args1 = @("Cyberpunk2077.exe+0x{0:X}" -f ($samples[0].Start + 1)) + @($samples | Select-Object -Skip 1 | ForEach-Object { [uint64]($_.Start + 1) })
$out = (& $tool -GameExe $GameExe -Rva $args1 2>&1 | Out-String)
$blocks = @{}
foreach ($blk in ($out -split '=== Cyberpunk2077\.exe\+0x' | Select-Object -Skip 1)) {
    $blocks[[Convert]::ToUInt64(($blk -split '\s')[0], 16)] = $blk
}

$checked = 0; $fail = 0
foreach ($s in $samples) {
    $blk = $blocks[[uint64]($s.Start + 1)]
    if (-not $blk) {
        Write-Output ("FAIL: no output block for Cyberpunk2077.exe+0x{0:X}." -f ($s.Start + 1))
        Write-Output  "      where: tools\AnalyseCrashDump.ps1 -Rva, the '=== module+0x... ===' header"
        Write-Output ("      fix: run .\tools\AnalyseCrashDump.ps1 -Rva 0x{0:X} and read what it printed instead" -f ($s.Start + 1))
        $fail++; continue
    }
    # A leaf with no .pdata (or a one-byte stub followed by another function) is not this check.
    if ($blk -notmatch '--- function 0x([0-9A-F]+)\.\.' -or [Convert]::ToUInt64($Matches[1], 16) -ne $s.Start) { continue }
    $checked++
    if ($blk -notmatch ("hash {0}\b" -f $s.Hash)) {
        Write-Output ("FAIL: Cyberpunk2077.exe+0x{0:X}, inside the function at 0x{1:X}, did not name its hash {2}." -f ($s.Start + 1), $s.Start, $s.Hash)
        Write-Output  "      where: tools\AnalyseCrashDump.ps1, the 'address hash at the function start' block"
        Write-Output ("      fix: compare the offset it printed with the table entry 0001:{0}" -f $s.Offset)
        $fail++
    }
}

if ($checked -lt 8) {
    Write-Output ("FAIL: only {0} of {1} sampled table entries had a .pdata function starting at them." -f $checked, $samples.Count)
    Write-Output  "      where: tools\AnalyseCrashDump.ps1, the .pdata binary search (or this test's sampling)"
    Write-Output  "      fix: run the tool on a few sample RVAs by hand; most game functions have unwind data"
    $fail++
}
if ($fail) { exit 1 }
Write-Output ("PASS: {0} of {1} sampled functions named by their address hash from one byte inside" -f $checked, $samples.Count)
