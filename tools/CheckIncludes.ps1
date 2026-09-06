# Find std:: symbols used without including the header that declares them.
#
# WHY THIS EXISTS
#
# The server compiles with MSVC on Windows and with GCC in a container on the NAS, and only
# the second one matters - it is the build that actually serves players. MSVC supplies a lot
# of the standard library transitively: <string> drags in <cstdio>, and code calling
# std::snprintf with no <cstdio> compiles perfectly here and fails on libstdc++ with
# "'snprintf' is not a member of 'std'".
#
# On 2026-09-02 that took the production server down for three hours without anybody
# noticing. The deploy script rebuilds and, on a build failure, KEEPS THE CONTAINER RUNNING
# THE PREVIOUS IMAGE - so the old protocol kept answering, every updated client was refused
# at connect, and the only visible symptom was a menu button that appeared to do nothing.
# Four files were missing <cstdio> and one was missing <map>.
#
# WHAT IT CHECKS, and what it deliberately does not
#
# A symbol counts as available if the header that declares it is included by the file
# itself, by any project header the file includes (followed transitively), or by the
# precompiled header. That is ordinary C++ and is fine.
#
# What is NOT fine, and what this catches, is relying on one STANDARD header to pull in
# another. That is an implementation detail of whichever library you happened to compile
# against, and it is not portable.
#
# This is a lint, not a compiler. It will not catch everything, and a clean run is not a
# promise that GCC is happy. It catches the one class of error that has actually bitten,
# cheaply, on a machine with no GCC on it.
#
#   .\tools\CheckIncludes.ps1
#   .\tools\CheckIncludes.ps1 -Path code\server\native
#
[CmdletBinding()]
param(
    [string]$Path = "code\server\native",

    # Force-included for every translation unit in the target, so its includes count.
    [string]$Pch = "code\server\native\ServerPCH.h"
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot "Environment.ps1")
Set-Location $Repo

# symbol -> the header that declares it. Deliberately partial: these are the ones that
# actually get used here and actually differ between the two standard libraries.
$decl = [ordered]@{
    'cstdio'     = 'snprintf','printf','fprintf','sprintf','fopen','fclose','FILE'
    'map'        = 'map','multimap'
    'unordered_map' = 'unordered_map'
    'set'        = 'set','unordered_set'
    'vector'     = 'vector'
    'string'     = 'string','stoi','stol','stoll','stoul','stoull','stof','stod','to_string'
    'algorithm'  = 'sort','stable_sort','find','find_if','remove','remove_if','max','min',
                   'count','count_if','copy','transform','any_of','all_of','none_of',
                   'unique','reverse','lower_bound','upper_bound','clamp'
    'numeric'    = 'accumulate','iota'
    'utility'    = 'pair','make_pair','move','forward','swap'
    'functional' = 'function','hash'
    'memory'     = 'shared_ptr','unique_ptr','make_shared','make_unique','weak_ptr'
    'thread'     = 'thread','this_thread'
    'mutex'      = 'mutex','lock_guard','unique_lock','recursive_mutex','scoped_lock'
    'atomic'     = 'atomic','memory_order'
    'chrono'     = 'chrono'
    'optional'   = 'optional','nullopt'
    'array'      = 'array'
    'list'       = 'list'
    'deque'      = 'deque'
    'cstring'    = 'memcpy','memset','memcmp','strchr','strcmp','strlen','strncmp'
    'cctype'     = 'toupper','tolower','isdigit','isalpha','isspace'
    'cstdint'    = 'uint8_t','uint16_t','uint32_t','uint64_t','int8_t','int16_t','int32_t','int64_t','uintptr_t'
    'cstddef'    = 'size_t','ptrdiff_t','byte'
    'fstream'    = 'ifstream','ofstream','fstream'
    'filesystem' = 'filesystem'
    'stdexcept'  = 'runtime_error','invalid_argument','out_of_range','logic_error'
    'random'     = 'random_device','mt19937','mt19937_64','uniform_int_distribution'
    'limits'     = 'numeric_limits'
}

# symbol -> header, flattened for lookup
$header = @{}
foreach ($h in $decl.Keys) { foreach ($s in $decl[$h]) { $header[$s] = $h } }

$root = Join-Path $Repo $Path
if (-not (Test-Path $root)) { Write-Host "no such path: $root" -ForegroundColor Red; exit 2 }

$files = Get-ChildItem $root -Recurse -Include *.h,*.hpp,*.cpp

