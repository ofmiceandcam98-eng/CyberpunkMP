@echo off
setlocal
title Night City Online - server launcher

REM ===========================================================================
REM  Starts the CyberpunkMP server in its OWN window.
REM
REM  The point: the server becomes independent of this window. Close this one,
REM  close the PowerShell you ran it from, log out of your shell - the server
REM  keeps running. Only closing the SERVER'S window (or Ctrl+C in it) stops it.
REM
REM  It still gets a real console, which it needs: without one, Swan never
REM  registers its ConsoleLogger and WebApi throws on startup trying to
REM  unregister a logger that was never there. So this is not "run hidden" -
REM  it is "run in a window that is not a child of yours".
REM ===========================================================================

REM --- credentials -----------------------------------------------------------
REM Set these ONCE, in your own terminal, and they persist across reboots:
REM
REM     setx CYBERPUNKMP_ADMIN_USERNAME "admin"
REM     setx CYBERPUNKMP_ADMIN_PASSWORD "your-password"
REM
REM Open a NEW terminal afterwards - setx does not affect the one you typed it
REM in. Nothing is stored in this file, so it is safe to share or commit.

if "%CYBERPUNKMP_ADMIN_USERNAME%"=="" goto :nocreds
if "%CYBERPUNKMP_ADMIN_PASSWORD%"=="" goto :nocreds

REM --- locate the server ------------------------------------------------------
set "SERVER_DIR=%~dp0..\build\windows\x64\release"

if not exist "%SERVER_DIR%\Server.Loader.exe" (
    echo.
    echo   Could not find Server.Loader.exe at:
    echo     %SERVER_DIR%
    echo.
    echo   Build it first:  xmake build Server.Loader
    echo.
    pause
    exit /b 1
)

REM --- already running? -------------------------------------------------------
tasklist /FI "IMAGENAME eq Server.Loader.exe" 2>nul | find /I "Server.Loader.exe" >nul
if not errorlevel 1 (
    echo.
    echo   The server is already running.
    echo   Close its window first if you want to restart it.
    echo.
    pause
    exit /b 0
)

REM --- start it, detached -----------------------------------------------------
REM `start` with a title gives the server its own console window and returns
REM immediately. The new process is not a child of this shell, so nothing that
REM happens to this window can take the server down with it.
echo.
echo   Starting server...
echo.

start "CyberpunkMP Server" /D "%SERVER_DIR%" "%SERVER_DIR%\Server.Loader.exe"

echo   Server started in its own window.
echo.
echo   It will keep running if you close this window or your terminal.
echo   To stop it: close the "CyberpunkMP Server" window, or press Ctrl+C in it.
echo.
timeout /t 4 >nul
exit /b 0

:nocreds
echo.
echo   Admin credentials are not set.
echo.
echo   Release builds refuse to start without them. Set them once, in your own
echo   terminal, then open a NEW terminal and run this again:
echo.
echo       setx CYBERPUNKMP_ADMIN_USERNAME "admin"
echo       setx CYBERPUNKMP_ADMIN_PASSWORD "pick-a-real-password"
echo.
echo   The admin panel binds to 0.0.0.0, so it is reachable from your network
echo   and your tailnet. Do not use a throwaway password.
echo.
pause
exit /b 1
