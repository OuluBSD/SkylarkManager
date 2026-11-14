#include "GitHubPlatform.h"
#include "GitHubRepoPage.h"
#include "GitHubForgeHost.h"
#include <GitHubForge/GitHubForge.h>

NAMESPACE_UPP

// Synthetic checks for the GitHub adapter path. A fake gh script stands in for
// the CLI through GHFORGE_GH: it answers the adapter's argv shapes with canned
// JSON, so no network or credentials are involved.
static const char *kFakeGhScript = R"SH(#!/bin/sh
# Fake gh for the GitHub adapter self-test; unexpected argv fails loudly.
case "$*" in
	*"repos/acme/tools/issues?state=all"*)
		echo '[{"number":3,"title":"Open one","state":"open","user":{"login":"alice"},"created_at":"2026-10-30T18:00:00Z","comments":2},{"number":4,"title":"A pull request","state":"open","user":{"login":"bob"},"created_at":"2026-10-01T00:00:00Z","comments":0,"pull_request":{"url":"x"}}]' ;;
	*"repos/acme/tools/issues/3/comments"*)
		echo '[{"id":77,"user":{"login":"carol"},"body":"hello","created_at":"2026-10-31T10:00:00Z"}]' ;;
	*"repos/acme/tools/issues/3"*)
		echo '{"number":3,"title":"Open one","body":"Body text","state":"open","user":{"login":"alice"},"created_at":"2026-10-30T18:00:00Z","comments":1}' ;;
	*"-X POST repos/acme/tools/issues "*)
		echo '{"number":9,"title":"New","state":"open","user":{"login":"alice"},"created_at":"2026-11-01T08:00:00Z","comments":0}' ;;
	*"-X POST repos/acme/tools/pulls "*)
		echo '{"number":12,"title":"Title","state":"open","user":{"login":"alice"},"created_at":"2026-11-03T09:00:00Z","merged_at":null,"head":{"ref":"feature/x"},"base":{"ref":"master"},"html_url":"https://github.com/acme/tools/pull/12"}' ;;
	*"-X PUT repos/acme/tools/pulls/7/merge"*)
		echo '{"sha":"abc123","merged":true,"message":"Pull Request successfully merged"}' ;;
	*"-X PUT repos/acme/tools/pulls/9/merge"*)
		echo '{"merged":false,"message":"Pull Request is not mergeable"}' ;;
	*"-X PATCH repos/acme/tools/pulls/7 "*)
		echo '{"number":7,"title":"Add parity","state":"closed","user":{"login":"alice"},"created_at":"2026-10-20T09:00:00Z","merged_at":null,"head":{"ref":"feature/parity"},"base":{"ref":"master"},"html_url":"https://github.com/acme/tools/pull/7"}' ;;
	*"-X POST repos/acme/tools/issues/7/comments "*)
		echo '{"id":92,"user":{"login":"alice"},"body":"Looks good","created_at":"2026-11-03T10:00:00Z"}' ;;
	*"repos/acme/tools/pulls?state=all"*)
		echo '[{"number":7,"title":"Add parity","state":"open","user":{"login":"alice"},"created_at":"2026-10-20T09:00:00Z","merged_at":null,"head":{"ref":"feature/parity"},"base":{"ref":"master"},"html_url":"https://github.com/acme/tools/pull/7"},{"number":8,"title":"Old change","state":"closed","user":{"login":"bob"},"created_at":"2026-10-02T09:00:00Z","merged_at":"2026-10-03T09:00:00Z","head":{"ref":"feature/old"},"base":{"ref":"master"},"html_url":"https://github.com/acme/tools/pull/8"}]' ;;
	*"repos/acme/tools/issues/7/comments"*)
		echo '[{"id":91,"user":{"login":"carol"},"body":"PR thread comment","created_at":"2026-10-21T10:00:00Z"}]' ;;
	*"repos/acme/tools/pulls/7"*)
		echo '{"number":7,"title":"Add parity","body":"PR body","state":"open","user":{"login":"alice"},"created_at":"2026-10-20T09:00:00Z","merged_at":null,"head":{"ref":"feature/parity"},"base":{"ref":"master"},"html_url":"https://github.com/acme/tools/pull/7"}' ;;
	*"pr diff 7 -R acme/tools"*)
		echo 'diff --git a/README.md b/README.md'
		echo '+added line' ;;
	*"repo list acme --limit 100 --json"*)
		echo '[{"name":"tools","description":"Tooling","isPrivate":false,"pushedAt":"2026-11-01T10:00:00Z","stargazerCount":5,"forkCount":2,"primaryLanguage":{"name":"C++"},"url":"https://github.com/acme/tools"}]' ;;
	*"repos/acme/tools/actions/runs?per_page=15"*)
		echo '{"total_count":1,"workflow_runs":[{"id":501,"name":"CI","status":"completed","conclusion":"success","head_branch":"master","created_at":"2026-11-02T08:00:00Z","event":"push"}]}' ;;
	*)
		echo "unexpected gh arguments: $*" >&2
		exit 2 ;;
