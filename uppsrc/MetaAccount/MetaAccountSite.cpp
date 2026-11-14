#include "MetaAccountSite.h"
#include "MetaInbox.h"
#include <WebEngine/SessionRoutes.h>

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
	       "<header>" + SessionChromeHtml(r) + "</header>"
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

// The sign-in look follows the reference card (~/webref/javascript-login-form): dark navy, violet
// accent, labeled form groups. No external font or script: the page falls back to the system stack.
static String MetaLoginStyle()
{
	return R"MLOGIN(<style>
body { margin: 0; min-height: 100vh; box-sizing: border-box; padding: 16px; background: #1d015a; color: rgba(255, 255, 255, 0.6);
  font-family: Raleway, system-ui, -apple-system, "Segoe UI", sans-serif; display: flex; align-items: center; justify-content: center; }
a { color: #7f71fe; text-decoration: none; cursor: pointer; }
.m-container { width: 100%; max-width: 375px; box-sizing: border-box; padding: 30px 0; border-radius: 16px;
  border: 4px solid rgba(255, 255, 255, 0.1); background: linear-gradient(0deg, rgba(4, 158, 116, 0.3), rgba(255, 255, 215, 0.02));
  box-shadow: 0 32px 47px rgba(32, 23, 23, 0.09); }
.m-form { width: 100%; padding: 0 30px; box-sizing: border-box; }
.m-form_title { color: #fff; font-size: 26px; font-weight: 500; margin: 0 0 20px; }
.m-form_group { display: flex; flex-wrap: wrap; justify-content: space-between; padding-bottom: 18px; }
.m-form_label { display: block; margin-bottom: 10px; font-size: 15px; }
.m-form_input { width: 100%; appearance: none; outline: 0; line-height: 44px; border: 1px solid rgba(255, 255, 255, 0.09);
  background-color: rgba(255, 255, 255, 0.04); border-radius: 3px; padding: 0 15px; font-size: 18px; font-family: inherit;
  box-sizing: border-box; color: #fff; font-weight: 300; }
.m-form_input:focus { border-color: #7f71fe; }
.m-form_btn { appearance: none; outline: 0; background-color: #7f71fe; border: 0; margin-top: 15px; line-height: 44px; padding: 0 15px;
  color: #fff; font-weight: 500; border-radius: 3px; width: 100%; cursor: pointer; font-size: 18px; font-family: inherit; }
.m-form_link { text-align: center; }
.m-form_btn-register { display: block; line-height: 44px; color: rgba(255, 255, 255, 0.35); cursor: not-allowed; }
.m-form_error { margin: 0 0 18px; padding: 8px 12px; border: 1px solid rgba(255, 140, 140, 0.5); border-radius: 3px; color: #ffb8b8; }
</style>)MLOGIN";
}

static void MetaPage(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\">"
	       "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
	       "<title>" + title + "</title>" + MetaLoginStyle() + "</head><body>"
	       "<main class=\"m-container\">" + body + "</main></body></html>");
}

// A browser sign-in that failed gets a readable page with the reason (task 042). An API caller keeps JSON.
static void BrowserError(WebRequest& r, const String& back, const String& message, int code)
{
	r.Status(code, "Error");
	MetaPage(r, "Not done",
	         "<div class=\"m-form\"><h2 class=\"m-form_title\">Not done</h2>"
	         "<p class=\"m-form_error\" role=\"alert\">" + EscText(message) + "</p>"
	         "<div class=\"m-form_link\"><a href=\"" + EscText(r.Site().Url(back)) + "\">Back</a></div></div>");
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
	String body = "<form class=\"m-form\" method=\"post\" action=\"" + EscText(r.Site().Url("login:POST")) + "\">"
	       "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + EscText(r.CsrfToken()) + "\">"
	       "<h2 class=\"m-form_title\">Account Login</h2>"
	       "<div class=\"m-form_group\"><label class=\"m-form_label\" for=\"username\">Username</label>"
	       "<input class=\"m-form_input\" type=\"text\" id=\"username\" name=\"username\" autocomplete=\"username\" required></div>"
	       "<div class=\"m-form_group\"><label class=\"m-form_label\" for=\"password\">Password</label>"
	       "<input class=\"m-form_input\" type=\"password\" id=\"password\" name=\"password\" autocomplete=\"current-password\" required></div>"
	       "<div class=\"m-form_group\"><label class=\"m-form_label\" for=\"code\">Authentication code</label>"
	       "<input class=\"m-form_input\" type=\"text\" id=\"code\" name=\"code\" inputmode=\"numeric\" autocomplete=\"one-time-code\""
	       " placeholder=\"6 digits, if enrolled\"></div>"
	       "<input type=\"hidden\" name=\"next\" value=\"" + EscText(r.Param("next")) + "\">"
	       "<div class=\"m-form_group\"><button class=\"m-form_btn\" type=\"submit\">Log In</button></div>"
	       "<div class=\"m-form_link\"><span class=\"m-form_btn-register\" aria-disabled=\"true\">Register (not available yet)</span></div>"
	       "</form>";
	MetaPage(r, "Sign in", body);
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
	r.DropSession();
	WebAccountUser u;
	if(!r.Site().Accounts().VerifyPassword(username, password, u).ok) {
		if(browser)
			BrowserError(r, back, "The username or password was not recognized.", 401);
		else
			r.Json(JsonOk(false, "bad credentials"), 401);
		return;
	}
	// Enrolled accounts must present a live TOTP code or a recovery code.
	if(u.totp_enrolled && !r.Site().Accounts().VerifyTotpOrRecovery(u.user_id, code)) {
		if(browser)
			BrowserError(r, back, "A second-factor code is needed. Enter a current code and try again.", 401);
		else
			r.Json(JsonOk(false, "second factor required"), 401);
		return;
	}
	String sid, csrf, err;
	if(!r.Site().Accounts().Login(username, password, r.ClientIp(), sid, csrf, u, err).ok) {
		if(browser)
			BrowserError(r, back, "Sign-in did not work. Try again.", 401);
		else
			r.Json(JsonOk(false, err), 401);
		return;
	}
	r.AdoptSession(sid);
	if(browser) {
		r.Redirect(!next.IsEmpty() && SsoReturnAllowed(r, next) ? next : r.Site().Url(""));
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

void RegisterMetaAccountService(const String& meta_site_id, SiteAccounts& accounts, WebGrants& grants)
{
	TheWebEngine().Broker().Register(meta_site_id, new MetaAccountService(accounts, grants));
}

END_UPP_NAMESPACE
