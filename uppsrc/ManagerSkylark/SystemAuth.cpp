#include "ManagerSkylark.h"
#include "SystemAuth.h"
#include <Core/SSL/SSL.h>

#ifdef PLATFORM_POSIX
#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#ifdef PLATFORM_WIN32
#include <windows.h>
#ifdef COMPILER_MSC
#pragma comment(lib, "Advapi32.lib")
#endif
#endif

NAMESPACE_UPP

namespace {

VectorMap<String, UserSession>& SessionMap()
{
	static VectorMap<String, UserSession> sessions;
	return sessions;
}

StaticMutex& SessionMutex()
{
	static StaticMutex mutex;
	return mutex;
}

bool SameOrigin(const Http& http)
{
	String host = http.GetHeader("host");
	String source = http.GetHeader("origin");
	if(source.IsEmpty()) source = http.GetHeader("referer");
	String fetch_site = ToLower(http.GetHeader("sec-fetch-site"));
	bool same_host = source == "http://" + host || source.StartsWith("http://" + host + "/") ||
	                 source == "https://" + host || source.StartsWith("https://" + host + "/");
	return !host.IsEmpty() && !source.IsEmpty() && same_host &&
	       (fetch_site.IsEmpty() || fetch_site == "same-origin");
}

struct LoginThrottleState {
	int failures = 0;
	Time window_start = Null;
	Time blocked_until = Null;
};

LoginThrottleState& LoginThrottle()
{
	static LoginThrottleState state;
	return state;
}

StaticMutex& LoginThrottleMutex()
{
	static StaticMutex mutex;
	return mutex;
}

bool LoginIsThrottled()
{
	Mutex::Lock lock(LoginThrottleMutex());
	Time now = GetSysTime();
	LoginThrottleState& state = LoginThrottle();
	if(state.blocked_until.IsValid() && state.blocked_until > now)
		return true;
	if(!state.window_start.IsValid() || now > state.window_start + 60) {
		state.failures = 0;
		state.window_start = now;
		state.blocked_until = Null;
	}
	return false;
}

void RecordLoginFailure()
{
	Mutex::Lock lock(LoginThrottleMutex());
	Time now = GetSysTime();
	LoginThrottleState& state = LoginThrottle();
	if(!state.window_start.IsValid() || now > state.window_start + 60) {
		state.failures = 0;
		state.window_start = now;
	}
	if(++state.failures >= 5) {
		state.blocked_until = now + 30;
		state.failures = 0;
		state.window_start = state.blocked_until;
	}
}

void ClearLoginFailures()
{
	Mutex::Lock lock(LoginThrottleMutex());
	LoginThrottle() = LoginThrottleState();
}

String RandomToken()
{
	try {
		SecureBuffer<byte> random = SecureRandom(32);
		if(random.IsEmpty()) return String();
		static const char hex[] = "0123456789abcdef";
		String token;
		token.Reserve(64);
		for(int i = 0; i < 32; i++) {
			token.Cat(hex[random[i] >> 4]);
			token.Cat(hex[random[i] & 15]);
		}
		return token;
	}
	catch(...) { return String(); }
}

String EscapeAuthHtml(const String& text)
{
	String out;
	for(int i = 0; i < text.GetCount(); i++) {
		switch(text[i]) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(text[i]); break;
		}
	}
	return out;
}

} // namespace

void RegisterManagerSkylarkHandler(void (*handler)(Http&), const char *id, const char *path,
                                   int (*progress)(int, Http&, int))
{
	RegisterHandler(handler, id, path, progress);
}

void RegisterManagerSkylarkProgressHandler(void (*handler)(Http&), const char *id, const char *path,
                                            int (*progress)(int, Http&, int))
{
	RegisterHandler(handler, id, path, progress);
}

