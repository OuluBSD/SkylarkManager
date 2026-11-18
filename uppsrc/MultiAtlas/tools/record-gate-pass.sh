#!/bin/sh
# Record a verified child gate through the MultiAtlas API.
# Requires MA_API_BASE, MA_META_VID, MA_COOKIE and MA_CSRF from a scoped session.
set -eu
if [ "$#" -ne 4 ]; then
	echo "usage: record-gate-pass.sh <ensemble> <track_id> <gate_version> <git_commit_hash>" >&2
	exit 2
fi
ensemble=$1 track_id=$2 gate_version=$3 commit=$4
base=${MA_API_BASE:-http://127.0.0.1:18219}
meta_vid=${MA_META_VID:?MA_META_VID must name a configured meta-family version}
: "${MA_COOKIE:?MA_COOKIE must contain the scoped MultiAtlas session cookie}"
: "${MA_CSRF:?MA_CSRF must contain the matching CSRF token}"
case "$ensemble" in ''|*[!A-Za-z0-9_-]*) echo "invalid ensemble" >&2; exit 2;; esac
case "$track_id" in ''|*[!A-Za-z0-9_-]*) echo "invalid track id" >&2; exit 2;; esac
case "$gate_version" in ''|*[!A-Za-z0-9._-]*) echo "invalid gate version" >&2; exit 2;; esac
case "$meta_vid" in ''|*[!A-Za-z0-9._-]*) echo "invalid meta version" >&2; exit 2;; esac
case "$commit" in *[!A-Fa-f0-9]*) echo "invalid commit hash" >&2; exit 2;; esac
[ "${#commit}" -eq 40 ] || { echo "commit hash must be full length" >&2; exit 2; }
repo=${MA_TRACK_REPO:-.}
head=$(git -C "$repo" rev-parse --verify HEAD 2>/dev/null) || { echo "track git worktree unavailable" >&2; exit 1; }
branch=$(git -C "$repo" symbolic-ref --short -q HEAD 2>/dev/null) || { echo "track is detached" >&2; exit 1; }
[ "$head" = "$(printf '%s' "$commit" | tr 'A-F' 'a-f')" ] || { echo "requested commit is not current HEAD" >&2; exit 1; }
[ -f "$repo/.multiatlas/gates/$gate_version.json" ] || { echo "committed gate evidence file is missing" >&2; exit 1; }
proof_ids=$(python3 - "$repo/.multiatlas/gates/$gate_version.json" "$track_id" "$gate_version" <<'PY'
import json, re, sys
try:
    p=json.load(open(sys.argv[1], encoding="utf-8"))
except Exception:
    raise SystemExit("invalid gate evidence JSON")
event=p.get("event_id", ""); child=p.get("child_vid", "")
if p.get("track_id") != sys.argv[2] or p.get("gate_version") != sys.argv[3] or p.get("result") != "passed" or not re.fullmatch(r"[A-Za-z0-9._-]{1,120}", event) or not re.fullmatch(r"[A-Za-z0-9._-]{1,120}", child):
    raise SystemExit("gate evidence does not match requested track and version")
print(event + "\t" + child)
PY
)
event_id=${proof_ids%%"$(printf '\t')"*}
child_vid=${proof_ids#*"$(printf '\t')"}
tmp=$(mktemp -d)
chmod 700 "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
umask 077
headers=$tmp/headers
printf 'Cookie: %s\nX-WF-CSRF: %s\nOrigin: %s\n' "$MA_COOKIE" "$MA_CSRF" "${MA_API_ORIGIN:-$base}" > "$headers"
request() {
	url=$1 output=$2
	curl --silent --show-error --fail --connect-timeout 5 --max-time 15 \
		--retry 3 --retry-connrefused --retry-delay 1 -H "@$headers" \
		"$url" -o "$output" || { echo "MultiAtlas API unavailable or rejected the gate request" >&2; return 1; }
}
request "$base/api/multiatlas/$ensemble/check" "$tmp/check.json"
request "$base/api/multiatlas/$ensemble/status" "$tmp/status.json"
python3 - "$tmp/check.json" "$tmp/status.json" "$track_id" "$gate_version" "$head" "$branch" "$event_id" "$child_vid" <<'PY'
import json, sys
check=json.load(open(sys.argv[1], encoding="utf-8")); status=json.load(open(sys.argv[2], encoding="utf-8"))
data=check.get("data",{})
row=next((x for x in data.get("tracks",[]) if x.get("track_id")==sys.argv[3]),None)
if not row or row.get("errors"):
    raise SystemExit("selected child replay has errors")
tracks=status.get("data",{}).get("tracks",[])
t=next((x for x in tracks if x.get("track_id")==sys.argv[3]),None)
if not t or sys.argv[4] not in t.get("passed_gates",[]):
    raise SystemExit("API replay has not exported the requested child tested gate")
git=t.get("git",{})
if git.get("head") != sys.argv[5] or git.get("branch") != sys.argv[6]:
    raise SystemExit("API worktree commit or branch does not match local gate evidence")
if not any(x.get("gate_version")==sys.argv[4] and x.get("event_id")==sys.argv[7] and x.get("child_vid")==sys.argv[8] for x in t.get("gate_evidence",[])):
    raise SystemExit("committed evidence does not name a replayed child gate event")
PY
python3 - "$meta_vid" "$track_id" "$gate_version" "$head" "$event_id" "$child_vid" >"$tmp/body.json" <<'PY'
import json, sys
v,t,g,c,e,child=sys.argv[1:]
print(json.dumps({"vid":v, "subject_kind":"element", "subject_id":"e_track_"+t,
 "type":"stage", "action":"RecordGatePass", "effects":["track:"+t+":tested:"+g],
 "text":"Track %s passed gate %s at commit %s"%(t,g,c), "gate_track_id":t,
 "gate_version":g, "git_commit_hash":c, "gate_event_id":e, "gate_child_vid":child}, separators=(",",":")))
PY
curl --silent --show-error --fail --connect-timeout 5 --max-time 15 \
	--retry 3 --retry-connrefused --retry-delay 1 -X POST "$base/api/multiatlas/$ensemble/meta/event" \
	-H "@$headers" -H 'Content-Type: application/json' --data-binary "@$tmp/body.json" \
	-o "$tmp/result.json" || { echo "gate event rejected or API unavailable after bounded retries" >&2; exit 1; }
python3 - "$tmp/result.json" <<'PY'
import json, sys
r=json.load(open(sys.argv[1], encoding="utf-8"))
if not r.get("ok"):
    raise SystemExit("gate event was not accepted")
e=r.get("data",{}).get("event",{})
if e.get("action") != "RecordGatePass" or e.get("effects") or not e.get("requires"):
    raise SystemExit("API returned an invalid normalized gate event")
print("verified gate pass recorded")
PY
