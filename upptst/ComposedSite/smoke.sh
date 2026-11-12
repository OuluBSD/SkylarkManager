#!/bin/sh
# Real-process fence for the composed engine sites (briefs 11A step 4, 11B steps 0 and 4).
# Starts the ManagerSkylark binary with PrivateHub, Science, TaskFoundry and MetaAccount
# composed on temporary ports, with the Company module composed under a temporary root.
# Checks that each site is fenced from the stock app and from the other sites, that a
# Science author can write an article, and that a TaskFoundry round trip runs candidate ->
# proposal -> Science and PrivateHub evidence -> validate -> submit -> Company proposal id,
# with a repeat submit returning the same submission. TaskFoundry access is consented on
# the MetaAccount site (link, request, approve, revoke) and the operator CLI is kept as a
# fallback check. The Channels site reads a canned AgentIPC shim, so its fence is
# deterministic.
#
# Usage: sh upptst/ComposedSite/smoke.sh <ManagerSkylark binary>
#   e.g. sh upptst/ComposedSite/smoke.sh \
#        bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Mt-Posix-Shared/ManagerSkylark
# The binary path is required ($1); the Mt config directory holds the build.
# Exit 0 when every check passes. Writes only under a mktemp directory.
BIN=$1
T=$(mktemp -d "${TMPDIR:-/tmp}/composed-site.XXXXXX") || exit 1
MAIN=18220; PH_PORT=18221; SC_PORT=18222; TF_PORT=18223; META_PORT=18224; CH_PORT=18225; FIN_PORT=18226; ATL_PORT=18227
MAIN_URL=http://127.0.0.1:$MAIN
PH_URL=http://127.0.0.1:$PH_PORT
SC_URL=http://127.0.0.1:$SC_PORT
TF_URL=http://127.0.0.1:$TF_PORT
META_URL=http://127.0.0.1:$META_PORT
CH_URL=http://127.0.0.1:$CH_PORT
FIN_URL=http://127.0.0.1:$FIN_PORT
ATL_URL=http://127.0.0.1:$ATL_PORT
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
# The workspace repo is a real git checkout, so PrivateHub evidence can resolve its branch.
if git -C "$T/ws/acme/widget" init -q && \
   git -C "$T/ws/acme/widget" -c user.name=smoke -c user.email=smoke@example.invalid commit -q --allow-empty -m smoke; then
	pass "workspace repo is a git checkout"
else
	fail "workspace repo is a git checkout"
fi
# One public paper, so the science author route has a paper to check auth on.
printf '%s' '{"id":"2612.00002","kind":"paper","title":"Smoke paper","status":"draft","abstract":"Fixture.","envelope":{"schema_id":"skylark/paper","schema_version":"1","workspace_id":"science","stable_id":"2612.00002","record_kind":"paper","revision":"1","visibility":"public","provenance_ref":""}}' > "$T/corpus/2612.00002.json"

# The Channels fixtures: a canned agentipc shim and the node record file whose
# metadata the overview reads (bodies are never read).
mkdir -p "$T/mesh" || exit 1
cat > "$T/channels-shim.sh" <<'SHIMEOF'
#!/bin/sh
last=""
for a in "$@"; do last="$a"; done
case "$last" in
  names) echo s1 ;;
  *) printf '#SMOKE [2026-11-11T04:00:00Z] s1: smoke line <b>raw</b>\n' ;;
esac
SHIMEOF
chmod +x "$T/channels-shim.sh"
printf '%s' '{"chan":"SMOKE","origin":"x","oseq":1,"server":"skylark","ts":"2026-11-11T04:00:00Z","user":"s1"}' > "$T/mesh/records.jsonl"

# Provision the PrivateHub and Science author accounts, then start the server.
MANAGERSKYLARK_PRIVATEHUB_PASSWORD=pw-ph "$BIN" --site-dir "$T" --privatehub-workspace "$T/ws" \
	--set-privatehub-user dev >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision PrivateHub account"; else fail "provision PrivateHub account"; fi
MANAGERSKYLARK_PRIVATEHUB_PASSWORD=pw-view "$BIN" --site-dir "$T" --privatehub-workspace "$T/ws" \
	--set-privatehub-user viewer --privatehub-role viewer >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision PrivateHub viewer account (no member role)"; else fail "provision PrivateHub viewer account (no member role)"; fi
