#include "ManagerSkylark.h"
#include "SystemAuth.h"
#include "EngineComposition.h"
#include <WebEngine/SessionRoutes.h>
#include "PortalShell.h"
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

One<WebAuditLog>& PortalAudit()
{
	static One<WebAuditLog> log;
	return log;
}

struct RootAuditRequestContext {
	const Http *http = NULL;
	UserSession session;
	bool allowed = true;
};

thread__ RootAuditRequestContext t_root_audit_context;

StaticMutex& SessionMutex()
{
	static StaticMutex mutex;
	return mutex;
}

String NewSessionToken()
{
	return WebAuthRandomBytes(32, true);
}

String PortalUserId(const String& username)
{
	return username.IsEmpty() ? String() : "portal_u_" + SHA256String(username).Left(32);
}

void RecordPortalEvent(const String& kind, const String& route, const String& method,
                       const String& outcome, int status, const String& operation,
                       const UserSession *session = NULL)
{
	if(!PortalAudit()) return;
	WebAuditEvent e;
	e.at = GetSysTime(); e.site = "portal"; e.kind = kind; e.route = route;
	e.method = method; e.outcome = outcome; e.status = status; e.operation = operation;
	if(session && session->is_authenticated) {
		e.actor_user_id = PortalUserId(session->username);
		e.session_ref = session->audit_ref;
	}
	PortalAudit()->Record(e);
}

bool PortalSecureTransport(Http& http)
{
	TcpSocket *socket = http.GetSocket();
	if(socket && socket->IsSSL()) return true;
	if(!socket) return false;
	if(!IsTrustedManagerProxyPeer(socket->GetPeerAddr())) return false;
	return ToLower(TrimBoth(http.GetHeader("x-forwarded-proto"))) == "https";
}

}

void SystemAuthService::ConfigureSite(const String& site_dir, bool demo_site)
{
	ConfiguredSiteDir() = site_dir;
	ConfiguredDemoSite() = demo_site;
	// A request thread can still be recording into the previous log after its response was sent, so the previous
	// log is kept alive rather than deleted. Reconfiguring is rare (startup and tests), so the cost is small.
	static Vector<WebAuditLog *> retired;
	if(PortalAudit())
		retired.Add(PortalAudit().Detach());
	PortalAudit().Create("portal", AppendFileName(site_dir, "portal-audit"));
}

WebAuditLog& SystemAuthService::AuditLog()
{
	if(!PortalAudit())
		PortalAudit().Create("portal", AppendFileName(ConfiguredSiteDir(), "portal-audit"));
	return *PortalAudit();
}

