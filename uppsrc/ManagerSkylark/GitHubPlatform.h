#ifndef _reference_ManagerSkylark_GitHubPlatform_h_
#define _reference_ManagerSkylark_GitHubPlatform_h_

#include "ManagerSkylark.h"

NAMESPACE_UPP

struct LocalRepoInfo : Moveable<LocalRepoInfo> {
	String name;
	String path;
	String owner;
	String branch;
	int    commits = 0;
	String last_commit_hash;
	String last_commit_author;
	String last_commit_time;
	String last_commit_msg;
	String description;
	String language;
	bool   is_private = false;
	int    stars = 0;
	int    forks = 0;
	int    open_issues = 0;

	void Jsonize(JsonIO& io) {
		io("name", name)("path", path)("owner", owner)("branch", branch)
		  ("commits", commits)("last_commit_hash", last_commit_hash)
		  ("last_commit_author", last_commit_author)("last_commit_time", last_commit_time)
		  ("last_commit_msg", last_commit_msg)("description", description)
		  ("language", language)("is_private", is_private)("stars", stars)
		  ("forks", forks)("open_issues", open_issues);
	}
};

struct WorkflowTemplate : Moveable<WorkflowTemplate> {
	String id;
	String title;
	String category; // "continuous-integration", "deployment", "automation", "pages"
	String author;
	String description;
	String icon;
	String default_filename;
	String default_yaml;
	bool   suggested = false;
};

struct MarketplaceAction : Moveable<MarketplaceAction> {
	String id;
	String name;
	String author;
	String version;
	String stars;
	String description;
	String icon;
	String yaml_snippet;
	String doc_summary;
};

struct PackageBuildCacheInfo : Moveable<PackageBuildCacheInfo> {
	String package_name;
	String flags;
	String hash;
	String last_status; // "success", "failed", "untested"
	int    duration_secs = 0;
	bool   is_cached = true;
};

class GitHubPlatformService {
public:
	static String GetDevDir();
	static Vector<LocalRepoInfo> GetRepositories(const String& owner = "SeppoPakonen");
	static LocalRepoInfo GetRepo(const String& owner, const String& repo);
	static ValueMap LoadScrapedData(const String& owner, const String& repo);
	static String RenderRepoTabs(const String& owner, const String& repo, const String& active_tab, const LocalRepoInfo& info);
	static String RenderRepoHeader(const LocalRepoInfo& info);
	static Vector<String> GetBranches(const String& repo_path);
	static Vector<ValueMap> GetTree(const String& repo_path, const String& subpath = "");
	static ValueMap GetBlob(const String& repo_path, const String& filepath);
	static Vector<ValueMap> GetCommits(const String& repo_path, int limit = 30);
	static String EscapeHtml(const String& s);
	static String FormatFileSize(int64 bytes);

	// Live GitHub dynamic queries via gh CLI
	static bool                  IsGhCliAvailable();
	static Vector<LocalRepoInfo> GetLiveGitHubRepos(const String& owner = "SeppoPakonen");
	static Vector<ValueMap>      GetLiveIssues(const String& owner, const String& repo);
	static ValueMap              GetLiveIssueDetail(const String& owner, const String& repo, int number);
	static Vector<ValueMap>      GetLivePullRequests(const String& owner, const String& repo);
	static ValueMap              GetLivePullRequestDetail(const String& owner, const String& repo, int number);
	static String                GetPullRequestDiff(const String& owner, const String& repo, int number);
	static String                CreatePullRequest(const String& owner, const String& repo, const String& title, const String& body, const String& head, const String& base);
	static String                MergePullRequest(const String& owner, const String& repo, int number);
	static String                ClosePullRequest(const String& owner, const String& repo, int number);
	static String                AddPullRequestComment(const String& owner, const String& repo, int number, const String& body);
	static Vector<ValueMap>      GetLiveActions(const String& owner, const String& repo);
	static String                CreateIssue(const String& owner, const String& repo, const String& title, const String& body);
	static String                AddComment(const String& owner, const String& repo, int number, const String& body);
	static String                ToggleIssueState(const String& owner, const String& repo, int number, bool close_it);

	// Actions, Starter Workflows & C++ Caching Parity
	static Vector<WorkflowTemplate>      GetWorkflowTemplates(const String& owner, const String& repo);
	static WorkflowTemplate              GetWorkflowTemplate(const String& id);
	static Vector<MarketplaceAction>     GetMarketplaceActions();
	static Vector<PackageBuildCacheInfo> GetPackageBuildCaches(const String& repo_path);
	static String                        RenderActionsCatalog(const String& owner, const String& repo, const String& category, const String& search_q);
	static String                        RenderWorkflowEditor(const String& owner, const String& repo, const String& template_id, const String& filename);
	static String                        RenderOperatorWarningBar(const String& owner, const String& repo);
	static String                        RenderTppDocSite(const String& tpp_dir);
	static bool                          SaveWorkflowFile(const String& repo_path, const String& filename, const String& content);
	static void                          TriggerWorkflowRunAsync(const String& owner, const String& repo, const String& workflow_name, const String& run_type);
};

END_UPP_NAMESPACE

#endif
