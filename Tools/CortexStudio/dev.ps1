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
$env:CORTEX_STUDIO_PAGE = [System.IO.Path]::GetFullPath((Join-Path $here '..\ModStudio'))
$appArgs = @()
if ($GameDir) { $appArgs += @('--game-dir', $GameDir) }
if ($Bridge) { $appArgs += '--bridge' }
cargo run --manifest-path (Join-Path $here 'src-tauri\Cargo.toml') -- @appArgs