String SystemAuthService::CurrentOsUsername()
{
#ifdef PLATFORM_POSIX
	struct passwd *pw = getpwuid(getuid());
	return pw ? String(pw->pw_name) : String();
#elif defined(PLATFORM_WIN32)
	char username[512];
	DWORD size = (DWORD)sizeof(username);
	if(!GetUserNameA(username, &size))
		return String();
	return String(username);
#else
	return String();
#endif
}

bool SystemAuthService::VerifySystemPassword(const String& username, const String& password)
{
	if(username.IsEmpty() || password.IsEmpty() || username != CurrentOsUsername())
		return false;
#if defined(PLATFORM_LINUX)
	const char *helper = FileExists("/usr/sbin/unix_chkpwd") ? "/usr/sbin/unix_chkpwd" :
	                     FileExists("/usr/bin/unix_chkpwd") ? "/usr/bin/unix_chkpwd" : NULL;
	if(!helper) {
		RLOG("Authentication unavailable: unix_chkpwd is not installed; login fails closed.");
		return false;
	}
	int input_pipe[2];
	int output_pipe[2];
	if(pipe(input_pipe) != 0) {
		RLOG("Authentication unavailable: cannot create unix_chkpwd pipes.");
		return false;
	}
	if(pipe(output_pipe) != 0) {
		close(input_pipe[0]);
		close(input_pipe[1]);
		RLOG("Authentication unavailable: cannot create unix_chkpwd pipes.");
		return false;
	}
	pid_t pid = fork();
	if(pid == 0) {
		close(input_pipe[1]);
		close(output_pipe[0]);
		dup2(input_pipe[0], STDIN_FILENO);
		dup2(output_pipe[1], STDOUT_FILENO);
		dup2(output_pipe[1], STDERR_FILENO);
		close(input_pipe[0]);
		close(output_pipe[1]);
		execl(helper, "unix_chkpwd", ~username, "nullok", (char *)NULL);
		_exit(127);
	}
	close(input_pipe[0]);
	close(output_pipe[1]);
	if(pid < 0) {
		close(input_pipe[1]);
		close(output_pipe[0]);
		RLOG("Authentication unavailable: could not start unix_chkpwd.");
		return false;
	}
	String input = password + "\n";
	int written = 0;
	while(written < input.GetCount()) {
		int n = (int)write(input_pipe[1], input.Begin() + written, input.GetCount() - written);
		if(n <= 0) break;
		written += n;
	}
	close(input_pipe[1]);
	char response[128];
	bool timed_out = false;
	bool pipe_failed = false;
	for(;;) {
		struct pollfd pfd;
		pfd.fd = output_pipe[0];
		pfd.events = POLLIN | POLLHUP;
		pfd.revents = 0;
		int ready = poll(&pfd, 1, 5000);
		if(ready < 0 && errno == EINTR)
			continue;
		if(ready <= 0) {
			timed_out = true;
			break;
		}
		int n = (int)read(output_pipe[0], response, sizeof(response));
		if(n == 0)
			break;
		if(n < 0 && errno == EINTR)
			continue;
		if(n < 0) {
			pipe_failed = true;
			break;
		}
	}
	close(output_pipe[0]);
	if(timed_out || pipe_failed)
		kill(pid, SIGKILL);
	int status = 0;
	pid_t waited;
	do {
		waited = waitpid(pid, &status, 0);
	} while(waited < 0 && errno == EINTR);
	return !timed_out && !pipe_failed && waited == pid && written == input.GetCount() && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#elif defined(PLATFORM_WIN32)
	{
		HANDLE token = NULL;
		BOOL verified = LogonUserA(~username, ".", ~password, LOGON32_LOGON_INTERACTIVE,
		                           LOGON32_PROVIDER_DEFAULT, &token);
		if(token)
			CloseHandle(token);
		return verified != 0;
	}
#else
	RLOG("Authentication unavailable: OS password verification is unsupported on this platform.");
	return false;
#endif
}

