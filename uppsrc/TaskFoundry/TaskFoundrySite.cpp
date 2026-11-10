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

static String SiteText(const ValueMap& m, const char *key)
{
	return (String)m.Get(key, "");
}

// Grants are consent records held by the meta grant store and bound to their
// subject user. Only an approved grant for this site, owned by the caller's
// session, gives the caller any TaskFoundry scope.
static WebGrants *s_grants = NULL;

static bool ActiveGrant(const String& user_id, WebAccountGrant& out)
{
	if(!s_grants)
		return false;
	Vector<WebAccountGrant> list;
	s_grants->ListForSubject(user_id, "granted", list);
	for(const WebAccountGrant& g : list)
		if(g.client_site == TASKFOUNDRY_SITE_ID && g.IsActive() &&
		   (IsNull(g.expires_at) || g.expires_at > GetSysTime())) {
			out = g;
			return true;
		}
	return false;
}

static TfActor Actor(WebRequest& r)
{
	TfActor a;
	a.user_id = r.User().user_id;
	a.username = r.User().username;
	WebAccountGrant g;
	if(ActiveGrant(a.user_id, g))
		a.scopes = TaskFoundryScopesOf(g.scopes);
	return a;
}

// Returns false, after redirecting to the site login, for anonymous callers.
static bool RequireLogin(WebRequest& r)
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

// Pages and writes need a login and an approved grant. A write without a grant
// is refused with 403; a page without one goes to the grants page.
static bool RequireGrant(WebRequest& r)
{
	if(!RequireLogin(r))
		return false;
	WebAccountGrant g;
	if(ActiveGrant(r.User().user_id, g))
		return true;
	if(r.Method() == "POST") {
		TfResult res;
		res.status = 403;
		res.error = "TaskFoundry needs an approved grant; request one on the grants page";
		Failed(r, res);
	}
	else
		r.Redirect(r.Site().Url("grants"));
	return false;
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
	if(!RequireGrant(r))
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
		b << ListItem(r.Site().Url("candidates/*", IdArgs(SiteText(c, "id"))), SiteText(c, "title"),
		              "paper " + SiteText(c, "paper_id"));
	}
	b << "</ul>"
	     "<form method=\"post\" action=\"" << Esc(r.Site().Url("candidates/create")) << "\">"
	  << Hidden(r) <<
	     "<input name=\"title\" placeholder=\"Candidate title\" required>"
	     "<input name=\"paper_id\" placeholder=\"Science paper id\" required>"
	     "<textarea name=\"summary\" placeholder=\"Summary\"></textarea>"
	     "<button>Add candidate</button></form>";
	b << "<h2>Proposals</h2><ul>";
	ValueArray props = res.payload.Get("proposals", ValueArray());
	for(int i = 0; i < props.GetCount(); i++) {
		ValueMap p = props[i];
		b << ListItem(r.Site().Url("proposals/*", IdArgs(SiteText(p, "id"))), SiteText(p, "title"), SiteText(p, "status"));
	}
	b << "</ul>";
	Page(r, "TaskFoundry", String(b));
}

static void CandidateCreate(WebRequest& r)
{
	if(!RequireGrant(r))
		return;
	TfResult res = Service().CreateCandidate(Actor(r), r.Param("title"), r.Param("summary"), r.Param("paper_id"));
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	r.Redirect(r.Site().Url("candidates/*", IdArgs(SiteText(res.payload, "candidate_id"))));
}

static void CandidateDetail(WebRequest& r)
{
	if(!RequireGrant(r))
		return;
	String id = r.PathArg(0);
	TfResult res = Service().GetCandidate(id);
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	StringBuffer b;
	b << "<p>" << Esc(SiteText(res.payload, "summary")) << "</p>"
	  << "<p>Science paper <code>" << Esc(SiteText(res.payload, "paper_id")) << "</code>, revision "
	  << Esc(SiteText(res.payload, "paper_revision")) << "</p>"
	  << "<form method=\"post\" action=\"" << Esc(r.Site().Url("candidates/*/propose", IdArgs(id))) << "\">"
	  << Hidden(r) << "<button>Start a proposal from this candidate</button></form>";
	Page(r, SiteText(res.payload, "title"), String(b));
}

static void CandidatePropose(WebRequest& r)
{
	if(!RequireGrant(r))
		return;
	TfResult res = Service().CreateProposal(Actor(r), r.PathArg(0));
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	r.Redirect(r.Site().Url("proposals/*", IdArgs(SiteText(res.payload, "proposal_id"))));
}

