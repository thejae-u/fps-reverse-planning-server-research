@echo off
chcp 65001 > nul
title "AuthServer & Dedicated Server Match Test"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test_match_flow.ps1"

echo.
echo Press any key to exit.
pause > nul
