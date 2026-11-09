#!/bin/sh
# Stands in for the gh CLI during capture. Answers the argv shapes the GitHub
# views use for SeppoPakonen/demo-repo with canned JSON (same shapes as the
# adapter self-test's fake gh), so no network or credentials are involved.
# Any other argv fails loudly, which the views report as unavailable.
case "$*" in
	*"repos/SeppoPakonen/demo-repo/issues?state=all"*)
		echo '[{"number":1,"title":"Crash on start","state":"open","user":{"login":"alice"},"created_at":"2026-11-02T10:00:00Z","comments":1},{"number":2,"title":"Docs gap","state":"closed","user":{"login":"bob"},"created_at":"2026-11-03T12:00:00Z","comments":0}]' ;;
	*"repos/SeppoPakonen/demo-repo/issues/1/comments"*)
		echo '[{"id":11,"user":{"login":"carol"},"body":"Repro attached","created_at":"2026-11-02T11:00:00Z"}]' ;;
	*"repos/SeppoPakonen/demo-repo/issues/1"*)
		echo '{"number":1,"title":"Crash on start","body":"Start it and it crashes.","state":"open","user":{"login":"alice"},"created_at":"2026-11-02T10:00:00Z","comments":1}' ;;
	*"repos/SeppoPakonen/demo-repo/issues/2/comments"*)
		echo '[]' ;;
	*"repos/SeppoPakonen/demo-repo/issues/2"*)
		echo '{"number":2,"title":"Docs gap","body":"","state":"closed","user":{"login":"bob"},"created_at":"2026-11-03T12:00:00Z","comments":0}' ;;
	*"repos/SeppoPakonen/demo-repo/pulls/2"*)
		echo '{"number":2,"title":"Merged change","body":"","state":"closed","user":{"login":"alice"},"created_at":"2026-11-02T09:00:00Z","merged_at":"2026-11-03T10:00:00Z","head":{"ref":"feature/done"},"base":{"ref":"master"},"html_url":"https://github.com/SeppoPakonen/demo-repo/pull/2"}' ;;
	*"pr diff 2 -R SeppoPakonen/demo-repo"*)
		: ;;
	*"repos/SeppoPakonen/demo-repo/pulls?state=all"*)
		echo '[{"number":1,"title":"Add parity","state":"open","user":{"login":"alice"},"created_at":"2026-11-02T09:00:00Z","merged_at":null,"head":{"ref":"feature/parity"},"base":{"ref":"master"},"html_url":"https://github.com/SeppoPakonen/demo-repo/pull/1"}]' ;;
	*"repos/SeppoPakonen/demo-repo/pulls/1/comments"*)
		echo '[{"id":21,"user":{"login":"carol"},"body":"Looks fine","created_at":"2026-11-02T13:00:00Z"}]' ;;
	*"repos/SeppoPakonen/demo-repo/pulls/1"*)
		echo '{"number":1,"title":"Add parity","body":"PR body text","state":"open","user":{"login":"alice"},"created_at":"2026-11-02T09:00:00Z","merged_at":null,"head":{"ref":"feature/parity"},"base":{"ref":"master"},"html_url":"https://github.com/SeppoPakonen/demo-repo/pull/1"}' ;;
	*"pr diff 1 -R SeppoPakonen/demo-repo"*)
		echo 'diff --git a/README.md b/README.md'
		echo '+added line' ;;
	*"repo list SeppoPakonen --limit 100 --json"*)
		echo '[{"name":"demo-repo","description":"Fixture repo","isPrivate":false,"pushedAt":"2026-11-03T09:15:00Z","stargazerCount":3,"forkCount":1,"primaryLanguage":{"name":"C++"},"url":"https://github.com/SeppoPakonen/demo-repo"},{"name":"second-repo","description":"","isPrivate":true,"pushedAt":"2026-11-04T08:00:00Z","stargazerCount":0,"forkCount":0,"primaryLanguage":null,"url":"https://github.com/SeppoPakonen/second-repo"}]' ;;
	*"repos/SeppoPakonen/demo-repo/actions/runs?per_page=15"*)
		echo '{"total_count":3,"workflow_runs":[{"id":501,"name":"CI","status":"completed","conclusion":"success","head_branch":"master","created_at":"2026-11-03T08:00:00Z","event":"push"},{"id":502,"name":"Docs","status":"in_progress","conclusion":null,"head_branch":"master","created_at":"2026-11-04T08:00:00Z","event":"pull_request"},{"id":503,"name":"Release","status":"completed","conclusion":"failure","head_branch":"master","created_at":"2026-11-05T08:00:00Z","event":"workflow_dispatch"}]}' ;;
	*)
		echo "fake gh: unexpected arguments: $*" >&2
		exit 2 ;;
esac
