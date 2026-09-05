<#
    Proves netpack's enum support works, end to end.

    WHY THIS IS SEPARATE FROM Verify.ps1

    Verify auto-discovers tools\tests\*.cpp and compiles each with one fixed command. This
    test cannot work that way: it needs sources that do not exist until NetPack has been run
    against a .proto, plus the Protocol PCH and the Common sources for Buffer/Serialization.

    So it runs generation and compilation itself, against a SCRATCH proto that is deliberately
    outside code\protocol\ - a file the Protocol target never sees cannot affect any protocol
    identifier.

    When Stage 6 lands a real enum in a real .proto, fold this into Verify proper.
#>

[CmdletBinding()]
param(
    [switch]$KeepOutput
)

$ErrorActionPreference = "Stop"
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$netpack = Join-Path $repo "build\windows\x64\release\NetPack.exe"

if (-not (Test-Path $netpack)) {
    Write-Host "NetPack.exe not found - build it first:  xmake build NetPack" -ForegroundColor Red
    exit 1
}

$out = Join-Path $env:TEMP "nco-netpack-enum"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

Write-Host "== codegen ==" -ForegroundColor Cyan

# NetPack resolves its input relative to the CURRENT DIRECTORY and does not accept an
# absolute path - handed one, it appends the path to the cwd and reports "File not found"
# against a nonsense location. The xmake rule always passes repo-relative sourcefiles, so
# this has never come up. Match that.
Push-Location $repo
try {
    & $netpack "tools\netpack-scratch\enumtest.proto" $out
}
finally {
    Pop-Location
}

if ($LASTEXITCODE -ne 0) {
    # Exit 139 here is the original defect: GetType's TYPE_ENUM branch calling
    # field->message_type(), which is null for an enum field. See code\netpack\main.cpp.
    Write-Host "  codegen FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
    exit 1
}

$header = Join-Path $out "enumtest.gen.h"
$source = Join-Path $out "enumtest.gen.cpp"
foreach ($f in @($header, $source)) {
    if (-not (Test-Path $f)) {
        Write-Host "  codegen produced no $(Split-Path $f -Leaf)" -ForegroundColor Red
        exit 1
    }
}
Write-Host "  ok - $(Split-Path $header -Leaf) and $(Split-Path $source -Leaf) generated"

# The identifier this scratch protocol hashes to. Printed rather than asserted: the point is
# that hashing RUNS at all (it is where the segfault used to happen), not what it produces.
$id = (Select-String -Path $header -Pattern "kIdentifier = (0x[0-9a-f]+)").Matches.Groups[1].Value
Write-Host "  scratch protocol identifier: $id"

Write-Host "== compile ==" -ForegroundColor Cyan

# Same lookup Verify.ps1 uses, and in the same order - this box has BuildTools under
# Program Files (x86), and `vswhere -latest -property installationPath` returns EMPTY for it,
# so the literal path is the one that actually works and the vswhere branch is the fallback.
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -property installationPath
        if ($vs) { $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat" }
    }
}
if (-not (Test-Path $vcvars)) {
    Write-Host "  no MSVC found - cannot compile" -ForegroundColor Yellow
    exit 1
}

# entt is header-only and already on disk for the normal build; the generated Deserializer
# needs its dispatcher.
$packageInclude = {
    param($letter, $name)
    (Get-ChildItem "$env:USERPROFILE\AppData\Local\.xmake\packages\$letter\$name" -Recurse -Directory -Filter "include" -ErrorAction SilentlyContinue |
     Select-Object -First 1).FullName
}

$entt = & $packageInclude "e" "entt"
$hopscotch = & $packageInclude "h" "hopscotch-map"   # Core/Stl.h needs tsl/hopscotch_map.h

foreach ($p in @(@("entt", $entt), @("hopscotch-map", $hopscotch))) {
    if (-not $p[1]) {
        Write-Host "  $($p[0]) include not found in the xmake package cache" -ForegroundColor Yellow
        exit 1
    }
}

$exe = Join-Path $out "netpack_enum_test.exe"

$incs = @(
    "/I `"$out`"",                                  # the generated header
    "/I `"$(Join-Path $repo 'code\protocol')`"",    # ProtocolPCH.h
    "/I `"$(Join-Path $repo 'code\common')`"",      # Core/Buffer.h, Core/Serialization.h
    "/I `"$entt`"",
    "/I `"$hopscotch`""
) -join " "

$srcs = @(
    "`"$(Join-Path $PSScriptRoot 'netpack_enum_test.cpp')`"",
    "`"$source`""
) -join " "

<#
    Link the already-built Common.lib rather than compiling its sources in.

    Buffer.cpp and Serialization.cpp alone are not enough: the project's String/Vector run
    through StlAllocator, which needs Allocator::Get, which needs MimallocAllocator, which
    needs mimalloc. Pulling that thread means rebuilding half of Common by hand. Common.lib
    is right there and is what the real build links.
#>
$commonLib = Join-Path $repo "build\windows\x64\release\Common.lib"
if (-not (Test-Path $commonLib)) {
    Write-Host "  Common.lib not built - run:  xmake build Common" -ForegroundColor Yellow
    exit 1
}

# mimalloc, because Common.lib's allocator references it.
$mimalloc = (Get-ChildItem "$env:USERPROFILE\AppData\Local\.xmake\packages\m\mimalloc" -Recurse -File -Filter "mimalloc-static.lib" -ErrorAction SilentlyContinue |
             Select-Object -First 1).FullName
if (-not $mimalloc) {
    Write-Host "  mimalloc-static.lib not found in the xmake package cache" -ForegroundColor Yellow
    exit 1
}

$libs = "`"$commonLib`" `"$mimalloc`" advapi32.lib bcrypt.lib"

# /std:c++20 because Core/Meta.h uses concepts - the real build sets cxx20 in xmake.lua, and
# c++17 here fails inside Meta.h rather than anywhere near the enum being tested.
# /wd4267 /wd4244: the generated code narrows on purpose in places; not the subject here.
$cmd = "`"$vcvars`" >nul 2>&1 && cl /nologo /std:c++20 /utf-8 /EHsc /MD /wd4267 /wd4244 $incs /Fe:`"$exe`" /Fo:`"$out\\`" $srcs /link $libs 2>&1"
$r = cmd /c $cmd

if ($LASTEXITCODE -ne 0) {
    Write-Host "  COMPILE FAILED" -ForegroundColor Red
    $r | Where-Object { $_ -match 'error|warning C4' } | Select-Object -First 15 | ForEach-Object { Write-Host "    $_" }
    exit 1
}
Write-Host "  ok - generated enum code compiles"

Write-Host "== round trip ==" -ForegroundColor Cyan
& $exe
$code = $LASTEXITCODE

if (-not $KeepOutput) { Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue }

if ($code -ne 0) {
    Write-Host "`nENUM ROUND TRIP FAILED" -ForegroundColor Red
    exit 1
}

Write-Host "`nnetpack enum support VERIFIED - codegen, compile, round trip." -ForegroundColor Green
