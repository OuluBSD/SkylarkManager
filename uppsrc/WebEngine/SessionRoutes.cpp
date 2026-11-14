#include "SessionRoutes.h"
#include "Engine.h"
#include "Request.h"

NAMESPACE_UPP

static String EscHtml(const String& s)
{
	StringBuffer out;
	for(int i = 0; i < s.GetCount(); i++) {
		char c = s[i];
		switch(c) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		default: out.Cat(c);
		}
	}
	return String(out);
}

// A browser form post gets a redirect or a page; an API caller gets JSON (task 039).
static bool WantsJson(WebRequest& r)
{
	return r.Header("accept").Find("json") >= 0 || r.Header("content-type").Find("json") >= 0;
}

// The composed-site palette (the same tokens Atlas and Science use), so a sign-in inside a
// webapp frame reads as part of that app rather than a bare page (task 038).
static String LoginStylesheet()
{
	return R"LOGIN(<style>
:root { --bg: #f7f7f5; --fg: #1b1b1b; --muted: #5c5c5c; --line: #d0d0cc; --panel: #ffffff; --link: #1864ab; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --bg: #151515; --fg: #ececec; --muted: #a0a0a0; --line: #3a3a3a; --panel: #1f1f1f; --link: #74c0fc; }
}
:root[data-theme="dark"] { --bg: #151515; --fg: #ececec; --muted: #a0a0a0; --line: #3a3a3a; --panel: #1f1f1f; --link: #74c0fc; }
body { background: var(--bg); color: var(--fg); font: 15px/1.45 system-ui, sans-serif; margin: 0; padding: 16px; max-width: 420px; }
h1 { font-size: 1.5rem; margin: 8px 0 16px; }
label { display: flex; flex-direction: column; gap: 4px; font-size: 0.9rem; }
input { font: inherit; color: var(--fg); background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 6px 8px; }
button { font: inherit; color: #ffffff; background: var(--link); border: 0; border-radius: 6px; padding: 6px 14px; cursor: pointer; }
.login-error { color: #c92a2a; border: 1px solid #c92a2a; border-radius: 6px; padding: 6px 8px; }
</style>)LOGIN";
}

bool SsoReturnAllowed(WebRequest& r, const String& url);

static const char* SSO_IDP_SITE = "meta";

// The sign-in page. A browser that failed to sign in gets the same page with a short
// message above the form (task 042); the message never echoes the typed username.
static void RenderLoginPage(WebRequest& r, const String& error, int code)
{
	// A validated return target travels with the form, so a sign-in started from a webapp
	// comes back to that webapp (task 039). An unlisted target is simply not carried.
	String next = r.Param("next");
	String hidden = TheWebEngine().MetaSso() && SsoReturnAllowed(r, next)
		? "<input type=\"hidden\" name=\"next\" value=\"" + EscHtml(next) + "\">"
		: String();
	// A browser that still holds a session posts this form with that session's CSRF token, so the
	// mutation fence accepts it; a guest form carries none (the fence has nothing to check).
	String csrf = r.CsrfToken().IsEmpty()
		? String()
		: "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + EscHtml(r.CsrfToken()) + "\">";
	String alert = error.IsEmpty() ? String()
		: "<p role=\"alert\" class=\"login-error\">" + EscHtml(error) + "</p>";
	if(code != 200)
		r.Status(code, "Sign in");
	// With MetaAccount SSO on, a composed site has no password form of its own: the browser signs
	// in through MetaAccount (task 042). MetaAccount itself keeps its own login form.
	if(TheWebEngine().MetaSso() && r.Site().id != SSO_IDP_SITE) {
		r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign in</title>" + LoginStylesheet() + "</head><body>"
		       "<h1>Sign in</h1>" + alert +
		       "<p>Sign in with your MetaAccount to open this site.</p>"
		       "<p><a href=\"" + EscHtml(r.Site().Url("session/sso")) + "?next=%2F\">Continue with MetaAccount</a></p>"
		       "</body></html>");
		return;
	}
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign in</title>" + LoginStylesheet() + "</head><body>"
	       "<h1>Sign in</h1>" + alert + "<form method=\"post\" action=\"" + EscHtml(r.Site().Url("session/login")) + "\">" + hidden + csrf +
	       "<p><label>Username <input name=\"username\" autocomplete=\"username\" required></label></p>"
	       "<p><label>Password <input type=\"password\" name=\"password\" autocomplete=\"current-password\" required></label></p>"
	       "<p><button type=\"submit\">Sign in</button></p></form>"
	       "<p><a href=\"" + EscHtml(r.Site().Url("session/sso")) + "?next=%2F\">Continue with MetaAccount</a></p>"
	       "</body></html>");
}

