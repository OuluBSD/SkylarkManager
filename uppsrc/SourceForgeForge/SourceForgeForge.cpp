#include "SourceForgeForge.h"

NAMESPACE_UPP

SfHttpResponse SfHttpTransport::Request(const String& method, const String& path,
                                        const String& form_body)
{
	SfHttpResponse r;
	HttpRequest http(base_url + path);
	if(method == "POST")
		http.Post(form_body);
	http.Execute();
	r.status = http.GetStatusCode();
	r.body = http.GetContent();
	r.ok = r.status >= 200 && r.status < 300;
	if(!r.ok)
		r.error = Format("HTTP %d: ", r.status) + http.GetErrorDesc();
	return r;
}

SourceForgeForgeBackend::SourceForgeForgeBackend(const SfConfig& config, ISfTransport* transport_)
{
	cfg = config;
	transport = transport_;
	caps.list_issues = true;
	caps.get_issue = true;
	caps.list_comments = true;
	bool can_write = cfg.api_key.GetCount() && cfg.api_token.GetCount();
	caps.create_issue = can_write;
	caps.add_comment = can_write;
	caps.set_issue_state = can_write;
}

SourceForgeForgeBackend::SourceForgeForgeBackend(const SfConfig& config, const String& base_url)
	: SourceForgeForgeBackend(config, new SfHttpTransport(base_url))
{
}

bool SourceForgeForgeBackend::IsValidTicketId(const String& id)
{
	if(id.IsEmpty() || id.GetCount() > 12)
		return false;
	for(int i = 0; i < id.GetCount(); i++)
		if(!IsDigit(id[i]))
			return false;
	return true;
}

