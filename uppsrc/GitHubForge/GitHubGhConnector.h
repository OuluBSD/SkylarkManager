#ifndef _GitHubForge_GitHubGhConnector_h_
#define _GitHubForge_GitHubGhConnector_h_

#include "GitHubForge.h"

NAMESPACE_UPP

// github.com adapter: implements GitHubForgeBackend through the gh CLI.
// All invocations are argv-only (LocalProcess::Start2); user data never
// reaches a shell. Failures report provenance=unavailable; there is no
// synthetic fallback.
class GitHubGhConnector : public GitHubForgeBackend {
public:
	// gh_bin: path to the gh executable (or "gh" on PATH). The environment
	// variable GHFORGE_GH overrides the default at construction time.
	GitHubGhConnector(const String& gh_bin = String());

	bool AuthAvailable() override { return GhRun({"auth", "status"}).ok; }

	ForgeResult ListIssues(const String& project, Vector<ForgeIssue>& out) override;
	ForgeResult GetIssue(const String& project, const String& issue_id, ForgeIssue& out) override;
	ForgeResult CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out) override;
	ForgeResult AddComment(const String& project, const String& issue_id,
	                       const ForgeComment& comment, ForgeComment& out) override;
	ForgeResult SetIssueState(const String& project, const String& issue_id,
	                          bool open, ForgeIssue& out) override;
	ForgeResult ListComments(const String& project, const String& issue_id,
	                         Vector<ForgeComment>& out) override;

	ForgeResult ListPullRequests(const String& project, Vector<ForgePullRequest>& out) override;
	ForgeResult GetPullRequest(const String& project, const String& pr_id, ForgePullRequest& out) override;
	ForgeResult GetPullRequestDiff(const String& project, const String& pr_id, String& out) override;
	ForgeResult CreatePullRequest(const String& project, const String& title, const String& body,
	                              const String& head, const String& base, ForgePullRequest& out) override;
	ForgeResult SetPullRequestState(const String& project, const String& pr_id, bool open, ForgePullRequest& out) override;
	ForgeResult MergePullRequest(const String& project, const String& pr_id, String& message) override;
	ForgeResult ListRepos(const String& owner, Vector<ForgeRepo>& out) override;
	ForgeResult CreateRepository(const String& owner, const String& name, bool is_private, ForgeRepo& out) override;
	ForgeResult ListRuns(const String& project, int limit, Vector<ForgeRun>& out) override;

	// One gh invocation. Exposed for tests.
	struct GhOutcome {
		bool   ok = false;
		int    exit_code = -1;
		String output;
		String error;
		String command_summary;
	};
	GhOutcome GhRun(const Vector<String>& args, int timeout_ms = 30000);

private:
	static String ResolveGhPath(const String& configured);

	String gh_bin;
};

END_UPP_NAMESPACE

#endif
