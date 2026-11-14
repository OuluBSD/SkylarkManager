#include "GitHubPlatform.h"
#include "GitHubForgeHost.h"
#include <GitHubForge/GitHubForge.h>

NAMESPACE_UPP

SKYLARK(GitHubShortcut, "gh")
{
	http.Redirect("/repo/SeppoPakonen");
}

SKYLARK(GitHubReposShortcut, "gh/repos")
{
	http.Redirect("/repo/SeppoPakonen?tab=repositories");
}

SKYLARK(UserHomeSeppo, "SeppoPakonen")
{
	RenderUserProfile(http, "SeppoPakonen");
}

SKYLARK(UserGeneric, "user/*")
{
	String username = http[0];
	RenderUserProfile(http, username);
}

SKYLARK(RepoUserDirect, "repo/*")
{
	String username = http[0];
	RenderUserProfile(http, username);
}

SKYLARK(RepoPlatform, "repo/*/*")
{
	String owner = http[0];
	String repo = http[1];
	RenderRepoView(http, owner, repo, "");
}

SKYLARK(RepoPlatformDeep, "repo/*/*/**")
{
	String owner = http[0];
	String repo = http[1];
	String subpath;
	for(int i = 2; i < http.GetParamCount(); i++) {
		if(!subpath.IsEmpty()) subpath << "/";
		subpath << http[i];
	}
	RenderRepoView(http, owner, repo, subpath);
}

SKYLARK(RepoRawFile, "raw/*/*/**")
{
	String owner = http[0];
	String repo = http[1];
	String filepath;
	for(int i = 2; i < http.GetParamCount(); i++) {
		if(!filepath.IsEmpty()) filepath << "/";
		filepath << http[i];
	}
	LocalRepoInfo info = GitHubPlatformService::GetRepo(owner, repo);
	ValueMap blob = GitHubPlatformService::GetBlob(info.path, filepath);
	if(!blob.IsEmpty()) {
		http.ContentType("text/plain; charset=utf-8") << (String)blob["content"];
	} else {
		http.Response(404, "File Not Found") << "File not found";
	}
}

SKYLARK(ApiGitHubLiveRepos, "api/gh/repos/*")
{
	String owner = http[0];
	Vector<LocalRepoInfo> repos = GitHubPlatformService::GetLiveGitHubRepos(owner);
	ValueArray va;
	for(int i = 0; i < repos.GetCount(); i++)
		va.Add(StoreAsJsonValue(repos[i]));
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiGitHubLiveIssues, "api/gh/issues/*/*")
{
	String owner = http[0];
	String repo = http[1];
	Vector<ValueMap> issues = GitHubPlatformService::GetLiveIssues(owner, repo);
	ValueArray va;
	for(int i = 0; i < issues.GetCount(); i++)
		va.Add(issues[i]);
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiGitHubLivePRs, "api/gh/pulls/*/*")
{
	String owner = http[0];
	String repo = http[1];
	Vector<ValueMap> prs = GitHubPlatformService::GetLivePullRequests(owner, repo);
	ValueArray va;
	for(int i = 0; i < prs.GetCount(); i++)
		va.Add(prs[i]);
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiCreateIssue, "api/gh/issues/*/create:POST_RAW")
{
	String owner = http[0];
	Value json = ParseJSON(http.GetRequestContent());
	String repo = json["repo"];
	String title = json["title"];
	String body = json["body"];
	String res = GitHubPlatformService::CreateIssue(owner, repo, title, body);
	ValueMap out;
	out.Set("status", (res.StartsWith("http") || res.Find("Created") >= 0) ? "ok" : "error");
	out.Set("url", res);
	http.ContentType("application/json") << AsJSON(out);
}

SKYLARK(ApiCommentIssue, "api/gh/issues/*/comment:POST_RAW")
{
	String owner = http[0];
	Value json = ParseJSON(http.GetRequestContent());
	String repo = json["repo"];
	int number = (int)json["number"];
	String body = json["body"];
	String res = GitHubPlatformService::AddComment(owner, repo, number, body);
	ValueMap out;
	out.Set("status", res.StartsWith("http") ? "ok" : "error");
	out.Set("url", res);
	http.ContentType("application/json") << AsJSON(out);
}

SKYLARK(ApiToggleIssue, "api/gh/issues/*/toggle:POST_RAW")
{
	String owner = http[0];
	Value json = ParseJSON(http.GetRequestContent());
	String repo = json["repo"];
	int number = (int)json["number"];
	bool close_it = (bool)json["close"];
	String res = GitHubPlatformService::ToggleIssueState(owner, repo, number, close_it);
	ValueMap out;
	out.Set("status", "ok");
	out.Set("result", res);
	http.ContentType("application/json") << AsJSON(out);
}

SKYLARK(ApiGitHubLiveActions, "api/gh/actions/*/*")
{
	String owner = http[0];
	String repo = http[1];
	Vector<ValueMap> acts = GitHubPlatformService::GetLiveActions(owner, repo);
	ValueArray va;
	for(int i = 0; i < acts.GetCount(); i++)
		va.Add(acts[i]);
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiGitHubLiveIssueDetail, "api/gh/issue/*/*/*")
{
	String owner = http[0];
	String repo = http[1];
	int number = ScanInt(http[2]);
	ValueMap detail = GitHubPlatformService::GetLiveIssueDetail(owner, repo, number);
	http.ContentType("application/json") << AsJSON(detail);
}

