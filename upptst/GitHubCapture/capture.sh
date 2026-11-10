#!/bin/sh
# Headless GitHub route capture for ManagerSkylark.
# Boots the binary on a temp site with fixture repos and the emulated GitHub backend, logs in,
# fetches every route in routes.txt into <out_dir>, then stops the server and
# deletes its temp data.
# Usage: capture.sh <ManagerSkylark binary> <out_dir> [port]
set -eu
BIN=$1
OUT=$2
PORT=${3:-18400}
HERE=$(cd "$(dirname "$0")" && pwd)
export TZ=UTC LANG=C
WORK=$(mktemp -d "${TMPDIR:-/tmp}/ms-capture.XXXXXX")
SRV=
cleanup() {
	[ -n "$SRV" ] && kill "$SRV" 2>/dev/null || true
	wait "$SRV" 2>/dev/null || true
	rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

mkdir -p "$WORK/site" "$WORK/dev" "$OUT"
sh "$HERE/make-fixtures.sh" "$WORK/dev"
sh "$HERE/make-store.sh" "$WORK/site"
MANAGERSKYLARK_SITE_PASSWORD=capture-pw "$BIN" --site-dir "$WORK/site" --set-site-password capture > "$WORK/setup.log" 2>&1
DEV_DIR="$WORK/dev" "$BIN" --site-dir "$WORK/site" --github-backend emulated --github-store "$WORK/github" --port "$PORT" > "$WORK/server.log" 2>&1 &
SRV=$!

up=0
for i in $(seq 1 100); do
	if curl -s -o /dev/null "http://127.0.0.1:$PORT/login" 2>/dev/null; then up=1; break; fi
	sleep 0.1
done
[ "$up" = 1 ] || { echo "server did not start; see $WORK/server.log" >&2; exit 1; }

curl -s -c "$WORK/jar" -o /dev/null --data "username=capture&password=capture-pw" \
	"http://127.0.0.1:$PORT/login/submit"

n=0
while IFS= read -r route; do
	[ -n "$route" ] || continue
	n=$((n + 1))
	name=$(printf '%s' "$route" | sed 's#[^A-Za-z0-9]#_#g')
	curl -s -b "$WORK/jar" -D "$OUT/$name.headers" -o "$OUT/$name.body" "http://127.0.0.1:$PORT$route"
	printf '%s %s\n' "$name" "$route" >> "$OUT/index.txt.tmp"
done < "$HERE/routes.txt"
mv "$OUT/index.txt.tmp" "$OUT/index.txt"
echo "captured $n routes into $OUT"
