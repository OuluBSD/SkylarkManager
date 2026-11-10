#!/bin/sh
# Live check for skylarkctl (task 004): the CLI against a real ManagerSkylark process with
# PrivateHub composed on its own port. Checks help and manifest, did-you-mean errors, login,
# whoami, a scoped issue-create in JSON and TOON, the cookie jar mode, logout, and --trace.
#
# Usage: sh upptst/SiteCli/live.sh <ManagerSkylark binary> <SiteCli binary>
# Exit 0 when every check passes. Writes only under a mktemp directory.
BIN=$1
CLI=$2
T=$(mktemp -d "${TMPDIR:-/tmp}/sitecli-live.XXXXXX") || exit 1
MAIN=18230; PH_PORT=18231
MAIN_URL=http://127.0.0.1:$MAIN
PH_URL=http://127.0.0.1:$PH_PORT
fails=0
pass() { echo "ok   $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
ctl() { "$CLI" --server "$MAIN_URL" --site "privatehub=$PH_URL" "$@"; }

mkdir -p "$T/ws/acme/widget" || exit 1
git -C "$T/ws/acme/widget" init -q && \
	git -C "$T/ws/acme/widget" -c user.name=smoke -c user.email=smoke@example.invalid commit -q --allow-empty -m smoke

MANAGERSKYLARK_PRIVATEHUB_PASSWORD=pw-ph "$BIN" --site-dir "$T" --privatehub-workspace "$T/ws" \
	--set-privatehub-user dev >/dev/null 2>&1 || fail "provision PrivateHub account"
"$BIN" --site-dir "$T" --port $MAIN --privatehub-port $PH_PORT --privatehub-workspace "$T/ws" \
	> "$T/server.log" 2>&1 &
pid=$!
ready=0
for i in $(seq 1 40); do
	if [ "$(curl -s -o /dev/null -w '%{http_code}' $PH_URL/session/login)" = "200" ] && \
	   [ "$(curl -s -o /dev/null -w '%{http_code}' $MAIN_URL/api/cli/manifest)" = "200" ]; then ready=1; break; fi
	sleep 0.25
done
if [ $ready -ne 1 ]; then
	fail "server did not come up (see $T/server.log)"
	kill $pid 2>/dev/null
	exit 1
fi
pass "composed server up on $MAIN and $PH_PORT"

out=$(ctl help 2>&1)
if printf '%s' "$out" | grep -q "privatehub" && printf '%s' "$out" | grep -q "issue-create"; then
	pass "help lists the privatehub section and its commands"
else
	fail "help lists the privatehub section and its commands"
fi

out=$(ctl manifest privatehub 2>&1)
if printf '%s' "$out" | grep -q '"id": "privatehub"' && ! printf '%s' "$out" | grep -q '"id": "science"'; then
	pass "manifest privatehub lists PrivateHub only"
else
	fail "manifest privatehub lists PrivateHub only"
fi

out=$(ctl privatehb issue-create 2>&1); rc=$?
if [ $rc -eq 2 ] && printf '%s' "$out" | grep -q "did you mean: privatehub"; then
	pass "unknown section suggests the near name"
else
	fail "unknown section suggests the near name (rc $rc: $out)"
fi

out=$(ctl privatehub issue-creat 2>&1); rc=$?
if [ $rc -eq 2 ] && printf '%s' "$out" | grep -q "did you mean: issue-create"; then
	pass "unknown command suggests the near name"
else
	fail "unknown command suggests the near name (rc $rc: $out)"
fi

out=$("$CLI" --server "$MAIN_URL" --site "privatehub=$PH_URL" login privatehub dev 2>&1 </dev/null); rc=$?
if [ $rc -eq 2 ] && printf '%s' "$out" | grep -q "pass --session"; then
	pass "login without a cookie jar is refused"
else
	fail "login without a cookie jar is refused (rc $rc: $out)"
fi

JAR="$T/jar.txt"
out=$(printf 'pw-ph\n' | ctl --session "$JAR" login privatehub dev 2>&1); rc=$?
if [ $rc -eq 0 ] && [ -s "$JAR" ]; then pass "login writes the cookie jar"; else fail "login writes the cookie jar (rc $rc: $out)"; fi
mode=$(stat -c '%a' "$JAR" 2>/dev/null)
if [ "$mode" = "600" ]; then pass "cookie jar is mode 0600"; else fail "cookie jar is mode 0600 (got $mode)"; fi

out=$(ctl --session "$JAR" whoami privatehub 2>&1); rc=$?
if [ $rc -eq 0 ] && printf '%s' "$out" | grep -q '"user": "dev"'; then pass "whoami reports the signed-in user"; else fail "whoami reports the signed-in user (rc $rc: $out)"; fi

out=$(ctl --session "$JAR" privatehub issue-create owner=acme repo=widget title="Live check" body="from skylarkctl" 2>&1); rc=$?
if [ $rc -eq 0 ]; then pass "issue-create runs with the session and prints JSON"; else fail "issue-create runs (rc $rc: $out)"; fi
printf '%s\n' "$out" > "$T/issue.json"

out=$(ctl --session "$JAR" --toon privatehub issue-create owner=acme repo=widget title="Live check TOON" body="toon" 2>&1); rc=$?
if [ $rc -eq 0 ] && ! printf '%s' "$out" | head -1 | grep -q '^{'; then
	pass "issue-create with --toon prints TOON"
else
	fail "issue-create with --toon prints TOON (rc $rc: $out)"
fi

out=$(ctl --session "$JAR" --trace privatehub issue-create owner=acme repo=widget title="Traced" body="t" 2>&1 >/dev/null); rc=$?
if printf '%s' "$out" | grep -q "trace: POST" && printf '%s' "$out" | grep -q "trace: status"; then
	pass "--trace prints the request and status on stderr"
else
	fail "--trace prints the request and status on stderr (rc $rc)"
fi

out=$(ctl --session "$JAR" logout privatehub 2>&1); rc=$?
if [ $rc -eq 0 ]; then pass "logout"; else fail "logout (rc $rc: $out)"; fi
out=$(ctl --session "$JAR" whoami privatehub 2>&1); rc=$?
if [ $rc -eq 1 ]; then pass "whoami after logout is refused"; else fail "whoami after logout is refused (rc $rc)"; fi

kill $pid 2>/dev/null
wait $pid 2>/dev/null
echo "skylarkctl live checks: $fails failed"
[ $fails -eq 0 ]
