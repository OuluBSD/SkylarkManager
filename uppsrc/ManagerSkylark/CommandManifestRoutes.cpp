#include "ManagerSkylark.h"
#include "EngineComposition.h"
#include "CommandManifestRoutes.h"

NAMESPACE_UPP

// The manifest describes each command; it never runs one. Broker operations
// are enforced by the service that serves them, through the caller's scopes.
static const char *kBrokerAuth = "broker caller with the operation's scope (enforced by the service)";

void RegisterComposedBrokerCommands()
{
	CommandManifest& cm = TheWebEngine().Manifest();
	Vector<String> project = {"project:string:required"};

	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-issue-list", "issue.list", kBrokerAuth, "List issues of a project", project));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-issue-get", "issue.get", kBrokerAuth, "Read one issue",
	                                                 {"project:string:required", "issue_id:string:required"}));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-issue-create", "issue.create", kBrokerAuth, "Open an issue",
	                                                 {"project:string:required", "title:string:required", "body:text"}));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-issue-comment", "issue.comment", kBrokerAuth, "Comment on an issue",
	                                                 {"project:string:required", "issue_id:string:required", "body:text:required"}));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-wiki-list", "wiki.list", kBrokerAuth, "List wiki pages", project));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-wiki-get", "wiki.get", kBrokerAuth, "Read one wiki page",
	                                                 {"project:string:required", "slug:string:required"}));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-topic-list", "topic.list", kBrokerAuth, "List discussion topics", project));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-topic-get", "topic.get", kBrokerAuth, "Read one discussion topic",
	                                                 {"project:string:required", "slug:string:required"}));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-post-list", "post.list", kBrokerAuth, "List posts in a topic",
	                                                 {"project:string:required", "topic:string:required"}));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-file-list", "file.list", kBrokerAuth, "List release files", project));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-file-get", "file.get", kBrokerAuth, "Read one release file",
	                                                 {"project:string:required", "name:string:required"}));
	cm.Add("privatehub", "PrivateHub", BrokerCommand("broker-repo-info", "repo.info", kBrokerAuth, "Read the project's repository facts", project));

	cm.Add("science", "Science", BrokerCommand("broker-papers-list", "list-papers", kBrokerAuth, "List papers in the corpus", {}));
	cm.Add("science", "Science", BrokerCommand("broker-paper-get", "get-paper", kBrokerAuth, "Read one paper's metadata",
	                                           {"id:string:required"}));
	cm.Add("science", "Science", BrokerCommand("broker-article-get", "get-article", kBrokerAuth, "Read one paper's article tree",
	                                           {"id:string:required"}));
	cm.Add("science", "Science", BrokerCommand("broker-article-write", "put-article", kBrokerAuth,
	                                           "Replace the article tree when the expected revision matches",
	                                           {"id:string:required", "expected_paper_revision:string:required",
	                                            "article:text:required"}));

	cm.Add("company", "Company", BrokerCommand("broker-proposal-draft", COMPANY_BROKER_CREATE_DRAFT, kBrokerAuth,
	                                           "Create a proposal draft for a repository",
	                                           {"title:string:required", "description:text", "repository:string:required",
	                                            "estimated_hours:number:required"}));
	cm.Add("company", "Company", BrokerCommand("broker-proposal-status", COMPANY_BROKER_STATUS, kBrokerAuth,
	                                           "Read a proposal's status", {"proposal_id:string:required"}));
}

