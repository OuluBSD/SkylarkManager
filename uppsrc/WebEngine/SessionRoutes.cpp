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

static void LoginForm(WebRequest& r)
{
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign in</title>" + LoginStylesheet() + "</head><body>"
	       "<h1>Sign in</h1><form method=\"post\" action=\"" + EscHtml(r.Site().Url("session/login")) + "\">"
	       "<p><label>Username <input name=\"username\" autocomplete=\"username\" required></label></p>"
	       "<p><label>Password <input type=\"password\" name=\"password\" autocomplete=\"current-password\" required></label></p>"
	       "<p><button type=\"submit\">Sign in</button></p></form></body></html>");
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
		r.Redirect("/");
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

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("session-login", "POST", "session/login", "any account of this site",
	                                        "json", "Sign in and open a session cookie",
	                                        {"username:string:required", "password:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("session-logout", "POST", "session/logout", "open session",
	                                        "json", "Close the current session", {}));
	cm.Add(site.id, site.title, HttpCommand("session-whoami", "GET", "session/whoami", "open session",
	                                        "json", "Show the signed-in account", {}));
}

END_UPP_NAMESPACE
