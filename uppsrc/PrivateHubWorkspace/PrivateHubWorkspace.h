#ifndef _PrivateHubWorkspace_PrivateHubWorkspace_h_
#define _PrivateHubWorkspace_PrivateHubWorkspace_h_

#include <Core/Core.h>
#include <ForgeCore/ForgeCore.h>
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// Live local-repo facts: branch, head commit, dirty file count.
struct RepoInfo {
	String org;
	String repo;
	String path;        // absolute, contained
	String branch;
	String head_commit;
	int    dirty_files = 0;
	bool   is_repo = false;
	String error;
};

// PrivateHub backend: issues in the site's WebStorage store; repository
// inspection over the local filesystem with argv-only git.
class PrivateHubWorkspaceBackend : public IForgeBackend {
public:
	// workspace_root: contained root for <org>/<repo> checkouts.
	// issues_root: site-scoped storage root for PrivateHub's own issue records.
	PrivateHubWorkspaceBackend(const String& workspace_root, const String& issues_root);

	String          BackendId() const override { return "privatehub"; }
	const ForgeCapabilities& Capabilities() const override { return caps; }

	// --- IForgeBackend over the site's JSON issue store ---
	ForgeResult ListIssues(const String& project, Vector<ForgeIssue>& out) override;
	ForgeResult GetIssue(const String& project, const String& issue_id, ForgeIssue& out) override;
	ForgeResult CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out) override;
	ForgeResult AddComment(const String& project, const String& issue_id,
	                       const ForgeComment& comment, ForgeComment& out) override;
	ForgeResult SetIssueState(const String& project, const String& issue_id,
	                          bool open, ForgeIssue& out) override;
	ForgeResult ListComments(const String& project, const String& issue_id,
	                         Vector<ForgeComment>& out) override;

	// --- workspace inspection ---
	static bool ValidName(const String& name);       // org or repo name
	bool        IsContained(const String& rel) const;
	String      RepoPath(const String& org, const String& repo) const;

	RepoInfo RepoStatus(const String& org, const String& repo) const;
	bool     ListOrganizations(Vector<String>& out) const;
	bool     ListRepositories(const String& org, Vector<String>& out) const;

private:
	IRecordCollection& Issues(const String& project);
	String           IssuesRoot() const { return issues_root; }

	String        workspace_root;
	String        issues_root;
	ForgeCapabilities caps;
};

END_UPP_NAMESPACE

#endif
