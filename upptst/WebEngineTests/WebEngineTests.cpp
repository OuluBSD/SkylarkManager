#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include <WebEngine/WebEngine.h>

using namespace Upp;

// ---------------------------------------------------------------------------
// WebEngine fence self-test.
//
// One process hosts three fenced sites (shell on the stock port, alpha and
// beta on extra ports) with IDENTICAL route paths and the same usernames.
// Raw HTTP assertions verify that nothing leaks across sites, that mutations
// are fenced by exact-origin + CSRF, and that cross-site work flows through
// the observable WebBroker.
// ---------------------------------------------------------------------------

static const int MAIN_PORT  = 18200;
static const int ALPHA_PORT = 18201;
static const int BETA_PORT  = 18202;

// ---------------------------------------------------------------------------
// Minimal raw HTTP client (Connection: close)
// ---------------------------------------------------------------------------

struct HttpReply {
	int    status = 0;
	String body;
	int    content_length = -1;
	Vector<String> set_cookies;
};

static HttpReply HttpCallOnce(const String& method, int port, const String& path,
                              const VectorMap<String, String>& headers, const String& body,
                              const char *content_type, String& connect_error)
{
	HttpReply r;
	TcpSocket s;
	s.Timeout(10000);
	if(!s.Connect("127.0.0.1", port)) {
		connect_error = "connect failed: " + s.GetErrorDesc();
		return r;
	}
	StringBuffer req;
	req << method << ' ' << path << " HTTP/1.1\r\n";
	req << "Host: 127.0.0.1:" << port << "\r\n";
	req << "Connection: close\r\n";
	req << "Content-Type: " << content_type << "\r\n";
	for(int i = 0; i < headers.GetCount(); i++)
		req << headers.GetKey(i) << ": " << headers[i] << "\r\n";
	if(body.GetCount())
		req << "Content-Length: " << body.GetCount() << "\r\n";
	req << "\r\n" << body;
	s.Put(req);

	StringBuffer all;
	String head;
	bool head_done = false;
	String data;
	while((data = s.Get(4096)).GetCount()) {
		if(!head_done) {
			head << data;
			int q = head.Find("\r\n\r\n");
			if(q >= 0) {
				head_done = true;
				all << head.Mid(q + 4);
				head = head.Mid(0, q);
			}
		}
		else
			all << data;
	}
	Vector<String> lines = Split(head, '\n');
	if(lines.GetCount()) {
		Vector<String> st = Split(lines[0], ' ');
		if(st.GetCount() >= 2)
			r.status = ScanInt(st[1]);
	}
	for(int i = 1; i < lines.GetCount(); i++) {
		String line = TrimBoth(lines[i]);
		int q = line.Find(':');
		if(q < 0)
			continue;
		String key = ToLower(TrimBoth(line.Mid(0, q)));
		if(key == "set-cookie")
			r.set_cookies.Add(TrimBoth(line.Mid(q + 1)));
		else if(key == "content-length")
			r.content_length = ScanInt(TrimBoth(line.Mid(q + 1)));
	}
	r.body = String(all);
	if(r.content_length >= 0 && r.body.GetCount() < r.content_length) {
			for(int i = 0; i < min(r.body.GetCount(), 48); i++)
			Cout() << (unsigned)(byte)r.body[i] << ' ';
		Cout() << "\n";
	}
	return r;
}

static HttpReply HttpCall(const String& method, int port, const String& path,
                          const VectorMap<String, String>& headers, const String& body,
                          const char *content_type = "application/x-www-form-urlencoded")
{
	// The engine listeners come up asynchronously; a refused exchange at
	// startup is retried rather than failing the fence assertions.
	HttpReply r;
	for(int attempt = 0; attempt < 6; attempt++) {
		String connect_error;
		r = HttpCallOnce(method, port, path, headers, body, content_type, connect_error);
		if(r.status != 0 && (r.content_length < 0 || r.body.GetCount() >= r.content_length))
			return r; // complete exchange
		if(connect_error.GetCount())
			r.body = connect_error;
		Sleep(50);
	}
	return r;
}