// The sign-in page of this site: MetaAccount has one, at login; composed sites use session/login (task 048).
static String SignInPath(WebRequest& r)
{
	return r.Site().id == SSO_IDP_SITE ? String("login") : String("session/login");
}

static void LoginForm(WebRequest& r)
{
	RenderLoginPage(r, String(), 200);
}

// ---------------------------------------------------------------------------
// Shared MetaAccount sign-in (task 039). MetaAccount is the one identity provider.
// A composed site adopts its own site-stamped session only when a live central
// session is present, the central account holds an approved sign-in grant for
// this site, and the user has not signed out of this site. Roles and grants stay
// the site's own: adoption copies no authority from MetaAccount.
// Cross-site calls are in-process WebBroker calls; the session value is never
// placed in a URL, a diagnostic entry or a reply.
// ---------------------------------------------------------------------------

static const char* SSO_SCOPE = "sso";
static const int   SSO_SUPPRESS_DAYS = 30;

// The marker a deliberate local sign-out sets. Only session/sso clears it.
// The lowest existing role a new visitor's account gets on each site (task 042, per S1). An empty
// role means the site does not offer provisioning: the visitor keeps the public pages only.
//   atlas        reader     Atlas edits need the admin role; reader is read-only.
//   financials   viewer     ledger:read only; finance is never granted by consent.
//   science      reader     author needs science-author; public reads only.
//   channels     viewer     read-only; no write role exists to grant.
//   privatehub, taskfoundry, youtubeplanner, meta: no provisioning (private or write roles,
//   or no login role needed, or the identity provider itself).
static String SsoDefaultRole(const String& site_id)
{
	if(site_id == "atlas" || site_id == "science")
		return "reader";
	if(site_id == "financials" || site_id == "channels")
		return "viewer";
	return String();
}

static String SsoSuppressName(const String& site_id)
{
	return SiteAccounts::CookieNameFor(site_id) + "_sso_off";
}

// A return target is a same-site absolute path: no scheme, no authority, no
// backslash or control characters. Anything else becomes "/".
static String SsoSafeNext(const String& next)
{
	if(next.GetCount() == 0 || next.GetCount() > 512 || next[0] != '/'
	   || (next.GetCount() > 1 && next[1] == '/'))
		return "/";
	for(int i = 0; i < next.GetCount(); i++) {
		int c = (byte)next[i];
		if(c < 0x20 || c == 0x7f || c == '\\')
			return "/";
	}
	return next;
}

// The host this request was made to, without its port. Composed sites share the host and differ by
// port, so a return target is checked against this host and a composed site's port.
static String SsoRequestHost(WebRequest& r)
{
	String h = TrimBoth(r.Header("host"));
	if(h.StartsWith("[")) {
		int e = h.Find(']');
		return e < 0 ? String() : h.Left(e + 1);
	}
	int c = h.Find(':');
	return c < 0 ? h : h.Left(c);
}

static int SsoSitePort(const String& site_id)
{
	Vector<EngineSiteListing> sites = TheWebEngine().ListSites();
	for(const EngineSiteListing& s : sites)
		if(s.id == site_id)
			return s.port;
	return 0;
}

// "http://<request host>:<site port><path>" for a composed site, or "" when unknown.
static String SsoSiteUrl(WebRequest& r, const String& site_id, const String& path)
{
	String host = SsoRequestHost(r);
	int port = SsoSitePort(site_id);
	if(host.IsEmpty() || port <= 0)
		return String();
	return "http://" + host + ":" + AsString(port) + path;
}

