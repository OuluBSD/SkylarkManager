#include "MetaAccountSite.h"
#include "MetaInbox.h"

NAMESPACE_UPP

// One meta-account site per process (mirroring the engine/skylark app
// singletons): the grants store is file-scoped and set at install time.
static WebGrants *s_grants = NULL;

static WebGrants& Grants()
{
	ASSERT_(s_grants, "meta-account site not installed");
	return *s_grants;
}

// The identity inbox, created with the site (its store lives under the site's
// data root). Client sites reach it only through the broker `notify` op.
static One<MetaInbox> s_inbox;

static MetaInbox *Inbox()
{
	return s_inbox.Get();
}

// ---------------------------------------------------------------------------
// HTML helpers (minimal, functional pages until the Witz work package)
// ---------------------------------------------------------------------------

static ValueMap JsonOk(bool ok, const String& err = String())
{
	ValueMap m;
	m.Add("ok", ok);
	if(err.GetCount())
		m.Add("error", err);
	return m;
}

static void HtmlPage(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\">"
	       "<title>" + title + "</title></head><body>"
	       "<h1>" + title + "</h1>" + body + "</body></html>");
}

static String HtmlForm(const String& action, const String& csrf, const String& fields)
{
	return "<form method=\"post\" action=\"" + action + "\">"
	       "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + csrf + "\">"
	       + fields + "<input type=\"submit\" value=\"OK\"></form>";
}

// Inbox rows carry text composed by client sites (usernames included), so
// they are escaped at render time.
static String EscText(const String& s)
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

// ---------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------

static void MetaHome(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Redirect(r.Site().Url("login"));
		return;
	}
	MetaInbox *inbox = Inbox();
	int unread = inbox ? inbox->UnreadCount(r.User().user_id) : 0;
	String body = "<p>Signed in as <b>" + r.User().username + "</b> ("
	            + r.User().user_id + ")</p>"
	            + "<p>Two-factor: " + (r.User().totp_enrolled ? String("enrolled") : String("not enrolled")) + "</p>"
	            + "<p><a href=\"" + r.Site().Url("consent") + "\">Consent grants</a></p>"
	            + "<p><a href=\"" + r.Site().Url("inbox") + "\">Inbox"
	            + (unread > 0 ? " (" + AsString(unread) + " unread)" : String()) + "</a></p>";
	HtmlPage(r, "Meta account", body);
}

static void MetaLoginForm(WebRequest& r)
{
	r.EnsureSession();
	String body = HtmlForm(r.Site().Url("login:POST"), r.CsrfToken(),
		"<input name=\"username\" placeholder=\"username\"><br>"
		"<input name=\"password\" type=\"password\"><br>"
		"<input name=\"code\" placeholder=\"2FA code (if enrolled)\"><br>");
	HtmlPage(r, "Sign in", body);
}

static void MetaLoginSubmit(WebRequest& r)
{
	String username = r.Param("username");
	String password = r.Param("password");
	String code = r.Param("code");
	r.DropSession();
	WebAccountUser u;
	if(!r.Site().Accounts().VerifyPassword(username, password, u).ok) {
		r.Json(JsonOk(false, "bad credentials"), 401);
		return;
	}
	// Enrolled accounts must present a live TOTP code or a recovery code.
	if(u.totp_enrolled && !r.Site().Accounts().VerifyTotpOrRecovery(u.user_id, code)) {
		r.Json(JsonOk(false, "second factor required"), 401);
		return;
	}
	String sid, csrf, err;
	if(!r.Site().Accounts().Login(username, password, r.ClientIp(), sid, csrf, u, err).ok) {
		r.Json(JsonOk(false, err), 401);
		return;
	}
	r.AdoptSession(sid);
	ValueMap m;
	m.Add("ok", true);
	m.Add("user", u.username);
	m.Add("user_id", u.user_id);
	m.Add("totp_enrolled", u.totp_enrolled);
	m.Add("csrf", r.CsrfToken());
	r.Json(m);
}

static void MetaLogout(WebRequest& r)
{
	r.DropSession();
	r.Json(JsonOk(true));
}

static void MetaTotpBegin(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	String secret;
	Vector<String> recovery;
	StoreResult st = r.Site().Accounts().BeginTotpEnrollment(r.User().user_id, r.Param("password"),
	                                                         secret, recovery);
	if(!st.ok) {
		r.Json(JsonOk(false, st.error), 403);
		return;
	}
	ValueArray codes;
	for(const String& c : recovery)
		codes.Add(c);
	ValueMap m;
	m.Add("ok", true);
	m.Add("secret", secret);
	m.Add("recovery", codes);
	m.Add("hint", "confirm with a live code from your authenticator");
	// Recovery codes are shown exactly once, at enrollment.
	r.Json(m);
}