Time SourceForgeForgeBackend::ParseAlluraTime(const String& iso)
{
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

ForgeIssue SourceForgeForgeBackend::TicketFromJson(const ValueMap& t, const String& project)
{
	ForgeIssue iss;
	iss.project = project;
	iss.issue_id = t.Get("ticket_num").IsNull() ? String() : AsString((int)t.Get("ticket_num"));
	iss.title = t.Get("summary").IsNull() ? String() : (String)t.Get("summary");
	iss.body = t.Get("description").IsNull() ? String() : (String)t.Get("description");
	String status = t.Get("status").IsNull() ? String() : (String)t.Get("status");
	iss.open = !(status == "closed" || status == "fixed" || status == "wont-fix" || status == "invalid");
	if(!t.Get("reported_by").IsNull())
		iss.author = (String)t.Get("reported_by");
	iss.created = ParseAlluraTime(t.Get("ticket_date").IsNull() ? String() : (String)t.Get("ticket_date"));
	iss.updated = ParseAlluraTime(t.Get("mod_date").IsNull() ? String() : (String)t.Get("mod_date"));
	return iss;
}

void SourceForgeForgeBackend::ThreadPostsFromJson(const Value& thread, Vector<ForgeComment>& out,
                                                  const String& issue_id)
{
	out.Clear();
	if(!thread.Is<ValueMap>())
		return;
	Value posts = ((ValueMap)thread).Get("posts");
	if(!posts.Is<ValueArray>())
		return;
	for(const Value& pv : (ValueArray)posts) {
		if(!pv.Is<ValueMap>())
			continue;
		ValueMap m = pv;
		ForgeComment c;
		c.issue_id = issue_id;
		Value idv = m.Get("_id");
		if(!idv.IsNull())
			c.comment_id = idv.Is<String>() ? (String)idv : AsString(idv);
		Value body = m.Get("text");
		if(body.IsNull())
			body = m.Get("body");
		c.body = body.IsNull() ? String() : (String)body;
		Value author = m.Get("author");
		if(author.Is<ValueMap>() && !author["username"].IsNull())
			c.author = (String)author["username"];
		else if(!author.IsNull() && author.Is<String>())
			c.author = (String)author;
		c.created = ParseAlluraTime(m.Get("timestamp").IsNull()
			? (m.Get("posted_date").IsNull() ? String() : (String)m.Get("posted_date"))
			: (String)m.Get("timestamp"));
		out.Add(c);
	}
}

String SourceForgeForgeBackend::FormEncode(const VectorMap<String, String>& fields)
{
	String out;
	for(int i = 0; i < fields.GetCount(); i++) {
		if(i)
			out << '&';
		out << fields.GetKey(i) << '=' << UrlEncode(fields[i]);
	}
	return out;
}

ForgeResult SourceForgeForgeBackend::JsonFail(const char *op, const String& project, const String& error)
{
	return Failed(op, project, error);
}

ForgeResult SourceForgeForgeBackend::TransportFail(const char *op, const String& project,
                                                   const SfHttpResponse& res)
{
	return Failed(op, project, res.error.GetCount() ? res.error
	                                                : Format("HTTP %d", res.status));
}

ForgeResult SourceForgeForgeBackend::GateWrite(const char *op, const String& project)
{
	if(cfg.api_key.IsEmpty() || cfg.api_token.IsEmpty())
		return Failed(op, project, "sourceforge writes require api credentials");
	return Ok(op, project, ForgeProvenance::Remote);
}

ForgeResult SourceForgeForgeBackend::FetchTicketDoc(const String& project, const String& issue_id,
                                                    ValueMap& doc)
{
	SfHttpResponse res = transport->Request("GET",
		"/rest/p/" + project + "/" + cfg.tool_mount + "/" + issue_id + "/", "");
	if(!res.ok)
		return TransportFail("get-issue", project, res);
	Value v;
	try {
		v = ParseJSON(res.body);
	}
	catch(Exc& e) {
		return JsonFail("get-issue", project, String("bad JSON: ") + e);
	}
	if(!v.Is<ValueMap>())
		return JsonFail("get-issue", project, "allura did not return an object");
	doc = v;
	return Ok("get-ticket-doc", project, ForgeProvenance::Remote);
}

ForgeResult SourceForgeForgeBackend::ListIssues(const String& project, Vector<ForgeIssue>& out)
{
	out.Clear();
	if(project.IsEmpty() || project != cfg.project)
		return Failed("list-issues", project, "adapter is bound to project '" + cfg.project + "'");
	SfHttpResponse res = transport->Request("GET",
		"/rest/p/" + project + "/" + cfg.tool_mount + "/tickets?limit=100", "");
	if(!res.ok)
		return TransportFail("list-issues", project, res);
	Value v;
	try {
		v = ParseJSON(res.body);
	}
	catch(Exc& e) {
		return JsonFail("list-issues", project, String("bad JSON: ") + e);
	}
	if(!v.Is<ValueMap>())
		return JsonFail("list-issues", project, "allura did not return an object");
	Value tickets = ((ValueMap)v).Get("tickets");
	if(!tickets.Is<ValueArray>())
		return JsonFail("list-issues", project, "allura tickets array missing");
	for(const Value& tv : (ValueArray)tickets) {
		if(!tv.Is<ValueMap>())
			continue;
		out.Add(TicketFromJson(tv, project));
	}
	return Ok("list-issues", project, ForgeProvenance::Remote);
}

ForgeResult SourceForgeForgeBackend::GetIssue(const String& project, const String& issue_id, ForgeIssue& out)
{
	if(project.IsEmpty() || project != cfg.project)
		return Failed("get-issue", project, "adapter is bound to project '" + cfg.project + "'");
	if(!IsValidTicketId(issue_id))
		return Failed("get-issue", project, "invalid ticket id");
	ValueMap doc;
	ForgeResult fr = FetchTicketDoc(project, issue_id, doc);
	if(!fr.ok)
		return fr;
	Value ticket = doc.Get("ticket");
	if(!ticket.Is<ValueMap>())
		return JsonFail("get-issue", project, "allura ticket object missing");
	out = TicketFromJson(ticket, project);
	return Ok("get-issue", project, ForgeProvenance::Remote);
}

ForgeResult SourceForgeForgeBackend::CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out)
{
	if(project.IsEmpty() || project != cfg.project)
		return Failed("create-issue", project, "adapter is bound to project '" + cfg.project + "'");
	ForgeResult gate = GateWrite("create-issue", project);
	if(!gate.ok)
		return gate;
	if(draft.title.IsEmpty())
		return Failed("create-issue", project, "issue needs a title");
	VectorMap<String, String> form;
	form.Add("api_key", cfg.api_key);
	form.Add("api_token", cfg.api_token);
	form.Add("summary", draft.title);
	form.Add("description", draft.body);
	SfHttpResponse res = transport->Request("POST",
		"/rest/p/" + project + "/" + cfg.tool_mount + "/save_ticket", FormEncode(form));
	if(!res.ok)
		return TransportFail("create-issue", project, res);
	Value v;
	try {
		v = ParseJSON(res.body);
	}
	catch(Exc& e) {
		return JsonFail("create-issue", project, String("bad JSON: ") + e);
	}
	if(!v.Is<ValueMap>())
		return JsonFail("create-issue", project, "allura did not return an object");
	ValueMap doc = v;
	Value tv = doc.Get("ticket");
	int num = -1;
	if(tv.Is<ValueMap>() && IsNumber(((ValueMap)tv).Get("ticket_num")))
		num = (int)((ValueMap)tv).Get("ticket_num");
	else if(IsNumber(doc.Get("ticket_num")))
		num = (int)doc.Get("ticket_num");
	if(num <= 0)
		return Failed("create-issue", project, "created ticket id not reported by allura");
	out = draft;
	out.project = project;
	out.issue_id = AsString(num);
	out.author = cfg.author;
	out.created = GetSysTime();
	out.updated = out.created;
	out.open = true;
	return Ok("create-issue", project, ForgeProvenance::Remote);
}