UserSession SystemAuthService::CreateSession(const String& username)
{
	if(username.IsEmpty() || username != CurrentOsUsername()) return UserSession();
	UserSession session;
	session.username = username;
	session.display_name = username;
	session.home_dir = GetHomeDirectory();
	session.created_at = GetSysTime();
	session.expires_at = session.created_at + 8 * 3600;
	session.is_authenticated = true;
	// The only accepted identity is the current OS account: this initial
	// deployment mode represents one local operator, not arbitrary admins.
	session.is_admin = true;
	session.token = RandomToken();
	if(session.token.IsEmpty()) return UserSession();
	Mutex::Lock lock(SessionMutex());
	for(int i = SessionMap().GetCount() - 1; i >= 0; i--)
		if(SessionMap()[i].expires_at <= session.created_at) SessionMap().Remove(i);
	while(SessionMap().GetCount() >= 32) {
		int oldest = 0;
		for(int i = 1; i < SessionMap().GetCount(); i++)
			if(SessionMap()[i].created_at < SessionMap()[oldest].created_at)
				oldest = i;
		SessionMap().Remove(oldest);
	}
	SessionMap().GetAdd(session.token) = session;
	return session;
}

UserSession SystemAuthService::CreateTestSessionForTests()
{
	if(!ManagerSkylarkApp::IsolatedTestModeActive()) return UserSession();
	UserSession session;
	session.username = "synthetic-test-operator";
	session.display_name = "Synthetic Test Operator";
	session.created_at = GetSysTime();
	session.expires_at = session.created_at + 3600;
	session.is_authenticated = true;
	session.is_admin = true;
	session.token = RandomToken();
	if(session.token.IsEmpty()) return UserSession();
	Mutex::Lock lock(SessionMutex());
	SessionMap().GetAdd(session.token) = session;
	return session;
}

ManagerSkylarkApp::BatchFinalizationResult SystemAuthService::FinalizeBatchForSession(
	ManagerSkylarkApp& app, const UserSession& session, const String& source_id,
	const String& workspace_id, const String& operation_id,
	const Vector<DataBatchChange>& changes)
{
	ManagerSkylarkApp::BatchFinalizationResult denied;
	denied.operation_id = operation_id;
	denied.status = "authorization_denied";
	if(session.token.IsEmpty() || source_id.IsEmpty() || workspace_id.IsEmpty()) return denied;

	UserSession live;
	{
		Mutex::Lock lock(SessionMutex());
		int q = SessionMap().Find(session.token);
		if(q < 0) return denied;
		if(SessionMap()[q].expires_at <= GetSysTime()) {
			SessionMap().Remove(q);
			return denied;
		}
		live = SessionMap()[q];
	}
	if(!live.is_authenticated || !live.is_admin || live.username.IsEmpty() ||
	   session.username != live.username ||
	   session.is_authenticated != live.is_authenticated || session.is_admin != live.is_admin ||
	   session.created_at != live.created_at || session.expires_at != live.expires_at) return denied;

	DataSourceRegistration registration;
	if(!app.ReadCurrentCatalogRegistration(registration) || !registration.enabled ||
	   registration.id != source_id || registration.read_only || !registration.write_enabled)
		return denied;

	CatalogReadHandle catalog = app.PinLatestCatalog();
	if(!catalog || catalog->status != "loaded" || catalog->data.GetSourceId() != source_id)
		return denied;
	bool workspace_found = false;
	for(const DataDocument& document : catalog->data.GetDocuments()) {
		if(document.workspace_id != workspace_id || document.source_id != source_id) continue;
		workspace_found = true;
		if(!document.valid || !document.validated) return denied;
	}
	if(!workspace_found) return denied;

	DataWriteAuthorization authorization;
	authorization.authenticated = true;
	authorization.action_scope = "record:write";
	authorization.principal = live.username;
	authorization.source_id = registration.id;
	authorization.workspace_id = workspace_id;
	return app.FinalizeBatch(authorization, operation_id, changes);
}

