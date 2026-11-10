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

static void LoginForm(WebRequest& r)
{
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Sign in</title></head><body>"
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
