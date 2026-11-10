#include "ManagerSkylark.h"
#include "SystemAuth.h"
#include "EngineComposition.h"
#include <WebAuth/WebAuth.h>

NAMESPACE_UPP

namespace {

String& ConfiguredSiteDir()
{
	static String dir = GetHomeDirFile(".config/managerskylark");
	return dir;
}

bool& ConfiguredDemoSite()
{
	static bool demo = false;
	return demo;
}

StaticMutex& SessionMutex()
{
	static StaticMutex mutex;
	return mutex;
}

String NewSessionToken()
{
	return WebAuthRandomBytes(32, true);
}

}

void SystemAuthService::ConfigureSite(const String& site_dir, bool demo_site)
{
	ConfiguredSiteDir() = site_dir;
	ConfiguredDemoSite() = demo_site;
}

bool SystemAuthService::SetSitePassword(const String& site_dir, const String& username, const String& password)
{
	if(site_dir.IsEmpty() || username.IsEmpty() || username.GetCount() > 256 ||
	   password.IsEmpty() || password.GetCount() > 4096)
		return false;
	String encoded = WebAuthHashPassword(password, 210000);
	if(encoded.IsEmpty()) return false;
	String path = AppendFileName(site_dir, "auth.json");
	ValueMap users;
	if(FileExists(path)) {
		int64 size = GetFileLength(path);
		if(size < 0 || size > 64 * 1024) return false;
		Value existing = ParseJSON(LoadFile(path));
		if(!existing.Is<ValueMap>()) return false;
		users = existing;
	}
	ValueMap credential;
	credential.Add("password_hash", encoded);
	users.Set(username, credential);
	RealizeDirectory(site_dir);
	return SaveFile(path, AsJSON(users, true));
}

bool SystemAuthService::IsDemoSite()
{
	return ConfiguredDemoSite();
}

VectorMap<String, UserSession>& SystemAuthService::Sessions()
{
	static VectorMap<String, UserSession> s;
	return s;
}

bool SystemAuthService::AuthorizeRequest(Http& http)
{
	String id = http.GetHandlerId();
	bool public_route = id == "LoginPage" || id == "LoginPost" || id == "LogoutHandler" ||
	                    id == "ApiHealth" || id == "ApiAuthStatus" || id == "ApiAuthLogin" ||
	                    id == "ApiCliManifest" || id == "ApiCliManifestSite" || // public, read-only command manifest
	                    IsEngineGatewayHandler(id); // engine sites authenticate with their own sessions
	if(public_route)
		return true;

	UserSession session = GetCurrentSession(http);
	if(!session.is_authenticated) {
		if(id.StartsWith("Api") || id.StartsWith("Manager")) {
			http.Content("application/json; charset=utf-8", "{\"error\":\"authentication_required\"}");
			http.Response(401, "Unauthorized");
		}
		else {
			http.Content("text/plain; charset=utf-8", "Authentication required");
			http.Response(302, "Found");
			http.Redirect("/login");
		}
		return false;
	}

	if(ConfiguredDemoSite()) {
		bool read_only_page = id == "HomePage" || id == "ProjectsPage" || id == "ProjectDetailPage" ||
		                      id == "GoalsPage" || id == "GoalDetailPage" || id == "IssuesPage" ||
		                      id == "ReleasesPage" || id == "ApiProjects" || id == "ApiProjectDetail" ||
		                      id == "ApiNextTask" || id == "ApiGoals" || id == "ApiIssues" ||
		                      id == "ApiReleases" || id == "ManagerFrameworkSpec" ||
		                      id == "DataIndexPage" || id == "ApiDataSpec" || id == "ApiDataDocs" ||
		                      id == "ResearchIndexPage" || id == "ApiResearchPapers" ||
		                      id == "CandidateIndexPage" || id == "ApiCandidateLineage" ||
		                      id == "CandidateWorkflowPage" || id == "ApiCandidateWorkflow" ||
		                      id == "ReviewIndexPage" || id == "ReviewDetailPage" || id == "ApiReviewPackages" ||
		                      id == "ApiReviewPackage" || id == "ApiReviewRelated";
		if(http.GetMethod() != "GET" || !read_only_page) {
			http.Content("text/plain; charset=utf-8", "This synthetic demo site is read-only.");
			http.Response(403, "Forbidden");
			return false;
		}
	}
	return true;
}

