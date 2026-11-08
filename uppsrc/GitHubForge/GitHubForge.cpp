#include "GitHubForge.h"

NAMESPACE_UPP

GitHubForgeBackend::GitHubForgeBackend(const String& gh_bin_)
{
	gh_bin = ResolveGhPath(gh_bin_);
	caps.list_issues = true;
	caps.get_issue = true;
	caps.create_issue = true;
	caps.add_comment = true;
	caps.set_issue_state = true;
	caps.list_comments = true;
}

String GitHubForgeBackend::ResolveGhPath(const String& configured)
{
	if(!configured.IsEmpty())
		return configured;
	String env = GetEnv("GHFORGE_GH");
	if(!env.IsEmpty())
		return env;
	return "gh";
}

bool GitHubForgeBackend::IsValidRepoSlug(const String& slug)
{
	if(slug.GetCount() < 3 || slug.GetCount() > 200)
		return false;
	Vector<String> parts = Split(slug, '/');
	if(parts.GetCount() != 2 || parts[0].IsEmpty() || parts[1].IsEmpty())
		return false;
	for(int i = 0; i < slug.GetCount(); i++) {
		int c = slug[i];
		bool safe = IsAlpha(c) || IsDigit(c) || c == '.' || c == '_' || c == '-' || c == '/';
		if(!safe)
			return false;
	}
	return true;
}

bool GitHubForgeBackend::IsValidIssueId(const String& issue_id)
{
	if(issue_id.IsEmpty() || issue_id.GetCount() > 18)
		return false;
	for(int i = 0; i < issue_id.GetCount(); i++)
		if(!IsDigit(issue_id[i]))
			return false;
	return true;
}

GitHubForgeBackend::GhOutcome GitHubForgeBackend::GhRun(const Vector<String>& args, int timeout_ms)
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

Time GitHubForgeBackend::ParseGhTime(const String& iso)
{
	// 2026-11-08T12:34:56Z (also tolerate +hh:mm offsets by ignoring them).
	if(iso.GetCount() < 19)
		return Time();
	int y = ScanInt(iso.Mid(0, 4));
	int mo = ScanInt(iso.Mid(5, 2));
	int d = ScanInt(iso.Mid(8, 2));
	int h = ScanInt(iso.Mid(11, 2));
	int mi = ScanInt(iso.Mid(14, 2));
	int s = ScanInt(iso.Mid(17, 2));
	if(y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31)
		return Time();
	return Time(y, mo, d, h, mi, s);
}

ForgeIssue GitHubForgeBackend::IssueFromJson(const ValueMap& m, const String& project)
{
	ForgeIssue iss;
	iss.project = project;
	iss.issue_id = AsString((int)m.Get("number"));
	iss.title = m.Get("title").IsNull() ? String() : (String)m.Get("title");
	iss.body = m.Get("body").IsNull() ? String() : (String)m.Get("body");
	Value user = m.Get("user");
	if(user.Is<ValueMap>() && !user["login"].IsNull())
		iss.author = (String)user["login"];
	iss.open = ((String)m.Get("state")) != "closed";
	iss.created = ParseGhTime(m.Get("created_at").IsNull() ? String() : (String)m.Get("created_at"));
	iss.updated = ParseGhTime(m.Get("updated_at").IsNull() ? String() : (String)m.Get("updated_at"));
	Value labels = m.Get("labels");
	if(labels.Is<ValueArray>())
		for(const Value& l : (ValueArray)labels)
			if(l.Is<ValueMap>() && !l["name"].IsNull())
				iss.labels.Add((String)l["name"]);
	if(!m.Get("comments").IsNull())
		iss.comment_count = (int)m.Get("comments");
	return iss;
}

ForgeComment GitHubForgeBackend::CommentFromJson(const ValueMap& m)
{
	ForgeComment c;
	c.comment_id = m.Get("id").IsNull() ? String() : AsString(m.Get("id"));
	c.author = String();
	Value user = m.Get("user");
	if(user.Is<ValueMap>() && !user["login"].IsNull())
		c.author = (String)user["login"];
	c.body = m.Get("body").IsNull() ? String() : (String)m.Get("body");
	c.created = ParseGhTime(m.Get("created_at").IsNull() ? String() : (String)m.Get("created_at"));
	return c;
}

static ForgeResult GhFail(const char *op, const String& project,
                          const GitHubForgeBackend::GhOutcome& gh)
{
	String err = gh.error;
	if(err.IsEmpty())
		err = gh.output.Mid(0, 500);
	return IForgeBackend::Failed(op, project, err);
}

