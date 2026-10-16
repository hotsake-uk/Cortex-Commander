@echo off
rem Starts Cortex Mod Studio for the game folder this file is in, using Windows' built-in PowerShell. Close the window to stop it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-ModStudio.ps1" %*
if errorlevel 1 pause
