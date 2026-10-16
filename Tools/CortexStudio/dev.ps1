# Runs Cortex Studio from source for working on it: a debug build of the app, serving Mod Studio's page straight from
# Tools/ModStudio, so a change to the page shows when you reload the window (Ctrl+R or F5); a change to the Rust side needs this run again.
#
#   .\dev.ps1 [-GameDir <game folder>] [-Bridge]
#
# Without -GameDir it uses this checkout as the game folder.
param(
	[string]$GameDir = '',
	[switch]$Bridge
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $GameDir) { $GameDir = [System.IO.Path]::GetFullPath((Join-Path $here '..\..')) }
$env:CORTEX_STUDIO_PAGE = [System.IO.Path]::GetFullPath((Join-Path $here '..\ModStudio'))
$appArgs = @('--game-dir', $GameDir)
if ($Bridge) { $appArgs += '--bridge' }
cargo run --manifest-path (Join-Path $here 'src-tauri\Cargo.toml') -- @appArgs