MANAGERSKYLARK_SCIENCE_PASSWORD=pw-sci "$BIN" --site-dir "$T" --set-science-user author >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision Science author account"; else fail "provision Science author account"; fi
MANAGERSKYLARK_TASKFOUNDRY_PASSWORD=pw-tf "$BIN" --site-dir "$T" --company-repos acme/widget \
	--set-taskfoundry-user tfdev >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision TaskFoundry account"; else fail "provision TaskFoundry account"; fi
MANAGERSKYLARK_META_PASSWORD=pw-meta "$BIN" --site-dir "$T" --set-meta-user tfmeta >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision MetaAccount account"; else fail "provision MetaAccount account"; fi
MANAGERSKYLARK_CHANNELS_PASSWORD=pw-chan "$BIN" --site-dir "$T" --set-channels-user cviewer >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision Channels account"; else fail "provision Channels account"; fi
MANAGERSKYLARK_FINANCIALS_PASSWORD=pw-fin "$BIN" --site-dir "$T" --financials-role finance \
	--set-financials-user ffin >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision Financials finance account"; else fail "provision Financials finance account"; fi
MANAGERSKYLARK_ATLAS_PASSWORD=pw-atl "$BIN" --site-dir "$T" --atlas-role admin --set-atlas-user atlasadmin >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "provision Atlas admin account"; else fail "provision Atlas admin account"; fi

MANAGERSKYLARK_AGENTIPC_PASSWORD=123 "$BIN" --site-dir "$T" --port $MAIN --privatehub-port $PH_PORT \
	--science-port $SC_PORT --taskfoundry-port $TF_PORT --meta-port $META_PORT --channels-port $CH_PORT \
	--financials-port $FIN_PORT --atlas-port $ATL_PORT \
	--agentipc-python /bin/sh --agentipc-client "$T/channels-shim.sh" --agentipc-home "$T" \
	--company-root "$T/company" --company-repos acme/widget --science-corpus "$T/corpus" \
	--privatehub-workspace "$T/ws" > "$T/server.log" 2>&1 &
pid=$!
ready=0
for i in $(seq 1 40); do
	if [ "$(curl -s -o /dev/null -w '%{http_code}' $PH_URL/session/login)" = "200" ] && \
	   [ "$(curl -s -o /dev/null -w '%{http_code}' $SC_URL/)" != "000" ] && \
	   [ "$(curl -s -o /dev/null -w '%{http_code}' $TF_URL/session/login)" = "200" ] && \
   [ "$(curl -s -o /dev/null -w '%{http_code}' $META_URL/)" != "000" ] && \
   [ "$(curl -s -o /dev/null -w '%{http_code}' $CH_URL/)" != "000" ] && \
   [ "$(curl -s -o /dev/null -w '%{http_code}' $FIN_URL/)" != "000" ] && \
   [ "$(curl -s -o /dev/null -w '%{http_code}' $ATL_URL/)" != "000" ]; then ready=1; break; fi

	sleep 0.25
done
if [ $ready -ne 1 ]; then
	fail "server did not come up (see $T/server.log)"
	kill $pid 2>/dev/null
	exit 1
fi
pass "composed server up on $MAIN, $PH_PORT, $SC_PORT, $TF_PORT, $META_PORT, $CH_PORT, $FIN_PORT"

# Stock app: global login enforcement stays on the stock port.
check "stock / redirects to login" 302 "$MAIN_URL/"
check "stock /issues redirects to login" 302 "$MAIN_URL/issues"

# Command manifest (task 003): public, read-only, lists the sites that registered commands.
check "manifest is public" 200 "$MAIN_URL/api/cli/manifest"
man=$(cat "$T/body")
for site in privatehub science taskfoundry meta channels financials atlas company github; do
	if printf '%s' "$man" | grep -q "\"id\": *\"$site\""; then pass "manifest lists $site"; else fail "manifest lists $site"; fi
done
for path in 'api/papers' 'api/papers/\*/article' 'session/login' 'privatehub/\*/\*/issues/create' \
	'privatehub/\*/\*/files/register' 'candidates/create' 'proposals/\*/submit' 'consent/decide' \
	'api/gh/issues/\*/create'; do
	if printf '%s' "$man" | grep -q "\"api_path\": *\"$path\""; then pass "manifest covers $path"; else fail "manifest covers $path"; fi
