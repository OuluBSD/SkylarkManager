#!/bin/sh
# Seeds the Manager local store under <site_dir>/mgr with one project matching
# the demo repo, so the projects tab has cards in all three columns. Titles are
# plain text on purpose: the captured bodies must not depend on escaping.
# Usage: make-store.sh <site_dir>
set -eu
MGR=$1/mgr/projects/demo-repo
mkdir -p "$MGR/phases" "$MGR/tasks"
cat > "$MGR/project.json" <<'JSON'
{"title":"Demo repo","description":"Fixture project for the capture harness","status":"active","slug":"demo-repo"}
JSON
cat > "$MGR/phases/001-setup.json" <<'JSON'
{"seq":1,"project":"demo-repo","title":"Setup","description":"","status":"in-progress","slug":"setup"}
JSON
task() {
	cat > "$MGR/tasks/$1-$3.json" <<JSON
{"seq":$1,"project":"demo-repo","phase_seq":1,"title":"$2","description":"","details":"","instructions":"","result":"","status":"$4","link_next_task":0,"slug":"$3"}
JSON
}
task 1 "Read the brief" read-brief done
task 2 "Write the template" write-template done
task 3 "Convert the tab" convert-tab in-progress
task 4 "Capture the route" capture-route in-progress
task 5 "Review the diff" review-diff open
task 6 "Run the gate" run-gate open
task 7 "Commit the change" commit-change open
task 8 "Write the log" write-log open
task 9 "Stop and wait" stop-wait blocked
