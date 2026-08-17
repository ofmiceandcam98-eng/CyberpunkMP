<#
.SYNOPSIS
  Join the machine to Tailscale using an auth key and start the CyberpunkMP server.

.NOTES
  - Do NOT hardcode auth keys into the repository. Provide via argument or environment
    variable `TS_AUTHKEY`.
  - This script assumes the repo is checked out on the target host and run from the
    repository root.
#>

param(
    [string]$AuthKey = $env:TS_AUTHKEY,
    [string]$Hostname = "CyberpunkMP-Server",
    [switch]$ForceInstall
)

function Ensure-Tailscale {
    if (Get-Command tailscale -ErrorAction SilentlyContinue) {
        Write-Host "Tailscale is already installed"
        return
    }

    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Write-Error "winget not found. Install Tailscale manually or run winget."; exit 2
    }

    Write-Host "Installing Tailscale via winget..."
    winget install --id=Tailscale.Tailscale -e --accept-package-agreements --accept-source-agreements
}

if (-not $AuthKey) {
    Write-Error "No Tailscale auth key supplied. Pass as -AuthKey or set TS_AUTHKEY environment variable."; exit 2
}

if ($ForceInstall) { Ensure-Tailscale }

if (-not (Get-Command tailscale -ErrorAction SilentlyContinue)) {
    Write-Host "Tailscale not found. Attempting install..."
    Ensure-Tailscale
}

Write-Host "Bringing up Tailscale with hostname $Hostname"
& tailscale up --authkey=$AuthKey --hostname=$Hostname

if ($LASTEXITCODE -ne 0) {
    Write-Error "tailscale up failed with exit code $LASTEXITCODE"; exit $LASTEXITCODE
}

Write-Host "Tailscale is up. Starting the game server..."

$startScript = Join-Path -Path (Get-Location) -ChildPath "tools\StartServer.bat"
if (Test-Path $startScript) {
    Write-Host "Found $startScript — launching."
    Start-Process -FilePath $startScript -WorkingDirectory (Get-Location) -NoNewWindow
    Write-Host "Server start command launched (detached)."
} else {
    Write-Warning "No Windows start script found at tools\StartServer.bat. Start the server manually or adapt this script."
}

Write-Host "Done. Check the coordination API health endpoint on :11780 once the server is running."
