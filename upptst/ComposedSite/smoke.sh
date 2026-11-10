#!/bin/sh
# Real-process fence for the composed engine sites (brief 11A step 4).
# Starts the ManagerSkylark binary with PrivateHub and Science composed on
# temporary ports, against isolated temporary data, and checks that each
# site is fenced from the stock app and from the other site.
# Usage: smoke.sh <ManagerSkylark binary>. Exit 0 when every check passes.
BIN=$1
T=$(mktemp -d "${TMPDIR:-/tmp}/composed-site.XXXXXX") || exit 1
MAIN=18220; PH_PORT=18221; SC_PORT=18222
MAIN_URL=http://127.0.0.1:$MAIN
PH_URL=http://127.0.0.1:$PH_PORT
SC_URL=http://127.0.0.1:$SC_PORT
fails=0
pass() { echo "ok   $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }
code() { curl -s -o "$2" -w '%{http_code}' "$@" 2>/dev/null; }
# check <name> <expected status> <url> [extra curl args...]
check() {
	name=$1; want=$2; url=$3; shift 3
	got=$(curl -s -o "$T/body" -w '%{http_code}' "$@" "$url")
	if [ "$got" = "$want" ]; then pass "$name"; else fail "$name (got $got, want $want)"; fi
}
mkdir -p "$T/corpus" "$T/ws/acme/widget" || exit 1
# One public paper, so the science author route has a paper to check auth on.
printf '%s' '{"id":"2612.00002","kind":"paper","title":"Smoke paper","status":"draft","abstract":"Fixture.","envelope":{"schema_id":"skylark/paper","schema_version":"1","workspace_id":"science","stable_id":"2612.00002","record_kind":"paper","revision":"1","visibility":"public","provenance_ref":""}}' > "$T/corpus/2612.00002.json"

# Provision the PrivateHub account, then start the composed server.
MANAGERSKYLARK_PRIVATEHUB_PASSWORD=pw-ph "$BIN" --site-dir "$T" --privatehub-workspace "$T/ws" \
	--set-privatehub-user dev >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision PrivateHub account"; else fail "provision PrivateHub account"; fi

"$BIN" --site-dir "$T" --port $MAIN --privatehub-port $PH_PORT --science-port $SC_PORT \
	--science-corpus "$T/corpus" --privatehub-workspace "$T/ws" > "$T/server.log" 2>&1 &
pid=$!
ready=0
for i in $(seq 1 40); do
	if [ "$(curl -s -o /dev/null -w '%{http_code}' $PH_URL/session/login)" = "200" ] && \
	   [ "$(curl -s -o /dev/null -w '%{http_code}' $SC_URL/)" != "000" ]; then ready=1; break; fi
	sleep 0.25
done
if [ $ready -ne 1 ]; then
	fail "server did not come up (see $T/server.log)"
	kill $pid 2>/dev/null
	exit 1
fi
pass "composed server up on $MAIN, $PH_PORT, $SC_PORT"

# Stock app: global login enforcement stays on the stock port.
check "stock / redirects to login" 302 "$MAIN_URL/"
check "stock /issues redirects to login" 302 "$MAIN_URL/issues"

# Anonymous callers on composed ports get no content.
check "privatehub / anonymous redirects" 302 "$PH_URL/"
check "privatehub project issues anonymous redirects" 302 "$PH_URL/privatehub/acme/widget/issues"

# Login is per site: the PrivateHub session is established on its own cookie.
curl -s -c "$T/ph.jar" -o /dev/null -H "Origin: $PH_URL" -H "Accept: text/html" \
	--data "username=dev&password=pw-ph" "$PH_URL/session/login"
check "privatehub / after login" 200 "$PH_URL/" -b "$T/ph.jar"
check "privatehub overview lists the workspace repo" 200 "$PH_URL/privatehub/acme/widget" -b "$T/ph.jar"
if grep -q "acme" "$T/body"; then pass "privatehub project page names acme/widget"; else fail "privatehub project page names acme/widget"; fi
check "privatehub issues list after login" 200 "$PH_URL/privatehub/acme/widget/issues" -b "$T/ph.jar"

# Site-scoped cookies: the PrivateHub session does not authenticate on Science.
check "privatehub cookie is not honored by science author page" 403 \
	"$SC_URL/papers/2612.00002/author" -b "$T/ph.jar"

# Identical-path fencing: each site serves its own root on the same path.
curl -s -o "$T/ph_root" "$PH_URL/" -b "$T/ph.jar"
curl -s -o "$T/sc_root" "$SC_URL/"
if grep -q "PrivateHub" "$T/ph_root" && ! grep -q "PrivateHub" "$T/sc_root"; then
	pass "identical path / serves each site's own content"
else
	fail "identical path / serves each site's own content"
fi

# Route isolation: stock and foreign routes are not reachable on composed ports.
check "stock /issues is not served on privatehub" 404 "$PH_URL/issues" -b "$T/ph.jar"
check "stock /goals is not served on science" 404 "$SC_URL/goals"
check "privatehub routes are not served on science" 404 "$SC_URL/privatehub/acme/widget/issues"

# CSRF: a mutation without the token is refused even for a session user.
check "privatehub issue create without csrf is refused" 403 \
	"$PH_URL/privatehub/acme/widget/issues/create" -b "$T/ph.jar" \
	-H "Origin: $PH_URL" --data "title=no-token"

# Science login with an unknown account is refused.
check "science login with unknown account is refused" 401 \
	"$SC_URL/session/login" -H "Origin: $SC_URL" -H "Accept: application/json" --data "username=nobody&password=x"

kill $pid 2>/dev/null
wait $pid 2>/dev/null
echo "composed-site checks: $fails failed"
[ $fails -eq 0 ]
