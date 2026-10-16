#!/bin/sh
# Runs Cortex Studio from source for working on it: a debug build of the app, serving Mod Studio's page straight from
# Tools/ModStudio, so a change to the page shows when you reload the window; a change to the Rust side needs this run again.
#
#   ./dev.sh [game folder] [--bridge]
#
# Without a game folder it uses this checkout.
here=$(cd "$(dirname "$0")" && pwd)
game="$here/../.."
case "$1" in --*|"") ;; *) game="$1"; shift ;; esac
export CORTEX_STUDIO_PAGE="$here/../ModStudio"
exec cargo run --manifest-path "$here/src-tauri/Cargo.toml" -- --game-dir "$game" "$@"
