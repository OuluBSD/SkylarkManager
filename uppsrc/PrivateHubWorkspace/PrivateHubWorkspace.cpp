#include "PrivateHubWorkspace.h"

NAMESPACE_UPP

bool PrivateHubWorkspaceBackend::ValidName(const String& name)
{
	if(name.IsEmpty() || name.GetCount() > 100)
		return false;
	if(name == "." || name == "..")
		return false;
	for(int i = 0; i < name.GetCount(); i++) {
		int c = name[i];
		bool safe = IsAlpha(c) || IsDigit(c) || c == '_' || c == '-' || c == '.';
		if(!safe)
			return false;
	}
	return true;
}

PrivateHubWorkspaceBackend::PrivateHubWorkspaceBackend(const String& workspace_root_,
                                                       const String& issues_root_)
{
	ASSERT_(workspace_root_.GetCount(), "PrivateHubWorkspace needs a workspace root");
	workspace_root = NormalizePath(workspace_root_);
	RealizeDirectory(workspace_root);
	issues_root = issues_root_;
	caps.list_issues = true;
	caps.get_issue = true;
	caps.create_issue = true;
	caps.add_comment = true;
	caps.set_issue_state = true;
	caps.list_comments = true;
}

IRecordCollection& PrivateHubWorkspaceBackend::Issues(const String& project)
{
	// Store collection ids must be filesystem-safe: flatten "org/repo".
	String safe = project;
	safe.Replace("/", "__");
	return TheStoreManager().JsonCollection("privatehub", issues_root, "issues-" + safe);
}

bool PrivateHubWorkspaceBackend::IsContained(const String& rel) const
{
	String full = NormalizePath(AppendFileName(workspace_root, rel));
	return full.StartsWith(workspace_root) && full.GetCount() > workspace_root.GetCount();
}

String PrivateHubWorkspaceBackend::RepoPath(const String& org, const String& repo) const
{
	return AppendFileName(AppendFileName(workspace_root, org), repo);
}

static Value IssueToJson(ForgeIssue& iss)
{
	return StoreAsJsonValue(iss);
}

static Value CommentToJson(ForgeComment& c)
{
	return StoreAsJsonValue(c);
}

// git repository facts, executed argv-only (no shell).
static bool GitStatusPorcelain(const String& repo_path, String& branch, String& head,
                               int& dirty, String& error)
{
	LocalProcess p;
	if(!p.Start2("git", {"-C", repo_path, "rev-parse", "--abbrev-ref", "HEAD"})) {
		error = "cannot start git";
		return false;
	}
	p.CloseWrite();
	String out;
	uint32 t0 = GetTickCount();
	for(;;) {
		String co, ce;
		bool more = p.Read2(co, ce);
		out << co;
		if(!more)
			break;
		if(GetTickCount() - t0 > 20000) {
			p.Kill();
			error = "git timed out";
			return false;
		}
		Sleep(2);
	}
	branch = TrimBoth(out);

	LocalProcess p2;
	if(!p2.Start2("git", {"-C", repo_path, "rev-parse", "HEAD"})) {
		error = "cannot start git";
		return false;
	}
	p2.CloseWrite();
	out.Clear();
	t0 = GetTickCount();
	for(;;) {
		String co, ce;
		bool more = p2.Read2(co, ce);
		out << co;
		if(!more)
			break;
		if(GetTickCount() - t0 > 20000) {
			p2.Kill();
			error = "git timed out";
			return false;
		}
		Sleep(2);
	}
	head = TrimBoth(out);

	LocalProcess p3;
	if(!p3.Start2("git", {"-C", repo_path, "status", "--porcelain"})) {
		error = "cannot start git";
		return false;
	}
	p3.CloseWrite();
	out.Clear();
	t0 = GetTickCount();
	for(;;) {
		String co, ce;
		bool more = p3.Read2(co, ce);
		out << co;
		if(!more)
			break;
		if(GetTickCount() - t0 > 20000) {
			p3.Kill();
			error = "git timed out";
			return false;
		}
		Sleep(2);
	}
	dirty = 0;
	for(const String& line : Split(out, '\n'))
		if(TrimBoth(line).GetCount())
			dirty++;
	return true;
}

RepoInfo PrivateHubWorkspaceBackend::RepoStatus(const String& org, const String& repo) const
{
	RepoInfo ri;
	ri.org = org;
	ri.repo = repo;
	if(!ValidName(org) || !ValidName(repo)) {
		ri.error = "invalid org or repo name";
		return ri;
	}
	if(!IsContained(org + "/" + repo)) {
		ri.error = "path escapes workspace root";
		return ri;
	}
	ri.path = RepoPath(org, repo);
	ri.is_repo = DirectoryExists(AppendFileName(ri.path, ".git"));
	if(!ri.is_repo) {
		ri.error = "not a git repository";
		return ri;
	}
	String err;
	if(!GitStatusPorcelain(ri.path, ri.branch, ri.head_commit, ri.dirty_files, err)) {
		ri.error = err;
		ri.is_repo = false;
	}
	return ri;
}

