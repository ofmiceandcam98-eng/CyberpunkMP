# MakeIcon.ps1 - turn a source PNG into a proper multi-size Windows .ico.
#
# WHY THIS EXISTS
#
# An .ico is a CONTAINER, and Windows picks a size out of it per context: 16 in the title
# bar, 32 on the taskbar at 100% DPI, 48 on the desktop, 256 in Alt-Tab and the Start menu,
# and 40/64/96 at the DPI scalings in between. Ship one 32x32 and Windows upscales it
# everywhere else - which is exactly what a blurry desktop shortcut is.
#
# electron-builder also refuses a Windows icon smaller than 256x256, so a single small
# entry does not merely look bad, it fails the build.
#
# Prompted 2026-09-04: a replacement icon arrived containing one 32x32 image, against a
# current icon.ico carrying six sizes. Rather than hand-convert once, this makes it
# repeatable - and prints what went in and what came out, so "the icon looks wrong" is
# answerable without guessing.
#
#   .\tools\MakeIcon.ps1 -Source "C:\path\to\logo.png"
#   .\tools\MakeIcon.ps1 -Source logo.png -Out code\launcher-lite\build\icon.ico
#
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Source,

    # Defaults to the launcher's icon - the desktop shortcut, taskbar and installer all
    # read this one file (package.json: build.icon and nsis.installerIcon).
    [string]$Out = "code\launcher-lite\build\icon.ico",

    # Every size Windows actually asks for. 256 first because it is the one that matters
    # most and the one electron-builder checks for.
    [int[]]$Sizes = @(256, 128, 64, 48, 32, 16),

    [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

. (Join-Path $PSScriptRoot "Environment.ps1")
Set-Location $Repo

if (-not (Test-Path -LiteralPath $Source)) { Write-Host "no such file: $Source" -ForegroundColor Red; exit 1 }

$src = [System.Drawing.Image]::FromFile((Resolve-Path -LiteralPath $Source))
Write-Host "source: $Source  ($($src.Width) x $($src.Height))" -ForegroundColor Cyan

# A source smaller than the largest requested size can only be UPSCALED, which looks worse
# than the icon it replaces. Say so rather than quietly producing a soft 256.
$largest = ($Sizes | Measure-Object -Maximum).Maximum
if ($src.Width -lt $largest -or $src.Height -lt $largest) {
    Write-Host "  !! source is smaller than ${largest}px - the large entries will be upscaled and will look soft" -ForegroundColor Yellow
    Write-Host "     supply a source at least ${largest}x${largest} for a clean result" -ForegroundColor Yellow
}

# Render each size to a PNG in memory. PNG-in-ICO is what every modern icon uses; it keeps
# the alpha channel intact, which a logo on a transparent ground needs.
$frames = @()
foreach ($size in $Sizes) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    # Fit the source INSIDE the square, preserving aspect. A non-square logo stretched to a
    # square icon is instantly recognisable as wrong.
    $scale = [Math]::Min($size / $src.Width, $size / $src.Height)
    $w = [int]([Math]::Round($src.Width * $scale))
    $h = [int]([Math]::Round($src.Height * $scale))
    $g.DrawImage($src, [int](($size - $w) / 2), [int](($size - $h) / 2), $w, $h)
    $g.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()

    $frames += [pscustomobject]@{ Size = $size; Bytes = $ms.ToArray() }
    $ms.Dispose()
}
$src.Dispose()

if ($WhatIf) {
    Write-Host "`nwould write $Out with:" -ForegroundColor Yellow
    $frames | ForEach-Object { "    {0,4} x {1,-4} {2,7:N0} bytes" -f $_.Size, $_.Size, $_.Bytes.Length }
    exit 0
}

# ---- assemble the container ------------------------------------------------------------
# ICONDIR (6 bytes) + one ICONDIRENTRY (16 bytes) per image, then the image data.
#
# NOT named $out - PowerShell variable names are CASE-INSENSITIVE, so $out and the $Out
# parameter above are the same variable. Assigning a MemoryStream to it silently replaced
# the destination path with a stream, and BinaryWriter then refused a string it had never
# been given. The error named the constructor, not the collision.
$icoStream = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter -ArgumentList $icoStream

$w.Write([UInt16]0)                  # reserved
$w.Write([UInt16]1)                  # type 1 = icon
$w.Write([UInt16]$frames.Count)

$offset = 6 + (16 * $frames.Count)

foreach ($f in $frames) {
    # 256 is written as 0 - the field is one byte, so 256 does not fit and 0 means 256.
    $w.Write([Byte]$(if ($f.Size -ge 256) { 0 } else { $f.Size }))
    $w.Write([Byte]$(if ($f.Size -ge 256) { 0 } else { $f.Size }))
    $w.Write([Byte]0)                # palette count - 0 for truecolour
    $w.Write([Byte]0)                # reserved
    $w.Write([UInt16]1)              # colour planes
    $w.Write([UInt16]32)             # bits per pixel
    $w.Write([UInt32]$f.Bytes.Length)
    $w.Write([UInt32]$offset)
    $offset += $f.Bytes.Length
}

foreach ($f in $frames) { $w.Write($f.Bytes) }

$w.Flush()
$bytes = $icoStream.ToArray()
$w.Dispose(); $icoStream.Dispose()

$outPath = Join-Path $Repo $Out
$dir = Split-Path $outPath -Parent
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

# Keep the previous icon next to the new one. Icons get judged by eye and reverted by hand.
if (Test-Path $outPath) {
    $backup = "$outPath.previous"
    Copy-Item $outPath $backup -Force
    Write-Host "  previous icon kept at $(Split-Path $backup -Leaf)" -ForegroundColor DarkGray
}

[System.IO.File]::WriteAllBytes($outPath, $bytes)

Write-Host "`nwrote $Out  ($([math]::Round($bytes.Length/1KB,1)) KB)" -ForegroundColor Green
$frames | ForEach-Object { "    {0,4} x {1,-4} {2,7:N0} bytes" -f $_.Size, $_.Size, $_.Bytes.Length }
Write-Host "`nRebuild the launcher for it to reach the desktop shortcut and taskbar." -ForegroundColor DarkGray