done
check "manifest for privatehub" 200 "$MAIN_URL/api/cli/manifest/privatehub"
ph=$(cat "$T/body")
if printf '%s' "$ph" | grep -q '"id": *"privatehub"' && ! printf '%s' "$ph" | grep -q '"id": *"science"'; then
	pass "privatehub manifest lists PrivateHub only"
else
	fail "privatehub manifest lists PrivateHub only"
fi
check "manifest for an unknown site" 404 "$MAIN_URL/api/cli/manifest/nosuch"
got=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$MAIN_URL/api/cli/manifest")
if [ "$got" != "200" ]; then pass "manifest refuses writes ($got)"; else fail "manifest refuses writes ($got)"; fi

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

# The gateway routes on the raw request target: the query string is ignored and
# percent-encoded segments decode to the route's segments.
check "composed route ignores a query string" 200 "$SC_URL/papers/2612.00002?view=1"
check "composed route decodes a percent-encoded segment" 200 "$SC_URL/papers/2612%2E00002"

# CSRF: a mutation without the token is refused even for a session user.
check "privatehub issue create without csrf is refused" 403 \
	"$PH_URL/privatehub/acme/widget/issues/create" -b "$T/ph.jar" \
	-H "Origin: $PH_URL" --data "title=no-token"

# PrivateHub wiki authoring in the real process: a member creates a page, a duplicate
# create is refused, an edit from a stale revision is refused, and the current revision saves.
PH_WIKI="$PH_URL/privatehub/acme/widget/wiki"
check "privatehub wiki new-page form renders for a member" 200 "$PH_WIKI/new" -b "$T/ph.jar"
ph_csrf=$(sed -n 's/.*__wf_csrf__" value="\([^"]*\)".*/\1/p' "$T/body")
check "privatehub wiki create redirects" 302 "$PH_WIKI/create" -b "$T/ph.jar" \
	-H "Origin: $PH_URL" --data "slug=smoke-page&title=Smoke&body=first&__wf_csrf__=$ph_csrf"
check "privatehub wiki create refuses an existing slug" 409 "$PH_WIKI/create" -b "$T/ph.jar" \
	-H "Origin: $PH_URL" --data "slug=smoke-page&title=Again&body=x&__wf_csrf__=$ph_csrf"
check "privatehub wiki edit form renders" 200 "$PH_WIKI/smoke-page/edit" -b "$T/ph.jar"
ph_rev=$(sed -n 's/.*name="expected_revision" value="\([^"]*\)".*/\1/p' "$T/body")
check "privatehub wiki save from a stale revision is refused" 409 "$PH_WIKI/smoke-page/save" -b "$T/ph.jar" \
	-H "Origin: $PH_URL" --data "expected_revision=stale&title=Smoke&body=lost&__wf_csrf__=$ph_csrf"
check "privatehub wiki save from the current revision redirects" 302 "$PH_WIKI/smoke-page/save" -b "$T/ph.jar" \
	-H "Origin: $PH_URL" --data "expected_revision=$ph_rev&title=Smoke&body=Smoke+edited&__wf_csrf__=$ph_csrf"
check "privatehub wiki page shows the saved edit" 200 "$PH_WIKI/smoke-page" -b "$T/ph.jar"
if grep -q "Smoke edited" "$T/body"; then pass "privatehub wiki page shows the edited body"; else fail "privatehub wiki page shows the edited body"; fi

# A logged-in PrivateHub account without the member role is refused every authoring endpoint,
# and its refused create leaves nothing behind.
curl -s -c "$T/viewer.jar" -o /dev/null -H "Origin: $PH_URL" -H "Accept: text/html" \
	--data "username=viewer&password=pw-view" "$PH_URL/session/login"
check "privatehub viewer reads the issues list (with a csrf form)" 200 "$PH_URL/privatehub/acme/widget/issues" -b "$T/viewer.jar"
view_csrf=$(sed -n 's/.*__wf_csrf__" value="\([^"]*\)".*/\1/p' "$T/body")
check "privatehub viewer cannot open the wiki new-page form" 403 "$PH_WIKI/new" -b "$T/viewer.jar"
check "privatehub viewer wiki create is refused" 403 "$PH_WIKI/create" -b "$T/viewer.jar" \
	-H "Origin: $PH_URL" --data "slug=viewer-page&title=Nope&body=x&__wf_csrf__=$view_csrf"
