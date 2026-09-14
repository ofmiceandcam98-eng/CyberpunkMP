# PullLogs.ps1 - a player's latest client logs and launcher trail, in one command.
#
# The recurring diagnostic move on this project is "read <player>'s client log and their
# launcher trail" - and every time, somebody re-derives the server path, the file naming,
# and the ssh incantation by hand. This encodes all of it once:
#
#   .\tools\PullLogs.ps1 zeldfep              # newest 3 client logs + launcher-trail.log
#   .\tools\PullLogs.ps1 noremacxxi -Count 5
#   .\tools\PullLogs.ps1                      # no player: list who has logs
#
# THE QUIRK THAT COSTS PEOPLE AN HOUR: client logs ship to the server ON GAME EXIT. The
# session currently being played is NOT here yet - the newest file below is the newest
# FINISHED session. The launcher trail ships on its own cadence and is usually fresher.
#
# The server account and host are NOT in this file - the repository is public. They come
# from tools\ship.local.ps1 (gitignored) as $ServerUser / $ServerHost, or the environment
# as NCO_SERVER_USER / NCO_SERVER_HOST. See tools\ship.local.example.ps1.
[CmdletBinding()]
param(
    [string]$Player,
    [int]$Count = 3,
    [string]$Dest
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Environment.ps1')

$user = if ($env:NCO_SERVER_USER) { $env:NCO_SERVER_USER } else { $script:ServerUser }
$hostName = if ($env:NCO_SERVER_HOST) { $env:NCO_SERVER_HOST } else { $script:ServerHost }

if (-not $user -or -not $hostName) {
    Write-Host "No server configured." -ForegroundColor Red
    Write-Host "  Set `$ServerUser and `$ServerHost in tools\ship.local.ps1, or NCO_SERVER_USER" -ForegroundColor Yellow
    Write-Host "  and NCO_SERVER_HOST in the environment. Real values are NOT in this repo -" -ForegroundColor Yellow
    Write-Host "  see docs\deploy\ADDRESSES.example.md. (These are the CURRENT server, not the" -ForegroundColor Yellow
    Write-Host "  retired NAS the older `$NasUser/`$NasHost variables may still name.)" -ForegroundColor Yellow
    exit 1
}

$remoteRoot = '/mnt/vol/projects/CyberpunkMP/logs/clients'

# Fail fast instead of hanging on an interactive prompt when the key is wrong or the host
# key is unknown - the flags BackupServerData.ps1 carried and this script had dropped. A
# bad key should surface as an ssh error, not a silent wait on "Are you sure you want to
# continue connecting?" with nobody at the keyboard. LogLevel=ERROR also mutes the
# known-hosts add notice that StrictHostKeyChecking=no would otherwise print.
$sshOpts = @('-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=no', '-o', 'LogLevel=ERROR')

# No player named: answer the question that comes first anyway.
if (-not $Player) {
    Write-Host "Players with shipped logs on $hostName`:" -ForegroundColor Cyan
    ssh @sshOpts "$user@$hostName" "ls -1 $remoteRoot" | ForEach-Object { Write-Host "  $_" }
    Write-Host "`nPull one:  .\tools\PullLogs.ps1 <player>" -ForegroundColor DarkGray
    exit 0
}

# The player dir is the SANITIZED in-game name: the server's WebApi SanitizeName keeps
# only letters, digits, '.', '_' and '-' (case preserved) before creating the dir, so
# "Johnny Silverhand" ships to JohnnySilverhand. Mirror it here - it also keeps spaces
# and shell metacharacters out of the remote command line.
$dirName = -join ($Player.ToCharArray() | Where-Object { [char]::IsLetterOrDigit($_) -or $_ -eq '.' -or $_ -eq '_' -or $_ -eq '-' })
$remote = "$remoteRoot/$dirName"

$names = ssh @sshOpts "$user@$hostName" "ls -t $remote 2>/dev/null"
if ($LASTEXITCODE -eq 255) {
    # 255 is ssh itself failing - transport, not a missing player. Diagnosing this as a
    # wrong name sent people chasing case-sensitivity while the tailnet was down.
    Write-Host "  FAIL  could not reach $user@$hostName (ssh exit 255)" -ForegroundColor Red
    Write-Host "        what   the CONNECTION failed - host down, tailnet off, or key rejected; the player name was never checked" -ForegroundColor DarkYellow
    Write-Host "        where  ssh $user@$hostName" -ForegroundColor DarkYellow
    Write-Host "        fix    check 'tailscale status', then try the ssh by hand" -ForegroundColor DarkYellow
    exit 1
}
if ($LASTEXITCODE -ne 0 -or -not $names) {
    Write-Host "  FAIL  no logs for '$Player'" -ForegroundColor Red
    Write-Host "        what   no dir '$dirName' on the server (the in-game name reduced to letters/digits/._-, case-sensitive) - or that player has never finished a session with log shipping on" -ForegroundColor DarkYellow
    Write-Host "        where  $user@$hostName`:$remote" -ForegroundColor DarkYellow
    Write-Host "        fix    run .\tools\PullLogs.ps1 with no player to list who has logs" -ForegroundColor DarkYellow
    exit 1
}

$names = @($names)
$clientLogs = @($names | Where-Object { $_ -like 'CyberpunkMP_*.log' } | Select-Object -First $Count)
$trail = $names | Where-Object { $_ -eq 'launcher-trail.log' } | Select-Object -First 1

if (-not $Dest) { $Dest = Join-Path $env:TEMP "nco-logs\$Player" }
New-Item -ItemType Directory -Force $Dest | Out-Null

$wanted = @(@($clientLogs) + @($trail) | Where-Object { $_ })

# One scp, one SSH connection, all files - instead of a connect-per-file loop. The names
# are sanitized log basenames (letters/digits/._-, no spaces or shell metacharacters - see
# the SanitizeName note above), so a remote brace list is safe: "dir/{a,b,c}" reaches the
# remote shell, which expands it to the three files. That brace expansion is a SHELL
# feature, so it needs scp's legacy transfer mode (-O); SFTP-mode scp (OpenSSH 9+, the
# default now) globs "*?[]" but never "{}", and would look for a literal "{a,b,c}". A lone
# file gets no braces at all (bash would leave "{a}" untouched).
$pulled = @()
if ($wanted.Count -gt 0) {
    $remoteSpec = if ($wanted.Count -eq 1) { "$remote/$($wanted[0])" } else { "$remote/{$($wanted -join ',')}" }
    scp -O -q @sshOpts "$user@$hostName`:$remoteSpec" $Dest

    # scp's exit status is for the whole batch; check each file so a missing one is
    # reported without discarding the rest.
    foreach ($name in $wanted) {
        $local = Join-Path $Dest $name
        if (Test-Path $local) { $pulled += $local }
        else { Write-Host "  warn  could not pull $name" -ForegroundColor DarkYellow }
    }
}

Write-Host "Pulled $($pulled.Count) file(s) for $Player`:" -ForegroundColor Cyan
foreach ($p in $pulled) {
    $item = Get-Item $p
    Write-Host ("  {0}  {1,10:n0} bytes  {2}" -f $item.LastWriteTime.ToString('MM-dd HH:mm'), $item.Length, $item.FullName)
}
Write-Host "`nRemember: client logs ship on GAME EXIT - a session being played right now is not here yet." -ForegroundColor DarkGray