// A MetaAccount return target is accepted only when it is an http URL on this request's host, on the
// port of a composed site, with a same-site path. Anything else (another host, an unlisted port, a
// scheme, or a path that fails SsoSafeNext) is refused, so the caller falls back to "/".
bool SsoReturnAllowed(WebRequest& r, const String& url)
{
	String host = SsoRequestHost(r);
	if(host.IsEmpty() || !url.StartsWith("http://" + host + ":"))
		return false;
	String rest = url.Mid(7 + host.GetCount());   // ":<port><path>"
	String port_s;
	int i = 1;
	while(i < rest.GetCount() && IsDigit(rest[i]))
		port_s.Cat(rest[i++]);
	if(port_s.IsEmpty() || port_s.GetCount() > 5)
		return false;
	int port = ScanInt(port_s);
	bool composed = false;
	Vector<EngineSiteListing> sites = TheWebEngine().ListSites();
	for(const EngineSiteListing& s : sites)
		if(s.port == port)
			composed = true;
	if(!composed)
		return false;
	String path = rest.Mid(i);
	return path.IsEmpty() || SsoSafeNext(path) == path;
}

// The central account behind this request's MetaAccount cookie, or false.
static bool SsoCentralUser(WebRequest& r, BrokerCallContext& ctx, String& subject, String* username = nullptr)
{
	String central = r.RequestCookie(SiteAccounts::CookieNameFor(SSO_IDP_SITE));
	if(central.IsEmpty())
		return false;
	BrokerCall who;
	who.op = "resolve-session";
	who.payload.Add("session_id", central);
	BrokerResult w = TheWebEngine().Broker().Call(ctx, who);
	if(!w.ok)
		return false;
	subject = w.payload.Get("user_id");
	if(username)
		*username = w.payload.Get("username");
	return subject.GetCount() > 0;
}

// Adopts a site session for the linked account, if the grant is active. Returns
// true when the request now has a user. Every refusal leaves the request a guest.
static bool SsoAdoptLinked(WebRequest& r)
{
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = SSO_IDP_SITE;
	String subject;
	if(!SsoCentralUser(r, ctx, subject))
		return false;

	BrokerCall g;
	g.op = "check-grant";
	g.payload.Add("subject_user_id", subject);
	g.payload.Add("client_site", r.Site().id);
	ValueArray scopes;
	scopes.Add(SSO_SCOPE);
	g.payload.Add("scopes", scopes);
	BrokerResult gr = TheWebEngine().Broker().Call(ctx, g);
	if(!gr.ok || !(bool)gr.payload.Get("active"))
		return false;

	String local_id = gr.payload.Get("client_user_id");
	if(local_id.IsEmpty())
		return false;
	String sid, csrf, err;
	if(!r.Site().Accounts().AdoptUserSession(local_id, r.ClientIp(), sid, csrf, err).ok)
		return false;
	r.AdoptSession(sid);
	return r.HasUser();
}

// Adopts the linked account and records one outcome code for the site. The record names the site
// and the code only: no account id, username, session value or grant id (task 039).
static bool SsoAdopt(WebRequest& r)
{
	const char *outcome = "no-central-session";
	if(r.RequestCookie(SiteAccounts::CookieNameFor(SSO_IDP_SITE)).GetCount())
		outcome = SsoAdoptLinked(r) ? "adopted" : "no-active-link";
	TheWebEngine().Broker().NoteOutcome(r.Site().id, SSO_IDP_SITE, "sso-adopt", outcome, String(outcome) == "adopted");
	return r.HasUser();
}

// Called for every composed request before its handler (WebEngine dispatch). A
// safe request from a guest with a live central session and no local sign-out
// marker is signed in to this site. Mutations are never adopted here.
void SsoAutoSignIn(WebRequest& r)
{
	if(!TheWebEngine().MetaSso() || r.HasUser() || r.IsMutation() || r.Site().id == SSO_IDP_SITE)
		return;
	if(r.RequestCookie(SsoSuppressName(r.Site().id)).GetCount()) {
		TheWebEngine().Broker().NoteOutcome(r.Site().id, SSO_IDP_SITE, "sso-adopt", "suppressed", false);
		return;
	}
	SsoAdopt(r);
}

