<#
    UpdateTodoList.ps1 - keep a #to-do-list channel in step with publish\TODO.md

    Usage:
        .\tools\UpdateTodoList.ps1 -Channel 1234567890    # first run only
        .\tools\UpdateTodoList.ps1                        # every run after that
        .\tools\UpdateTodoList.ps1 -DryRun                # print, send nothing

    EDITS one message rather than posting a new one each time. A to-do list that
    appends is a changelog: after a week the channel is twenty stale lists and the
    current one is somewhere in the middle. One message that is always right is worth
    more than a history nobody scrolls.

    The message id is remembered in tools\.discord-todo alongside the channel. Delete
    that file to start a fresh message.

    The bot needs Send Messages in the target channel. It does NOT need View Channels
    or Manage Messages - editing your own message requires neither - so this works with
    the same narrow permissions it already has.
#>

[CmdletBinding()]
param(
    [string]$Channel,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$repo    = Split-Path $PSScriptRoot -Parent
$source  = Join-Path $repo "publish\TODO.md"
$state   = Join-Path $PSScriptRoot ".discord-todo"
$botFile = Join-Path $PSScriptRoot ".discord-bot"

function Die { param($T) Write-Host "`n$T" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $source)) { Die "No publish\TODO.md - nothing to publish." }
if (-not (Test-Path $botFile)) { Die "No tools\.discord-bot - the bot token lives there." }

# ---------------------------------------------------------------------------
# Credentials and state
# ---------------------------------------------------------------------------

$token = $null
foreach ($line in Get-Content $botFile) {
    if ($line -match '^\s*token\s*=\s*(.+)$') { $token = $Matches[1].Trim() }
}
if (-not $token) { Die "No 'token=' line in tools\.discord-bot" }

$messageId = $null
if (Test-Path $state) {
    foreach ($line in Get-Content $state) {
        if ($line -match '^\s*channel\s*=\s*(.+)$') { if (-not $Channel) { $Channel = $Matches[1].Trim() } }
        if ($line -match '^\s*message\s*=\s*(.+)$') { $messageId = $Matches[1].Trim() }
    }
}

if (-not $Channel) {
    Die @"
No channel yet. Pass it once and it will be remembered:

    .\tools\UpdateTodoList.ps1 -Channel <channel id>

In Discord: Settings > Advanced > Developer Mode, then right-click the channel and
Copy Channel ID.
"@
}

# ---------------------------------------------------------------------------
# Content
# ---------------------------------------------------------------------------

# Read as UTF8 explicitly. Without it PowerShell reads a BOM-less UTF-8 file as ANSI
# and an em dash becomes garbage, which Discord rejects with a bare 400.
$body = Get-Content $source -Raw -Encoding UTF8

# Sent as an EMBED, not a plain message.
#
# A plain message caps at 2000 characters and this list is comfortably past that; an
# embed description allows 4096. It also renders with a title and a coloured edge,
# which matters for something meant to be scanned rather than read - and the bot
# already has Embed Links, so this needs no new permission.
if ($body.Length -gt 4096) {
    $body = $body.Substring(0, 4096 - 120) + "`n`n_(truncated - ask in chat for the rest)_"
    Write-Host "TODO.md is over Discord's 4096 character embed limit - truncated." -ForegroundColor Yellow
}

# The JSON is built by hand, deliberately.
#
# ConvertTo-Json -Depth in PowerShell 5.1 does not stop at strings: it recurses into the
# String object's own .NET members - Length, the character indexer, then reflection
# metadata - until it hits a circular reference. The -Depth 6 needed for the nested embed
# structure turned this 3,400 character list into a FORTY-SIX MEGABYTE payload, which
# Discord rejected with a bare 403 and an empty body. It looked exactly like a permissions
# problem, and sent us both chasing channel settings that were already correct.
#
# Escaping by hand is a few lines and behaves the same way every time.
function ConvertTo-JsonString {
    param([string]$Value)

    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('"')

    foreach ($ch in $Value.ToCharArray()) {
        switch ($ch) {
            '"'     { [void]$sb.Append('\"') }
            '\'     { [void]$sb.Append('\\') }
            "`n"    { [void]$sb.Append('\n') }
            "`r"    { [void]$sb.Append('\r') }
            "`t"    { [void]$sb.Append('\t') }
            default {
                # Control characters must be escaped; everything else - including emoji -
                # travels as UTF-8, which is what the request is encoded as.
                if ([int]$ch -lt 32) { [void]$sb.AppendFormat('\u{0:x4}', [int]$ch) }
                else                 { [void]$sb.Append($ch) }
            }
        }
    }

    [void]$sb.Append('"')
    return $sb.ToString()
}