static String EvidenceRow(const ValueMap& e)
{
	if(SiteText(e, "kind") == "science")
		return "<li>science paper <code>" + Esc(SiteText(e, "paper_id")) + "</code> revision " +
		       Esc(SiteText(e, "paper_revision")) + " node <code>" + Esc(SiteText(e, "node_id")) + "</code></li>";
	return "<li>privatehub <code>" + Esc(SiteText(e, "project")) + "</code> branch " + Esc(SiteText(e, "branch")) +
	       " at <code>" + Esc(SiteText(e, "head_commit")) + "</code></li>";
}

static void ProposalEditor(WebRequest& r)
{
	if(!RequireGrant(r))
		return;
	String id = r.PathArg(0);
	TfResult res = Service().GetProposal(id);
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	const ValueMap& p = res.payload;
	StringBuffer b;
	b << "<p>Status: <strong>" << Esc(SiteText(p, "status")) << "</strong></p>";

	b << "<form method=\"post\" action=\"" << Esc(r.Site().Url("proposals/*/edit", IdArgs(id))) << "\">" << Hidden(r)
	  << "<input name=\"title\" value=\"" << Esc(SiteText(p, "title")) << "\" required>"
	  << "<input name=\"repository\" value=\"" << Esc(SiteText(p, "repository")) << "\" placeholder=\"owner/repo\">"
	  << "<input name=\"estimated_hours\" value=\"" << Esc(AsString((double)p.Get("estimated_hours", 0.0))) << "\">"
	  << "<textarea name=\"body\">" << Esc(SiteText(p, "body")) << "</textarea>"
	  << "<button>Save</button></form>";

	b << "<h2>Evidence</h2><ul>";
	ValueArray ev = p.Get("evidence", ValueArray());
	for(int i = 0; i < ev.GetCount(); i++) {
		ValueMap e = ev[i];
		b << EvidenceRow(e);
	}
	b << "</ul>"
	     "<form method=\"post\" action=\"" << Esc(r.Site().Url("proposals/*/evidence", IdArgs(id))) << "\">" << Hidden(r)
	  << "<select name=\"kind\"><option value=\"science\">Science article node</option>"
	     "<option value=\"privatehub\">PrivateHub repository</option></select>"
	     "<input name=\"ref\" placeholder=\"paper id or owner/repo\" required>"
	     "<input name=\"node_id\" placeholder=\"node id (science only)\">"
	     "<button>Attach evidence</button></form>";

	b << "<h2>Validation</h2>";
	ValueArray msgs = p.Get("messages", ValueArray());
	if(msgs.GetCount() == 0 && SiteText(p, "status") == TF_STATUS_VALIDATED)
		b << "<p>Valid. Content hash <code>" << Esc(SiteText(p, "content_hash")) << "</code></p>";
	else if(msgs.GetCount() > 0) {
		b << "<ul>";
		for(int i = 0; i < msgs.GetCount(); i++)
			b << "<li>" << Esc((String)msgs[i]) << "</li>";
		b << "</ul>";
	}
	b << "<form method=\"post\" action=\"" << Esc(r.Site().Url("proposals/*/validate", IdArgs(id))) << "\">" << Hidden(r)
	  << "<button>Validate</button></form>";

	b << "<h2>Submit to Company</h2>";
	if(SiteText(p, "status") == TF_STATUS_SUBMITTED)
		b << "<p>Submitted as <a href=\"" << Esc(r.Site().Url("submissions/*", IdArgs(SiteText(p, "submission_id"))))
		  << "\">" << Esc(SiteText(p, "submission_id")) << "</a></p>";
	b << "<form method=\"post\" action=\"" << Esc(r.Site().Url("proposals/*/submit", IdArgs(id))) << "\">" << Hidden(r)
	  << "<button>Submit</button></form>";
	Page(r, SiteText(p, "title"), String(b));
}

static void ProposalEdit(WebRequest& r)
{
	if(!RequireGrant(r))
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
	if(!RequireGrant(r))
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
	if(!RequireGrant(r))
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
	if(!RequireGrant(r))
		return;
	TfResult res = Service().Submit(Actor(r), r.PathArg(0));
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	r.Redirect(r.Site().Url("submissions/*", IdArgs(SiteText(res.payload, "id"))));
}

static void SubmissionStatus(WebRequest& r)
{
	if(!RequireGrant(r))
		return;
	String id = r.PathArg(0);
	TfResult res = Service().Status(Actor(r), id);
	if(!res.ok) {
		Failed(r, res);
		return;
	}
	const ValueMap& s = res.payload;
	StringBuffer b;
	b << "<p>Company proposal <code>" << Esc(SiteText(s, "company_proposal_id")) << "</code></p>"
	  << "<p>Company status: <strong>" << Esc(SiteText(s, "company_status")) << "</strong></p>"
	  << "<p>Content hash <code>" << Esc(SiteText(s, "content_hash")) << "</code></p>"
	  << "<p>Submitted " << Esc(SiteText(s, "submitted")) << "</p>";
	Page(r, "Submission " + id, String(b));
}