esac
)SH";

static bool Fail(const String& what)
{
	Cerr() << "FAILED: " << what << "\n";
	return false;
}

bool GitHubPlatformService::RunGitHubAdapterSyntheticTests(const String& scratch_dir)
{
	String gh = AppendFileName(scratch_dir, "fake-gh.sh");
	if(!SaveFile(gh, kFakeGhScript))
		return Fail("cannot write fake gh fixture");
	Sys("chmod", {"+x", gh});
	SetEnv("GHFORGE_GH", gh);

	String unavailable;
	Vector<ValueMap> issues = GitHubPlatformService::GetLiveIssues("acme", "tools", &unavailable);
	if(!unavailable.IsEmpty() || issues.GetCount() != 1 || (int)issues[0]["number"] != 3 ||
	   (String)issues[0]["title"] != "Open one" || (String)issues[0]["state"] != "OPEN" ||
	   (String)issues[0]["author"] != "alice" || (String)issues[0]["created_at"] != "2026-10-30" ||
	   (int)issues[0]["comments"] != 2)
		return Fail("issue listing did not map the adapter result (pull requests must be filtered): " + unavailable);
	Cout() << "ok    GitHub issue list maps the adapter result and filters pull requests\n";

	ValueMap detail = GitHubPlatformService::GetLiveIssueDetail("acme", "tools", 3, &unavailable);
	ValueArray thread = detail["comments"];
	if(!unavailable.IsEmpty() || (String)detail["title"] != "Open one" || (String)detail["state"] != "OPEN" ||
	   (String)detail["author"]["login"] != "alice" || thread.GetCount() != 1 ||
	   (String)thread[0]["author"]["login"] != "carol" || (String)thread[0]["body"] != "hello")
		return Fail("issue detail did not map the adapter result");
	Cout() << "ok    GitHub issue detail maps the issue and its comment thread\n";

	String created = GitHubPlatformService::CreateIssue("acme", "tools", "New", "Body");
	if(!created.StartsWith("https://github.com/acme/tools/issues/9"))
		return Fail("issue creation did not return the created issue URL: " + created);
	Cout() << "ok    GitHub issue creation returns the created issue URL\n";

	unavailable.Clear();
	Vector<ValueMap> prs = GitHubPlatformService::GetLivePullRequests("acme", "tools", &unavailable);
	if(!unavailable.IsEmpty() || prs.GetCount() != 2 || (int)prs[0]["number"] != 7 ||
	   (String)prs[0]["state"] != "OPEN" || (String)prs[0]["author"] != "alice" ||
	   (String)prs[0]["head"] != "feature/parity" || (String)prs[0]["base"] != "master" ||
	   (String)prs[0]["created_at"] != "2026-10-20" || (String)prs[1]["state"] != "MERGED")
		return Fail("pull request listing did not map the adapter result");
	Cout() << "ok    GitHub pull request list maps open and merged states\n";

	unavailable.Clear();
	ValueMap pr = GitHubPlatformService::GetLivePullRequestDetail("acme", "tools", 7, &unavailable);
	ValueArray pr_thread = pr["comments"];
	if(!unavailable.IsEmpty() || (String)pr["title"] != "Add parity" || (String)pr["state"] != "OPEN" ||
	   (String)pr["author"]["login"] != "alice" || (String)pr["headRefName"] != "feature/parity" ||
	   pr_thread.GetCount() != 1 || (String)pr_thread[0]["body"] != "PR thread comment")
		return Fail("pull request detail did not map the adapter result");
	Cout() << "ok    GitHub pull request detail maps the pull request and its comment thread\n";

	unavailable.Clear();
	String diff = GitHubPlatformService::GetPullRequestDiff("acme", "tools", 7, &unavailable);
	if(!unavailable.IsEmpty() || diff.Find("+added line") < 0)
		return Fail("pull request diff was not returned as text");
	Cout() << "ok    GitHub pull request diff is returned as plain text\n";

	unavailable.Clear();
	Vector<ValueMap> runs = GitHubPlatformService::GetLiveActions("acme", "tools", &unavailable);
	if(!unavailable.IsEmpty() || runs.GetCount() != 1 || (int64)runs[0]["id"] != 501 ||
	   (String)runs[0]["name"] != "CI" || (String)runs[0]["conclusion"] != "success" ||
	   (String)runs[0]["branch"] != "master" || (String)runs[0]["created_at"] != "2026-11-02")
		return Fail("workflow runs did not map the adapter result");
	Cout() << "ok    GitHub workflow runs map the run list\n";

	Vector<LocalRepoInfo> repos = GitHubPlatformService::GetLiveGitHubRepos("acme");
	if(repos.GetCount() != 1 || repos[0].name != "tools" || repos[0].stars != 5 ||
	   repos[0].language != "C++" || repos[0].last_commit_time != "2026-11-01")
		return Fail("repository list did not map the adapter result");
	Cout() << "ok    GitHub repository list maps the adapter result\n";

	String repo_dir = AppendFileName(scratch_dir, "local-repo");
	RealizeDirectory(repo_dir);
	Sys("git", {"-C", repo_dir, "init", "-q"});
	Sys("git", {"-C", repo_dir, "-c", "user.name=Skylark Test", "-c", "user.email=test@example.invalid",
	            "commit", "-q", "--allow-empty", "-m", "fixture"});
	Vector<ValueMap> commits = GitHubPlatformService::GetCommits(repo_dir, 5);
	if(commits.GetCount() != 1 || (String)commits[0]["msg"] != "fixture")
		return Fail("local commit log did not come back through argv git");
	if(GitHubPlatformService::GetBranches(repo_dir).IsEmpty())
		return Fail("local branch list came back empty");
	Cout() << "ok    local git log and branches run argv-only and return the fixture repo\n";

	String created_pr = GitHubPlatformService::CreatePullRequest("acme", "tools", "Title", "Body", "feature/x", "");
	if(!created_pr.StartsWith("https://github.com/acme/tools/pull/12"))
		return Fail("pull request creation did not return the created pull request URL: " + created_pr);
	Cout() << "ok    GitHub pull request creation returns the created pull request URL\n";

	if(!GitHubPlatformService::CreatePullRequest("acme", "tools", "Title", "Body", "bad branch;rm", "master").StartsWith("error: invalid head branch"))
		return Fail("pull request creation must reject an unsafe branch name before gh runs");
	Cout() << "ok    GitHub pull request creation rejects unsafe branch names before gh runs\n";

	if(GitHubPlatformService::MergePullRequest("acme", "tools", 7) != "Pull Request successfully merged")
		return Fail("pull request merge did not report the GitHub message");
	String refused = GitHubPlatformService::MergePullRequest("acme", "tools", 9);
	if(!refused.StartsWith("error:") || refused.Find("not mergeable") < 0)
		return Fail("a refused pull request merge must report an error, not success: " + refused);
	Cout() << "ok    GitHub pull request merge reports success and refusal\n";

	if(GitHubPlatformService::ClosePullRequest("acme", "tools", 7) != "Closed successfully")
		return Fail("pull request close did not report success");
	Cout() << "ok    GitHub pull request close reports success\n";

	String pr_comment = GitHubPlatformService::AddPullRequestComment("acme", "tools", 7, "Looks good");
	if(!pr_comment.StartsWith("https://github.com/acme/tools/issues/7#issuecomment-92"))
		return Fail("pull request comment did not return the comment URL: " + pr_comment);
	Cout() << "ok    GitHub pull request comment returns the comment URL\n";

	SetEnv("GHFORGE_GH", AppendFileName(scratch_dir, "no-such-gh"));
	unavailable.Clear();
	issues = GitHubPlatformService::GetLiveIssues("acme", "tools", &unavailable);
	if(!issues.IsEmpty() || unavailable.IsEmpty())
		return Fail("a missing gh executable must report unavailable, not fabricate issues");
	Cout() << "ok    GitHub issue list reports unavailable when gh cannot run\n";

	unavailable.Clear();
	prs = GitHubPlatformService::GetLivePullRequests("acme", "tools", &unavailable);
	if(!prs.IsEmpty() || unavailable.IsEmpty())
		return Fail("a missing gh executable must report pull requests unavailable, not fabricate them");
	Cout() << "ok    GitHub pull request list reports unavailable when gh cannot run\n";

	unavailable.Clear();
	GitHubPlatformService::GetLiveActions("acme", "tools", &unavailable);
	if(unavailable.IsEmpty())
		return Fail("workflow runs must report unavailable when gh cannot run");
	Cout() << "ok    workflow runs report unavailable when gh cannot run\n";

	// Visibility: unknown, public and private each render their own label.
	if(GitHubPlatformService::VisibilityLabel(-1) != "Unknown" || GitHubPlatformService::VisibilityLabel(0) != "Public" ||
	   GitHubPlatformService::VisibilityLabel(1) != "Private")
		return Fail("visibility labels must render unknown, public and private");
	LocalRepoInfo vis;
	vis.owner = "acme";
	vis.name = "tools";
	vis.path = scratch_dir;
	for(int v = -1; v <= 1; v++) {
		vis.is_private = v;
		String header = GitHubPlatformService::RenderRepoHeader(vis);
		String expect = String("<span class=\"wf-gh-badge\">") + GitHubPlatformService::VisibilityLabel(v) + "</span>";
		if(header.Find(expect) < 0)
			return Fail("repo header did not render the visibility badge for is_private=" + AsString(v));
	}
	Cout() << "ok    repo header renders unknown, public and private visibility\n";

	// The security and settings tabs' claims are checked on their captured bodies
	// by upptst/GitHubCapture/check-claims.sh: their templates need the app's root.

	SetEnv("GHFORGE_GH", "");
	return true;
}

END_UPP_NAMESPACE
