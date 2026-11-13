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
</style>)LOGIN";
}

static bool SsoReturnAllowed(WebRequest& r, const String& url);

static void LoginForm(WebRequest& r)
{
	// A validated return target travels with the form, so a sign-in started from a webapp
	// comes back to that webapp (task 039). An unlisted target is simply not carried.
	String next = r.Param("next");
	String hidden = SsoReturnAllowed(r, next)
		? "<input type=\"hidden\" name=\"next\" value=\"" + EscHtml(next) + "\">"
		: String();
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign in</title>" + LoginStylesheet() + "</head><body>"
	       "<h1>Sign in</h1><form method=\"post\" action=\"" + EscHtml(r.Site().Url("session/login")) + "\">" + hidden +
	       "<p><label>Username <input name=\"username\" autocomplete=\"username\" required></label></p>"
	       "<p><label>Password <input type=\"password\" name=\"password\" autocomplete=\"current-password\" required></label></p>"
	       "<p><button type=\"submit\">Sign in</button></p></form>"
	       "<p><a href=\"" + EscHtml(r.Site().Url("session/sso")) + "?next=%2F\">Continue with MetaAccount</a></p>"
	       "</body></html>");
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

static const char* SSO_IDP_SITE = "meta";
static const char* SSO_SCOPE = "sso";
static const int   SSO_SUPPRESS_DAYS = 30;

// The marker a deliberate local sign-out sets. Only session/sso clears it.
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
static bool SsoReturnAllowed(WebRequest& r, const String& url)
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
static bool SsoCentralUser(WebRequest& r, BrokerCallContext& ctx, String& subject)
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
	return subject.GetCount() > 0;
}

// Adopts a site session for the linked account, if the grant is active. Returns
// true when the request now has a user. Every refusal leaves the request a guest.
static bool SsoAdopt(WebRequest& r)
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

// Called for every composed request before its handler (WebEngine dispatch). A
// safe request from a guest with a live central session and no local sign-out
// marker is signed in to this site. Mutations are never adopted here.
void SsoAutoSignIn(WebRequest& r)
{
	if(r.HasUser() || r.IsMutation() || r.Site().id == SSO_IDP_SITE)
		return;
	if(r.RequestCookie(SsoSuppressName(r.Site().id)).GetCount())
		return;
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
	String login = SsoSiteUrl(r, SSO_IDP_SITE, "/session/login");
	if(login.GetCount() && back.GetCount())
		login << "?next=" << UrlEncode(back);
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Continue with MetaAccount</title></head><body>"
	       "<h1>Continue with MetaAccount</h1>"
	       "<p>No MetaAccount session is open in this browser, or this account is not linked to this site.</p>"
	       "<p><a href=\"" + EscHtml(login) + "\">Sign in to MetaAccount</a></p>"
	       "<p><a href=\"" + EscHtml(r.Site().Url("session/login")) + "\">Use this site's own sign-in</a></p>"
	       "</body></html>");
}

// POST session/sso/link: a signed-in local user asks the central account to link
// this site. The request is a consent the central user must approve on MetaAccount.
static void SsoLink(WebRequest& r)
{
	ValueMap m;
	if(!r.HasUser()) {
		m.Add("ok", false);
		m.Add("error", "sign in to this site first");
		r.Json(m, 401);
		return;
	}
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = SSO_IDP_SITE;
	String subject;
	if(!SsoCentralUser(r, ctx, subject)) {
		m.Add("ok", false);
		m.Add("error", "no active MetaAccount session in this browser");
		r.Json(m, 401);
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
		r.Json(m, cr.status);
		return;
	}
	m.Add("ok", true);
	m.Add("status", "approval requested on the MetaAccount consent page");
	m.Add("grant_id", cr.payload.Get("grant_id"));
	r.Json(m);
}

static void Login(WebRequest& r)
{
	String username = r.Param("username");
	String password = r.Param("password");
	r.DropSession();
	String sid, csrf, err;
	WebAccountUser u;
	if(!r.Site().Accounts().Login(username, password, r.ClientIp(), sid, csrf, u, err).ok) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", err);
		r.Json(m, 401);
		return;
	}
	r.AdoptSession(sid);
	if(r.Header("accept").Find("json") < 0 && r.Header("content-type").Find("json") < 0) {
		// Back to the webapp that started the sign-in, only for an allowlisted target (task 039).
		String next = r.Param("next");
		r.Redirect(SsoReturnAllowed(r, next) ? next : String("/"));
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
	r.SetCookieValue(SsoSuppressName(r.Site().id), "1", GetSysTime() + SSO_SUPPRESS_DAYS * 24 * 3600);
	ValueMap m;
	m.Add("ok", true);
	r.Json(m);
}

void InstallSessionRoutes(EngineSite& site)
{
	site.Router().Add("session/login", LoginForm);
	site.Router().Add("session/login:POST", Login);
	site.Router().Add("session/logout:POST", Logout);
	site.Router().Add("session/whoami", Whoami);
	site.Router().Add("session/sso", SsoContinue);
	site.Router().Add("session/sso/link:POST", SsoLink);

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("session-login", "POST", "session/login", "any account of this site",
	                                        "json", "Sign in and open a session cookie",
	                                        {"username:string:required", "password:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("session-logout", "POST", "session/logout", "open session",
	                                        "json", "Close the current session", {}));
	cm.Add(site.id, site.title, HttpCommand("session-whoami", "GET", "session/whoami", "open session",
	                                        "json", "Show the signed-in account", {}));
	cm.Add(site.id, site.title, HttpCommand("session-sso-link", "POST", "session/sso/link", "open session",
	                                        "json", "Ask MetaAccount to link this account to this site", {}));
}

END_UPP_NAMESPACE
