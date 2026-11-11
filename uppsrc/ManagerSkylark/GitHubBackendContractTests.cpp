#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include <GitHubForge/GitHubGhConnector.h>
#include <GitHubForge/GitHubEmulatedBackend.h>

NAMESPACE_UPP

// Shared interface tests. One body of expectations runs against every
// GitHubForgeBackend implementation, both seeded with the same fixture
// (SeppoPakonen/demo-repo). The gh connector has to talk to an executable, so
// its run uses a canned gh that answers the same records; the emulation runs
// on its own store and needs no executable.

static const char *kContractProject = "SeppoPakonen/demo-repo";

static const char *kContractGhScript = R"SH(#!/bin/sh
# Canned gh for the shared contract run; unexpected argv fails loudly.
case "$*" in
	*"repos/SeppoPakonen/demo-repo/issues?state=all"*)
		echo '[{"number":1,"title":"Crash on start","state":"open","user":{"login":"alice"},"created_at":"2026-11-02T10:00:00Z","comments":1},{"number":2,"title":"Docs gap","state":"closed","user":{"login":"bob"},"created_at":"2026-11-03T12:00:00Z","comments":0}]' ;;
	*"repos/SeppoPakonen/demo-repo/issues/1/comments"*)
		echo '[{"id":11,"user":{"login":"carol"},"body":"Repro attached","created_at":"2026-11-02T11:00:00Z"}]' ;;
	*"repos/SeppoPakonen/demo-repo/issues/1"*)
		echo '{"number":1,"title":"Crash on start","body":"Start it and it crashes.","state":"open","user":{"login":"alice"},"created_at":"2026-11-02T10:00:00Z","comments":1}' ;;
	*"repos/SeppoPakonen/demo-repo/pulls?state=all"*)
		echo '[{"number":1,"title":"Add parity","state":"open","user":{"login":"alice"},"created_at":"2026-11-02T09:00:00Z","merged_at":null,"head":{"ref":"feature/parity"},"base":{"ref":"master"},"html_url":"https://github.com/SeppoPakonen/demo-repo/pull/1"}]' ;;
	*"repos/SeppoPakonen/demo-repo/pulls/1"*)
		echo '{"number":1,"title":"Add parity","body":"PR body text","state":"open","user":{"login":"alice"},"created_at":"2026-11-02T09:00:00Z","merged_at":null,"head":{"ref":"feature/parity"},"base":{"ref":"master"},"html_url":"https://github.com/SeppoPakonen/demo-repo/pull/1"}' ;;
	*"pr diff 1 -R SeppoPakonen/demo-repo"*)
		echo 'diff --git a/README.md b/README.md'
		echo '+added line' ;;
	*"repo create SeppoPakonen/new-repo --private"*)
		echo 'https://github.com/SeppoPakonen/new-repo' ;;
	*"repo list SeppoPakonen --limit 100 --json"*)
		echo '[{"name":"demo-repo","description":"Fixture repo","isPrivate":false,"pushedAt":"2026-11-03T09:15:00Z","stargazerCount":3,"forkCount":1,"primaryLanguage":{"name":"C++"},"url":"https://github.com/SeppoPakonen/demo-repo"},{"name":"second-repo","description":"","isPrivate":true,"pushedAt":"2026-11-04T08:00:00Z","stargazerCount":0,"forkCount":0,"primaryLanguage":null,"url":"https://github.com/SeppoPakonen/second-repo"}]' ;;
	*"repos/SeppoPakonen/demo-repo/actions/runs?per_page=15"*)
		echo '{"total_count":3,"workflow_runs":[{"id":501,"name":"CI","status":"completed","conclusion":"success","head_branch":"master","created_at":"2026-11-03T08:00:00Z","event":"push"},{"id":502,"name":"Docs","status":"in_progress","conclusion":null,"head_branch":"master","created_at":"2026-11-04T08:00:00Z","event":"pull_request"},{"id":503,"name":"Release","status":"completed","conclusion":"failure","head_branch":"master","created_at":"2026-11-05T08:00:00Z","event":"workflow_dispatch"}]}' ;;
	*)
		echo "unexpected gh arguments: $*" >&2
		exit 2 ;;
esac
)SH";

static bool ContractFail(const char *label, const char *what)
{
	Cerr() << "FAILED: " << label << ": " << what << "\n";
	return false;
}

