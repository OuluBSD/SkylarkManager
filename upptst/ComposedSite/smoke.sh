#!/bin/sh
# Real-process fence for the composed engine sites (briefs 11A step 4, 11B step 0).
# Starts the ManagerSkylark binary with PrivateHub and Science composed on
# temporary ports, against isolated temporary data, and checks that each
# site is fenced from the stock app and from the other site, and that a
# Science author can write an article through the composed site.
#
# Usage: sh upptst/ComposedSite/smoke.sh <ManagerSkylark binary>
#   e.g. sh upptst/ComposedSite/smoke.sh \
#        bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Mt-Posix-Shared/ManagerSkylark
# The binary path is required ($1); the Mt config directory holds the build.
# Exit 0 when every check passes. Writes only under a mktemp directory.
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

# Provision the PrivateHub and Science author accounts, then start the server.
MANAGERSKYLARK_PRIVATEHUB_PASSWORD=pw-ph "$BIN" --site-dir "$T" --privatehub-workspace "$T/ws" \
	--set-privatehub-user dev >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision PrivateHub account"; else fail "provision PrivateHub account"; fi
MANAGERSKYLARK_SCIENCE_PASSWORD=pw-sci "$BIN" --site-dir "$T" --set-science-user author >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision Science author account"; else fail "provision Science author account"; fi

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

# Science authoring in the real process: the author logs in on the Science
# site, then writes an article through the composed route.
curl -s -c "$T/sc.jar" -o "$T/sc_login" -H "Origin: $SC_URL" -H "Accept: application/json" \
	--data "username=author&password=pw-sci" "$SC_URL/session/login"
sc_csrf=$(sed -n 's/.*"csrf": *"\([^"]*\)".*/\1/p' "$T/sc_login")
if [ -n "$sc_csrf" ]; then pass "science author login returns a csrf token"; else fail "science author login returns a csrf token"; fi
good='{"expected_paper_revision":"1","article":{"title":"Authored","nodes":[{"id":"s1","type":"section","heading":"Intro"},{"id":"p1","type":"paragraph","text":"Authored text."}]}}'
stale='{"expected_paper_revision":"99","article":{"title":"Authored","nodes":[{"id":"s1","type":"section","heading":"Intro"}]}}'
write() {
	curl -s -o "$T/body" -w '%{http_code}' -b "$T/sc.jar" -H "Origin: $SC_URL" \
		-H "x-wf-csrf: $sc_csrf" -H "Content-Type: application/json" --data "$1" \
		"$SC_URL/api/papers/2612.00002/article"
}
got=$(write "$good")
if [ "$got" = "200" ]; then pass "science author writes an article (200)"; else fail "science author writes an article (got $got, want 200)"; fi
if [ -f "$T/corpus/2612.00002.article.json" ]; then pass "article sidecar written under the temp corpus"; else fail "article sidecar written under the temp corpus"; fi
got=$(write "$stale")
if [ "$got" = "409" ]; then pass "stale expected revision gets 409"; else fail "stale expected revision gets 409 (got $got, want 409)"; fi
got=$(curl -s -o /dev/null -w '%{http_code}' -H "Origin: $SC_URL" -H "Content-Type: application/json" \
	--data "$good" "$SC_URL/api/papers/2612.00002/article")
if [ "$got" = "403" ]; then pass "article write without a session is refused"; else fail "article write without a session is refused (got $got, want 403)"; fi

kill $pid 2>/dev/null
wait $pid 2>/dev/null
echo "composed-site checks: $fails failed"
[ $fails -eq 0 ]