// GET session/sso?next=/path: the explicit "Continue with MetaAccount" action.
// Clears this site's sign-out marker, adopts the central session when linked,
// and returns to a validated same-site path.
static void SsoContinue(WebRequest& r)
{
	r.SetCookieValue(SsoSuppressName(r.Site().id), "", GetSysTime() - 3600);
	String next = SsoSafeNext(r.Param("next"));
	if(!r.HasUser())
		SsoAdopt(r);
	if(r.HasUser()) {
		r.Redirect(next);
		return;
	}
	// MetaAccount signs in once and returns here: its login carries this site's own
	// session/sso URL as the return target, which MetaAccount accepts only on this host (task 039).
	String back = SsoSiteUrl(r, r.Site().id, "/session/sso?next=" + UrlEncode(next));
	String login = SsoSiteUrl(r, SSO_IDP_SITE, "/login");
	if(login.GetCount() && back.GetCount())
		login << "?next=" << UrlEncode(back);
	// A central session with no link to this site can ask for a new local account (task 042).
	String offer;
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = SSO_IDP_SITE;
	String subject, username;
	if(!SsoDefaultRole(r.Site().id).IsEmpty() && SsoCentralUser(r, ctx, subject, &username)) {
		String csrf = r.CsrfToken().IsEmpty()
			? String()
			: "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + EscHtml(r.CsrfToken()) + "\">";
		offer = "<form method=\"post\" action=\"" + EscHtml(r.Site().Url("session/sso/provision")) + "\">" + csrf +
		        "<p>Ask MetaAccount to create a <b>" + EscHtml(username) + "</b> account on this site. "
		        "Nothing is created until you approve it on MetaAccount.</p>"
		        "<p><button type=\"submit\">Request account</button></p></form>";
	}
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Continue with MetaAccount</title></head><body>"
	       "<h1>Continue with MetaAccount</h1>"
	       "<p>No MetaAccount session is open in this browser, or this account is not linked to this site.</p>" + offer +
	       "<p><a href=\"" + EscHtml(login) + "\">Sign in to MetaAccount</a></p>"
	       "</body></html>");
}

// A browser form gets the MetaAccount consent page after a request, or a short page for a refusal.
// An API caller gets the JSON reply (task 039).
static void SsoReply(WebRequest& r, const ValueMap& m, int code)
{
	if(WantsJson(r)) {
		r.Json(m, code);
		return;
	}
	if((bool)m.Get("ok")) {
		String consent = SsoSiteUrl(r, SSO_IDP_SITE, "/consent");
		r.Redirect(consent.GetCount() ? consent : r.Site().Url(""));
		return;
	}
	r.Status(code, "Error");
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Link not requested</title></head><body>"
	       "<h1>Link not requested</h1><p>" + EscHtml((String)m.Get("error")) + "</p>"
	       "<p><a href=\"" + EscHtml(r.Site().Url("")) + "\">Back to this site</a></p></body></html>");
}

// POST session/sso/provision: a visitor with a MetaAccount session and no link asks MetaAccount to
// create a local account here. The request is a consent the central user approves (task 042).
static void SsoProvision(WebRequest& r)
{
	ValueMap m;
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = SSO_IDP_SITE;
	String subject, username;
	if(SsoDefaultRole(r.Site().id).IsEmpty()) {
		m.Add("ok", false);
		m.Add("error", "this site does not offer a new account from MetaAccount");
		SsoReply(r, m, 403);
		return;
	}
	if(!SsoCentralUser(r, ctx, subject, &username)) {
		m.Add("ok", false);
		m.Add("error", "no active MetaAccount session in this browser");
		SsoReply(r, m, 401);
		return;
	}
	BrokerCall c;
	c.op = "request-provision";
	c.payload.Add("subject_user_id", subject);
	c.payload.Add("client_site", r.Site().id);
	c.payload.Add("client_id", "sso");
	c.payload.Add("proposed_username", username);
	c.payload.Add("proposed_role", SsoDefaultRole(r.Site().id));
	ValueArray scopes;
	scopes.Add(SSO_SCOPE);
	c.payload.Add("scopes", scopes);
	BrokerResult cr = TheWebEngine().Broker().Call(ctx, c);
	if(!cr.ok) {
		m.Add("ok", false);
		m.Add("error", cr.error);
		SsoReply(r, m, cr.status);
		return;
	}
	m.Add("ok", true);
	m.Add("status", "account requested on the MetaAccount consent page");
	SsoReply(r, m, 200);
}

