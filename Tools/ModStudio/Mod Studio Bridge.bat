@echo off
rem Runs Cortex Mod Studio as a file bridge, without opening the page: edit scripts and INI files in any editor, and a game started
rem from Studio (Test In Game, Test In Sandbox or Actor Editor) reloads them when you save. The window shows each file it sends; open the
rem address it prints to use Studio as well. Close the window to stop it.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-ModStudio.ps1" -NoBrowser %*
if errorlevel 1 pause
