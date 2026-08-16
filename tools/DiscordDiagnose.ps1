<#
    DiscordDiagnose.ps1 - work out why the bot cannot post

    A failed post gives you one status code, and 403 in particular is ambiguous: it covers
    "never invited", "cannot see the channel", and "can see it but cannot send". This walks
    the three checks in order so you get the actual cause instead of guessing.

        .\DiscordDiagnose.ps1

    Never prints the token.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$appId = '1536256811706089512'

# --- read credentials ---------------------------------------------------------
$token = $env:DISCORD_BOT_TOKEN
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

if (-not $token -or -not $channel) {
    Write-Host "No bot credentials found." -ForegroundColor Yellow
    Write-Host "Set DISCORD_BOT_TOKEN and DISCORD_CHANNEL_ID, or fill in tools\.discord-bot." -ForegroundColor DarkGray
    exit 1
}

$headers = @{
    'Authorization' = "Bot $token"
    'User-Agent'    = 'CyberpunkMP-Diag (https://github.com/ofmiceandcam98-eng/CyberpunkMP, 1.0)'
}

function Get-Status {
    param($ErrorRecord)
    if ($ErrorRecord.Exception.Response) { return [int]$ErrorRecord.Exception.Response.StatusCode }
    return $null
}

$invite = "https://discord.com/api/oauth2/authorize?client_id=$appId&scope=bot&permissions=18432"

# --- 1. is the token valid? ---------------------------------------------------
Write-Host ""
Write-Host "1. Token" -ForegroundColor Cyan
try {
    $me = Invoke-RestMethod "https://discord.com/api/v10/users/@me" -Headers $headers
    Write-Host "   OK - authenticated as '$($me.username)' (id $($me.id))" -ForegroundColor Green
} catch {
    if ((Get-Status $_) -eq 401) {
        Write-Host "   BAD TOKEN (401). It is wrong, or it was reset after you copied it." -ForegroundColor Red
        Write-Host "   Developer Portal -> your app -> Bot -> Reset Token, then update tools\.discord-bot." -ForegroundColor DarkGray
    } else {
        Write-Host "   FAILED: $($_.Exception.Message)" -ForegroundColor Red
    }
    exit 1
}

# --- 2. is the bot actually in a server? --------------------------------------
Write-Host ""
Write-Host "2. Server membership" -ForegroundColor Cyan
try {
    $guilds = Invoke-RestMethod "https://discord.com/api/v10/users/@me/guilds" -Headers $headers
    if (-not $guilds -or $guilds.Count -eq 0) {
        Write-Host "   NOT IN ANY SERVER." -ForegroundColor Red
        Write-Host ""
        Write-Host "   Creating a bot and inviting it are separate steps - this is the second one." -ForegroundColor DarkGray
        Write-Host "   Open this, pick the server, Authorize:" -ForegroundColor DarkGray
        Write-Host "   $invite" -ForegroundColor Yellow
        exit 1
    }
    foreach ($g in $guilds) { Write-Host "   OK - in '$($g.name)'" -ForegroundColor Green }
} catch {
    Write-Host "   FAILED: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# --- 3. can it see, and post to, the channel? ---------------------------------
Write-Host ""
Write-Host "3. Channel access" -ForegroundColor Cyan
try {
    $ch = Invoke-RestMethod "https://discord.com/api/v10/channels/$channel" -Headers $headers
    Write-Host "   OK - can see #$($ch.name)" -ForegroundColor Green
} catch {
    $s = Get-Status $_
    if ($s -eq 404) {
        Write-Host "   CHANNEL NOT FOUND (404). That id does not exist, or is in a server the bot is not in." -ForegroundColor Red
        Write-Host "   Discord -> Settings -> Advanced -> Developer Mode, then right-click the channel -> Copy Channel ID." -ForegroundColor DarkGray
    } elseif ($s -eq 403) {
        Write-Host "   NO ACCESS (403). The bot's role cannot even VIEW that channel." -ForegroundColor Red
        Write-Host "   Edit Channel -> Permissions -> add the bot's role -> allow View Channel." -ForegroundColor DarkGray
    } else {
        Write-Host "   FAILED: $($_.Exception.Message)" -ForegroundColor Red
    }
    exit 1
}

Write-Host ""
Write-Host "All three checks passed - posting should work." -ForegroundColor Green
Write-Host "Try:  .\DiscordNotify.ps1 -Title 'Test' -Message 'hello' -Level info" -ForegroundColor DarkGray
Write-Host ""
