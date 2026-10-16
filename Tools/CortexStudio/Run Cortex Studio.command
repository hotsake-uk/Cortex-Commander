#!/bin/sh
# macOS: double-click to build Cortex Studio from source and open it on this checkout (the same as ./dev.sh).
# The first build takes a few minutes; later ones take seconds. Close the Terminal window to stop it.
cd "$(dirname "$0")" || exit 1
./dev.sh "$@"
status=$?
if [ "$status" -ne 0 ]; then
	echo
	echo "Cortex Studio stopped with an error (see above). Press Return to close this window."
	read -r _
fi
exit "$status"