// Expectations every implementation must meet on the fixture. Pull request
// listings are not checked for an exact count, because the emulation keeps
// the merged pull request that the canned gh omits.
static bool RunSharedContract(GitHubForgeBackend& b, const char *label)
{
	Vector<ForgeIssue> issues;
	ForgeResult r = b.ListIssues(kContractProject, issues);
	if(!r.ok || issues.GetCount() != 2 || issues[0].issue_id != "1" || issues[0].title != "Crash on start" ||
	   !issues[0].open || issues[0].author != "alice" || issues[0].comment_count != 1 || issues[1].open)
		return ContractFail(label, "issue list does not match the fixture");
	if(b.ListIssues("bad slug", issues).ok)
		return ContractFail(label, "invalid repository slug accepted");
	ForgeRepo unnamed;
	if(b.CreateRepository("SeppoPakonen", "bad name", true, unnamed).ok)
		return ContractFail(label, "invalid repository name accepted for creation");

	ForgeIssue one;
	r = b.GetIssue(kContractProject, "1", one);
	if(!r.ok || one.body != "Start it and it crashes.")
		return ContractFail(label, "get issue does not match the fixture");
	if(b.GetIssue(kContractProject, "99", one).ok)
		return ContractFail(label, "missing issue reported as found");

	Vector<ForgeComment> comments;
	r = b.ListComments(kContractProject, "1", comments);
	if(!r.ok || comments.GetCount() != 1 || comments[0].author != "carol" || comments[0].body != "Repro attached")
		return ContractFail(label, "comment thread does not match the fixture");

	Vector<ForgePullRequest> prs;
	r = b.ListPullRequests(kContractProject, prs);
	bool found = false;
	for(const ForgePullRequest& p : prs)
		if(p.pr_id == "1") {
			found = p.state == "open" && p.head == "feature/parity" && p.base == "master" && p.author == "alice";
		}
	if(!r.ok || !found)
		return ContractFail(label, "pull request list does not match the fixture");
	for(const ForgePullRequest& p : prs)
		if(p.pr_id == "1" && p.state == "open" && p.title != "Add parity")
			return ContractFail(label, "pull request title does not match the fixture");

	ForgePullRequest pr;
	r = b.GetPullRequest(kContractProject, "1", pr);
	if(!r.ok || pr.body != "PR body text" || pr.head != "feature/parity" || pr.state != "open")
		return ContractFail(label, "get pull request does not match the fixture");

	String diff;
	r = b.GetPullRequestDiff(kContractProject, "1", diff);
	if(!r.ok || diff.Find("+added line") < 0)
		return ContractFail(label, "pull request diff does not match the fixture");

	Vector<ForgeRun> runs;
	r = b.ListRuns(kContractProject, 15, runs);
	if(!r.ok || runs.GetCount() != 3 || runs[2].run_id != 503 || runs[2].conclusion != "failure")
		return ContractFail(label, "workflow runs do not match the fixture");

	Vector<ForgeRepo> repos;
	r = b.ListRepos("SeppoPakonen", repos);
	bool has_private = false;
	bool has_demo = false;
	for(const ForgeRepo& rep : repos) {
		if(rep.name == "demo-repo" && !rep.is_private)
			has_demo = true;
		if(rep.name == "second-repo" && rep.is_private)
			has_private = true;
	}
	if(!r.ok || !has_demo || !has_private)
		return ContractFail(label, "repository list does not match the fixture");
	return true;
}

