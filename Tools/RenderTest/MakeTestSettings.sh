#!/bin/sh
# Writes a test's settings file: the releasezone2 preset (or BASE=<file>), then each overrides file and Key=Value given, in order.
# Later keys win over earlier ones when the game reads the file, so a test overrides exactly what it relies on, debug views included.
# Usage: MakeTestSettings.sh <out.ini> [overrides.ini ...] [Key=Value ...]
# e.g.   MakeTestSettings.sh /tmp/wind.ini gym.ini WorldSimOverlay=5 Wind=150
set -e
out=$1
shift
repo=$(cd "$(dirname "$0")/../.." && pwd)
base=${BASE:-$repo/Data/Presets/releasezone2.ini}
tr -d '\r' < "$base" > "$out"
for arg in "$@"; do
	case $arg in
		*=*) printf '\t%s = %s\n' "${arg%%=*}" "${arg#*=}" >> "$out" ;;
		*) tr -d '\r' < "$arg" | grep -v '^SettingsMan' >> "$out" ;;
	esac
done