check "privatehub viewer topic create is refused" 403 "$PH_URL/privatehub/acme/widget/discussions/create" -b "$T/viewer.jar" \
	-H "Origin: $PH_URL" --data "slug=viewer-topic&title=Nope&__wf_csrf__=$view_csrf"
check "privatehub viewer release register is refused" 403 "$PH_URL/privatehub/acme/widget/files/register" -b "$T/viewer.jar" \
	-H "Origin: $PH_URL" --data "name=nope.zip&release=1.0.0&size_bytes=1&url=file:///nope.zip&__wf_csrf__=$view_csrf"
check "refused viewer wiki create left no page behind" 404 "$PH_WIKI/viewer-page" -b "$T/viewer.jar"

# Discussion topics and release files: a member starts a topic and registers a release file.
check "privatehub topic create redirects" 302 "$PH_URL/privatehub/acme/widget/discussions/create" -b "$T/ph.jar" \
	-H "Origin: $PH_URL" --data "slug=smoke-topic&title=Smoke+topic&forum=general&__wf_csrf__=$ph_csrf"
check "privatehub release file register redirects" 302 "$PH_URL/privatehub/acme/widget/files/register" -b "$T/ph.jar" \
	-H "Origin: $PH_URL" --data "name=smoke.zip&release=1.0.0&size_bytes=2048&url=file:///smoke.zip&__wf_csrf__=$ph_csrf"
check "privatehub release file size must be a whole number" 400 "$PH_URL/privatehub/acme/widget/files/register" -b "$T/ph.jar" \
	-H "Origin: $PH_URL" --data "name=bad.zip&release=1.0.0&size_bytes=twelve&url=file:///bad.zip&__wf_csrf__=$ph_csrf"

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

# TaskFoundry round trip in the real process. The article written above gives
# paper 2612.00002 the node p1. Every write carries the site's CSRF token.
curl -s -c "$T/tf.jar" -o "$T/tf_login" -H "Origin: $TF_URL" -H "Accept: application/json" \
	--data "username=tfdev&password=pw-tf" "$TF_URL/session/login"
tf_csrf=$(sed -n 's/.*"csrf": *"\([^"]*\)".*/\1/p' "$T/tf_login")
if [ -n "$tf_csrf" ]; then pass "taskfoundry login returns a csrf token"; else fail "taskfoundry login returns a csrf token"; fi
check "taskfoundry board anonymous redirects" 302 "$TF_URL/"
# post_form <path> <form data>: prints "<status> <redirect url>".
post_form() {
	curl -s -o "$T/tf_body" -w '%{http_code} %{redirect_url}' -b "$T/tf.jar" \
		-H "Origin: $TF_URL" -H "x-wf-csrf: $tf_csrf" --data "$2" "$TF_URL/$1"
}

# Access needs an approved grant bound to this session. Without one, pages go to
# the grants page and writes are refused with 403.
loc=$(curl -s -o /dev/null -w '%{redirect_url}' -b "$T/tf.jar" "$TF_URL/")
case "$loc" in
*/grants) pass "taskfoundry board without a grant redirects to the grants page" ;;
*) fail "taskfoundry board without a grant redirects to the grants page (got '$loc')" ;;
esac
r=$(post_form "candidates/create" "title=Early&summary=x&paper_id=2612.00002")
if [ "${r%% *}" = "403" ]; then pass "taskfoundry write without a grant is refused (403)"; else fail "taskfoundry write without a grant is refused (got $r)"; fi

