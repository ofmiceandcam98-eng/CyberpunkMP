<#
    DiscordNotify.ps1 - post a project update to #server-update in Night City Online

    Supports two ways of posting. If both are configured, the BOT is used.

    ---------------------------------------------------------------------------
    OPTION A - BOT  (application 1536256811706089512)
    ---------------------------------------------------------------------------
    Posts as a real server member with your own name and avatar, can be scoped to
    specific channels with Discord's normal permission system, and leaves room to add
    slash commands later. Posting needs only the REST API - no gateway connection and
    nothing to keep running.

    Set up in the developer portal, then:
        setx DISCORD_BOT_TOKEN  "your-bot-token"
        setx DISCORD_CHANNEL_ID "the-server-update-channel-id"

    or write two lines into  tools\.discord-bot  (gitignored):
        token=...
        channel=...

    ---------------------------------------------------------------------------
    OPTION B - WEBHOOK  (fallback)
    ---------------------------------------------------------------------------
        setx NIGHT_CITY_WEBHOOK "https://discord.com/api/webhooks/..."
    or the URL as the only line of  tools\.discord-webhook  (gitignored).

    ---------------------------------------------------------------------------
    A BOT TOKEN IS A PASSWORD, and a stronger one than a webhook URL: it grants
    access to everything the bot can reach, not just one channel. Never commit it,
    never paste it into a chat or screenshot, and if it is ever exposed, hit
    "Reset Token" in the portal immediately - that invalidates the old one.
    ---------------------------------------------------------------------------

    USAGE:
      .\DiscordNotify.ps1 -Title "Probe build deployed" -Message "Ten probes added..."
      .\DiscordNotify.ps1 -Title "Fixed" -Message "..." -Level success
      .\DiscordNotify.ps1 -Title "Test" -Message "hello" -DryRun
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Title,
    [Parameter(Mandatory = $true)][string]$Message,
    [ValidateSet('info', 'success', 'warn', 'blocked')][string]$Level = 'info',
    [string]$Footer,
    # "name=value;name=value". Deliberately ONE string, not an array: when this script is
    # invoked via `powershell.exe -File`, an array parameter binds only its first value and
    # the rest slide onto the following parameters positionally, which silently mangles the
    # call. A single delimited string passes through -File intact.
    [string]$Fields,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

# Discord requires modern TLS; PowerShell 5.1 does not always negotiate it by default.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Get-WebhookUrl {
    if ($env:NIGHT_CITY_WEBHOOK) { return $env:NIGHT_CITY_WEBHOOK.Trim() }

    $file = Join-Path $PSScriptRoot '.discord-webhook'
    if (Test-Path $file) {
        $fromFile = (Get-Content $file -Raw).Trim()
        if ($fromFile) { return $fromFile }
    }

    return $null
}

function Get-BotCredentials {
    $token   = $env:DISCORD_BOT_TOKEN
    $channel = $env:DISCORD_CHANNEL_ID

    $file = Join-Path $PSScriptRoot '.discord-bot'
    if (Test-Path $file) {
        foreach ($line in Get-Content $file) {
            $t = $line.Trim()
            if (-not $t -or $t.StartsWith('#')) { continue }

            $kv = $t.Split('=', 2)
            if ($kv.Count -ne 2) { continue }

            switch ($kv[0].Trim().ToLower()) {
                'token'   { if (-not $token)   { $token   = $kv[1].Trim() } }
                'channel' { if (-not $channel) { $channel = $kv[1].Trim() } }
            }
        }
    }

    if ($token -and $channel) {
        return @{ Token = $token.Trim(); Channel = $channel.Trim() }
    }

    return $null
}

# Left stripe on the embed. Cyberpunk yellow for ordinary updates.
$colors = @{
    info    = 16575498   # #FCEE0A
    success = 4034140    # #3D8A5C
    warn    = 14582819   # #DE8123
    blocked = 11875871   # #B53A1F
}

$embed = @{
    title       = $Title
    description = $Message
    color       = $colors[$Level]
    timestamp   = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
}

if ($Footer) {
    $embed.footer = @{ text = $Footer }
}