StoreResult SystemAuthService::ListAuditSessions(int offset, int limit, Vector<PortalAuditSessionSnapshot>& out)
{
	out.Clear();
	if(offset < 0 || offset > 10000 || limit < 1 || limit > 200)
		return StoreResult("invalid snapshot limit");
	Vector<PortalAuditSessionSnapshot> active;
	Time now = GetSysTime();
	{
		Mutex::Lock lock(SessionMutex());
		for(int i = 0; i < Sessions().GetCount(); i++) {
			const UserSession& s = Sessions()[i];
			if(!s.is_authenticated || s.expires_at <= now || s.is_demo != ConfiguredDemoSite()) continue;
			PortalAuditSessionSnapshot snapshot;
			snapshot.user_id = PortalUserId(s.username);
			snapshot.session_ref = s.audit_ref;
			snapshot.created = s.created_at;
			snapshot.expires = s.expires_at;
			active.Add(pick(snapshot));
		}
	}
	for(int i = active.GetCount() - 1 - offset; i >= 0 && out.GetCount() < limit; i--)
		out.Add(active[i]);
	return StoreResult(true);
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

bool SystemAuthService::AuthorizeRequest(Http& http, const UserSession *resolved, int *rejection_status)
{
	if(rejection_status) *rejection_status = 0;
	String id = http.GetHandlerId();
	bool public_route = id == "LoginPage" || id == "LoginPost" || id == "LogoutHandler" || id == "HostSsoContinue" ||
	                    id == "PortalShellPage" || // the portal shell is public; gated webapps frame the login
	                    id == "PortalAuditPage" || id == "PortalAuditEvents" ||
	                    id == "PortalAuditSessions" || id == "PortalAuditHealth" ||
	                    id == "ApiHealth" || id == "ApiAuthStatus" || id == "ApiAuthLogin" ||
	                    id == "ApiCliManifest" || id == "ApiCliManifestSite" || // public, read-only command manifest
	                    IsEngineGatewayHandler(id); // engine sites authenticate with their own sessions
	if(public_route)
		return true;

	UserSession session = resolved ? *resolved : GetCurrentSession(http);
	if(!session.is_authenticated) {
		if(id.StartsWith("Api") || id.StartsWith("Manager")) {
			if(rejection_status) *rejection_status = 401;
			http.Content("application/json; charset=utf-8", "{\"error\":\"authentication_required\"}");
			http.Response(401, "Unauthorized");
		}
		else {
			// A page request returns to the page it asked for once signed in (task 035).
			String uri = http.GetRequestURI();
			bool back = http.GetMethod() == "GET" && uri.StartsWith("/") && !uri.StartsWith("//");
			if(rejection_status) *rejection_status = 302;
			http.Content("text/plain; charset=utf-8", "Authentication required");
			http.Response(302, "Found");
			http.Redirect(back ? "/login?next=" + UrlEncode(uri) : String("/login"));
		}
		return false;
	}

	if(ConfiguredDemoSite()) {
		bool read_only_page = id == "HomePage" || id == "ProjectsPage" || id == "ProjectDetailPage" ||
		                      id == "GoalsPage" || id == "GoalDetailPage" || id == "IssuesPage" ||
		                      id == "CalendarPage" ||
		                      id == "ReleasesPage" || id == "ApiProjects" || id == "ApiProjectDetail" ||
		                      id == "ApiNextTask" || id == "ApiGoals" || id == "ApiIssues" ||
		                      id == "ApiReleases" || id == "ManagerFrameworkSpec" ||
		                      id == "DataIndexPage" || id == "ApiDataSpec" || id == "ApiDataDocs" ||
	                      id == "ResearchIndexPage" || id == "ApiResearchPapers" ||
	                      id == "CandidateIndexPage" || id == "ApiCandidateLineage" ||
	                      id == "CandidateWorkflowPage" || id == "ApiCandidateWorkflow" ||
	                      id == "ReviewIndexPage" || id == "ReviewDetailPage" || id == "ApiReviewPackages" ||
	                      id == "ApiReviewPackage" || id == "ApiReviewRelated" ||
	                      id == "DocsPage" || id == "DocsRepoPage" || id == "DocsFilePage";
		if(http.GetMethod() != "GET" || !read_only_page) {
			if(rejection_status) *rejection_status = 403;
			http.Content("text/plain; charset=utf-8", "This synthetic demo site is read-only.");
			http.Response(403, "Forbidden");
			return false;
		}
	}
	return true;
}

void SystemAuthService::SetRootAuditContext(const Http& http, const UserSession& session, bool allowed)
{
	t_root_audit_context.http = &http;
	t_root_audit_context.session = session;
	t_root_audit_context.allowed = allowed;
}

void SystemAuthService::RecordRootRequestComplete(const Http& http, int duration_ms)
{
	String id = http.GetHandlerId();
	UserSession session;
	bool authorized = true;
	if(t_root_audit_context.http == &http) {
		session = t_root_audit_context.session;
		authorized = t_root_audit_context.allowed;
		t_root_audit_context = RootAuditRequestContext();
	}
	else {
		t_root_audit_context = RootAuditRequestContext();
		session = GetCurrentSession(http);
	}
	if(id == "PortalAuditEvents" || id == "PortalAuditSessions" || id == "PortalAuditHealth" ||
	   id == "PortalAuditPage")
		return;
	bool not_found = (id.IsEmpty() || IsEngineGatewayHandler(id)) && http.GetResponseCode() == 404;
	String route = not_found || id.GetCount() > 150 ? "host:unmatched" : "host:" + id;
	String method = http.GetMethod();
	if(method == "POST_RAW") method = "POST";
	if(method.GetCount() > 8) method = "OTHER";
	WebAuditEvent e;
	e.at = GetSysTime(); e.site = "portal"; e.kind = "request"; e.route = route;
	e.method = method;
	e.status = http.GetResponseCode();
	e.duration_ms = duration_ms < 0 ? 0 : duration_ms > 3600000 ? 3600000 : duration_ms;
	e.outcome = not_found ? "not_found" :
	            (!authorized ? "blocked" : e.status >= 500 ? "error" :
	             e.status >= 400 ? "http_error" : e.status >= 300 ? "redirect" : "ok");
	if(session.is_authenticated) e.actor_user_id = PortalUserId(session.username);
	if(!session.audit_ref.IsEmpty()) e.session_ref = session.audit_ref;
	AuditLog().Record(e);
	if(method == "POST" || method == "PUT" || method == "PATCH" || method == "DELETE") {
		e.kind = "action";
		AuditLog().Record(e);
	}
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

// A host account is a credential entry in auth.json. The bridge checks only that the name exists.
bool SystemAuthService::HasSiteAccount(const String& username)
{
	String conf_file = AppendFileName(ConfiguredSiteDir(), "auth.json");
	if(username.IsEmpty() || !FileExists(conf_file) || GetFileLength(conf_file) > 64 * 1024)
		return false;
	Value v = ParseJSON(LoadFile(conf_file));
	return v.Is<ValueMap>() && ((ValueMap)v).Find(username) >= 0;
}

// Set while the visitor has signed out of the portal, so a plain visit does not sign them back in.
static const char *HOST_SSO_SUPPRESS = "wf_host_sso_suppress";

void SystemAuthService::SetHostSsoSuppressed(Http& http, bool on)
{
	if(on)
		http.SetCookie(HOST_SSO_SUPPRESS, "1", GetSysTime() + 30 * 24 * 3600, "/", NULL, PortalSecureTransport(http), true);
	else
		http.SetCookie(HOST_SSO_SUPPRESS, "", GetSysTime() - 3600, "/", NULL, PortalSecureTransport(http), true);
}

UserSession SystemAuthService::BridgeMetaSession(Http& http, bool ignore_suppression, String* central_user)
{
	UserSession none;
	if(!TheWebEngine().MetaSso() || ConfiguredDemoSite())
		return none;
	if(!ignore_suppression && !ExtractCookie(http, HOST_SSO_SUPPRESS).IsEmpty())
		return none;
	String central = ExtractCookie(http, SsoCentralCookieName());
	String username;
	if(central.IsEmpty())
		return none;
	if(!SsoCentralUsername(central, username)) {
		RecordPortalEvent("session", "session:resolve", "GET", "failed_resolution", 0, "sso");
		return none;
	}
	if(central_user)
		*central_user = username;
	UserSession sess = AdoptHostAccount(username, "sso");
	if(sess.is_authenticated) {
		http.SetCookie("wf_auth_session", sess.token, sess.expires_at, "/", NULL, PortalSecureTransport(http), true);
	}
	else
		RecordPortalEvent("login", "host:sso_bridge", "GET", "failure", 401, "sso_no_account");
	return sess;
}

// A host session for an existing host account. It never creates an account.
UserSession SystemAuthService::AdoptHostAccount(const String& username, const String& source)
{
	if(!HasSiteAccount(username))
		return UserSession();
	UserSession session = CreateSession(username, source);
	if(session.is_authenticated && source == "sso")
		RecordPortalEvent("login", "host:sso_bridge", "GET", "success", 200, "sso", &session);
	return session;
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

UserSession SystemAuthService::CreateSession(const String& username, const String& source)
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
	s.audit_ref = WebAuditSessionRef();
	{
		Mutex::Lock lock(SessionMutex());
		Sessions().GetAdd(s.token) = s;
	}
	RecordPortalEvent("session", "session:create", "POST", "created", 0, source, &s);
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
		UserSession expired;
		bool found = false;
		{
			Mutex::Lock lock(SessionMutex());
			VectorMap<String, UserSession>& map = Sessions();
			int q = map.Find(token);
			if(q >= 0) {
				UserSession session = map[q];
				if(session.expires_at > GetSysTime() && session.is_demo == ConfiguredDemoSite())
					return session;
				expired = session;
				found = true;
				map.Remove(q);
			}
		}
		if(found)
			RecordPortalEvent("session", "session:resolve", "GET", "expired", 0, "host", &expired);
		else
			RecordPortalEvent("session", "session:resolve", "GET", "failed_resolution", 0, "host");
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
		UserSession sess = CreateSession(username, "password");
		if(!sess.is_authenticated)
			return sess;
		// SetHeader("Set-Cookie", ...) is silently dropped on a redirect
		// response (Skylark's Dispatch::Finalize only flushes the dedicated
		// `cookies` vector, not generic headers, when `redirect` is set) --
		// SetCookie populates that vector, so the cookie survives the
		// post-login redirect.
		http.SetCookie("wf_auth_session", sess.token, sess.expires_at, "/", NULL, PortalSecureTransport(http), true);
		RecordPortalEvent("login", "host:login", "POST", "success", 200, "password", &sess);
		return sess;
	}
	RecordPortalEvent("login", "host:login", "POST", "failure", 401, "password");
	UserSession failed;
	failed.username = username;
	failed.is_authenticated = false;
	return failed;
}

void SystemAuthService::Logout(Http& http)
{
	String token = ExtractCookie(http, "wf_auth_session");
	UserSession revoked;
	bool had_session = false;
	if(!token.IsEmpty()) {
		Mutex::Lock lock(SessionMutex());
		int q = Sessions().Find(token);
		if(q >= 0) { revoked = Sessions()[q]; had_session = true; }
		Sessions().RemoveKey(token);
	}
	if(had_session)
		RecordPortalEvent("session", "session:logout", "GET", "revoked", 302, "logout", &revoked);
	http.SetCookie("wf_auth_session", "", GetSysTime() - 3600, "/", NULL, PortalSecureTransport(http), true);
	// Signing out of the portal drops only the host session; the MetaAccount session is untouched (task 050).
	if(TheWebEngine().MetaSso())
		SetHostSsoSuppressed(http, true);
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
	  << (TheWebEngine().MetaSso() ? "        <p style=\"margin-top:0.75rem; text-align:center;\"><a href=\"/sso/meta?next=" + UrlEncode(redirect_url.IsEmpty() ? String("/") : redirect_url) + "\">Continue with MetaAccount</a></p>\n" : String())
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
	// The sign-in page is plain: the form and a brand line, with no navigator and no
	// webapp list. A framed sign-in returns to the page that asked for it (next).
	String next = http["next"];
	if(next.IsEmpty())
		next = http["redirect"];
	String form = SystemAuthService::RenderLoginForm(next, http["err"]);

	String html = PageBuilder()
		.Title("Sign In - ManagerSkylark Site Auth")
		.Brand("ManagerSkylark", "/", "shield")
		.Header("<div style=\"padding:12px 16px; font-weight:700;\">Manager Skylark</div>")
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
	bool redirect_rejected = redir.IsEmpty() || !redir.StartsWith("/") || redir.StartsWith("//") ||
	                        redir.Find('\\') >= 0 ||
	                        redir.Find('\r') >= 0 || redir.Find('\n') >= 0 || redir.StartsWith("/login");
	if(redirect_rejected)
		redir = "/";
	if(redirect_rejected)
		RecordPortalEvent("redirect", "host:redirect", "POST", "rejected", 302, "local");

	if(SystemAuthService::Login(http, user, pass)) {
		RecordPortalEvent("redirect", "host:redirect", "POST", "approved_local", 302, "local");
		http.Redirect(redir);
	} else {
		String back = "/login?err=" + UrlEncode("Authentication failed. Invalid site username or password.");
		if(redir != "/")
			back << "&next=" << UrlEncode(redir);
		RecordPortalEvent("redirect", "host:redirect", "POST", "approved_local", 302, "local");
		http.Redirect(back);
	}
}

// Portal sign-in through MetaAccount (task 050). The browser goes to MetaAccount when no central session
// exists, and MetaAccount returns here to the same route, which then bridges the session. Only a relative
// return path is accepted; no token ever travels in the URL.
SKYLARK(HostSsoContinue, "sso/meta")
{
	String next = http["next"];
	bool redirect_rejected = next.IsEmpty() || !next.StartsWith("/") || next.StartsWith("//") ||
	                         next.Find('\\') >= 0 || next.Find('\r') >= 0 || next.Find('\n') >= 0 ||
	                         next.StartsWith("/login") || next.StartsWith("/sso/meta");
	if(redirect_rejected)
		next = "/";
	if(redirect_rejected)
		RecordPortalEvent("redirect", "host:sso_redirect", "GET", "rejected", 302, "sso");
	if(!TheWebEngine().MetaSso()) {
		RecordPortalEvent("redirect", "host:sso_redirect", "GET", "approved_local", 302, "local");
		http.Redirect("/login");
		return;
	}
	SystemAuthService::SetHostSsoSuppressed(http, false);
	String central_user;
	UserSession s = SystemAuthService::BridgeMetaSession(http, true, &central_user);
	if(s.is_authenticated) {
		RecordPortalEvent("redirect", "host:sso_redirect", "GET", "approved_local", 302, "sso", &s);
		http.Redirect(next);
		return;
	}
	if(central_user.GetCount()) {
		RecordPortalEvent("redirect", "host:sso_redirect", "GET", "approved_local", 302, "sso");
		http.Redirect("/login?err=" + UrlEncode("MetaAccount signed in as " + central_user + ", but this portal has no account for it. Sign in with a portal account."));
		return;
	}
	EngineSite *meta_site = TheWebEngine().FindSite("meta");
	String portal_origin = TheWebEngine().PortalOrigin();
	if(!meta_site || portal_origin.IsEmpty()) {
		RecordPortalEvent("redirect", "host:sso_redirect", "GET", "approved_local", 302, "sso");
		http.Redirect("/login?err=" + UrlEncode("MetaAccount is not running here."));
		return;
	}
	String back = portal_origin + "/sso/meta?next=" + UrlEncode(next);
	String target = meta_site->PublicUrl("/login?next=" + UrlEncode(back));
	if(target.IsEmpty()) {
		RecordPortalEvent("redirect", "host:sso_redirect", "GET", "approved_local", 302, "sso");
		http.Redirect("/login?err=" + UrlEncode("MetaAccount public origin is unavailable."));
		return;
	}
	RecordPortalEvent("redirect", "host:sso_redirect", "GET", "approved_sso", 302, "sso");
	http.Redirect(target);
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

static void PortalAuditNoStore(Http& http)
{
	http.SetHeader("Cache-Control", "no-store, no-cache, must-revalidate");
	http.SetHeader("Pragma", "no-cache");
}

static bool PortalAuditAdmin(Http& http)
{
	UserSession session = SystemAuthService::GetCurrentSession(http);
	if(!session.is_authenticated) {
		http.Response(401, "Unauthorized");
		http.ContentType("application/json; charset=utf-8") << "{\"ok\":false,\"error\":\"authentication_required\"}";
		return false;
	}
	if(!session.is_admin) {
		http.Response(403, "Forbidden");
		http.ContentType("application/json; charset=utf-8") << "{\"ok\":false,\"error\":\"admin_required\"}";
		return false;
	}
	return true;
}

static bool PortalAuditInt(const String& value, int fallback, int min_value, int max_value, int& out)
{
	if(value.IsEmpty()) { out = fallback; return true; }
	if(value.GetCount() > 10) return false;
	for(int i = 0; i < value.GetCount(); i++)
		if(!IsDigit(value[i])) return false;
	out = ScanInt(value);
	return out >= min_value && out <= max_value;
}

SKYLARK(PortalAuditEvents, "api/audit/events")
{
	PortalAuditNoStore(http);
	if(!PortalAuditAdmin(http)) return;
	int offset, limit;
	if(!PortalAuditInt(http["offset"], 0, 0, 10000, offset) ||
	   !PortalAuditInt(http["limit"], 100, 1, 200, limit)) {
		http.Response(400, "Bad Request");
		http.ContentType("application/json; charset=utf-8") << "{\"ok\":false,\"error\":\"invalid_pagination\"}";
		return;
	}
	String from_arg = http["from"], to_arg = http["to"];
	Time to = to_arg.IsEmpty() ? GetSysTime() : ScanTime(~to_arg);
	Time from = from_arg.IsEmpty() ? to - 24 * 60 * 60 : ScanTime(~from_arg);
	Vector<ValueMap> rows;
	StoreResult result = SystemAuthService::AuditLog().List(from, to, offset, limit, rows);
	if(!result) {
		http.Response(result.error == "invalid audit range" ? 400 : 503,
		              result.error == "invalid audit range" ? "Bad Request" : "Unavailable");
		http.ContentType("application/json; charset=utf-8") << "{\"ok\":false,\"error\":\"audit_history_unavailable\"}";
		return;
	}
	ValueArray events;
	for(const ValueMap& row : rows) events.Add(row);
	ValueMap response; response.Add("ok", true); response.Add("site", "portal");
	response.Add("offset", offset); response.Add("limit", limit); response.Add("events", events);
	http.ContentType("application/json; charset=utf-8") << AsJSON(response, true);
}

SKYLARK(PortalAuditSessions, "api/audit/sessions")
{
	PortalAuditNoStore(http);
	if(!PortalAuditAdmin(http)) return;
	int offset, limit;
	if(!PortalAuditInt(http["offset"], 0, 0, 10000, offset) ||
	   !PortalAuditInt(http["limit"], 100, 1, 200, limit)) {
		http.Response(400, "Bad Request");
		http.ContentType("application/json; charset=utf-8") << "{\"ok\":false,\"error\":\"invalid_pagination\"}";
		return;
	}
	Vector<PortalAuditSessionSnapshot> snapshots;
	StoreResult result = SystemAuthService::ListAuditSessions(offset, limit, snapshots);
	if(!result) {
		http.Response(400, "Bad Request");
		http.ContentType("application/json; charset=utf-8") << "{\"ok\":false,\"error\":\"invalid_pagination\"}";
		return;
	}
	ValueArray sessions;
	for(const PortalAuditSessionSnapshot& s : snapshots) {
		ValueMap item; item.Add("user_id", s.user_id); item.Add("session_ref", s.session_ref);
		item.Add("created", AsString(s.created)); item.Add("expires", AsString(s.expires));
		sessions.Add(item);
	}
	ValueMap response; response.Add("ok", true); response.Add("site", "portal");
	response.Add("offset", offset); response.Add("limit", limit); response.Add("sessions", sessions);
	http.ContentType("application/json; charset=utf-8") << AsJSON(response, true);
}

SKYLARK(PortalAuditHealth, "api/audit/health")
{
	PortalAuditNoStore(http);
	if(!PortalAuditAdmin(http)) return;
	ValueMap response; response.Add("ok", true); response.Add("site", "portal");
	response.Add("audit_write_healthy", SystemAuthService::AuditLog().Healthy());
	response.Add("durable_ledger", "webstorage");
	response.Add("broker_diagnostics", "operational_only; communications_are_durable_in_WebAudit");
	http.ContentType("application/json; charset=utf-8") << AsJSON(response, true);
}

SKYLARK(PortalAuditPage, "audit")
{
	PortalAuditNoStore(http);
	if(!PortalAuditAdmin(http)) return;
	String scopes;
	for(const EngineSiteListing& site : TheWebEngine().ListSites())
		if(!site.public_url.IsEmpty())
			scopes << "<li><a rel=\"noopener\" href=\"" << EscapeHtml(site.public_url + "audit")
			       << "\">" << EscapeHtml(site.id) << " audit scope</a></li>";
	http.ContentType("text/html; charset=utf-8")
		<< "<!doctype html><meta charset=utf-8><title>Portal audit</title><h1>Portal audit: portal</h1>"
		   "<p>Host portal scope only. Composed-site scopes require authorization on each site.</p><ul>"
		<< scopes
		<< "</ul><h2>Diagnostics</h2><pre id=health></pre><h2>Active sessions</h2><pre id=sessions></pre>"
		   "<h2>Recent events</h2><pre id=events></pre><script>"
		   "for(const id of ['health','sessions','events']){const p=id==='health'?'api/audit/health':id==='sessions'?'api/audit/sessions':'api/audit/events';"
		   "fetch('/'+p,{cache:'no-store',credentials:'same-origin'}).then(r=>r.json()).then(x=>document.getElementById(id).textContent=JSON.stringify(x,null,2)).catch(()=>document.getElementById(id).textContent='Unavailable');}"
		   "</script>";
}

// Self-tests for the portal bridge's pure parts (task 050): the host account lookup and adoption, which
// never create an account, and the return-port allowance. The cookie and central-session halves run
// only on a live request and are covered by the scratch Aria matrix.
bool SystemAuthSelfTests(const String& scratch_dir)
{
	String dir = AppendFileName(scratch_dir, "bridge");
	RealizeDirectory(dir);
	String auth_file = AppendFileName(dir, "auth.json");
	SaveFile(auth_file, "{\"seppo\": {\"password_hash\": \"unused\"}}");
	String saved_dir = ConfiguredSiteDir();
	bool saved_demo = ConfiguredDemoSite();
	SystemAuthService::ConfigureSite(dir, false);

	int checks = 0, failed = 0;
	auto check = [&](bool ok, const char *what) {
		checks++;
		if(!ok) failed++;
		Cout() << (ok ? "ok   " : "FAIL ") << what << "\n";
	};
	{
		Http response;
		response.Content("text/plain", "completed-response-body");
		response.Response(503, "Unavailable");
		int code = response.GetResponseCode();
		String body = response.GetResponse();
		bool called = false;
		CallRequestCompleteObserver([&] { called = true; throw Exc("observer failure fixture"); });
		check(called && response.GetResponseCode() == code && response.GetResponse() == body,
		      "throwing completion observer is swallowed without changing the response object");
	}

	check(SystemAuthService::HasSiteAccount("seppo"), "a host account in auth.json is found by its username");
	UserSession adopted = SystemAuthService::AdoptHostAccount("seppo");
	check(adopted.is_authenticated && adopted.username == "seppo", "adopting an existing host account gives its session");
	UserSession sso_adopted = SystemAuthService::AdoptHostAccount("seppo", "sso");
	check(sso_adopted.is_authenticated && sso_adopted.audit_ref.StartsWith("as_") &&
	      sso_adopted.audit_ref != sso_adopted.token,
	      "SSO adoption carries a non-bearer opaque root-session reference");
	Vector<PortalAuditSessionSnapshot> host_sessions;
	StoreResult host_sessions_result = SystemAuthService::ListAuditSessions(0, 20, host_sessions);
	bool sso_snapshot_seen = false;
	for(const PortalAuditSessionSnapshot& session : host_sessions)
		sso_snapshot_seen |= session.session_ref == sso_adopted.audit_ref && session.user_id.StartsWith("portal_u_");
	Vector<ValueMap> portal_events;
	StoreResult portal_events_result = SystemAuthService::AuditLog().List(
		GetSysTime() - 60, GetSysTime() + 1, 0, 100, portal_events);
	ValueArray portal_event_values;
	bool sso_create_seen = false, sso_login_seen = false;
	for(const ValueMap& event : portal_events) {
		portal_event_values.Add(event);
		sso_create_seen |= (String)event.Get("kind") == "session" &&
		                   (String)event.Get("outcome") == "created" && (String)event.Get("operation") == "sso";
		sso_login_seen |= (String)event.Get("kind") == "login" &&
		                  (String)event.Get("route") == "host:sso_bridge" &&
		                  (String)event.Get("outcome") == "success";
	}
	String portal_event_json = AsJSON(portal_event_values, true);
	check(host_sessions_result.ok && sso_snapshot_seen && portal_events_result.ok && sso_create_seen && sso_login_seen &&
	      portal_event_json.Find(adopted.token) < 0 && portal_event_json.Find(sso_adopted.token) < 0 &&
	      portal_event_json.Find("seppo") < 0,
	      "portal root store/snapshots keep SSO session token and username out of audit records");
	check(!SystemAuthService::HasSiteAccount("mallory"), "a username with no host account is not found");
	UserSession refused = SystemAuthService::AdoptHostAccount("mallory");
	check(!refused.is_authenticated, "adopting a username with no host account gives no session");
	check(LoadFile(auth_file) == "{\"seppo\": {\"password_hash\": \"unused\"}}", "auth.json is unchanged: no account was created");
	check(!SystemAuthService::HasSiteAccount(""), "an empty username is not a host account");

	int saved_host = TheWebEngine().HostPort();
	TheWebEngine().SetHostPort(8081);
	check(SsoPortAllowed(8081), "the stock host port is an allowed MetaAccount return");
	check(!SsoPortAllowed(9999), "a port that is neither the host nor a composed site is refused");
	TheWebEngine().SetHostPort(saved_host);
	String previous_portal_origin = GetEnv("MANAGERSKYLARK_ORIGIN_PORTAL");
	SetEnv("MANAGERSKYLARK_ORIGIN_PORTAL", "https://portal.example:8443");
	String portal_origin, origin_error;
	check(ResolvePortalExternalOrigin("192.168.1.169", 8081, portal_origin, origin_error) &&
	      portal_origin == "https://portal.example:8443",
	      "portal SSO origin uses the configured HTTPS public origin");
	SetEnv("MANAGERSKYLARK_ORIGIN_PORTAL", "");
	check(ResolvePortalExternalOrigin("192.168.1.169", 8081, portal_origin, origin_error) &&
	      portal_origin == "http://192.168.1.169:8081",
	      "portal origin defaults to the public host, not loopback");
	String public_host, public_host_error;
	check(!ResolvePublicHost("localhost", public_host, public_host_error) &&
	      !ResolvePublicHost("127.0.0.1", public_host, public_host_error) &&
	      ResolvePublicHost("203.0.113.7", public_host, public_host_error) && public_host == "203.0.113.7",
	      "public host refuses loopback and accepts an explicit public address");
	SetEnv("MANAGERSKYLARK_ORIGIN_PORTAL", previous_portal_origin);
	String previous_proxy_peers = GetEnv("MANAGERSKYLARK_TRUSTED_PROXY_PEERS");
	SetEnv("MANAGERSKYLARK_TRUSTED_PROXY_PEERS", "127.0.0.1");
	check(IsTrustedManagerProxyPeer("127.0.0.1") && !IsTrustedManagerProxyPeer("127.0.0.2") &&
	      !IsTrustedManagerProxyPeer("proxy.example"),
	      "portal Secure-cookie proxy trust uses the same exact IPv4 peer allowlist");
	SetEnv("MANAGERSKYLARK_TRUSTED_PROXY_PEERS", previous_proxy_peers);

	SystemAuthService::ConfigureSite(saved_dir, saved_demo);
	Cout() << "\nSystem auth bridge self-tests: " << (checks - failed) << "/" << checks << "\n";
	return failed == 0;
}

END_UPP_NAMESPACE
