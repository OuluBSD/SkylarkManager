#!/bin/sh
# Copies the WebFramework assets into <bin_dir>/static/WebFramework/.
# Usage: deploy-assets.sh <bin_dir> [<bin_dir> ...]
# Run before any test or render of a binary that uses WebFramework.
HERE=$(cd "$(dirname "$0")" && pwd)
for BIN in "$@"; do
	DEST="$BIN/static/WebFramework"
	mkdir -p "$DEST" || exit 1
	cp "$HERE"/assets/*.css "$HERE"/assets/*.js "$DEST"/ || exit 1
	echo "deployed $(ls "$DEST" | wc -l) assets to $DEST"
done