// Shows the caller's TaskFoundry grants and lets them request one. Approval is
// made outside the site (see ManagerSkylark --decide-taskfoundry-grant).
static void GrantPage(WebRequest& r)
{
	if(!RequireLogin(r))
		return;
	Vector<WebAccountGrant> list;
	if(s_grants)
		s_grants->ListForSubject(r.User().user_id, "", list);
	StringBuffer b;
	bool active = false;
	b << "<table><tr><th>Grant</th><th>State</th><th>Scopes</th></tr>";
	for(const WebAccountGrant& g : list) {
		if(g.client_site != TASKFOUNDRY_SITE_ID)
			continue;
		String scopes;
		for(const String& sc : g.scopes)
			scopes << (scopes.IsEmpty() ? "" : ", ") << sc;
		b << "<tr><td>" << Esc(g.grant_id) << "</td><td>" << Esc(g.state) << "</td><td>" << Esc(scopes) << "</td></tr>";
		if(g.IsActive())
			active = true;
	}
	b << "</table>";
	if(active)
		b << "<p>Your TaskFoundry access is active.</p>";
	else
		b << "<p>No approved TaskFoundry grant. Request one, and an operator approves it.</p>"
		  << "<form method=\"post\" action=\"" << Esc(r.Site().Url("grants/request")) << "\">" << Hidden(r)
		  << "<button type=\"submit\">Request TaskFoundry access</button></form>";
	Page(r, "TaskFoundry access", String(b));
}

static void GrantRequest(WebRequest& r)
{
	if(!RequireLogin(r))
		return;
	TfResult res;
	if(!s_grants) {
		res.status = 500;
		res.error = "grant store is not installed";
		Failed(r, res);
		return;
	}
	String grant_id;
	bool existed = false;
	StoreResult st = s_grants->RequestGrant(r.User().user_id, TASKFOUNDRY_SITE_ID, "taskfoundry-ui",
	                                        TaskFoundryGrantableScopes(), grant_id, existed);
	if(!st) {
		res.status = 500;
		res.error = st.error;
		Failed(r, res);
		return;
	}
	r.Redirect(r.Site().Url("grants"));
}

void InstallTaskFoundrySite(EngineSite& site, TaskFoundryService& service, WebGrants *grants)
{
	s_service = &service;
	s_grants = grants;
	SiteRouter& rt = site.Router();
	rt.Add("", Board);
	rt.Add("grants", GrantPage);
	rt.Add("grants/request:POST", GrantRequest);
	rt.Add("candidates/create:POST", CandidateCreate);
	rt.Add("candidates/*", CandidateDetail);
	rt.Add("candidates/*/propose:POST", CandidatePropose);
	rt.Add("proposals/*", ProposalEditor);
	rt.Add("proposals/*/edit:POST", ProposalEdit);
	rt.Add("proposals/*/evidence:POST", ProposalEvidence);
	rt.Add("proposals/*/validate:POST", ProposalValidate);
	rt.Add("proposals/*/submit:POST", ProposalSubmit);
	rt.Add("submissions/*", SubmissionStatus);

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("grant-request", "POST", "grants/request", "session account",
	                                        "html", "Ask for a grant to use TaskFoundry", {}));
	cm.Add(site.id, site.title, HttpCommand("candidate-create", "POST", "candidates/create", "session account",
	                                        "html", "Create a candidate from a title, summary and paper",
	                                        {"title:string:required", "summary:text", "paper_id:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("candidate-propose", "POST", "candidates/*/propose", "session account",
	                                        "html", "Turn a candidate into a proposal", {"id:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("proposal-edit", "POST", "proposals/*/edit", "session account",
	                                        "html", "Edit a proposal's title, body, repository and hours",
	                                        {"id:string:required", "title:string", "body:text",
	                                         "repository:string", "estimated_hours:number"}));
	cm.Add(site.id, site.title, HttpCommand("proposal-evidence", "POST", "proposals/*/evidence", "session account",
	                                        "html", "Attach evidence to a proposal",
	                                        {"id:string:required", "kind:string", "ref:string", "node_id:string"}));
	cm.Add(site.id, site.title, HttpCommand("proposal-validate", "POST", "proposals/*/validate", "session account",
	                                        "html", "Validate a proposal before submission", {"id:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("proposal-submit", "POST", "proposals/*/submit", "session account",
	                                        "html", "Submit a validated proposal", {"id:string:required"}));
}

END_UPP_NAMESPACE