static void MetaTotpConfirm(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	StoreResult st = r.Site().Accounts().ConfirmTotpEnrollment(r.User().user_id, r.Param("code"));
	if(!st.ok) {
		r.Json(JsonOk(false, st.error), 403);
		return;
	}
	r.Json(JsonOk(true));
}

static void MetaTotpDisable(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	StoreResult st = r.Site().Accounts().DisableTotp(r.User().user_id, r.Param("password"));
	if(!st.ok) {
		r.Json(JsonOk(false, st.error), 403);
		return;
	}
	r.Json(JsonOk(true));
}

static void MetaConsentList(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Redirect(r.Site().Url("login"));
		return;
	}
	Vector<WebAccountGrant> all;
	Grants().ListForSubject(r.User().user_id, "", all);
	String body;
	body << "<table border=\"1\"><tr><th>client</th><th>client user</th><th>scopes</th><th>state</th><th></th></tr>";
	for(const WebAccountGrant& g : all) {
		body << "<tr><td>" + g.client_site + "/" + g.client_id + "</td><td>"
		   << (g.client_user_id.IsEmpty() ? String("(any session of the subject)") : g.client_user_id)
		   << "</td><td>" << Join(g.scopes, ", ") << "</td><td>" << g.state << "</td><td>";
		if(g.state == "requested")
			body << HtmlForm(r.Site().Url("consent/decide:POST"), r.CsrfToken(),
			                 "<input type=\"hidden\" name=\"grant_id\" value=\"" + g.grant_id + "\">"
			                 "<input type=\"hidden\" name=\"approve\" value=\"1\">"
			                 "<input type=\"submit\" value=\"Approve\">")
		    << " "
		    << HtmlForm(r.Site().Url("consent/decide:POST"), r.CsrfToken(),
		                "<input type=\"hidden\" name=\"grant_id\" value=\"" + g.grant_id + "\">"
		                "<input type=\"hidden\" name=\"approve\" value=\"0\">"
		                "<input type=\"submit\" value=\"Deny\">");
		else if(g.IsActive())
			body << HtmlForm(r.Site().Url("consent/decide:POST"), r.CsrfToken(),
			                 "<input type=\"hidden\" name=\"grant_id\" value=\"" + g.grant_id + "\">"
			                 "<input type=\"hidden\" name=\"approve\" value=\"0\">"
			                 "<input type=\"submit\" value=\"Revoke\">");
		body << "</td></tr>";
	}
	body << "</table>";
	HtmlPage(r, "Consent grants", body);
}

// Inbox: the subject's events, newest first; a mark-all-read mutation (CSRF).
static void MetaInboxPage(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Redirect(r.Site().Url("login"));
		return;
	}
	Vector<InboxEvent> events;
	MetaInbox *inbox = Inbox();
	if(inbox)
		inbox->List(r.User().user_id, events, 50);
	String body;
	if(events.IsEmpty())
		body = "<p>No notifications.</p>";
	else {
		body = "<table border=\"1\"><tr><th>When</th><th>Source</th><th>Event</th><th>Message</th></tr>";
		for(const InboxEvent& e : events) {
			body << "<tr" << (e.read ? "" : String(" class=\"unread\"")) << "><td>"
			   << AsString(e.created_at) << "</td><td>" << EscText(e.source_site)
			   << "</td><td>" << EscText(e.type) << "</td><td>" << EscText(e.message);
			if(e.refs.GetCount()) {
				if(e.refs.StartsWith("meta:"))
					body << " <a href=\"" << EscText(e.refs.Mid(5)) << "\">open</a>";
				else
					body << " <code>" << EscText(e.refs) << "</code>";
			}
			body << "</td></tr>";
		}
		body << "</table>"
		   << "<p>" << HtmlForm(r.Site().Url("inbox/read-all:POST"), r.CsrfToken(),
		                        "<input type=\"submit\" value=\"Mark all read\">") << "</p>";
	}
	HtmlPage(r, "Inbox", body);
}

static void MetaInboxReadAll(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	MetaInbox *inbox = Inbox();
	if(inbox)
		inbox->MarkAllRead(r.User().user_id);
	r.Json(JsonOk(true));
}

