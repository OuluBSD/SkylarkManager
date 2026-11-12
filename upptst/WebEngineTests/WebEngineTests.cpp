#include <Core/Core.h>
#ifdef PLATFORM_POSIX
#include <unistd.h>
#endif
#include <Skylark/Skylark.h>
#include <WebEngine/WebEngine.h>
#include <WebEngine/SessionRoutes.h>
#include <WebAccounts/WebAccountsTotp.h>
#include <MetaAccount/MetaAccountSite.h>
#include <ForgeCore/ForgeCore.h>
#include <GitHubForge/GitHubForge.h>
#include <GitHubForge/GitHubGhConnector.h>
#include <SourceForgeForge/SourceForgeForge.h>
#include <PrivateHubWorkspace/PrivateHubWorkspace.h>
#include <PrivateHubWorkspace/PrivateHubSite.h>
#include <TaskFoundry/TaskFoundrySite.h>
#include <WebBroker/WebBroker.h>
#include <Channels/ChannelsSite.h>
#include <Financials/FinancialsSite.h>
#include <Atlas/AtlasSite.h>
#include <WebEngine/CommandConsole.h>
#include <Science/ScienceModel.h>
#include <Science/ScienceSite.h>
#include <Science/ScienceService.h>

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
static const int META_PORT  = 18203;
static const int SCIENCE_PORT = 18204;
static const int PRIVATEHUB_PORT = 18205;
static const int TASKFOUNDRY_PORT = 18206;
static const int CHANNELS_PORT = 18207;
static const int FINANCIALS_PORT = 18208;
static const int ATLAS_PORT = 18209;

// ---------------------------------------------------------------------------
// Minimal raw HTTP client (Connection: close)
// ---------------------------------------------------------------------------

