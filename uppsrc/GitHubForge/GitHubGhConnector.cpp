#include "GitHubGhConnector.h"

NAMESPACE_UPP

GitHubGhConnector::GitHubGhConnector(const String& gh_bin_)
{
	gh_bin = ResolveGhPath(gh_bin_);
	caps.list_issues = true;
	caps.get_issue = true;
	caps.create_issue = true;
	caps.add_comment = true;
	caps.set_issue_state = true;
	caps.list_comments = true;
}

String GitHubGhConnector::ResolveGhPath(const String& configured)
{
	if(!configured.IsEmpty())
		return configured;
	String env = GetEnv("GHFORGE_GH");
	if(!env.IsEmpty())
		return env;
	return "gh";
}

GitHubGhConnector::GhOutcome GitHubGhConnector::GhRun(const Vector<String>& args, int timeout_ms)
{
	GhOutcome r;
	for(int i = 0; i < args.GetCount(); i++) {
		if(i)
			r.command_summary << " ";
		r.command_summary << "'" << args[i] << "'";
	}
	LocalProcess p;
	if(!p.Start2(gh_bin, args)) {
		r.error = "cannot start gh executable: " + gh_bin;
		return r;
	}
	p.CloseWrite();

	// Bounded non-blocking read loop (Read2 polls with zero timeout).
	String out, errout;
	uint32 start = GetTickCount();
	for(;;) {
		String co, ce;
		bool more = p.Read2(co, ce);
		out << co;
		errout << ce;
		if(!more)
			break;
		if(GetTickCount() - start > (uint32)timeout_ms) {
			p.Kill();
			r.output = out;
			r.error = "gh invocation timed out: " + r.command_summary;
			return r;
		}
		Sleep(5);
	}
	r.exit_code = p.GetExitCode();
	r.output = out;
	r.error = errout;
	if(r.output.GetCount() > 4 * 1024 * 1024)
		r.output = r.output.Left(4 * 1024 * 1024);
	r.ok = r.exit_code == 0;
	return r;
}

static ForgeResult GhFail(const char *op, const String& project,
                          const GitHubGhConnector::GhOutcome& gh)
{
	String err = gh.error;
	if(err.IsEmpty())
		err = gh.output.Mid(0, 500);
	return IForgeBackend::Failed(op, project, err);
}