// POST session/sso/link: a signed-in local user asks the central account to link
// this site. The request is a consent the central user must approve on MetaAccount.
static void SsoLink(WebRequest& r)
{
	ValueMap m;
	if(!r.HasUser()) {
		m.Add("ok", false);
		m.Add("error", "sign in to this site first");
		SsoReply(r, m, 401);
		return;
	}
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = SSO_IDP_SITE;
	String subject;
	if(!SsoCentralUser(r, ctx, subject)) {
		m.Add("ok", false);
		m.Add("error", "no active MetaAccount session in this browser");
		SsoReply(r, m, 401);
		return;
	}
	BrokerCall c;
	c.op = "request-consent";
	c.payload.Add("subject_user_id", subject);
	c.payload.Add("client_user_id", r.User().user_id);
	c.payload.Add("client_site", r.Site().id);
	c.payload.Add("client_id", "sso");
	ValueArray scopes;
	scopes.Add(SSO_SCOPE);
	c.payload.Add("scopes", scopes);
	BrokerResult cr = TheWebEngine().Broker().Call(ctx, c);
	if(!cr.ok) {
		m.Add("ok", false);
		m.Add("error", cr.error);
		SsoReply(r, m, cr.status);
		return;
	}
	m.Add("ok", true);
	m.Add("status", "approval requested on the MetaAccount consent page");
	m.Add("grant_id", cr.payload.Get("grant_id"));
	SsoReply(r, m, 200);
}

static void Login(WebRequest& r)
{
	// With MetaAccount sign-in on, a composed site takes no local password from a browser form
	// (task 048). The JSON API keeps the password path for operators and automation (AGENTS.md).
	if(TheWebEngine().MetaSso() && r.Site().id != SSO_IDP_SITE && !WantsJson(r)) {
		RenderLoginPage(r, "This site signs in through MetaAccount. Use Continue with MetaAccount on the sign-in page.", 403);
		return;
	}
	String username = r.Param("username");
	String password = r.Param("password");
	r.DropSession();
	String sid, csrf, err;
	WebAccountUser u;
	if(!r.Site().Accounts().Login(username, password, r.ClientIp(), sid, csrf, u, err).ok) {
		// A browser form gets the sign-in page again with a plain message, not a JSON document
		// (task 042). An API caller keeps the JSON reply.
		if(!WantsJson(r)) {
			RenderLoginPage(r, "The username or password was not recognized. Check both and try again.", 401);
			return;
		}
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", err);
		r.Json(m, 401);
		return;
	}
	r.AdoptSession(sid);
	if(!WantsJson(r)) {
		// Back to the webapp that started the sign-in, only for an allowlisted target (task 039, SSO on).
		String next = r.Param("next");
		r.Redirect(TheWebEngine().MetaSso() && SsoReturnAllowed(r, next) ? next : String("/"));
		return;
	}
	ValueMap m;
	m.Add("ok", true);
	m.Add("user", u.username);
	m.Add("user_id", u.user_id);
	m.Add("csrf", r.CsrfToken());
	r.Json(m);
}

static void Whoami(WebRequest& r)
{
	if(!r.HasUser()) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "not signed in");
		r.Json(m, 401);
		return;
	}
	ValueMap m;
	m.Add("ok", true);
	m.Add("user", r.User().username);
	m.Add("user_id", r.User().user_id);
	m.Add("csrf", r.CsrfToken());
	r.Json(m);
}

static void Logout(WebRequest& r)
{
	r.DropSession();
	// This site stays signed out after a deliberate sign-out, even while the central
	// MetaAccount session is open; session/sso clears the marker (task 039). The
	// MetaAccount session and every other site are untouched.
	if(TheWebEngine().MetaSso())
		r.SetCookieValue(SsoSuppressName(r.Site().id), "1", GetSysTime() + SSO_SUPPRESS_DAYS * 24 * 3600);
	if(!WantsJson(r)) {
		r.Redirect(r.Site().Url(SignInPath(r)));
		return;
	}
	ValueMap m;
	m.Add("ok", true);
	r.Json(m);
}

