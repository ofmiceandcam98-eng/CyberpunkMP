<#
    AnnounceRelease.ps1 - post the current release to #server-update

    Reads the latest GitHub release and announces it, so the Discord message can never
    disagree with what people actually download. Nothing is typed by hand.

    Run it after cutting a release:
        .\AnnounceRelease.ps1

    See what it would post, without posting:
        .\AnnounceRelease.ps1 -DryRun

    Announce a specific tag instead of whatever is newest:
        .\AnnounceRelease.ps1 -Tag nightcity-2026.08.11
#>

[CmdletBinding()]
param(
    [string]$Repo = 'ofmiceandcam98-eng/CyberpunkMP',
    [string]$Tag,
    [string]$Highlights,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$notify = Join-Path $PSScriptRoot 'DiscordNotify.ps1'
if (-not (Test-Path $notify)) {
    Write-Host "DiscordNotify.ps1 is missing - expected next to this script." -ForegroundColor Red
    exit 1
}

# gh handles auth for us, and works for public repos even unauthenticated.
if ($Tag) {
    $endpoint = "repos/$Repo/releases/tags/$Tag"
} else {
    $endpoint = "repos/$Repo/releases/latest"
}

try {
    $release = gh api $endpoint | ConvertFrom-Json
} catch {
    Write-Host "Could not read the release from GitHub: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Is 'gh' installed and is there a published release on $Repo?" -ForegroundColor DarkGray
    exit 1
}

if ($release.draft) {
    Write-Host "That release is still a DRAFT - nobody can download it yet. Not announcing." -ForegroundColor Yellow
    exit 1
}

# The thing a person should actually download, named explicitly.
#
# This used to take whatever asset happened to be FIRST, which is meaningless: on one
# release that was the 8.3 MB install package, on the next it was the 4.1 MB DLL. The
# announced size changed with no change in what was being offered, and the number
# described a file nobody was meant to download.
$preferred = @("NightCityOnline-Setup.exe", "FullInstall.zip")

$asset = $null
foreach ($name in $preferred) {
    $asset = $release.assets | Where-Object { $_.name -eq $name } | Select-Object -First 1
    if ($asset) { break }
}

if (-not $asset) {
    Write-Host "That release has no installer attached - nothing for anyone to download." -ForegroundColor Yellow
    Write-Host "Expected one of: $($preferred -join ', ')" -ForegroundColor DarkGray
    exit 1
}

$sizeMb   = [math]::Round($asset.size / 1MB, 1)
$download = "https://github.com/$Repo/releases/latest"

# WHAT CHANGED, IN THE MESSAGE ITSELF.
#
# zeldfep, 2026-09-07: "make sure when pushing to discord you explain whats being done in
# those updates we want to be tranparent." Until now this posted "A new build is up." and a
# download link - the release notes existed, were written for players, and were on a page
# nobody clicks. An announcement that does not say what changed asks people to update on
# trust, and after a week where three releases shipped a mod that could not load, trust is
# exactly the thing we are short of.
#
# Pulled from publish/release-notes.md rather than retyped, so the Discord post and the
# release page cannot disagree. If the section is missing the post still goes out - a build
# people can download beats a perfect message nobody gets - but it says so out loud rather
# than quietly looking the same as an ordinary release.
function Get-ReleaseNotesSection {
    param([string]$Tag)

    $notesFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'publish\release-notes.md'
    if (-not (Test-Path $notesFile)) { return $null }

    # -Encoding UTF8 is not optional. PowerShell 5.1 reads a BOM-less file as ANSI, which
    # turns the em dash in "What changed - v0.3.118" into three mojibake characters and
    # makes the heading unmatchable. That is exactly how this shipped silently the first
    # time: the script reported "no section" and posted the generic message.
    $lines = Get-Content $notesFile -Encoding UTF8
    # The heading uses an em dash, and has been written with a hyphen before now. Accept both.
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        # One any-character separator: PowerShell does not expand backslash-u escapes in
        # a string, and this heading has been written with both an em dash and a hyphen.
        if ($lines[$i] -match "^##\s+What changed\s+.+?\s+$([regex]::Escape($Tag))\s*$") { $start = $i + 1; break }
    }
    if ($start -lt 0) { return $null }

    $out = @()
    for ($i = $start; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^##\s') { break }
        $out += $lines[$i]
    }
    return ($out -join "`n").Trim()
}

$changed = Get-ReleaseNotesSection -Tag $release.tag_name

$body = @()
$body += "A new build is up."
$body += ""
if ($Highlights) {
    $body += $Highlights
    $body += ""
}

if ($changed) {
    # Discord caps an embed description at 4096 characters. Cut on a line boundary rather
    # than mid-sentence, and say that it was cut instead of trailing off.
    $budget = 2600
    if ($changed.Length -gt $budget) {
        $kept = @()
        $used = 0
        foreach ($line in ($changed -split "`n")) {
            if ($used + $line.Length + 1 -gt $budget) { break }
            $kept += $line
            $used += $line.Length + 1
        }
        $changed = ($kept -join "`n").TrimEnd() + "`n`n*(trimmed - the full notes are on the release page)*"
    }
    $body += "__**What changed**__"
    $body += ""
    $body += $changed
    $body += ""
}
else {
    Write-Host "No 'What changed - $($release.tag_name)' section in publish\release-notes.md." -ForegroundColor Yellow
    Write-Host "  Posting anyway, but the message will not say what changed. Add the section and re-run" -ForegroundColor DarkGray
    Write-Host "  to replace it - people should not be asked to update on trust." -ForegroundColor DarkGray
    $body += "_Release notes for this build are on the release page._"
    $body += ""
}

$body += "**[Download it here]($download)**"
$body += ""
$body += "**New here?** Download the launcher, sign in with Discord, and press Install - it fetches the mod and everything it needs. You do not need to unzip anything by hand."
$body += ""
$body += "If you crash, grab the log from ``red4ext\plugins\zzzCyberpunkMP\logs\`` and post the whole file - that is the most useful thing you can do for us right now."

$message = $body -join "`n"

$fields = "Version=$($release.tag_name);Download=$sizeMb MB;Game patch=2.31"

# Not $args - that is an automatic variable holding this script's own unbound arguments,
# and assigning to it corrupts the call.
$notifyArgs = @{
    Title   = $release.name
    Message = $message
    Level   = 'success'
    Fields  = $fields
    Footer  = 'Night City Online'
}
if ($DryRun) { $notifyArgs.DryRun = $true }

& $notify @notifyArgs
