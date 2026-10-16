#!/bin/sh
# Checks that a built Cortex Studio starts and answers: makes a tiny game folder, starts the app with no window (--bridge) the way
# dev.sh does, and asks it for /api/info with the token it printed. Used by the Cortex Studio workflow on macOS and Linux.
#
#   ./smoke.sh <path to the cortex-studio program> [page folder]
set -u
exe="${1:?usage: smoke.sh <path to the cortex-studio program> [page folder]}"
here=$(cd "$(dirname "$0")" && pwd)
page="${2:-$here/../ModStudio}"

work=$(mktemp -d)
mkdir -p "$work/game/Data/Base.rte" "$work/game/Mods" "$work/config"
log="$work/out.log"
pid=""
cleanup() {
	[ -n "$pid" ] && kill "$pid" 2>/dev/null
	rm -rf "$work"
}
trap cleanup EXIT INT TERM

# Keep the settings Studio saves out of the real config folder.
export HOME="$work"
export XDG_CONFIG_HOME="$work/config"
export CORTEX_STUDIO_PAGE="$page"

"$exe" --game-dir "$work/game" --bridge --port 0 >"$log" 2>&1 &
pid=$!

url=""
i=0
while [ "$i" -lt 60 ]; do
	url=$(sed -n 's/^Open: //p' "$log" | head -n 1)
	[ -n "$url" ] && break
	kill -0 "$pid" 2>/dev/null || break
	sleep 0.5
	i=$((i + 1))
done
if [ -z "$url" ]; then
	echo "Cortex Studio didn't start. Its output:" >&2
	cat "$log" >&2
	exit 1
fi

base=${url%%\?*}
token=${url##*token=}
body=$(curl -fsS -H "X-ModStudio-Token: $token" "${base}api/info") || {
	echo "Cortex Studio started at $url but /api/info didn't answer." >&2
	cat "$log" >&2
	exit 1
}
case "$body" in
	*'"app":"Cortex Studio"'*) echo "OK: $body" ;;
	*) echo "Unexpected answer from /api/info: $body" >&2; exit 1 ;;
esac

# The page itself must come from the folder we pointed it at.
curl -fsS "${base}" >/dev/null || { echo "The Mod Studio page didn't load from $page." >&2; exit 1; }
echo "OK: the page loads."
