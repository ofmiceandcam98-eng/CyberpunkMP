Join-and-Start Server scripts
=============================

This directory contains convenience scripts to join a host to Tailscale and start the CyberpunkMP server.

Files:

- `join-and-start-server.ps1` — Windows PowerShell script. Usage:

```powershell
.\tools\join-and-start-server.ps1 -AuthKey <your-ts-authkey> -Hostname CyberpunkMP-Server
```

- `join-and-start-server.sh` — Linux shell script. Usage:

```bash
./tools/join-and-start-server.sh <your-ts-authkey> [hostname]
```

Security:
- Do not commit auth keys into git. Provide them via the first argument or via the `TS_AUTHKEY` environment variable.

Notes:
- The scripts attempt a best-effort install of Tailscale on typical platforms (winget on Windows, apt on Debian/Ubuntu).
- They assume the repository is checked out on the target host and that the `tools/StartServer.bat` or `tools/StartServer.sh` script exists to start the server. If your host uses a custom startup method, adapt the script accordingly.