String SystemAuthService::ExtractCookie(const Http& http, const char* name)
{
	String cookie_hdr = http.GetHeader("cookie");
	Vector<String> parts = Split(cookie_hdr, ';');
	for(int i = 0; i < parts.GetCount(); i++) {
		String p = TrimBoth(parts[i]);
		if(p.StartsWith(String(name) + "="))
			return TrimBoth(p.Mid((int)strlen(name) + 1));
	}
	return String();
}

bool SystemAuthService::VerifySystemPassword(const String& username, const String& password)
{
	if(username.IsEmpty() || username.GetCount() > 256 || password.IsEmpty() || password.GetCount() > 4096)
		return false;
	if(ConfiguredDemoSite())
		return username == "demo" && password == "demo";

	String conf_file = AppendFileName(ConfiguredSiteDir(), "auth.json");
	if(FileExists(conf_file) && GetFileLength(conf_file) <= 64 * 1024) {
		Value v = ParseJSON(LoadFile(conf_file));
		if(v.Is<ValueMap>()) {
			ValueMap m = v;
			int q = m.Find(username);
			if(q >= 0) {
				Value credential = m[username];
				if(IsString(credential) && (String)credential == password)
					return true;
				if(credential.Is<ValueMap>()) {
					ValueMap record = credential;
					if(IsString(record["password"]) && (String)record["password"] == password)
						return true;
					if(IsString(record["password_hash"]) && WebAuthVerifyPassword(password, record["password_hash"]))
						return true;
				}
			}
		}
	}
	return false;
}

UserSession SystemAuthService::CreateSession(const String& username)
{
	UserSession s;
	s.username = username;
	s.display_name = ConfiguredDemoSite() ? "Demo User" : ToUpper(username.Left(1)) + username.Mid(1);
	s.created_at = GetSysTime();
	s.expires_at = GetSysTime() + 8 * 3600;
	s.is_authenticated = true;
	s.is_admin = !ConfiguredDemoSite();
	s.is_demo = ConfiguredDemoSite();

	s.token = NewSessionToken();
	if(s.token.IsEmpty()) return UserSession();
	Mutex::Lock lock(SessionMutex());
	Sessions().GetAdd(s.token) = s;
	return s;
}

UserSession SystemAuthService::GetCurrentSession(const Http& http)
{
	String token = ExtractCookie(http, "wf_auth_session");
	if(token.IsEmpty()) {
		String auth_hdr = http.GetHeader("authorization");
		if(auth_hdr.StartsWith("Bearer "))
			token = TrimBoth(auth_hdr.Mid(7));
	}

	if(!token.IsEmpty()) {
		Mutex::Lock lock(SessionMutex());
		VectorMap<String, UserSession>& map = Sessions();
		int q = map.Find(token);
		if(q >= 0) {
			UserSession session = map[q];
			if(session.expires_at > GetSysTime() && session.is_demo == ConfiguredDemoSite())
				return session;
			map.Remove(q);
		}
	}

	// Default unauthenticated guest
	UserSession guest;
	guest.username = "guest";
	guest.display_name = "Guest Visitor";
	guest.is_authenticated = false;
	guest.is_admin = false;
	return guest;
}

bool SystemAuthService::Login(Http& http, const String& username, const String& password)
{
	UserSession sess = LoginSession(http, username, password);
	return sess.is_authenticated;
}

UserSession SystemAuthService::LoginSession(Http& http, const String& username, const String& password)
{
	if(VerifySystemPassword(username, password)) {
		UserSession sess = CreateSession(username);
		if(!sess.is_authenticated)
			return sess;
		// SetHeader("Set-Cookie", ...) is silently dropped on a redirect
		// response (Skylark's Dispatch::Finalize only flushes the dedicated
		// `cookies` vector, not generic headers, when `redirect` is set) --
		// SetCookie populates that vector, so the cookie survives the
		// post-login redirect.
		http.SetCookie("wf_auth_session", sess.token, sess.expires_at, "/", NULL, false, true);
		return sess;
	}
	UserSession failed;
	failed.username = username;
	failed.is_authenticated = false;
	return failed;
}

