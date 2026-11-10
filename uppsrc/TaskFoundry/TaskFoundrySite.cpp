#include "TaskFoundrySite.h"

NAMESPACE_UPP

// TaskFoundry engine site. Every page needs a session user, and every write
// goes through the engine's mutation fence (CSRF token on each form). Pages
// call the service, which does the scope checks and talks to other sites only
// through the broker.

static TaskFoundryService *s_service = NULL;

static TaskFoundryService& Service()
{
	ASSERT_(s_service, "taskfoundry site not installed");
	return *s_service;
}

static String Esc(const String& s)
{
	StringBuffer out;
	for(int i = 0; i < s.GetCount(); i++) {
		char c = s[i];
		switch(c) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(c);
		}
	}
	return String(out);
}

static Vector<Value> IdArgs(const String& id)
{
	Vector<Value> args;
	args << Value(id);
	return args;
}

static String Text(const ValueMap& m, const char *key)
{
	return (String)m.Get(key, "");
}

static TfActor Actor(WebRequest& r)
{
	TfActor a;
	a.user_id = r.User().user_id;
	a.username = r.User().username;
	a.scopes = TaskFoundryMemberScopes();
	return a;
}

// Returns false, after redirecting to the site login, for anonymous callers.
static bool RequireUser(WebRequest& r)
{
	if(r.HasUser())
		return true;
	r.Redirect(r.Site().Url("session/login"));
	return false;
}

static void Page(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\">"
	       "<title>" + Esc(title) + "</title></head><body>"
	       "<nav><a href=\"" + Esc(r.Site().Url("")) + "\">Board</a></nav>"
	       "<h1>" + Esc(title) + "</h1>" + body + "</body></html>");
}

static void Failed(WebRequest& r, const TfResult& res)
{
	r.Status(res.status, res.error);
	Page(r, "TaskFoundry", "<p>" + Esc(res.error) + "</p>");
}

static String Hidden(WebRequest& r)
{
	return "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + Esc(r.CsrfToken()) + "\">";
}

static String ListItem(const String& href, const String& label, const String& status)
{
	return "<li><a href=\"" + Esc(href) + "\">" + Esc(label) + "</a> <small>" + Esc(status) + "</small></li>";
}

static void Board(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	TfResult res = Service().Board();
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	StringBuffer b;
	b << "<h2>Candidates</h2><ul>";
	ValueArray cands = res.payload.Get("candidates", ValueArray());
	for(int i = 0; i < cands.GetCount(); i++) {
		ValueMap c = cands[i];
		b << ListItem(r.Site().Url("candidates/*", IdArgs(Text(c, "id"))), Text(c, "title"),
		              "paper " + Text(c, "paper_id"));
	}
	b << "</ul>"
	     "<form method=\"post\" action=\"" << Esc(r.Site().Url("candidates/create:POST")) << "\">"
	  << Hidden(r) <<
	     "<input name=\"title\" placeholder=\"Candidate title\" required>"
	     "<input name=\"paper_id\" placeholder=\"Science paper id\" required>"
	     "<textarea name=\"summary\" placeholder=\"Summary\"></textarea>"
	     "<button>Add candidate</button></form>";
	b << "<h2>Proposals</h2><ul>";
	ValueArray props = res.payload.Get("proposals", ValueArray());
	for(int i = 0; i < props.GetCount(); i++) {
		ValueMap p = props[i];
		b << ListItem(r.Site().Url("proposals/*", IdArgs(Text(p, "id"))), Text(p, "title"), Text(p, "status"));
	}
	b << "</ul>";
	Page(r, "TaskFoundry", String(b));
}

static void CandidateCreate(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	TfResult res = Service().CreateCandidate(Actor(r), r.Param("title"), r.Param("summary"), r.Param("paper_id"));
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	r.Redirect(r.Site().Url("candidates/*", IdArgs(Text(res.payload, "candidate_id"))));
}

static void CandidateDetail(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	String id = r.PathArg(0);
	TfResult res = Service().GetCandidate(id);
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	StringBuffer b;
	b << "<p>" << Esc(Text(res.payload, "summary")) << "</p>"
	  << "<p>Science paper <code>" << Esc(Text(res.payload, "paper_id")) << "</code>, revision "
	  << Esc(Text(res.payload, "paper_revision")) << "</p>"
	  << "<form method=\"post\" action=\"" << Esc(r.Site().Url("candidates/*/propose:POST", IdArgs(id))) << "\">"
	  << Hidden(r) << "<button>Start a proposal from this candidate</button></form>";
	Page(r, Text(res.payload, "title"), String(b));
}

static void CandidatePropose(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	TfResult res = Service().CreateProposal(Actor(r), r.PathArg(0));
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	r.Redirect(r.Site().Url("proposals/*", IdArgs(Text(res.payload, "proposal_id"))));
}

static String EvidenceRow(const ValueMap& e)
{
	if(Text(e, "kind") == "science")
		return "<li>science paper <code>" + Esc(Text(e, "paper_id")) + "</code> revision " +
		       Esc(Text(e, "paper_revision")) + " node <code>" + Esc(Text(e, "node_id")) + "</code></li>";
	return "<li>privatehub <code>" + Esc(Text(e, "project")) + "</code> branch " + Esc(Text(e, "branch")) +
	       " at <code>" + Esc(Text(e, "head_commit")) + "</code></li>";
}

