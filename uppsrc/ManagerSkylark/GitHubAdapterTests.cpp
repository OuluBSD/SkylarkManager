#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "GitHubRepoPage.h"
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

	GitHubRepoPage page;
	page.owner = "acme";
	page.repo = "tools";
	page.pr_num = 7;
	RenderRepoPullsList(page);
	if(page.content.Find("unavailable from GitHub") < 0)
		return Fail("the pull request tab must show the unavailable state, not an empty list");
	page.content.Clear();
	RenderRepoPullDetail(page);
	if(page.content.Find("Pull request #7 is unavailable from GitHub") < 0)
		return Fail("the pull request detail must show the unavailable state, not sample data");
	Cout() << "ok    pull request tab and detail render the unavailable state\n";

	unavailable.Clear();
	GitHubPlatformService::GetLiveActions("acme", "tools", &unavailable);
	if(unavailable.IsEmpty())
		return Fail("workflow runs must report unavailable when gh cannot run");
	Cout() << "ok    workflow runs report unavailable when gh cannot run\n";

	SetEnv("GHFORGE_GH", "");
	return true;
}

END_UPP_NAMESPACE