bool PrivateHubWorkspaceBackend::ListOrganizations(Vector<String>& out) const
{
	out.Clear();
	for(const FindFile& ff : FindFile(workspace_root + "/*"))
		if(ff.IsDirectory() && ff.GetName() != "." && ff.GetName() != "..")
			out.Add(ff.GetName());
	Sort(out);
	return true;
}

bool PrivateHubWorkspaceBackend::ListRepositories(const String& org, Vector<String>& out) const
{
	out.Clear();
	if(!ValidName(org) || !IsContained(org + "/x"))
		return false;
	String orgdir = AppendFileName(workspace_root, org);
	if(!DirectoryExists(orgdir))
		return false;
	for(const FindFile& ff : FindFile(orgdir + "/*"))
		if(ff.IsDirectory() && DirectoryExists(AppendFileName(AppendFileName(orgdir, ff.GetName()), ".git")))
			out.Add(ff.GetName());
	Sort(out);
	return true;
}

// --- IForgeBackend over the site's JSON issue store ---

ForgeResult PrivateHubWorkspaceBackend::ListIssues(const String& project, Vector<ForgeIssue>& out)
{
	out.Clear();
	Vector<String> ids;
	StoreResult lr = Issues(project).List(ids);
	if(!lr.ok)
		return Failed("list-issues", project, lr.error);
	for(const String& id : ids) {
		String json, rev;
		if(!Issues(project).Get(id, json, rev).ok)
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			ForgeIssue iss;
			LoadFromJsonValue(iss, v);
			out.Add(iss);
		}
		catch(...) {}
	}
	return Ok("list-issues", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::GetIssue(const String& project, const String& issue_id, ForgeIssue& out)
{
	String json, rev;
	StoreResult gr = Issues(project).Get(issue_id, json, rev);
	if(!gr.ok)
		return Failed("get-issue", project, gr.error);
	try {
		Value v = ParseJSON(json);
		if(!v.Is<ValueMap>())
			return Failed("get-issue", project, "corrupt issue record");
		LoadFromJsonValue(out, v);
	}
	catch(Exc& e) {
		return Failed("get-issue", project, String("cannot parse issue: ") + e);
	}
	return Ok("get-issue", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out)
{
	if(draft.title.IsEmpty())
		return Failed("create-issue", project, "issue needs a title");
	if(draft.author.IsEmpty())
		return Failed("create-issue", project, "issue needs an authenticated author");
	String id = "phi_" + AsString(Uuid::Create());
	ForgeIssue iss = draft;
	iss.project = project;
	iss.issue_id = id;
	iss.open = true;
	iss.created = GetSysTime();
	iss.updated = iss.created;
	String new_rev;
	StoreResult pr = Issues(project).Put(id, AsJSON(IssueToJson(iss)), String(), new_rev);
	if(!pr.ok)
		return Failed("create-issue", project, pr.error);
	out = iss;
	return Ok("create-issue", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::AddComment(const String& project, const String& issue_id,
                                                   const ForgeComment& comment, ForgeComment& out)
{
	if(comment.body.IsEmpty())
		return Failed("add-comment", project, "comment needs a body");
	if(comment.author.IsEmpty())
		return Failed("add-comment", project, "comment needs an authenticated author");
	ForgeIssue got;
	ForgeResult gr = GetIssue(project, issue_id, got);
	if(!gr.ok)
		return gr;
	ForgeComment c = comment;
	c.issue_id = issue_id;
	c.comment_id = "phc_" + AsString(Uuid::Create());
	c.created = GetSysTime();
	VectorMap<String, String> kv; // comments stored as sibling records: "<issue>#<comment>"
	String new_rev;
	StoreResult pr = Issues(project).Put(issue_id + "." + c.comment_id,
		AsJSON(CommentToJson(c)), String(), new_rev);
	if(!pr.ok)
		return Failed("add-comment", project, pr.error);
	out = c;
	return Ok("add-comment", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::SetIssueState(const String& project, const String& issue_id,
                                                      bool open, ForgeIssue& out)
{
	ForgeIssue got;
	ForgeResult gr = GetIssue(project, issue_id, got);
	if(!gr.ok)
		return gr;
	got.open = open;
	got.updated = GetSysTime();
	String json, rev;
	Issues(project).Get(issue_id, json, rev);
	String new_rev;
	StoreResult pr = Issues(project).Put(issue_id, AsJSON(IssueToJson(got)), rev, new_rev);
	if(!pr.ok)
		return Failed("set-issue-state", project, pr.error);
	out = got;
	return Ok("set-issue-state", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::ListComments(const String& project, const String& issue_id,
                                                     Vector<ForgeComment>& out)
{
	out.Clear();
	ForgeIssue got;
	ForgeResult gr = GetIssue(project, issue_id, got);
	if(!gr.ok)
		return gr;
	Vector<String> ids;
	Issues(project).List(ids);
	String prefix = issue_id + ".";
	for(const String& id : ids) {
		if(!id.StartsWith(prefix))
			continue;
		String json, rev;
		if(!Issues(project).Get(id, json, rev).ok)
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			ForgeComment c;
			LoadFromJsonValue(c, v);
			out.Add(c);
		}
		catch(...) {}
	}
	return Ok("list-comments", project, ForgeProvenance::LocalPersistent);
}

END_UPP_NAMESPACE