bool SystemAuthService::AuthorizeWriteForSession(ManagerSkylarkApp& app, const UserSession& session,
                                                  const String& source_id, const String& workspace_id)
{
	if(session.token.IsEmpty() || source_id.IsEmpty() || workspace_id.IsEmpty()) return false;
	UserSession live;
	{
		Mutex::Lock lock(SessionMutex());
		int q = SessionMap().Find(session.token);
		if(q < 0) return false;
		if(SessionMap()[q].expires_at <= GetSysTime()) { SessionMap().Remove(q); return false; }
		live = SessionMap()[q];
	}
	if(!live.is_authenticated || !live.is_admin || live.username.IsEmpty() ||
	   session.username != live.username || session.is_authenticated != live.is_authenticated ||
	   session.is_admin != live.is_admin || session.created_at != live.created_at ||
	   session.expires_at != live.expires_at) return false;
	DataSourceRegistration registration;
	if(!app.ReadCurrentCatalogRegistration(registration) || !registration.enabled ||
	   registration.id != source_id || registration.read_only || !registration.write_enabled) return false;
	CatalogReadHandle catalog = app.PinLatestCatalog();
	if(!catalog || catalog->status != "loaded" || catalog->data.GetSourceId() != source_id) return false;
	bool found = false;
	for(const DataDocument& document : catalog->data.GetDocuments()) {
		if(document.workspace_id != workspace_id || document.source_id != source_id) continue;
		found = true;
		if(!document.valid || !document.validated) return false;
	}
	return found;
}

bool SystemAuthService::AuthorizeWriteForSession(ManagerSkylarkApp& app, const UserSession& session,
	                                              const String& workspace_id)
{
	if(session.token.IsEmpty() || workspace_id.IsEmpty()) return false;
	UserSession live;
	{
		Mutex::Lock lock(SessionMutex());
		int q = SessionMap().Find(session.token);
		if(q < 0) return false;
		if(SessionMap()[q].expires_at <= GetSysTime()) { SessionMap().Remove(q); return false; }
		live = SessionMap()[q];
	}
	if(!live.is_authenticated || !live.is_admin || live.username.IsEmpty() ||
	   session.username != live.username || session.is_authenticated != live.is_authenticated ||
	   session.is_admin != live.is_admin || session.created_at != live.created_at ||
	   session.expires_at != live.expires_at) return false;
	DataSourceRegistration registration;
	if(!app.ReadCurrentCatalogRegistration(registration) || !registration.enabled ||
	   registration.id.IsEmpty() || registration.read_only || !registration.write_enabled) return false;
	CatalogReadHandle catalog = app.PinLatestCatalog();
	if(!catalog || catalog->status != "loaded" || catalog->data.GetSourceId() != registration.id) return false;
	bool found = false;
	for(const DataDocument& document : catalog->data.GetDocuments()) {
		if(document.workspace_id != workspace_id || document.source_id != registration.id) continue;
		found = true;
		if(!document.valid || !document.validated) return false;
	}
	return found;
}

void SystemAuthService::ExpireSessionForTest(const String& token)
{
	if(!ManagerSkylarkApp::IsolatedTestModeActive()) return;
	Mutex::Lock lock(SessionMutex());
	int q = SessionMap().Find(token);
	if(q >= 0) SessionMap()[q].expires_at = GetSysTime() - 1;
}

