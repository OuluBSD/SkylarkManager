#include "MetaAccountSite.h"
#include "MetaInbox.h"
#include <WebEngine/SessionRoutes.h>
#include <WebsiteTemplates/WebsiteTemplates.h>

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
	const WebsiteTemplate *t = FindWebsiteTemplate("glassmorphism");
	ASSERT(t);
	r.Html(t->RenderPage(title, "<header>" + SessionChromeHtml(r) + "</header><h1>" + title + "</h1>" + body,
	                      ThemeHtmlAttrs(r), ThemeHeadScript()));
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

static void MetaPage(WebRequest& r, const String& title, const String& body)
{
	const WebsiteTemplate *t = FindWebsiteTemplate("glassmorphism");
	ASSERT(t);
	r.Html(t->RenderPage(title, body, ThemeHtmlAttrs(r), ThemeHeadScript()));
}

// A browser sign-in that failed gets a readable page with the reason (task 042). An API caller keeps JSON.
static void BrowserError(WebRequest& r, const String& back, const String& message, int code)
{
	r.Status(code, "Error");
	const WebsiteTemplate *t = FindWebsiteTemplate("meta-login");
	ASSERT(t);
	r.Html(t->RenderPage("Not done",
	         "<div class=\"m-form\"><h2 class=\"m-form_title\">Not done</h2>"
	         "<p class=\"m-form_error\" role=\"alert\">" + EscText(message) + "</p>"
	         "<div class=\"m-form_link\"><a href=\"" + EscText(r.Site().Url(back)) + "\">Back</a></div></div>",
	         ThemeHtmlAttrs(r), ThemeHeadScript()));
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
	const WebsiteTemplate *t = FindWebsiteTemplate("meta-login");
	ASSERT(t);
	r.Html(t->RenderPage("Sign in", t->RenderMetaLoginForm(r.Site().Url("login:POST"), r.CsrfToken(), r.Param("next")),
	                     ThemeHtmlAttrs(r), ThemeHeadScript()));
}

// One sign-in surface: session/login is kept for the API caller, and a browser that opens it goes to login (task 048).
static void MetaLoginRedirect(WebRequest& r)
{
	String next = r.Param("next");
	r.Redirect(r.Site().Url(next.IsEmpty() ? String("login") : "login?next=" + UrlEncode(next)));
}