# -Fields "Branch=main;Build=ok"
if ($Fields) {
    $list = @()
    foreach ($pair in $Fields.Split(';')) {
        if (-not $pair.Trim()) { continue }

        $split = $pair.Split('=', 2)
        if ($split.Count -ne 2) { continue }

        $name  = $split[0].Trim()
        $value = $split[1].Trim()

        if ($name -and $value) {
            $list += @{ name = $name; value = $value; inline = $true }
        }
    }
    if ($list.Count -gt 0) { $embed.fields = $list }
}

$payload = @{ embeds = @($embed) } | ConvertTo-Json -Depth 6 -Compress

if ($DryRun) {
    Write-Host "DRY RUN - nothing was sent. Payload:" -ForegroundColor Yellow
    Write-Host $payload
    return
}

$bot = Get-BotCredentials
$url = Get-WebhookUrl

if (-not $bot -and -not $url) {
    Write-Host ""
    Write-Host "Nothing configured - no message sent." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Bot:     set DISCORD_BOT_TOKEN and DISCORD_CHANNEL_ID," -ForegroundColor DarkGray
    Write-Host "           or create tools\.discord-bot with token= and channel= lines." -ForegroundColor DarkGray
    Write-Host "  Webhook: set NIGHT_CITY_WEBHOOK, or create tools\.discord-webhook." -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "The header of this script has the full steps." -ForegroundColor DarkGray
    Write-Host ""
    exit 1
}

try {
    if ($bot) {
        if ($bot.Channel -notmatch '^\d{5,}$') {
            Write-Host "DISCORD_CHANNEL_ID should be all digits - got '$($bot.Channel)'." -ForegroundColor Red
            Write-Host "Enable Developer Mode in Discord, right-click #server-update, Copy Channel ID." -ForegroundColor DarkGray
            exit 1
        }

        $headers = @{
            'Authorization' = "Bot $($bot.Token)"
            'User-Agent'    = 'CyberpunkMP-Notify (https://github.com/ofmiceandcam98-eng/CyberpunkMP, 1.0)'
        }

        $endpoint = "https://discord.com/api/v10/channels/$($bot.Channel)/messages"

        Invoke-RestMethod -Uri $endpoint -Method Post -Headers $headers `
                          -ContentType 'application/json' -Body $payload | Out-Null

        Write-Host "Posted as the bot: $Title" -ForegroundColor Green
    }
    else {
        if ($url -notmatch '^https://(discord\.com|discordapp\.com)/api/webhooks/') {
            Write-Host "That does not look like a Discord webhook URL. Refusing to send." -ForegroundColor Red
            exit 1
        }

        Invoke-RestMethod -Uri $url -Method Post -ContentType 'application/json' -Body $payload | Out-Null
        Write-Host "Posted via webhook: $Title" -ForegroundColor Green
    }
} catch {
    $status = $null
    $detail = $null

    if ($_.Exception.Response) {
        $status = [int]$_.Exception.Response.StatusCode

        # Discord explains 400s in the response body. Reading it turns "Bad Request" into
        # the actual field that was wrong, which is the difference between a fix and an
        # afternoon of bisecting.
        try {
            $reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
            $detail = $reader.ReadToEnd()
            $reader.Close()
        } catch { }
    }

    Write-Host "Failed to post: $($_.Exception.Message)" -ForegroundColor Red
    if ($detail) { Write-Host "  Discord said: $detail" -ForegroundColor DarkGray }

    # The three failures that actually happen, and what each one means.
    switch ($status) {
        401 { Write-Host "  401 - the bot token is wrong or was reset. Copy it again from the portal." -ForegroundColor DarkGray }
        403 { Write-Host "  403 - the bot is in the server but cannot post in that channel." -ForegroundColor DarkGray
              Write-Host "        Check the channel's permissions for the bot's role: Send Messages + Embed Links." -ForegroundColor DarkGray }
        404 { Write-Host "  404 - that channel ID does not exist, or the bot was never invited to the server." -ForegroundColor DarkGray }
    }
    exit 1
}