ForgeResult GitHubForgeBackend::ListIssues(const String& project, Vector<ForgeIssue>& out)
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

ForgeResult GitHubForgeBackend::GetIssue(const String& project, const String& issue_id, ForgeIssue& out)
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

ForgeResult GitHubForgeBackend::CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out)
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

ForgeResult GitHubForgeBackend::AddComment(const String& project, const String& issue_id,
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

ForgeResult GitHubForgeBackend::SetIssueState(const String& project, const String& issue_id,
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

ForgeResult GitHubForgeBackend::ListComments(const String& project, const String& issue_id,
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

bool GitHubForgeBackend::IsValidLogin(const String& login)
{
	if(login.IsEmpty() || login.GetCount() > 39 || login[0] == '-')
		return false;
	for(int i = 0; i < login.GetCount(); i++) {
		int c = login[i];
		if(!(IsAlpha(c) || IsDigit(c) || c == '-'))
			return false;
	}
	return true;
}

bool GitHubForgeBackend::IsValidBranchName(const String& branch)
{
	if(branch.IsEmpty() || branch.GetCount() > 200 || branch[0] == '-')
		return false;
	for(int i = 0; i < branch.GetCount(); i++) {
		int c = branch[i];
		bool safe = IsAlpha(c) || IsDigit(c) || c == '.' || c == '_' || c == '-' || c == '/';
		if(!safe)
			return false;
	}
	return true;
}

static String Field(const ValueMap& m, const char *key)
{
	Value v = m.Get(key);
	return v.IsNull() ? String() : AsString(v);
}

// Reads one string from a nested object such as user.login or head.ref.
static String SubField(const Value& v, const char *key)
{
	if(!v.Is<ValueMap>())
		return String();
	ValueMap nested = v;
	return Field(nested, key);
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

ForgePullRequest GitHubForgeBackend::PullRequestFromJson(const ValueMap& m, const String& project)
{
	ForgePullRequest pr;
	pr.project = project;
	pr.pr_id = AsString((int)m.Get("number"));
	pr.title = Field(m, "title");
	pr.body = Field(m, "body");
	pr.author = SubField(m.Get("user"), "login");
	pr.created = ParseGhTime(Field(m, "created_at"));
	pr.head = SubField(m.Get("head"), "ref");
	pr.base = SubField(m.Get("base"), "ref");
	pr.url = Field(m, "html_url");
	// REST reports merged pulls as "closed" with a merged_at timestamp.
	if(!Field(m, "merged_at").IsEmpty())
		pr.state = "merged";
	else
		pr.state = Field(m, "state") == "closed" ? String("closed") : String("open");
	return pr;
}

ForgeRepo GitHubForgeBackend::RepoFromJson(const ValueMap& m, const String& owner)
{
	ForgeRepo r;
	r.owner = owner;
	r.name = Field(m, "name");
	r.description = Field(m, "description");
	r.is_private = (bool)m.Get("isPrivate");
	r.stars = (int)m.Get("stargazerCount");
	r.forks = (int)m.Get("forkCount");
	r.language = SubField(m.Get("primaryLanguage"), "name");
	r.pushed_at = Field(m, "pushedAt");
	return r;
}

ForgeRun GitHubForgeBackend::RunFromJson(const ValueMap& m)
{
	ForgeRun r;
	r.run_id = (int64)m.Get("id");
	r.name = Field(m, "name");
	r.status = Field(m, "status");
	r.conclusion = Field(m, "conclusion");
	r.head_branch = Field(m, "head_branch");
	r.event = Field(m, "event");
	r.created_at = Field(m, "created_at");
	return r;
}

ForgeResult GitHubForgeBackend::ListPullRequests(const String& project, Vector<ForgePullRequest>& out)
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

ForgeResult GitHubForgeBackend::GetPullRequest(const String& project, const String& pr_id, ForgePullRequest& out)
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

ForgeResult GitHubForgeBackend::GetPullRequestDiff(const String& project, const String& pr_id, String& out)
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

ForgeResult GitHubForgeBackend::CreatePullRequest(const String& project, const String& title, const String& body,
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

ForgeResult GitHubForgeBackend::SetPullRequestState(const String& project, const String& pr_id, bool open, ForgePullRequest& out)
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

ForgeResult GitHubForgeBackend::MergePullRequest(const String& project, const String& pr_id, String& message)
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

ForgeResult GitHubForgeBackend::ListRepos(const String& owner, Vector<ForgeRepo>& out)
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

ForgeResult GitHubForgeBackend::ListRuns(const String& project, int limit, Vector<ForgeRun>& out)
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