ForgeResult GitHubGhConnector::ListIssues(const String& project, Vector<ForgeIssue>& out)
{
	out.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("list-issues", project, "invalid repository slug");
	GhOutcome gh = GhRun({"api", "repos/" + project + "/issues?state=all&per_page=100"});
	if(!gh.ok)
		return GhFail("list-issues", project, gh);
	Value v;
	try {
		v = ParseJSON(gh.output);
	}
	catch(Exc& e) {
		return Failed("list-issues", project, String("bad JSON from gh: ") + e);
	}
	if(!v.Is<ValueArray>())
		return Failed("list-issues", project, "gh did not return an array");
	const ValueArray& arr = v;
	for(int i = 0; i < arr.GetCount(); i++) {
		if(!arr[i].Is<ValueMap>())
			continue;
		ValueMap m = arr[i];
		if(m.Find("pull_request") >= 0)
			continue; // pull requests are not issues
		out.Add(IssueFromJson(m, project));
	}
	return Ok("list-issues", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::GetIssue(const String& project, const String& issue_id, ForgeIssue& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("get-issue", project, "invalid repository slug");
	if(!IsValidIssueId(issue_id))
		return Failed("get-issue", project, "invalid issue id");
	GhOutcome gh = GhRun({"api", "repos/" + project + "/issues/" + issue_id});
	if(!gh.ok)
		return GhFail("get-issue", project, gh);
	Value v;
	try {
		v = ParseJSON(gh.output);
	}
	catch(Exc& e) {
		return Failed("get-issue", project, String("bad JSON from gh: ") + e);
	}
	if(!v.Is<ValueMap>())
		return Failed("get-issue", project, "gh did not return an object");
	out = IssueFromJson(v, project);
	return Ok("get-issue", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("create-issue", project, "invalid repository slug");
	if(draft.title.IsEmpty())
		return Failed("create-issue", project, "issue needs a title");
	Vector<String> args;
	args.Add("api");
	args.Add("-X");
	args.Add("POST");
	args.Add("repos/" + project + "/issues");
	args.Add("-f");
	args.Add("title=" + draft.title);
	args.Add("-f");
	args.Add("body=" + draft.body);
	GhOutcome gh = GhRun(args);
	if(!gh.ok)
		return GhFail("create-issue", project, gh);
	Value v;
	try {
		v = ParseJSON(gh.output);
	}
	catch(Exc& e) {
		return Failed("create-issue", project, String("bad JSON from gh: ") + e);
	}
	if(!v.Is<ValueMap>())
		return Failed("create-issue", project, "gh did not return an object");
	out = IssueFromJson(v, project);
	return Ok("create-issue", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::AddComment(const String& project, const String& issue_id,
                                           const ForgeComment& comment, ForgeComment& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("add-comment", project, "invalid repository slug");
	if(!IsValidIssueId(issue_id))
		return Failed("add-comment", project, "invalid issue id");
	if(comment.body.IsEmpty())
		return Failed("add-comment", project, "comment needs a body");
	Vector<String> args;
	args.Add("api");
	args.Add("-X");
	args.Add("POST");
	args.Add("repos/" + project + "/issues/" + issue_id + "/comments");
	args.Add("-f");
	args.Add("body=" + comment.body);
	GhOutcome gh = GhRun(args);
	if(!gh.ok)
		return GhFail("add-comment", project, gh);
	Value v;
	try {
		v = ParseJSON(gh.output);
	}
	catch(Exc& e) {
		return Failed("add-comment", project, String("bad JSON from gh: ") + e);
	}
	if(!v.Is<ValueMap>())
		return Failed("add-comment", project, "gh did not return an object");
	out = CommentFromJson(v);
	out.issue_id = issue_id;
	return Ok("add-comment", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::SetIssueState(const String& project, const String& issue_id,
                                              bool open, ForgeIssue& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("set-issue-state", project, "invalid repository slug");
	if(!IsValidIssueId(issue_id))
		return Failed("set-issue-state", project, "invalid issue id");
	Vector<String> args;
	args.Add("api");
	args.Add("-X");
	args.Add("PATCH");
	args.Add("repos/" + project + "/issues/" + issue_id);
	args.Add("-f");
	args.Add(open ? String("state=open") : String("state=closed"));
	GhOutcome gh = GhRun(args);
	if(!gh.ok)
		return GhFail("set-issue-state", project, gh);
	Value v;
	try {
		v = ParseJSON(gh.output);
	}
	catch(Exc& e) {
		return Failed("set-issue-state", project, String("bad JSON from gh: ") + e);
	}
	if(!v.Is<ValueMap>())
		return Failed("set-issue-state", project, "gh did not return an object");
	out = IssueFromJson(v, project);
	return Ok("set-issue-state", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::ListComments(const String& project, const String& issue_id,
                                             Vector<ForgeComment>& out)
{
	out.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("list-comments", project, "invalid repository slug");
	if(!IsValidIssueId(issue_id))
		return Failed("list-comments", project, "invalid issue id");
	GhOutcome gh = GhRun({"api", "repos/" + project + "/issues/" + issue_id + "/comments?per_page=100"});
	if(!gh.ok)
		return GhFail("list-comments", project, gh);
	Value v;
	try {
		v = ParseJSON(gh.output);
	}
	catch(Exc& e) {
		return Failed("list-comments", project, String("bad JSON from gh: ") + e);
	}
	if(!v.Is<ValueArray>())
		return Failed("list-comments", project, "gh did not return an array");
	const ValueArray& arr = v;
	for(int i = 0; i < arr.GetCount(); i++)
		if(arr[i].Is<ValueMap>()) {
			ForgeComment c = CommentFromJson(arr[i]);
			c.issue_id = issue_id;
			out.Add(c);
		}
	return Ok("list-comments", project, ForgeProvenance::Remote);
}

static bool ParseGhJson(const String& text, Value& v, String& error)
{
	try {
		v = ParseJSON(text);
	}
	catch(Exc& e) {
		error = String("bad JSON from gh: ") + e;
		return false;
	}
	return true;
}

ForgeResult GitHubGhConnector::ListPullRequests(const String& project, Vector<ForgePullRequest>& out)
{
	out.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("list-pulls", project, "invalid repository slug");
	GhOutcome gh = GhRun({"api", "repos/" + project + "/pulls?state=all&per_page=20"});
	if(!gh.ok)
		return GhFail("list-pulls", project, gh);
	Value v;
	String error;
	if(!ParseGhJson(gh.output, v, error))
		return Failed("list-pulls", project, error);
	if(!v.Is<ValueArray>())
		return Failed("list-pulls", project, "gh did not return an array");
	const ValueArray& arr = v;
	for(int i = 0; i < arr.GetCount(); i++)
		if(arr[i].Is<ValueMap>()) {
			ValueMap m = arr[i];
			out.Add(PullRequestFromJson(m, project));
		}
	return Ok("list-pulls", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::GetPullRequest(const String& project, const String& pr_id, ForgePullRequest& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("get-pull", project, "invalid repository slug");
	if(!IsValidIssueId(pr_id))
		return Failed("get-pull", project, "invalid pull request number");
	GhOutcome gh = GhRun({"api", "repos/" + project + "/pulls/" + pr_id});
	if(!gh.ok)
		return GhFail("get-pull", project, gh);
	Value v;
	String error;
	if(!ParseGhJson(gh.output, v, error))
		return Failed("get-pull", project, error);
	if(!v.Is<ValueMap>())
		return Failed("get-pull", project, "gh did not return an object");
	ValueMap m = v;
	out = PullRequestFromJson(m, project);
	return Ok("get-pull", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::GetPullRequestDiff(const String& project, const String& pr_id, String& out)
{
	out.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("pull-diff", project, "invalid repository slug");
	if(!IsValidIssueId(pr_id))
		return Failed("pull-diff", project, "invalid pull request number");
	GhOutcome gh = GhRun({"pr", "diff", pr_id, "-R", project});
	if(!gh.ok)
		return GhFail("pull-diff", project, gh);
	out = gh.output;
	return Ok("pull-diff", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::CreatePullRequest(const String& project, const String& title, const String& body,
                                                  const String& head, const String& base, ForgePullRequest& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("create-pull", project, "invalid repository slug");
	if(title.IsEmpty())
		return Failed("create-pull", project, "pull request needs a title");
	if(!IsValidBranchName(head))
		return Failed("create-pull", project, "invalid head branch");
	if(!IsValidBranchName(base))
		return Failed("create-pull", project, "invalid base branch");
	Vector<String> args;
	args.Add("api");
	args.Add("-X");
	args.Add("POST");
	args.Add("repos/" + project + "/pulls");
	args.Add("-f");
	args.Add("title=" + title);
	args.Add("-f");
	args.Add("head=" + head);
	args.Add("-f");
	args.Add("base=" + base);
	args.Add("-f");
	args.Add("body=" + body);
	GhOutcome gh = GhRun(args);
	if(!gh.ok)
		return GhFail("create-pull", project, gh);
	Value v;
	String error;
	if(!ParseGhJson(gh.output, v, error))
		return Failed("create-pull", project, error);
	if(!v.Is<ValueMap>())
		return Failed("create-pull", project, "gh did not return an object");
	ValueMap m = v;
	out = PullRequestFromJson(m, project);
	return Ok("create-pull", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::SetPullRequestState(const String& project, const String& pr_id, bool open, ForgePullRequest& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("set-pull-state", project, "invalid repository slug");
	if(!IsValidIssueId(pr_id))
		return Failed("set-pull-state", project, "invalid pull request number");
	Vector<String> args;
	args.Add("api");
	args.Add("-X");
	args.Add("PATCH");
	args.Add("repos/" + project + "/pulls/" + pr_id);
	args.Add("-f");
	args.Add(open ? String("state=open") : String("state=closed"));
	GhOutcome gh = GhRun(args);
	if(!gh.ok)
		return GhFail("set-pull-state", project, gh);
	Value v;
	String error;
	if(!ParseGhJson(gh.output, v, error))
		return Failed("set-pull-state", project, error);
	if(!v.Is<ValueMap>())
		return Failed("set-pull-state", project, "gh did not return an object");
	ValueMap m = v;
	out = PullRequestFromJson(m, project);
	return Ok("set-pull-state", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::MergePullRequest(const String& project, const String& pr_id, String& message)
{
	message.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("merge-pull", project, "invalid repository slug");
	if(!IsValidIssueId(pr_id))
		return Failed("merge-pull", project, "invalid pull request number");
	GhOutcome gh = GhRun({"api", "-X", "PUT", "repos/" + project + "/pulls/" + pr_id + "/merge", "-f", "merge_method=merge"});
	if(!gh.ok)
		return GhFail("merge-pull", project, gh);
	Value v;
	String error;
	if(!ParseGhJson(gh.output, v, error))
		return Failed("merge-pull", project, error);
	if(!v.Is<ValueMap>())
		return Failed("merge-pull", project, "gh did not return an object");
	ValueMap m = v;
	message = Field(m, "message");
	if(!(bool)m.Get("merged"))
		return Failed("merge-pull", project, message.IsEmpty() ? String("pull request was not merged") : message);
	return Ok("merge-pull", project, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::CreateRepository(const String& owner, const String& name, bool is_private, ForgeRepo& out)
{
	out = ForgeRepo();
	String slug = owner + "/" + name;
	if(!IsValidRepoSlug(slug))
		return Failed("create-repo", slug, "invalid repository name");
	GhOutcome gh = GhRun({"repo", "create", slug, is_private ? "--private" : "--public"});
	if(!gh.ok)
		return GhFail("create-repo", slug, gh);
	out.owner = owner;
	out.name = name;
	out.is_private = is_private;
	return Ok("create-repo", slug, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::ListRepos(const String& owner, Vector<ForgeRepo>& out)
{
	out.Clear();
	if(!IsValidLogin(owner))
		return Failed("list-repos", owner, "invalid owner login");
	GhOutcome gh = GhRun({"repo", "list", owner, "--limit", "100", "--json",
	                      "name,description,isPrivate,pushedAt,stargazerCount,forkCount,primaryLanguage,url"});
	if(!gh.ok)
		return GhFail("list-repos", owner, gh);
	Value v;
	String error;
	if(!ParseGhJson(gh.output, v, error))
		return Failed("list-repos", owner, error);
	if(!v.Is<ValueArray>())
		return Failed("list-repos", owner, "gh did not return an array");
	const ValueArray& arr = v;
	for(int i = 0; i < arr.GetCount(); i++)
		if(arr[i].Is<ValueMap>()) {
			ValueMap m = arr[i];
			out.Add(RepoFromJson(m, owner));
		}
	return Ok("list-repos", owner, ForgeProvenance::Remote);
}

ForgeResult GitHubGhConnector::ListRuns(const String& project, int limit, Vector<ForgeRun>& out)
{
	out.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("list-runs", project, "invalid repository slug");
	if(limit < 1)
		limit = 1;
	if(limit > 100)
		limit = 100;
	GhOutcome gh = GhRun({"api", "repos/" + project + "/actions/runs?per_page=" + AsString(limit)});
	if(!gh.ok)
		return GhFail("list-runs", project, gh);
	Value v;
	String error;
	if(!ParseGhJson(gh.output, v, error))
		return Failed("list-runs", project, error);
	if(!v.Is<ValueMap>())
		return Failed("list-runs", project, "gh did not return an object");
	ValueMap m = v;
	Value runs = m.Get("workflow_runs");
	if(!runs.Is<ValueArray>())
		return Failed("list-runs", project, "gh did not return workflow runs");
	const ValueArray& arr = runs;
	for(int i = 0; i < arr.GetCount(); i++)
		if(arr[i].Is<ValueMap>()) {
			ValueMap run = arr[i];
			out.Add(RunFromJson(run));
		}
	return Ok("list-runs", project, ForgeProvenance::Remote);
}

END_UPP_NAMESPACE
