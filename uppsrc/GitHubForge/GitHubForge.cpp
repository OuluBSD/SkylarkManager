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
	return iss;
}

ForgeComment GitHubForgeBackend::CommentFromJson(const ValueMap& m)
{
	ForgeComment c;
	c.comment_id = m.Get("id").IsNull() ? String() : AsString((int64)m.Get("id"));
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

END_UPP_NAMESPACE
