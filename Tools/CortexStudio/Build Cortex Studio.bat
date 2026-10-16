@echo off
rem Builds the release Cortex Studio app and its Windows installers (.msi and setup .exe) with the page built in.
rem They land in src-tauri\target\release\bundle, and the app itself is src-tauri\target\release\cortex-studio.exe.
rem Needs Rust (rustup.rs), Node.js and Visual Studio's C++ build tools: see README.md.
cd /d "%~dp0"
call npx --yes @tauri-apps/cli@2 build
if errorlevel 1 (
	pause
	exit /b 1
)
echo.
echo Built. Installers are in %~dp0src-tauri\target\release\bundle
explorer "%~dp0src-tauri\target\release\bundle"
pause