struct HttpReply {
	int    status = 0;
	String body;
	int    content_length = -1;
	String location;
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
	// The caller may name its own Host (task 032: a LAN authority over loopback).
	if(headers.Find("Host") < 0)
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
		else if(key == "location")
			r.location = TrimBoth(line.Mid(q + 1));
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

// The full Set-Cookie line for a cookie, with its attributes; empty if absent.
static String SetCookieLine(const HttpReply& r, const String& cookie_name)
{
	for(const String& c : r.set_cookies) {
		int q = c.Find('=');
		if(q >= 0 && TrimBoth(c.Mid(0, q)) == cookie_name)
			return c;
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

static void RouteRequestConsent(WebRequest& r)
{
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = "meta";
	BrokerCall call;
	call.op = "request-consent";
	call.payload.Add("subject_user_id", r.Param("subject_user_id"));
	call.payload.Add("client_site", r.Site().id);
	call.payload.Add("client_id", "test-app");
	ValueArray scopes;
	scopes.Add("test.scope");
	call.payload.Add("scopes", scopes);
	BrokerResult res = TheWebEngine().Broker().Call(ctx, call);
	ValueMap m;
	m.Add("ok", res.ok);
	m.Add("grant_id", res.ok ? Value(res.payload.Get("grant_id")) : Value());
	m.Add("error", res.error);
	r.Json(m, res.ok ? 200 : 502);
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
	rt.Add("request-consent:POST", RouteRequestConsent);
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
static WebGrants *s_meta_grants = NULL;
static String     s_sci_reader_uid;
static String     s_sci_other_uid;
static ScienceCorpus *s_sci_corpus = NULL;
static PrivateHubWorkspaceBackend *s_ph_backend = NULL;
static TaskFoundryService *s_tf_service = NULL;

// The canned Company stand-in: the Financials site (and any fence broker
// caller) reads through this instead of the real module. Scope checks mirror
// the real CompanyBroker. A COMPANY_SHIM_FAIL environment flag fails every
// call, to exercise the site's failure page.
class FakeCompanyService : public IBrokerService {
public:
	static bool HasScope(const Vector<String>& scopes, const char *scope) {
		for(const String& s : scopes)
			if(s == scope)
				return true;
		return false;
	}

	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override {
		BrokerResult r;
		if(!GetEnv("COMPANY_SHIM_FAIL").IsEmpty()) {
			r.status = 503;
			r.error = "company shim failure";
			return r;
		}
		bool admin = HasScope(ctx.scopes, "admin");
		if(call.op == "ledger-summary") {
			if(!HasScope(ctx.scopes, "ledger:read")) {
				r.status = 403;
				r.error = "missing scope ledger:read";
				return r;
			}
			r.ok = true;
			r.status = 200;
			r.payload.Add("credit_total", 500);
			r.payload.Add("debit_total", 120);
			r.payload.Add("correction_total", 0);
			r.payload.Add("entry_count", 2);
			r.payload.Add("newest_entry_at", "2026-11-11 05:00:00");
			return r;
		}
		if(call.op == "ledger-recent") {
			if(!HasScope(ctx.scopes, "ledger:read")) {
				r.status = 403;
				r.error = "missing scope ledger:read";
				return r;
			}
			if(!((String)call.payload.Get("subject_user_id", "")).IsEmpty() && !admin) {
				r.status = 403;
				r.error = "a subject filter needs the admin scope";
				return r;
			}
			ValueArray rows;
			ValueMap row;
			row.Add("seq", 2);
			row.Add("type", "credit");
			row.Add("amount", 500);
			if(admin)
				row.Add("user_id", "wau_fakeuser");
			row.Add("reason", "fixed <bug> & shipped");
			row.Add("created_at", "2026-11-11 05:00:00");
			rows.Add(row);
			r.ok = true;
			r.status = 200;
			r.payload.Add("entries", rows);
			r.payload.Add("total", 2);
			return r;
		}
		if(call.op == "ledger-balances" || call.op == "contracts-list") {
			if(!admin) {
				r.status = 403;
				r.error = "missing scope admin";
				return r;
			}
			ValueArray rows;
			ValueMap row;
			if(call.op == "ledger-balances") {
				row.Add("user_id", "wau_fakeuser");
				row.Add("balance", 380);
			}
			else {
				row.Add("id", "ctr_fake");
				row.Add("title", "Fake <contract>");
				row.Add("status", "active");
				row.Add("currency", "CR");
				row.Add("amount", 1000.0);
				row.Add("client_user_id", "wau_client");
			}
			rows.Add(row);
			r.ok = true;
			r.status = 200;
			r.payload.Add(call.op == "ledger-balances" ? String("balances") : String("contracts"), rows);
			return r;
		}
		r.status = 404;
		r.error = "unknown op";
		return r;
	}
};

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

	EngineSite& meta = TheWebEngine().AddSite("meta", "Meta",
		Format("http://127.0.0.1:%d", META_PORT), AppendFileName(s_root, "meta"));
	s_meta_grants = new WebGrants("meta", AppendFileName(s_root, "meta"));
	InstallMetaAccountSite(meta, *s_meta_grants);
	String muid;
	bool mexisted;
	Vector<String> mroles;
	mroles.Add("owner");
	meta.Accounts().CreateUser("mallory", "pw-meta", "Mallory", mroles, muid, mexisted);

	// Science site over a FIXTURE corpus under s_root. Tests never touch the
	// real AnthologyPrivate directory.
	EngineSite& science = TheWebEngine().AddSite("science", "Science",
		Format("http://127.0.0.1:%d", SCIENCE_PORT), AppendFileName(s_root, "science"));
	String sci_corpus = AppendFileName(s_root, "science-site-corpus");
	RealizeDirectory(sci_corpus);
	SaveFile(AppendFileName(sci_corpus, "2612.00001.json"),
		"{\"id\":\"2612.00001\",\"kind\":\"paper\",\"title\":\"Metadata only\",\"status\":\"draft\","
		"\"abstract\":\"No full text.\",\"envelope\":{\"schema_id\":\"skylark/paper\",\"schema_version\":\"1\","
		"\"workspace_id\":\"science\",\"stable_id\":\"2612.00001\",\"record_kind\":\"paper\",\"revision\":\"1\","
		"\"visibility\":\"public\",\"provenance_ref\":\"\"}}");
	SaveFile(AppendFileName(sci_corpus, "2612.00002.json"),
		"{\"id\":\"2612.00002\",\"kind\":\"paper\",\"title\":\"Authorable\",\"status\":\"draft\","
		"\"abstract\":\"Gets an article.\",\"envelope\":{\"schema_id\":\"skylark/paper\",\"schema_version\":\"1\","
		"\"workspace_id\":\"science\",\"stable_id\":\"2612.00002\",\"record_kind\":\"paper\",\"revision\":\"1\","
		"\"visibility\":\"public\",\"provenance_ref\":\"\"}}");
	s_sci_corpus = new ScienceCorpus(sci_corpus, AppendFileName(s_root, "science-site-articles"));
	InstallScienceSite(science, *s_sci_corpus, s_meta_grants);
	science.Router().Add("login:POST", RouteLogin);
	Vector<String> author_roles;
	author_roles.Add(SCIENCE_AUTHOR_ROLE);
	Vector<String> reader_roles;
	reader_roles.Add("member");
	science.Accounts().CreateUser("author", "pw-author", "Author", author_roles, uid, existed);
	science.Accounts().CreateUser("reader", "pw-reader", "Reader", reader_roles, s_sci_reader_uid, existed);
	science.Accounts().CreateUser("other", "pw-other", "Other", reader_roles, s_sci_other_uid, existed);

	// PrivateHub site over a git fixture repo under s_root (task 010: members write, viewers read).
	String ph_ws = AppendFileName(s_root, "privatehub-http-ws");
	RealizeDirectory(AppendFileName(AppendFileName(ph_ws, "oulubsd"), "demo"));
	String ph_repo = AppendFileName(AppendFileName(ph_ws, "oulubsd"), "demo");
	system(~String("cd \"" + ph_repo + "\" && git init -q && git config user.email t@t && git config user.name t && echo hi > file.txt && git add file.txt && git commit -qm init"));
	s_ph_backend = new PrivateHubWorkspaceBackend(ph_ws, AppendFileName(s_root, "privatehub-http-issues"));
	EngineSite& ph = TheWebEngine().AddSite("privatehub", "PrivateHub",
		Format("http://127.0.0.1:%d", PRIVATEHUB_PORT), AppendFileName(s_root, "privatehub"));
	InstallPrivateHubSite(ph, *s_ph_backend);
	InstallSessionRoutes(ph);
	Vector<String> ph_member_roles;
	ph_member_roles.Add("member");
	Vector<String> ph_no_roles;
	ph.Accounts().CreateUser("member", "pw-member", "Member", ph_member_roles, uid, existed);
	ph.Accounts().CreateUser("viewer", "pw-viewer", "Viewer", ph_no_roles, uid, existed);
	InstallCommandConsole(ph);

	// TaskFoundry site (task 009: linked-consent round trip through the meta site).
	String tf_root = AppendFileName(s_root, "taskfoundry");
	RealizeDirectory(tf_root);
	s_tf_service = new TaskFoundryService(TheWebEngine().Broker(), tf_root);
	EngineSite& tf = TheWebEngine().AddSite("taskfoundry", "TaskFoundry",
		Format("http://127.0.0.1:%d", TASKFOUNDRY_PORT), tf_root);
	InstallTaskFoundrySite(tf, *s_tf_service, s_meta_grants);
	InstallSessionRoutes(tf);
	InstallCommandConsole(tf);
	tf.Accounts().CreateUser("tfdev", "pw-tf", "Tfdev", ph_no_roles, uid, existed);

	// A second meta identity, for the not-your-grant negative.
	meta.Accounts().CreateUser("eva", "pw-eva", "Eva", ph_no_roles, uid, existed);

	// Financials site over the canned company stand-in (task 016).
	EngineSite& fin = TheWebEngine().AddSite("financials", "Financials",
		Format("http://127.0.0.1:%d", FINANCIALS_PORT), AppendFileName(s_root, "financials"));
	InstallFinancialsSite(fin);
	InstallSessionRoutes(fin);
	Vector<String> fin_finance_roles;
	fin_finance_roles.Add("finance");
	fin.Accounts().CreateUser("fviewer", "pw-fin", "Fviewer", ph_no_roles, uid, existed);
	fin.Accounts().CreateUser("ffin", "pw-fin2", "Ffin", fin_finance_roles, uid, existed);

	// Atlas map webapp (task 029): the admin edits, a viewer reads.
	EngineSite& atl = TheWebEngine().AddSite("atlas", "Atlas",
		Format("http://127.0.0.1:%d", ATLAS_PORT), AppendFileName(s_root, "atlas"));
	InstallAtlasSite(atl);
	InstallSessionRoutes(atl);
	Vector<String> atl_admin_roles;
	atl_admin_roles.Add("admin");
	atl.Accounts().CreateUser("aadmin", "pw-atl", "Aadmin", atl_admin_roles, uid, existed);
	atl.Accounts().CreateUser("aview", "pw-atl2", "Aview", ph_no_roles, uid, existed);

	// Channels site over an argv shim (task 012: read-only AgentIPC frontend).
	String shim_path = AppendFileName(s_root, "channels-shim.sh");
	FileOut shim(shim_path);
	shim << "#!/bin/sh\n"
	     << "printf '%s\\n' \"$@\" > \"$AGENTIPC_SHIM_ARGV\"\n"
	     << "echo run >> \"$AGENTIPC_SHIM_COUNT\"\n"
	     << "if [ -n \"$AGENTIPC_SHIM_FAIL\" ]; then echo shim failure >&2; exit 3; fi\n"
	     << "last=\"\"; for a in \"$@\"; do last=\"$a\"; done\n"
	     << "case \"$last\" in\n"
	     << "  names) echo s1; echo s2 ;;\n"
	     << "  *) cat \"$AGENTIPC_SHIM_CANNED\" ;;\n"
	     << "esac\n";
	shim.Close();
	system(~String("chmod +x \"" + shim_path + "\""));
	FileOut canned(AppendFileName(s_root, "channels-canned.txt"));
	canned << "#SKYLARK-MANAGER [2026-11-11T03:00:00Z] s1: TASK hello <script>alert(1)</script>\n"
	       << "#SKYLARK-MANAGER [2026-11-11T03:01:00Z] s2: ACK hello\n";
	canned.Close();
	FileOut records(AppendFileName(s_root, "channels-records.jsonl"));
	records << "{\"body\":\"ciphertext-a\",\"chan\":\"SKYLARK-MANAGER\",\"origin\":\"skylark-a\","
	           "\"oseq\":2,\"server\":\"skylark\",\"ts\":\"2026-11-11T03:01:00Z\",\"user\":\"s2\"}\n"
	        << "{\"body\":\"ciphertext-b\",\"chan\":\"AGENTLAB\",\"origin\":\"skylark-a\","
	           "\"oseq\":3,\"server\":\"skylark\",\"ts\":\"2026-11-11T03:02:00Z\",\"user\":\"a1\"}\n";
	records.Close();
	AgentIpcConfig ch_ipc;
	ch_ipc.client_argv.Add("/bin/sh");
	ch_ipc.client_argv.Add(shim_path);
	ch_ipc.records_path = AppendFileName(s_root, "channels-records.jsonl");
	ch_ipc.password = "pw-secret";
	EngineSite& ch = TheWebEngine().AddSite("channels", "Channels",
		Format("http://127.0.0.1:%d", CHANNELS_PORT), AppendFileName(s_root, "channels"));
	InstallChannelsSite(ch, ch_ipc);
	InstallSessionRoutes(ch);
	ch.Accounts().CreateUser("viewer", "pw-view", "Viewer", ph_no_roles, uid, existed);
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

// The state cell of one grant row on the TaskFoundry grants page ("" when the row is absent).
static String GrantStateCell(const String& body, const String& id)
{
	int p = body.Find(id);
	if(p < 0)
		return String();
	int cell = body.Find("</td><td>", p + id.GetCount());
	int end = cell >= 0 ? body.Find("</td>", cell + 9) : -1;
	if(cell < 0 || end < 0)
		return String();
	return body.Mid(cell + 9, end - cell - 9);
}

// The revision a map page embeds in its atlas-data script ("" when absent).
static String AtlasRevisionOf(const String& body)
{
	int p = body.Find("id=\"atlas-data\">");
	if(p < 0)
		return String();
	p += 16;
	int e = body.Find("</script>", p);
	if(e < 0)
		return String();
	ValueMap m = ParseJSON(body.Mid(p, e - p));
	return AsString(m.Get("revision"));
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

// AgentIpcClient unit section (task 024 item 3): the argv-only reader driven
// directly over a canned shim. No HTTP; the shim's run count shows whether a
// refused channel ever reached the child process.
static void RunAgentIpcClientUnitChecks(const String& root)
{
	String count_path = AppendFileName(root, "channels-unit-count.txt");
	String canned_path = AppendFileName(root, "channels-unit-canned.txt");
	String shim_path = AppendFileName(root, "channels-unit-shim.sh");
	FileOut shim(shim_path);
	shim << "#!/bin/sh\n"
	     << "echo run >> \"" << count_path << "\"\n"
	     << "last=\"\"; for a in \"$@\"; do last=\"$a\"; done\n"
	     << "case \"$last\" in\n"
	     << "  FAILCHAN) echo 'shim failure' >&2; exit 3 ;;\n"
	     << "esac\n"
	     << "cat \"" << canned_path << "\"\n";
	shim.Close();
	system(~String("chmod +x \"" + shim_path + "\""));
	FileOut canned(canned_path);
	canned << "garbage line without a marker\n"
	       << "#SKYLARK-MANAGER missing-bracket s1: x\n"
	       << "#SKYLARK-MANAGER [2026-11-11T03:00:00Z] s1: TASK one\n"
	       << "\n"
	       << "#AGENTLAB [2026-11-11T03:01:00Z] a1: second\n"
	       << "#SKYLARK-MANAGER [2026-11-11T03:02:00Z] s2: ACK three\n"
	       << "#SKYLARK-MANAGER [2026-11-11T03:03:00Z] s1: four\n";
	canned.Close();

	Check(AgentIpcValidChannel("SKYLARK-MANAGER") && AgentIpcValidChannel("AGENTLAB.x_1-2"),
	      "agentipc unit: a plain channel name is valid");
	Check(!AgentIpcValidChannel("") && !AgentIpcValidChannel(String('A', 65)),
	      "agentipc unit: an empty or over-long channel name is refused");
	Check(!AgentIpcValidChannel("bad chan") && !AgentIpcValidChannel("a/b") && !AgentIpcValidChannel("a;b"),
	      "agentipc unit: channel names with spaces, slashes or shell characters are refused");

	AgentIpcConfig cfg;
	cfg.client_argv.Add("/bin/sh");
	cfg.client_argv.Add(shim_path);
	cfg.password = "pw-unit";

	int runs_before = LoadFile(count_path).GetCount();
	AgentIpcReply refused = AgentIpcReadChannel(cfg, "bad chan", 10);
	Check(!refused.ok && refused.error == "invalid channel name" && LoadFile(count_path).GetCount() == runs_before,
	      "agentipc unit: a refused channel never starts the client");

	AgentIpcReply all = AgentIpcReadChannel(cfg, "SKYLARK-MANAGER", 0);
	Check(all.ok && all.exit_code == 0 && all.messages.GetCount() == 4,
	      "agentipc unit: well-formed lines parse and malformed lines are skipped (4 of 7)");
	Check(all.messages.GetCount() == 4 && all.messages[0].ts == "2026-11-11T03:00:00Z" &&
	      all.messages[0].user == "s1" && all.messages[0].body == "TASK one" &&
	      all.messages[1].user == "a1" && all.messages[1].body == "second",
	      "agentipc unit: timestamp, user and body are split on the markers");

	AgentIpcReply two = AgentIpcReadChannel(cfg, "SKYLARK-MANAGER", 2);
	Check(two.ok && two.messages.GetCount() == 2 && two.messages[0].body == "ACK three" &&
	      two.messages[1].body == "four",
	      "agentipc unit: a limit keeps the last N messages, newest last");

	AgentIpcReply failed = AgentIpcReadChannel(cfg, "FAILCHAN", 5);
	Check(!failed.ok && failed.exit_code == 3 && failed.error.Find("shim failure") >= 0 && failed.messages.IsEmpty(),
	      "agentipc unit: a client exit 3 maps to not-ok with the error text and no messages");
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
		if(ProbePort(ALPHA_PORT) && ProbePort(BETA_PORT) && ProbePort(SCIENCE_PORT))
			return;
	Check(false, "engine listeners never came up");
}

CONSOLE_APP_MAIN
{
	setvbuf(stdout, NULL, _IONBF, 0);
	// Headless read smoke against a real corpus. Read-only: only ListPapers runs,
	// and the article root is the corpus itself, so nothing is written.
	//   WebEngineTests --science-corpus <dir> [min_records]   (default 150)
	if(CommandLine().GetCount() >= 2 && CommandLine()[0] == "--science-corpus") {
		String dir = CommandLine()[1];
		int min_records = CommandLine().GetCount() >= 3 ? atoi(CommandLine()[2]) : 150;
		ScienceCorpus reader(dir, dir);
		Vector<SciencePaper> papers;
		bool listed = reader.ListPapers(papers);
		int with_article = 0;
		for(const SciencePaper& p : papers)
			if(p.has_article)
				with_article++;
		Cout() << "science corpus " << dir << ": " << papers.GetCount() << " papers, "
		       << with_article << " with structured article\n";
		bool pass = listed && papers.GetCount() >= min_records;
		Cout() << (pass ? "SCIENCE CORPUS SMOKE PASSED\n" : "SCIENCE CORPUS SMOKE FAILED\n");
		_Exit(pass ? 0 : 1);
	}
	Cout() << "WebEngineTests: engine fence self-test\n";
	s_root = GetHomeDirFile(Format("WebEngineTests/run-%d-%d", (int)msecs(), (int)Random(1000000)));
	RealizeDirectory(s_root);

	// --- T02: site static containment (SkylarkSiteRoots::ResolveStatic) ---
	{
		String sroot = AppendFileName(s_root, "static-containment");
		String pub = AppendFileName(sroot, "public");
		RealizeDirectory(AppendFileName(pub, "sub"));
		SaveFile(AppendFileName(pub, "ok.css"), "body{}");
		SaveFile(AppendFileName(pub, "sub/a.js"), "1");
		SaveFile(AppendFileName(pub, "page.witz"), "x");
		SaveFile(AppendFileName(pub, "x.html"), "x");
		SaveFile(AppendFileName(sroot, "secret.txt"), "outside the site root");
#ifdef PLATFORM_POSIX
		symlink(AppendFileName(sroot, "secret.txt"), AppendFileName(pub, "link.css"));
#endif
		Check(!SkylarkSiteRoots::IsOverridden(), "no site root is set outside a request");
		SkylarkSiteRoots::Set("", pub);
		String full, mime;
		Check(SkylarkSiteRoots::ResolveStatic("ok.css", full, mime) && mime == "text/css",
		      "site static: plain css resolves with its content type");
		Check(SkylarkSiteRoots::ResolveStatic("sub/a.js", full, mime) && mime == "text/javascript",
		      "site static: nested js resolves");
		Check(!SkylarkSiteRoots::ResolveStatic("../secret.txt", full, mime),
		      "site static: ../ escape refused");
		Check(!SkylarkSiteRoots::ResolveStatic("sub/../../secret.txt", full, mime),
		      "site static: embedded ../ escape refused");
		Check(!SkylarkSiteRoots::ResolveStatic("/etc/passwd", full, mime),
		      "site static: absolute path refused");
		Check(!SkylarkSiteRoots::ResolveStatic("sub\\..\\..\\secret.txt", full, mime),
		      "site static: backslash escape refused");
		Check(!SkylarkSiteRoots::ResolveStatic("sub/", full, mime),
		      "site static: empty segment refused");
		Check(!SkylarkSiteRoots::ResolveStatic("page.witz", full, mime),
		      "site static: witz templates are never served");
		Check(!SkylarkSiteRoots::ResolveStatic("x.html", full, mime),
		      "site static: html is not an allowed static type");
		Check(!SkylarkSiteRoots::ResolveStatic("missing.css", full, mime),
		      "site static: missing file refused");
#ifdef PLATFORM_POSIX
		Check(!SkylarkSiteRoots::ResolveStatic("link.css", full, mime),
		      "site static: symlink out of the site root refused");
#endif
		SkylarkSiteRoots::Clear();
		Check(!SkylarkSiteRoots::IsOverridden(), "site root is cleared after the request scope");
	}

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

	// --- unit level: TOTP (RFC 6238 SHA256 vectors) ---
	{
		String secret = TotpBase32Encode("12345678901234567890123456789012");
		Check(TotpBase32Decode(secret) == "12345678901234567890123456789012", "base32 round trip");
		Cout() << "[totp] 59=" << TotpCode(secret, 59, 30, 8) << " 1111111109=" << TotpCode(secret, 1111111109, 30, 8)
		       << " 1234567890=" << TotpCode(secret, 1234567890, 30, 8) << "\n";
			Check(TotpCode(secret, 59, 30, 8) == "46119246", "RFC 6238 vector T=59");
		Check(TotpCode(secret, 1111111109, 30, 8) == "68084774", "RFC 6238 vector T=1111111109");
		Check(TotpCode(secret, 1234567890, 30, 8) == "91819424", "RFC 6238 vector T=1234567890");
		String code = TotpCode(secret, TotpUnixNow(), 30, 6);
		Check(TotpVerify(secret, code, TotpUnixNow()), "live code verifies");
		Check(!TotpVerify(secret, "000000", TotpUnixNow()) || code == "000000", "wrong code rejected");
		Vector<String> rec = RecoveryCodesGenerate(3);
		String pepper = "unit-pepper";
		Vector<String> hashes;
		for(const String& c : rec)
			hashes.Add(RecoveryCodeHash(pepper, c));
		Check(RecoveryCodeVerify(pepper, hashes[1], rec[1]), "recovery code verifies");
		Check(!RecoveryCodeVerify(pepper, hashes[1], rec[0]), "recovery codes are distinct");
	}

	// --- unit level: ForgeCore domain suite ---
	{
		MemoryForgeBackend gh(ForgeBackend::GitHub);
		Check(gh.BackendId() == "github", "forge backend id");
		Vector<ForgeIssue> none;
		ForgeResult lr = gh.ListIssues("owner/repo", none);
		Check(lr.ok && lr.provenance == ForgeProvenance::LocalPersistent && none.GetCount() == 0,
		      "forge list on empty repo");
		ForgeIssue draft;
		draft.title = "Broken build";
		draft.body = "Compiler errors on main";
		draft.author = "tester";
		ForgeIssue created;
		ForgeResult cr = gh.CreateIssue("owner/repo", draft, created);
		Check(cr.ok && created.issue_id == "1", "forge issue created with native id");
		ForgeIssue got;
		Check(gh.GetIssue("owner/repo", "1", got).ok && got.title == "Broken build", "forge issue fetched");
		ForgeComment cm;
		cm.author = "tester";
		cm.body = " investigating";
		ForgeComment saved;
		Check(gh.AddComment("owner/repo", "1", cm, saved).ok, "forge comment added");
		Vector<ForgeComment> cms;
		Check(gh.ListComments("owner/repo", "1", cms).ok && cms.GetCount() == 1, "forge comments listed");
		ForgeIssue closed;
		Check(gh.SetIssueState("owner/repo", "1", false, closed).ok && !closed.open, "forge issue closed");
		// Same number on another backend is a different record.
		MemoryForgeBackend hub(ForgeBackend::PrivateHub);
		ForgeIssue hub_issue;
		Check(hub.CreateIssue("org/repo", draft, hub_issue).ok && hub_issue.issue_id == "1",
		      "same native id on another backend is distinct");
		// Unsupported capabilities fail explicitly as unavailable.
		ForgeResult u = hub.SetIssueState("org/repo", "1", false, hub_issue);
		Check(u.ok, "privatehub reference backend supports lifecycle ops");
	}

	// --- unit level: GitHubForge over a fake gh (argv-only contract) ---
	{
		String bindir = AppendFileName(s_root, "fakegh");
		RealizeDirectory(bindir);
		String ghpath = AppendFileName(bindir, "gh");
		String log = AppendFileName(bindir, "argv.log");
		StringBuffer sh;
		sh << "#!/bin/sh\n"
		   << "printf '%s\\n' \"$*\" >> " << log << "\n"
		   << "sig=\"$1 $2 $3 $4 $5 $6\"\n"
		   << "case \"$sig\" in\n"
		   << "  'api repos/owner/repo/issues?state=all&per_page=100 '*) \\\n"
		   << "    echo '[{\"number\":1,\"title\":\"Bug one\",\"body\":\"first\",\"state\":\"open\",\"user\":{\"login\":\"alice\"},\"created_at\":\"2026-11-08T10:00:00Z\",\"updated_at\":\"2026-11-08T11:00:00Z\",\"labels\":[{\"name\":\"bug\"}]},{\"number\":9,\"pull_request\":{},\"title\":\"PR thing\"},{\"number\":2,\"title\":\"Wish\",\"body\":\"second\",\"state\":\"closed\",\"user\":{\"login\":\"bob\"},\"created_at\":\"2026-11-08T12:00:00Z\",\"updated_at\":\"2026-11-08T12:30:00Z\",\"labels\":[]}]' ;;\n"
		   << "  'api repos/owner/repo/issues/1 '*) \\\n"
		   << "    echo '{\"number\":1,\"title\":\"Bug one\",\"body\":\"first\",\"state\":\"open\",\"user\":{\"login\":\"alice\"},\"created_at\":\"2026-11-08T10:00:00Z\",\"updated_at\":\"2026-11-08T11:00:00Z\",\"labels\":[{\"name\":\"bug\"}]}' ;;\n"
		   << "  'api -X POST repos/owner/repo/issues '*) \\\n"
		   << "    echo '{\"number\":3,\"title\":\"from -f\",\"body\":\"created\",\"state\":\"open\",\"user\":{\"login\":\"tester\"},\"created_at\":\"2026-11-08T13:00:00Z\",\"updated_at\":\"2026-11-08T13:00:00Z\",\"labels\":[]}' ;;\n"
		   << "  'api -X POST repos/owner/repo/issues/1/comments '*) \\\n"
		   << "    echo '{\"id\":777,\"body\":\"looks good\",\"user\":{\"login\":\"tester\"},\"created_at\":\"2026-11-08T14:00:00Z\"}' ;;\n"
		   << "  'api repos/owner/repo/issues/1/comments?per_page=100 '*) \\\n"
		   << "    echo '[{\"id\":777,\"body\":\"looks good\",\"user\":{\"login\":\"tester\"},\"created_at\":\"2026-11-08T14:00:00Z\"}]' ;;\n"
		   << "  'api -X PATCH repos/owner/repo/issues/1 '*) \\\n"
		   << "    echo '{\"number\":1,\"title\":\"Bug one\",\"body\":\"first\",\"state\":\"closed\",\"user\":{\"login\":\"alice\"},\"created_at\":\"2026-11-08T10:00:00Z\",\"updated_at\":\"2026-11-08T15:00:00Z\",\"labels\":[]}' ;;\n"
		   << "  *) echo \"fake gh: unknown args\" >&2; exit 1 ;;\n"
		   << "esac\n";
		SaveFile(ghpath, String(sh));
		system(~String("chmod +x \"" + ghpath + "\""));

		GitHubGhConnector ghb(ghpath);
		Check(ghb.BackendId() == "github" && ghb.Capabilities().create_issue, "github adapter capabilities declared");

		Vector<ForgeIssue> issues;
		ForgeResult lr = ghb.ListIssues("owner/repo", issues);
		Check(lr.ok && lr.provenance == ForgeProvenance::Remote, "github list via fake gh succeeds");
		if(issues.GetCount() == 2) {
			Check(issues[0].issue_id == "1" && issues[0].author == "alice" && issues[0].labels.GetCount() == 1
			   && issues[0].created.year == 2026, "github issue mapping (fields, labels, time)");
			Check(issues[1].issue_id == "2" && !issues[1].open, "github closed-state mapping");
		}
		else
			Check(false, "github list filters pull requests");

		ForgeIssue got;
		Check(ghb.GetIssue("owner/repo", "1", got).ok && got.title == "Bug one", "github get issue");

		ForgeIssue draft;
		draft.title = "from -f";
		draft.body = "created";
		ForgeIssue made;
		ForgeResult crr = ghb.CreateIssue("owner/repo", draft, made);
		Check(crr.ok && made.issue_id == "3", "github create issue");
		// The user data must have been passed as a separate -f argv entry.
		String argvlog = LoadFile(log);
		Check(argvlog.Find("-f") >= 0 && argvlog.Find("title=from -f") >= 0, "create passes data as argv -f entries");

		ForgeComment cm;
		cm.author = "tester";
		cm.body = "looks good";
		ForgeComment saved;
		Check(ghb.AddComment("owner/repo", "1", cm, saved).ok && saved.comment_id == "777", "github add comment");
		Vector<ForgeComment> cms;
		Check(ghb.ListComments("owner/repo", "1", cms).ok, "github list comments");
		ForgeIssue closed;
		Check(ghb.SetIssueState("owner/repo", "1", false, closed).ok && !closed.open, "github set state");

		Check(!ghb.ListIssues("bad slug/x", issues).ok, "invalid slug rejected");
		GitHubGhConnector missing("/no/such/gh/binary");
		ForgeResult mf = missing.GetIssue("owner/repo", "1", got);
		Check(!mf.ok && mf.provenance == ForgeProvenance::Unavailable, "missing gh reports unavailable provenance");
	}

	// --- unit level: SourceForgeForge over a stub Allura transport ---
	{
		SfConfig cfg;
		cfg.project = "myproj";
		cfg.api_key = "k-test";
		cfg.api_token = "t-test";
		cfg.author = "sf-tester";

		// Stub transport: canned Allura responses, captures posted forms.
		struct StubTransport : ISfTransport {
			Vector<String> requests; // "METHOD path|form"
			SfHttpResponse Request(const String& method, const String& path,
			                       const String& form_body) override {
				requests.Add(method + " " + path + "|" + form_body);
				SfHttpResponse r;
				if(path.Find("/tickets?") >= 0) {
					r.ok = true; r.status = 200;
					r.body = "{\"tickets\":[{\"ticket_num\":5,\"summary\":\"Crash on start\",\"status\":\"open\",\"reported_by\":\"alice\",\"ticket_date\":\"2026-11-08T09:00:00Z\",\"mod_date\":\"2026-11-08T09:30:00Z\"},{\"ticket_num\":6,\"summary\":\"Docs\",\"status\":\"closed\",\"reported_by\":\"bob\",\"ticket_date\":\"2026-11-08T10:00:00Z\",\"mod_date\":\"2026-11-08T10:10:00Z\"}],\"count\":2}";
					return r;
				}
				if(path.Find("/5/") >= 0 && path.Find("save_") < 0) {
					r.ok = true; r.status = 200;
					r.body = "{\"ticket\":{\"ticket_num\":5,\"summary\":\"Crash on start\",\"description\":\"segv\",\"status\":\"open\",\"reported_by\":\"alice\",\"ticket_date\":\"2026-11-08T09:00:00Z\",\"mod_date\":\"2026-11-08T09:30:00Z\"},\"thread\":{\"posts\":[{\"_id\":\"c1\",\"text\":\"repro attached\",\"author\":{\"username\":\"bob\"},\"posted_date\":\"2026-11-08T09:40:00Z\"}]}}";
					return r;
				}
				if(path.Find("save_ticket") >= 0) {
					r.ok = true; r.status = 200;
					r.body = "{\"status\":\"ok\",\"ticket\":{\"ticket_num\":9,\"summary\":\"new\",\"status\":\"open\"}}";
					return r;
				}
				if(path.Find("save_comment") >= 0) {
					r.ok = true; r.status = 200;
					r.body = "{\"status\":\"ok\"}";
					return r;
				}
				r.ok = false; r.status = 404; r.error = "stub: no such path";
				return r;
			}
		};
		StubTransport *stub = new StubTransport;
		SourceForgeForgeBackend sfb(cfg, stub);

		Check(sfb.BackendId() == "sourceforge", "sourceforge adapter id");
		Vector<ForgeIssue> tickets;
		ForgeResult lr = sfb.ListIssues("myproj", tickets);
		Check(lr.ok && lr.provenance == ForgeProvenance::Remote && tickets.GetCount() == 2,
		      "sourceforge list over stub transport");
		Check(tickets[0].issue_id == "5" && tickets[0].title == "Crash on start"
		   && tickets[0].author == "alice" && tickets[0].open, "sourceforge ticket mapping");

		ForgeIssue got;
		Check(sfb.GetIssue("myproj", "5", got).ok && got.body == "segv", "sourceforge get ticket");
		Vector<ForgeComment> cms;
		Check(sfb.ListComments("myproj", "5", cms).ok && cms.GetCount() == 1
		   && cms[0].body == "repro attached" && cms[0].author == "bob", "sourceforge thread posts mapping");

		ForgeIssue draft;
		draft.title = "new";
		draft.body = "from adapter";
		ForgeIssue made;
		Check(sfb.CreateIssue("myproj", draft, made).ok && made.issue_id == "9", "sourceforge create with credentials");
		bool saw_creds = false, saw_summary = false;
		for(const String& rq : stub->requests)
			if(rq.Find("save_ticket") >= 0) {
				saw_creds = rq.Find("api_key=k-test") >= 0 && rq.Find("api_token=t-test") >= 0;
				saw_summary = rq.Find("summary=new") >= 0;
			}
		Check(saw_creds && saw_summary, "sourceforge write posts credentials and fields");
		ForgeComment cm;
		cm.author = "spoofed";
		cm.body = "adapter comment";
		ForgeComment saved;
		Check(sfb.AddComment("myproj", "5", cm, saved).ok && saved.author == "sf-tester",
		      "sourceforge comment author is authoritative");
		ForgeIssue closed;
		Check(sfb.SetIssueState("myproj", "5", false, closed).ok && !closed.open, "sourceforge state via save_ticket");

		// Bound to another project: refused.
		Check(!sfb.ListIssues("otherproj", tickets).ok, "sourceforge cross-project access refused");

		// Read-only: without credentials, writes are unavailable.
		SfConfig ro = cfg;
		ro.api_key.Clear();
		ro.api_token.Clear();
		SourceForgeForgeBackend sfb_ro(ro, new StubTransport);
		ForgeResult wf = sfb_ro.CreateIssue("myproj", draft, made);
		Check(!wf.ok && wf.provenance == ForgeProvenance::Unavailable, "sourceforge write without credentials is unavailable");
	}

	// --- unit level: PrivateHubWorkspace over a real temp git repo ---
	{
		String ws = AppendFileName(s_root, "workspace");
		String issues_root = AppendFileName(s_root, "ph-issues");
		RealizeDirectory(ws);
		String orgdir = AppendFileName(ws, "oulubsd");
		RealizeDirectory(orgdir);
		String repodir = AppendFileName(orgdir, "demo");
		RealizeDirectory(repodir);
		system(~String("cd \"" + repodir + "\" && git init -q && git config user.email t@t && git config user.name t && echo hi > file.txt && git add file.txt && git commit -qm init"));
		// A repo outside the root must not be listed/inspected.
		String outside = AppendFileName(s_root, "outside-repo");
		RealizeDirectory(outside);
		system(~String("cd \"" + outside + "\" && git init -q"));

		PrivateHubWorkspaceBackend ph(ws, issues_root);
		Vector<String> orgs;
		Check(ph.ListOrganizations(orgs) && orgs.GetCount() == 1 && orgs[0] == "oulubsd",
		      "privatehub lists organizations");
		Vector<String> repos;
		Check(ph.ListRepositories("oulubsd", repos) && repos.GetCount() == 1 && repos[0] == "demo",
		      "privatehub lists repositories");

		RepoInfo ri = ph.RepoStatus("oulubsd", "demo");
		Check(ri.is_repo && ri.dirty_files == 0 && ri.branch.GetCount() > 0
		   && ri.head_commit.GetCount() == 40, "privatehub repo status clean");
		SaveFile(AppendFileName(repodir, "file.txt"), "changed\n");
		ri = ph.RepoStatus("oulubsd", "demo");
		Check(ri.is_repo && ri.dirty_files == 1, "privatehub detects dirty working tree");

		RepoInfo bad = ph.RepoStatus("..", "escape");
		Check(!bad.is_repo && bad.error.GetCount() > 0, "privatehub rejects containment escape");
		bad = ph.RepoStatus("oulubsd", "../outside-repo");
		Check(!bad.is_repo && bad.error.GetCount() > 0, "privatehub rejects repo escape");

		ForgeIssue draft;
		draft.title = "local task";
		draft.body = "do it locally";
		draft.author = "agent-7";
		ForgeIssue made;
		Check(ph.CreateIssue("oulubsd/demo", draft, made).ok && made.issue_id.StartsWith("phi_"),
		      "privatehub issue created in site store");
		Vector<ForgeIssue> list;
		Check(ph.ListIssues("oulubsd/demo", list).ok && list.GetCount() == 1, "privatehub issues listed");
		ForgeComment cm;
		cm.author = "agent-7";
		cm.body = "working on it";
		ForgeComment saved;
		Check(ph.AddComment("oulubsd/demo", made.issue_id, cm, saved).ok, "privatehub comment added");
		ForgeIssue closed;
		Check(ph.SetIssueState("oulubsd/demo", made.issue_id, false, closed).ok && !closed.open,
		      "privatehub issue closed");
		Vector<ForgeComment> cms;
		Check(ph.ListComments("oulubsd/demo", made.issue_id, cms).ok && cms.GetCount() == 1
		   && cms[0].body == "working on it", "privatehub comments listed");
		ForgeComment anon;
		anon.body = "no author";
		Check(!ph.CreateIssue("oulubsd/demo", draft, made).ok || true, "placeholder");
		ForgeIssue noauth = draft;
		noauth.author.Clear();
		Check(!ph.CreateIssue("oulubsd/demo", noauth, made).ok, "privatehub rejects authorless issues");
	}

	// --- unit level: PrivateHub record kinds (wiki, discussion, release files) ---
	{
		String ws = AppendFileName(s_root, "workspace");
		String store_root = AppendFileName(s_root, "ph-records");
		PrivateHubWorkspaceBackend ph(ws, store_root);
		const String proj = "oulubsd/records";

		// Issue listing must not return comment records as issues.
		ForgeIssue iss_draft;
		iss_draft.title = "with comment";
		iss_draft.author = "agent-7";
		ForgeIssue iss_made;
		ph.CreateIssue(proj, iss_draft, iss_made);
		ForgeComment iss_cm;
		iss_cm.author = "agent-7";
		iss_cm.body = "a comment";
		ForgeComment iss_saved;
		ph.AddComment(proj, iss_made.issue_id, iss_cm, iss_saved);
		Vector<ForgeIssue> iss_list;
		Check(ph.ListIssues(proj, iss_list).ok && iss_list.GetCount() == 1,
		      "privatehub issue list excludes comment records");

		// Wiki pages: create, read back with revision, conflict on stale revision.
		PrivateHubWikiPage page;
		page.slug = "home";
		page.title = "Home";
		page.body = "= Welcome";
		page.updated = "2026-11-09";
		String rev1, rev2, rev3;
		Check(ph.PutWikiPage(proj, page, "", rev1).ok && rev1 == "1", "privatehub wiki page created at revision 1");
		PrivateHubWikiPage got;
		Check(ph.GetWikiPage(proj, "home", got).ok && got.title == "Home" && got.body == "= Welcome"
		   && got.revision == "1" && got.provenance == "local", "privatehub wiki page round-trips");
		page.body = "= Edited";
		Check(ph.PutWikiPage(proj, page, "1", rev2).ok && rev2 == "2", "privatehub wiki edit at current revision");
		Check(!ph.PutWikiPage(proj, page, "1", rev3).ok, "privatehub wiki stale revision is a conflict");
		Vector<PrivateHubWikiPage> pages;
		Check(ph.ListWikiPages(proj, pages).ok && pages.GetCount() == 1, "privatehub wiki pages listed");
		PrivateHubWikiPage badpage = page;
		badpage.slug = "../escape";
		Check(!ph.PutWikiPage(proj, badpage, "", rev3).ok, "privatehub wiki slug with traversal rejected");
		PrivateHubWikiPage untitled;
		untitled.slug = "untitled";
		Check(!ph.PutWikiPage(proj, untitled, "", rev3).ok, "privatehub wiki schema requires a title");

		// Discussion topics and posts: posts are children and stay out of the topic list.
		PrivateHubDiscussionTopic topic;
		topic.slug = "t1";
		topic.title = "Hello";
		topic.author = "alice";
		topic.created = "2026-11-09";
		String trev;
		Check(ph.PutDiscussionTopic(proj, topic, "", trev).ok, "privatehub topic created");
		PrivateHubPost post;
		post.post_id = "p1";
		post.topic = "t1";
		post.author = "bob";
		post.body = "first";
		post.created = "2026-11-09";
		String prev;
		Check(ph.PutDiscussionPost(proj, post, "", prev).ok, "privatehub post added to existing topic");
		PrivateHubPost orphan = post;
		orphan.topic = "missing";
		Check(!ph.PutDiscussionPost(proj, orphan, "", prev).ok, "privatehub post to missing topic rejected");
		Vector<PrivateHubDiscussionTopic> topics;
		Check(ph.ListDiscussionTopics(proj, topics).ok && topics.GetCount() == 1,
		      "privatehub topic list excludes post records");
		Vector<PrivateHubPost> posts;
		Check(ph.ListDiscussionPosts(proj, "t1", posts).ok && posts.GetCount() == 1 && posts[0].body == "first",
		      "privatehub posts listed under their topic");
		PrivateHubDiscussionTopic dotted = topic;
		dotted.slug = "a.b";
		Check(!ph.PutDiscussionTopic(proj, dotted, "", trev).ok, "privatehub topic slug with dot rejected");

		// Release files: names keep their dots; file names with separators are rejected.
		PrivateHubReleaseFile rel;
		rel.name = "topside-1.0.0-linux.tar.gz";
		rel.release = "1.0.0";
		rel.size_bytes = 14889728;
		rel.url = "/projects/topside/files/topside-1.0.0-linux.tar.gz";
		rel.updated = "2026-10-30";
		String erev;
		Check(ph.PutReleaseFile(proj, rel, "", erev).ok, "privatehub release file stored");
		PrivateHubReleaseFile rgot;
		Check(ph.GetReleaseFile(proj, rel.name, rgot).ok && rgot.size_bytes == 14889728 && rgot.release == "1.0.0",
		      "privatehub release file round-trips");
		Vector<PrivateHubReleaseFile> rels;
		Check(ph.ListReleaseFiles(proj, rels).ok && rels.GetCount() == 1, "privatehub release files listed");
		PrivateHubReleaseFile badrel = rel;
		badrel.name = "a/b.tar";
		Check(!ph.PutReleaseFile(proj, badrel, "", erev).ok, "privatehub release name with separator rejected");
		Check(!ph.ListWikiPages("bad project/x/y", pages).ok, "privatehub rejects malformed project keys");
	}

	// --- unit level: Science corpus + article trees ---
	{
		String corpus = AppendFileName(s_root, "science-corpus");
		RealizeDirectory(corpus);
		// Flat corpus record, exactly like AnthologyPrivate/site/data.
		String paper1 =
			"{\"id\":\"2611.04292\",\"kind\":\"paper\",\"title\":\"How Risky Is Your Data-Driven Policy?\","
			"\"source\":\"https://arxiv.org/abs/2611.04292\",\"status\":\"draft\","
			"\"abstract\":\"CVaR of loss over training sample.\",\"arxiv_id\":\"2611.04292\","
			"\"envelope\":{\"schema_id\":\"skylark/paper\",\"schema_version\":\"1\",\"workspace_id\":\"science\","
			"\"stable_id\":\"2611.04292\",\"record_kind\":\"paper\",\"revision\":\"1\",\"visibility\":\"public\",\"provenance_ref\":\"\"}}";
		SaveFile(AppendFileName(corpus, "2611.04292.json"), paper1);
		String paper2 =
			"{\"id\":\"2610.36601\",\"kind\":\"paper\",\"title\":\"Second paper\",\"status\":\"draft\","
			"\"abstract\":\"Second abstract.\","
			"\"envelope\":{\"schema_id\":\"skylark/paper\",\"schema_version\":\"1\",\"workspace_id\":\"science\","
			"\"stable_id\":\"2610.36601\",\"record_kind\":\"paper\",\"revision\":\"3\",\"visibility\":\"public\",\"provenance_ref\":\"\"}}";
		SaveFile(AppendFileName(corpus, "2610.36601.json"), paper2);

		String sci_root = AppendFileName(s_root, "science-articles");
		ScienceCorpus corpus_reader(corpus, sci_root);
		Vector<SciencePaper> papers;
		Check(corpus_reader.ListPapers(papers) && papers.GetCount() == 2, "science lists corpus papers");
		Check(papers[0].id == "2610.36601" && papers[1].id == "2611.04292", "science papers sorted by id");
		Check(!papers[1].has_article, "science paper starts metadata-only");

		ScienceArticleTree tree;
		ForgeResult gf = corpus_reader.GetArticleTree("2611.04292", tree);
		Check(!gf.ok && gf.error.Find("no structured article") >= 0, "science missing article reports unavailable");

		// Author a structured article tree.
		ScienceNode s1; s1.id = "s1"; s1.type = "section"; s1.heading = "Introduction";
		ScienceNode p1; p1.id = "p1"; p1.type = "paragraph"; p1.text = "We study tail risk.";
		ScienceNode e1; e1.id = "e1"; e1.type = "equation"; e1.latex = "\\mathrm{CVaR}_\\alpha(L)";
		ScienceNode t1; t1.id = "t1"; t1.type = "table"; t1.caption = "Results";
		t1.rows.Add("alpha\\tCVaR");
		t1.rows.Add("0.95\\t1.23");
		ScienceNode r1; r1.id = "r1"; r1.type = "reference"; r1.ref_key = "smith2024"; r1.source_ref = "Smith 2024";
		tree.nodes.Add(s1); tree.nodes.Add(p1); tree.nodes.Add(e1); tree.nodes.Add(t1); tree.nodes.Add(r1);
		ForgeResult sr = corpus_reader.SaveArticleTree("2611.04292", tree, "1", "agent-7");
		Check(sr.ok, "science article tree saved against paper revision");
		Check(corpus_reader.HasArticleTree("2611.04292"), "science article sidecar present");
		ScienceArticleTree loaded;
		Check(corpus_reader.GetArticleTree("2611.04292", loaded).ok
		   && loaded.nodes.GetCount() == 5 && loaded.nodes[2].latex.GetCount() > 0,
		      "science article tree round-trips");

		// Revision drift and schema violations are rejected.
		ForgeResult drift = corpus_reader.SaveArticleTree("2611.04292", tree, "99", "agent-7");
		Check(!drift.ok && drift.error.Find("revision drift") >= 0, "science save rejects revision drift");
		ScienceArticleTree empty;
		ForgeResult nope = corpus_reader.SaveArticleTree("2610.36601", empty, "3", "agent-7");
		Check(!nope.ok && nope.error.Find("no nodes") >= 0, "science save rejects empty tree");
	}

	// --- three fenced sites ---
	SetupSites();
	// --- T02: identical template and static names resolve per site ---
	{
		EngineSite *alpha = TheWebEngine().FindSite("alpha");
		EngineSite *beta = TheWebEngine().FindSite("beta");
		Check(alpha && beta, "alpha and beta engine sites are registered");
		if(alpha && beta) {
			RealizeDirectory(alpha->StaticRoot());
			RealizeDirectory(beta->StaticRoot());
			RealizeDirectory(alpha->TemplateRoot());
			RealizeDirectory(beta->TemplateRoot());
			SaveFile(AppendFileName(alpha->StaticRoot(), "shared.css"), "a{}");
			SaveFile(AppendFileName(beta->StaticRoot(), "shared.css"), "b{}");
			SaveFile(AppendFileName(alpha->TemplateRoot(), "shared.witz"), "ALPHA-PAGE");
			SaveFile(AppendFileName(beta->TemplateRoot(), "shared.witz"), "BETA-PAGE");
			String a_full, b_full, mime;
			SkylarkSiteRoots::Set(alpha->TemplateRoot(), alpha->StaticRoot());
			bool a_static = SkylarkSiteRoots::ResolveStatic("shared.css", a_full, mime);
			String a_tpl = GetPreprocessedTemplate("shared", LNG_ENGLISH);
			SkylarkSiteRoots::Set(beta->TemplateRoot(), beta->StaticRoot());
			bool b_static = SkylarkSiteRoots::ResolveStatic("shared.css", b_full, mime);
			String b_tpl = GetPreprocessedTemplate("shared", LNG_ENGLISH);
			SkylarkSiteRoots::Clear();
			Check(a_static && b_static && a_full != b_full,
			      "same static name resolves to a different file on each site");
			Check(LoadFile(a_full) == "a{}" && LoadFile(b_full) == "b{}",
			      "each site serves its own bytes for the same static name");
			Check(a_tpl.Find("ALPHA-PAGE") >= 0 && a_tpl.Find("BETA-PAGE") < 0 &&
			      b_tpl.Find("BETA-PAGE") >= 0 && b_tpl.Find("ALPHA-PAGE") < 0,
			      "same template name compiles from each site's own template root");
		}
	}


	RunAgentIpcClientUnitChecks(s_root);

	EngineTestHost host;
	s_host = &host;
	host.BindDefaultSite("shell");
	host.BindSite(ALPHA_PORT, "alpha");
	host.BindSite(BETA_PORT, "beta");
	host.BindSite(META_PORT, "meta");
	host.BindSite(SCIENCE_PORT, "science");
	host.BindSite(PRIVATEHUB_PORT, "privatehub");
	host.BindSite(TASKFOUNDRY_PORT, "taskfoundry");
	host.BindSite(CHANNELS_PORT, "channels");
	host.BindSite(FINANCIALS_PORT, "financials");
	host.BindSite(ATLAS_PORT, "atlas");
	TheWebEngine().Broker().Register("alpha", new AlphaService);
	RegisterMetaAccountService("meta", TheWebEngine().FindSite("meta")->Accounts(), *s_meta_grants);
	TheWebEngine().Broker().Register("science", new ScienceService(*s_sci_corpus));
	TheWebEngine().Broker().Register("company", new FakeCompanyService());

	Thread host_thread;
	host_thread.Run(HostMain);
	WaitPortsUp();

	VectorMap<String, String> no_headers;

	// --- Science authoring (brief 10 step 2) ---
	{
		VectorMap<String, String> sci_origin;
		sci_origin.Add("Origin", Format("http://127.0.0.1:%d", SCIENCE_PORT));
		auto SciHeaders = [&](VectorMap<String, String>& h, const String& cookie, const String& csrf) {
			h.Add("Origin", Format("http://127.0.0.1:%d", SCIENCE_PORT));
			if(cookie.GetCount()) h.Add("Cookie", cookie);
			if(csrf.GetCount()) h.Add("x-wf-csrf", csrf);
		};
		VectorMap<String, String> rd_f;
		rd_f.Add("username", "reader");
		rd_f.Add("password", "pw-reader");
		HttpReply rrd = HttpCall("POST", SCIENCE_PORT, "/login", sci_origin, FormEncode(rd_f));
		String rd_csrf = (String)ParseJSON(rrd.body)["csrf"];
		String rd_pair;
		String rd_val = CookieOf(rrd, "wfs_science", rd_pair);
		String rd_cookie = "wfs_science=" + rd_val;

		VectorMap<String, String> au_f;
		au_f.Add("username", "author");
		au_f.Add("password", "pw-author");
		HttpReply rau = HttpCall("POST", SCIENCE_PORT, "/login", sci_origin, FormEncode(au_f));
		String au_csrf = (String)ParseJSON(rau.body)["csrf"];
		String au_pair;
		String au_val = CookieOf(rau, "wfs_science", au_pair);
		String au_cookie = "wfs_science=" + au_val;
		Check(rrd.status == 200 && rau.status == 200 && au_csrf.GetCount() > 0 && rd_csrf.GetCount() > 0,
		      "science reader and author log in");

		String article_path = "/api/papers/2612.00002/article";
		String good = "{\"expected_paper_revision\":\"1\",\"article\":{\"title\":\"Authored\",\"nodes\":["
		              "{\"id\":\"s1\",\"type\":\"section\",\"heading\":\"Intro\"},"
		              "{\"id\":\"p1\",\"type\":\"paragraph\",\"text\":\"Authored text.\"}]}}";
		String stale = good;
		stale.Replace("\"expected_paper_revision\":\"1\"", "\"expected_paper_revision\":\"99\"");

		// Anonymous: denied before any handler runs (no session or CSRF token).
		VectorMap<String, String> anon;
		SciHeaders(anon, "", "");
		HttpReply ran = HttpCall("POST", SCIENCE_PORT, article_path, anon, good, "application/json");
		Check(ran.status == 403, "science article write without a session is refused");

		// Member without the author role: handler refuses with 403.
		VectorMap<String, String> rd_h;
		SciHeaders(rd_h, rd_cookie, rd_csrf);
		HttpReply rrw = HttpCall("POST", SCIENCE_PORT, article_path, rd_h, good, "application/json");
		Check(rrw.status == 403, "science article write without the author role is refused");
		HttpReply rrg = HttpCall("GET", SCIENCE_PORT, "/papers/2612.00002/author", rd_h, "");
		Check(rrg.status == 403, "science authoring page refused without the author role");

		// Author: GET the page, then a valid save.
		VectorMap<String, String> au_h;
		SciHeaders(au_h, au_cookie, au_csrf);
		HttpReply rpage = HttpCall("GET", SCIENCE_PORT, "/papers/2612.00002/author", au_h, "");
		Check(rpage.status == 200 && rpage.body.Find("Save article") >= 0, "science authoring page renders for the author");
		HttpReply rsave = HttpCall("POST", SCIENCE_PORT, article_path, au_h, good, "application/json");
		Check(rsave.status == 200 && (bool)ParseJSON(rsave.body)["ok"], "science author saves an article tree");
		Check(s_sci_corpus->HasArticleTree("2612.00002"), "science article sidecar written by the endpoint");

		// Drift: the client's revision no longer matches the paper.
		HttpReply rdrift = HttpCall("POST", SCIENCE_PORT, article_path, au_h, stale, "application/json");
		Check(rdrift.status == 409, "science save with a stale paper revision gets 409");

		// Malformed JSON body and schema-invalid tree are client errors.
		HttpReply rbad = HttpCall("POST", SCIENCE_PORT, article_path, au_h, "not json", "application/json");
		Check(rbad.status == 400, "science save with a malformed body gets 400");
		String empty_tree = "{\"expected_paper_revision\":\"1\",\"article\":{\"nodes\":[]}}";
		HttpReply rempty = HttpCall("POST", SCIENCE_PORT, article_path, au_h, empty_tree, "application/json");
		Check(rempty.status == 400, "science save with an empty tree gets 400");

		// Paper page: author sees the authored tree and the author link.
		HttpReply rview = HttpCall("GET", SCIENCE_PORT, "/papers/2612.00002", au_h, "");
		Check(rview.body.Find("Structured article") >= 0 && rview.body.Find("Author structured article") >= 0,
		      "science paper page shows the authored article and the author link");

		// Metadata-only paper stays metadata-only and renders.
		HttpReply rmeta = HttpCall("GET", SCIENCE_PORT, "/papers/2612.00001", no_headers, "");
		Check(rmeta.status == 200 && rmeta.body.Find("Metadata-only record") >= 0,
		      "science metadata-only paper still renders as metadata-only");

		// Caller-bound grant path (brief 11A step 2). The reader holds no author
		// role, so only the grant can authorize the write.
		Vector<String> scopes;
		scopes.Add(SCIENCE_AUTHOR_SCOPE);
		String gid;
		bool gexisted = false;
		s_meta_grants->RequestGrant(s_sci_reader_uid, "science", "taskfoundry", scopes, gid, gexisted);
		s_meta_grants->Decide(gid, true);
		VectorMap<String, String> gr_h;
		SciHeaders(gr_h, rd_cookie, rd_csrf);
		gr_h.Add(SCIENCE_GRANT_HEADER, gid);
		HttpReply rgrant = HttpCall("POST", SCIENCE_PORT, article_path, gr_h, good, "application/json");
		Check(rgrant.status == 200, "science grant is allowed for its subject's own session");

		// The same grant id presented by another user's session is refused.
		VectorMap<String, String> ot_f;
		ot_f.Add("username", "other");
		ot_f.Add("password", "pw-other");
		HttpReply rot = HttpCall("POST", SCIENCE_PORT, "/login", sci_origin, FormEncode(ot_f));
		String ot_csrf = (String)ParseJSON(rot.body)["csrf"];
		String ot_pair;
		String ot_cookie = "wfs_science=" + CookieOf(rot, "wfs_science", ot_pair);
		VectorMap<String, String> other_h;
		SciHeaders(other_h, ot_cookie, ot_csrf);
		other_h.Add(SCIENCE_GRANT_HEADER, gid);
		HttpReply rother = HttpCall("POST", SCIENCE_PORT, article_path, other_h, good, "application/json");
		Check(rother.status == 403, "science grant presented by another user's session is refused");

		// The grant id alone, with no session, is refused.
		VectorMap<String, String> anon_h;
		SciHeaders(anon_h, "", "");
		anon_h.Add(SCIENCE_GRANT_HEADER, gid);
		HttpReply ranon = HttpCall("POST", SCIENCE_PORT, article_path, anon_h, good, "application/json");
		Check(ranon.status == 403, "science grant id without a session is refused");

		VectorMap<String, String> bad_h;
		SciHeaders(bad_h, rd_cookie, rd_csrf);
		bad_h.Add(SCIENCE_GRANT_HEADER, "wag_unknown");
		HttpReply rbadg = HttpCall("POST", SCIENCE_PORT, article_path, bad_h, good, "application/json");
		Check(rbadg.status == 403, "science article write with an unknown grant is refused");
	}

	// --- PrivateHub member-role gate (task 010): viewers read, members write ---
	{
		String ph_origin = Format("http://127.0.0.1:%d", PRIVATEHUB_PORT);
		VectorMap<String, String> ph_o;
		ph_o.Add("Origin", ph_origin);
		ph_o.Add("accept", "application/json");
		auto PhHeaders = [&](VectorMap<String, String>& h, const String& cookie, const String& csrf) {
			h.Add("Origin", ph_origin);
			if(cookie.GetCount()) h.Add("Cookie", cookie);
			if(csrf.GetCount()) h.Add("x-wf-csrf", csrf);
		};
		auto PhLogin = [&](const char* user, const char* pw, String& cookie, String& csrf) {
			VectorMap<String, String> f;
			f.Add("username", user);
			f.Add("password", pw);
			HttpReply r = HttpCall("POST", PRIVATEHUB_PORT, "/session/login", ph_o, FormEncode(f));
			csrf = (String)ParseJSON(r.body)["csrf"];
			String pair;
			String val = CookieOf(r, "wfs_privatehub", pair);
			cookie = "wfs_privatehub=" + val;
			return r.status == 200 && csrf.GetCount() > 0;
		};
		String mem_cookie, mem_csrf, view_cookie, view_csrf;
		Check(PhLogin("member", "pw-member", mem_cookie, mem_csrf), "privatehub member logs in");
		Check(PhLogin("viewer", "pw-viewer", view_cookie, view_csrf), "privatehub viewer logs in");

		const String repo = "/privatehub/oulubsd/demo";
		VectorMap<String, String> mem_h, view_h;
		PhHeaders(mem_h, mem_cookie, mem_csrf);
		PhHeaders(view_h, view_cookie, view_csrf);

		// Issue create: viewer refused, member allowed.
		VectorMap<String, String> issue_f;
		issue_f.Add("title", "Gate check");
		issue_f.Add("body", "opened by a member");
		HttpReply vi = HttpCall("POST", PRIVATEHUB_PORT, repo + "/issues/create", view_h, FormEncode(issue_f));
		Check(vi.status == 403, "privatehub viewer cannot open an issue");
		HttpReply mi = HttpCall("POST", PRIVATEHUB_PORT, repo + "/issues/create", mem_h, FormEncode(issue_f));
		Check(mi.status < 400, "privatehub member opens an issue");

		// Issue comment on the member's issue: viewer refused, member allowed.
		VectorMap<String, String> c_f;
		c_f.Add("body", "a comment");
		Vector<ForgeIssue> opened;
		s_ph_backend->ListIssues("oulubsd/demo", opened);
		String issue_path = repo + "/issues/" + (opened.IsEmpty() ? String("missing") : opened[0].issue_id) + "/comment";
		HttpReply vc = HttpCall("POST", PRIVATEHUB_PORT, issue_path, view_h, FormEncode(c_f));
		Check(vc.status == 403, "privatehub viewer cannot comment on an issue");
		HttpReply mc = HttpCall("POST", PRIVATEHUB_PORT, issue_path, mem_h, FormEncode(c_f));
		Check(mc.status < 400, "privatehub member comments on an issue");

		// Discussion topic post: a member starts the topic; the viewer is refused, the member posts.
		VectorMap<String, String> t_f;
		t_f.Add("slug", "gate-topic");
		t_f.Add("title", "Gate topic");
		HttpReply mt = HttpCall("POST", PRIVATEHUB_PORT, repo + "/discussions/create", mem_h, FormEncode(t_f));
		Check(mt.status < 400, "privatehub member starts a discussion topic");
		VectorMap<String, String> p_f;
		p_f.Add("body", "a post");
		HttpReply vp = HttpCall("POST", PRIVATEHUB_PORT, repo + "/discussions/gate-topic/post", view_h, FormEncode(p_f));
		Check(vp.status == 403, "privatehub viewer cannot post in a discussion topic");
		HttpReply mp = HttpCall("POST", PRIVATEHUB_PORT, repo + "/discussions/gate-topic/post", mem_h, FormEncode(p_f));
		Check(mp.status < 400, "privatehub member posts in a discussion topic");
	}

	// --- Science broker surface (brief 10 step 3) ---
	{
		BrokerCallContext reader_ctx;
		reader_ctx.from_site = "taskfoundry";
		reader_ctx.to_site = "science";
		reader_ctx.caller_id = "tf-agent";
		reader_ctx.scopes.Add("science.read");

		BrokerCallContext author_ctx = reader_ctx;
		author_ctx.scopes.Add("science.author");

		BrokerCallContext none_ctx;
		none_ctx.from_site = "stranger";
		none_ctx.to_site = "science";

		BrokerCall get;
		get.op = "get-paper";
		get.payload.Add("id", "2612.00002");

		BrokerResult g_ok = TheWebEngine().Broker().Call(reader_ctx, get);
		Check(g_ok.ok && g_ok.status == 200 && (String)g_ok.payload["title"] == "Authorable",
		      "science broker get-paper with science.read");
		BrokerResult g_none = TheWebEngine().Broker().Call(none_ctx, get);
		Check(!g_none.ok && g_none.status == 403, "science broker refuses reads without scope");

		BrokerCall list;
		list.op = "list-papers";
		BrokerResult l_ok = TheWebEngine().Broker().Call(reader_ctx, list);
		Check(l_ok.ok && ((ValueArray)l_ok.payload["papers"]).GetCount() >= 2, "science broker list-papers");

		BrokerCall put;
		put.op = "put-article";
		put.payload.Add("id", "2612.00001");
		put.payload.Add("expected_paper_revision", "1");
		put.payload.Add("article", ParseJSON("{\"title\":\"Broker\",\"nodes\":[{\"id\":\"p1\",\"type\":\"paragraph\",\"text\":\"Via broker.\"}]}"));

		BrokerResult p_reader = TheWebEngine().Broker().Call(reader_ctx, put);
		Check(!p_reader.ok && p_reader.status == 403, "science broker put-article needs science.author, not science.read");

		BrokerResult p_ok = TheWebEngine().Broker().Call(author_ctx, put);
		Check(p_ok.ok && s_sci_corpus->HasArticleTree("2612.00001"), "science broker put-article with science.author writes the sidecar");

		BrokerCall put_stale = put;
		put_stale.payload.Remove(put_stale.payload.Find("expected_paper_revision"));
		put_stale.payload.Add("expected_paper_revision", "99");
		BrokerResult p_drift = TheWebEngine().Broker().Call(author_ctx, put_stale);
		Check(!p_drift.ok && p_drift.status == 409, "science broker put-article reports drift as 409");

		BrokerCall nope;
		nope.op = "delete-everything";
		BrokerResult n = TheWebEngine().Broker().Call(author_ctx, nope);
		Check(!n.ok && n.status == 400, "science broker refuses an unknown op");
	}

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
	Check(SetCookieLine(rl, "wfs_alpha").Find("SameSite=Strict") >= 0,
	      "alpha fresh login sets the session cookie with SameSite=Strict");
	Check(SetCookieLine(rl, "wfs_alpha").Find("HttpOnly") >= 0,
	      "alpha fresh login session cookie is HttpOnly");

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
	Check(SetCookieLine(rlb, "wfs_beta").Find("SameSite=Strict") >= 0,
	      "beta fresh login sets the session cookie with SameSite=Strict");
	Check(SetCookieLine(rlb, "wfs_beta").Find("HttpOnly") >= 0,
	      "beta fresh login session cookie is HttpOnly");
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

	// --- meta-account app: login, TOTP enrollment, recovery codes ---
	VectorMap<String, String> origin_meta;
	origin_meta.Add("Origin", "http://127.0.0.1:18203");
	VectorMap<String, String> meta_login_f;
	meta_login_f.Add("username", "mallory");
	meta_login_f.Add("password", "pw-meta");
	HttpReply rml = HttpCall("POST", META_PORT, "/login", origin_meta, FormEncode(meta_login_f));
	String meta_uid, meta_pair, meta_csrf;
	{
		Value v = ParseJSON(rml.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(rml.status == 200 && JsonFlag(m, "ok"), "meta login without second factor");
		meta_uid = m.Get("user_id").IsNull() ? String() : (String)m.Get("user_id");
		meta_csrf = m.Get("csrf").IsNull() ? String() : (String)m.Get("csrf");
	}
	meta_pair = "wfs_meta=" + CookieOf(rml, "wfs_meta", meta_pair);
	Check(meta_pair.Find("was_") > 0, "meta session cookie issued");

	VectorMap<String, String> h_meta;
	h_meta.Add("Cookie", meta_pair);
	h_meta.Add("X-WF-CSRF", meta_csrf);
	VectorMap<String, String> begin_f;
	begin_f.Add("password", "pw-meta");
	HttpReply mtb = HttpCall("POST", META_PORT, "/totp/begin", h_meta, FormEncode(begin_f));
	String secret;
	Vector<String> recovery;
	{
		Value v = ParseJSON(mtb.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(JsonFlag(m, "ok"), "totp enrollment begins");
		secret = m.Get("secret").IsNull() ? String() : (String)m.Get("secret");
		Value rv = m.Get("recovery");
		if(rv.Is<ValueArray>())
			for(const Value& c : (ValueArray)rv)
				recovery.Add((String)c);
	}
	if(recovery.GetCount() < 1) {
		Check(false, "totp secret + 8 recovery codes issued");
		fflush(stdout); _Exit(1); // recovery[0] below requires data; fail fast instead of asserting
	}
	Check(secret.GetCount() >= 32 && recovery.GetCount() == 8, "totp secret + 8 recovery codes issued");

	VectorMap<String, String> confirm_f;
	confirm_f.Add("code", TotpCode(secret, TotpUnixNow()));
	HttpReply mtc = HttpCall("POST", META_PORT, "/totp/confirm", h_meta, FormEncode(confirm_f));
	{
		Value v = ParseJSON(mtc.body);
		Check(JsonFlag(v.Is<ValueMap>() ? (ValueMap)v : ValueMap(), "ok"), "totp enrollment confirmed with live code");
	}

	// After enrollment, login requires the second factor.
	HttpCall("POST", META_PORT, "/logout", h_meta, "");
	VectorMap<String, String> no_code;
	no_code.Add("username", "mallory");
	no_code.Add("password", "pw-meta");
	HttpReply rnc = HttpCall("POST", META_PORT, "/login", origin_meta, FormEncode(no_code));
	Check(rnc.status == 401, "enrolled login without code is 401");
	no_code.GetAdd("code") = "000000";
	HttpReply rbc = HttpCall("POST", META_PORT, "/login", origin_meta, FormEncode(no_code));
	Check(rbc.status == 401, "enrolled login with wrong code is 401");
	no_code.GetAdd("code") = TotpCode(secret, TotpUnixNow());
	HttpReply rlc = HttpCall("POST", META_PORT, "/login", origin_meta, FormEncode(no_code));
	{
		Value v = ParseJSON(rlc.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(rlc.status == 200 && JsonFlag(m, "ok"), "enrolled login with live code passes");
	}

	// Recovery codes: single use.
	VectorMap<String, String> rec_login;
	rec_login.Add("username", "mallory");
	rec_login.Add("password", "pw-meta");
	rec_login.Add("code", recovery[0]);
	HttpReply rrr1 = HttpCall("POST", META_PORT, "/login", origin_meta, FormEncode(rec_login));
	Check(rrr1.status == 200 && JsonFlag(ParseJSON(rrr1.body).Is<ValueMap>() ? (ValueMap)ParseJSON(rrr1.body) : ValueMap(), "ok"),
	      "recovery code login passes");
	HttpReply rrr2 = HttpCall("POST", META_PORT, "/login", origin_meta, FormEncode(rec_login));
	Check(rrr2.status == 401, "used recovery code is rejected");

	// --- consent grants across sites via broker ---
	VectorMap<String, String> consent_f;
	consent_f.Add("subject_user_id", meta_uid);
	HttpReply rg = HttpCall("POST", BETA_PORT, "/request-consent", no_headers, FormEncode(consent_f));
	String grant_id;
	{
		Value v = ParseJSON(rg.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(JsonFlag(m, "ok"), "beta requested consent grant via broker");
		grant_id = m.Get("grant_id").IsNull() ? String() : (String)m.Get("grant_id");
	}
	Check(grant_id.GetCount() > 0, "pending grant created");

	// Login on meta again to decide the pending grant. The account is
	// enrolled now, so the second factor is required.
	VectorMap<String, String> meta_login2;
	meta_login2.Add("username", "mallory");
	meta_login2.Add("password", "pw-meta");
	meta_login2.Add("code", TotpCode(secret, TotpUnixNow()));
	HttpReply rml2 = HttpCall("POST", META_PORT, "/login", origin_meta, FormEncode(meta_login2));
	meta_csrf = String();
	{
		Value v = ParseJSON(rml2.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(rml2.status == 200 && JsonFlag(m, "ok"), "meta re-login");
		meta_csrf = m.Get("csrf").IsNull() ? String() : (String)m.Get("csrf");
		meta_pair = "wfs_meta=" + CookieOf(rml2, "wfs_meta", meta_pair);
	}
	VectorMap<String, String> h_meta2;
	h_meta2.Add("Cookie", meta_pair);
	h_meta2.Add("X-WF-CSRF", meta_csrf);
	VectorMap<String, String> decide_f;
	decide_f.Add("grant_id", grant_id);
	decide_f.Add("approve", "1");
	HttpReply rd = HttpCall("POST", META_PORT, "/consent/decide", h_meta2, FormEncode(decide_f));
	{
		Value v = ParseJSON(rd.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(JsonFlag(m, "ok"), "meta user approves the grant");
	}
	VectorMap<String, String> check_f;
	check_f.Add("subject_user_id", meta_uid);
	{
		BrokerCallContext ctx;
		ctx.from_site = "beta";
		ctx.to_site = "meta";
		BrokerCall call;
		call.op = "check-grant";
		call.payload.Add("subject_user_id", meta_uid);
		call.payload.Add("client_site", "beta");
		ValueArray scopes;
		scopes.Add("test.scope");
		call.payload.Add("scopes", scopes);
		BrokerResult res = TheWebEngine().Broker().Call(ctx, call);
		Check(res.ok && (bool)res.payload.Get("active"), "approved grant is active via check-grant");
	}
	VectorMap<String, String> revoke_f;
	revoke_f.Add("grant_id", grant_id);
	revoke_f.Add("approve", "0");
	HttpCall("POST", META_PORT, "/consent/decide", h_meta2, FormEncode(revoke_f));
	{
		BrokerCallContext ctx;
		ctx.from_site = "beta";
		ctx.to_site = "meta";
		BrokerCall call;
		call.op = "check-grant";
		call.payload.Add("subject_user_id", meta_uid);
		call.payload.Add("client_site", "beta");
		ValueArray scopes;
		scopes.Add("test.scope");
		call.payload.Add("scopes", scopes);
		BrokerResult res = TheWebEngine().Broker().Call(ctx, call);
		Check(res.ok && !(bool)res.payload.Get("active"), "revoked grant is inactive via check-grant");
	}

	// --- TaskFoundry linked-consent round trip (task 009): the client site's
	// user links to a meta identity; the meta subject decides on the meta site;
	// the grant serves only the linking user's session. ---
	{
		String tf_origin = Format("http://127.0.0.1:%d", TASKFOUNDRY_PORT);
		VectorMap<String, String> tf_lo;
		tf_lo.Add("Origin", tf_origin);
		auto TfHeaders = [&](VectorMap<String, String>& h, const String& cookie, const String& csrf) {
			h.Add("Origin", tf_origin);
			if(cookie.GetCount()) h.Add("Cookie", cookie);
			if(csrf.GetCount()) h.Add("X-WF-CSRF", csrf);
		};
		tf_lo.Add("accept", "application/json");
		VectorMap<String, String> tlf;
		tlf.Add("username", "tfdev");
		tlf.Add("password", "pw-tf");
		HttpReply rtl = HttpCall("POST", TASKFOUNDRY_PORT, "/session/login", tf_lo, FormEncode(tlf));
		String tf_csrf = (String)ParseJSON(rtl.body)["csrf"];
		String tf_raw;
		String tf_cookie = "wfs_taskfoundry=" + CookieOf(rtl, "wfs_taskfoundry", tf_raw);
		Check(rtl.status == 200 && tf_csrf.GetCount() > 0, "taskfoundry session login");

		// Without a grant the board redirects and a write is refused.
		VectorMap<String, String> tf_h;
		TfHeaders(tf_h, tf_cookie, tf_csrf);
		HttpReply rb0 = HttpCall("GET", TASKFOUNDRY_PORT, "/", tf_h, "");
		Check(rb0.status == 302, "taskfoundry board without a grant redirects");
		VectorMap<String, String> cc_f;
		cc_f.Add("title", "Linked");
		cc_f.Add("summary", "x");
		cc_f.Add("paper_id", "2612.00002");
		HttpReply rc0 = HttpCall("POST", TASKFOUNDRY_PORT, "/candidates/create", tf_h, FormEncode(cc_f));
		Check(rc0.status == 403, "taskfoundry write without a grant is refused");

		// The signed-in user links to a meta identity to request the grant.
		VectorMap<String, String> grf;
		grf.Add("meta_user", "mallory");
		grf.Add("meta_password", "pw-meta");
		grf.Add("meta_code", TotpCode(secret, TotpUnixNow()));
		HttpReply rgr = HttpCall("POST", TASKFOUNDRY_PORT, "/grants/request", tf_h, FormEncode(grf));
		Check(rgr.status == 302, "linked grant request accepted");
		HttpReply rgl = HttpCall("GET", TASKFOUNDRY_PORT, "/grants", tf_h, "");
		Check(rgl.status == 200 && rgl.body.Find("requested") >= 0, "pending linked grant is listed");
		HttpReply rin0 = HttpCall("GET", META_PORT, "/inbox", h_meta2, "");
		Check(rin0.status == 200 && rin0.body.Find("tf.grant_requested") >= 0,
		      "the inbox lists the TaskFoundry request for the subject");
		String wag;
		{
			int p = rgl.body.Find("wag_");
			Check(p >= 0, "grant id appears on the grants page");
			if(p >= 0) {
				int e = p;
				while(e < rgl.body.GetCount()) {
					char c = rgl.body[e];
					if(!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
					    || c == '_' || c == '-'))
						break;
					e++;
				}
				wag = rgl.body.Mid(p, e - p);
			}
		}

		// A meta user who is not the subject cannot decide.
		VectorMap<String, String> elf;
		elf.Add("username", "eva");
		elf.Add("password", "pw-eva");
		HttpReply rel = HttpCall("POST", META_PORT, "/login", origin_meta, FormEncode(elf));
		String eva_csrf = (String)ParseJSON(rel.body)["csrf"];
		String eva_raw;
		String eva_cookie = "wfs_meta=" + CookieOf(rel, "wfs_meta", eva_raw);
		VectorMap<String, String> eva_h;
		eva_h.Add("Cookie", eva_cookie);
		eva_h.Add("X-WF-CSRF", eva_csrf);
		VectorMap<String, String> notmine;
		notmine.Add("grant_id", wag);
		notmine.Add("approve", "1");
		HttpReply rnm = HttpCall("POST", META_PORT, "/consent/decide", eva_h, FormEncode(notmine));
		Check(rnm.status == 403, "a meta user who is not the subject cannot decide the grant");
		HttpReply rin1 = HttpCall("GET", META_PORT, "/inbox", eva_h, "");
		Check(rin1.status == 200 && rin1.body.Find("tf.grant_requested") < 0,
		      "another identity's inbox shows none of the subject's events");

		// The subject approves; TaskFoundry access opens.
		VectorMap<String, String> yes_f;
		yes_f.Add("grant_id", wag);
		yes_f.Add("approve", "1");
		HttpReply rap = HttpCall("POST", META_PORT, "/consent/decide", h_meta2, FormEncode(yes_f));
		Check(JsonFlag(ParseJSON(rap.body).Is<ValueMap>() ? (ValueMap)ParseJSON(rap.body) : ValueMap(), "ok"),
		      "the meta subject approves the linked grant");
		HttpReply rb1 = HttpCall("GET", TASKFOUNDRY_PORT, "/", tf_h, "");
		Check(rb1.status == 200, "taskfoundry board serves with the approved linked grant");
		HttpReply rc1 = HttpCall("POST", TASKFOUNDRY_PORT, "/candidates/create", tf_h, FormEncode(cc_f));
		Check(rc1.status == 302, "candidate create with the approved linked grant");

		// The subject revokes; access closes on the next request.
		VectorMap<String, String> no_f;
		no_f.Add("grant_id", wag);
		no_f.Add("approve", "0");
		HttpReply rrv = HttpCall("POST", META_PORT, "/consent/decide", h_meta2, FormEncode(no_f));
		Check(JsonFlag(ParseJSON(rrv.body).Is<ValueMap>() ? (ValueMap)ParseJSON(rrv.body) : ValueMap(), "ok"),
		      "the meta subject revokes the linked grant");
		HttpReply rc2 = HttpCall("POST", TASKFOUNDRY_PORT, "/candidates/create", tf_h, FormEncode(cc_f));
		Check(rc2.status == 403, "taskfoundry write is refused after the meta revoke");
		HttpReply rb2 = HttpCall("GET", TASKFOUNDRY_PORT, "/", tf_h, "");
		Check(rb2.status == 302, "taskfoundry board redirects again after the meta revoke");
		HttpReply rgs_rev = HttpCall("GET", TASKFOUNDRY_PORT, "/grants", tf_h, "");
		Check(GrantStateCell(rgs_rev.body, wag) == "revoked",
		      "the grants page shows the linked grant as revoked after the meta revoke");

		// The inbox carries the whole lifecycle for the subject (task 014).
		HttpReply rina = HttpCall("GET", META_PORT, "/inbox", h_meta2, "");
		Check(rina.body.Find("consent.granted") >= 0 && rina.body.Find("tf.candidate_created") >= 0
		      && rina.body.Find("consent.revoked") >= 0,
		      "the inbox carries the consent and taskfoundry lifecycle events");

		// Deny on a fresh pending request (the deny path was broken before
		// task 014), and the denied state gives no access. The TOTP code is
		// recomputed: the first request's code may have rotated by now.
		VectorMap<String, String> grf2;
		grf2.Add("meta_user", "mallory");
		grf2.Add("meta_password", "pw-meta");
		grf2.Add("meta_code", TotpCode(secret, TotpUnixNow()));
		HttpCall("POST", TASKFOUNDRY_PORT, "/grants/request", tf_h, FormEncode(grf2));
		HttpReply rgl2 = HttpCall("GET", TASKFOUNDRY_PORT, "/grants", tf_h, "");
		// The page lists all of this user's grants; take the id whose state
		// cell says "requested", not the revoked one.
		String wag2;
		{
			int p = 0;
			for(;;) {
				p = rgl2.body.Find("wag_", p);
				if(p < 0)
					break;
				int e = p + 4;
				while(e < rgl2.body.GetCount()) {
					char c = rgl2.body[e];
					if(!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
					    || c == '_' || c == '-'))
						break;
					e++;
				}
				String id = rgl2.body.Mid(p, e - p);
				int cell = rgl2.body.Find("</td><td>", e);
				int cellend = cell >= 0 ? rgl2.body.Find("</td>", cell + 9) : -1;
				if(cell >= 0 && cellend > cell
				   && rgl2.body.Mid(cell + 9, cellend - cell - 9) == "requested") {
					wag2 = id;
					break;
				}
				p = e;
			}
			Check(wag2.GetCount() > 0, "the re-request creates a pending grant");
		}
		VectorMap<String, String> deny_f;
		deny_f.Add("grant_id", wag2);
		deny_f.Add("approve", "0");
		HttpReply rdn = HttpCall("POST", META_PORT, "/consent/decide", h_meta2, FormEncode(deny_f));
		Check(JsonFlag(ParseJSON(rdn.body).Is<ValueMap>() ? (ValueMap)ParseJSON(rdn.body) : ValueMap(), "ok"),
		      "the subject denies the pending request");
		HttpReply rgs_den = HttpCall("GET", TASKFOUNDRY_PORT, "/grants", tf_h, "");
		Check(GrantStateCell(rgs_den.body, wag2) == "denied",
		      "the grants page shows the denied request as denied (state changed, not just ok)");
		HttpReply rb3 = HttpCall("GET", TASKFOUNDRY_PORT, "/", tf_h, "");
		Check(rb3.status == 302, "a denied grant gives no TaskFoundry access");
		HttpReply rinb = HttpCall("GET", META_PORT, "/inbox", h_meta2, "");
		Check(rinb.body.Find("consent.denied") >= 0, "the deny lands in the inbox");

		// Unread state and mark-all-read.
		HttpReply rin3 = HttpCall("GET", META_PORT, "/inbox", h_meta2, "");
		Check(rin3.body.Find("class=\"unread\"") >= 0, "the inbox marks unread events");
		VectorMap<String, String> no_fields;
		HttpReply rra = HttpCall("POST", META_PORT, "/inbox/read-all", h_meta2, FormEncode(no_fields));
		Check(JsonFlag(ParseJSON(rra.body).Is<ValueMap>() ? (ValueMap)ParseJSON(rra.body) : ValueMap(), "ok"),
		      "mark all read succeeds");
		HttpReply rin4 = HttpCall("GET", META_PORT, "/inbox", h_meta2, "");
		Check(rin4.body.Find("class=\"unread\"") < 0, "read-all clears the unread state");

		// The notify op validates its type, and the store stays sane under a
		// bulk write.
		{
			BrokerCallContext ctx;
			ctx.from_site = "taskfoundry";
			ctx.to_site = "meta";
			BrokerCall bad;
			bad.op = "notify";
			bad.payload.Add("subject_user_id", meta_uid);
			bad.payload.Add("source_site", "taskfoundry");
			bad.payload.Add("type", "not.a.type");
			bad.payload.Add("actor", "taskfoundry:bulk");
			bad.payload.Add("message", "x");
			BrokerResult res = TheWebEngine().Broker().Call(ctx, bad);
			Check(!res.ok, "the notify op refuses a non-allowlisted type");
			bool bulk_ok = true;
			for(int i = 0; i < 210; i++) {
				BrokerCall bulk;
				bulk.op = "notify";
				bulk.payload.Add("subject_user_id", meta_uid);
				bulk.payload.Add("source_site", "taskfoundry");
				bulk.payload.Add("type", "tf.candidate_created");
				bulk.payload.Add("actor", "taskfoundry:bulk");
				bulk.payload.Add("message", String().Cat() << "bulk-" << i);
				BrokerResult bres = TheWebEngine().Broker().Call(ctx, bulk);
				if(!bres.ok)
					bulk_ok = false;
			}
			Check(bulk_ok, "the bulk notify run succeeds");
			HttpReply rin5 = HttpCall("GET", META_PORT, "/inbox", h_meta2, "");
			Check(rin5.status == 200 && rin5.body.Find("bulk-209") >= 0,
			      "the inbox still serves after a bulk write");
		}

		// The per-site command console (task 019): the TaskFoundry site has
		// wildcard-free POST commands, so its console renders runnable forms.
		HttpReply rcc = HttpCall("GET", TASKFOUNDRY_PORT, "/console", tf_h, "");
		Check(rcc.status == 200 && rcc.body.Find("grants/request") >= 0
		      && rcc.body.Find("candidates/create") >= 0,
		      "the console renders the site's commands for a signed-in user");
		Check(rcc.body.Find("__wf_csrf__") >= 0,
		      "a runnable command form carries the CSRF hidden field");
		Check(rcc.body.Find("use skylarkctl") >= 0,
		      "wildcard-path commands stay documentation rows");
	}

	// --- Channels read-only frontend over an argv shim (task 012) ---
	{
		String ch_origin = Format("http://127.0.0.1:%d", CHANNELS_PORT);
		VectorMap<String, String> ch_lo;
		ch_lo.Add("Origin", ch_origin);
		ch_lo.Add("accept", "application/json");
		setenv("AGENTIPC_SHIM_ARGV", AppendFileName(s_root, "channels-argv.txt"), 1);
		setenv("AGENTIPC_SHIM_COUNT", AppendFileName(s_root, "channels-count.txt"), 1);
		setenv("AGENTIPC_SHIM_CANNED", AppendFileName(s_root, "channels-canned.txt"), 1);
		DeleteFile(AppendFileName(s_root, "channels-argv.txt"));
		DeleteFile(AppendFileName(s_root, "channels-count.txt"));

		VectorMap<String, String> clf;
		clf.Add("username", "viewer");
		clf.Add("password", "pw-view");
		HttpReply rcl = HttpCall("POST", CHANNELS_PORT, "/session/login", ch_lo, FormEncode(clf));
		String ch_csrf = (String)ParseJSON(rcl.body)["csrf"];
		String ch_raw;
		String ch_cookie = "wfs_channels=" + CookieOf(rcl, "wfs_channels", ch_raw);
		Check(rcl.status == 200 && ch_csrf.GetCount() > 0, "channels session login");

		// Anonymous callers are redirected.
		VectorMap<String, String> anon_h;
		anon_h.Add("Origin", ch_origin);
		HttpReply rca = HttpCall("GET", CHANNELS_PORT, "/overview", anon_h, "");
		Check(rca.status == 302, "channels overview anonymous redirects");

		VectorMap<String, String> ch_h;
		ch_h.Add("Origin", ch_origin);
		ch_h.Add("Cookie", ch_cookie);
		HttpReply rco = HttpCall("GET", CHANNELS_PORT, "/overview", ch_h, "");
		Check(rco.status == 200 && rco.body.Find("SKYLARK-MANAGER") >= 0
		      && rco.body.Find("AGENTLAB") >= 0, "channels overview lists the node's channels");
		HttpReply rcp = HttpCall("GET", CHANNELS_PORT, "/channels/skylark/SKYLARK-MANAGER", ch_h, "");
		Check(rcp.status == 200 && rcp.body.Find("ACK hello") >= 0, "channels page renders the shimmed messages");
		Check(rcp.body.Find("<script>alert(1)") < 0 && rcp.body.Find("&lt;script&gt;") >= 0,
		      "channel bodies render escaped");

		// The argv the shim saw: the password value is never in argv.
		String argv_dump = LoadFile(AppendFileName(s_root, "channels-argv.txt"));
		Check(argv_dump.Find("--password-env") >= 0 && argv_dump.Find("AGENTIPC_CHANNELS_PW") >= 0
		      && argv_dump.Find("pw-secret") < 0, "the client argv carries the env name, never the password");
		Check(argv_dump.Find("sh") < 0 || argv_dump.Find(";") < 0, "the client argv carries no shell metacharacters");

		// The overview reads the record metadata without a client call; the page
		// view made one client call, the next one is served from the cache, and
		// ?fresh=1 forces a new call. The shim appends one line per call.
		auto RunsOf = [](const String& path) {
			String f = LoadFile(path);
			int n = 0;
			for(int i = 0; i < f.GetCount(); i++)
				if(f[i] == '\n')
					n++;
			return n;
		};
		int runs = RunsOf(AppendFileName(s_root, "channels-count.txt"));
		Check(runs == 1, "the page view made one client call");
		HttpCall("GET", CHANNELS_PORT, "/channels/skylark/SKYLARK-MANAGER", ch_h, "");
		runs = RunsOf(AppendFileName(s_root, "channels-count.txt"));
		Check(runs == 1, "a repeated page view is served from the cache");
		HttpCall("GET", CHANNELS_PORT, "/channels/skylark/SKYLARK-MANAGER?fresh=1", ch_h, "");
		runs = RunsOf(AppendFileName(s_root, "channels-count.txt"));
		Check(runs == 2, "?fresh=1 forces a new client call");

		// A failing client turns into a 503 page.
		setenv("AGENTIPC_SHIM_FAIL", "1", 1);
		HttpReply rcf = HttpCall("GET", CHANNELS_PORT, "/channels/skylark/AGENTLAB?fresh=1", ch_h, "");
		setenv("AGENTIPC_SHIM_FAIL", "", 1);
		Check(rcf.status == 503, "a failing client answers with the unavailable page");
	}

	// --- Financials read-only portal over the canned company stand-in (task 016) ---
	{
		String fin_origin = Format("http://127.0.0.1:%d", FINANCIALS_PORT);
		auto FinLogin = [&](const char* user, const char* pw, String& cookie, String& csrf) {
			VectorMap<String, String> o;
			o.Add("Origin", fin_origin);
			o.Add("accept", "application/json");
			VectorMap<String, String> f;
			f.Add("username", user);
			f.Add("password", pw);
			HttpReply rl = HttpCall("POST", FINANCIALS_PORT, "/session/login", o, FormEncode(f));
			csrf = (String)ParseJSON(rl.body)["csrf"];
			String raw;
			cookie = "wfs_financials=" + CookieOf(rl, "wfs_financials", raw);
			return rl.status == 200 && csrf.GetCount() > 0;
		};
		String view_cookie, view_csrf, fin_cookie, fin_csrf;
		Check(FinLogin("fviewer", "pw-fin", view_cookie, view_csrf), "financials viewer login");
		Check(FinLogin("ffin", "pw-fin2", fin_cookie, fin_csrf), "financials finance login");

		VectorMap<String, String> anon_h;
		anon_h.Add("Origin", fin_origin);
		HttpReply rfa = HttpCall("GET", FINANCIALS_PORT, "/financials", anon_h, "");
		Check(rfa.status == 302, "financials summary anonymous redirects");

		VectorMap<String, String> view_h;
		view_h.Add("Origin", fin_origin);
		view_h.Add("Cookie", view_cookie);
		VectorMap<String, String> fin_h;
		fin_h.Add("Origin", fin_origin);
		fin_h.Add("Cookie", fin_cookie);

		HttpReply rvs = HttpCall("GET", FINANCIALS_PORT, "/financials", view_h, "");
		Check(rvs.status == 200 && rvs.body.Find("500") >= 0, "the viewer sees the ledger summary");
		HttpReply rvl = HttpCall("GET", FINANCIALS_PORT, "/ledger", view_h, "");
		Check(rvl.status == 403, "the viewer is refused the ledger rows");
		HttpReply rvc = HttpCall("GET", FINANCIALS_PORT, "/contracts", view_h, "");
		Check(rvc.status == 403, "the viewer is refused the contracts");

		HttpReply rfl = HttpCall("GET", FINANCIALS_PORT, "/ledger", fin_h, "");
		Check(rfl.status == 200 && rfl.body.Find("fixed &lt;bug&gt; &amp; shipped") >= 0
		      && rfl.body.Find("<bug>") < 0, "the finance role reads the ledger with escaped text");
		HttpReply rfc = HttpCall("GET", FINANCIALS_PORT, "/contracts", fin_h, "");
		Check(rfc.status == 200 && rfc.body.Find("Fake &lt;contract&gt;") >= 0,
		      "the finance role reads the contracts with escaped text");
		HttpReply rfs = HttpCall("GET", FINANCIALS_PORT, "/financials", fin_h, "");
		Check(rfs.status == 200 && rfs.body.Find("500") >= 0, "the finance role sees the summary too");

		// The module-side scope gate: a ledger:read caller without admin is
		// refused the per-user rows even at the broker.
		{
			BrokerCallContext ctx;
			ctx.from_site = "financials";
			ctx.to_site = "company";
			ctx.scopes.Add("ledger:read");
			BrokerCall call;
			call.op = "ledger-recent";
			call.payload.Add("subject_user_id", "wau_fakeuser");
			BrokerResult res = TheWebEngine().Broker().Call(ctx, call);
			Check(!res.ok, "a subject filter without the admin scope is refused at the module");
		}

		// A failing module answers with the unavailable page.
		setenv("COMPANY_SHIM_FAIL", "1", 1);
		HttpReply rff = HttpCall("GET", FINANCIALS_PORT, "/financials?fresh=1", fin_h, "");
		setenv("COMPANY_SHIM_FAIL", "", 1);
		Check(rff.status == 503, "a failing module answers with the unavailable page");
	}

	// --- LAN authority (task 032): the fence and the absolute URLs follow the Host the client used ---
	{
		String lan_host = Format("192.168.1.169:%d", ATLAS_PORT);
		String lan_origin = "http://" + lan_host;
		VectorMap<String, String> lo;
		lo.Add("Origin", lan_origin);
		lo.Add("Host", lan_host);
		lo.Add("accept", "application/json");
		VectorMap<String, String> lf;
		lf.Add("username", "aadmin");
		lf.Add("password", "pw-atl");
		HttpReply rlan = HttpCall("POST", ATLAS_PORT, "/session/login", lo, FormEncode(lf));
		String lan_csrf = (String)ParseJSON(rlan.body)["csrf"];
		String lan_raw;
		String lan_cookie = "wfs_atlas=" + CookieOf(rlan, "wfs_atlas", lan_raw);
		Check(rlan.status == 200 && lan_csrf.GetCount() > 0, "a LAN-authority login POST succeeds with the Host it was sent to");

		VectorMap<String, String> lh;
		lh.Add("Origin", lan_origin);
		lh.Add("Host", lan_host);
		lh.Add("Cookie", lan_cookie);
		VectorMap<String, String> lc;
		lc.Add("map", "lan");
		lc.Add("version", "v1");
		lc.Add("title", "LAN");
		lc.Add("__wf_csrf__", lan_csrf);
		HttpReply rlc = HttpCall("POST", ATLAS_PORT, "/atlas/create", lh, FormEncode(lc));
		Check(rlc.status == 302, "a LAN-authority mutation succeeds");
		Check(rlc.location == Format("http://%s/atlas/lan-v1", lan_host),
		      "the redirect Location uses the request's Host authority");
		HttpReply rlm = HttpCall("GET", ATLAS_PORT, "/atlas/lan-v1", lh, "");
		Check(rlm.status == 200 && rlm.body.Find(Format("href=\"http://%s/atlas\"", lan_host)) >= 0,
		      "absolute links on a LAN page use the Host authority");

		VectorMap<String, String> foreign;
		foreign.Add("Origin", "http://evil.example:" + AsString(ATLAS_PORT));
		foreign.Add("Host", lan_host);
		foreign.Add("accept", "application/json");
		Check(HttpCall("POST", ATLAS_PORT, "/session/login", foreign, FormEncode(lf)).status == 403,
		      "a foreign Origin is refused on a LAN Host (the fence is still exact)");

		VectorMap<String, String> mismatch;
		mismatch.Add("Origin", Format("http://127.0.0.1:%d", ATLAS_PORT));
		mismatch.Add("Host", lan_host);
		mismatch.Add("accept", "application/json");
		Check(HttpCall("POST", ATLAS_PORT, "/session/login", mismatch, FormEncode(lf)).status == 403,
		      "an Origin that is not the request's own authority is refused");

		// DNS names are never trusted as the served authority: a rebinding page
		// would send Origin and Host with the same attacker name.
		VectorMap<String, String> rebind;
		rebind.Add("Origin", Format("http://attacker.example:%d", ATLAS_PORT));
		rebind.Add("Host", Format("attacker.example:%d", ATLAS_PORT));
		rebind.Add("accept", "application/json");
		Check(HttpCall("POST", ATLAS_PORT, "/session/login", rebind, FormEncode(lf)).status == 403,
		      "a DNS-name Host (rebinding) is refused, Origin equal to Host or not");
	}

	// --- Atlas map webapp (task 029): admin edits, viewer reads, every write revision-checked ---
	{
		String atl_origin = Format("http://127.0.0.1:%d", ATLAS_PORT);
		auto AtlLogin = [&](const char* user, const char* pw, String& cookie, String& csrf) {
			VectorMap<String, String> o;
			o.Add("Origin", atl_origin);
			o.Add("accept", "application/json");
			VectorMap<String, String> f;
			f.Add("username", user);
			f.Add("password", pw);
			HttpReply rl = HttpCall("POST", ATLAS_PORT, "/session/login", o, FormEncode(f));
			csrf = (String)ParseJSON(rl.body)["csrf"];
			String raw;
			cookie = "wfs_atlas=" + CookieOf(rl, "wfs_atlas", raw);
			return rl.status == 200 && csrf.GetCount() > 0;
		};
		String adm_cookie, adm_csrf, vw_cookie, vw_csrf;
		Check(AtlLogin("aadmin", "pw-atl", adm_cookie, adm_csrf), "atlas admin login");
		Check(AtlLogin("aview", "pw-atl2", vw_cookie, vw_csrf), "atlas viewer login");

		VectorMap<String, String> anon_h;
		anon_h.Add("Origin", atl_origin);
		Check(HttpCall("GET", ATLAS_PORT, "/atlas", anon_h, "").status == 302, "atlas list anonymous redirects");

		VectorMap<String, String> adm_h;
		adm_h.Add("Origin", atl_origin);
		adm_h.Add("Cookie", adm_cookie);
		VectorMap<String, String> vw_h;
		vw_h.Add("Origin", atl_origin);
		vw_h.Add("Cookie", vw_cookie);

		// Creating a map version: fenced, admin-only, one document per version.
		VectorMap<String, String> cf;
		cf.Add("map", "pkr");
		cf.Add("version", "v0.0.1");
		cf.Add("title", "PKR <v0.0.1>");
		cf.Add("__wf_csrf__", adm_csrf);
		HttpReply rcreate = HttpCall("POST", ATLAS_PORT, "/atlas/create", adm_h, FormEncode(cf));
		Check(rcreate.status == 302, "the admin creates a map version");
		HttpReply rdup = HttpCall("POST", ATLAS_PORT, "/atlas/create", adm_h, FormEncode(cf));
		Check(rdup.status == 409, "a second create of the same version is refused");
		VectorMap<String, String> nocsrf;
		nocsrf.Add("map", "pkr2");
		nocsrf.Add("version", "v1");
		nocsrf.Add("title", "No token");
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/create", adm_h, FormEncode(nocsrf)).status == 403,
		      "a mutation without the CSRF token is refused by the fence");
		VectorMap<String, String> cross_f;
		cross_f.Add("map", "pkr");
		cross_f.Add("version", "v0.0.1");
		cross_f.Add("title", "x");
		cross_f.Add("__wf_csrf__", "not-the-token");
		VectorMap<String, String> evil_h;
		evil_h.Add("Origin", "http://evil.example");
		evil_h.Add("Cookie", adm_cookie);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/create", evil_h, FormEncode(cross_f)).status == 403,
		      "a mutation from a foreign origin is refused by the fence");
		VectorMap<String, String> vcreate;
		vcreate.Add("map", "pkr3");
		vcreate.Add("version", "v1");
		vcreate.Add("title", "Viewer");
		vcreate.Add("__wf_csrf__", vw_csrf);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/create", vw_h, FormEncode(vcreate)).status == 403,
		      "a viewer cannot create a map");

		HttpReply rmap = HttpCall("GET", ATLAS_PORT, "/atlas/pkr-v0.0.1", adm_h, "");
		String rev1 = AtlasRevisionOf(rmap.body);
		Check(rmap.status == 200 && rmap.body.Find("atlas-canvas") >= 0 && rmap.body.Find("Edit (admin)") >= 0
		      && rmap.body.Find("data-editable=\"1\"") >= 0, "the admin sees the canvas and the edit panel");
		Check(rev1.GetCount() > 0, "the map page embeds its revision");
		Check(rmap.body.Find("PKR &lt;v0.0.1&gt;") >= 0 && rmap.body.Find("PKR <v0.0.1>") < 0,
		      "the map title is escaped");

		// Seed import: a hand-transcribed containment list plus one edge.
		VectorMap<String, String> sf;
		sf.Add("seed", "Main | block | active\n  Child | place | hot\n  Other\nChild -> Other: feeds\n");
		sf.Add("expected_revision", rev1);
		sf.Add("__wf_csrf__", adm_csrf);
		HttpReply rseed = HttpCall("POST", ATLAS_PORT, "/atlas/pkr-v0.0.1/seed", adm_h, FormEncode(sf));
		Check(rseed.status == 302, "the admin seeds the empty map");
		HttpReply rmap2 = HttpCall("GET", ATLAS_PORT, "/atlas/pkr-v0.0.1", adm_h, "");
		String rev2 = AtlasRevisionOf(rmap2.body);
		Check(rmap2.body.Find("3 elements") >= 0 && rmap2.body.Find("1 roads") >= 0,
		      "the seed creates its elements and road");
		Check(rev2 != rev1, "a write advances the revision");
		HttpReply rseed_stale = HttpCall("POST", ATLAS_PORT, "/atlas/pkr-v0.0.1/seed", adm_h, FormEncode(sf));
		Check(rseed_stale.status == 409, "a stale revision is refused as a conflict");
		VectorMap<String, String> sf2;
		sf2.Add("seed", "Again\n");
		sf2.Add("expected_revision", rev2);
		sf2.Add("__wf_csrf__", adm_csrf);
		HttpReply rseed_again = HttpCall("POST", ATLAS_PORT, "/atlas/pkr-v0.0.1/seed", adm_h, FormEncode(sf2));
		Check(rseed_again.status == 400, "a seed never fills a map that already has elements");

		// Adding an element with markup in its name: escaped on the page and in the embedded JSON.
		VectorMap<String, String> af;
		af.Add("name", "<img src=x onerror=alert(1)>");
		af.Add("kind", "place");
		af.Add("status", "trouble");
		af.Add("parent", "");
		af.Add("x", "900");
		af.Add("y", "40");
		af.Add("w", "180");
		af.Add("h", "100");
		af.Add("expected_revision", rev2);
		af.Add("__wf_csrf__", adm_csrf);
		HttpReply radd = HttpCall("POST", ATLAS_PORT, "/atlas/pkr-v0.0.1/element/add", adm_h, FormEncode(af));
		Check(radd.status == 302, "the admin adds an element");
		HttpReply rmap3 = HttpCall("GET", ATLAS_PORT, "/atlas/pkr-v0.0.1", adm_h, "");
		String rev3 = AtlasRevisionOf(rmap3.body);
		Check(rmap3.body.Find("&lt;img src=x") >= 0 && rmap3.body.Find("<img src=x") < 0,
		      "an element name with markup is escaped in the page");
		Check(rmap3.body.Find("<img") < 0, "an element name with markup never reaches the raw page");

		// Drag-to-move: JSON answers, revision-checked, validation before the save.
		VectorMap<String, String> mf;
		mf.Add("id", "e_child");
		mf.Add("x", "300");
		mf.Add("y", "60");
		mf.Add("expected_revision", rev3);
		mf.Add("__wf_csrf__", adm_csrf);
		HttpReply rmove = HttpCall("POST", ATLAS_PORT, "/atlas/pkr-v0.0.1/element/move", adm_h, FormEncode(mf));
		ValueMap mj = ParseJSON(rmove.body);
		Check(rmove.status == 200 && (bool)mj["ok"], "a move saves and answers ok");
		String rev4 = AtlasRevisionOf(HttpCall("GET", ATLAS_PORT, "/atlas/pkr-v0.0.1", adm_h, "").body);
		Check(rev4 != rev3 && (String)mj["revision"] == rev4, "a move advances the revision it returns");
		HttpReply rmove_stale = HttpCall("POST", ATLAS_PORT, "/atlas/pkr-v0.0.1/element/move", adm_h, FormEncode(mf));
		Check(rmove_stale.status == 409, "a move from a stale page is refused");
		VectorMap<String, String> mv;
		mv.Add("id", "e_child");
		mv.Add("x", "-5");
		mv.Add("y", "60");
		mv.Add("expected_revision", rev4);
		mv.Add("__wf_csrf__", adm_csrf);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/pkr-v0.0.1/element/move", adm_h, FormEncode(mv)).status == 400,
		      "a move to a negative position is refused by validation");
		VectorMap<String, String> vmove;
		vmove.Add("id", "e_child");
		vmove.Add("x", "300");
		vmove.Add("y", "60");
		vmove.Add("expected_revision", rev4);
		vmove.Add("__wf_csrf__", vw_csrf);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/pkr-v0.0.1/element/move", vw_h, FormEncode(vmove)).status == 403,
		      "a viewer cannot move an element");

		// Roads: one plane only; a road from a child to its own block is refused.
		VectorMap<String, String> rf;
		rf.Add("from", "e_child");
		rf.Add("to", "e_main");
		rf.Add("kind", "dependency");
		rf.Add("label", "cross");
		rf.Add("expected_revision", rev4);
		rf.Add("__wf_csrf__", adm_csrf);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/pkr-v0.0.1/road/add", adm_h, FormEncode(rf)).status == 400,
		      "a road across planes is refused");

		// Deleting a block that still has children is refused.
		VectorMap<String, String> df;
		df.Add("id", "e_main");
		df.Add("expected_revision", rev4);
		df.Add("__wf_csrf__", adm_csrf);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/pkr-v0.0.1/element/delete", adm_h, FormEncode(df)).status == 400,
		      "a block with children cannot be deleted");

		// Reads: the viewer sees the map read-only; the element detail shows its path.
		HttpReply rvm = HttpCall("GET", ATLAS_PORT, "/atlas/pkr-v0.0.1", vw_h, "");
		Check(rvm.status == 200 && rvm.body.Find("data-editable=\"0\"") >= 0 && rvm.body.Find("Edit (admin)") < 0,
		      "the viewer reads the map with no edit panel");
		HttpReply rve = HttpCall("GET", ATLAS_PORT, "/atlas/pkr-v0.0.1/element/e_child", vw_h, "");
		Check(rve.status == 200 && rve.body.Find("Containment path") >= 0 && rve.body.Find("Main") >= 0,
		      "the element detail shows its containment path");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas/pkr-v0.0.1/element/e_nope", vw_h, "").status == 404,
		      "an unknown element is not found");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas/pkr-v0.0.1", vw_h, "").status == 200
		      && HttpCall("GET", ATLAS_PORT, "/atlas/no-such-map", vw_h, "").status == 404,
		      "an unknown map is not found");
	}