static void MetaLoginSubmit(WebRequest& r)
{
	String username = r.Param("username");
	String password = r.Param("password");
	String code = r.Param("code");
	String next = r.Param("next");
	String back = next.IsEmpty() ? String("login") : "login?next=" + UrlEncode(next);
	bool browser = r.Header("accept").Find("text/html") >= 0;
	String auth_method = "password";
	auto login_event = [&](const String& outcome, const String& method, const String& actor = String()) {
		WebAuditEvent e; e.at = GetSysTime(); e.site = r.Site().id; e.kind = "login";
		e.route = "login:POST"; e.method = "POST"; e.outcome = outcome; e.operation = method;
		e.actor_user_id = actor; e.status = outcome == "success" ? 200 : 401;
		if(r.HasSession()) e.session_ref = r.Session().data.Get("__audit_ref", "");
		r.Site().Audit().Record(e);
	};
	r.DropSession();
	WebAccountUser u;
	if(!r.Site().Accounts().VerifyPassword(username, password, u).ok) {
		login_event("failure", auth_method);
		if(browser)
			BrowserError(r, back, "The username or password was not recognized.", 401);
		else
			r.Json(JsonOk(false, "bad credentials"), 401);
		return;
	}
	// Enrolled accounts must present a live TOTP code or a recovery code.
	bool used_recovery = false;
	if(u.totp_enrolled && !r.Site().Accounts().VerifyTotpOrRecovery(u.user_id, code, used_recovery)) {
		login_event("failure", "password_second_factor");
		if(browser)
			BrowserError(r, back, "A second-factor code is needed. Enter a current code and try again.", 401);
		else
			r.Json(JsonOk(false, "second factor required"), 401);
		return;
	}
	if(u.totp_enrolled)
		auth_method = used_recovery ? "password_recovery" : "password_totp";
	String sid, csrf, err;
	if(!r.Site().Accounts().Login(username, password, r.ClientIp(), sid, csrf, u, err).ok) {
		login_event("failure", auth_method);
		if(browser)
			BrowserError(r, back, "Sign-in did not work. Try again.", 401);
		else
			r.Json(JsonOk(false, err), 401);
		return;
	}
	r.AdoptSession(sid);
	login_event("success", auth_method, u.user_id);
	if(browser) {
		bool allowed_next = !next.IsEmpty() && SsoReturnAllowed(r, next);
		if(!next.IsEmpty() && !allowed_next) {
			WebAuditEvent rejected; rejected.at = GetSysTime(); rejected.site = r.Site().id;
			rejected.kind = "redirect"; rejected.route = "redirect"; rejected.method = "POST";
			rejected.outcome = "rejected"; rejected.status = 302; rejected.actor_user_id = u.user_id;
			r.Site().Audit().Record(rejected);
		}
		r.Redirect(allowed_next ? next : r.Site().PublicUrl("/"));
		return;
	}
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

static void MetaAuditAction(WebRequest& r, const String& route, const String& outcome, int status)
{
	WebAuditEvent e; e.at = GetSysTime(); e.site = r.Site().id;
	e.kind = "sensitive_action"; e.route = route; e.method = "POST";
	e.outcome = outcome; e.status = status;
	if(r.HasUser()) e.actor_user_id = r.User().user_id;
	if(r.HasSession()) e.session_ref = r.Session().data.Get("__audit_ref", "");
	r.Site().Audit().Record(e);
}

static void MetaTotpBegin(WebRequest& r)
{
	if(!r.HasUser()) {
		MetaAuditAction(r, "account:totp_begin", "failure", 401);
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	String secret;
	Vector<String> recovery;
	StoreResult st = r.Site().Accounts().BeginTotpEnrollment(r.User().user_id, r.Param("password"),
	                                                         secret, recovery);
	if(!st.ok) {
		MetaAuditAction(r, "account:totp_begin", "failure", 403);
		r.Json(JsonOk(false, st.error), 403);
		return;
	}
	MetaAuditAction(r, "account:totp_begin", "success", 200);
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
		MetaAuditAction(r, "account:totp_confirm", "failure", 401);
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	StoreResult st = r.Site().Accounts().ConfirmTotpEnrollment(r.User().user_id, r.Param("code"));
	if(!st.ok) {
		MetaAuditAction(r, "account:totp_confirm", "failure", 403);
		r.Json(JsonOk(false, st.error), 403);
		return;
	}
	MetaAuditAction(r, "account:totp_confirm", "success", 200);
	r.Json(JsonOk(true));
}

static void MetaTotpDisable(WebRequest& r)
{
	if(!r.HasUser()) {
		MetaAuditAction(r, "account:totp_disable", "failure", 401);
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	StoreResult st = r.Site().Accounts().DisableTotp(r.User().user_id, r.Param("password"));
	if(!st.ok) {
		MetaAuditAction(r, "account:totp_disable", "failure", 403);
		r.Json(JsonOk(false, st.error), 403);
		return;
	}
	MetaAuditAction(r, "account:totp_disable", "success", 200);
	r.Json(JsonOk(true));
}

// Roles a consent can never grant: an approval of a new local account must not hand out authority
// (task 042). Least privilege: the site's own role for a new visitor is chosen from the rest.
static bool SsoRoleAllowed(const String& role)
{
	static const char* const elevated[] = { "admin", "owner", "editor", "finance" };
	if(role.IsEmpty())
		return false;
	for(const char* e : elevated)
		if(role == e)
			return false;
	return true;
}

// Creates the local account a provisioning request proposed. Refuses a username already in use on
// the client site: nothing is created or linked, and an operator must map the account (task 042).
static bool ProvisionLocalAccount(const WebAccountGrant& g, String& user_id, String& error)
{
	EngineSite *site = TheWebEngine().FindSite(g.client_site);
	if(!site) {
		error = "the requesting site is not composed";
		return false;
	}
	if(!SsoRoleAllowed(g.proposed_role)) {
		error = "this role cannot be granted by consent";
		return false;
	}
	WebAccountUser existing;
	if(site->Accounts().FindUserByUsername(g.proposed_username, existing)) {
		error = "the username '" + g.proposed_username + "' is already used on this site. "
		        "Nothing was linked; an operator must map that account.";
		return false;
	}
	Vector<String> roles;
	roles.Add(g.proposed_role);
	bool existed = false;
	StoreResult st = site->Accounts().CreateSsoUser(g.proposed_username, g.proposed_username, roles, user_id, existed);
	if(!st.ok) {
		error = st.error;
		return false;
	}
	return true;
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
		   << (!g.proposed_username.IsEmpty()
		       ? "new local account <b>" + EscText(g.proposed_username) + "</b>, role " + EscText(g.proposed_role)
		       : g.client_user_id.IsEmpty() ? String("(any session of the subject)") : g.client_user_id)
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

// A browser form post (Accept: text/html) goes back to a page, so the next navigation is a
// normal same-site request that carries the SameSite=Strict session cookie. A JSON reply left
// the browser on a JSON document, and Firefox then withheld the cookie from the next page
// (task 039). API callers keep the JSON reply.
// A browser decision that is refused shows its reason; success goes back to the page (task 042).
static void FormDone(WebRequest& r, const char *back, const Value& json, int code = 200)
{
	if(r.Header("accept").Find("text/html") >= 0) {
		if(code >= 400)
			BrowserError(r, back, (String)((ValueMap)json).Get("error"), code);
		else
			r.Redirect(r.Site().Url(back));
	}
	else
		r.Json(json, code);
}

static void MetaInboxReadAll(WebRequest& r)
{
	if(!r.HasUser()) {
		FormDone(r, "login", JsonOk(false, "sign in first"), 401);
		return;
	}
	MetaInbox *inbox = Inbox();
	if(inbox)
		inbox->MarkAllRead(r.User().user_id);
	FormDone(r, "inbox", JsonOk(true));
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
		FormDone(r, "login", JsonOk(false, "sign in first"), 401);
		return;
	}
	String grant_id = r.Param("grant_id");
	bool approve = r.Param("approve") == "1";
	WebAccountGrant g;
	if(grant_id.IsEmpty() || !Grants().GetGrant(grant_id, g)) {
		FormDone(r, "consent", JsonOk(false, "unknown grant"), 404);
		return;
	}
	// The decider must be the grant's subject.
	if(g.subject_user_id != r.User().user_id) {
		FormDone(r, "consent", JsonOk(false, "not your grant"), 403);
		return;
	}
	if(approve && g.state == "requested" && !g.proposed_username.IsEmpty()) {
		String uid, err;
		if(!ProvisionLocalAccount(g, uid, err)) {
			FormDone(r, "consent", JsonOk(false, err), 409);
			return;
		}
		StoreResult bound = Grants().ApproveProvision(grant_id, uid);
		if(!bound.ok) {
			FormDone(r, "consent", JsonOk(false, bound.error), 409);
			return;
		}
		ConsentEvent(g.subject_user_id, g.client_site, "meta:" + r.User().username, "consent.granted");
		MetaAuditAction(r, "meta:grant_approve", "success", 200);
		FormDone(r, "consent", JsonOk(true));
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
		FormDone(r, "consent", JsonOk(false, st.error), 409);
		return;
	}
	ConsentEvent(g.subject_user_id, g.client_site, "meta:" + r.User().username,
	             approve ? "consent.granted"
	                     : (g.state == "requested" ? "consent.denied" : "consent.revoked"));
	MetaAuditAction(r, approve ? "meta:grant_approve" : (g.state == "requested" ? "meta:grant_deny" : "meta:grant_revoke"), "success", 200);
	FormDone(r, "consent", JsonOk(true));
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
	// The composed-site sign-in and the API caller (session-login) reach the same handler, so the
	// site has one credential path, always TOTP-enforced (task 047).
	rt.Add("session/login", MetaLoginRedirect);
	rt.Add("session/login:POST", MetaLoginSubmit);
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
	if(call.op == "grant-consent") {
		String subject = call.payload.Get("subject_user_id");
		String client_site = call.payload.Get("client_site");
		String client_user = call.payload.Get("client_user_id");
		if(ctx.from_site != client_site || subject.IsEmpty() || client_user.IsEmpty()) {
			r.status = 403;
			r.error = "invalid consent caller";
			return r;
		}
		Vector<String> scopes;
		Value sv = call.payload.Get("scopes");
		if(sv.Is<ValueArray>())
			for(const Value& s : (ValueArray)sv)
				scopes.Add((String)s);
		String grant_id;
		StoreResult st = grants.GrantAs(subject, client_user, client_site,
		                               call.payload.Get("client_id"), scopes, grant_id);
		if(!st.ok) {
			r.status = 409;
			r.error = st.error;
			return r;
		}
		r.ok = true;
		r.status = 200;
		r.payload.Add("grant_id", grant_id);
		return r;
	}
	if(call.op == "clear-provision-requests") {
		String subject = call.payload.Get("subject_user_id");
		String client_site = call.payload.Get("client_site");
		if(ctx.from_site != client_site || subject.IsEmpty()) {
			r.status = 403;
			r.error = "invalid consent caller";
			return r;
		}
		StoreResult st = grants.DenyPendingProvision(subject, client_site);
		if(!st.ok) {
			r.status = 409;
			r.error = st.error;
			return r;
		}
		r.ok = true;
		r.status = 200;
		return r;
	}
	// The state of this site's account request for one username: "requested", "denied" or "none". The
	// client site explains a decline from it and never retries on its own (task 042).
	if(call.op == "provision-status") {
		String subject = call.payload.Get("subject_user_id");
		String client_site = call.payload.Get("client_site");
		String username = call.payload.Get("proposed_username");
		Vector<WebAccountGrant> asked, denied;
		grants.ListForSubject(subject, "requested", asked);
		grants.ListForSubject(subject, "denied", denied);
		String state = "none";
		for(const WebAccountGrant& g : denied)
			if(g.client_site == client_site && g.proposed_username == username && g.client_user_id.IsEmpty())
				state = "denied";
		for(const WebAccountGrant& g : asked)
			if(g.client_site == client_site && g.proposed_username == username && g.client_user_id.IsEmpty())
				state = "requested";
		r.ok = true;
		r.status = 200;
		r.payload.Add("state", state);
		return r;
	}
	if(call.op == "request-provision") {
		String subject = call.payload.Get("subject_user_id");
		String client_site = call.payload.Get("client_site");
		String role = call.payload.Get("proposed_role");
		if(!SsoRoleAllowed(role)) {
			r.status = 400;
			r.error = "this role cannot be granted by consent";
			return r;
		}
		Vector<String> scopes;
		Value sv = call.payload.Get("scopes");
		if(sv.Is<ValueArray>())
			for(const Value& s : (ValueArray)sv)
				scopes.Add((String)s);
		String grant_id;
		bool existed = false;
		StoreResult st = grants.RequestProvision(subject, client_site, call.payload.Get("client_id"),
		                                         call.payload.Get("proposed_username"), role, scopes,
		                                         grant_id, existed);
		if(!st.ok) {
			r.status = 400;
			r.error = st.error;
			return r;
		}
		if(!existed && Inbox())
			Inbox()->Push(subject, client_site, "consent.requested",
			              client_site + ":new account",
			              "A new local account was proposed for " + client_site, "meta:/consent");
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

String MetaAccountService::AuditOperation(const String& op) const
{
	if(op == "ping" || op == "authenticate" || op == "request-consent" || op == "grant-consent" || op == "clear-provision-requests" ||
	   op == "provision-status" || op == "request-provision" || op == "notify" ||
	   op == "check-grant" || op == "resolve-session" || op == "revoke-grant")
		return op;
	return "unclassified";
}

void RegisterMetaAccountService(const String& meta_site_id, SiteAccounts& accounts, WebGrants& grants)
{
	TheWebEngine().Broker().Register(meta_site_id, new MetaAccountService(accounts, grants));
}

END_UPP_NAMESPACE