static String CookieOf(const HttpReply& r, const String& cookie_name, String& raw_pair)
{
	for(const String& c : r.set_cookies) {
		int q = c.Find('=');
		if(q < 0)
			continue;
		if(TrimBoth(c.Mid(0, q)) == cookie_name) {
			int e = c.Find(';', q);
			raw_pair = TrimBoth(e < 0 ? c : c.Mid(0, e));
			return TrimBoth(c.Mid(q + 1, (e < 0 ? c.GetCount() : e) - q - 1));
		}
	}
	return String();
}

static String FormEncode(const VectorMap<String, String>& fields)
{
	String out;
	for(int i = 0; i < fields.GetCount(); i++) {
		if(i)
			out << '&';
		out << fields.GetKey(i) << '=' << UrlEncode(fields[i]);
	}
	return out;
}

// ---------------------------------------------------------------------------
// Site handlers (registered identically on alpha and beta)
// ---------------------------------------------------------------------------

static void RouteHome(WebRequest& r)
{
	r.Html("home of site <b>" + r.Site().id + "</b>");
}

static void RouteWhoami(WebRequest& r)
{
	ValueMap m;
	m.Add("site", r.Site().id);
	m.Add("user", r.HasUser() ? Value(r.User().username) : Value());
	m.Add("user_id", r.HasUser() ? Value(r.User().user_id) : Value());
	r.Json(m);
}

static void RouteSetsession(WebRequest& r)
{
	r.EnsureSession();
	ValueMap m;
	m.Add("ok", true);
	m.Add("csrf", r.CsrfToken());
	r.Json(m);
}

static void RouteLogin(WebRequest& r)
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
	ValueMap m;
	m.Add("ok", true);
	m.Add("user", u.username);
	m.Add("user_id", u.user_id);
	m.Add("csrf", r.CsrfToken());
	r.Json(m);
}

static void RouteLogout(WebRequest& r)
{
	r.DropSession();
	ValueMap m;
	m.Add("ok", true);
	r.Json(m);
}

static void RouteCount(WebRequest& r)
{
	IRecordCollection& store = TheStoreManager().JsonCollection(r.Site().id, r.Site().data_root, "counter");
	String json, rev;
	int value = 0;
	if(store.Get("counter", json, rev).ok) {
		Value v = ParseJSON(json);
		if(v.Is<ValueMap>() && IsNumber(((ValueMap)v).Get("value")))
			value = (int)((ValueMap)v).Get("value");
	}
	ValueMap m;
	m.Add("site", r.Site().id);
	m.Add("count", value);
	r.Json(m);
}

static void RouteCountPost(WebRequest& r)
{
	IRecordCollection& store = TheStoreManager().JsonCollection(r.Site().id, r.Site().data_root, "counter");
	String json, rev;
	int value = 0;
	bool exists = store.Get("counter", json, rev).ok;
	if(exists) {
		Value v = ParseJSON(json);
		if(v.Is<ValueMap>() && IsNumber(((ValueMap)v).Get("value")))
			value = (int)((ValueMap)v).Get("value");
	}
	ValueMap doc;
	doc.Add("value", value + 1);
	String new_rev;
	if(!store.Put("counter", AsJSON(doc), exists ? rev : String(), new_rev).ok) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "store update failed");
		r.Json(m, 500);
		return;
	}
	ValueMap m;
	m.Add("ok", true);
	m.Add("site", r.Site().id);
	m.Add("count", value + 1);
	r.Json(m);
}

static void RouteGreet(WebRequest& r)
{
	r.Html("hello " + r.PathArg(0) + " from " + r.Site().id);
}

