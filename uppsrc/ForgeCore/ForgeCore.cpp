#include "ForgeCore.h"

NAMESPACE_UPP

const char *ForgeBackendName(ForgeBackend b)
{
	switch(b) {
	case ForgeBackend::GitHub:       return "github";
	case ForgeBackend::SourceForge:  return "sourceforge";
	case ForgeBackend::PrivateHub:   return "privatehub";
	}
	return "unknown";
}

ForgeBackend ForgeBackendFromName(const String& name)
{
	if(name == "github")      return ForgeBackend::GitHub;
	if(name == "sourceforge") return ForgeBackend::SourceForge;
	return ForgeBackend::PrivateHub;
}

const char *ForgeProvenanceName(ForgeProvenance p)
{
	switch(p) {
	case ForgeProvenance::Remote:          return "remote";
	case ForgeProvenance::LocalPersistent: return "local";
	case ForgeProvenance::Snapshot:        return "snapshot";
	case ForgeProvenance::Synthetic:       return "synthetic";
	case ForgeProvenance::Unavailable:     return "unavailable";
	}
	return "unknown";
}

ForgeProvenance ForgeProvenanceFromName(const String& name)
{
	if(name == "remote")     return ForgeProvenance::Remote;
	if(name == "local")      return ForgeProvenance::LocalPersistent;
	if(name == "snapshot")   return ForgeProvenance::Snapshot;
	if(name == "synthetic")  return ForgeProvenance::Synthetic;
	return ForgeProvenance::Unavailable;
}

ForgeResult IForgeBackend::Unsupported(const char *op, const String& project)
{
	ForgeResult r;
	r.ok = false;
	r.op = op;
	r.ref.project = project;
	r.error = "operation not supported by this backend";
	r.provenance = ForgeProvenance::Unavailable;
	return r;
}

ForgeResult IForgeBackend::Failed(const char *op, const String& project, const String& error)
{
	ForgeResult r;
	r.ok = false;
	r.op = op;
	r.ref.project = project;
	r.error = error;
	r.provenance = ForgeProvenance::Unavailable;
	return r;
}

ForgeResult IForgeBackend::Ok(const char *op, const String& project, ForgeProvenance prov)
{
	ForgeResult r;
	r.ok = true;
	r.op = op;
	r.ref.project = project;
	r.provenance = prov;
	return r;
}

// ---------------------------------------------------------------------------
// MemoryForgeBackend
// ---------------------------------------------------------------------------

MemoryForgeBackend::MemoryForgeBackend(ForgeBackend backend_, ForgeProvenance provenance_)
	: backend(backend_), provenance(provenance_)
{
	caps.list_issues = true;
	caps.get_issue = true;
	caps.create_issue = true;
	caps.add_comment = true;
	caps.set_issue_state = true;
	caps.list_comments = true;
}

static String IssueKey(const String& project, const String& issue_id)
{
	return project + "\x01" + issue_id;
}

static String CommentKey(const String& project, const String& issue_id, const String& comment_id)
{
	return project + "\x01" + issue_id + "\x01" + comment_id;
}

ForgeResult MemoryForgeBackend::ListIssues(const String& project, Vector<ForgeIssue>& out)
{
	mutex.Enter();
	out.Clear();
	for(int i = 0; i < issues.GetCount(); i++) {
		const ForgeIssue& iss = issues[i];
		if(iss.project == project)
			out.Add(iss);
	}
	mutex.Leave();
	return Ok("list-issues", project, provenance);
}

ForgeResult MemoryForgeBackend::GetIssue(const String& project, const String& issue_id, ForgeIssue& out)
{
	mutex.Enter();
	int q = issues.Find(IssueKey(project, issue_id));
	if(q < 0) {
		mutex.Leave();
		return Failed("get-issue", project, "issue not found: " + issue_id);
	}
	out = issues[q];
	mutex.Leave();
	return Ok("get-issue", project, provenance);
}

ForgeResult MemoryForgeBackend::CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out)
{
	if(draft.title.IsEmpty())
		return Failed("create-issue", project, "issue needs a title");
	mutex.Enter();
	ForgeIssue iss = draft;
	iss.project = project;
	iss.issue_id = AsString(next_issue++);
	iss.created = GetSysTime();
	iss.updated = iss.created;
	issues.Add(IssueKey(project, iss.issue_id), iss);
	out = iss;
	mutex.Leave();
	return Ok("create-issue", project, provenance);
}

ForgeResult MemoryForgeBackend::AddComment(const String& project, const String& issue_id,
                                           const ForgeComment& comment, ForgeComment& out)
{
	{
		mutex.Enter();
		bool exists = issues.Find(IssueKey(project, issue_id)) >= 0;
		mutex.Leave();
		if(!exists)
			return Failed("add-comment", project, "issue not found: " + issue_id);
	}
	if(comment.body.IsEmpty())
		return Failed("add-comment", project, "comment needs a body");
	ForgeComment c = comment;
	c.issue_id = issue_id;
	c.comment_id = AsString(next_comment++);
	c.created = GetSysTime();
	mutex.Enter();
	comments.Add(CommentKey(project, issue_id, c.comment_id), c);
	mutex.Leave();
	out = c;
	return Ok("add-comment", project, provenance);
}

ForgeResult MemoryForgeBackend::SetIssueState(const String& project, const String& issue_id,
                                              bool open, ForgeIssue& out)
{
	mutex.Enter();
	int q = issues.Find(IssueKey(project, issue_id));
	if(q < 0) {
		mutex.Leave();
		return Failed("set-issue-state", project, "issue not found: " + issue_id);
	}
	ForgeIssue iss = issues[q];
	iss.open = open;
	iss.updated = GetSysTime();
	issues[q] = iss;
	out = iss;
	mutex.Leave();
	return Ok("set-issue-state", project, provenance);
}

ForgeResult MemoryForgeBackend::ListComments(const String& project, const String& issue_id,
                                             Vector<ForgeComment>& out)
{
	mutex.Enter();
	out.Clear();
	String prefix = project + "\x01" + issue_id + "\x01";
	for(int i = 0; i < comments.GetCount(); i++)
		if(comments.GetKey(i).StartsWith(prefix))
			out.Add(comments[i]);
	mutex.Leave();
	return Ok("list-comments", project, provenance);
}

END_UPP_NAMESPACE