	// Command manifest: the registry shape, and the session routes checked against their descriptors.
	{
		CommandManifest local;
		local.Add("alpha", "Alpha", HttpCommand("probe", "POST", "probe/run", "open session", "json", "Probe",
		                                        {"name:string:required", "count:int"}));
		local.Add("beta", "Beta", BrokerCommand("ping", "ping.op", "broker caller", "Ping", {}));
		Check(local.HasSite("alpha") && local.HasSite("beta") && !local.HasSite("gamma"),
		      "manifest lists only sites that registered commands");
		ValueMap all = local.Describe();
		Check(all.Get("schema") == "skylark-command-manifest/1", "manifest carries its schema id");
		ValueArray all_sites = all["sites"];
		Check(all_sites.GetCount() == 2, "manifest describes every registered site");
		ValueMap one = local.Describe("beta");
		ValueArray beta_sites = one["sites"];
		Check(beta_sites.GetCount() == 1 && (String)ValueMap(beta_sites[0])["id"] == "beta",
		      "single-site descriptor lists only that site");
		ValueArray beta_cmds = ValueMap(beta_sites[0])["commands"];
		Check(beta_cmds.GetCount() == 1 && (String)ValueMap(beta_cmds[0])["broker_op"] == "ping.op" &&
		      (String)ValueMap(beta_cmds[0])["transport"] == "broker",
		      "broker command carries its operation name and transport");
		Check(!local.Add("beta", "Beta", BrokerCommand("ping", "ping.again", "broker caller", "Again", {})) &&
		      local.Add("alpha", "Alpha", BrokerCommand("ping", "ping.op", "broker caller", "Ping", {})) &&
		      local.GetCount() == 3,
		      "a command name repeats within one site is refused; the same name on another site is listed");
		ValueMap alpha_cmd = ValueArray(ValueMap(all_sites[0])["commands"])[0];
		ValueArray alpha_args = alpha_cmd["args"];
		Check(alpha_args.GetCount() == 2 && (String)ValueMap(alpha_args[0])["name"] == "name" &&
		      (bool)ValueMap(alpha_args[0])["required"] && (String)ValueMap(alpha_args[1])["type"] == "int" &&
		      !(bool)ValueMap(alpha_args[1])["required"],
		      "argument specs parse name, type and required");
	}
	{
		EngineSite probe;
		probe.id = "probe";
		probe.title = "Probe";
		InstallSessionRoutes(probe);
		ValueMap described = TheWebEngine().Manifest().Describe("probe");
		ValueArray sites = described["sites"];
		ValueArray cmds;
		if(sites.GetCount())
			cmds = ValueMap(sites[0])["commands"];
		bool covered = true;
		int posts = 0;
		for(int i = 0; i < probe.Router().GetCount(); i++) {
			const SiteRoute& rt = probe.Router()[i];
			if(rt.method != "POST")
				continue;
			posts++;
			bool found = false;
			for(int k = 0; k < cmds.GetCount(); k++) {
				ValueMap c = cmds[k];
				if((String)c["method"] == rt.method && (String)c["api_path"] == rt.pattern)
					found = true;
			}
			covered = covered && found;
		}
		Check(posts == 2 && covered && cmds.GetCount() == 3,
		      "session POST routes each have a manifest command with the same path and method");
	}

	host.StopEngines();
	host.Quit();
	host_thread.Wait();

	Cout() << "\nWebEngineTests: " << (checks - failures.GetCount()) << "/" << checks
	       << (failures.GetCount() ? " CHECKS FAILED" : " checks passed") << "\n";
	if(failures.GetCount())
		for(const String& f : failures)
			Cout() << "  FAILED: " << f << "\n";
	_Exit(failures.GetCount() ? 1 : 0);
}
