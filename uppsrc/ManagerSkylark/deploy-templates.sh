#!/bin/sh
# Copies the ManagerSkylark witz templates into <bin_dir>. The app's default
# template root is the executable's directory, so the templates must sit beside
# the binary. Usage: deploy-templates.sh <bin_dir> [<bin_dir> ...]
# Run before any test or render of a binary that renders GitHub pages.
HERE=$(cd "$(dirname "$0")" && pwd)
for BIN in "$@"; do
	cp "$HERE"/templates/*.witz "$BIN"/ || exit 1
	echo "deployed $(ls "$HERE"/templates/*.witz | wc -l) templates to $BIN"
done
