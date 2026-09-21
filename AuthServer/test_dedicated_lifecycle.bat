@echo off
chcp 65001 > nul
title "Dedicated Server Lifecycle Test"

where pwsh >nul 2>nul
if %errorlevel% equ 0 (
    set "PS_EXE=pwsh"
) else (
    set "PS_EXE=powershell"
)

%PS_EXE% -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_dedicated_lifecycle.ps1" %*

echo.
pause
