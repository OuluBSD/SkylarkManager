#!/bin/sh
# Compares two capture directories after normalization.
# Usage: compare.sh <before_dir> <after_dir>
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
A=$1
B=$2
diffs=0
total=0
for f in "$A"/*.body "$A"/*.headers; do
	[ -e "$f" ] || continue
	base=$(basename "$f")
	total=$((total + 1))
	if ! diff -q <(sh "$HERE/normalize.sh" "$f") <(sh "$HERE/normalize.sh" "$B/$base") > /dev/null 2>&1; then
		echo "DIFF $base"
		diffs=$((diffs + 1))
	fi
done
if [ "$diffs" = 0 ]; then
	echo "IDENTICAL after normalization: $total files"
else
	echo "DIFFERENT: $diffs of $total files"
	exit 1
fi