static void ProposalEditor(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	String id = r.PathArg(0);
	TfResult res = Service().GetProposal(id);
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	const ValueMap& p = res.payload;
	StringBuffer b;
	b << "<p>Status: <strong>" << Esc(Text(p, "status")) << "</strong></p>";

	b << "<form method=\"post\" action=\"" << Esc(r.Site().Url("proposals/*/edit:POST", IdArgs(id))) << "\">" << Hidden(r)
	  << "<input name=\"title\" value=\"" << Esc(Text(p, "title")) << "\" required>"
	  << "<input name=\"repository\" value=\"" << Esc(Text(p, "repository")) << "\" placeholder=\"owner/repo\">"
	  << "<input name=\"estimated_hours\" value=\"" << Esc(AsString((double)p.Get("estimated_hours", 0.0))) << "\">"
	  << "<textarea name=\"body\">" << Esc(Text(p, "body")) << "</textarea>"
	  << "<button>Save</button></form>";

	b << "<h2>Evidence</h2><ul>";
	ValueArray ev = p.Get("evidence", ValueArray());
	for(int i = 0; i < ev.GetCount(); i++) {
		ValueMap e = ev[i];
		b << EvidenceRow(e);
	}
	b << "</ul>"
	     "<form method=\"post\" action=\"" << Esc(r.Site().Url("proposals/*/evidence:POST", IdArgs(id))) << "\">" << Hidden(r)
	  << "<select name=\"kind\"><option value=\"science\">Science article node</option>"
	     "<option value=\"privatehub\">PrivateHub repository</option></select>"
	     "<input name=\"ref\" placeholder=\"paper id or owner/repo\" required>"
	     "<input name=\"node_id\" placeholder=\"node id (science only)\">"
	     "<button>Attach evidence</button></form>";

	b << "<h2>Validation</h2>";
	ValueArray msgs = p.Get("messages", ValueArray());
	if(msgs.GetCount() == 0 && Text(p, "status") == TF_STATUS_VALIDATED)
		b << "<p>Valid. Content hash <code>" << Esc(Text(p, "content_hash")) << "</code></p>";
	else if(msgs.GetCount() > 0) {
		b << "<ul>";
		for(int i = 0; i < msgs.GetCount(); i++)
			b << "<li>" << Esc((String)msgs[i]) << "</li>";
		b << "</ul>";
	}
	b << "<form method=\"post\" action=\"" << Esc(r.Site().Url("proposals/*/validate:POST", IdArgs(id))) << "\">" << Hidden(r)
	  << "<button>Validate</button></form>";

	b << "<h2>Submit to Company</h2>";
	if(Text(p, "status") == TF_STATUS_SUBMITTED)
		b << "<p>Submitted as <a href=\"" << Esc(r.Site().Url("submissions/*", IdArgs(Text(p, "submission_id"))))
		  << "\">" << Esc(Text(p, "submission_id")) << "</a></p>";
	b << "<form method=\"post\" action=\"" << Esc(r.Site().Url("proposals/*/submit:POST", IdArgs(id))) << "\">" << Hidden(r)
	  << "<button>Submit</button></form>";
	Page(r, Text(p, "title"), String(b));
}

static void ProposalEdit(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	String id = r.PathArg(0);
	double hours = ScanDouble(r.Param("estimated_hours"));
	TfResult res = Service().EditProposal(Actor(r), id, r.Param("title"), r.Param("body"),
	                                      r.Param("repository"), hours);
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	r.Redirect(r.Site().Url("proposals/*", IdArgs(id)));
}

static void ProposalEvidence(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	String id = r.PathArg(0);
	TfResult res = Service().AttachEvidence(Actor(r), id, r.Param("kind"), r.Param("ref"), r.Param("node_id"));
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	r.Redirect(r.Site().Url("proposals/*", IdArgs(id)));
}

static void ProposalValidate(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	String id = r.PathArg(0);
	TfResult res = Service().Validate(Actor(r), id);
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	r.Redirect(r.Site().Url("proposals/*", IdArgs(id)));
}

static void ProposalSubmit(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	TfResult res = Service().Submit(Actor(r), r.PathArg(0));
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	r.Redirect(r.Site().Url("submissions/*", IdArgs(Text(res.payload, "id"))));
}

static void SubmissionStatus(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	String id = r.PathArg(0);
	TfResult res = Service().Status(Actor(r), id);
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	const ValueMap& s = res.payload;
	StringBuffer b;
	b << "<p>Company proposal <code>" << Esc(Text(s, "company_proposal_id")) << "</code></p>"
	  << "<p>Company status: <strong>" << Esc(Text(s, "company_status")) << "</strong></p>"
	  << "<p>Content hash <code>" << Esc(Text(s, "content_hash")) << "</code></p>"
	  << "<p>Submitted " << Esc(Text(s, "submitted")) << "</p>";
	Page(r, "Submission " + id, String(b));
}

void InstallTaskFoundrySite(EngineSite& site, TaskFoundryService& service)
{
	s_service = &service;
	SiteRouter& rt = site.Router();
	rt.Add("", Board);
	rt.Add("candidates/create:POST", CandidateCreate);
	rt.Add("candidates/*", CandidateDetail);
	rt.Add("candidates/*/propose:POST", CandidatePropose);
	rt.Add("proposals/*", ProposalEditor);
	rt.Add("proposals/*/edit:POST", ProposalEdit);
	rt.Add("proposals/*/evidence:POST", ProposalEvidence);
	rt.Add("proposals/*/validate:POST", ProposalValidate);
	rt.Add("proposals/*/submit:POST", ProposalSubmit);
	rt.Add("submissions/*", SubmissionStatus);
}

END_UPP_NAMESPACE