UserSession SystemAuthService::GetCurrentSession(const Http& http)
{
	if(ManagerSkylarkApp::IsolatedTestModeActive()) {
		String test_token = ExtractCookie(http, "wf_auth_session");
		if(test_token.IsEmpty()) {
			String auth = http.GetHeader("authorization");
			if(auth.StartsWith("Bearer ")) test_token = TrimBoth(auth.Mid(7));
		}
		if(!test_token.IsEmpty()) {
			Mutex::Lock lock(SessionMutex());
			int q = SessionMap().Find(test_token);
			if(q >= 0 && SessionMap()[q].expires_at > GetSysTime()) return SessionMap()[q];
		}
		UserSession test;
		test.username = "test-operator";
		test.display_name = "Isolated Test Operator";
		test.is_authenticated = true;
		test.is_admin = true;
		return test;
	}
	String token = ExtractCookie(http, "wf_auth_session");
	if(token.IsEmpty()) {
		String auth = http.GetHeader("authorization");
		if(auth.StartsWith("Bearer ")) token = TrimBoth(auth.Mid(7));
	}
	UserSession guest;
	guest.username = "guest";
	guest.display_name = "Guest";
	Mutex::Lock lock(SessionMutex());
	int q = SessionMap().Find(token);
	if(q < 0) return guest;
	if(SessionMap()[q].expires_at <= GetSysTime()) {
		SessionMap().Remove(q);
		return guest;
	}
	return SessionMap()[q];
}

bool SystemAuthService::Login(Http& http, const String& username, const String& password)
{
	return LoginSession(http, username, password).is_authenticated;
}

UserSession SystemAuthService::LoginSession(Http& http, const String& username, const String& password)
{
	if(!SameOrigin(http) || LoginIsThrottled())
		return UserSession();
	if(!VerifySystemPassword(username, password)) {
		RecordLoginFailure();
		return UserSession();
	}
	ClearLoginFailures();
	UserSession session = CreateSession(username);
	if(!session.is_authenticated) return session;
	// Emit the application-owned cookie on a normal response so that SameSite
	// survives Skylark's redirect serializer, which only emits its cookie jar.
	http.SetHeader("Set-Cookie", "wf_auth_session=" + UrlEncode(session.token) +
	              "; Path=/; HttpOnly; SameSite=Lax; Max-Age=28800");
	return session;
}