# ---- read every file once: its std includes, and its project includes ----------------
$stdIncludes  = @{}   # full path -> set of <...> headers
$projIncludes = @{}   # full path -> list of "..." include names

foreach ($f in $files) {
    $text = Get-Content $f.FullName -Raw -ErrorAction SilentlyContinue
    if (-not $text) { continue }

    $s = New-Object System.Collections.Generic.HashSet[string]
    foreach ($m in [regex]::Matches($text, '#\s*include\s*<([A-Za-z0-9_./]+)>')) { [void]$s.Add($m.Groups[1].Value) }
    $stdIncludes[$f.FullName] = $s

    $p = @()
    foreach ($m in [regex]::Matches($text, '#\s*include\s*"([^"]+)"')) { $p += $m.Groups[1].Value }
    $projIncludes[$f.FullName] = $p
}

# The PCH is force-included, so whatever it pulls in is available everywhere in the target.
$pchPath = Join-Path $Repo $Pch
$pchHeaders = New-Object System.Collections.Generic.HashSet[string]
if (Test-Path $pchPath) {
    $t = Get-Content $pchPath -Raw
    foreach ($m in [regex]::Matches($t, '#\s*include\s*<([A-Za-z0-9_./]+)>')) { [void]$pchHeaders.Add($m.Groups[1].Value) }
}

# Resolve a project include name to a real file, by basename - close enough here, and it
# only ever over-approximates what is available, so it cannot produce a false alarm.
$byName = @{}
foreach ($f in $files) { $byName[$f.Name] = $f.FullName }

function Get-Reachable([string]$file, $seen) {
    if ($seen.Contains($file)) { return @() }
    [void]$seen.Add($file)

    $out = @()
    if ($stdIncludes.ContainsKey($file)) { $out += $stdIncludes[$file] }

    foreach ($inc in $projIncludes[$file]) {
        $leaf = Split-Path $inc -Leaf
        if ($byName.ContainsKey($leaf)) { $out += Get-Reachable $byName[$leaf] $seen }
    }

    return $out
}

# ---- check ---------------------------------------------------------------------------
$problems = @()

foreach ($f in $files) {
    $text = Get-Content $f.FullName -Raw -ErrorAction SilentlyContinue
    if (-not $text) { continue }

    # Strip comments and strings so a symbol named only in prose is not reported. This is
    # what keeps the noise down enough for the output to be worth reading.
    $code = [regex]::Replace($text, '/\*.*?\*/', '', 'Singleline')
    $code = [regex]::Replace($code, '//[^\r\n]*', '')
    $code = [regex]::Replace($code, '"(\\.|[^"\\])*"', '""')

    $seen = New-Object System.Collections.Generic.HashSet[string]
    $reachable = New-Object System.Collections.Generic.HashSet[string]
    foreach ($h in (Get-Reachable $f.FullName $seen)) { [void]$reachable.Add($h) }
    foreach ($h in $pchHeaders) { [void]$reachable.Add($h) }

    $missing = @{}

    foreach ($m in [regex]::Matches($code, 'std::([A-Za-z_][A-Za-z0-9_]*)')) {
        $sym = $m.Groups[1].Value
        if (-not $header.ContainsKey($sym)) { continue }

        $need = $header[$sym]
        if ($reachable.Contains($need)) { continue }

        if (-not $missing.ContainsKey($need)) { $missing[$need] = @() }
        if ($missing[$need] -notcontains $sym) { $missing[$need] += $sym }
    }

    foreach ($need in $missing.Keys) {
        $problems += [pscustomobject]@{
            File    = $f.FullName.Substring($Repo.Length + 1)
            Header  = $need
            Symbols = ($missing[$need] | Sort-Object) -join ', '
        }
    }
}

if (-not $problems) {
    Write-Host "OK - every std:: symbol has its header ($($files.Count) files checked)" -ForegroundColor Green
    exit 0
}

Write-Host "`nMissing includes - these compile on MSVC and fail on GCC:`n" -ForegroundColor Yellow

foreach ($p in ($problems | Sort-Object File, Header)) {
    Write-Host ("  {0}" -f $p.File) -ForegroundColor White
    Write-Host ("      needs #include <{0}>  for  {1}" -f $p.Header, $p.Symbols) -ForegroundColor Yellow
}

Write-Host "`n$($problems.Count) missing include(s)." -ForegroundColor Yellow
exit 1