// Host GitHub routes: the JSON API under api/gh. These run the host's own
// SystemAuth check (a signed-in host session), so auth states that.
void RegisterGitHubCommands()
{
	CommandManifest& cm = TheWebEngine().Manifest();
	const char *host_auth = "signed-in host session";
	const char *site = "github";
	const char *title = "GitHub";
	Vector<String> repo = {"owner:string:required", "repo:string:required"};

	cm.Add(site, title, HttpCommand("repos-list", "GET", "api/gh/repos/*", host_auth, "json",
	                                "List an owner's repositories", {"owner:string:required"}));
	cm.Add(site, title, HttpCommand("issues-list", "GET", "api/gh/issues/*/*", host_auth, "json",
	                                "List a repository's issues", repo));
	cm.Add(site, title, HttpCommand("pulls-list", "GET", "api/gh/pulls/*/*", host_auth, "json",
	                                "List a repository's pull requests", repo));
	cm.Add(site, title, HttpCommand("actions-list", "GET", "api/gh/actions/*/*", host_auth, "json",
	                                "List a repository's workflow runs", repo));
	cm.Add(site, title, HttpCommand("issue-detail", "GET", "api/gh/issue/*/*/*", host_auth, "json",
	                                "Read one issue with its comments",
	                                {"owner:string:required", "repo:string:required", "number:int:required"}));
	cm.Add(site, title, HttpCommand("pull-detail", "GET", "api/gh/pull/*/*/*", host_auth, "json",
	                                "Read one pull request with its comments",
	                                {"owner:string:required", "repo:string:required", "number:int:required"}));
	cm.Add(site, title, HttpCommand("issue-create", "POST", "api/gh/issues/*/create", host_auth, "json",
	                                "Open an issue", {"owner:string:required", "repo:string:required",
	                                                  "title:string:required", "body:text"}));
	cm.Add(site, title, HttpCommand("issue-comment", "POST", "api/gh/issues/*/comment", host_auth, "json",
	                                "Comment on an issue", {"owner:string:required", "repo:string:required",
	                                                        "number:int:required", "body:text:required"}));
	cm.Add(site, title, HttpCommand("issue-toggle", "POST", "api/gh/issues/*/toggle", host_auth, "json",
	                                "Close or reopen an issue", {"owner:string:required", "repo:string:required",
	                                                             "number:int:required", "close:bool:required"}));
	cm.Add(site, title, HttpCommand("pull-create", "POST", "api/gh/pulls/*/create", host_auth, "json",
	                                "Open a pull request", {"owner:string:required", "repo:string:required",
	                                                        "title:string:required", "body:text", "head:string:required",
	                                                        "base:string:required"}));
	cm.Add(site, title, HttpCommand("pull-merge", "POST", "api/gh/pulls/*/merge", host_auth, "json",
	                                "Merge a pull request", {"owner:string:required", "repo:string:required",
	                                                         "number:int:required"}));
	cm.Add(site, title, HttpCommand("pull-close", "POST", "api/gh/pulls/*/close", host_auth, "json",
	                                "Close a pull request without merging", {"owner:string:required",
	                                                                         "repo:string:required", "number:int:required"}));
	cm.Add(site, title, HttpCommand("pull-comment", "POST", "api/gh/pulls/*/comment", host_auth, "json",
	                                "Comment on a pull request", {"owner:string:required", "repo:string:required",
	                                                              "number:int:required", "body:text:required"}));
	cm.Add(site, title, HttpCommand("workflow-save", "POST", "api/gh/workflows/save", host_auth, "json",
	                                "Save a workflow file", {"owner:string:required", "repo:string:required",
	                                                         "filename:string:required", "content:text:required"}));
	cm.Add(site, title, HttpCommand("actions-trigger", "POST", "api/gh/actions/trigger", host_auth, "json",
	                                "Start a local build (not a GitHub Actions run)", {}));
}

SKYLARK(ApiCliManifest, "api/cli/manifest")
{
	http.ContentType("application/json") << AsJSON(TheWebEngine().Manifest().Describe(), true);
}

SKYLARK(ApiCliManifestSite, "api/cli/manifest/*")
{
	String site = http[0];
	if(!TheWebEngine().Manifest().HasSite(site)) {
		ValueMap err;
		err.Set("error", "unknown_site");
		err.Set("site", site);
		http.Response(404, "Not found");
		http.ContentType("application/json") << AsJSON(err);
		return;
	}
	http.ContentType("application/json") << AsJSON(TheWebEngine().Manifest().Describe(site), true);
}

END_UPP_NAMESPACE