static void RouteCallAlpha(WebRequest& r)
{
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = "alpha";
	ctx.caller_id = r.HasUser() ? r.User().user_id : String();
	BrokerCall call;
	call.op = "add";
	call.payload.Add("a", r.IntParam("a"));
	call.payload.Add("b", r.IntParam("b"));
	BrokerResult res = TheWebEngine().Broker().Call(ctx, call);
	ValueMap m;
	m.Add("ok", res.ok);
	m.Add("sum", res.ok ? Value(res.payload.Get("sum")) : Value());
	m.Add("error", res.error);
	r.Json(m, res.ok ? 200 : 502);
}

static void RouteCallNowhere(WebRequest& r)
{
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = "ghost-site";
	BrokerCall call;
	call.op = "ping";
	BrokerResult res = TheWebEngine().Broker().Call(ctx, call);
	ValueMap m;
	m.Add("ok", res.ok);
	m.Add("error", res.error);
	r.Json(m);
}

static void RegisterSiteHandlers(EngineSite& site)
{
	SiteRouter& rt = site.Router();
	rt.Add("", RouteHome);
	rt.Add("whoami", RouteWhoami);
	rt.Add("setsession", RouteSetsession);
	rt.Add("login:POST", RouteLogin);
	rt.Add("logout:POST", RouteLogout);
	rt.Add("count", RouteCount);
	rt.Add("count:POST", RouteCountPost);
	rt.Add("greet/*", RouteGreet);
	rt.Add("call-alpha:POST", RouteCallAlpha);
	rt.Add("call-nowhere:POST", RouteCallNowhere);
}
// Broker service on alpha.
struct AlphaService : IBrokerService {
	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override
	{
		BrokerResult r;
		if(call.op == "add") {
			Value a = call.payload.Get("a");
			Value b = call.payload.Get("b");
			if(!IsNumber(a) || !IsNumber(b)) {
				r.status = 400;
				r.error = "add needs numeric a and b";
				return r;
			}
			r.ok = true;
			r.status = 200;
			r.payload.Add("sum", (int)a + (int)b);
			return r;
		}
		if(call.op == "ping") {
			r.ok = true;
			r.status = 200;
			r.payload.Add("pong", true);
			return r;
		}
		r.status = 404;
		r.error = "unknown op";
		return r;
	}
};

// ---------------------------------------------------------------------------
// Test host
// ---------------------------------------------------------------------------

static String s_root;

static void SetupSites()
{
	EngineSite& alpha = TheWebEngine().AddSite("alpha", "Alpha",
		Format("http://127.0.0.1:%d", ALPHA_PORT), AppendFileName(s_root, "alpha"));
	EngineSite& beta = TheWebEngine().AddSite("beta", "Beta",
		Format("http://127.0.0.1:%d", BETA_PORT), AppendFileName(s_root, "beta"));
	Vector<String> roles;
	roles.Add("member");
	String uid;
	bool existed;
	alpha.Accounts().CreateUser("alice", "pw-alpha", "Alice", roles, uid, existed);
	alpha.Accounts().CreateUser("bob", "pw-bob", "Bob", roles, uid, existed);
	beta.Accounts().CreateUser("alice", "pw-beta", "Alice", roles, uid, existed);
	beta.Accounts().CreateUser("bob", "pw-bob", "Bob", roles, uid, existed);
	RegisterSiteHandlers(alpha);
	RegisterSiteHandlers(beta);

	EngineSite& shell = TheWebEngine().AddSite("shell", "Shell",
		Format("http://127.0.0.1:%d", MAIN_PORT), AppendFileName(s_root, "shell"));
	shell.Router().Add("", RouteHome);
}

struct EngineTestHost : WebEngineHost {
	EngineTestHost()
	{
		port = MAIN_PORT;
		threads = 4;
		prefork = 0;
	}
};

static EngineTestHost *s_host = NULL;

static void HostMain()
{
	s_host->Run();
}