void SystemAuthService::Logout(Http& http)
{
	String token = ExtractCookie(http, "wf_auth_session");
	if(token.IsEmpty()) {
		String auth = http.GetHeader("authorization");
		if(auth.StartsWith("Bearer ")) token = TrimBoth(auth.Mid(7));
	}
	{
		Mutex::Lock lock(SessionMutex());
		SessionMap().RemoveKey(token);
	}
	http.SetHeader("Set-Cookie", "wf_auth_session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
}

bool SystemAuthService::RequireAuth(Http& http)
{
	return AuthorizeRequest(http);
}

bool SystemAuthService::AuthorizeRequest(Http& http, const char *route_path)
{
	String id = http.GetHandlerId();
	bool is_public = (id == "LoginPage" && route_path && String(route_path) == "login") ||
	                 (id == "LoginPost" && route_path && String(route_path) == "login/submit:POST_RAW") ||
	                 (id == "ApiAuthStatus" && route_path && String(route_path) == "api/auth/status") ||
	                 (id == "ApiAuthLogin" && route_path && String(route_path) == "api/auth/login:POST_RAW") ||
	                 (id == "ApiHealth" && route_path && String(route_path) == "api/health");
	if(is_public) {
		if(id != "ApiHealth")
			http.SetHeader("Cache-Control", "no-store");
		return true;
	}
	if(route_path && String(route_path).Find(":POST") >= 0 && !SameOrigin(http)) {
		http.Response(403, "Forbidden");
		http.ContentType("application/json");
		http.SetHeader("Cache-Control", "no-store");
		return false;
	}
	if(GetCurrentSession(http).is_authenticated) {
		http.SetHeader("Cache-Control", "no-store");
		return true;
	}
	http.SetHeader("Cache-Control", "no-store");
	if(id.StartsWith("Api") || (route_path && String(route_path).StartsWith("api/"))) {
		http.Response(401, "Unauthorized");
		http.ContentType("application/json");
		http << "{\"error\":\"authentication_required\"}";
	}
	else
		http.Redirect("/login");
	return false;
}

String SystemAuthService::ExtractCookie(const Http& http, const char *name)
{
	Vector<String> cookies = Split(http.GetHeader("cookie"), ';');
	String prefix = String(name) + "=";
	for(const String& cookie : cookies) {
		String value = TrimBoth(cookie);
		if(value.StartsWith(prefix)) return value.Mid(prefix.GetCount());
	}
	return String();
}

String SystemAuthService::RenderUserMenu(const UserSession& session)
{
	if(!session.is_authenticated)
		return "<a href=\"/login\" class=\"wf-btn wf-btn-primary\">Log In</a>";
	return "<span>" + EscapeAuthHtml(session.username) + "</span> <form method=\"POST\" action=\"/logout\" style=\"display:inline\"><button type=\"submit\" class=\"wf-btn\">Log out</button></form>";
}

String SystemAuthService::RenderLoginForm(const String&, const String& error_msg)
{
	String user = CurrentOsUsername();
	String html;
	html << "<main><h1>Sign in to ManagerSkylark</h1>"
	     << "<p>Authenticate as the operating-system account <strong>" << EscapeAuthHtml(user) << "</strong>.</p>";
	if(!error_msg.IsEmpty()) html << "<p role=\"alert\">" << EscapeAuthHtml(error_msg) << "</p>";
	html << "<form method=\"POST\" action=\"/login/submit\"><input type=\"hidden\" name=\"username\" value=\""
	     << EscapeAuthHtml(user) << "\"><label>Password <input type=\"password\" name=\"password\" required></label>"
	     << "<button type=\"submit\">Sign in</button></form></main>";
	return html;
}

SKYLARK(LoginPage, "login")
{
	String html = PageBuilder().Title("Sign In - ManagerSkylark").Brand("ManagerSkylark", "/", "shield")
		.TrustedContent(SystemAuthService::RenderLoginForm("", http["err"])).Build();
	http.ContentType("text/html; charset=utf-8") << html;
}

SKYLARK(LoginPost, "login/submit:POST_RAW")
{
	VectorMap<String, String> form;
	for(const String& pair : Split(http.GetRequestContent(), '&')) {
		int eq = pair.Find('=');
		if(eq >= 0) form.Add(UrlDecode(pair.Left(eq)), UrlDecode(pair.Mid(eq + 1)));
	}
	if(SystemAuthService::Login(http, form.Get("username", ""), form.Get("password", ""))) {
		http.ContentType("text/html; charset=utf-8");
		http << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Signed in</title></head>"
		        "<body><p>Sign-in succeeded. <a href=\"/\">Continue to the workspace</a>.</p></body></html>";
	}
	else
		http.Redirect("/login?err=" + UrlEncode("Authentication failed or system password verification is unavailable."));
}

SKYLARK(LogoutHandler, "logout:POST_RAW")
{
	SystemAuthService::Logout(http);
	http.ContentType("text/html; charset=utf-8");
	http << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Signed out</title></head>"
	        "<body><p>Signed out. <a href=\"/login\">Sign in</a>.</p></body></html>";
}

SKYLARK(ApiAuthStatus, "api/auth/status")
{
	UserSession session = SystemAuthService::GetCurrentSession(http);
	ValueMap response;
	response.Set("authenticated", session.is_authenticated);
	if(session.is_authenticated) response.Set("username", session.username);
	http.ContentType("application/json") << AsJSON(response);
}

SKYLARK(ApiAuthLogin, "api/auth/login:POST_RAW")
{
	Value value = ParseJSON(http.GetRequestContent());
	if(value.Is<ValueMap>()) {
		UserSession session = SystemAuthService::LoginSession(http, value["username"], value["password"]);
		if(session.is_authenticated) {
			ValueMap response;
			response.Set("status", "ok");
			response.Set("username", session.username);
			http.ContentType("application/json") << AsJSON(response);
			return;
		}
	}
	http.Response(401, "Unauthorized");
	http.ContentType("application/json") << "{\"error\":\"invalid_credentials\"}";
}

END_UPP_NAMESPACE
