@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0console\start-console.ps1"
if errorlevel 1 pause