# Request a grant by linking the session's meta identity, approve it on the
# MetaAccount site, then use the site.
r=$(post_form "grants/request" "meta_user=tfmeta&meta_password=pw-meta")
if [ "${r%% *}" = "302" ]; then pass "taskfoundry grant requested with meta credentials"; else fail "taskfoundry grant requested with meta credentials ($r)"; fi
curl -s -o "$T/tf_grants" -b "$T/tf.jar" "$TF_URL/grants"
gid=$(grep -o 'wag_[A-Za-z0-9_-]*</td><td>requested' "$T/tf_grants" | head -1 | sed 's#</td><td>requested##')
if [ -n "$gid" ] && grep -q "requested" "$T/tf_grants"; then pass "requested grant is listed on the grants page"; else fail "requested grant is listed on the grants page"; fi
curl -s -c "$T/meta.jar" -o "$T/meta_login" -H "Origin: $META_URL" -H "Accept: application/json" \
	--data "username=tfmeta&password=pw-meta" "$META_URL/session/login"
meta_csrf=$(sed -n 's/.*"csrf": *"\([^"]*\)".*/\1/p' "$T/meta_login")
if [ -n "$meta_csrf" ]; then pass "meta account login returns a csrf token"; else fail "meta account login returns a csrf token"; fi
curl -s -o "$T/meta_consent" -b "$T/meta.jar" "$META_URL/consent"
if grep -q "requested" "$T/meta_consent" && grep -q "$gid" "$T/meta_consent"; then
	pass "pending request shows on the meta consent page"
else
	fail "pending request shows on the meta consent page"
fi
got=$(curl -s -o "$T/meta_decide" -w '%{http_code}' -b "$T/meta.jar" -H "Origin: $META_URL" \
	-H "X-WF-CSRF: $meta_csrf" --data "grant_id=$gid&approve=1" "$META_URL/consent/decide")
if [ "$got" = "200" ]; then pass "the meta user approves the grant on the meta site"; else fail "the meta user approves the grant on the meta site (got $got)"; fi
check "taskfoundry board with an approved grant" 200 "$TF_URL/" -b "$T/tf.jar"