static bool JsonFlag(const ValueMap& m, const char *key)
{
	Value v = m.Get(key);
	return v.Is<bool>() ? (bool)v : (!v.IsNull() && IsNumber(v) && (int)v != 0);
}

static Vector<String> failures;
static int checks = 0;

static void Check(bool cond, const String& what)
{
	checks++;
	if(!cond)
		failures.Add(what);
	Cout() << (cond ? "ok   " : "FAIL ") << what << "\n";
}

static bool ProbePort(int port)
{
	TcpSocket s;
	s.Timeout(3000);
	if(!s.Connect("127.0.0.1", port))
		return false;
	s.Put("GET / HTTP/1.1\r\nHost: probe\r\nConnection: close\r\n\r\n");
	String d;
	while((d = s.Get(1024)).GetCount())
		;
	return true;
}

static void WaitPortsUp()
{
	for(int i = 0; i < 200; i++)
		if(ProbePort(ALPHA_PORT) && ProbePort(BETA_PORT))
			return;
	Check(false, "engine listeners never came up");
}

CONSOLE_APP_MAIN
{
	Cout() << "WebEngineTests: engine fence self-test\n";
	s_root = GetHomeDirFile(Format("WebEngineTests/run-%d-%d", (int)msecs(), (int)Random(1000000)));
	RealizeDirectory(s_root);

	// --- unit level: accounts ---
	String report;
	StoreResult st = WebAccountsSelfTest::RunAll(AppendFileName(s_root, "accounts"), report);
	Cout() << report;
	if(!st.ok)
		Cout() << "accounts err: " << st.error << "\n";
	Check(st.ok, "WebAccounts self test");

	// --- unit level: storage ---
	{
		JsonFileStore store("unit", AppendFileName(s_root, "storeunit"), "col");
		ValueMap doc;
		doc.Add("v", 1);
		String rev1, rev2;
		StoreResult p1 = store.Put("rec", AsJSON(doc), String(), rev1);
		if(!p1.ok)
			Cout() << "put1 err: " << p1.error << "\n";
		Check(p1.ok, "json store initial put");
		doc.Set("v", 2);
		StoreResult p2 = store.Put("rec", AsJSON(doc), rev1, rev2);
		if(!p2.ok)
			Cout() << "put2 err: " << p2.error << "\n";
		Check(p2.ok && rev2 != rev1, "json store revisioned put");
		StoreResult p3 = store.Put("rec", AsJSON(doc), rev1, rev2);
		if(!p3.ok)
			Cout() << "put3(stale) err: " << p3.error << "\n";
		Check(!p3.ok, "stale expected_revision rejected");
		StoreResult del = store.Delete("rec", "wrong");
		if(!del.ok)
			Cout() << "del err: " << del.error << "\n";
		Check(!del.ok, "delete with wrong revision rejected");
		Vector<String> ids;
		store.List(ids);
		Cout() << "ids: " << Join(ids, ",") << "\n";
		Check(ids.GetCount() == 1 && ids[0] == "rec", "list sees record");
	}

	// --- unit level: broker tcp stub ---
	{
		BrokerResult r = TheWebEngine().Broker().CallRemote(BrokerCallContext(), BrokerCall());
		Check(!r.ok && r.error == WebBroker::TCP_NOT_IMPLEMENTED(), "tcp broker transport is an explicit stub");
	}

	// --- three fenced sites ---
	SetupSites();

	EngineTestHost host;
	s_host = &host;
	host.BindDefaultSite("shell");
	host.BindSite(ALPHA_PORT, "alpha");
	host.BindSite(BETA_PORT, "beta");
	TheWebEngine().Broker().Register("alpha", new AlphaService);

	Thread host_thread;
	host_thread.Run(HostMain);
	WaitPortsUp();

	VectorMap<String, String> no_headers;

	// 1. identical paths, different sites
	HttpReply ra = HttpCall("GET", ALPHA_PORT, "/", no_headers, "");
	HttpReply rb = HttpCall("GET", BETA_PORT, "/", no_headers, "");
	HttpReply rm = HttpCall("GET", MAIN_PORT, "/", no_headers, "");
	Check(ra.status == 200 && ra.body.Find("alpha") >= 0, "GET / on alpha serves alpha");
	Check(rb.status == 200 && rb.body.Find("beta") >= 0, "GET / on beta serves beta");
	Check(rm.status == 200 && rm.body.Find("shell") >= 0, "GET / on stock port serves shell (default site)");

	// 2. unknown route -> 404
	HttpReply r404 = HttpCall("GET", ALPHA_PORT, "/nope/deeper", no_headers, "");
	Check(r404.status == 404, "unknown route is 404");

	// 3. path args
	HttpReply rg1 = HttpCall("GET", ALPHA_PORT, "/greet/world", no_headers, "");
	HttpReply rg2 = HttpCall("GET", BETA_PORT, "/greet/world", no_headers, "");
	Check(rg1.body.Find("hello world from alpha") >= 0, "route arg on alpha");
	Check(rg2.body.Find("hello world from beta") >= 0, "route arg on beta");

	// 4. cookie names are site-specific and HttpOnly
	HttpReply rs_a = HttpCall("GET", ALPHA_PORT, "/setsession", no_headers, "");
	HttpReply rs_b = HttpCall("GET", BETA_PORT, "/setsession", no_headers, "");
	String pair_a, pair_b, csrf_a, csrf_b;
	String val_a = CookieOf(rs_a, "wfs_alpha", pair_a);
	String val_b = CookieOf(rs_b, "wfs_beta", pair_b);
	Check(val_a.GetCount() > 0 && val_b.GetCount() > 0, "per-site session cookies issued");
	bool httponly = false;
	for(const String& c : rs_a.set_cookies)
		if(c.Find("HttpOnly") >= 0)
			httponly = true;
	Check(val_a.GetCount() > 0 && httponly, "session cookie is HttpOnly");
	{
		Value v = ParseJSON(rs_a.body);
		csrf_a = v.Is<ValueMap>() ? (String)((ValueMap)v).Get("csrf") : String();
		Value v2 = ParseJSON(rs_b.body);
		csrf_b = v2.Is<ValueMap>() ? (String)((ValueMap)v2).Get("csrf") : String();
	}
	Check(csrf_a.GetCount() >= 32 && csrf_b.GetCount() >= 32, "guest sessions carry csrf tokens");
	Check(csrf_a != csrf_b, "csrf tokens differ per site");

	// 5. login on alpha
	VectorMap<String, String> origin_a;
	origin_a.Add("Origin", "http://127.0.0.1:18201");
	VectorMap<String, String> login_f;
	login_f.Add("username", "alice");
	login_f.Add("password", "pw-alpha");
	HttpReply rl = HttpCall("POST", ALPHA_PORT, "/login", origin_a, FormEncode(login_f));
	String alpha_csrf, alpha_pair, alpha_uid;
	{
		Value v = ParseJSON(rl.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(rl.status == 200 && JsonFlag(m, "ok"), "login alice on alpha");
		alpha_csrf = m.Get("csrf").IsNull() ? String() : (String)m.Get("csrf");
		alpha_uid = m.Get("user_id").IsNull() ? String() : (String)m.Get("user_id");
	}
	alpha_pair = "wfs_alpha=" + CookieOf(rl, "wfs_alpha", alpha_pair);
	Check(alpha_pair.Find("=") > 0 && alpha_pair.Find("was_") > 0, "alpha login sets wfs_alpha session cookie");

	// 6. same username on beta needs ITS OWN password
	VectorMap<String, String> origin_b;
	origin_b.Add("Origin", "http://127.0.0.1:18202");
	VectorMap<String, String> wrongpw;
	wrongpw.Add("username", "alice");
	wrongpw.Add("password", "pw-alpha");
	HttpReply rwx = HttpCall("POST", BETA_PORT, "/login", origin_b, FormEncode(wrongpw));
	Check(rwx.status == 401, "beta alice rejects alpha's password");
	VectorMap<String, String> rightpw;
	rightpw.Add("username", "alice");
	rightpw.Add("password", "pw-beta");
	HttpReply rlb = HttpCall("POST", BETA_PORT, "/login", origin_b, FormEncode(rightpw));
	String beta_pair, beta_uid;
	{
		Value v = ParseJSON(rlb.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(rlb.status == 200 && JsonFlag(m, "ok"), "login alice on beta with its own password");
		beta_uid = m.Get("user_id").IsNull() ? String() : (String)m.Get("user_id");
	}
	beta_pair = "wfs_beta=" + CookieOf(rlb, "wfs_beta", beta_pair);
	Check(alpha_uid != beta_uid && alpha_uid.GetCount() > 0, "same username has distinct user ids per site");

	// 7. whoami with sessions
	VectorMap<String, String> h_alpha, h_beta;
	h_alpha.Add("Cookie", alpha_pair);
	h_beta.Add("Cookie", beta_pair);
	HttpReply rw_a = HttpCall("GET", ALPHA_PORT, "/whoami", h_alpha, "");
	HttpReply rw_b = HttpCall("GET", BETA_PORT, "/whoami", h_beta, "");
	Check(rw_a.body.Find("alice") >= 0 && rw_a.body.Find(alpha_uid) >= 0, "whoami resolves on alpha");
	Check(rw_b.body.Find("alice") >= 0 && rw_b.body.Find(beta_uid) >= 0, "whoami resolves on beta");

	// 8. cross-site cookies are rejected
	VectorMap<String, String> h_steal;
	h_steal.Add("Cookie", alpha_pair);
	HttpReply rx_b = HttpCall("GET", BETA_PORT, "/whoami", h_steal, "");
	Check(rx_b.body.Find("alice") < 0, "alpha session cookie rejected by beta");
	VectorMap<String, String> h_steal2;
	h_steal2.Add("Cookie", beta_pair);
	HttpReply rx_a = HttpCall("GET", ALPHA_PORT, "/whoami", h_steal2, "");
	Check(rx_a.body.Find("alice") < 0, "beta session cookie rejected by alpha");

	// 9. counters: per-site storage isolation
	int c_a0 = 0, c_b0 = 0;
	{
		Value v = ParseJSON(HttpCall("GET", ALPHA_PORT, "/count", no_headers, "").body);
		c_a0 = v.Is<ValueMap>() ? (int)((ValueMap)v).Get("count") : -1;
		Value v2 = ParseJSON(HttpCall("GET", BETA_PORT, "/count", no_headers, "").body);
		c_b0 = v2.Is<ValueMap>() ? (int)((ValueMap)v2).Get("count") : -1;
		Check(c_a0 == 0 && c_b0 == 0, "fresh per-site counters");
	}

	// 10. CSRF fence: logged-in mutation without token is rejected
	HttpReply rf1 = HttpCall("POST", ALPHA_PORT, "/count", h_alpha, "");
	Check(rf1.status == 403, "logged-in mutation without csrf is 403");

	// 11. CSRF fence: wrong token rejected
	VectorMap<String, String> h_badcsrf = clone(h_alpha);
	h_badcsrf.Add("X-WF-CSRF", "definitely-not-the-token");
	HttpReply rf2 = HttpCall("POST", ALPHA_PORT, "/count", h_badcsrf, "");
	Check(rf2.status == 403, "logged-in mutation with wrong csrf is 403");

	// 12. correct token passes
	VectorMap<String, String> h_csrf = clone(h_alpha);
	h_csrf.Add("X-WF-CSRF", alpha_csrf);
	HttpReply rf3 = HttpCall("POST", ALPHA_PORT, "/count", h_csrf, "");
	int c_a1 = -1;
	{
		Value v = ParseJSON(rf3.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(rf3.status == 200 && JsonFlag(m, "ok"), "logged-in mutation with correct csrf passes");
		c_a1 = m.Get("count").IsNull() ? -1 : (int)m.Get("count");
	}

	// 13. cross-origin mutation rejected even with valid token
	VectorMap<String, String> h_evil = clone(h_csrf);
	h_evil.Add("Origin", "http://evil.example");
	HttpReply rf4 = HttpCall("POST", ALPHA_PORT, "/count", h_evil, "");
	Check(rf4.status == 403, "cross-origin mutation is 403 even with valid csrf");

	// 14. guest mutation without session is allowed (no session to ride)
	HttpReply rf5 = HttpCall("POST", BETA_PORT, "/count", no_headers, "");
	int c_b1 = -1;
	{
		Value v = ParseJSON(rf5.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(rf5.status == 200 && JsonFlag(m, "ok"), "guest mutation allowed (no session to ride)");
		c_b1 = m.Get("count").IsNull() ? -1 : (int)m.Get("count");
	}

	// 15. storage isolation between sites
	Check(c_a1 == c_a0 + 1, "alpha counter incremented once");
	Check(c_b1 == c_b0 + 1, "beta counter incremented once");

	// 16. broker: cross-site call via beta -> alpha
	VectorMap<String, String> call_f;
	call_f.Add("a", "2");
	call_f.Add("b", "3");
	HttpReply rc = HttpCall("POST", BETA_PORT, "/call-alpha", no_headers, FormEncode(call_f));
	{
		Value v = ParseJSON(rc.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(rc.status == 200 && JsonFlag(m, "ok") && (int)m.Get("sum") == 5,
		      "broker call beta->alpha computes add");
	}
	bool diag_ok = false;
	{
		String dj = TheWebEngine().DiagnosticsJSON();
		Value dv = ParseJSON(dj);
		if(dv.Is<ValueMap>() && ((ValueMap)dv).Get("entries").Is<ValueArray>()) {
			const ValueArray& arr = (ValueArray)((ValueMap)dv).Get("entries");
			for(int i = 0; i < arr.GetCount(); i++) {
				ValueMap m = arr[i];
				if((String)m.Get("from_site") == "beta" && (String)m.Get("to_site") == "alpha"
				&& (String)m.Get("op") == "add" && (bool)m.Get("ok"))
					diag_ok = true;
			}
		}
	}
	Check(diag_ok, "broker diagnostics record the cross-site call");

	// 17. broker: unknown target fails explicitly
	HttpReply rc2 = HttpCall("POST", BETA_PORT, "/call-nowhere", no_headers, "");
	{
		Value v = ParseJSON(rc2.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(!JsonFlag(m, "ok") && ((String)m.Get("error")).Find("ghost-site") >= 0,
		      "broker call to unknown site fails explicitly");
	}

	// 18. logout invalidates the session
	{
		VectorMap<String, String> h_csrf2 = clone(h_alpha);
		h_csrf2.Add("X-WF-CSRF", alpha_csrf);
		HttpCall("POST", ALPHA_PORT, "/logout", h_csrf2, "");
		HttpReply rw2 = HttpCall("GET", ALPHA_PORT, "/whoami", h_alpha, "");
		Check(rw2.body.Find("alice") < 0, "logout invalidates session");
	}

	// 19. second site can log in the same username independently (already shown);

	host.StopEngines();
	host.Quit();
	host_thread.Wait();

	Cout() << "\nWebEngineTests: " << (checks - failures.GetCount()) << "/" << checks
	       << (failures.GetCount() ? " CHECKS FAILED" : " checks passed") << "\n";
	if(failures.GetCount())
		for(const String& f : failures)
			Cout() << "  FAILED: " << f << "\n";
	fflush(stdout);
	_Exit(failures.GetCount() ? 1 : 0);
}
