#ifndef _GitHubForge_GitHubForge_h_
#define _GitHubForge_GitHubForge_h_

#include <Core/Core.h>
#include <ForgeCore/ForgeCore.h>

NAMESPACE_UPP

// Official github.com adapter: implements IForgeBackend through the gh CLI.
// All invocations are argv-only (LocalProcess::Start2); user data never
// reaches a shell. Failures report provenance=unavailable; there is no
// synthetic fallback.
class GitHubForgeBackend : public IForgeBackend {
public:
	// gh_bin: path to the gh executable (or "gh" on PATH). The environment
	// variable GHFORGE_GH overrides the default at construction time.
	GitHubForgeBackend(const String& gh_bin = String());

	String          BackendId() const override { return "github"; }
	const ForgeCapabilities& Capabilities() const override { return caps; }

	ForgeResult ListIssues(const String& project, Vector<ForgeIssue>& out) override;
	ForgeResult GetIssue(const String& project, const String& issue_id, ForgeIssue& out) override;
	ForgeResult CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out) override;
	ForgeResult AddComment(const String& project, const String& issue_id,
	                       const ForgeComment& comment, ForgeComment& out) override;
	ForgeResult SetIssueState(const String& project, const String& issue_id,
	                          bool open, ForgeIssue& out) override;
	ForgeResult ListComments(const String& project, const String& issue_id,
	                         Vector<ForgeComment>& out) override;

	// Pull requests, repositories and workflow runs. These are GitHub-specific
	// until the forge domain grows the matching contract. Pull requests share
	// the issue comment endpoints: use AddComment/ListComments with the number.
	ForgeResult ListPullRequests(const String& project, Vector<ForgePullRequest>& out);
	ForgeResult GetPullRequest(const String& project, const String& pr_id, ForgePullRequest& out);
	ForgeResult GetPullRequestDiff(const String& project, const String& pr_id, String& out);
	ForgeResult CreatePullRequest(const String& project, const String& title, const String& body,
	                              const String& head, const String& base, ForgePullRequest& out);
	ForgeResult SetPullRequestState(const String& project, const String& pr_id, bool open, ForgePullRequest& out);
	ForgeResult MergePullRequest(const String& project, const String& pr_id, String& message);
	ForgeResult ListRepos(const String& owner, Vector<ForgeRepo>& out);
	ForgeResult ListRuns(const String& project, int limit, Vector<ForgeRun>& out);

	// One gh invocation. Exposed for tests.
	struct GhOutcome {
		bool   ok = false;
		int    exit_code = -1;
		String output;
		String error;
		String command_summary;
	};
	GhOutcome GhRun(const Vector<String>& args, int timeout_ms = 30000);

	static bool IsValidRepoSlug(const String& slug);
	static bool IsValidIssueId(const String& issue_id);
	static bool IsValidLogin(const String& login);
	static bool IsValidBranchName(const String& branch);

	static ForgePullRequest PullRequestFromJson(const ValueMap& m, const String& project);
	static ForgeRepo RepoFromJson(const ValueMap& m, const String& owner);
	static ForgeRun RunFromJson(const ValueMap& m);

	// "2026-11-08T12:34:56Z" -> Time (UTC), Null on failure.
	static Time ParseGhTime(const String& iso);

	static ForgeIssue IssueFromJson(const ValueMap& m, const String& project);
	static ForgeComment CommentFromJson(const ValueMap& m);

private:
	static String ResolveGhPath(const String& configured);

	String           gh_bin;
	ForgeCapabilities caps;
};

END_UPP_NAMESPACE

#endif