r=$(post_form "candidates/create" "title=Round+trip&summary=Smoke&paper_id=2612.00002")
code=${r%% *}; loc=${r#* }; cand=${loc##*/}
if [ "$code" = "302" ] && [ "${cand#cand_}" != "$cand" ]; then pass "taskfoundry candidate created from a Science paper"; else fail "taskfoundry candidate created from a Science paper ($r)"; fi

r=$(post_form "candidates/$cand/propose" "")
code=${r%% *}; loc=${r#* }; prop=${loc##*/}
if [ "$code" = "302" ] && [ "${prop#prop_}" != "$prop" ]; then pass "taskfoundry proposal starts from the candidate"; else fail "taskfoundry proposal starts from the candidate ($r)"; fi

r=$(post_form "proposals/$prop/evidence" "kind=science&ref=2612.00002&node_id=p1")
if [ "${r%% *}" = "302" ]; then pass "taskfoundry attaches Science evidence through the broker"; else fail "taskfoundry attaches Science evidence through the broker ($r)"; fi
r=$(post_form "proposals/$prop/evidence" "kind=privatehub&ref=acme/widget")
if [ "${r%% *}" = "302" ]; then pass "taskfoundry attaches PrivateHub evidence through the broker"; else fail "taskfoundry attaches PrivateHub evidence through the broker ($r)"; fi

r=$(post_form "proposals/$prop/edit" "title=Round+trip&body=Smoke+body&repository=acme/widget&estimated_hours=2")
if [ "${r%% *}" = "302" ]; then pass "taskfoundry proposal edited"; else fail "taskfoundry proposal edited ($r)"; fi

r=$(post_form "proposals/$prop/validate" "")
curl -s -o "$T/tf_prop" -b "$T/tf.jar" "$TF_URL/proposals/$prop"
if [ "${r%% *}" = "302" ] && grep -q "Valid. Content hash" "$T/tf_prop"; then pass "taskfoundry proposal validates"; else fail "taskfoundry proposal validates ($r)"; fi

r=$(post_form "proposals/$prop/submit" "")
code=${r%% *}; loc=${r#* }; sub=${loc##*/}
if [ "$code" = "302" ] && [ "${sub#sub_}" != "$sub" ]; then pass "taskfoundry submits to Company"; else fail "taskfoundry submits to Company ($r)"; fi
curl -s -o "$T/tf_sub" -b "$T/tf.jar" "$TF_URL/submissions/$sub"
company_id=$(grep -o 'prp_[0-9a-f]\{24\}' "$T/tf_sub" | head -1)
if [ -n "$company_id" ]; then pass "submission names the Company proposal id ($company_id)"; else fail "submission names the Company proposal id"; fi
if grep -q "Company status: <strong>proposed" "$T/tf_sub"; then pass "Company reports the draft as proposed"; else fail "Company reports the draft as proposed"; fi

r=$(post_form "proposals/$prop/submit" "")
code=${r%% *}; loc=${r#* }
if [ "$code" = "302" ] && [ "${loc##*/}" = "$sub" ]; then pass "duplicate submit returns the same submission"; else fail "duplicate submit returns the same submission ($r)"; fi
curl -s -o "$T/tf_sub2" -b "$T/tf.jar" "$TF_URL/submissions/$sub"
if grep -q "$company_id" "$T/tf_sub2"; then pass "duplicate submit keeps the same Company proposal id"; else fail "duplicate submit keeps the same Company proposal id"; fi

anon=$(curl -s -o /dev/null -w '%{http_code}' -H "Origin: $TF_URL" -H "x-wf-csrf: $tf_csrf" \
	--data "" "$TF_URL/proposals/$prop/submit")
if [ "$anon" = "302" ]; then pass "anonymous submit is refused"; else fail "anonymous submit is refused (got $anon)"; fi

# The grant is revocable from the meta site; the same session is refused again.
got=$(curl -s -o "$T/meta_revoke" -w '%{http_code}' -b "$T/meta.jar" -H "Origin: $META_URL" \
	-H "X-WF-CSRF: $meta_csrf" --data "grant_id=$gid&approve=0" "$META_URL/consent/decide")
if [ "$got" = "200" ]; then pass "the meta user revokes the grant on the meta site"; else fail "the meta user revokes the grant on the meta site (got $got)"; fi
r=$(post_form "candidates/create" "title=After+revoke&summary=x&paper_id=2612.00002")
if [ "${r%% *}" = "403" ]; then pass "taskfoundry write after revoke is refused (403)"; else fail "taskfoundry write after revoke is refused (got $r)"; fi
loc=$(curl -s -o /dev/null -w '%{redirect_url}' -b "$T/tf.jar" "$TF_URL/")
case "$loc" in
*/grants) pass "taskfoundry board after revoke redirects to the grants page" ;;
*) fail "taskfoundry board after revoke redirects to the grants page (got '$loc')" ;;
esac

# The inbox (task 014): the subject's consent and TaskFoundry events land on
# the meta site.
check "meta inbox lists the subject's events" 200 "$META_URL/inbox" -b "$T/meta.jar"
if grep -q "tf.grant_requested" "$T/body" && grep -q "consent.granted" "$T/body" \
   && grep -q "tf.candidate_created" "$T/body" && grep -q "tf.proposal_submitted" "$T/body" \
   && grep -q "consent.revoked" "$T/body"; then
	pass "meta inbox carries the consent and taskfoundry lifecycle"
else
	fail "meta inbox carries the consent and taskfoundry lifecycle"
fi

# The Channels site (task 012): session-gated reads over the canned shim; no
# writes exist.
check "channels overview anonymous redirects" 302 "$CH_URL/overview"
curl -s -c "$T/chan.jar" -o "$T/chan_login" -H "Origin: $CH_URL" -H "Accept: application/json" \
	--data "username=cviewer&password=pw-chan" "$CH_URL/session/login"
check "channels overview lists the node channels" 200 "$CH_URL/overview" -b "$T/chan.jar"
if grep -q "SMOKE" "$T/body"; then pass "channels overview lists the SMOKE channel"; else fail "channels overview lists the SMOKE channel"; fi
check "channels page renders the shimmed messages" 200 "$CH_URL/channels/skylark/SMOKE" -b "$T/chan.jar"
if grep -q "&lt;b&gt;raw&lt;/b&gt;" "$T/body" && ! grep -q "<b>raw</b>" "$T/body"; then
	pass "channels bodies render escaped"
else
	fail "channels bodies render escaped"
fi

# The Financials site (task 016): the real, freshly seeded Company store has
# an empty ledger, so the finance viewer renders the empty states end to end.
check "financials summary anonymous redirects" 302 "$FIN_URL/financials"
curl -s -c "$T/fin.jar" -o "$T/fin_login" -H "Origin: $FIN_URL" -H "Accept: application/json" \
	--data "username=ffin&password=pw-fin" "$FIN_URL/session/login"
check "financials summary for the finance role" 200 "$FIN_URL/financials" -b "$T/fin.jar"
check "financials ledger empty state" 200 "$FIN_URL/ledger" -b "$T/fin.jar"
if grep -q "No ledger entries yet" "$T/body"; then pass "financials ledger shows the empty state"; else fail "financials ledger shows the empty state"; fi
check "financials contracts empty state" 200 "$FIN_URL/contracts" -b "$T/fin.jar"
if grep -q "No contracts yet" "$T/body"; then pass "financials contracts shows the empty state"; else fail "financials contracts shows the empty state"; fi

# The Atlas site (task 029): anonymous requests redirect to login; the admin
# account creates a map version through the fenced form, and the empty map
# renders its canvas and the edit panel.
check "atlas list anonymous redirects" 302 "$ATL_URL/atlas"
curl -s -c "$T/atl.jar" -o "$T/atl_login" -H "Origin: $ATL_URL" -H "Accept: application/json" \
	--data "username=atlasadmin&password=pw-atl" "$ATL_URL/session/login"
atl_csrf=$(curl -s -b "$T/atl.jar" "$ATL_URL/session/whoami" | sed -n 's/.*"csrf": *"\([^"]*\)".*/\1/p')
check "atlas create refuses a missing csrf token" 403 "$ATL_URL/atlas/create" -b "$T/atl.jar" \
	-H "Origin: $ATL_URL" --data "map=pkr&version=v0.0.1&title=PKR" -X POST
check "atlas create a map version" 302 "$ATL_URL/atlas/create" -b "$T/atl.jar" -H "Origin: $ATL_URL" \
	--data "map=pkr&version=v0.0.1&title=PKR+v0.0.1&__wf_csrf__=$atl_csrf" -X POST
check "atlas map view renders the canvas" 200 "$ATL_URL/atlas/pkr-v0.0.1" -b "$T/atl.jar"
if grep -q 'id="atlas-canvas"' "$T/body" && grep -q "Edit (admin)" "$T/body"; then pass "atlas map view has the canvas and the admin panel"; else fail "atlas map view has the canvas and the admin panel"; fi

# The command console (task 019): the meta site's console renders its
# commands for the signed-in identity.
check "meta console for the signed-in identity" 200 "$META_URL/console" -b "$T/meta.jar"
if grep -q "consent/decide" "$T/body" && grep -q "__wf_csrf__" "$T/body"; then
	pass "the meta console renders a runnable consent form"
else
	fail "the meta console renders a runnable consent form"
fi

# The operator CLI stays as a documented fallback: a fresh request can be
# approved and revoked out of band.
r=$(post_form "grants/request" "meta_user=tfmeta&meta_password=pw-meta")
if [ "${r%% *}" = "302" ]; then pass "taskfoundry grant re-requested after the revoke"; else fail "taskfoundry grant re-requested after the revoke ($r)"; fi
curl -s -o "$T/tf_grants2" -b "$T/tf.jar" "$TF_URL/grants"
gid2=$(grep -o 'wag_[A-Za-z0-9_-]*</td><td>requested' "$T/tf_grants2" | head -1 | sed 's#</td><td>requested##')
if [ -n "$gid2" ] && [ "$gid2" != "$gid" ]; then pass "the re-request creates a fresh pending grant"; else fail "the re-request creates a fresh pending grant (gid2=$gid2)"; fi
"$BIN" --site-dir "$T" --approve-taskfoundry-grant "$gid2" >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "operator approves the grant with the CLI (fallback)"; else fail "operator approves the grant with the CLI (fallback)"; fi
check "taskfoundry board with an operator-approved grant" 200 "$TF_URL/" -b "$T/tf.jar"
"$BIN" --site-dir "$T" --revoke-taskfoundry-grant "$gid2" >/dev/null 2>&1
if [ $? -eq 0 ]; then pass "operator revokes the grant with the CLI (fallback)"; else fail "operator revokes the grant with the CLI (fallback)"; fi

kill $pid 2>/dev/null
wait $pid 2>/dev/null
echo "composed-site checks: $fails failed"
[ $fails -eq 0 ]
