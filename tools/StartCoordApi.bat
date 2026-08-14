@echo off
REM Starts the assistant coordination API and opens the key console.
REM
REM The console is where the keys live, and it only answers to this machine - so opening
REM it here is the whole setup step. Leave this window running.

title Night City Online - coordination API

cd /d "%~dp0.."

start "" http://127.0.0.1:11780/

node code\coord-api\server.js %*

echo.
echo The coordination API has stopped.
pause