static void ConsentEvent(const String& subject, const String& client_site,
                         const String& decider, const String& what)
{
	MetaInbox *inbox = Inbox();
	if(inbox)
		inbox->Push(subject, client_site, what, decider,
		            String().Cat() << "Consent for " << client_site << " " << what.Mid(8),
		            "meta:/consent");
}

static void MetaConsentDecide(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	String grant_id = r.Param("grant_id");
	bool approve = r.Param("approve") == "1";
	WebAccountGrant g;
	if(grant_id.IsEmpty() || !Grants().GetGrant(grant_id, g)) {
		r.Json(JsonOk(false, "unknown grant"), 404);
		return;
	}
	// The decider must be the grant's subject.
	if(g.subject_user_id != r.User().user_id) {
		r.Json(JsonOk(false, "not your grant"), 403);
		return;
	}
	// Approve/deny only pending requests; revoke only active grants. (Deny on
	// a pending request was broken before task 014: it tried to revoke.)
	StoreResult st;
	if(approve)
		st = Grants().Decide(grant_id, true);
	else if(g.state == "requested")
		st = Grants().Decide(grant_id, false);
	else
		st = Grants().Revoke(grant_id);
	if(!st.ok) {
		r.Json(JsonOk(false, st.error), 409);
		return;
	}
	ConsentEvent(g.subject_user_id, g.client_site, "meta:" + r.User().username,
	             approve ? "consent.granted"
	                     : (g.state == "requested" ? "consent.denied" : "consent.revoked"));
	r.Json(JsonOk(true));
}

void InstallMetaAccountSite(EngineSite& site, WebGrants& grants)
{
	s_grants = &grants;
	if(!s_inbox)
		s_inbox.Create(site.id, site.data_root);
	SiteRouter& rt = site.Router();
	rt.Add("", MetaHome);
	rt.Add("login", MetaLoginForm);
	rt.Add("login:POST", MetaLoginSubmit);
	rt.Add("logout:POST", MetaLogout);
	rt.Add("totp/begin:POST", MetaTotpBegin);
	rt.Add("totp/confirm:POST", MetaTotpConfirm);
	rt.Add("totp/disable:POST", MetaTotpDisable);
	rt.Add("consent", MetaConsentList);
	rt.Add("consent/decide:POST", MetaConsentDecide);
	rt.Add("inbox", MetaInboxPage);
	rt.Add("inbox/read-all:POST", MetaInboxReadAll);

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("consent-list", "GET", "consent", "session account",
	                                        "html", "List my consent grants", {}));
	cm.Add(site.id, site.title, HttpCommand("consent-decide", "POST", "consent/decide",
	                                        "session account (the grant's subject)", "html",
	                                        "Approve, deny or revoke a consent grant",
	                                        {"grant_id:string:required", "approve:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("inbox-list", "GET", "inbox", "session account",
	                                        "html", "Show my inbox events (unread first)", {}));
	cm.Add(site.id, site.title, HttpCommand("inbox-read-all", "POST", "inbox/read-all",
	                                        "session account", "html",
	                                        "Mark all my inbox events read", {}));
}

// ---------------------------------------------------------------------------
// Broker service
// ---------------------------------------------------------------------------

