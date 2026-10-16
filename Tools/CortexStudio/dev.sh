#!/bin/sh
# Runs Cortex Studio from source for working on it: a debug build of the app, serving Mod Studio's page from Tools/ModStudio/dist,
# which Vite rebuilds whenever a file in Tools/ModStudio changes, so a change to the page shows when you reload the window; a change
# to the Rust side needs this run again. Needs Node.js for Vite (the first run installs it into Tools/ModStudio/node_modules).
#
#   ./dev.sh [game folder] [--bridge]
#
# Without a game folder it uses the one Studio used last (a version the Game screen launched, or this checkout after "Edit the repo
# checkout"), else this checkout. On macOS you can also double-click "Run Cortex Studio.command".
here=$(cd "$(dirname "$0")" && pwd)
game=""
case "$1" in --*|"") ;; *) game="$1"; shift ;; esac

# A terminal opened before Rust was installed, or a double-click, may not have cargo on its PATH yet.
if ! command -v cargo >/dev/null 2>&1 && [ -f "$HOME/.cargo/env" ]; then
	. "$HOME/.cargo/env"
fi
if ! command -v cargo >/dev/null 2>&1; then
	echo "Rust isn't installed (or this terminal doesn't know about it yet). Install it from https://rustup.rs, then open a new terminal." >&2
	exit 1
fi

case "$(uname -s)" in
	Darwin)
		if ! xcode-select -p >/dev/null 2>&1; then
			echo "Xcode's command line tools are missing. Install them with:  xcode-select --install" >&2
			exit 1
		fi
		;;
	Linux)
		if command -v pkg-config >/dev/null 2>&1 && ! pkg-config --exists webkit2gtk-4.1 2>/dev/null; then
			echo "The WebKitGTK development files look to be missing. On Debian or Ubuntu:" >&2
			echo "  sudo apt install libwebkit2gtk-4.1-dev libayatana-appindicator3-dev librsvg2-dev libxdo-dev" >&2
		fi
		;;
esac

if [ -n "$game" ] && [ ! -d "$game/Data/Base.rte" ]; then
	echo "$game has no Data/Base.rte, so it isn't a game folder. Pass the folder that has Data/Base.rte inside:  ./dev.sh /path/to/game" >&2
	exit 1
fi

if ! command -v npm >/dev/null 2>&1; then
	echo "Node.js isn't installed (Studio's page is built with it). Install it from https://nodejs.org, then open a new terminal." >&2
	exit 1
fi
page="$here/../ModStudio"
# cargo's build script builds the page once; Vite then rebuilds it on every change while Studio runs.
(cd "$page" && { [ -d node_modules/vite ] || npm ci --no-audit --no-fund; } && exec npm run watch --silent -- --logLevel warn) &
watcher=$!
trap 'kill $watcher 2>/dev/null' EXIT INT TERM

export CORTEX_STUDIO_PAGE="$page/dist"
if [ -n "$game" ]; then
	set -- --game-dir "$game" "$@"
fi
cargo run --manifest-path "$here/src-tauri/Cargo.toml" -- "$@"
