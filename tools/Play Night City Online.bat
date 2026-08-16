@echo off
setlocal
title Night City Online

REM ===========================================================================
REM  Double-click this to play.
REM
REM  This is the only file you need. It fetches everything else itself, updates
REM  the mod to the current build, and starts Cyberpunk with the right flags -
REM  so you never set Steam launch options and never remember to update.
REM
REM  Put the server address between the quotes below. Leave it empty to launch
REM  in multiplayer mode without connecting anywhere.
REM ===========================================================================

set "SERVER="
set "PORT=11778"

REM ===========================================================================
REM  Nothing below here needs editing.
REM ===========================================================================

set "UPDATER_URL=https://github.com/ofmiceandcam98-eng/CyberpunkMP/releases/download/test-2026.08.12-probes/UpdateMod.ps1"
set "UPDATER=%TEMP%\NightCityOnline_UpdateMod.ps1"

echo.
echo   Night City Online
echo   -----------------
echo.

REM Always fetch the updater fresh, so improvements reach everyone without
REM anyone having to re-download this file.
echo   Checking for updates...
powershell -ExecutionPolicy Bypass -NoProfile -Command ^
  "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; try { Invoke-WebRequest -Uri '%UPDATER_URL%' -OutFile '%UPDATER%' -UseBasicParsing; exit 0 } catch { exit 1 }"

if errorlevel 1 (
    echo.
    echo   Could not reach GitHub to check for updates.
    echo   Check your internet connection and try again.
    echo.
    pause
    exit /b 1
)

if not exist "%UPDATER%" (
    echo.
    echo   The updater did not download correctly.
    echo.
    pause
    exit /b 1
)

powershell -ExecutionPolicy Bypass -NoProfile -File "%UPDATER%" -Launch -Quiet -Server "%SERVER%" -Port %PORT%

if errorlevel 1 (
    echo.
    echo   Something went wrong above - the game was not started.
    echo.
    pause
    exit /b 1
)

REM The game is starting in its own window; this one has nothing left to say.
exit /b 0
