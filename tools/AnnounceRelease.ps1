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

$asset = $release.assets | Select-Object -First 1

if (-not $asset) {
    Write-Host "That release has no attached file - nothing for anyone to download." -ForegroundColor Yellow
    Write-Host "Attach the zip first, then run this again." -ForegroundColor DarkGray
    exit 1
}

$sizeMb   = [math]::Round($asset.size / 1MB, 1)
$download = "https://github.com/$Repo/releases/latest"

$body = @()
$body += "A new build is up."
$body += ""
if ($Highlights) {
    $body += $Highlights
    $body += ""
}
$body += "**[Download it here]($download)**"
$body += ""
$body += "Install steps are in ``INSTALL.txt`` inside the zip. If you crash, grab the log from ``red4ext\plugins\zzzCyberpunkMP\logs\`` and post the whole file - that is the most useful thing you can do for us right now."

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
