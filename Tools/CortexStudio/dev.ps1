# Runs Cortex Studio from source for working on it: a debug build of the app, serving Mod Studio's page straight from
# Tools/ModStudio, so a change to the page shows when you reload the window (Ctrl+R or F5); a change to the Rust side needs this run again.
#
#   .\dev.ps1 [-GameDir <game folder>] [-Bridge]
#
# Without -GameDir it uses the game folder Studio used last (a version the Game screen launched, or this checkout after "Edit the repo
# checkout"), else this checkout.
param(
	[string]$GameDir = '',
	[switch]$Bridge
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$page = [System.IO.Path]::GetFullPath((Join-Path $here '..\ModStudio'))
if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
	Write-Error "Node.js isn't installed (Studio's page is built with it). Install it from https://nodejs.org, then open a new terminal."
}
if (-not (Test-Path (Join-Path $page 'node_modules\vite'))) {
	Push-Location $page; npm ci --no-audit --no-fund; Pop-Location
}
# cargo's build script builds the page once; Vite then rebuilds it on every change while Studio runs.
$watcher = Start-Process npm.cmd -ArgumentList 'run', 'watch', '--silent', '--', '--logLevel', 'warn' -WorkingDirectory $page -NoNewWindow -PassThru
$env:CORTEX_STUDIO_PAGE = Join-Path $page 'dist'
$appArgs = @()
if ($GameDir) { $appArgs += @('--game-dir', $GameDir) }
if ($Bridge) { $appArgs += '--bridge' }
try {
	cargo run --manifest-path (Join-Path $here 'src-tauri\Cargo.toml') -- @appArgs
} finally {
	if ($watcher -and -not $watcher.HasExited) { taskkill /T /F /PID $watcher.Id | Out-Null }
}
