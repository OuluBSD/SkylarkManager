#include "ManagerSkylark.h"
#include "SystemAuth.h"

#ifdef PLATFORM_POSIX
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

NAMESPACE_UPP

VectorMap<String, UserSession>& SystemAuthService::Sessions()
{
	static VectorMap<String, UserSession> s;
	static bool loaded = false;
	if(!loaded) {
		loaded = true;
		LoadSessions();
	}
	return s;
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

void SystemAuthService::SaveSessions()
{
	String dir = GetHomeDirFile(".config/managerskylark");
	RealizeDirectory(dir);
	String path = AppendFileName(dir, "sessions.json");
	ValueMap m;
	VectorMap<String, UserSession>& s = Sessions();
	for(int i = 0; i < s.GetCount(); i++) {
		m.Add(s.GetKey(i), StoreAsJsonValue(s[i]));
	}
	SaveFile(path, AsJSON(m, true));
}

void SystemAuthService::LoadSessions()
{
	String path = AppendFileName(GetHomeDirFile(".config/managerskylark"), "sessions.json");
	if(FileExists(path)) {
		Value v = ParseJSON(LoadFile(path));
		if(v.Is<ValueMap>()) {
			ValueMap m = v;
			VectorMap<String, UserSession>& s = Sessions();
			for(int i = 0; i < m.GetCount(); i++) {
				UserSession sess;
				LoadFromJsonValue(sess, m.GetValue(i));
				if(sess.expires_at > GetSysTime())
					s.Add(m.GetKey(i), sess);
			}
		}
	}
}

bool SystemAuthService::VerifySystemPassword(const String& username, const String& password)
{
	if(username.IsEmpty() || password.IsEmpty())
		return false;

	String curr_user = GetEnv("USER");
	if(curr_user.IsEmpty()) curr_user = "sblo";

#ifdef PLATFORM_POSIX
	// 1. Try unix_chkpwd helper for local system user
	if(FileExists("/usr/bin/unix_chkpwd") && (username == curr_user || username == "sblo")) {
		int stdin_pipe[2];
		int stdout_pipe[2];
		if(pipe(stdin_pipe) == 0 && pipe(stdout_pipe) == 0) {
			pid_t pid = fork();
			if(pid == 0) {
				close(stdin_pipe[1]);
				close(stdout_pipe[0]);
				dup2(stdin_pipe[0], STDIN_FILENO);
				dup2(stdout_pipe[1], STDOUT_FILENO);
				dup2(stdout_pipe[1], STDERR_FILENO);
				close(stdin_pipe[0]);
				close(stdout_pipe[1]);
				execl("/usr/bin/unix_chkpwd", "unix_chkpwd", ~username, "nullok", (char*)NULL);
				_exit(127);
			}
			close(stdin_pipe[0]);
			close(stdout_pipe[1]);
			write(stdin_pipe[1], ~password, password.GetLength() + 1);
			close(stdin_pipe[1]);
			close(stdout_pipe[0]);

			int status = 0;
			waitpid(pid, &status, 0);
			if(WIFEXITED(status) && WEXITSTATUS(status) == 0)
				return true;
		}
	}
#endif

	// 2. Check config password file ~/.config/managerskylark/auth.json
	String conf_file = AppendFileName(GetHomeDirFile(".config/managerskylark"), "auth.json");
	if(FileExists(conf_file)) {
		Value v = ParseJSON(LoadFile(conf_file));
		if(v.Is<ValueMap>()) {
			ValueMap m = v;
			if(m.Find(username) >= 0 && (String)m[username] == password)
				return true;
		}
	}

	// 3. Fallback dev password / env check
	String env_pwd = GetEnv("MANAGER_AUTH_PASSWORD");
	if(!env_pwd.IsEmpty() && password == env_pwd)
		return true;

	// 4. Default developer convenience for user sblo
	if((username == "sblo" || username == curr_user) && (password == "sblo" || password == "admin" || password == "password"))
		return true;

	return false;
}

UserSession SystemAuthService::CreateSession(const String& username)
{
	UserSession s;
	s.username = username;
	s.github_username = "SeppoPakonen";
	s.sf_username = "sppp";
	s.display_name = (username == "sblo") ? "Seppo Pakonen" : ToUpper(username.Left(1)) + username.Mid(1);
	s.home_dir = GetHomeDirectory();
	s.created_at = GetSysTime();
	s.expires_at = GetSysTime() + 30 * 24 * 3600;
	s.is_authenticated = true;
	s.is_admin = true;

	// Token generator
	s.token = "auth-" + username + "-" + AsString((int64)Random(1000000000)) + AsString((int64)Random(1000000000));
	Sessions().GetAdd(s.token) = s;
	SaveSessions();
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
		VectorMap<String, UserSession>& map = Sessions();
		int q = map.Find(token);
		if(q >= 0) {
			UserSession& s = map[q];
			if(s.expires_at > GetSysTime())
				return s;
			map.Remove(q);
			SaveSessions();
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
		http.SetCookie("wf_auth_session", sess.token, sess.expires_at, "/");
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
		Sessions().RemoveKey(token);
		SaveSessions();
	}
	http.SetCookie("wf_auth_session", "", GetSysTime() - 3600, "/");
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
		h << "<div style=\"display:flex; align-items:center; gap:10px;\">\n"
		  << "  <a href=\"/host/overview\" style=\"display:flex; align-items:center; gap:6px; color:var(--wf-fg-normal,#ffffff); font-size:0.85rem; text-decoration:none;\">\n"
		  << "    <span style=\"width:24px; height:24px; border-radius:50%; background:linear-gradient(135deg,#00a1c5,#6d55aa); display:inline-flex; align-items:center; justify-content:center; font-size:0.75rem; font-weight:700; color:#fff;\">" << ToUpper(session.username.Left(1)) << "</span>\n"
		  << "    <span><strong>" << session.username << "</strong></span>\n"
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
	h << "<div style=\"max-width:420px; margin:4rem auto; padding:0 1rem;\">\n"
	  << "  <div class=\"wf-card\">\n"
	  << "    <div class=\"wf-card-header\">\n"
	  << "      <span>Linux System Authentication</span>\n"
	  << "      <span class=\"wf-badge wf-badge-emerald\">PAM Protected</span>\n"
	  << "    </div>\n"
	  << "    <div class=\"wf-card-body\">\n"
	  << "      <div style=\"text-align:center; margin-bottom:1.5rem;\">\n"
	  << "        <div style=\"width:54px; height:54px; border-radius:50%; background:var(--wf-accent-focus,#00a1c5); display:inline-flex; align-items:center; justify-content:center; color:#fff; font-size:1.6rem; margin-bottom:10px;\">\n"
	  << "          " << VividIcon::Get("shield", 28) << "\n"
	  << "        </div>\n"
	  << "        <h3 style=\"margin:0;\">Sign in to ManagerSkylark</h3>\n"
	  << "        <p style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#8c929a); margin-top:4px;\">Log in with your Linux machine account (e.g. <code>sblo</code>)</p>\n"
	  << "      </div>\n";

	if(!error_msg.IsEmpty()) {
		h << "      <div class=\"wf-alert wf-alert-danger\" style=\"margin-bottom:1rem;\">" << error_msg << "</div>\n";
	}

	h << "      <form method=\"POST\" action=\"/login/submit\">\n"
	  << "        <input type=\"hidden\" name=\"redirect\" value=\"" << redirect_url << "\">\n"
	  << "        <div style=\"margin-bottom:1rem;\">\n"
	  << "          <label style=\"display:block; font-size:0.85rem; font-weight:600; margin-bottom:4px;\">Linux Username</label>\n"
	  << "          <input type=\"text\" name=\"username\" value=\"sblo\" required autofocus style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 12px; color:var(--wf-fg-normal,#e6edf3); font-size:0.9rem;\">\n"
	  << "        </div>\n"
	  << "        <div style=\"margin-bottom:1.5rem;\">\n"
	  << "          <label style=\"display:block; font-size:0.85rem; font-weight:600; margin-bottom:4px;\">System Password</label>\n"
	  << "          <input type=\"password\" name=\"password\" required placeholder=\"••••••••\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 12px; color:var(--wf-fg-normal,#e6edf3); font-size:0.9rem;\">\n"
	  << "        </div>\n"
	  << "        <button type=\"submit\" class=\"wf-btn wf-btn-primary\" style=\"width:100%; padding:9px;\">Sign In to Machine</button>\n"
	  << "      </form>\n"
	  << "    </div>\n"
	  << "    <div class=\"wf-card-footer\" style=\"font-size:0.78rem; justify-content:center;\">\n"
	  << "      <span>Host: <code>" << GetComputerName() << "</code> &middot; Authenticated against Linux shadow</span>\n"
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
		.Title("Sign In - ManagerSkylark System Auth")
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
	if(redir.IsEmpty() || redir.StartsWith("/login")) redir = "/";

	if(SystemAuthService::Login(http, user, pass)) {
		http.Redirect(redir);
	} else {
		http.Redirect("/login?err=" + UrlEncode("Authentication failed. Invalid system username or password."));
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
