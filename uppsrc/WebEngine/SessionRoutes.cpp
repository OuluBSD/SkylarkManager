#include "SessionRoutes.h"
#include "Engine.h"
#include "Request.h"
#include <WebsiteTemplates/WebsiteTemplates.h>

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

bool SsoReturnAllowed(WebRequest& r, const String& url);

static const char* SSO_IDP_SITE = "meta";
static String SsoSiteUrl(const String& site_id, const String& path);

// The sign-in page. A browser that failed to sign in gets the same page with a short
// message above the form (task 042); the message never echoes the typed username.
static void RenderLoginPage(WebRequest& r, const String& error, int code)
{
	// A validated return target travels with the form, so a sign-in started from a webapp
	// comes back to that webapp (task 039). An unlisted target is simply not carried.
	String next = r.Param("next");
	String safe_next = TheWebEngine().MetaSso() && SsoReturnAllowed(r, next) ? next : String();
	// A browser session's CSRF token is carried by the template-rendered form;
	// guest forms have no session token to include.
	String csrf = r.CsrfToken();
	if(code != 200)
		r.Status(code, "Sign in");
	// With MetaAccount SSO on, a composed site has no password form of its own: the browser signs
	// in through MetaAccount (task 042). MetaAccount itself keeps its own login form.
	if(TheWebEngine().MetaSso() && r.Site().id != SSO_IDP_SITE) {
		const WebsiteTemplate *t = FindWebsiteTemplate("engine-login");
		ASSERT(t);
		r.Html(t->RenderPage("Sign in", t->RenderEngineLoginBody(error, String(), String(), String(),
		       SsoSiteUrl(r.Site().id, "/session/sso?next=%2F"), true), ThemeHtmlAttrs(r), ThemeHeadScript()));
		return;
	}
	const WebsiteTemplate *t = FindWebsiteTemplate("engine-login");
	ASSERT(t);
	r.Html(t->RenderPage("Sign in", t->RenderEngineLoginBody(error, r.Site().Url("session/login"), csrf,
	       safe_next, SsoSiteUrl(r.Site().id, "/session/sso?next=%2F"), false), ThemeHtmlAttrs(r), ThemeHeadScript()));
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

// Use the target site's configured public origin; never manufacture an SSO
// credential redirect from an untrusted Host or forwarded header.
static String SsoSiteUrl(const String& site_id, const String& path)
{
	EngineSite *target = TheWebEngine().FindSite(site_id);
	if(!target)
		return String();
	return target->PublicUrl(path);
}

// A MetaAccount return target is accepted only when it is an http URL on this request's host, on the
// port of a composed site, with a same-site path. Anything else (another host, an unlisted port, a
// scheme, or a path that fails SsoSafeNext) is refused, so the caller falls back to "/".
// A MetaAccount return may go to a composed site port or to the stock host port (task 050), and nowhere else.
bool SsoPortAllowed(int port)
{
	if(port == TheWebEngine().HostPort())
		return true;
	for(const EngineSiteListing& site : TheWebEngine().ListSites())
		if(site.port == port)
			return true;
	return false;
}

bool SsoReturnAllowed(WebRequest& r, const String& url)
{
	if(url.GetCount() > 1024 || url.Find('#') >= 0)
		return false;
	for(const EngineSiteListing& entry : TheWebEngine().ListSites()) {
		EngineSite *target = TheWebEngine().FindSite(entry.id);
		if(!target) continue;
		String origin = TrimBoth(target->external_origin);
		if(!origin.StartsWith("http://") && !origin.StartsWith("https://")) continue;
		int slash = origin.Find('/', origin.Find("://") + 3);
		if(slash >= 0) origin = origin.Left(slash);
		if(!url.StartsWith(origin + "/")) continue;
		String path = url.Mid(origin.GetCount());
		return SsoSafeNext(path) == path;
	}
	String portal_origin = TheWebEngine().PortalOrigin();
	if(!portal_origin.IsEmpty() && url.StartsWith(portal_origin + "/")) {
		String path = url.Mid(portal_origin.GetCount());
		return SsoSafeNext(path) == path;
	}
	return false;
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

String SsoCentralCookieName()
{
	return SiteAccounts::CookieNameFor(SSO_IDP_SITE);
}

// The username behind a central MetaAccount session id, or false. Used by the portal host bridge (task 050).
bool SsoCentralUsername(const String& session_id, String& username)
{
	BrokerCallContext ctx;
	ctx.from_site = "host";
	ctx.to_site = SSO_IDP_SITE;
	BrokerCall who;
	who.op = "resolve-session";
	who.payload.Add("session_id", session_id);
	BrokerResult w = TheWebEngine().Broker().Call(ctx, who);
	if(!w.ok)
		return false;
	username = w.payload.Get("username");
	return username.GetCount() > 0;
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
	r.AdoptSession(sid, "sso");
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

// GET session/sso?next=/path: adopts an existing link or presents this site's
// first-use consent. A MetaAccount login is reused across sibling ports.
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
	String back = SsoSiteUrl(r.Site().id, "/session/sso?next=" + UrlEncode(next));
	String login = SsoSiteUrl(SSO_IDP_SITE, "/login");
	if(login.GetCount() && back.GetCount())
		login << "?next=" << UrlEncode(back);
	String offer;
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = SSO_IDP_SITE;
	String subject, username;
	if(!SsoDefaultRole(r.Site().id).IsEmpty() && SsoCentralUser(r, ctx, subject, &username)) {
		WebAccountUser collision;
		bool exists = r.Site().Accounts().FindUserByUsername(username, collision);
		r.EnsureSession();
		String csrf = "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + EscHtml(r.CsrfToken()) + "\">";
		offer << "<p>Allow MetaAccount to create or link a least-privilege account on "
		      << EscHtml(r.Site().title) << "?</p>"
		      << "<form method=\"post\" action=\"" << EscHtml(r.Site().Url("session/sso/consent")) << "\">"
		      << csrf << "<input type=\"hidden\" name=\"next\" value=\"" << EscHtml(next) << "\">"
		      << "<input type=\"hidden\" name=\"decision\" value=\"yes\">";
		if(exists)
			offer << "<p>An account named <b>" << EscHtml(username) << "</b> already exists. Prove ownership to link it.</p>"
			      << "<label>Existing account password <input type=\"password\" name=\"local_password\" required autocomplete=\"current-password\"></label>";
		if(exists && collision.totp_enrolled)
			offer << "<label>Authenticator or recovery code <input type=\"text\" name=\"local_code\" required inputmode=\"numeric\" autocomplete=\"one-time-code\"></label>";
		offer << "<p><button type=\"submit\">Yes, continue</button></p></form>"
		      << "<form method=\"post\" action=\"" << EscHtml(r.Site().Url("session/sso/consent")) << "\">"
		      << csrf << "<input type=\"hidden\" name=\"next\" value=\"" << EscHtml(next) << "\">"
		      << "<input type=\"hidden\" name=\"decision\" value=\"no\"><button type=\"submit\">No</button></form>";
	}
	r.Html("<!doctype html><html" + ThemeHtmlAttrs(r) + "><head><meta charset=\"utf-8\">" + ThemeHeadScript() + "<title>Continue with MetaAccount</title></head><body>"
	       "<h1>Continue with MetaAccount</h1>"
	       "<p>This MetaAccount account is not linked to this site yet. Sign in to MetaAccount if prompted, then choose whether to continue.</p>" + offer +
	       "<p><a href=\"" + EscHtml(login) + "\">Sign in to MetaAccount</a></p>"
	       "</body></html>");
}

static void SsoConsent(WebRequest& r)
{
	if(!TheWebEngine().MetaSso() || SsoDefaultRole(r.Site().id).IsEmpty()) {
		r.Status(403, "SSO consent is unavailable");
		return;
	}
	String next = SsoSafeNext(r.Param("next"));
	if(r.Param("decision") != "yes" && r.Param("decision") != "no") {
		r.Status(400, "Invalid consent decision");
		return;
	}
	// Consent only runs on the anonymous session created for this form. Never let a
	// forged/replayed choice revoke an already authenticated local account session.
	if(!r.HasSession() || r.HasUser()) {
		r.Status(403, "Consent session is unavailable");
		return;
	}
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = SSO_IDP_SITE;
	String subject, username;
	if(!SsoCentralUser(r, ctx, subject, &username)) {
		r.Redirect(r.Site().Url("session/login"));
		return;
	}
	BrokerCall cleanup;
	cleanup.op = "clear-provision-requests";
	cleanup.payload.Add("subject_user_id", subject);
	cleanup.payload.Add("client_site", r.Site().id);
	BrokerResult cleaned = TheWebEngine().Broker().Call(ctx, cleanup);
	if(!cleaned.ok) {
		r.Status(cleaned.status, "Previous account requests could not be reconciled");
		return;
	}
	if(r.Param("decision") != "yes") {
		if(!r.Site().Accounts().Logout(r.Session().session_id)) {
			r.Status(503, "Consent session could not be revoked");
			return;
		}
		r.DropSession();
		r.Redirect(r.Site().Url("session/login"));
		return;
	}
	String local_id;
	WebAccountUser existing;
	if(r.Site().Accounts().FindUserByUsername(username, existing)) {
		WebAccountUser verified;
		if(!r.Site().Accounts().VerifyPassword(username, r.Param("local_password"), verified).ok || verified.user_id != existing.user_id) {
			r.Status(403, "Ownership proof failed");
			r.Html("<p>That account was not linked. Sign in to the existing account first, then retry MetaAccount consent.</p>");
			return;
		}
		if(existing.totp_enrolled && !r.Site().Accounts().VerifyTotpOrRecovery(existing.user_id, r.Param("local_code"))) {
			r.Status(403, "Ownership proof failed");
			r.Html("<p>That account was not linked. Use a valid authenticator or recovery code.</p>");
			return;
		}
		local_id = existing.user_id;
	}
	else {
		Vector<String> roles;
		roles.Add(SsoDefaultRole(r.Site().id));
		bool existed = false;
		StoreResult created = r.Site().Accounts().CreateSsoUser(username, username, roles, local_id, existed);
		if(!created.ok || existed) {
			r.Status(409, "Account could not be provisioned safely");
			return;
		}
	}
	BrokerCall grant;
	grant.op = "grant-consent";
	grant.payload.Add("subject_user_id", subject);
	grant.payload.Add("client_user_id", local_id);
	grant.payload.Add("client_site", r.Site().id);
	grant.payload.Add("client_id", "sso");
	ValueArray scopes;
	scopes.Add(SSO_SCOPE);
	grant.payload.Add("scopes", scopes);
	BrokerResult granted = TheWebEngine().Broker().Call(ctx, grant);
	if(!granted.ok) {
		r.Status(granted.status, "Consent could not be saved");
		return;
	}
	String sid, csrf, err;
	if(!r.Site().Accounts().AdoptUserSession(local_id, r.ClientIp(), sid, csrf, err).ok) {
		r.Status(503, "Local session could not be opened");
		return;
	}
	r.AdoptSession(sid, "sso");
	r.Redirect(next);
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
		String consent = SsoSiteUrl(SSO_IDP_SITE, "/consent");
		r.Redirect(consent.GetCount() ? consent : r.Site().Url(""));
		return;
	}
	r.Status(code, "Error");
	r.Html("<!doctype html><html" + ThemeHtmlAttrs(r) + "><head><meta charset=\"utf-8\">" + ThemeHeadScript() + "<title>Link not requested</title></head><body>"
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
	auto login_event = [&](const String& outcome, const String& method, const String& actor = String()) {
		WebAuditEvent e; e.at = GetSysTime(); e.site = r.Site().id; e.kind = "login";
		e.route = "session/login"; e.method = "POST"; e.outcome = outcome; e.operation = method;
		e.actor_user_id = actor; e.status = outcome == "success" ? 200 : method == "sso_required" ? 403 : 401;
		if(r.HasSession()) e.session_ref = r.Session().data.Get("__audit_ref", "");
		r.Site().Audit().Record(e);
	};
	// With MetaAccount sign-in on, a composed site takes no local password from a browser form
	// (task 048). The JSON API keeps the password path for operators and automation (AGENTS.md).
	if(TheWebEngine().MetaSso() && r.Site().id != SSO_IDP_SITE && !WantsJson(r)) {
		login_event("failure", "sso_required");
		RenderLoginPage(r, "This site signs in through MetaAccount. Use Continue with MetaAccount on the sign-in page.", 403);
		return;
	}
	String username = r.Param("username");
	String password = r.Param("password");
	r.DropSession();
	String sid, csrf, err;
	WebAccountUser u;
	if(!r.Site().Accounts().Login(username, password, r.ClientIp(), sid, csrf, u, err).ok) {
		login_event("failure", "password");
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
	login_event("success", "password", u.user_id);
	if(!WantsJson(r)) {
		// Back to the webapp that started the sign-in, only for an allowlisted target (task 039, SSO on).
		String next = r.Param("next");
		bool allowed = TheWebEngine().MetaSso() && SsoReturnAllowed(r, next);
		if(!next.IsEmpty() && !allowed) {
			WebAuditEvent rejected; rejected.at = GetSysTime(); rejected.site = r.Site().id;
			rejected.kind = "redirect"; rejected.route = "redirect"; rejected.method = "POST";
			rejected.outcome = "rejected"; rejected.status = 302; rejected.actor_user_id = u.user_id;
			r.Site().Audit().Record(rejected);
		}
		r.Redirect(allowed ? next : String("/"));
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

String ThemeHtmlAttrs(WebRequest& r)
{
	return WebsiteThemeHtmlAttributes(r.RequestCookie("wf_theme"), r.RequestCookie("wf_density"));
}

// Applies a shell broadcast. It accepts only a message from the same host (any port) and a whitelisted value,
// and sets an attribute, never HTML.
String ThemeHeadScript()
{
	return WebsiteThemeHeadScript();
}

void InstallSessionRoutes(EngineSite& site)
{
	InstallAuditRoutes(site);
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
		site.Router().Add("session/sso/consent:POST", SsoConsent);
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
		cm.Add(site.id, site.title, HttpCommand("session-sso-consent", "POST", "session/sso/consent", "MetaAccount session and explicit Yes/No choice",
		                                        "html", "Consent to a least-privilege local account for this site",
		                                        {"decision:string:required", "next:string:optional", "local_password:string:optional", "local_code:string:optional"}));
		cm.Add(site.id, site.title, HttpCommand("session-sso-link", "POST", "session/sso/link", "open session",
		                                        "json", "Ask MetaAccount to link this account to this site", {}));
		cm.Add(site.id, site.title, HttpCommand("session-sso-provision", "POST", "session/sso/provision", "any visitor with a MetaAccount session",
		                                        "json", "Ask MetaAccount to create a local account for this visitor on this site", {}));
	}
}

END_UPP_NAMESPACE