SKYLARK(ApiCreatePullRequest, "api/gh/pulls/*/create:POST_RAW")
{
	String owner = http[0];
	Value json = ParseJSON(http.GetRequestContent());
	String repo = json["repo"];
	String title = json["title"];
	String body = json["body"];
	String head = json["head"];
	String base = json["base"];
	String res = GitHubPlatformService::CreatePullRequest(owner, repo, title, body, head, base);
	ValueMap out;
	bool failed = res.StartsWith("error:");
	out.Set("status", failed ? "error" : "ok");
	out.Set("url", res);
	if(!failed) {
		int last_slash = res.ReverseFind('/');
		if(last_slash >= 0 && IsDigit(res[last_slash + 1]))
			out.Set("number", ScanInt(res.Mid(last_slash + 1)));
	}
	http.ContentType("application/json") << AsJSON(out);
}

SKYLARK(ApiMergePullRequest, "api/gh/pulls/*/merge:POST_RAW")
{
	String owner = http[0];
	Value json = ParseJSON(http.GetRequestContent());
	String repo = json["repo"];
	int number = (int)json["number"];
	String res = GitHubPlatformService::MergePullRequest(owner, repo, number);
	ValueMap out;
	out.Set("status", res.StartsWith("error:") ? "error" : "ok");
	out.Set("result", res);
	http.ContentType("application/json") << AsJSON(out);
}

SKYLARK(ApiClosePullRequest, "api/gh/pulls/*/close:POST_RAW")
{
	String owner = http[0];
	Value json = ParseJSON(http.GetRequestContent());
	String repo = json["repo"];
	int number = (int)json["number"];
	String res = GitHubPlatformService::ClosePullRequest(owner, repo, number);
	ValueMap out;
	out.Set("status", res.StartsWith("error:") ? "error" : "ok");
	out.Set("result", res);
	http.ContentType("application/json") << AsJSON(out);
}

SKYLARK(ApiCommentPullRequest, "api/gh/pulls/*/comment:POST_RAW")
{
	String owner = http[0];
	Value json = ParseJSON(http.GetRequestContent());
	String repo = json["repo"];
	int number = (int)json["number"];
	String body = json["body"];
	String res = GitHubPlatformService::AddPullRequestComment(owner, repo, number, body);
	ValueMap out;
	out.Set("status", res.StartsWith("error:") ? "error" : "ok");
	out.Set("result", res);
	http.ContentType("application/json") << AsJSON(out);
}

SKYLARK(ApiGitHubLivePRDetail, "api/gh/pull/*/*/*")
{
	String owner = http[0];
	String repo = http[1];
	int number = ScanInt(http[2]);
	ValueMap detail = GitHubPlatformService::GetLivePullRequestDetail(owner, repo, number);
	http.ContentType("application/json") << AsJSON(detail);
}

// Creates a repository on the configured GitHub implementation. Creating a
// repository is administrative: only an admin session may call it.
SKYLARK(ApiCreateRepository, "api/gh/repos/create:POST_RAW")
{
	if(!GetGitHubForgeHost().is_admin(http)) {
		http.Content("application/json; charset=utf-8", "{\"error\":\"admin_required\"}");
		http.Response(403, "Forbidden");
		return;
	}
	Value json = ParseJSON(http.GetRequestContent());
	String owner = json["owner"];
	String name = json["name"];
	String visibility = json["visibility"];

	ValueMap out;
	out.Set("repository", owner + "/" + name);
	if(visibility != "public" && visibility != "private") {
		out.Set("status", "error");
		out.Set("message", "visibility must be public or private");
		http.ContentType("application/json") << AsJSON(out);
		return;
	}
	One<GitHubForgeBackend> backend = GitHubForgeBackend::Create();
	ForgeRepo repo;
	ForgeResult res = backend->CreateRepository(owner, name, visibility == "private", repo);
	out.Set("status", res.ok ? String("ok") : String("error"));
	out.Set("message", res.ok ? String() : res.error);
	http.ContentType("application/json") << AsJSON(out);
}

SKYLARK(ApiSaveWorkflow, "api/gh/workflows/save:POST_RAW")
{
	Value json = ParseJSON(http.GetRequestContent());
	String owner = json["owner"];
	String repo = json["repo"];
	String filename = json["filename"];
	String content = json["content"];

	String dev_dir = GitHubPlatformService::GetDevDir();
	String repo_path = AppendFileName(dev_dir, repo);
	if(!DirectoryExists(repo_path)) {
		repo_path = GetHomeDirFile("Dev/ai-upp");
	}

	bool ok = GitHubPlatformService::SaveWorkflowFile(repo_path, filename, content);
	ValueMap out;
	if(ok) {
		out.Set("status", "ok");
		out.Set("path", ".github/workflows/" + filename);
	} else {
		out.Set("status", "error");
		out.Set("message", "Failed to save workflow file to disk");
	}
	http.ContentType("application/json") << AsJSON(out);
}

SKYLARK(ApiTriggerAction, "api/gh/actions/trigger:POST_RAW")
{
	Value json = ParseJSON(http.GetRequestContent());
	GitHubPlatformService::TriggerLocalBuildAsync();

	ValueMap out;
	out.Set("status", "ok");
	out.Set("message", "Local build started. This is not a GitHub Actions run.");
	http.ContentType("application/json") << AsJSON(out);
}

SKYLARK(ApiTppPreview, "api/gh/tpp/preview")
{
	String rel_dir = http["dir"];
	if(rel_dir.IsEmpty()) rel_dir = "uppsrc/Core/srcdoc.tpp";
	String html = GitHubPlatformService::RenderTppDocSite(rel_dir);
	http.ContentType("text/html; charset=utf-8") << html;
}

END_UPP_NAMESPACE