void SystemAuthService::Logout(Http& http)
{
	String token = ExtractCookie(http, "wf_auth_session");
	if(!token.IsEmpty()) {
		Mutex::Lock lock(SessionMutex());
		Sessions().RemoveKey(token);
	}
	http.SetCookie("wf_auth_session", "", GetSysTime() - 3600, "/", NULL, false, true);
}

bool SystemAuthService::RequireAuth(Http& http)
{
	UserSession s = GetCurrentSession(http);
	if(!s.is_authenticated) {
		String uri = http.GetRequestContent();
		http.Redirect("/login");
		return false;
	}
	return true;
}

String SystemAuthService::RenderUserMenu(const UserSession& session)
{
	String h;
	if(session.is_authenticated) {
		String username = EscapeHtml(session.username);
		h << "<div style=\"display:flex; align-items:center; gap:10px;\">\n"
		  << "  <a href=\"" << (session.is_demo ? "/" : "/host/overview") << "\" style=\"display:flex; align-items:center; gap:6px; color:var(--wf-fg-normal,#ffffff); font-size:0.85rem; text-decoration:none;\">\n"
		  << "    <span style=\"width:24px; height:24px; border-radius:50%; background:linear-gradient(135deg,#00a1c5,#6d55aa); display:inline-flex; align-items:center; justify-content:center; font-size:0.75rem; font-weight:700; color:#fff;\">" << EscapeHtml(ToUpper(session.username.Left(1))) << "</span>\n"
		  << "    <span><strong>" << username << "</strong></span>\n"
		  << "  </a>\n"
		  << "  <a href=\"/logout\" class=\"wf-btn wf-btn-secondary\" style=\"padding:2px 8px; font-size:0.75rem;\">Log Out</a>\n"
		  << "</div>\n";
	} else {
		h << "<div style=\"display:flex; align-items:center; gap:8px;\">\n"
		  << "  <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.75rem;\">Guest</span>\n"
		  << "  <a href=\"/login\" class=\"wf-btn wf-btn-primary\" style=\"padding:3px 10px; font-size:0.78rem;\">Log In</a>\n"
		  << "</div>\n";
	}
	return h;
}

String SystemAuthService::RenderLoginForm(const String& redirect_url, const String& error_msg)
{
	String h;
	String mode = ConfiguredDemoSite() ? "Synthetic Demo Site Authentication" : "Site Account Authentication";
	String username_label = ConfiguredDemoSite() ? "Demo Username" : "Site Username";
	String password_label = ConfiguredDemoSite() ? "Demo Password" : "Site Password";
	String preset_username = ConfiguredDemoSite() ? "demo" : "";
	String helper = ConfiguredDemoSite() ? "Use the synthetic demo account: demo / demo" :
	                "Use an account from this site's auth.json. Configure one in the server terminal with --set-site-password USER (and the same --site-dir, if set).";
	h << "<div style=\"max-width:420px; margin:4rem auto; padding:0 1rem;\">\n"
	  << "  <div class=\"wf-card\">\n"
	  << "    <div class=\"wf-card-header\">\n"
	  << "      <span>" << EscapeHtml(mode) << "</span>\n"
	  << "      <span class=\"wf-badge wf-badge-emerald\">Site Configured</span>\n"
	  << "    </div>\n"
	  << "    <div class=\"wf-card-body\">\n"
	  << "      <div style=\"text-align:center; margin-bottom:1.5rem;\">\n"
	  << "        <div style=\"width:54px; height:54px; border-radius:50%; background:var(--wf-accent-focus,#00a1c5); display:inline-flex; align-items:center; justify-content:center; color:#fff; font-size:1.6rem; margin-bottom:10px;\">\n"
	  << "          " << VividIcon::Get("shield", 28) << "\n"
	  << "        </div>\n"
	  << "        <h3 style=\"margin:0;\">Sign in to ManagerSkylark</h3>\n"
	  << "        <p style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#8c929a); margin-top:4px;\">" << EscapeHtml(helper) << "</p>\n"
	  << "      </div>\n";

	if(!error_msg.IsEmpty()) {
		h << "      <div class=\"wf-alert wf-alert-danger\" style=\"margin-bottom:1rem;\">" << EscapeHtml(error_msg) << "</div>\n";
	}

	h << "      <form method=\"POST\" action=\"/login/submit\">\n"
	  << "        <input type=\"hidden\" name=\"redirect\" value=\"" << EscapeHtml(redirect_url) << "\">\n"
	  << "        <div style=\"margin-bottom:1rem;\">\n"
	  << "          <label style=\"display:block; font-size:0.85rem; font-weight:600; margin-bottom:4px;\">" << EscapeHtml(username_label) << "</label>\n"
	  << "          <input type=\"text\" name=\"username\" value=\"" << EscapeHtml(preset_username) << "\" required autofocus style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 12px; color:var(--wf-fg-normal,#e6edf3); font-size:0.9rem;\">\n"
	  << "        </div>\n"
	  << "        <div style=\"margin-bottom:1.5rem;\">\n"
	  << "          <label style=\"display:block; font-size:0.85rem; font-weight:600; margin-bottom:4px;\">" << EscapeHtml(password_label) << "</label>\n"
	  << "          <input type=\"password\" name=\"password\" required placeholder=\"••••••••\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 12px; color:var(--wf-fg-normal,#e6edf3); font-size:0.9rem;\">\n"
	  << "        </div>\n"
	  << "        <button type=\"submit\" class=\"wf-btn wf-btn-primary\" style=\"width:100%; padding:9px;\">Sign In</button>\n"
	  << "      </form>\n"
	  << "    </div>\n"
	  << "    <div class=\"wf-card-footer\" style=\"font-size:0.78rem; justify-content:center;\">\n"
	  << "      <span>Authenticated against site installation configuration</span>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";
	return h;
}

