@echo off
rem Starts the game in the Actor Editor, which shows each unit sent from Mod Studio with Preview in game.
cd /d "%~dp0..\.."
start "" "Cortex Command.exe" -editor ActorEditor