$title  = "Night City Online - what we are working on"
$footer = "Updated automatically - " + (Get-Date -Format "d MMM yyyy, HH:mm")

# content is cleared explicitly: editing a message that previously had plain text would
# otherwise leave that text sitting above the embed forever.
$payload = '{"content":"","embeds":[{' +
           '"title":'       + (ConvertTo-JsonString $title)  + ',' +
           '"description":' + (ConvertTo-JsonString $body)   + ',' +
           '"color":16632664,' +
           '"footer":{"text":' + (ConvertTo-JsonString $footer) + '}' +
           '}]}'

if ($DryRun) {
    Write-Host "--- would send to channel $Channel ---" -ForegroundColor Cyan
    Write-Host $body
    Write-Host "--- end ($($body.Length) chars, $(if ($messageId) { "editing $messageId" } else { "new message" })) ---" -ForegroundColor Cyan
    exit 0
}

# ---------------------------------------------------------------------------
# Send
# ---------------------------------------------------------------------------

# The User-Agent is REQUIRED, not decoration.
#
# Discord's API mandates that bots identify themselves, and Cloudflare rejects requests
# that do not with a 403 and an EMPTY body - indistinguishable from "this bot cannot post
# here". That cost a long detour through channel permissions that were already correct.
# DiscordNotify.ps1 has always sent one, which is why #server-update worked throughout.
$headers = @{
    Authorization = "Bot $token"
    'User-Agent'  = 'CyberpunkMP-Todo (https://github.com/ofmiceandcam98-eng/CyberpunkMP, 1.0)'
}

# Send as UTF8 bytes. Invoke-RestMethod would otherwise encode the body as ISO-8859-1
# and mangle anything non-ASCII - the emoji and arrows in the list would arrive as junk.
$bytes = [System.Text.Encoding]::UTF8.GetBytes($payload)

try {
    if ($messageId) {
        $uri = "https://discord.com/api/v10/channels/$Channel/messages/$messageId"
        $result = Invoke-RestMethod -Uri $uri -Headers $headers -Method Patch -Body $bytes -ContentType 'application/json; charset=utf-8'
        Write-Host "Updated the to-do list in #$($result.channel_id)." -ForegroundColor Green
    }
    else {
        $uri = "https://discord.com/api/v10/channels/$Channel/messages"
        $result = Invoke-RestMethod -Uri $uri -Headers $headers -Method Post -Body $bytes -ContentType 'application/json; charset=utf-8'
        Write-Host "Posted the to-do list. It will be edited in place from now on." -ForegroundColor Green
    }

    $messageId = $result.id
}
catch {
    $code = $_.Exception.Response.StatusCode.value__

    # The three failures that actually happen, named rather than left as a number.
    switch ($code) {
        401 { Die "Discord says the bot token is wrong (401). Has it been reset?" }
        403 { Die "The bot cannot post in that channel (403). Give it Send Messages there." }
        404 {
            if ($messageId) {
                Die @"
That message is gone (404) - deleted, probably.

Remove the 'message=' line from tools\.discord-todo and run this again to post a
fresh one.
"@
            }
            Die "No such channel (404). Check the id, and that the bot was invited to this server."
        }
        default { Die "Discord returned $code : $($_.Exception.Message)" }
    }
}

# Written AFTER a successful send, so a failed post cannot leave a message id pointing
# at something that does not exist.
@(
    "# Written by UpdateTodoList.ps1 - safe to delete to start a new message.",
    "channel=$Channel",
    "message=$messageId"
) | Set-Content -Path $state -Encoding UTF8

Write-Host "https://discord.com/channels/@me/$Channel/$messageId" -ForegroundColor DarkGray