// ---------------------------------------------------------------------------
// SKYLARK Auth Handlers
// ---------------------------------------------------------------------------

SKYLARK(LoginPage, "login")
{
	String err = http["err"];
	String form = SystemAuthService::RenderLoginForm(http["redirect"], err);

	NavigationModel nav = ManagerSkylarkApp::BuildNavigation("login");
	String html = PageBuilder()
		.Title("Sign In - ManagerSkylark Site Auth")
		.Brand("ManagerSkylark", "/", "shield")
		.Navigation(nav)
		.Content(form)
		.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

SKYLARK(LoginPost, "login/submit:POST_RAW")
{
	String body = http.GetRequestContent();
	VectorMap<String, String> form;
	Vector<String> pairs = Split(body, '&');
	for(const String& p : pairs) {
		int eq = p.Find('=');
		if(eq >= 0)
			form.Add(UrlDecode(p.Left(eq)), UrlDecode(p.Mid(eq + 1)));
	}

	String user = form.Get("username", "");
	String pass = form.Get("password", "");
	String redir = form.Get("redirect", "/");
	if(redir.IsEmpty() || !redir.StartsWith("/") || redir.StartsWith("//") ||
	   redir.Find('\r') >= 0 || redir.Find('\n') >= 0 || redir.StartsWith("/login"))
		redir = "/";

	if(SystemAuthService::Login(http, user, pass)) {
		http.Redirect(redir);
	} else {
		http.Redirect("/login?err=" + UrlEncode("Authentication failed. Invalid site username or password."));
	}
}

SKYLARK(LogoutHandler, "logout")
{
	SystemAuthService::Logout(http);
	http.Redirect("/");
}

SKYLARK(ApiAuthStatus, "api/auth/status")
{
	UserSession s = SystemAuthService::GetCurrentSession(http);
	ValueMap m;
	m.Set("authenticated", s.is_authenticated);
	m.Set("username", s.username);
	m.Set("display_name", s.display_name);
	m.Set("github_user", s.github_username);
	m.Set("sf_user", s.sf_username);
	m.Set("is_admin", s.is_admin);
	http.ContentType("application/json") << AsJSON(m);
}

SKYLARK(ApiAuthLogin, "api/auth/login:POST_RAW")
{
	Value v = ParseJSON(http.GetRequestContent());
	if(v.Is<ValueMap>()) {
		String u = v["username"];
		String p = v["password"];
		UserSession s = SystemAuthService::LoginSession(http, u, p);
		if(s.is_authenticated) {
			ValueMap res;
			res.Set("status", "ok");
			res.Set("token", s.token);
			res.Set("username", s.username);
			res.Set("display_name", s.display_name);
			http.ContentType("application/json") << AsJSON(res);
			return;
		}
	}
	http.Response(401, "Unauthorized");
	http.ContentType("application/json") << "{\"error\":\"invalid_credentials\"}";
}

END_UPP_NAMESPACE
