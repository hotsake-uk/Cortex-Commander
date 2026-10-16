@echo off
rem Starts the game in the sandbox with the last unit sent from Mod Studio, and swaps in each new one when you press Preview in game.
rem A map name can follow, for example: "Test In Sandbox.bat" "Ketanot Hills"
cd /d "%~dp0..\.."
start "" "Cortex Command.exe" -modstudio -sandbox %*
