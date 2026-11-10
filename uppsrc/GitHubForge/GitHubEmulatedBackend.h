#ifndef _GitHubForge_GitHubEmulatedBackend_h_
#define _GitHubForge_GitHubEmulatedBackend_h_

#include "GitHubForge.h"

NAMESPACE_UPP

// Stateful local GitHub emulation: no network, no gh executable. All records
// live in <store_dir>/github-emulated.json. Each operation holds an exclusive
// flock on <store_dir>/github-emulated.lock for its whole read-modify-write,
// and a mutation replaces the file atomically (temp file, fsync, rename), so
// several CLI and HTTP processes can share one store. A missing store is seeded
// with the deterministic fixture on first use. Without POSIX file locks the
// emulation refuses to run (fail closed).
class GitHubEmulatedBackend : public GitHubForgeBackend {
public:
	GitHubEmulatedBackend(const String& store_dir);

	bool AuthAvailable() override { return !store_dir.IsEmpty(); }

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
	ForgeResult ListRuns(const String& project, int limit, Vector<ForgeRun>& out) override;

	static String StoreFileName() { return "github-emulated.json"; }

private:
	String store_dir;
};

END_UPP_NAMESPACE

#endif
