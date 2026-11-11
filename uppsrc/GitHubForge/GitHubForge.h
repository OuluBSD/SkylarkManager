#ifndef _GitHubForge_GitHubForge_h_
#define _GitHubForge_GitHubForge_h_

#include <Core/Core.h>
#include <ForgeCore/ForgeCore.h>

NAMESPACE_UPP

// GitHub forge contract. Two implementations sit behind it:
//   GitHubGhConnector      github.com through the argv-only gh CLI (remote provenance)
//   GitHubEmulatedBackend  file-backed local emulation, no network, no gh executable
// Callers obtain an instance from Create(), which follows the process-wide
// configuration set once at startup with Configure(). Failures report
// provenance=unavailable (connector) or a diagnostic (emulation); there is no
// fallback from one implementation to the other.
class GitHubForgeBackend : public IForgeBackend {
public:
	enum class Kind { Gh, Emulated };

	struct Config {
		Kind   kind = Kind::Gh;
		String gh_bin;       // Kind::Gh: gh executable (default: GHFORGE_GH or "gh")
		String store_dir;    // Kind::Emulated: directory holding github-emulated.json
	};

	// Set before the server starts; Create() reads it at each call.
	static void          Configure(const Config& cfg);
	static const Config& GetConfig();
	static bool          ParseKind(const String& name, Kind& kind);
	static One<GitHubForgeBackend> Create();

	String          BackendId() const override { return "github"; }
	const ForgeCapabilities& Capabilities() const override { return caps; }

	// Whether the implementation can reach GitHub (gh logged in, or the emulation is usable).
	virtual bool AuthAvailable() = 0;

	// Pull requests, repositories and workflow runs. These are GitHub-specific
	// until the forge domain grows the matching contract. Pull requests share
	// the issue comment endpoints: use AddComment/ListComments with the number.
	virtual ForgeResult ListPullRequests(const String& project, Vector<ForgePullRequest>& out) = 0;
	virtual ForgeResult GetPullRequest(const String& project, const String& pr_id, ForgePullRequest& out) = 0;
	virtual ForgeResult GetPullRequestDiff(const String& project, const String& pr_id, String& out) = 0;
	virtual ForgeResult CreatePullRequest(const String& project, const String& title, const String& body,
	                                      const String& head, const String& base, ForgePullRequest& out) = 0;
	virtual ForgeResult SetPullRequestState(const String& project, const String& pr_id, bool open, ForgePullRequest& out) = 0;
	virtual ForgeResult MergePullRequest(const String& project, const String& pr_id, String& message) = 0;
	virtual ForgeResult ListRepos(const String& owner, Vector<ForgeRepo>& out) = 0;
	// Creates a repository under owner. The emulation never creates remote repositories
	// and reports that as a failure; there is no fake success.
	virtual ForgeResult CreateRepository(const String& owner, const String& name, bool is_private, ForgeRepo& out) = 0;
	virtual ForgeResult ListRuns(const String& project, int limit, Vector<ForgeRun>& out) = 0;

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

	static String Field(const ValueMap& m, const char *key);
	static String SubField(const Value& v, const char *key);

protected:
	GitHubForgeBackend() {}

	ForgeCapabilities caps;
};

END_UPP_NAMESPACE

#endif
