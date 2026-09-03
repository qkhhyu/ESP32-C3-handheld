@echo off
setlocal EnableExtensions
title ESP32 Codex Usage Bridge

rem This script deliberately reads the LAN token from ignored usage_secrets.h.
rem It never prints the token and never handles the Codex OAuth credential.
set "PROJECT_DIR=%~dp0.."
for %%I in ("%PROJECT_DIR%") do set "PROJECT_DIR=%%~fI"
set "SECRETS_FILE=%PROJECT_DIR%\main\usage_secrets.h"

if not exist "%SECRETS_FILE%" (
    echo ERROR: Missing %SECRETS_FILE%
    echo Copy main\usage_secrets.example.h to main\usage_secrets.h first.
    pause
    exit /b 1
)

for /f tokens^=2^ delims^=^" %%T in ('findstr /r /c:"^[ ]*#define[ ]*CODEX_BRIDGE_TOKEN" "%SECRETS_FILE%"') do set "BRIDGE_TOKEN=%%T"

if not defined BRIDGE_TOKEN (
    echo ERROR: CODEX_BRIDGE_TOKEN is not configured.
    pause
    exit /b 1
)

if "%BRIDGE_TOKEN%"=="" (
    echo ERROR: CODEX_BRIDGE_TOKEN is empty.
    pause
    exit /b 1
)

netstat -ano | findstr /r /c:":8787 .*LISTENING" >nul
if not errorlevel 1 (
    echo Codex usage bridge is already listening on port 8787.
    pause
    exit /b 0
)

echo Starting Codex usage bridge at http://192.168.1.247:8787/usage
echo Keep this window open. Press Ctrl+C to stop the bridge.
echo.
python "%PROJECT_DIR%\tools\codex_usage_bridge.py" --token "%BRIDGE_TOKEN%" --bind 192.168.1.247 --port 8787

echo.
echo Bridge stopped or failed to start. Check the message above.
pause
