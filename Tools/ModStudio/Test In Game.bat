@echo off
rem Starts the game on the Maginot map (not its scenario) with you in the last unit sent from Mod Studio, and swaps in each new one. No sandbox.
rem A map name can follow, for example: "Test In Game.bat" "Ketanot Hills"
cd /d "%~dp0..\.."
start "" "Cortex Command.exe" -modstudio -test %*