// GET session/sso/diagnostics: the SSO outcomes in the broker ledger, for an account with the admin
// role on this site. Only the SSO ops are returned, and each entry holds sites, op, ok and a code,
// never caller ids, account names or session values (task 039 review).
static void SsoDiagnostics(WebRequest& r)
{
	if(!r.HasUser()) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "sign in first");
		r.Json(m, 401);
		return;
	}
	if(FindIndex(r.User().roles, String("admin")) < 0) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "admin role required");
		r.Json(m, 403);
		return;
	}
	int max = r.IntParam("max");
	if(max <= 0 || max > 1000)
		max = 1000;
	Vector<String> ops;
	ops << "resolve-session" << "check-grant" << "request-consent" << "sso-adopt";
	r.Plain(TheWebEngine().Broker().SanitizedDiagnosticsJSON(max, ops), "application/json; charset=UTF-8");
}

// The signed-in account and its session controls for a composed page's header. A guest gets a sign-in
// link. A signed-in account gets its name, a sign-out form and, on an app, a form that asks MetaAccount
// to link the account (task 039). Every form carries the session's CSRF token.
String SessionChromeHtml(WebRequest& r)
{
	if(!r.HasUser())
		return "<span class=\"wf-session\"><a href=\"" + EscHtml(r.Site().Url(SignInPath(r))) + "\">Sign in</a></span>";
	String csrf = "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + EscHtml(r.CsrfToken()) + "\">";
	String out = "<span class=\"wf-session\">Signed in as " + EscHtml(r.User().username) + " "
	             "<form method=\"post\" action=\"" + EscHtml(r.Site().Url("session/logout")) + "\" class=\"wf-session-form\">" + csrf +
	             "<button type=\"submit\" id=\"wf-sign-out\">Sign out</button></form>";
	if(TheWebEngine().MetaSso() && r.Site().id != SSO_IDP_SITE)
		out << " <form method=\"post\" action=\"" << EscHtml(r.Site().Url("session/sso/link")) << "\" class=\"wf-session-form\">" << csrf
		    << "<button type=\"submit\" id=\"wf-link-meta\">Link to MetaAccount</button></form>";
	return out + "</span>";
}

void InstallSessionRoutes(EngineSite& site)
{
	// The MetaAccount site signs in only through its own TOTP-enforcing login (task 047); its
	// session/login routes are registered by InstallMetaAccountSite, never the password-only ones here.
	if(site.id != SSO_IDP_SITE) {
		site.Router().Add("session/login", LoginForm);
		site.Router().Add("session/login:POST", Login);
	}
	site.Router().Add("session/logout:POST", Logout);
	site.Router().Add("session/whoami", Whoami);
	if(TheWebEngine().MetaSso()) {
		site.Router().Add("session/sso", SsoContinue);
		site.Router().Add("session/sso/diagnostics", SsoDiagnostics);
		site.Router().Add("session/sso/link:POST", SsoLink);
		site.Router().Add("session/sso/provision:POST", SsoProvision);
	}

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("session-login", "POST", "session/login", "any account of this site",
	                                        "json", "Sign in and open a session cookie",
	                                        {"username:string:required", "password:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("session-logout", "POST", "session/logout", "open session",
	                                        "json", "Close the current session", {}));
	cm.Add(site.id, site.title, HttpCommand("session-whoami", "GET", "session/whoami", "open session",
	                                        "json", "Show the signed-in account", {}));
	if(TheWebEngine().MetaSso())
	if(TheWebEngine().MetaSso()) {
		cm.Add(site.id, site.title, HttpCommand("session-sso-link", "POST", "session/sso/link", "open session",
		                                        "json", "Ask MetaAccount to link this account to this site", {}));
		cm.Add(site.id, site.title, HttpCommand("session-sso-provision", "POST", "session/sso/provision", "any visitor with a MetaAccount session",
		                                        "json", "Ask MetaAccount to create a local account for this visitor on this site", {}));
	}
}

END_UPP_NAMESPACE
