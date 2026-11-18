#!/bin/sh
# Trusted per-track adapter for worker-bridge .task.txt gate requests.
# Repo, branch, store, tester and API credentials come from the service environment,
# never from the task file. Install this under a distinct name in worker-bridge.
set -eu
mode=${1:-run}
case "$mode" in run|--once|--test) ;; *) echo "usage: multiatlas-gate-adapter.sh [--once|--test]" >&2; exit 2;; esac
queue=${MA_GATE_QUEUE_DIR:-${HOME}/OuluBSD/tools/worker-bridge/box}
manager_root=${MA_SKYLARKMANAGER_ROOT:-/home/sblo/OuluBSD/SkylarkManager}
record=${MA_RECORD_GATE_SCRIPT:-$manager_root/uppsrc/MultiAtlas/tools/record-gate-pass.sh}
poll=${MA_GATE_POLL_SECONDS:-2}
case "$poll" in *[!0-9]*|'') echo "invalid poll interval" >&2; exit 2;; esac

validate_task() {
	awk -F= '/^(ensemble|track_id|gate_version|git_commit_hash)=/ { if (seen[$1]++) exit 2; print } /^[[:space:]]*$/ { next } !/^(ensemble|track_id|gate_version|git_commit_hash)=/ { exit 2 } END { if (NR == 0 || seen["ensemble"] != 1 || seen["track_id"] != 1 || seen["gate_version"] != 1 || seen["git_commit_hash"] != 1) exit 2 }' "$1" >/dev/null
}
if [ "$mode" = "--test" ]; then
	test_dir=$(mktemp -d); trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
	cat > "$test_dir/valid.task.txt" <<'TASK'
ensemble=sample
track_id=vision
gate_version=0.1.999
git_commit_hash=0123456789012345678901234567890123456789
TASK
	cat > "$test_dir/invalid.task.txt" <<'TASK'
ensemble=sample
track_id=vision
command=touch /tmp/should-not-run
TASK
	validate_task "$test_dir/valid.task.txt" && ! validate_task "$test_dir/invalid.task.txt" || exit 1
	echo "worker-bridge adapter parser checks: 2/2 passed"
	exit 0
fi

process_task() (
	task=$1
	[ -f "$task" ] || return 0
	: "${MA_ENSEMBLE:?trusted per-adapter ensemble is required}"
	: "${MA_TRACK_ID:?trusted per-adapter track id is required}"
	: "${MA_TRACK_REPO:?trusted track worktree is required}"
	: "${MA_TRACK_BRANCH:?trusted track branch is required}"
	: "${MA_TRACK_FAMILY:?trusted track family is required}"
	: "${MA_TRACK_STORE:?trusted relative track store path is required}"
	: "${MA_GATE_TESTER:?trusted argv-only gate tester executable is required}"
	: "${MA_API_BASE:?MultiAtlas API base is required}"
	: "${MA_COOKIE:?scoped API session cookie is required}"
	: "${MA_CSRF:?matching CSRF token is required}"
	lock=$task.lock
	if ! (set -C; : > "$lock") 2>/dev/null; then return 0; fi
	tmp=$(mktemp -d); chmod 700 "$tmp"
	trap 'rm -rf "$tmp" "$lock"' EXIT HUP INT TERM
	if ! validate_task "$task"; then
		echo "rejected malformed gate task: $(basename "$task")" >&2
		rm -rf "$tmp"; rm -f "$lock"; trap - EXIT HUP INT TERM; return 1
	fi
	awk -F= '/^(ensemble|track_id|gate_version|git_commit_hash)=/ { print }' "$task" > "$tmp/fields"
	get_value() { sed -n "s/^$1=//p" "$tmp/fields"; }
	ens=$(get_value ensemble); track=$(get_value track_id); gate=$(get_value gate_version); requested_head=$(get_value git_commit_hash)
	if [ "$ens" != "$MA_ENSEMBLE" ] || [ "$track" != "$MA_TRACK_ID" ]; then
		echo "task does not match this adapter's trusted track scope" >&2
		rm -rf "$tmp"; rm -f "$lock"; trap - EXIT HUP INT TERM; return 1
	fi
	case "$ens" in ''|*[!A-Za-z0-9_-]*) echo "invalid ensemble" >&2; return 1;; esac
	case "$track" in ''|*[!A-Za-z0-9_-]*) echo "invalid track" >&2; return 1;; esac
	case "$gate" in ''|*[!A-Za-z0-9._-]*) echo "invalid gate version" >&2; return 1;; esac
	case "$requested_head" in *[!A-Fa-f0-9]*) echo "invalid task commit" >&2; return 1;; esac
	[ "${#requested_head}" -eq 40 ] || { echo "task commit must be full length" >&2; return 1; }
	repo=$MA_TRACK_REPO
	head=$(git -C "$repo" rev-parse --verify HEAD) || { echo "configured track worktree unavailable" >&2; return 1; }
	branch=$(git -C "$repo" symbolic-ref --short -q HEAD) || { echo "configured track is detached" >&2; return 1; }
	[ "$branch" = "$MA_TRACK_BRANCH" ] || { echo "configured track branch mismatch" >&2; return 1; }
	if [ "$head" != "$(printf '%s' "$requested_head" | tr 'A-F' 'a-f')" ]; then
		# Recovery after the scoped evidence commit: require both evidence files
		# in this exact HEAD before resending the meta notification.
		event_id="gate-$gate-$requested_head"
		marker_path=".multiatlas/gates/$gate.json"
		event_path="$MA_TRACK_STORE/data/events/$MA_TRACK_FAMILY.ev-$event_id.json"
		if git -C "$repo" cat-file -e "HEAD:$marker_path" 2>/dev/null &&
		   git -C "$repo" cat-file -e "HEAD:$event_path" 2>/dev/null &&
		   [ -f "$repo/$marker_path" ] && [ -f "$repo/$event_path" ]; then
			python3 - "$repo/$marker_path" "$event_id" "$track" "$gate" <<'PY'