BrokerResult MetaAccountService::Handle(const BrokerCallContext& ctx, const BrokerCall& call)
{
	BrokerResult r;
	r.status = 400;
	if(call.op == "ping") {
		r.ok = true;
		r.status = 200;
		r.payload.Add("pong", true);
		r.payload.Add("site", "meta");
		return r;
	}
	if(call.op == "authenticate") {
		String username = call.payload.Get("username");
		String password = call.payload.Get("password");
		String code = call.payload.Get("totp_code");
		WebAccountUser u;
		StoreResult vr = accounts.VerifyPassword(username, password, u);
		if(!vr.ok) {
			r.status = 401;
			r.error = vr.error;
			return r;
		}
		if(u.totp_enrolled && !accounts.VerifyTotpOrRecovery(u.user_id, code)) {
			r.status = 401;
			r.error = "second factor required";
			return r;
		}
		r.ok = true;
		r.status = 200;
		r.payload.Add("user_id", u.user_id);
		r.payload.Add("username", u.username);
		return r;
	}
	if(call.op == "request-consent") {
		String subject = call.payload.Get("subject_user_id");
		String client_user = call.payload.Get("client_user_id");
		String client_site = call.payload.Get("client_site");
		String client_id = call.payload.Get("client_id");
		Vector<String> scopes;
		Value sv = call.payload.Get("scopes");
		if(sv.Is<ValueArray>())
			for(const Value& s : (ValueArray)sv)
				scopes.Add((String)s);
		String grant_id;
		bool existed = false;
		StoreResult st = grants.RequestGrantAs(subject, client_user, client_site, client_id,
		                                       scopes, grant_id, existed);
		if(!st.ok) {
			r.status = 400;
			r.error = st.error;
			return r;
		}
		if(!existed && Inbox())
			Inbox()->Push(subject, client_site, "consent.requested",
			              client_site + ":" + client_user,
			              String().Cat() << "Access requested for " << client_site
			                             << (client_user.IsEmpty() ? String() : String(" by ") + client_user),
			              "meta:/consent");
		r.ok = true;
		r.status = 200;
		r.payload.Add("grant_id", grant_id);
		r.payload.Add("existed", existed);
		return r;
	}
	if(call.op == "notify") {
		MetaInbox *inbox = Inbox();
		if(!inbox) {
			r.status = 503;
			r.error = "the meta inbox is not installed";
			return r;
		}
		StoreResult st = inbox->Push(call.payload.Get("subject_user_id"),
		                             call.payload.Get("source_site"),
		                             call.payload.Get("type"),
		                             call.payload.Get("actor"),
		                             call.payload.Get("message"),
		                             call.payload.Get("refs"));
		if(!st.ok) {
			r.status = 400;
			r.error = st.error;
			return r;
		}
		r.ok = true;
		r.status = 200;
		return r;
	}
	if(call.op == "check-grant") {
		String subject = call.payload.Get("subject_user_id");
		String client_site = call.payload.Get("client_site");
		Vector<String> scopes;
		Value sv = call.payload.Get("scopes");
		if(sv.Is<ValueArray>())
			for(const Value& s : (ValueArray)sv)
				scopes.Add((String)s);
		// Sign-in links to two different local accounts on one site make the mapping
		// undefined: refuse rather than pick one (task 039).
		Vector<WebAccountGrant> linked;
		grants.ListForSubject(subject, "granted", linked);
		String first_local;
		bool ambiguous = false;
		for(int i = 0; i < linked.GetCount(); i++) {
			const WebAccountGrant& m = linked[i];
			bool sso = false;
			for(int k = 0; k < m.scopes.GetCount(); k++)
				if(m.scopes[k] == "sso")
					sso = true;
			if(!sso || m.client_site != client_site || m.client_user_id.IsEmpty())
				continue;
			if(first_local.IsEmpty())
				first_local = m.client_user_id;
			else if(m.client_user_id != first_local)
				ambiguous = true;
		}
		if(ambiguous) {
			r.status = 409;
			r.error = "sign-in link is ambiguous for this subject";
			return r;
		}
		WebAccountGrant g;
		if(!grants.FindActive(subject, client_site, scopes, g)) {
			r.ok = true;
			r.status = 200;
			r.payload.Add("active", false);
			return r;
		}
		r.ok = true;
		r.status = 200;
		r.payload.Add("active", true);
		r.payload.Add("grant_id", g.grant_id);
		// The linked client-site user, so a sign-in can map the identity (task 039).
		r.payload.Add("client_user_id", g.client_user_id);
		return r;
	}
	if(call.op == "resolve-session") {
		// Validates a central MetaAccount session. The raw id stays inside this call; the
		// reply names only the account, never the session value.
		WebAccountSession s;
		if(!accounts.ResolveSession(call.payload.Get("session_id"), s)) {
			r.status = 401;
			r.error = "no active central session";
			return r;
		}
		WebAccountUser u;
		if(!accounts.GetUser(s.user_id, u) || u.disabled) {
			r.status = 401;
			r.error = "central account is not available";
			return r;
		}
		r.ok = true;
		r.status = 200;
		r.payload.Add("user_id", u.user_id);
		r.payload.Add("username", u.username);
		return r;
	}
	if(call.op == "revoke-grant") {
		StoreResult st = grants.Revoke(call.payload.Get("grant_id"));
		if(!st.ok) {
			r.status = 409;
			r.error = st.error;
			return r;
		}
		r.ok = true;
		r.status = 200;
		return r;
	}
	r.status = 404;
	r.error = "unknown op";
	return r;
}

void RegisterMetaAccountService(const String& meta_site_id, SiteAccounts& accounts, WebGrants& grants)
{
	TheWebEngine().Broker().Register(meta_site_id, new MetaAccountService(accounts, grants));
}

END_UPP_NAMESPACE
