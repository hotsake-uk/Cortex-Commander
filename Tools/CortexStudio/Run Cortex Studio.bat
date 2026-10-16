@echo off
rem Builds Cortex Studio from source (a debug build) and runs it on the game folder it used last (this checkout the first time). Mod Studio's page is served
rem straight from Tools\ModStudio, so after editing it press Ctrl+R or F5 in the app. After changing the Rust code, run this again.
rem Add a game folder to use another one, or -Bridge for the file bridge with no window:  "Run Cortex Studio.bat" -GameDir D:\Games\Cortex -Bridge
rem Needs Rust (rustup.rs) and Visual Studio's C++ build tools: see README.md.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0dev.ps1" %*
if errorlevel 1 pause