import json, sys
j=json.load(open(sys.argv[1],encoding="utf-8"))
if j.get("event_id") != sys.argv[2] or j.get("track_id") != sys.argv[3] or j.get("gate_version") != sys.argv[4] or j.get("result") != "passed":
    raise SystemExit("committed gate marker does not match retry task")
PY
			if MA_TRACK_REPO="$repo" MA_API_BASE="$MA_API_BASE" "$record" "$ens" "$track" "$gate" "$head"; then
				mv -- "$task" "$task.done"
				rm -rf "$tmp"; rm -f "$lock"; trap - EXIT HUP INT TERM
				return 0
			fi
			echo "meta gate retry failed; committed child evidence remains available" >&2
			return 1
		fi
		echo "task commit is not current and no matching committed gate evidence exists" >&2
		return 1
	fi
	[ -x "$MA_GATE_TESTER" ] || { echo "configured gate tester is not executable" >&2; return 1; }
	if ! timeout "${MA_GATE_TEST_TIMEOUT:-180}" "$MA_GATE_TESTER" "$repo" "$gate" >"$tmp/test.log" 2>&1; then
		echo "local gate verification failed; task retained: $(basename "$task")" >&2
		return 1
	fi
	event_id="gate-$gate-$head"
	case "$event_id" in *[!A-Za-z0-9._-]*) echo "derived gate event id is invalid" >&2; return 1;; esac
	umask 077
	printf 'Cookie: %s\nX-WF-CSRF: %s\nOrigin: %s\n' "$MA_COOKIE" "$MA_CSRF" "${MA_API_ORIGIN:-$MA_API_BASE}" > "$tmp/headers"
	curl_call() { curl --silent --show-error --connect-timeout 5 --max-time 15 --retry 3 --retry-connrefused --retry-delay 1 "$@"; }
	curl_call --fail -H "@$tmp/headers" "$MA_API_BASE/api/multiatlas/$ens/status" -o "$tmp/status.json" || { echo "API status unavailable" >&2; return 1; }
	status_facts=$(python3 - "$tmp/status.json" "$track" "$gate" "$head" "$branch" <<'PY'
import json, sys
j=json.load(open(sys.argv[1],encoding="utf-8")); tracks=j.get("data",{}).get("tracks",[])
t=next((x for x in tracks if x.get("track_id")==sys.argv[2]),None)
if not t or t.get("git",{}).get("head") != sys.argv[4] or t.get("git",{}).get("branch") != sys.argv[5]:
    raise SystemExit("configured server worktree does not match adapter worktree")
r=t.get("family_revision")
if not r: raise SystemExit("server did not provide child family revision")
versions=t.get("versions",[])
v=next((x for x in versions if x.get("vid")==sys.argv[3] or str(x.get("code"))==sys.argv[3]),None)
if not v: raise SystemExit("gate does not match a configured child version")
print(r+"\t"+v.get("vid", ""))
PY
) || return 1
family_revision=${status_facts%%"$(printf '\t')"*}
child_vid=${status_facts#*"$(printf '\t')"}
	python3 - "$tmp/child-event.json" "$event_id" "$track" "$gate" "$child_vid" "$head" "$family_revision" <<'PY'
import json, sys
path,event,track,gate,child,head,rev=sys.argv[1:]
with open(path,"w",encoding="utf-8") as f:
    json.dump({"id":event,"expected_revision":rev,"vid":child,"type":"gate-pass","action":"GatePass",
      "effects":["tested:"+gate],"text":"Local gate verified at source commit "+head},f,separators=(",",":"))
PY
	curl_call --fail -X POST "$MA_API_BASE/api/multiatlas/$ens/track/$track/event" -H "@$tmp/headers" \
		-H 'Content-Type: application/json' --data-binary "@$tmp/child-event.json" -o "$tmp/event-response.json" || {
		# An earlier interrupted attempt may already have stored this deterministic event.
		if [ ! -f "$repo/$MA_TRACK_STORE/data/events/$MA_TRACK_FAMILY.ev-$event_id.json" ]; then
			echo "child gate event append failed; task retained" >&2; return 1
		fi
	}
	[ -f "$repo/$MA_TRACK_STORE/data/events/$MA_TRACK_FAMILY.ev-$event_id.json" ] || { echo "child event record missing from configured store" >&2; return 1; }
	python3 - "$repo/$MA_TRACK_STORE/data/events/$MA_TRACK_FAMILY.ev-$event_id.json" "$event_id" "$track" "$gate" "$child_vid" <<'PY'
import json, sys
j=json.load(open(sys.argv[1],encoding="utf-8")); d=j.get("data",j)
if d.get("id") != sys.argv[2] or d.get("track_id") != sys.argv[3] or d.get("vid") != sys.argv[5] or "tested:"+sys.argv[4] not in d.get("effects",[]):
    raise SystemExit("stored child event does not match gate request")
PY
	if [ -L "$repo/.multiatlas" ] || [ -L "$repo/.multiatlas/gates" ]; then echo "gate evidence path cannot be a symlink" >&2; return 1; fi
	mkdir -p "$repo/.multiatlas/gates"
	[ ! -L "$repo/.multiatlas/gates/$gate.json" ] || { echo "gate evidence file cannot be a symlink" >&2; return 1; }
	MA_GATE_SOURCE_COMMIT="$head" python3 - "$tmp/gate-evidence.json" "$track" "$gate" "$event_id" "$child_vid" <<'PY'
import json, os, sys
with open(sys.argv[1],"w",encoding="utf-8") as f:
    json.dump({"track_id":sys.argv[2],"gate_version":sys.argv[3],"event_id":sys.argv[4],"child_vid":sys.argv[5],"source_commit":os.environ["MA_GATE_SOURCE_COMMIT"],"result":"passed"},f,separators=(",",":"))
PY
	mv -f -- "$tmp/gate-evidence.json" "$repo/.multiatlas/gates/$gate.json"
	event_path="$MA_TRACK_STORE/data/events/$MA_TRACK_FAMILY.ev-$event_id.json"
	git -C "$repo" add -f -- "$event_path" ".multiatlas/gates/$gate.json" || { echo "could not stage scoped gate evidence" >&2; return 1; }
	if ! git -C "$repo" diff --cached --quiet -- "$event_path" ".multiatlas/gates/$gate.json"; then
		git -C "$repo" commit --only -m "gate-$gate-pass" -- "$event_path" ".multiatlas/gates/$gate.json" >/dev/null || { echo "scope-only gate commit failed" >&2; return 1; }
	fi
	committed=$(git -C "$repo" rev-parse --verify HEAD)
	if ! MA_TRACK_REPO="$repo" MA_API_BASE="$MA_API_BASE" "$record" "$ens" "$track" "$gate" "$committed"; then
		echo "meta gate synchronization failed; committed child evidence retained for retry" >&2; return 1
	fi
	mv -- "$task" "$task.done"
	rm -rf "$tmp"; rm -f "$lock"; trap - EXIT HUP INT TERM
)

if [ "$mode" = "--once" ]; then
	failed=0
	for task in "$queue"/*.task.txt; do [ -f "$task" ] || continue; process_task "$task" || failed=1; done
	exit "$failed"
fi
while :; do
	for task in "$queue"/*.task.txt; do [ -f "$task" ] || continue; process_task "$task" || :; done
	sleep "$poll"
done
