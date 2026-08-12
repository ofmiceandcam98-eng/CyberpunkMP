<#
    AnnouncePatch.ps1 - post formatted patch notes to #server-update

    Write the notes as ordinary markdown; this converts them to the embed layout
    Discord actually renders well. See publish\patch-notes\TEMPLATE.md.

        .\AnnouncePatch.ps1 publish\patch-notes\0.1.0.md
        .\AnnouncePatch.ps1 publish\patch-notes\0.1.0.md -DryRun

    Add -Download to append a link line pointing at the latest GitHub release.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Path,
    [string]$Repo = 'ofmiceandcam98-eng/CyberpunkMP',
    [switch]$Download,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Path)) {
    Write-Host "No such file: $Path" -ForegroundColor Red
    exit 1
}

$notify = Join-Path $PSScriptRoot 'DiscordNotify.ps1'
if (-not (Test-Path $notify)) {
    Write-Host "DiscordNotify.ps1 is missing - expected next to this script." -ForegroundColor Red
    exit 1
}

# -Encoding UTF8 is required, not optional. PowerShell 5.1 reads a UTF-8 file that has no
# BOM as ANSI, so an em dash arrives as "a€"" - Discord then rejects the request with a
# bare 400 and no explanation. Any apostrophe or dash in the notes triggers it.
$lines = Get-Content $Path -Encoding UTF8

$version = $null
$out     = New-Object System.Collections.Generic.List[string]
$inComment = $false

foreach ($raw in $lines) {
    $line = $raw.TrimEnd()

    # Skip the HTML comment block the template uses for its own instructions.
    if ($line -match '<!--') { $inComment = $true }
    if ($inComment) {
        if ($line -match '-->') { $inComment = $false }
        continue
    }

    # "# 1.2.3" - the version, and the only H1 we expect.
    if (-not $version -and $line -match '^#\s+(.+)$') {
        $version = $Matches[1].Trim()
        continue
    }

    # "## Section" / "### Section" -> bold header, with a blank line before it
    # so sections don't run together.
    if ($line -match '^#{2,}\s+(.+)$') {
        if ($out.Count -gt 0) { $out.Add('') }
        $out.Add('**' + $Matches[1].Trim() + '**')
        continue
    }

    # "- item" or "* item" -> bullet
    if ($line -match '^\s*[-*]\s+(.+)$') {
        $out.Add('- ' + $Matches[1].Trim())
        continue
    }

    $out.Add($line)
}

if (-not $version) {
    Write-Host "No version found. The first line should be a heading like '# 0.1.0'." -ForegroundColor Red
    exit 1
}

if ($Download) {
    $out.Add('')
    $out.Add("**[Download this build](https://github.com/$Repo/releases/latest)**")
}

# Collapse runs of blank lines, and trim the ends.
$body = ($out -join "`n") -replace "`n{3,}", "`n`n"
$body = $body.Trim()

# Discord's hard limit. Better to refuse than to post something cut off mid-sentence.
$limit = 4096
Write-Host ""
Write-Host "Version    : $version" -ForegroundColor Cyan
Write-Host "Description: $($body.Length) / $limit characters" -ForegroundColor DarkGray

if ($body.Length -gt $limit) {
    Write-Host ""
    Write-Host "Too long by $($body.Length - $limit) characters - Discord would truncate it." -ForegroundColor Red
    Write-Host "Trim the notes, or split them across two posts." -ForegroundColor DarkGray
    exit 1
}

$notifyArgs = @{
    Title   = "Server Update - Patch $version"
    Message = $body
    Level   = 'info'
    Footer  = "v$version"
}
if ($DryRun) { $notifyArgs.DryRun = $true }

& $notify @notifyArgs

# Propagate the real result. Without this the wrapper reported success while the post had
# failed, which is worse than no wrapper at all.
exit $LASTEXITCODE