ForgeResult SourceForgeForgeBackend::AddComment(const String& project, const String& issue_id,
                                                const ForgeComment& comment, ForgeComment& out)
{
	if(project.IsEmpty() || project != cfg.project)
		return Failed("add-comment", project, "adapter is bound to project '" + cfg.project + "'");
	ForgeResult gate = GateWrite("add-comment", project);
	if(!gate.ok)
		return gate;
	if(!IsValidTicketId(issue_id))
		return Failed("add-comment", project, "invalid ticket id");
	if(comment.body.IsEmpty())
		return Failed("add-comment", project, "comment needs a body");
	VectorMap<String, String> form;
	form.Add("api_key", cfg.api_key);
	form.Add("api_token", cfg.api_token);
	form.Add("text", comment.body);
	SfHttpResponse res = transport->Request("POST",
		"/rest/p/" + project + "/" + cfg.tool_mount + "/" + issue_id + "/save_comment",
		FormEncode(form));
	if(!res.ok)
		return TransportFail("add-comment", project, res);
	out = comment;
	out.issue_id = issue_id;
	out.author = cfg.author; // authoritative author, never request-supplied
	out.created = GetSysTime();
	return Ok("add-comment", project, ForgeProvenance::Remote);
}

ForgeResult SourceForgeForgeBackend::SetIssueState(const String& project, const String& issue_id,
                                                   bool open, ForgeIssue& out)
{
	if(project.IsEmpty() || project != cfg.project)
		return Failed("set-issue-state", project, "adapter is bound to project '" + cfg.project + "'");
	ForgeResult gate = GateWrite("set-issue-state", project);
	if(!gate.ok)
		return gate;
	if(!IsValidTicketId(issue_id))
		return Failed("set-issue-state", project, "invalid ticket id");
	// Allura: state changes are save_ticket updates carrying the new status.
	ValueMap doc;
	ForgeResult fr = FetchTicketDoc(project, issue_id, doc);
	if(!fr.ok)
		return fr;
	Value ticket = doc.Get("ticket");
	if(!ticket.Is<ValueMap>())
		return JsonFail("set-issue-state", project, "allura ticket object missing");
	ForgeIssue current = TicketFromJson(ticket, project);
	VectorMap<String, String> form;
	form.Add("api_key", cfg.api_key);
	form.Add("api_token", cfg.api_token);
	form.Add("ticket_num", issue_id);
	form.Add("summary", current.title);
	form.Add("description", current.body);
	form.Add("status", open ? String("open") : String("closed"));
	SfHttpResponse res = transport->Request("POST",
		"/rest/p/" + project + "/" + cfg.tool_mount + "/save_ticket", FormEncode(form));
	if(!res.ok)
		return TransportFail("set-issue-state", project, res);
	current.open = open;
	current.updated = GetSysTime();
	out = current;
	return Ok("set-issue-state", project, ForgeProvenance::Remote);
}

ForgeResult SourceForgeForgeBackend::ListComments(const String& project, const String& issue_id,
                                                  Vector<ForgeComment>& out)
{
	if(project.IsEmpty() || project != cfg.project)
		return Failed("list-comments", project, "adapter is bound to project '" + cfg.project + "'");
	if(!IsValidTicketId(issue_id))
		return Failed("list-comments", project, "invalid ticket id");
	ValueMap doc;
	ForgeResult fr = FetchTicketDoc(project, issue_id, doc);
	if(!fr.ok)
		return fr;
	ThreadPostsFromJson(doc.Get("thread"), out, issue_id);
	return Ok("list-comments", project, ForgeProvenance::Remote);
}

END_UPP_NAMESPACE