// Emulation-only behavior: writes persist across instances sharing one store,
// merges change state, and a missing store fails closed.
static bool RunEmulatedWriteTests(const String& store)
{
	GitHubEmulatedBackend writer(store);
	GitHubEmulatedBackend reader(store);
	const char *label = "emulated writes";

	ForgeIssue draft;
	draft.title = "New issue";
	draft.body = "Filed by the contract run";
	ForgeIssue made;
	ForgeResult r = writer.CreateIssue(kContractProject, draft, made);
	if(!r.ok || made.issue_id != "3")
		return ContractFail(label, "create issue did not allocate the next number");

	Vector<ForgeIssue> issues;
	if(!reader.ListIssues(kContractProject, issues).ok || issues.GetCount() != 3)
		return ContractFail(label, "second instance does not see the created issue");

	ForgeComment note;
	note.body = "Noted";
	ForgeComment posted;
	if(!writer.AddComment(kContractProject, "3", note, posted).ok || posted.issue_id != "3")
		return ContractFail(label, "add comment failed");
	Vector<ForgeComment> thread;
	if(!reader.ListComments(kContractProject, "3", thread).ok || thread.GetCount() != 1 || thread[0].body != "Noted")
		return ContractFail(label, "second instance does not see the comment");

	ForgeIssue closed;
	if(!writer.SetIssueState(kContractProject, "3", false, closed).ok || closed.open)
		return ContractFail(label, "close issue failed");

	ForgePullRequest pr;
	if(!writer.CreatePullRequest(kContractProject, "Contract change", "body", "feature/contract", "master", pr).ok ||
	   pr.pr_id != "4")
		return ContractFail(label, "create pull request did not allocate the next number");
	String message;
	if(!writer.MergePullRequest(kContractProject, "4", message).ok || message != "Pull Request successfully merged")
		return ContractFail(label, "merge of an open pull request failed");
	ForgePullRequest merged;
	if(!reader.GetPullRequest(kContractProject, "4", merged).ok || merged.state != "merged")
		return ContractFail(label, "merge did not change the stored state");
	if(writer.MergePullRequest(kContractProject, "4", message).ok)
		return ContractFail(label, "merge of an already merged pull request accepted");

	if(writer.GetIssue(kContractProject, "99", made).ok)
		return ContractFail(label, "missing issue reported as found");
	if(writer.CreateIssue("SeppoPakonen/no-such-repo", draft, made).ok)
		return ContractFail(label, "write to a missing repository accepted");

	GitHubEmulatedBackend unconfigured("");
	ForgeResult nr = unconfigured.ListIssues(kContractProject, issues);
	if(nr.ok || nr.provenance != ForgeProvenance::Unavailable)
		return ContractFail(label, "unconfigured store did not fail closed");

	if(!FileExists(AppendFileName(store, GitHubEmulatedBackend::StoreFileName())))
		return ContractFail(label, "store file was not written");
	return true;
}

bool GitHubPlatformService::RunGitHubBackendContractTests(const String& scratch_dir)
{
	if(!RealizeDirectory(scratch_dir))
		return ContractFail("shared contract", "cannot create scratch directory");
	String gh = AppendFileName(scratch_dir, "contract-gh.sh");
	if(!SaveFile(gh, kContractGhScript))
		return ContractFail("shared contract", "cannot write canned gh fixture");
	Sys("chmod", {"+x", gh});

	GitHubGhConnector connector(gh);
	if(!RunSharedContract(connector, "gh connector"))
		return false;
	Cout() << "ok    shared GitHub contract holds for the gh connector\n";
	ForgeRepo created;
	ForgeResult cr = connector.CreateRepository("SeppoPakonen", "new-repo", true, created);
	if(!cr.ok || created.name != "new-repo" || created.owner != "SeppoPakonen" || !created.is_private ||
	   cr.provenance != ForgeProvenance::Remote)
		return ContractFail("gh connector", "private repository creation did not succeed through gh");
	Cout() << "ok    gh connector creates a private repository through gh\n";

	String store = AppendFileName(scratch_dir, "contract-store");
	GitHubEmulatedBackend emulated(store);
	if(!RunSharedContract(emulated, "emulated"))
		return false;
	Cout() << "ok    shared GitHub contract holds for the emulated backend\n";
	ForgeRepo refused;
	if(emulated.CreateRepository("SeppoPakonen", "new-repo", true, refused).ok)
		return ContractFail("emulated", "the emulation claimed to create a remote repository");
	Cout() << "ok    the emulated backend refuses repository creation\n";

	if(!RunEmulatedWriteTests(store))
		return false;
	Cout() << "ok    emulated writes persist, merge, and fail closed\n";

	GitHubForgeBackend::Config previous = GitHubForgeBackend::GetConfig();
	GitHubForgeBackend::Config cfg;
	cfg.kind = GitHubForgeBackend::Kind::Emulated;
	cfg.store_dir = store;
	GitHubForgeBackend::Configure(cfg);
	One<GitHubForgeBackend> chosen = GitHubForgeBackend::Create();
	bool is_emulated = dynamic_cast<GitHubEmulatedBackend *>(&*chosen) != NULL;
	GitHubForgeBackend::Configure(previous);
	if(!is_emulated)
		return ContractFail("backend selection", "emulated configuration did not create the emulation");
	Cout() << "ok    backend selection follows the configured implementation\n";
	return true;
}

END_UPP_NAMESPACE
