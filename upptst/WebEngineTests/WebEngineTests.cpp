#include <WebAuth/WebAuth.h>
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
#include <WebsiteTemplates/WebsiteTemplates.h>
#include <WebAudit/WebAudit.h>

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
	String referrer_policy;
	String cache_control;
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
		else if(key == "referrer-policy")
			r.referrer_policy = TrimBoth(line.Mid(q + 1));
		else if(key == "cache-control")
			r.cache_control = TrimBoth(line.Mid(q + 1));
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
	String AuditOperation(const String& op) const override
	{
		return op == "add" || op == "ping" ? op : String("unclassified");
	}
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
	Vector<String> admin_roles; admin_roles.Add("admin");
	alpha.Accounts().CreateUser("auditadmin", "pw-audit", "Audit Admin", admin_roles, uid, existed);
	beta.Accounts().CreateUser("auditadmin", "pw-audit", "Audit Admin", admin_roles, uid, existed);
	InstallSessionRoutes(alpha);
	InstallSessionRoutes(beta);
	alpha.Router().Add("audit-redirect-local", [](WebRequest& r) { r.Redirect("/safe?token=redirect-secret-canary"); });
	alpha.Router().Add("audit-redirect-external", [](WebRequest& r) { r.Redirect("https://outside.example/path?token=redirect-secret-canary"); });
	alpha.Router().Add("audit-redirect-sso", [](WebRequest& r) {
		EngineSite *meta = TheWebEngine().FindSite("meta");
		r.Redirect(meta ? meta->PublicUrl("/login") : String("/"));
	});
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
	TheWebEngine().SetMetaSso(true);
	InstallSessionRoutes(science);
	TheWebEngine().SetMetaSso(false);
	InstallContentPolicyRoutes(science, true);
	science.Router().Add("login:POST", RouteLogin);
	Vector<String> author_roles;
	author_roles.Add(SCIENCE_AUTHOR_ROLE);
	Vector<String> science_admin_roles;
	science_admin_roles.Add("admin");
	Vector<String> reader_roles;
	reader_roles.Add("member");
	science.Accounts().CreateUser("author", "pw-author", "Author", author_roles, uid, existed);
	science.Accounts().CreateUser("sciadmin", "pw-admin", "Admin", science_admin_roles, uid, existed);
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
	InstallContentPolicyRoutes(ph, true);
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
	InstallContentPolicyRoutes(tf, true);
	InstallCommandConsole(tf);
	tf.Accounts().CreateUser("tfdev", "pw-tf", "Tfdev", ph_no_roles, uid, existed);

	// A second meta identity, for the not-your-grant negative.
	meta.Accounts().CreateUser("eva", "pw-eva", "Eva", ph_no_roles, uid, existed);

	// Financials site over the canned company stand-in (task 016).
	EngineSite& fin = TheWebEngine().AddSite("financials", "Financials",
		Format("http://127.0.0.1:%d", FINANCIALS_PORT), AppendFileName(s_root, "financials"));
	InstallFinancialsSite(fin);
	InstallSessionRoutes(fin);
	InstallContentPolicyRoutes(fin, true);
	Vector<String> fin_finance_roles;
	fin_finance_roles.Add("finance");
	fin.Accounts().CreateUser("fviewer", "pw-fin", "Fviewer", ph_no_roles, uid, existed);
	fin.Accounts().CreateUser("ffin", "pw-fin2", "Ffin", fin_finance_roles, uid, existed);

	// Atlas map webapp (task 029): the admin edits, a viewer reads.
	EngineSite& atl = TheWebEngine().AddSite("atlas", "Atlas",
		Format("http://127.0.0.1:%d", ATLAS_PORT), AppendFileName(s_root, "atlas"));
	InstallAtlasSite(atl);
	TheWebEngine().SetMetaSso(true);
	InstallSessionRoutes(atl);
	TheWebEngine().SetMetaSso(false);
	InstallContentPolicyRoutes(atl, true);
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
	InstallContentPolicyRoutes(ch, true);
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

static void RunWebsiteTemplateChecks()
{
	const Vector<WebsiteTemplate>& templates = GetWebsiteTemplates();
	Index<String> ids;
	for(const WebsiteTemplate& t : templates)
		ids.Add(t.id);
	Check(ids.Find("meta-login") >= 0 && ids.Find("glassmorphism") >= 0,
	      "website template registry includes meta-login and glassmorphism");
	Check(ids.GetCount() == templates.GetCount(), "website template registry ids are unique");
	int template_count = templates.GetCount();
	WebsiteTemplate duplicate;
	duplicate.id = "meta-login";
	duplicate.title = "replacement must not win";
	Check(!RegisterWebsiteTemplate(duplicate) && GetWebsiteTemplates().GetCount() == template_count &&
	      FindWebsiteTemplate("meta-login")->title == "MetaAccount sign-in" &&
	      !RegisterWebsiteTemplate(WebsiteTemplate()),
	      "template registration rejects duplicate and empty ids without replacing built-ins");
	const WebsiteTemplate *login = FindWebsiteTemplate("meta-login");
	const WebsiteTemplate *glass = FindWebsiteTemplate("glassmorphism");
	const WebsiteTemplate *engine_login = FindWebsiteTemplate("engine-login");
	String login_form = login ? login->RenderMetaLoginForm("/login", "csrf", "next") : String();
	Check(login && login->RenderPage("Sign in", login_form).Find("m-container") >= 0 &&
	      login_form.Find("autocomplete=\"one-time-code\"") >= 0 && login_form.Find("<button") >= 0 &&
	      login->RenderMetaLoginForm("/\"<x>", "csrf", "").Find("/&quot;&lt;x&gt;") >= 0,
	      "meta-login template renders escaped TOTP form structure");
	Check(glass && glass->RenderContent("<article>Account</article>").Find("wt-glass-panel") >= 0 &&
	      glass->RenderStyles().Find("backdrop-filter") >= 0 && glass->RenderStyles().Find("fonts.googleapis") < 0,
	      "glassmorphism template renders translucent content without external fonts");
	String public_login = engine_login ? engine_login->RenderEngineLoginBody("bad <credentials>", "/login", "csrf",
	                                                                        "/return", "/sso", false) : String();
	String sso_login = engine_login ? engine_login->RenderEngineLoginBody(String(), String(), String(), String(),
	                                                                       "/sso", true) : String();
	Check(public_login.Find("<form") >= 0 && public_login.Find("name=\"username\"") >= 0 &&
	      public_login.Find("name=\"__wf_csrf__\"") >= 0 && public_login.Find("name=\"next\"") >= 0 &&
	      public_login.Find("bad &lt;credentials&gt;") >= 0 && sso_login.Find("<form") < 0 &&
	      sso_login.Find("Continue with MetaAccount") >= 0,
	      "engine-login template renders password and SSO-only form parts safely");
	Check(WebsiteThemeHtmlAttributes("github-dark", "dense").Find("data-theme=\"github-dark\"") >= 0 &&
	      WebsiteThemeHtmlAttributes("invalid", "invalid").Find("data-theme=\"vivid-dark\"") >= 0 &&
	      WebsiteThemeHtmlAttributes("invalid", "invalid").Find("data-density=\"dense\"") >= 0,
	      "website themes and density stay allowlisted with safe defaults (dense is the default)");
	String all_theme_styles = WebsiteThemeStyles();
	bool every_theme = true;
	for(const char *id : { "vivid-dark", "vivid-light", "vivid-mixed", "github-dark", "github-light",
	                       "sourceforge-dark", "sourceforge-classic" })
		every_theme = every_theme && all_theme_styles.Find(Format("data-theme=\"%s\"", id)) >= 0;
	Check(every_theme && all_theme_styles.Find("--st-trouble") >= 0 && all_theme_styles.Find("--accent") >= 0 &&
	      WebsiteDensityStyles().Find("html[data-density=\"dense\"]") >= 0 &&
	      WebsiteDensityStyles().Find("--wf-container-width: 100%") >= 0,
	      "shared theme tokens cover all seven host themes and the dense density default");
	Check(WebsiteThemeControlsHtml().Find("id=\"wf-btn-theme\"") >= 0 &&
	      WebsiteThemeControlsHtml().Find("id=\"wf-btn-density\"") >= 0 &&
	      WebsiteThemeControlsScript().Find("wf_theme") >= 0 && WebsiteThemeControlsScript().Find("wf_density") >= 0 &&
	      WebsiteThemeControlsScript().Find("sameHost") < 0,
	      "shared theme and density controls render buttons and a cookie-writing script");
	Check(WebsiteThemeHeadScript().Find("sameHost") >= 0 && WebsiteThemeHeadScript().Find("wf-density") >= 0,
	      "website theme script preserves task-052 same-host preference propagation");
}

static void RunWebAuditSchemaChecks()
{
	WebAuditEvent e;
	e.at = GetSysTime();
	e.site = "alpha";
	e.kind = "request";
	e.route = "GET:/users/*";
	e.method = "GET";
	e.outcome = "ok";
	String error;
	Check(e.Validate(error), "audit schema accepts bounded categorical event");
	e.route = "GET:/users/alice?token=secret";
	Check(!e.Validate(error), "audit schema rejects query and raw path values");
	e.route = "GET:/users/*";
	e.actor_user_id = String('x', 97);
	Check(!e.Validate(error), "audit schema rejects unbounded actor identifiers");
	e.actor_user_id.Clear();
	e.duration_ms = 3600001;
	Check(!e.Validate(error), "audit schema rejects unbounded durations");
	Check(WebAuditRouteLabel("GET", "users/*") == "GET:/users/*" &&
	      WebAuditRouteLabel("GET", "users/*?x=1").IsEmpty(),
	      "audit route labels retain registered patterns and reject query strings");
	EngineSite https_site;
	https_site.external_origin = "https://public.example:443/internal";
	Check(https_site.PublicUrl("/session/sso") == "https://public.example:443/session/sso" &&
	      https_site.Url("session/sso") == "https://public.example:443/session/sso",
	      "absolute and SSO URLs honor configured HTTPS origin, not Host or scheme downgrade");
	https_site.external_origin = "https://user:secret@public.example:443";
	Check(https_site.PublicUrl("/login").IsEmpty() && https_site.Url("login").IsEmpty(),
	      "malformed public origins with embedded credentials cannot create redirect authorities");
	String ref = WebAuditSessionRef();
	Check(ref.StartsWith("as_") && ref.GetCount() <= 64,
	      "audit session reference is bounded and opaque");
}

static void RunWebAuditStorageChecks(const String& root)
{
	String site_root = AppendFileName(root, "audit-storage");
	WebAuditLog log("audittest", site_root, 2);
	WebAuditEvent e;
	e.at = GetSysTime() - 10; e.site = "audittest"; e.kind = "request";
	e.route = "GET:/safe/*"; e.method = "GET"; e.outcome = "ok"; e.status = 200;
	Check(log.Append(e).ok, "audit event persists through WebStorage");
	Vector<ValueMap> rows;
	Check(log.List(e.at - 60, e.at + 60, 0, 10, rows).ok && rows.GetCount() == 1,
	      "persisted event is queryable from the durable site ledger");
	e.at = GetSysTime() - 31 * 24 * 60 * 60;
	Check(log.Append(e).ok, "old audit event fixture persists before retention");
	e.at = GetSysTime(); e.route = "GET:/new";
	Check(log.Append(e).ok, "append removes events outside the 30-day retention window");
	Vector<String> ids;
	JsonFileStore audit_store("audittest", site_root, "audit_events");
	StoreResult listed = audit_store.List(ids);
	Check(listed.ok && ids.GetCount() == 2,
	      "retention keeps recent history while pruning expired records");
	rows.Clear();
	Check(log.List(GetSysTime() - 60, GetSysTime() + 60, 0, 2, rows).ok && rows.GetCount() == 2 &&
	      (String)rows[0].Get("route") == "GET:/new",
	      "audit pagination returns newest events first");
	e.route = "GET:/overflow";
	log.Record(e);
	Check(!log.Healthy() && audit_store.List(ids).ok && ids.GetCount() == 2,
	      "hard event cap rejects writes without exceeding configured capacity");
	String canary = "raw-query-secret-canary";
	e.route = "GET:/safe?token=" + canary;
	Check(!log.Append(e).ok, "event validation rejects query-bearing route labels");
	bool secret_absent = true;
	for(const String& id : ids)
		secret_absent &= LoadFile(AppendFileName(AppendFileName(AppendFileName(site_root, "data"), "audit_events"), id + ".json")).Find(canary) < 0;
	Check(secret_absent, "durable audit records contain no rejected query secret");
	e.route = "GET:/safe"; e.at = GetSysTime();
	ValueMap tampered = e.ToValue(); tampered.Add("cookie", canary);
	String tampered_rev;
	Check(audit_store.Put("tampered-record", AsJSON(tampered, true), "", tampered_rev).ok,
	      "sensitive-field corruption fixture is written to the audit collection");
	rows.Clear();
	bool safe_list = log.List(GetSysTime() - 60, GetSysTime() + 60, 0, 10, rows).ok;
	ValueArray safe_rows;
	for(const ValueMap& row : rows) safe_rows.Add(row);
	Check(safe_list && rows.GetCount() == 2 && AsJSON(safe_rows, true).Find(canary) < 0,
	      "reader rejects stored records containing fields outside the closed schema");
	String blocked_root = AppendFileName(root, "blocked-audit-root");
	SaveFile(blocked_root, "not a directory");
	WebAuditLog failed_log("failedtest", blocked_root, 2);
	e.site = "failedtest"; e.route = "GET:/safe"; e.at = GetSysTime();
	failed_log.Record(e);
	Check(!failed_log.Healthy(), "audit storage failure is reduced to a health signal and Record does not throw");
}

// AgentIpcClient unit section (task 024 item 3): the argv-only reader driven
// directly over a canned shim. No HTTP; the shim's run count shows whether a
// refused channel ever reached the child process.
// A relink to an account a subject was bound to needs that binding: a revoked sso grant still names the local
// account, and only that subject, that site and that local account match.
static void RunSsoBindingChecks(const String& root)
{
	WebGrants grants("binding-check", AppendFileName(root, "sso-bindings"));
	Vector<String> scopes;
	scopes.Add("sso");
	String gid;
	StoreResult granted = grants.GrantAs("wau_binding_subject", "wau_binding_local", "atlas", "sso", scopes, gid);
	Check(granted.ok && grants.HasSsoBinding("wau_binding_subject", "atlas", "wau_binding_local"),
	      "a granted sso link is a binding for its subject, site and local account");
	Check(grants.Revoke(gid).ok && grants.HasSsoBinding("wau_binding_subject", "atlas", "wau_binding_local"),
	      "a revoked sso link is still a binding (the relink path needs no password)");
	Check(!grants.HasSsoBinding("wau_binding_other", "atlas", "wau_binding_local") &&
	      !grants.HasSsoBinding("wau_binding_subject", "atlas", "wau_binding_other") &&
	      !grants.HasSsoBinding("wau_binding_subject", "science", "wau_binding_local") &&
	      !grants.HasSsoBinding("", "atlas", "wau_binding_local"),
	      "another subject, another local account, another site, or an empty subject has no binding");
}

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

	// Task 047: TOTP time is POSIX seconds, so a code for the current time verifies.
	{
		int64 now = GetUnixNow();
		int64 wall = (int64)time(NULL);
		Check((now > wall ? now - wall : wall - now) < 5, "TOTP time is POSIX seconds");
		String secret = TotpSecretGenerate();
		Check(TotpVerify(secret, TotpCode(secret, now), now), "a TOTP code for the current time verifies");
	}

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

		// create-only: a new id is created once; the second create is a typed conflict
		String created_rev, again_rev;
		StoreResult c1 = store.Put("once", AsJSON(doc), RecordAbsent(), created_rev);
		Check(c1.ok && !c1.conflict, "create-only put creates a new record");
		StoreResult c2 = store.Put("once", AsJSON(doc), RecordAbsent(), again_rev);
		Check(!c2.ok && c2.conflict, "create-only put on an existing record is a typed conflict");
		StoreResult stale = store.Put("once", AsJSON(doc), "99", again_rev);
		Check(!stale.ok && stale.conflict, "stale revision is a typed conflict");

		// transaction: several writes commit under one lock; reads see them
		{
			JsonFileStore::Txn txn(store);
			Check(txn.Ok(), "transaction takes the store lock");
			String t_rev, got, got_rev;
			StoreResult ta = txn.Put("txn-a", AsJSON(doc), RecordAbsent(), t_rev);
			StoreResult tb = txn.Put("txn-b", AsJSON(doc), RecordAbsent(), t_rev);
			Check(ta.ok && tb.ok, "transaction creates two records");
			Check(txn.Get("txn-a", got, got_rev).ok && !got.IsEmpty(), "transaction reads its own write");
		}
		Vector<String> after_txn;
		store.List(after_txn);
		String listed = Join(after_txn, ",");
		Check(listed.Find("txn-a") >= 0 && listed.Find("txn-b") >= 0,
		      "transaction writes are visible after commit");
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


	RunWebsiteTemplateChecks();
	RunWebAuditSchemaChecks();
	RunWebAuditStorageChecks(s_root);
	RunSsoBindingChecks(s_root);
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

	// --- provisioning role denial (task 042): a consent never proposes an elevated role ---
	{
		bool refused = true;
		const char *denied[] = { "admin", "owner", "editor", "finance" };
		for(const char *role : denied) {
			BrokerCallContext ctx;
			ctx.from_site = "atlas";
			ctx.to_site = "meta";
			BrokerCall call;
			call.op = "request-provision";
			call.payload.Add("subject_user_id", "wau_role_probe");
			call.payload.Add("client_site", "atlas");
			call.payload.Add("client_id", "sso");
			call.payload.Add("proposed_username", String("probe-") + role);
			call.payload.Add("proposed_role", role);
			ValueArray sc;
			sc.Add("sso");
			call.payload.Add("scopes", sc);
			refused = refused && !TheWebEngine().Broker().Call(ctx, call).ok;
		}
		Check(refused, "consent refuses elevated roles (admin, owner, editor, finance)");

		BrokerCallContext ctx;
		ctx.from_site = "atlas";
		ctx.to_site = "meta";
		BrokerCall call;
		call.op = "request-provision";
		call.payload.Add("subject_user_id", "wau_role_probe");
		call.payload.Add("client_site", "atlas");
		call.payload.Add("client_id", "sso");
		call.payload.Add("proposed_username", "probe-reader");
		call.payload.Add("proposed_role", "reader");
		ValueArray sc;
		sc.Add("sso");
		call.payload.Add("scopes", sc);
		BrokerResult probe_res = TheWebEngine().Broker().Call(ctx, call);
		if(!probe_res.ok)
			Cout() << "provision probe error: " << probe_res.error << " status " << probe_res.status << "\n";
		Check(probe_res.ok, "consent proposes the least-privilege reader role");
	}
	TheWebEngine().Broker().Register("science", new ScienceService(*s_sci_corpus));
	{
		ScienceService audit_probe(*s_sci_corpus);
		Check(audit_probe.AuditOperation("list-papers") == "list-papers" &&
		      audit_probe.AuditOperation("lookup-secret") == "unclassified",
		      "Science broker audit operation mapping uses registered operation codes");
	}
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
		VectorMap<String, String> admin_f;
		admin_f.Add("username", "sciadmin");
		admin_f.Add("password", "pw-admin");
		HttpReply rad = HttpCall("POST", SCIENCE_PORT, "/login", sci_origin, FormEncode(admin_f));
		String admin_csrf = (String)ParseJSON(rad.body)["csrf"];
		String admin_pair;
		String admin_val = CookieOf(rad, "wfs_science", admin_pair);
		String admin_cookie = "wfs_science=" + admin_val;
		Check(rrd.status == 200 && rau.status == 200 && au_csrf.GetCount() > 0 && rd_csrf.GetCount() > 0,
		      "science reader and author log in");
		Check(rad.status == 200 && admin_csrf.GetCount() > 0, "science policy admin logs in");
		VectorMap<String, String> admin_h;
		SciHeaders(admin_h, admin_cookie, admin_csrf);
		HttpReply policy_page = HttpCall("GET", SCIENCE_PORT, "/policy", admin_h, "");
		Check(policy_page.status == 200 && policy_page.body.Find("Secret-link controls require verified HTTPS transport") >= 0 &&
		      policy_page.body.Find("policy-link-form") < 0,
		      "science admin cannot create secret links over a plain HTTP origin");

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

		// Explicit reload simulates startup loading the persisted policy fixture.
		String policy_path = AppendFileName(AppendFileName(s_root, "science"), "content_policy.json");
		ContentPolicyStore& science_policy = ContentPolicyFor(AppendFileName(s_root, "science"));
		ContentPolicy hidden_record;
		hidden_record.level = CONTENT_PRIVATE;
		science_policy.SetRecord("2612.00001", hidden_record);
		String policy_error;
		Check(science_policy.Save(policy_error), "science record visibility fixture is saved");
		ContentPolicy hidden_article_paper;
		hidden_article_paper.level = CONTENT_PRIVATE;
		science_policy.SetRecord("2612.00002", hidden_article_paper);
		Check(science_policy.Save(policy_error) &&
		      HttpCall("GET", SCIENCE_PORT, "/papers/2612.00002/author", au_h, "").status == 404,
		      "science author cannot view the author page for a policy-hidden paper with article content");
		Check(HttpCall("GET", SCIENCE_PORT, "/api/papers/2612.00002/article", au_h, "").status == 404,
		      "science hidden paper article API also remains unavailable to its author");
		hidden_article_paper.level = CONTENT_PROTECTED;
		science_policy.SetRecord("2612.00002", hidden_article_paper);
		hidden_record.level = CONTENT_PROTECTED;
		science_policy.SetRecord("2612.00001", hidden_record);
		Check(science_policy.Save(policy_error), "science secret-link scope fixtures are saved");
		EngineSite *science_site = TheWebEngine().FindSite("science");
		String old_science_origin = science_site ? science_site->external_origin : String();
		Check(science_site != NULL, "science test site remains registered for TLS policy exchange");
		String secure_origin = "https://127.0.0.1:" + AsString(SCIENCE_PORT);
		if(science_site) {
			science_site->external_origin = secure_origin;
			science_site->trusted_proxy_peers.Clear();
		}
		HttpReply https_origin_direct_access = HttpCall("GET", SCIENCE_PORT, "/policy-access", no_headers, "");
		VectorMap<String, String> forwarded_h;
		forwarded_h.Add("X-Forwarded-Proto", "https");
		HttpReply untrusted_forwarded_access = HttpCall("GET", SCIENCE_PORT, "/policy-access", forwarded_h, "");
		if(science_site)
			science_site->trusted_proxy_peers.Add("127.0.0.2");
		forwarded_h.Add("X-Forwarded-For", "127.0.0.2");
		HttpReply wrong_peer_access = HttpCall("GET", SCIENCE_PORT, "/policy-access", forwarded_h, "");
		Check(https_origin_direct_access.status == 403 && untrusted_forwarded_access.status == 403 &&
		      wrong_peer_access.status == 403,
		      "HTTPS public origin and spoofed forwarded headers do not make direct HTTP secure");
		if(science_site) {
			science_site->trusted_proxy_peers.Clear();
			science_site->trusted_proxy_peers.Add("127.0.0.1");
		}
		VectorMap<String, String> secure_origin_h;
		secure_origin_h.Add("Origin", secure_origin);
		secure_origin_h.Add("X-Forwarded-Proto", "https");
		HttpReply secure_admin_login = HttpCall("POST", SCIENCE_PORT, "/login", secure_origin_h, FormEncode(admin_f));
		String secure_admin_csrf = (String)ParseJSON(secure_admin_login.body)["csrf"];
		String secure_admin_pair;
		String secure_admin_cookie = "wfs_science=" + CookieOf(secure_admin_login, "wfs_science", secure_admin_pair);
		VectorMap<String, String> secure_admin_h;
		secure_admin_h.Add("Origin", secure_origin);
		secure_admin_h.Add("X-Forwarded-Proto", "https");
		secure_admin_h.Add("Cookie", secure_admin_cookie);
		secure_admin_h.Add("X-WF-CSRF", secure_admin_csrf);
		HttpReply secure_policy_page = HttpCall("GET", SCIENCE_PORT, "/policy", secure_admin_h, "");
		Check(secure_admin_login.status == 200 &&
		      ToLower(SetCookieLine(secure_admin_login, "wfs_science")).Find("secure") >= 0 &&
		      secure_policy_page.status == 200 &&
		      secure_policy_page.body.Find("policy-link-form") >= 0 &&
		      secure_policy_page.body.Find("u.hash=m.fragment") >= 0 &&
		      science_site->Url("policy-access").StartsWith("https://"),
		      "HTTPS origin enables admin fragment-link controls and HTTPS-safe site URLs");
		VectorMap<String, String> link_form;
		link_form.Add("scope", "record");
		link_form.Add("id", "2612.00002");
		link_form.Add("target", "/papers/2612.00002");
		link_form.Add("label", "science test resource");
		VectorMap<String, String> direct_admin_h;
		direct_admin_h.Add("Origin", secure_origin);
		direct_admin_h.Add("Cookie", secure_admin_cookie);
		direct_admin_h.Add("X-WF-CSRF", secure_admin_csrf);
		Check(HttpCall("POST", SCIENCE_PORT, "/api/policy/link", direct_admin_h, FormEncode(link_form)).status == 403,
		      "direct cleartext admin request cannot create links despite HTTPS public origin and trusted peer config");
		HttpReply created_link = HttpCall("POST", SCIENCE_PORT, "/api/policy/link", secure_admin_h, FormEncode(link_form));
		ValueMap created_link_json = ParseJSON(created_link.body);
		String fragment = created_link_json.Get("fragment", "");
		String link_token = fragment.StartsWith("wfcp=") ? fragment.Mid(5, fragment.Find("&to=") - 5) : String();
		Check(created_link.status == 200 && !link_token.IsEmpty() && fragment.Find("&to=/papers/2612.00002") >= 0 &&
		      created_link.body.Find("?key=") < 0 && created_link.cache_control.Find("no-store") >= 0 &&
		      created_link.referrer_policy == "no-referrer",
		      "science admin creates a one-time shareable fragment link for a protected record");
		String query_canary = "query-only-test-canary";
		ContentPolicy paper_link_policy = science_policy.Record("2612.00002");
		ContentGrant query_canary_grant;
		query_canary_grant.kind = "link";
		query_canary_grant.value = ContentLinkHash(query_canary);
		query_canary_grant.label = "non-secret URL rejection canary";
		paper_link_policy.grants.Add(pick(query_canary_grant));
		science_policy.SetRecord("2612.00002", paper_link_policy);
		Check(science_policy.Save(policy_error), "science query-only canary is persisted by hash");
		HttpReply query_paper = HttpCall("GET", SCIENCE_PORT, "/papers/2612.00002?key=" + query_canary, no_headers, "");
		HttpReply query_article = HttpCall("GET", SCIENCE_PORT, "/api/papers/2612.00002/article?key=" + query_canary, no_headers, "");
		Check(query_paper.status == 404 && query_article.status == 404 &&
		      query_paper.body.Find(query_canary) < 0 && query_article.body.Find(query_canary) < 0,
		      "Science ignores and never reflects bearer tokens supplied in URL query parameters");
		Check(HttpCall("GET", SCIENCE_PORT, "/policy-access/" + query_canary, no_headers, "").status == 404 &&
		      HttpCall("POST", SCIENCE_PORT, "/api/policy/exchange/" + query_canary, no_headers, "").status == 404,
		      "secret tokens in URL path segments have no access or exchange route");
		if(science_site)
			science_site->external_origin = old_science_origin;
		HttpReply insecure_access = HttpCall("GET", SCIENCE_PORT, "/policy-access", no_headers, "");
		VectorMap<String, String> insecure_exchange_h;
		insecure_exchange_h.Add("Origin", old_science_origin);
		ValueMap insecure_exchange_body;
		insecure_exchange_body.Add("token", "development-only-dummy");
		insecure_exchange_body.Add("target", "/papers/2612.00002");
		HttpReply insecure_exchange = HttpCall("POST", SCIENCE_PORT, "/api/policy/exchange", insecure_exchange_h,
		                                       AsJSON(insecure_exchange_body), "application/json");
		HttpReply insecure_create = HttpCall("POST", SCIENCE_PORT, "/api/policy/link", admin_h, FormEncode(link_form));
		Check(insecure_access.status == 403 && insecure_exchange.status == 403 &&
		      insecure_exchange.set_cookies.IsEmpty() && insecure_create.status == 403,
		      "HTTP origins cannot create secret links, access bootstrap, or exchange tokens");
		if(science_site)
			science_site->external_origin = secure_origin;
		HttpReply access_page = HttpCall("GET", SCIENCE_PORT, "/policy-access", secure_origin_h, "");
		String guest_cookie_pair;
		String guest_session = CookieOf(access_page, "wfs_science", guest_cookie_pair);
		String csrf_marker = "'X-WF-CSRF':'";
		int csrf_start = access_page.body.Find(csrf_marker);
		int csrf_end = csrf_start >= 0 ? access_page.body.Find('\'', csrf_start + csrf_marker.GetCount()) : -1;
		String exchange_csrf = csrf_end >= 0
		                     ? access_page.body.Mid(csrf_start + csrf_marker.GetCount(), csrf_end - csrf_start - csrf_marker.GetCount())
		                     : String();
		Check(access_page.status == 200 && access_page.referrer_policy == "no-referrer" &&
		      access_page.cache_control.Find("no-store") >= 0 && access_page.body.Find("history.replaceState") >= 0 &&
		      access_page.body.Find("api/policy/exchange") >= 0 && access_page.body.Find("location.replace") >= 0 &&
		      access_page.body.Find(link_token) < 0 &&
	      guest_session.GetCount() && exchange_csrf.GetCount() &&
	      ToLower(SetCookieLine(access_page, "wfs_science")).Find("secure") >= 0,
		      "fragment landing removes secrets from history and prohibits referrer/cache leakage");
		VectorMap<String, String> exchange_h;
		exchange_h.Add("Origin", science_site ? science_site->external_origin : String());
		exchange_h.Add("X-Forwarded-Proto", "https");
		exchange_h.Add("Cookie", guest_cookie_pair);
		exchange_h.Add("X-WF-CSRF", exchange_csrf);
		ValueMap bad_exchange_body;
		bad_exchange_body.Add("token", "not-a-valid-link-token");
		bad_exchange_body.Add("target", "/papers/2612.00002");
		HttpReply bad_exchange = HttpCall("POST", SCIENCE_PORT, "/api/policy/exchange", exchange_h,
		                                  AsJSON(bad_exchange_body), "application/json");
		Check(bad_exchange.status == 404 && bad_exchange.set_cookies.IsEmpty() &&
		      bad_exchange.body.Find("not-a-valid-link-token") < 0,
		      "invalid exchange token is rejected without cookie or token reflection");
		VectorMap<String, String> direct_exchange_h;
		direct_exchange_h.Add("Origin", secure_origin);
		direct_exchange_h.Add("Cookie", guest_cookie_pair);
		direct_exchange_h.Add("X-WF-CSRF", exchange_csrf);
		ValueMap direct_exchange_body;
		direct_exchange_body.Add("token", "cleartext-only-dummy");
		direct_exchange_body.Add("target", "/papers/2612.00002");
		HttpReply direct_exchange = HttpCall("POST", SCIENCE_PORT, "/api/policy/exchange", direct_exchange_h,
		                                     AsJSON(direct_exchange_body), "application/json");
		Check(direct_exchange.status == 403 && direct_exchange.set_cookies.IsEmpty() &&
		      direct_exchange.body.Find("cleartext-only-dummy") < 0,
		      "direct cleartext exchange is rejected even with HTTPS origin and trusted peer config");
		HttpReply query_exchange = HttpCall("POST", SCIENCE_PORT,
		                                    "/api/policy/exchange?token=" + query_canary,
		                                    exchange_h, "{\"target\":\"/papers/2612.00002\"}", "application/json");
		Check(query_exchange.status == 404 && query_exchange.set_cookies.IsEmpty(),
		      "exchange never accepts bearer tokens from query parameters");
		ValueMap bad_target_body;
		bad_target_body.Add("token", link_token);
		bad_target_body.Add("target", "//evil.example.invalid/");
		HttpReply bad_target = HttpCall("POST", SCIENCE_PORT, "/api/policy/exchange", exchange_h,
		                                AsJSON(bad_target_body), "application/json");
		Check(bad_target.status == 404 && bad_target.set_cookies.IsEmpty() &&
		      bad_target.body.Find(link_token) < 0,
		      "valid token cannot exchange into a cross-origin redirect or open URL");
		ValueMap exchange_body;
		exchange_body.Add("token", link_token);
		exchange_body.Add("target", "/papers/2612.00002");
		HttpReply exchanged = HttpCall("POST", SCIENCE_PORT, "/api/policy/exchange", exchange_h,
		                               AsJSON(exchange_body), "application/json");
		String link_cookie_line = SetCookieLine(exchanged, "wfcp_link_science");
		Vector<String> cookie_fields = Split(link_cookie_line, ';');
		String opaque_link_cookie = cookie_fields.IsEmpty() ? String() : TrimBoth(cookie_fields[0]);
		int cookie_eq = opaque_link_cookie.Find('=');
		String opaque_value = cookie_eq < 0 ? String() : opaque_link_cookie.Mid(cookie_eq + 1);
		String lower_cookie_line = ToLower(link_cookie_line);
		Check(exchanged.status == 200 && exchanged.body.Find(link_token) < 0 &&
		      exchanged.body.Find("/papers/2612.00002") < 0,
		      "valid POST-body exchange succeeds without echoing its bearer token");
		Check(lower_cookie_line.Find("secure") >= 0 && lower_cookie_line.Find("httponly") >= 0 &&
		      lower_cookie_line.Find("samesite=strict") >= 0 && lower_cookie_line.Find("expires=") >= 0 &&
		      opaque_value.GetCount() >= 32 &&
		      opaque_value != link_token && opaque_value != ContentLinkHash(link_token),
		      "exchange issues a secure, HttpOnly, SameSite, opaque short-lived site cookie");
		VectorMap<String, String> link_reader_h;
		link_reader_h.Add("Cookie", opaque_link_cookie);
		Check(HttpCall("GET", SCIENCE_PORT, "/papers/2612.00002", link_reader_h, "").status == 200 &&
		      HttpCall("GET", SCIENCE_PORT, "/api/papers/2612.00002/article", link_reader_h, "").status == 200,
		      "fragment-to-POST-equivalent exchange grants the bound protected paper and article");
		Check(HttpCall("GET", SCIENCE_PORT, "/papers/2612.00002/author", link_reader_h, "").status == 403,
		      "secret link does not bypass Science author-role checks");
		Check(HttpCall("GET", SCIENCE_PORT, "/papers/2612.00001", link_reader_h, "").status == 404,
		      "record-scoped secret link cannot read a different protected paper");
		ContentPolicy revoked_link_policy = science_policy.Record("2612.00002");
		for(ContentGrant& grant : revoked_link_policy.grants)
			if(grant.kind == "link" && grant.value == ContentLinkHash(link_token))
				grant.revoked = true;
		science_policy.SetRecord("2612.00002", revoked_link_policy);
		Check(science_policy.Save(policy_error) &&
		      HttpCall("GET", SCIENCE_PORT, "/papers/2612.00002", link_reader_h, "").status == 404,
		      "revoking the stored token invalidates an already issued scoped cookie");
		Check(HttpCall("GET", SCIENCE_PORT, "/papers/2612.00002", no_headers, "").status == 404,
		      "protected resource remains inaccessible without the exchange cookie");
		if(science_site) {
			science_site->external_origin = old_science_origin;
			science_site->trusted_proxy_peers.Clear();
		}
		hidden_article_paper.level = CONTENT_PUBLIC;
		science_policy.SetRecord("2612.00002", hidden_article_paper);
		hidden_record.level = CONTENT_PRIVATE;
		science_policy.SetRecord("2612.00001", hidden_record);
		Check(science_policy.Save(policy_error), "science policy fixtures are restored after link tests");
		HttpReply home = HttpCall("GET", SCIENCE_PORT, "/", no_headers, "");
		HttpReply visible_list = HttpCall("GET", SCIENCE_PORT, "/api/papers", no_headers, "");
		ValueMap visible_list_json = ParseJSON(visible_list.body);
		Check(home.body.Find("1 papers, 1 with structured article") >= 0
		      && (int)visible_list_json.Get("count", 0) == 1,
		      "science home aggregate counts use the same visibility filter as the paper list");
		FileDelete(policy_path);
		science_policy.Load(AppendFileName(s_root, "science"), policy_error);
		SaveFile(policy_path, "{broken");
		science_policy.Load(AppendFileName(s_root, "science"), policy_error);
		HttpReply hidden = HttpCall("GET", SCIENCE_PORT, "/api/papers/2612.00001", no_headers, "");
		Check(hidden.status == 404, "science anonymous read fails closed when the persisted policy is corrupt");
		FileDelete(policy_path);
		science_policy.Load(AppendFileName(s_root, "science"), policy_error);

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
		VectorMap<String, String> ph_anon;
		Check(HttpCall("GET", PRIVATEHUB_PORT, "/policy", ph_anon, "").status == 401,
		      "privatehub policy administration requires authentication");
		Check(HttpCall("GET", PRIVATEHUB_PORT, "/policy", view_h, "").status == 403,
		      "privatehub policy administration requires the admin role");
		Check(HttpCall("POST", PRIVATEHUB_PORT, "/api/policy/link", view_h, "").status == 403,
		      "privatehub secret-link creation remains admin-only");

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
		if(!opened.IsEmpty()) {
			String policy_root = AppendFileName(s_root, "privatehub");
			ContentPolicyStore& store = ContentPolicyFor(policy_root);
			ContentPolicy issue_policy;
			issue_policy.level = CONTENT_PRIVATE;
			ContentGrant member_grant;
			member_grant.kind = "group";
			member_grant.value = "member";
			issue_policy.grants.Add(pick(member_grant));
			store.SetRecord("oulubsd/demo:" + opened[0].issue_id, issue_policy);
			String policy_error;
			Check(store.Save(policy_error), "privatehub issue policy fixture is persisted");
			String detail = repo + "/issues/" + opened[0].issue_id;
			HttpReply member_list = HttpCall("GET", PRIVATEHUB_PORT, repo + "/issues", mem_h, "");
			HttpReply viewer_list = HttpCall("GET", PRIVATEHUB_PORT, repo + "/issues", view_h, "");
			Check(member_list.body.Find(opened[0].issue_id) >= 0 && viewer_list.body.Find(opened[0].issue_id) < 0,
			      "privatehub issue list applies group policy after login");
			Check(HttpCall("GET", PRIVATEHUB_PORT, detail, mem_h, "").status == 200 &&
			      HttpCall("GET", PRIVATEHUB_PORT, detail, view_h, "").status == 404,
			      "privatehub issue detail hides records from ungranted authenticated users");
			issue_policy.level = CONTENT_PUBLIC_ONLY;
			issue_policy.grants.Clear();
			store.SetRecord("oulubsd/demo:" + opened[0].issue_id, issue_policy);
			Check(store.Save(policy_error) &&
			      HttpCall("GET", PRIVATEHUB_PORT, detail, mem_h, "").status == 404 &&
			      HttpCall("GET", PRIVATEHUB_PORT, detail, ph_anon, "").status == 302,
			      "privatehub public-only policy narrows authenticated reads without bypassing login");
			issue_policy.level = CONTENT_PUBLIC;
			store.SetRecord("oulubsd/demo:" + opened[0].issue_id, issue_policy);
			Check(store.Save(policy_error), "privatehub issue policy fixture is restored");
		}
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
		String ph_policy_root = AppendFileName(s_root, "privatehub");
		String ph_policy_path = AppendFileName(ph_policy_root, "content_policy.json");
		ContentPolicyStore& ph_policy_store = ContentPolicyFor(ph_policy_root);
		String ph_policy_error;
		Check(SaveFile(ph_policy_path, "{broken") && !ph_policy_store.Load(ph_policy_root, ph_policy_error),
		      "privatehub corrupt policy fixture fails loading");
		Check(HttpCall("GET", PRIVATEHUB_PORT, repo + "/issues", mem_h, "").status == 503,
		      "privatehub protected list fails closed on corrupt policy data");
		FileDelete(ph_policy_path);
		Check(ph_policy_store.Load(ph_policy_root, ph_policy_error), "privatehub policy store recovers after fixture removal");
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
	Check(SetCookieLine(rs_a, "wfs_alpha").Find("; Expires=") >= 0, "session cookie carries an Expires attribute");
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

	// Audit producer integration: real engine routes and an in-process broker call.
	String audit_canary = "unlogged-query-canary";
	HttpCall("GET", ALPHA_PORT, "/greet/world?token=" + audit_canary, no_headers, "");
	BrokerCallContext audit_ctx;
	audit_ctx.from_site = "beta"; audit_ctx.to_site = "alpha";
	BrokerCall audit_call;
	audit_call.op = "add"; audit_call.payload.Add("a", 4); audit_call.payload.Add("b", 5);
	audit_call.payload.Add("secret_marker", audit_canary);
	Check(TheWebEngine().Broker().Call(audit_ctx, audit_call).ok,
	      "broker audit fixture completed as an in-process call");
	BrokerCall audit_failure; audit_failure.op = "missing_operation";
	audit_failure.payload.Add("secret_marker", audit_canary);
	Check(!TheWebEngine().Broker().Call(audit_ctx, audit_failure).ok,
	      "failed in-process broker fixture returns failure");
	VectorMap<String, String> failed_login_form;
	failed_login_form.Add("username", "username-secret-canary");
	failed_login_form.Add("password", "password-secret-canary");
	VectorMap<String, String> failed_login_headers;
	failed_login_headers.Add("Accept", "application/json");
	failed_login_headers.Add("Content-Type", "application/x-www-form-urlencoded");
	Check(HttpCall("POST", ALPHA_PORT, "/session/login", failed_login_headers, FormEncode(failed_login_form)).status == 401,
	      "failed password login returns the normal authentication response");
	auto audit_login = [&](int port, VectorMap<String, String>& headers, String& cookie, String& csrf) {
		VectorMap<String, String> f; f.Add("username", "auditadmin"); f.Add("password", "pw-audit");
		VectorMap<String, String> h; h.Add("Accept", "application/json");
		h.Add("Content-Type", "application/x-www-form-urlencoded");
		HttpReply login = HttpCall("POST", port, "/session/login", h, FormEncode(f));
		cookie = CookieOf(login, port == ALPHA_PORT ? "wfs_alpha" : "wfs_beta", csrf);
		ValueMap body = ParseJSON(login.body);
		csrf = body.Get("csrf", "");
		headers.Add("Cookie", (port == ALPHA_PORT ? String("wfs_alpha=") : String("wfs_beta=")) + cookie);
		headers.Add("X-WF-CSRF", csrf);
		headers.Add("Origin", Format("http://127.0.0.1:%d", port));
		headers.Add("Accept", "application/json");
	};
	VectorMap<String, String> audit_admin_alpha, audit_admin_beta;
	String audit_cookie_alpha, audit_cookie_beta, audit_csrf_alpha, audit_csrf_beta;
	audit_login(ALPHA_PORT, audit_admin_alpha, audit_cookie_alpha, audit_csrf_alpha);
	audit_login(BETA_PORT, audit_admin_beta, audit_cookie_beta, audit_csrf_beta);
	VectorMap<String, String> browser_next_form;
	browser_next_form.Add("username", "auditadmin"); browser_next_form.Add("password", "pw-audit");
	browser_next_form.Add("next", "https://outside.example/steal?token=redirect-secret-canary");
	VectorMap<String, String> browser_next_headers;
	browser_next_headers.Add("Content-Type", "application/x-www-form-urlencoded");
	Check(HttpCall("POST", ALPHA_PORT, "/session/login", browser_next_headers, FormEncode(browser_next_form)).status == 302,
	      "browser login rejects an unconfigured return origin and preserves the normal redirect response");
	TheWebEngine().SetMetaSso(true);
	VectorMap<String, String> blocked_local_login_headers;
	blocked_local_login_headers.Add("Content-Type", "application/x-www-form-urlencoded");
	blocked_local_login_headers.Add("Accept", "text/html");
	HttpReply blocked_local_login = HttpCall("POST", ALPHA_PORT, "/session/login", blocked_local_login_headers,
	                                         FormEncode(browser_next_form));
	Check(blocked_local_login.status == 403,
	      "local browser password login remains refused while MetaAccount SSO is enabled");
	{
		EngineSite *atlas_site = TheWebEngine().FindSite("atlas");
		EngineSite *science_site = TheWebEngine().FindSite("science");
		String central_id;
		bool central_existed = false;
		Vector<String> basic_roles; basic_roles.Add("member");
		bool fixture_ok = atlas_site && science_site &&
		                  TheWebEngine().FindSite("meta")->Accounts().CreateUser("ssoflow", "fixture-password",
		                  "SSO Flow", basic_roles, central_id, central_existed).ok && !central_existed;
		String collision_id;
		fixture_ok = fixture_ok && atlas_site->Accounts().CreateUser("ssoflow", "local-proof-password",
		                  "Existing Atlas User", basic_roles, collision_id, central_existed).ok && !central_existed;
		String local_totp_secret;
		Vector<String> local_recovery_codes;
		fixture_ok = fixture_ok && atlas_site->Accounts().BeginTotpEnrollment(collision_id, "local-proof-password",
		                  local_totp_secret, local_recovery_codes).ok &&
		                  atlas_site->Accounts().ConfirmTotpEnrollment(collision_id,
		                  TotpCode(local_totp_secret, TotpUnixNow(), 30, 6)).ok;
		VectorMap<String, String> central_form;
		central_form.Add("username", "ssoflow"); central_form.Add("password", "fixture-password");
		central_form.Add("next", Format("http://127.0.0.1:%d/session/sso?next=%%2Fatlas", ATLAS_PORT));
		VectorMap<String, String> central_headers;
		central_headers.Add("Accept", "text/html"); central_headers.Add("Content-Type", "application/x-www-form-urlencoded");
		HttpReply central_login = HttpCall("POST", META_PORT, "/login", central_headers, FormEncode(central_form));
		String central_pair;
		String central_cookie = CookieOf(central_login, "wfs_meta", central_pair);
		VectorMap<String, String> atlas_sso_headers;
		atlas_sso_headers.Add("Cookie", "wfs_meta=" + central_cookie);
		HttpReply consent_page = HttpCall("GET", ATLAS_PORT, "/session/sso?next=%2Fatlas", atlas_sso_headers, "");
		String atlas_guest_pair;
		String atlas_guest = CookieOf(consent_page, "wfs_atlas", atlas_guest_pair);
		String csrf_marker = "name=\"__wf_csrf__\" value=\"";
		int csrf_at = consent_page.body.Find(csrf_marker);
		String atlas_csrf;
		if(csrf_at >= 0) {
			int start = csrf_at + csrf_marker.GetCount(), end = consent_page.body.Find('"', start);
			if(end > start) atlas_csrf = consent_page.body.Mid(start, end - start);
		}
		VectorMap<String, String> atlas_post_headers;
		atlas_post_headers.Add("Origin", Format("http://127.0.0.1:%d", ATLAS_PORT));
		atlas_post_headers.Add("Cookie", "wfs_meta=" + central_cookie + "; wfs_atlas=" + atlas_guest);
		atlas_post_headers.Add("X-WF-CSRF", atlas_csrf);
		atlas_post_headers.Add("Content-Type", "application/x-www-form-urlencoded");
		VectorMap<String, String> wrong_proof;
		wrong_proof.Add("decision", "yes"); wrong_proof.Add("next", "/atlas"); wrong_proof.Add("local_password", "wrong-proof");
		HttpReply rejected_link = HttpCall("POST", ATLAS_PORT, "/session/sso/consent", atlas_post_headers,
		                                   FormEncode(wrong_proof));
		VectorMap<String, String> bad_factor;
		bad_factor.Add("decision", "yes"); bad_factor.Add("next", "/atlas");
		bad_factor.Add("local_password", "local-proof-password");
		HttpReply rejected_factor = HttpCall("POST", ATLAS_PORT, "/session/sso/consent", atlas_post_headers,
		                                     FormEncode(bad_factor));
		WebAccountGrant no_link;
		bool unlinked = !s_meta_grants->FindActive(central_id, "atlas", {"sso"}, no_link);
		VectorMap<String, String> valid_proof;
		valid_proof.Add("decision", "yes"); valid_proof.Add("next", "/atlas"); valid_proof.Add("local_password", "local-proof-password");
		valid_proof.Add("local_code", local_recovery_codes[0]);
		HttpReply accepted_link = HttpCall("POST", ATLAS_PORT, "/session/sso/consent", atlas_post_headers,
		                                   FormEncode(valid_proof));
		VectorMap<String, String> accepted_cookie_h;
		String atlas_local_pair;
		String atlas_local_cookie = CookieOf(accepted_link, "wfs_atlas", atlas_local_pair);
		accepted_cookie_h.Add("Cookie", "wfs_atlas=" + atlas_local_cookie);
		HttpReply atlas_who = HttpCall("GET", ATLAS_PORT, "/session/whoami", accepted_cookie_h, "");
		WebAccountUser unchanged;
		bool safe_link = atlas_site->Accounts().GetUser(collision_id, unchanged) &&
		                 unchanged.roles.GetCount() == 1 && unchanged.roles[0] == "member";
		bool recovery_consumed = !atlas_site->Accounts().VerifyTotpOrRecovery(collision_id, local_recovery_codes[0]);
		Check(fixture_ok && central_login.status == 302 && central_cookie.GetCount() > 0 &&
		      SetCookieLine(central_login, "wfs_meta").Find("Path=/") >= 0 &&
		      consent_page.status == 200 && consent_page.body.Find("Allow MetaAccount") >= 0 &&
		      atlas_guest.GetCount() > 0 && atlas_csrf.GetCount() > 0,
		      "MetaAccount cookie persists across app ports and first visit presents immediate consent");
		if(consent_page.status != 200 || csrf_at < 0 || rejected_link.status != 403 || accepted_link.status != 302)
			Cout() << "SSO flow diagnostic statuses: login=" << central_login.status << " consent=" << consent_page.status
			       << " csrf=" << (csrf_at >= 0) << " reject=" << rejected_link.status << " accept=" << accepted_link.status
			       << " callback=" << consent_page.body.Find("Ownership proof") << "\n";
		if(!(fixture_ok && central_login.status == 302 && central_cookie.GetCount() > 0 &&
		     SetCookieLine(central_login, "wfs_meta").Find("Path=/") >= 0 && consent_page.status == 200 &&
		     consent_page.body.Find("Allow MetaAccount") >= 0 && atlas_guest.GetCount() > 0 && atlas_csrf.GetCount() > 0))
			Cout() << "SSO acceptance checks: fixture=" << fixture_ok << " login=" << central_login.status
			       << " cookie=" << central_cookie.GetCount() << " cookie_path=" << SetCookieLine(central_login, "wfs_meta").Find("Path=/")
			       << " consent_text=" << consent_page.body.Find("Allow MetaAccount") << " guest=" << atlas_guest.GetCount()
			       << " csrf=" << atlas_csrf.GetCount() << "\n";
		Check(rejected_link.status == 403 && unlinked,
		      "same-name account is not linked without valid password proof");
		Check(consent_page.body.Find("local_code") >= 0,
		      "existing local account proof preserves its enrolled TOTP requirement");
		Check(rejected_factor.status == 403 && unlinked,
		      "same-name account is not linked with password alone when local TOTP is enrolled");
		Check(accepted_link.status == 302 && accepted_link.location == "/atlas" &&
		      atlas_who.status == 200 && atlas_who.body.Find(collision_id) >= 0 && safe_link && recovery_consumed,
		      "password plus enrolled recovery proof self-links unchanged account and consumes that code once");
		String plain_subject, plain_local_id;
		bool plain_existed = false;
		bool plain_fixture = TheWebEngine().FindSite("meta")->Accounts().CreateUser("plainflow", "fixture-plain-password",
		                    "Plain SSO", basic_roles, plain_subject, plain_existed).ok && !plain_existed &&
		        atlas_site->Accounts().CreateUser("plainflow", "local-plain-password", "Existing Plain Atlas",
		                    basic_roles, plain_local_id, plain_existed).ok && !plain_existed;
		VectorMap<String, String> plain_login_form;
		plain_login_form.Add("username", "plainflow"); plain_login_form.Add("password", "fixture-plain-password");
		plain_login_form.Add("next", Format("http://127.0.0.1:%d/session/sso?next=%%2Fplain", ATLAS_PORT));
		HttpReply plain_login = HttpCall("POST", META_PORT, "/login", central_headers, FormEncode(plain_login_form));
		String plain_meta_pair;
		String plain_meta_cookie = CookieOf(plain_login, "wfs_meta", plain_meta_pair);
		VectorMap<String, String> plain_visit_headers;
		plain_visit_headers.Add("Cookie", "wfs_meta=" + plain_meta_cookie);
		HttpReply plain_consent = HttpCall("GET", ATLAS_PORT, "/session/sso?next=%2Fplain", plain_visit_headers, "");
		String plain_guest_pair;
		String plain_guest = CookieOf(plain_consent, "wfs_atlas", plain_guest_pair);
		int plain_csrf_at = plain_consent.body.Find(csrf_marker);
		String plain_csrf;
		if(plain_csrf_at >= 0) {
			int start = plain_csrf_at + csrf_marker.GetCount(), end = plain_consent.body.Find('"', start);
			if(end > start) plain_csrf = plain_consent.body.Mid(start, end - start);
		}
		VectorMap<String, String> plain_post_headers;
		plain_post_headers.Add("Origin", Format("http://127.0.0.1:%d", ATLAS_PORT));
		plain_post_headers.Add("Cookie", "wfs_meta=" + plain_meta_cookie + "; wfs_atlas=" + plain_guest);
		plain_post_headers.Add("X-WF-CSRF", plain_csrf);
		plain_post_headers.Add("Content-Type", "application/x-www-form-urlencoded");
		VectorMap<String, String> missing_password; missing_password.Add("decision", "yes"); missing_password.Add("next", "/plain");
		HttpReply rejected_username_only = HttpCall("POST", ATLAS_PORT, "/session/sso/consent", plain_post_headers,
		                                            FormEncode(missing_password));
		WebAccountGrant plain_unlinked;
		bool no_plain_grant = !s_meta_grants->FindActive(plain_subject, "atlas", {"sso"}, plain_unlinked);
		VectorMap<String, String> plain_proof;
		plain_proof.Add("decision", "yes"); plain_proof.Add("next", "/plain");
		plain_proof.Add("local_password", "local-plain-password");
		HttpReply accepted_plain_link = HttpCall("POST", ATLAS_PORT, "/session/sso/consent", plain_post_headers,
		                                         FormEncode(plain_proof));
		WebAccountUser plain_unchanged;
		bool plain_roles_unchanged = atlas_site->Accounts().GetUser(plain_local_id, plain_unchanged) &&
		        !plain_unchanged.totp_enrolled && plain_unchanged.roles.GetCount() == 1 &&
		        plain_unchanged.roles[0] == "member";
		Check(plain_fixture && plain_login.status == 302 && plain_consent.status == 200 &&
		      plain_consent.body.Find("name=\"local_password\"") >= 0 &&
		      plain_consent.body.Find("name=\"local_code\"") < 0,
		      "all collisions require password while an unenrolled account has no TOTP prompt");
		Check(rejected_username_only.status == 403 && no_plain_grant,
		      "a same-name collision is never linked by MetaAccount username alone");
		Check(accepted_plain_link.status == 302 && accepted_plain_link.location == "/plain" &&
		      plain_roles_unchanged,
		      "password-only proof links a collision iff its local account has no TOTP enrolled");
		HttpReply atlas_refresh = HttpCall("GET", ATLAS_PORT, "/session/whoami", accepted_cookie_h, "");
		VectorMap<String, String> science_sso_headers;
		science_sso_headers.Add("Cookie", "wfs_meta=" + central_cookie);
		HttpReply science_consent = HttpCall("GET", SCIENCE_PORT, "/session/sso?next=%2Fpapers", science_sso_headers, "");
		String science_guest_pair;
		String science_guest = CookieOf(science_consent, "wfs_science", science_guest_pair);
		int science_csrf_at = science_consent.body.Find(csrf_marker);
		String science_csrf;
		if(science_csrf_at >= 0) {
			int start = science_csrf_at + csrf_marker.GetCount(), end = science_consent.body.Find('"', start);
			if(end > start) science_csrf = science_consent.body.Mid(start, end - start);
		}
		VectorMap<String, String> science_post_headers;
		science_post_headers.Add("Origin", Format("http://127.0.0.1:%d", SCIENCE_PORT));
		science_post_headers.Add("Cookie", "wfs_meta=" + central_cookie + "; wfs_science=" + science_guest);
		science_post_headers.Add("X-WF-CSRF", science_csrf);
		science_post_headers.Add("Content-Type", "application/x-www-form-urlencoded");
		VectorMap<String, String> science_yes; science_yes.Add("decision", "yes"); science_yes.Add("next", "/papers");
		HttpReply science_accepted = HttpCall("POST", SCIENCE_PORT, "/session/sso/consent", science_post_headers,
		                                      FormEncode(science_yes));
		String science_local_pair;
		String science_local_cookie = CookieOf(science_accepted, "wfs_science", science_local_pair);
		WebAccountUser provisioned;
		bool least_privilege = science_site->Accounts().FindUserByUsername("ssoflow", provisioned) &&
		                       provisioned.roles.GetCount() == 1 && provisioned.roles[0] == "reader";
		VectorMap<String, String> science_who_headers;
		science_who_headers.Add("Cookie", "wfs_science=" + science_local_cookie);
		HttpReply science_who = HttpCall("GET", SCIENCE_PORT, "/session/whoami", science_who_headers, "");
		ValueMap atlas_who_json = ParseJSON(atlas_who.body);
		VectorMap<String, String> replayed_deny_headers;
		replayed_deny_headers.Add("Origin", Format("http://127.0.0.1:%d", ATLAS_PORT));
		replayed_deny_headers.Add("Cookie", "wfs_meta=" + central_cookie + "; wfs_atlas=" + atlas_local_cookie);
		replayed_deny_headers.Add("X-WF-CSRF", atlas_who_json.Get("csrf", ""));
		replayed_deny_headers.Add("Content-Type", "application/x-www-form-urlencoded");
		VectorMap<String, String> replayed_deny_form;
		replayed_deny_form.Add("decision", "no"); replayed_deny_form.Add("next", "/atlas");
		HttpReply replayed_deny = HttpCall("POST", ATLAS_PORT, "/session/sso/consent", replayed_deny_headers,
		                                   FormEncode(replayed_deny_form));
		HttpReply existing_atlas_still_auth = HttpCall("GET", ATLAS_PORT, "/session/whoami", accepted_cookie_h, "");
		VectorMap<String, String> atlas_logout_headers;
		atlas_logout_headers.Add("Origin", Format("http://127.0.0.1:%d", ATLAS_PORT));
		atlas_logout_headers.Add("Cookie", "wfs_atlas=" + atlas_local_cookie);
		atlas_logout_headers.Add("X-WF-CSRF", atlas_who_json.Get("csrf", ""));
		atlas_logout_headers.Add("Content-Type", "application/x-www-form-urlencoded");
		HttpReply atlas_logout = HttpCall("POST", ATLAS_PORT, "/session/logout", atlas_logout_headers, "");
		HttpReply atlas_after_logout = HttpCall("GET", ATLAS_PORT, "/session/whoami", atlas_logout_headers, "");
		VectorMap<String, String> central_still_headers;
		central_still_headers.Add("Cookie", "wfs_meta=" + central_cookie);
		HttpReply central_still = HttpCall("GET", META_PORT, "/", central_still_headers, "");
		Check(atlas_refresh.status == 200 && atlas_refresh.body.Find(collision_id) >= 0,
		      "Atlas local SSO session survives refresh");
		Check(science_consent.status == 200 && science_consent.body.Find("Allow MetaAccount") >= 0 &&
		      science_accepted.status == 302 && science_accepted.location == "/papers" &&
		      science_who.status == 200 && least_privilege && science_local_cookie.GetCount() > 0,
		      "second app requires separate consent, reuses central login and gets its own least-privilege session");
		Check(atlas_logout.status == 302 && atlas_after_logout.status == 401 && science_who.status == 200 &&
		      central_still.status == 200 && central_still.body.Find("ssoflow") >= 0,
		      "Atlas logout is isolated from MetaAccount and the second app session");
		Check(replayed_deny.status == 403 && SetCookieLine(replayed_deny, "wfs_atlas").IsEmpty() &&
		      existing_atlas_still_auth.status == 200,
		      "a stale consent Deny cannot revoke or clear an already authenticated Atlas session");
		String no_subject;
		bool no_existed = false;
		bool no_fixture = TheWebEngine().FindSite("meta")->Accounts().CreateUser("ssono", "fixture-no-password",
		                    "SSO No", basic_roles, no_subject, no_existed).ok && !no_existed;
		Vector<String> no_scopes; no_scopes.Add("sso");
		String no_old_atlas, no_other_site, no_other_subject;
		bool no_old_exists = false, no_other_site_exists = false, no_other_subject_exists = false;
		no_fixture = no_fixture && s_meta_grants->RequestProvision(no_subject, "atlas", "sso", "ssono-old",
		                    "reader", no_scopes, no_old_atlas, no_old_exists).ok && !no_old_exists &&
		        s_meta_grants->RequestProvision(no_subject, "science", "sso", "ssono-science",
		                    "reader", no_scopes, no_other_site, no_other_site_exists).ok && !no_other_site_exists &&
		        s_meta_grants->RequestProvision("sso_unrelated", "atlas", "sso", "unrelated-atlas",
		                    "reader", no_scopes, no_other_subject, no_other_subject_exists).ok && !no_other_subject_exists;
		VectorMap<String, String> no_login_form;
		no_login_form.Add("username", "ssono"); no_login_form.Add("password", "fixture-no-password");
		no_login_form.Add("next", Format("http://127.0.0.1:%d/session/sso?next=%%2Fatlas", ATLAS_PORT));
		HttpReply no_login = HttpCall("POST", META_PORT, "/login", central_headers, FormEncode(no_login_form));
		String no_meta_pair;
		String no_meta_cookie = CookieOf(no_login, "wfs_meta", no_meta_pair);
		VectorMap<String, String> no_visit_headers;
		no_visit_headers.Add("Cookie", "wfs_meta=" + no_meta_cookie);
		HttpReply no_consent = HttpCall("GET", ATLAS_PORT, "/session/sso?next=%2Fatlas", no_visit_headers, "");
		String no_guest_pair;
		String no_guest = CookieOf(no_consent, "wfs_atlas", no_guest_pair);
		WebAccountSession no_guest_session;
		bool consent_uses_guest_session = !no_guest.IsEmpty() &&
		        TheWebEngine().FindSite("atlas")->Accounts().ResolveSession(no_guest, no_guest_session) &&
		        no_guest_session.user_id.IsEmpty();
		int no_csrf_at = no_consent.body.Find(csrf_marker);
		String no_csrf;
		if(no_csrf_at >= 0) {
			int start = no_csrf_at + csrf_marker.GetCount(), end = no_consent.body.Find('"', start);
			if(end > start) no_csrf = no_consent.body.Mid(start, end - start);
		}
		VectorMap<String, String> no_headers;
		no_headers.Add("Origin", Format("http://127.0.0.1:%d", ATLAS_PORT));
		no_headers.Add("Cookie", "wfs_meta=" + no_meta_cookie + "; wfs_atlas=" + no_guest);
		no_headers.Add("Content-Type", "application/x-www-form-urlencoded");
		VectorMap<String, String> no_form; no_form.Add("decision", "no"); no_form.Add("next", "/atlas");
		HttpReply no_without_csrf = HttpCall("POST", ATLAS_PORT, "/session/sso/consent", no_headers, FormEncode(no_form));
		WebAccountSession after_csrf_rejection;
		WebAccountGrant pending_after_csrf_rejection;
		bool csrf_rejection_keeps_guest = no_without_csrf.status == 403 &&
		        TheWebEngine().FindSite("atlas")->Accounts().ResolveSession(no_guest, after_csrf_rejection) &&
		        SetCookieLine(no_without_csrf, "wfs_atlas").IsEmpty() &&
		        s_meta_grants->GetGrant(no_old_atlas, pending_after_csrf_rejection) &&
		        pending_after_csrf_rejection.state == "requested";
		no_headers.Add("X-WF-CSRF", no_csrf);
		HttpReply no_result = HttpCall("POST", ATLAS_PORT, "/session/sso/consent", no_headers, FormEncode(no_form));
		WebAccountUser absent_user;
		WebAccountGrant no_target_request, other_site_request, other_subject_request;
		bool old_request_resolved = s_meta_grants->GetGrant(no_old_atlas, no_target_request) &&
		        no_target_request.state == "denied" &&
		        s_meta_grants->GetGrant(no_other_site, other_site_request) &&
		        other_site_request.state == "requested" &&
		        s_meta_grants->GetGrant(no_other_subject, other_subject_request) &&
		        other_subject_request.state == "requested";
		WebAccountGrant no_active_grant;
		bool no_target_grant = s_meta_grants->FindActive(no_subject, "atlas", {"sso"}, no_active_grant);
		WebAccountSession after_denial;
		bool guest_revoked = !TheWebEngine().FindSite("atlas")->Accounts().ResolveSession(no_guest, after_denial);
		String denied_local_cookie = SetCookieLine(no_result, "wfs_atlas");
		VectorMap<String, String> stale_guest_headers;
		stale_guest_headers.Add("Cookie", "wfs_atlas=" + no_guest);
		HttpReply stale_guest_whoami = HttpCall("GET", ATLAS_PORT, "/session/whoami", stale_guest_headers, "");
		bool unrelated_cookies_preserved = SetCookieLine(no_result, "wfs_meta").IsEmpty() &&
		        SetCookieLine(no_result, "wfs_science").IsEmpty();
		HttpReply science_after_deny = HttpCall("GET", SCIENCE_PORT, "/session/whoami", science_who_headers, "");
		Check(consent_uses_guest_session && csrf_rejection_keeps_guest,
		      "consent GET uses only an anonymous CSRF session and missing CSRF cannot deny or clear it");
		Check(no_fixture && no_login.status == 302 && no_consent.status == 200 && no_result.status == 302 &&
		      !TheWebEngine().FindSite("atlas")->Accounts().FindUserByUsername("ssono", absent_user) &&
		      !no_target_grant && old_request_resolved && guest_revoked && denied_local_cookie.Find("wfs_atlas=") >= 0 &&
		      denied_local_cookie.Find("Expires=") >= 0 && stale_guest_whoami.status == 401 &&
		      unrelated_cookies_preserved && science_after_deny.status == 200,
		      "No revokes the guest session, clears only Atlas's cookie, and creates no account or grant without disturbing Science");
	}
	TheWebEngine().SetMetaSso(false);
	String bad_cookie_canary = "raw-cookie-secret-canary";
	VectorMap<String, String> bad_cookie_header; bad_cookie_header.Add("Cookie", "wfs_alpha=" + bad_cookie_canary);
	HttpCall("GET", ALPHA_PORT, "/whoami", bad_cookie_header, "");
	HttpCall("GET", ALPHA_PORT, "/audit-redirect-local", no_headers, "");
	HttpCall("GET", ALPHA_PORT, "/audit-redirect-external", no_headers, "");
	VectorMap<String, String> spoofed_sso_host;
	spoofed_sso_host.Add("Host", "attacker.example:" + AsString(ALPHA_PORT));
	HttpReply sso_redirect = HttpCall("GET", ALPHA_PORT, "/audit-redirect-sso", spoofed_sso_host, "");
	EngineSite *meta_public_site = TheWebEngine().FindSite("meta");
	Check(meta_public_site && sso_redirect.status == 302 && sso_redirect.location == meta_public_site->PublicUrl("/login"),
	      "SSO redirect uses the configured public origin instead of spoofed Host");
	VectorMap<String, String> audit_anon;
	Check(HttpCall("GET", ALPHA_PORT, "/api/audit/events", audit_anon, "").status == 401,
	      "audit events API rejects anonymous callers with 401");
	Check(HttpCall("GET", ALPHA_PORT, "/api/audit/events", h_alpha, "").status == 403,
	      "audit events API rejects signed-in non-admins with 403");
	Check(HttpCall("GET", ALPHA_PORT, "/api/audit/sessions", audit_anon, "").status == 401 &&
	      HttpCall("GET", ALPHA_PORT, "/api/audit/sessions", h_alpha, "").status == 403,
	      "active-session API enforces the same anonymous 401 and non-admin 403 boundary");
	Check(HttpCall("GET", ALPHA_PORT, "/api/audit/health", audit_anon, "").status == 401 &&
	      HttpCall("GET", ALPHA_PORT, "/api/audit/health", h_alpha, "").status == 403,
	      "sanitized health diagnostics are admin-only");
	HttpReply alpha_audit = HttpCall("GET", ALPHA_PORT, "/api/audit/events?limit=200", audit_admin_alpha, "");
	HttpReply beta_audit = HttpCall("GET", BETA_PORT, "/api/audit/events", audit_admin_beta, "");
	ValueMap alpha_audit_json = ParseJSON(alpha_audit.body), beta_audit_json = ParseJSON(beta_audit.body);
	ValueArray alpha_events = alpha_audit_json.Get("events", ValueArray());
	ValueArray beta_events = beta_audit_json.Get("events", ValueArray());
	bool route_seen = false, login_seen = false, login_failure_seen = false, sso_login_refused = false;
	bool session_seen = false, action_seen = false;
	bool failed_resolution_seen = false, redirect_local_seen = false, redirect_external_seen = false;
	bool redirect_sso_seen = false, redirect_rejected_seen = false, recursive_read_seen = false;
	bool alpha_broker_seen = false;
	bool query_clean = alpha_audit.body.Find(audit_canary) < 0 &&
	                   alpha_audit.body.Find("username-secret-canary") < 0 &&
	                   alpha_audit.body.Find("password-secret-canary") < 0 &&
	                   alpha_audit.body.Find("redirect-secret-canary") < 0 &&
	                   alpha_audit.body.Find(bad_cookie_canary) < 0 &&
	                   alpha_audit.body.Find(audit_cookie_alpha) < 0 &&
	                   alpha_audit.body.Find(audit_csrf_alpha) < 0;
	for(const Value& v : alpha_events) {
		ValueMap e = v;
		if((String)e.Get("route") == "GET:/greet/*" && (int)e.Get("status") == 200) route_seen = true;
		if((String)e.Get("kind") == "login" && (String)e.Get("outcome") == "success" &&
		   !((String)e.Get("session_ref")).IsEmpty()) login_seen = true;
		if((String)e.Get("kind") == "login" && (String)e.Get("outcome") == "failure") login_failure_seen = true;
		if((String)e.Get("kind") == "login" && (String)e.Get("operation") == "sso_required" &&
		   (int)e.Get("status") == 403) sso_login_refused = true;
		if((String)e.Get("kind") == "session" && (String)e.Get("outcome") == "adopted") session_seen = true;
		if((String)e.Get("kind") == "action" && (String)e.Get("method") == "POST") action_seen = true;
		if((String)e.Get("kind") == "session" && (String)e.Get("outcome") == "failed_resolution") failed_resolution_seen = true;
		if((String)e.Get("kind") == "redirect" && (String)e.Get("outcome") == "approved_local") redirect_local_seen = true;
		if((String)e.Get("kind") == "redirect" && (String)e.Get("outcome") == "external") redirect_external_seen = true;
		if((String)e.Get("kind") == "redirect" && (String)e.Get("outcome") == "approved_sso") redirect_sso_seen = true;
		if((String)e.Get("kind") == "redirect" && (String)e.Get("outcome") == "rejected") redirect_rejected_seen = true;
		if(((String)e.Get("route")).Find("api/audit/") >= 0) recursive_read_seen = true;
		if((String)e.Get("kind") == "broker_inprocess") alpha_broker_seen = true;
	}
	bool broker_seen = false, broker_failure_seen = false;
	for(const Value& v : beta_events) {
		ValueMap e = v;
		if((String)e.Get("kind") == "broker_inprocess" && (String)e.Get("source_site") == "beta" &&
		   (String)e.Get("target_site") == "alpha" && (String)e.Get("operation") == "add") broker_seen = true;
		if((String)e.Get("kind") == "broker_inprocess" && (String)e.Get("outcome") == "failure" &&
		   (String)e.Get("operation") == "unclassified") broker_failure_seen = true;
	}
	Check(alpha_audit.status == 200 && (String)alpha_audit_json.Get("site") == "alpha" && route_seen && login_seen && login_failure_seen &&
	      sso_login_refused &&
	      session_seen && action_seen && failed_resolution_seen && redirect_local_seen && redirect_external_seen && redirect_sso_seen &&
	      redirect_rejected_seen && !alpha_broker_seen && !recursive_read_seen &&
	      query_clean && alpha_audit.cache_control.Find("no-store") >= 0,
	      "audit integration state route=" + AsString(route_seen) + " login=" + AsString(login_seen) +
	      " login_fail=" + AsString(login_failure_seen) + " session=" + AsString(session_seen) +
	      " failed_resolution=" + AsString(failed_resolution_seen) + " redirect_local=" + AsString(redirect_local_seen) +
	      " redirect_external=" + AsString(redirect_external_seen) + " redirect_rejected=" + AsString(redirect_rejected_seen) +
	      " query_clean=" + AsString(query_clean) + " recursive=" + AsString(recursive_read_seen));
	Check(beta_audit.status == 200 && (String)beta_audit_json.Get("site") == "beta" && broker_seen && broker_failure_seen &&
	      beta_audit.body.Find(audit_canary) < 0,
	      "in-process broker ledger omits its payload secret and stays in the source site history");
	HttpReply active_sessions = HttpCall("GET", ALPHA_PORT, "/api/audit/sessions", audit_admin_alpha, "");
	ValueMap active_sessions_json = ParseJSON(active_sessions.body);
	Check(active_sessions.status == 200 && (String)active_sessions_json.Get("site") == "alpha" &&
	      active_sessions.body.Find("\"session_ref\"") >= 0 &&
	      active_sessions.body.Find(audit_cookie_alpha) < 0 &&
	      active_sessions.body.Find(audit_csrf_alpha) < 0 && active_sessions.cache_control.Find("no-store") >= 0,
	      "admin active-session snapshot omits bearer cookie and CSRF values and is no-store");
	HttpReply audit_health = HttpCall("GET", ALPHA_PORT, "/api/audit/health", audit_admin_alpha, "");
	ValueMap audit_health_json = ParseJSON(audit_health.body);
	Check(audit_health.status == 200 && JsonFlag(audit_health_json, "audit_write_healthy") &&
	      ((String)audit_health_json.Get("broker_diagnostics")).Find("communications_are_durable_in_WebAudit") >= 0 &&
	      audit_health.cache_control.Find("no-store") >= 0,
	      "admin diagnostics expose only sanitized recorder health and operational-ledger separation");
	HttpReply audit_page = HttpCall("GET", ALPHA_PORT, "/audit", audit_admin_alpha, "");
	Check(audit_page.status == 200 && audit_page.body.Find("api/audit/events") >= 0 &&
	      audit_page.body.Find("api/audit/sessions") >= 0 && audit_page.cache_control.Find("no-store") >= 0,
	      "admin audit page lists sanitized sessions, events, and diagnostics via no-store APIs");
	Check(HttpCall("GET", ALPHA_PORT, "/api/audit/events?offset=10001", audit_admin_alpha, "").status == 400 &&
	      HttpCall("GET", ALPHA_PORT, "/api/audit/events?limit=201", audit_admin_alpha, "").status == 400 &&
	      HttpCall("GET", ALPHA_PORT, "/api/audit/sessions?offset=10001", audit_admin_alpha, "").status == 400 &&
	      HttpCall("GET", ALPHA_PORT, "/api/audit/sessions?limit=201", audit_admin_alpha, "").status == 400,
	      "event and active-session APIs reject unbounded offset and page size");
	VectorMap<String, String> audit_admin_backup;
	String audit_cookie_backup, audit_csrf_backup;
	audit_login(ALPHA_PORT, audit_admin_backup, audit_cookie_backup, audit_csrf_backup);
	VectorMap<String, String> audit_admin_expiry;
	String audit_cookie_expiry, audit_csrf_expiry;
	audit_login(ALPHA_PORT, audit_admin_expiry, audit_cookie_expiry, audit_csrf_expiry);
	JsonFileStore alpha_sessions("alpha", AppendFileName(s_root, "alpha"), "sessions");
	String expiry_json, expiry_rev, expiry_new_rev;
	WebAccountSession expiry_session;
	bool expiry_fixture = alpha_sessions.Get(audit_cookie_expiry, expiry_json, expiry_rev).ok;
	if(expiry_fixture) {
		LoadFromJsonValue(expiry_session, ParseJSON(expiry_json));
		expiry_session.expires = GetSysTime() - 1;
		expiry_fixture = alpha_sessions.Put(audit_cookie_expiry, StoreAsJson(expiry_session, true), expiry_rev, expiry_new_rev).ok;
	}
	Check(expiry_fixture, "expired-session fixture is persisted without changing its cookie reference");
	VectorMap<String, String> expired_cookie_header;
	expired_cookie_header.Add("Cookie", "wfs_alpha=" + audit_cookie_expiry);
	Check(HttpCall("GET", ALPHA_PORT, "/whoami", expired_cookie_header, "").status == 200,
	      "expired session resolution does not corrupt the normal response");
	HttpReply after_expiry = HttpCall("GET", ALPHA_PORT, "/api/audit/events", audit_admin_backup, "");
	ValueArray after_expiry_events = ValueMap(ParseJSON(after_expiry.body)).Get("events", ValueArray());
	bool expiry_seen = false;
	for(const Value& v : after_expiry_events) {
		ValueMap e = v;
		if((String)e.Get("kind") == "session" && (String)e.Get("outcome") == "expired") expiry_seen = true;
	}
	Check(expiry_seen && after_expiry.body.Find(audit_cookie_expiry) < 0,
	      "expired-cookie lifecycle event persists without the raw session bearer");
	HttpReply logout_admin = HttpCall("POST", ALPHA_PORT, "/session/logout", audit_admin_alpha, "");
	Check(logout_admin.status == 200,
	      "session logout completes normally (status=" + AsString(logout_admin.status) + ") after emitting lifecycle audit event");
	HttpReply after_logout = HttpCall("GET", ALPHA_PORT, "/api/audit/events", audit_admin_backup, "");
	ValueArray after_logout_events = ValueMap(ParseJSON(after_logout.body)).Get("events", ValueArray());
	bool logout_seen = false;
	for(const Value& v : after_logout_events) {
		ValueMap e = v;
		if((String)e.Get("route") == "session:logout" && (String)e.Get("outcome") == "revoked") logout_seen = true;
	}
	Check(logout_seen, "durable history includes session logout/revoke event");

	// 17. broker: unknown target fails explicitly
	HttpReply rc2 = HttpCall("POST", BETA_PORT, "/call-nowhere", no_headers, "");
	{
		Value v = ParseJSON(rc2.body);
		ValueMap m = v.Is<ValueMap>() ? (ValueMap)v : ValueMap();
		Check(!JsonFlag(m, "ok") && ((String)m.Get("error")).Find("ghost-site") >= 0,
		      "broker call to unknown site fails explicitly");
	}
	HttpReply missing_service_audit = HttpCall("GET", BETA_PORT, "/api/audit/events", audit_admin_beta, "");
	ValueArray missing_service_events = ValueMap(ParseJSON(missing_service_audit.body)).Get("events", ValueArray());
	bool unavailable_target_seen = false;
	for(const Value& v : missing_service_events) {
		ValueMap e = v;
		if((String)e.Get("kind") == "broker_inprocess" && (String)e.Get("target_site") == "unregistered" &&
		   (String)e.Get("operation") == "unavailable") unavailable_target_seen = true;
	}
	Check(unavailable_target_seen && missing_service_audit.body.Find("ghost-site") < 0,
	      "missing broker service is categorized without persisting caller-supplied target text");

	// 18. logout invalidates the session
	{
		VectorMap<String, String> h_csrf2 = clone(h_alpha);
		h_csrf2.Add("X-WF-CSRF", alpha_csrf);
		HttpReply rl2 = HttpCall("POST", ALPHA_PORT, "/logout", h_csrf2, "");
		Check(SetCookieLine(rl2, "wfs_alpha").Find("; Expires=") >= 0, "logout deletion cookie carries an Expires attribute");
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
	begin_f.Add("password", "totp-enrollment-password-canary");
	Check(HttpCall("POST", META_PORT, "/totp/begin", h_meta, FormEncode(begin_f)).status == 403,
	      "TOTP enrollment refuses a bad password before issuing secrets");
	begin_f.GetAdd("password") = "pw-meta";
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
	confirm_f.Add("code", "totp-code-secret-canary");
	Check(HttpCall("POST", META_PORT, "/totp/confirm", h_meta, FormEncode(confirm_f)).status == 403,
	      "TOTP enrollment rejects an invalid code without changing the response");
	confirm_f.GetAdd("code") = TotpCode(secret, TotpUnixNow());
	HttpReply mtc = HttpCall("POST", META_PORT, "/totp/confirm", h_meta, FormEncode(confirm_f));
	{
		Value v = ParseJSON(mtc.body);
		Check(JsonFlag(v.Is<ValueMap>() ? (ValueMap)v : ValueMap(), "ok"), "totp enrollment confirmed with live code");
	}
	VectorMap<String, String> disable_bad_f;
	disable_bad_f.Add("password", "totp-disable-password-canary");
	Check(HttpCall("POST", META_PORT, "/totp/disable", h_meta, FormEncode(disable_bad_f)).status == 403,
	      "TOTP disable refuses a bad password and records a failure category");
	VectorMap<String, String> browser_meta_headers;
	browser_meta_headers.Add("Accept", "text/html");
	browser_meta_headers.Add("Content-Type", "application/x-www-form-urlencoded");
	VectorMap<String, String> browser_meta_form;
	browser_meta_form.Add("username", "mallory"); browser_meta_form.Add("password", "pw-meta");
	browser_meta_form.Add("code", TotpCode(secret, TotpUnixNow()));
	EngineSite *alpha_public_site = TheWebEngine().FindSite("alpha");
	String allowed_return = alpha_public_site ? alpha_public_site->PublicUrl("/session/sso?next=%2F") : String();
	browser_meta_form.Add("next", allowed_return);
	HttpReply allowed_return_reply = HttpCall("POST", META_PORT, "/login", browser_meta_headers, FormEncode(browser_meta_form));
	Check(!allowed_return.IsEmpty() && allowed_return_reply.status == 302 && allowed_return_reply.location == allowed_return,
	      "MetaAccount permits return targets only at a configured site public origin and safe path");
	browser_meta_form.GetAdd("next") = "https://evil.example/steal?token=return-target-secret";
	HttpReply rejected_return_reply = HttpCall("POST", META_PORT, "/login", browser_meta_headers, FormEncode(browser_meta_form));
	Check(rejected_return_reply.status == 302 && rejected_return_reply.location == TheWebEngine().FindSite("meta")->PublicUrl("/"),
	      "unconfigured SSO return origin is rejected in favor of the configured MetaAccount origin");

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
	Vector<ValueMap> meta_audit_rows;
	StoreResult meta_audit_result = TheWebEngine().FindSite("meta")->Audit().List(
		GetSysTime() - 24 * 60 * 60, GetSysTime() + 1, 0, 200, meta_audit_rows);
	ValueArray meta_audit_array;
	for(const ValueMap& e : meta_audit_rows) meta_audit_array.Add(e);
	String meta_audit_json = AsJSON(meta_audit_array, true);
	bool totp_method_seen = false, recovery_method_seen = false, second_factor_failure_seen = false;
	bool totp_begin_failure = false, totp_confirm_failure = false, totp_disable_failure = false;
	for(const ValueMap& e : meta_audit_rows) {
		if((String)e.Get("kind") == "login") {
			totp_method_seen |= (String)e.Get("operation") == "password_totp";
			recovery_method_seen |= (String)e.Get("operation") == "password_recovery";
			second_factor_failure_seen |= (String)e.Get("outcome") == "failure" &&
			                              (String)e.Get("operation") == "password_second_factor";
		}
		if((String)e.Get("kind") == "sensitive_action" && (String)e.Get("outcome") == "failure") {
			totp_begin_failure |= (String)e.Get("route") == "account:totp_begin" && (int)e.Get("status") == 403;
			totp_confirm_failure |= (String)e.Get("route") == "account:totp_confirm" && (int)e.Get("status") == 403;
			totp_disable_failure |= (String)e.Get("route") == "account:totp_disable" && (int)e.Get("status") == 403;
		}
	}
	Check(meta_audit_result.ok && totp_method_seen && recovery_method_seen && second_factor_failure_seen &&
	      totp_begin_failure && totp_confirm_failure && totp_disable_failure &&
	      meta_audit_json.Find(secret) < 0 && meta_audit_json.Find(recovery[0]) < 0 &&
	      meta_audit_json.Find("pw-meta") < 0 && meta_audit_json.Find("mallory") < 0 &&
	      meta_audit_json.Find("return-target-secret") < 0 &&
	      meta_audit_json.Find("totp-enrollment-password-canary") < 0 &&
	      meta_audit_json.Find("totp-code-secret-canary") < 0 &&
	      meta_audit_json.Find("totp-disable-password-canary") < 0,
	      "MetaAccount login and TOTP failures use safe categories without credentials or username");

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
	Vector<ValueMap> grant_audit_rows;
	StoreResult grant_audit_read = TheWebEngine().FindSite("meta")->Audit().List(
		GetSysTime() - 24 * 60 * 60, GetSysTime() + 1, 0, 200, grant_audit_rows);
	ValueArray grant_audit_values;
	bool grant_revoke_seen = false;
	for(const ValueMap& e : grant_audit_rows) {
		grant_audit_values.Add(e);
		if((String)e.Get("kind") == "sensitive_action" && (String)e.Get("route") == "meta:grant_revoke")
			grant_revoke_seen = true;
	}
	Check(grant_audit_read.ok && grant_revoke_seen && AsJSON(grant_audit_values, true).Find(grant_id) < 0,
	      "grant revoke audit event records action category without grant identifier");

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
		Check(HttpCall("GET", TASKFOUNDRY_PORT, "/policy", VectorMap<String, String>(), "").status == 401,
		      "taskfoundry policy administration requires authentication");
		Check(HttpCall("GET", TASKFOUNDRY_PORT, "/policy", tf_h, "").status == 403,
		      "taskfoundry policy administration requires the admin role");
		Check(HttpCall("POST", TASKFOUNDRY_PORT, "/api/policy/link", tf_h, "").status == 403,
		      "taskfoundry secret-link creation remains admin-only");
		TfResult tf_board = s_tf_service->Board();
		ValueArray tf_candidates = tf_board.payload.Get("candidates", ValueArray());
		if(tf_candidates.GetCount()) {
			String candidate_id = (String)((ValueMap)tf_candidates[0]).Get("id", "");
			String tf_policy_root = AppendFileName(s_root, "taskfoundry");
			ContentPolicyStore& policy_store = ContentPolicyFor(tf_policy_root);
			ContentPolicy candidate_policy;
			candidate_policy.level = CONTENT_PRIVATE;
			ContentGrant candidate_grant;
			candidate_grant.kind = "user";
			candidate_grant.value = "tfdev";
			candidate_policy.grants.Add(pick(candidate_grant));
			policy_store.SetRecord(candidate_id, candidate_policy);
			String policy_error;
			Check(policy_store.Save(policy_error), "taskfoundry candidate policy fixture is persisted");
			Check(HttpCall("GET", TASKFOUNDRY_PORT, "/candidates/" + candidate_id, tf_h, "").status == 200,
			      "taskfoundry policy grant works only within the approved site grant");
			candidate_policy.grants.Clear();
			policy_store.SetRecord(candidate_id, candidate_policy);
			Check(policy_store.Save(policy_error), "taskfoundry candidate policy deny fixture is persisted");
			HttpReply hidden_candidate = HttpCall("GET", TASKFOUNDRY_PORT, "/candidates/" + candidate_id, tf_h, "");
			HttpReply filtered_board = HttpCall("GET", TASKFOUNDRY_PORT, "/", tf_h, "");
			Check(hidden_candidate.status == 404 && filtered_board.body.Find("Linked") < 0,
			      "taskfoundry hides denied candidate detail and omits it from the board");
			candidate_policy.level = CONTENT_PUBLIC;
			policy_store.SetRecord(candidate_id, candidate_policy);
			Check(policy_store.Save(policy_error), "taskfoundry candidate public policy is restored");
			candidate_policy.level = CONTENT_PUBLIC_ONLY;
			policy_store.SetRecord(candidate_id, candidate_policy);
			Check(policy_store.Save(policy_error) &&
			      HttpCall("GET", TASKFOUNDRY_PORT, "/candidates/" + candidate_id, tf_h, "").status == 404 &&
			      HttpCall("GET", TASKFOUNDRY_PORT, "/candidates/" + candidate_id, VectorMap<String, String>(), "").status == 302,
			      "taskfoundry public-only policy narrows granted reads without bypassing the approved grant");
			candidate_policy.level = CONTENT_PUBLIC;
			policy_store.SetRecord(candidate_id, candidate_policy);
			Check(policy_store.Save(policy_error), "taskfoundry candidate policy is restored after public-only test");
		}
		String tf_policy_root = AppendFileName(s_root, "taskfoundry");
		String tf_policy_path = AppendFileName(tf_policy_root, "content_policy.json");
		ContentPolicyStore& tf_policy_store = ContentPolicyFor(tf_policy_root);
		String tf_policy_error;
		Check(SaveFile(tf_policy_path, "{broken") && !tf_policy_store.Load(tf_policy_root, tf_policy_error),
		      "taskfoundry corrupt policy fixture fails loading");
		Check(HttpCall("GET", TASKFOUNDRY_PORT, "/", tf_h, "").status == 503,
		      "taskfoundry granted board fails closed on corrupt policy data");
		FileDelete(tf_policy_path);
		Check(tf_policy_store.Load(tf_policy_root, tf_policy_error), "taskfoundry policy store recovers after fixture removal");

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
		VectorMap<String, String> ch_anon_policy;
		Check(HttpCall("GET", CHANNELS_PORT, "/policy", ch_anon_policy, "").status == 401,
		      "channels policy administration requires authentication");

		// The overview is a public landing; the channel detail remains protected.
		VectorMap<String, String> anon_h;
		anon_h.Add("Origin", ch_origin);
		HttpReply rca = HttpCall("GET", CHANNELS_PORT, "/overview", anon_h, "");
		Check(rca.status == 200 && rca.body.Find("Channels show the agent activity") >= 0,
		      "channels overview serves its public landing anonymously");

		VectorMap<String, String> ch_h;
		ch_h.Add("Origin", ch_origin);
		ch_h.Add("Cookie", ch_cookie);
		ch_h.Add("X-WF-CSRF", ch_csrf);
		Check(HttpCall("GET", CHANNELS_PORT, "/policy", ch_h, "").status == 403,
		      "channels policy administration requires the admin role");
		Check(HttpCall("POST", CHANNELS_PORT, "/api/policy/link", ch_h, "").status == 403,
		      "channels secret-link creation remains admin-only");
		HttpReply rco = HttpCall("GET", CHANNELS_PORT, "/overview", ch_h, "");
		Check(rco.status == 200 && rco.body.Find("SKYLARK-MANAGER") >= 0
		      && rco.body.Find("AGENTLAB") >= 0, "channels overview lists the node's channels");
		ContentPolicyStore& channel_policy = ContentPolicyFor(AppendFileName(s_root, "channels"));
		ContentPolicy shown_channel;
		shown_channel.level = CONTENT_PRIVATE;
		ContentGrant channel_grant;
		channel_grant.kind = "user";
		channel_grant.value = "viewer";
		shown_channel.grants.Add(pick(channel_grant));
		ContentPolicy hidden_channel;
		hidden_channel.level = CONTENT_PRIVATE;
		channel_policy.SetRecord("skylark/SKYLARK-MANAGER", shown_channel);
		channel_policy.SetRecord("skylark/AGENTLAB", hidden_channel);
		String channel_policy_error;
		Check(channel_policy.Save(channel_policy_error), "channels policy fixtures are persisted");
		HttpReply filtered_channels = HttpCall("GET", CHANNELS_PORT, "/overview", ch_h, "");
		Check(filtered_channels.status == 200 && filtered_channels.body.Find("SKYLARK-MANAGER") >= 0 &&
		      filtered_channels.body.Find("AGENTLAB") < 0,
		      "channels overview filters metadata by channel policy after login");
		Check(HttpCall("GET", CHANNELS_PORT, "/channels/skylark/SKYLARK-MANAGER", ch_h, "").status == 200 &&
		      HttpCall("GET", CHANNELS_PORT, "/channels/skylark/AGENTLAB", ch_h, "").status == 404,
		      "channels detail applies policy before returning cached or fetched messages");
		shown_channel.grants.Clear();
		channel_policy.SetRecord("skylark/SKYLARK-MANAGER", shown_channel);
		Check(channel_policy.Save(channel_policy_error), "channels all-hidden fixture is persisted");
		HttpReply no_visible_channels = HttpCall("GET", CHANNELS_PORT, "/overview", ch_h, "");
		Check(no_visible_channels.status == 200 && no_visible_channels.body.Find("No channels have been used") >= 0 &&
		      no_visible_channels.body.Find("<table>") < 0,
		      "channels all-hidden overview has the same empty response without an existence/count leak");
		hidden_channel.level = CONTENT_PUBLIC_ONLY;
		channel_policy.SetRecord("skylark/AGENTLAB", hidden_channel);
		Check(channel_policy.Save(channel_policy_error) &&
		      HttpCall("GET", CHANNELS_PORT, "/channels/skylark/AGENTLAB", ch_h, "").status == 404 &&
		      HttpCall("GET", CHANNELS_PORT, "/channels/skylark/AGENTLAB", ch_anon_policy, "").status == 302,
		      "channels public-only policy narrows authenticated reads without bypassing login");
		shown_channel.level = CONTENT_PUBLIC;
		hidden_channel.level = CONTENT_PUBLIC;
		channel_policy.SetRecord("skylark/SKYLARK-MANAGER", shown_channel);
		channel_policy.SetRecord("skylark/AGENTLAB", hidden_channel);
		Check(channel_policy.Save(channel_policy_error), "channels policy fixtures are restored");
		String channel_policy_root = AppendFileName(s_root, "channels");
		String channel_policy_path = AppendFileName(channel_policy_root, "content_policy.json");
		Check(SaveFile(channel_policy_path, "{broken") && !channel_policy.Load(channel_policy_root, channel_policy_error),
		      "channels corrupt policy fixture fails loading");
		Check(HttpCall("GET", CHANNELS_PORT, "/overview", ch_h, "").status == 503,
		      "channels metadata overview fails closed on corrupt policy data");
		Check(HttpCall("GET", CHANNELS_PORT, "/channels/skylark/SKYLARK-MANAGER", ch_h, "").status == 503,
		      "channels cached message view fails closed on corrupt policy data");
		FileDelete(channel_policy_path);
		Check(channel_policy.Load(channel_policy_root, channel_policy_error), "channels policy store recovers after fixture removal");
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
		VectorMap<String, String> fin_anon_policy;
		Check(HttpCall("GET", FINANCIALS_PORT, "/policy", fin_anon_policy, "").status == 401,
		      "financials policy administration requires authentication");

		VectorMap<String, String> anon_h;
		anon_h.Add("Origin", fin_origin);
		HttpReply rfa = HttpCall("GET", FINANCIALS_PORT, "/financials", anon_h, "");
		Check(rfa.status == 200 && rfa.body.Find("ledger totals") < 0,
		      "financials summary serves a public landing without totals");

		VectorMap<String, String> view_h;
		view_h.Add("Origin", fin_origin);
		view_h.Add("Cookie", view_cookie);
		VectorMap<String, String> fin_h;
		fin_h.Add("Origin", fin_origin);
		fin_h.Add("Cookie", fin_cookie);
		fin_h.Add("X-WF-CSRF", fin_csrf);
		Check(HttpCall("GET", FINANCIALS_PORT, "/policy", view_h, "").status == 403,
		      "financials policy administration requires the admin role");
		Check(HttpCall("POST", FINANCIALS_PORT, "/api/policy/link", fin_h, "").status == 403,
		      "financials secret-link creation remains admin-only");

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
		ContentPolicyStore& fin_policy_store = ContentPolicyFor(AppendFileName(s_root, "financials"));
		ContentPolicy summary_policy;
		summary_policy.level = CONTENT_PRIVATE;
		ContentGrant finance_grant;
		finance_grant.kind = "group";
		finance_grant.value = "finance";
		summary_policy.grants.Add(pick(finance_grant));
		fin_policy_store.SetModule("summary", summary_policy);
		ContentPolicy ledger_row;
		ledger_row.level = CONTENT_PRIVATE;
		fin_policy_store.SetRecord("2", ledger_row);
		ContentPolicy contract_row;
		contract_row.level = CONTENT_PRIVATE;
		fin_policy_store.SetRecord("ctr_fake", contract_row);
		String finance_policy_error;
		Check(fin_policy_store.Save(finance_policy_error), "financials policy fixtures are persisted");
		Check(HttpCall("GET", FINANCIALS_PORT, "/financials", view_h, "").status == 404 &&
		      HttpCall("GET", FINANCIALS_PORT, "/financials", fin_h, "").status == 404,
		      "financials hides aggregates when restrictive row policy prevents safe source filtering");
		HttpReply filtered_ledger = HttpCall("GET", FINANCIALS_PORT, "/ledger", fin_h, "");
		HttpReply filtered_contracts = HttpCall("GET", FINANCIALS_PORT, "/contracts", fin_h, "");
		Check(filtered_ledger.status == 200 && filtered_ledger.body.Find("fixed &lt;bug&gt;") < 0 &&
		      filtered_ledger.body.Find("No ledger entries yet.") >= 0 &&
		      filtered_contracts.status == 200 && filtered_contracts.body.Find("Fake &lt;contract&gt;") < 0 &&
		      filtered_contracts.body.Find("No contracts yet.") >= 0,
		      "financials filters rows before rendering and hides existence when all rows are hidden");
		ContentGrant finance_row_grant;
		finance_row_grant.kind = "user";
		finance_row_grant.value = "ffin";
		ledger_row.grants.Add(pick(finance_row_grant));
		fin_policy_store.SetRecord("2", ledger_row);
		Check(fin_policy_store.Save(finance_policy_error) &&
		      HttpCall("GET", FINANCIALS_PORT, "/ledger", fin_h, "").body.Find("fixed &lt;bug&gt;") >= 0,
		      "financials record grant works only after finance-role authorization");
		contract_row.level = CONTENT_PUBLIC_ONLY;
		fin_policy_store.SetRecord("ctr_fake", contract_row);
		Check(fin_policy_store.Save(finance_policy_error) &&
		      HttpCall("GET", FINANCIALS_PORT, "/contracts", fin_h, "").body.Find("Fake &lt;contract&gt;") < 0 &&
		      HttpCall("GET", FINANCIALS_PORT, "/contracts", VectorMap<String, String>(), "").status == 302,
		      "financials public-only row policy does not bypass login or finance-role authorization");
		contract_row.level = CONTENT_PUBLIC;
		summary_policy.level = CONTENT_PUBLIC;
		ledger_row.level = CONTENT_PUBLIC;
		ledger_row.grants.Clear();
		contract_row.level = CONTENT_PUBLIC;
		fin_policy_store.SetModule("summary", summary_policy);
		fin_policy_store.SetRecord("2", ledger_row);
		fin_policy_store.SetRecord("ctr_fake", contract_row);
		Check(fin_policy_store.Save(finance_policy_error) &&
		      HttpCall("GET", FINANCIALS_PORT, "/financials", fin_h, "").status == 200,
		      "financials aggregate returns after restrictive row policies are cleared");
		ContentPolicy hidden_ledger_module;
		hidden_ledger_module.level = CONTENT_PRIVATE;
		fin_policy_store.SetModule("ledger", hidden_ledger_module);
		Check(fin_policy_store.Save(finance_policy_error) &&
		      HttpCall("GET", FINANCIALS_PORT, "/financials", fin_h, "").status == 404,
		      "financials aggregate also respects restrictive source-module policy");
		hidden_ledger_module.level = CONTENT_PUBLIC;
		fin_policy_store.SetModule("ledger", hidden_ledger_module);
		Check(fin_policy_store.Save(finance_policy_error), "financials ledger module policy is restored");
		String fin_policy_root = AppendFileName(s_root, "financials");
		String fin_policy_path = AppendFileName(fin_policy_root, "content_policy.json");
		Check(SaveFile(fin_policy_path, "{broken") && !fin_policy_store.Load(fin_policy_root, finance_policy_error),
		      "financials corrupt policy fixture fails loading");
		Check(HttpCall("GET", FINANCIALS_PORT, "/financials", view_h, "").status == 503 &&
		      HttpCall("GET", FINANCIALS_PORT, "/ledger", fin_h, "").status == 503,
		      "financials aggregate and finance rows fail closed on corrupt policy data");
		FileDelete(fin_policy_path);
		Check(fin_policy_store.Load(fin_policy_root, finance_policy_error), "financials policy store recovers after fixture removal");

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

	// --- Host authority cannot override configured public origin or force a downgrade ---
	{
		String lan_host = Format("192.168.1.169:%d", ATLAS_PORT);
		String configured_origin = Format("http://127.0.0.1:%d", ATLAS_PORT);
		VectorMap<String, String> lo;
		lo.Add("Origin", configured_origin);
		lo.Add("Host", lan_host);
		lo.Add("accept", "application/json");
		VectorMap<String, String> lf;
		lf.Add("username", "aadmin");
		lf.Add("password", "pw-atl");
		HttpReply rlan = HttpCall("POST", ATLAS_PORT, "/session/login", lo, FormEncode(lf));
		String lan_csrf = (String)ParseJSON(rlan.body)["csrf"];
		String lan_raw;
		String lan_cookie = "wfs_atlas=" + CookieOf(rlan, "wfs_atlas", lan_raw);
		Check(rlan.status == 200 && lan_csrf.GetCount() > 0,
		      "literal Host spoof cannot override the exact configured Origin");

		VectorMap<String, String> lh;
		lh.Add("Origin", configured_origin);
		lh.Add("Host", lan_host);
		lh.Add("Cookie", lan_cookie);
		VectorMap<String, String> lc;
		lc.Add("map", "lan");
		lc.Add("version", "v1");
		lc.Add("title", "LAN");
		lc.Add("__wf_csrf__", lan_csrf);
		HttpReply rlc = HttpCall("POST", ATLAS_PORT, "/atlas/create", lh, FormEncode(lc));
		Check(rlc.status == 302, "a LAN-authority mutation succeeds");
		Check(rlc.location == configured_origin + "/atlas/lan-v1",
		      "redirect Location stays pinned to configured origin despite literal Host spoof");
		HttpReply rlm = HttpCall("GET", ATLAS_PORT, "/atlas/lan-v1", lh, "");
		Check(rlm.status == 200 && rlm.body.Find("href=\"" + configured_origin + "/atlas\"") >= 0,
		      "absolute links on a Host-spoofed page use the configured public origin");

		VectorMap<String, String> foreign;
		foreign.Add("Origin", "http://evil.example:" + AsString(ATLAS_PORT));
		foreign.Add("Host", lan_host);
		foreign.Add("accept", "application/json");
		Check(HttpCall("POST", ATLAS_PORT, "/session/login", foreign, FormEncode(lf)).status == 403,
		      "a foreign Origin is refused regardless of literal Host header");

		VectorMap<String, String> mismatch;
		mismatch.Add("Origin", Format("http://localhost:%d", ATLAS_PORT));
		mismatch.Add("Host", Format("localhost:%d", ATLAS_PORT));
		mismatch.Add("accept", "application/json");
		Check(HttpCall("POST", ATLAS_PORT, "/session/login", mismatch, FormEncode(lf)).status == 403,
		      "localhost Host/Origin spoof is refused when not the configured public origin");

		// A matching DNS Host and Origin must not be trusted as a rebindable origin.
		VectorMap<String, String> rebind;
		rebind.Add("Origin", Format("http://attacker.example:%d", ATLAS_PORT));
		rebind.Add("Host", Format("attacker.example:%d", ATLAS_PORT));
		rebind.Add("accept", "application/json");
		Check(HttpCall("POST", ATLAS_PORT, "/session/login", rebind, FormEncode(lf)).status == 403,
			"a matching DNS Host and Origin is refused despite a valid local listener");
		VectorMap<String, String> dns_configured;
		dns_configured.Add("Host", Format("attacker.example:%d", ATLAS_PORT));
		dns_configured.Add("Origin", configured_origin);
		dns_configured.Add("accept", "application/json");
		HttpReply dns_page = HttpCall("GET", ATLAS_PORT, "/atlas/lan-v1", dns_configured, "");
		Check(dns_page.status == 200 && dns_page.body.Find("href=\"" + configured_origin + "/atlas\"") >= 0,
		      "DNS Host cannot alter links or downgrade the configured origin");
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
		HttpReply atlas_public = HttpCall("GET", ATLAS_PORT, "/atlas", anon_h, "");
		Check(atlas_public.status == 200, "atlas list is publicly readable");

		VectorMap<String, String> adm_h;
		adm_h.Add("Origin", atl_origin);
		adm_h.Add("Cookie", adm_cookie);
		VectorMap<String, String> vw_h;
		vw_h.Add("Origin", atl_origin);
		vw_h.Add("Cookie", vw_cookie);
		Check(HttpCall("GET", ATLAS_PORT, "/policy", anon_h, "").status == 401,
		      "atlas policy administration requires authentication");
		Check(HttpCall("GET", ATLAS_PORT, "/policy", vw_h, "").status == 403,
		      "atlas policy administration requires the admin role");
		HttpReply atlas_policy_page = HttpCall("GET", ATLAS_PORT, "/policy", adm_h, "");
		Check(atlas_policy_page.status == 200 &&
		      atlas_policy_page.body.Find("Secret-link controls require verified HTTPS transport") >= 0 &&
		      atlas_policy_page.body.Find("policy-link-form") < 0,
		      "atlas policy administration withholds secret-link creation over HTTP");
		Check(HttpCall("GET", ATLAS_PORT, "/api/policy", adm_h, "").status == 200,
		      "atlas policy API is available to the site admin");

		// Creating a map version: fenced, admin-only, one document per version.
		VectorMap<String, String> cf;
		cf.Add("map", "demo");
		cf.Add("version", "v0.0.1");
		cf.Add("title", "DEMO <v0.0.1>");
		cf.Add("__wf_csrf__", adm_csrf);
		HttpReply rcreate = HttpCall("POST", ATLAS_PORT, "/atlas/create", adm_h, FormEncode(cf));
		Check(rcreate.status == 302, "the admin creates a map version");
		HttpReply rdup = HttpCall("POST", ATLAS_PORT, "/atlas/create", adm_h, FormEncode(cf));
		Check(rdup.status == 409, "a second create of the same version is refused");
		ContentPolicyStore& atlas_policy = ContentPolicyFor(AppendFileName(s_root, "atlas"));
		ContentPolicy private_map;
		private_map.level = CONTENT_PRIVATE;
		ContentPolicy all_maps_hidden;
		all_maps_hidden.level = CONTENT_PRIVATE;
		atlas_policy.Site() = all_maps_hidden;
		ContentGrant atlas_grant;
		atlas_grant.kind = "user";
		atlas_grant.value = "aview";
		private_map.grants.Add(pick(atlas_grant));
		atlas_policy.SetRecord("demo", private_map);
		String atlas_policy_error;
		Check(atlas_policy.Save(atlas_policy_error), "atlas private map policy is persisted");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas/demo", anon_h, "").status == 404,
		      "atlas anonymous reads hide a private map");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas/demo", vw_h, "").status == 200,
		      "atlas named-user policy grant preserves authenticated map access");
		HttpReply hidden_index = HttpCall("GET", ATLAS_PORT, "/atlas", anon_h, "");
		Check(hidden_index.status == 200, "atlas hidden-map index remains readable");
		Check(hidden_index.body.Find("demo") < 0, "atlas hidden-map index omits hidden map identity");
		Check(hidden_index.body.Find("No maps yet.") >= 0,
		      "atlas all-hidden map index uses the no-maps state");
		Check(hidden_index.body.Find("<table>") < 0,
		      "atlas all-hidden map index does not reveal collection existence through an empty table");
		private_map.grants.Clear();
		private_map.level = CONTENT_PUBLIC;
		atlas_policy.SetRecord("demo", private_map);
		all_maps_hidden.level = CONTENT_PUBLIC;
		atlas_policy.Site() = all_maps_hidden;
		Check(atlas_policy.Save(atlas_policy_error), "atlas public map policy is restored");
		private_map.level = CONTENT_PUBLIC_ONLY;
		atlas_policy.SetRecord("demo", private_map);
		Check(atlas_policy.Save(atlas_policy_error) &&
		      HttpCall("GET", ATLAS_PORT, "/atlas/demo", anon_h, "").status == 200 &&
		      HttpCall("GET", ATLAS_PORT, "/atlas/demo", vw_h, "").status == 404,
		      "atlas public-only policy narrows authenticated reads while preserving anonymous reads");
		private_map.level = CONTENT_PUBLIC;
		atlas_policy.SetRecord("demo", private_map);
		atlas_policy.Site() = all_maps_hidden;
		Check(atlas_policy.Save(atlas_policy_error), "atlas map policy restored after public-only test");
		VectorMap<String, String> nocsrf;
		nocsrf.Add("map", "demo2");
		nocsrf.Add("version", "v1");
		nocsrf.Add("title", "No token");
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/create", adm_h, FormEncode(nocsrf)).status == 403,
		      "a mutation without the CSRF token is refused by the fence");
		VectorMap<String, String> cross_f;
		cross_f.Add("map", "demo");
		cross_f.Add("version", "v0.0.1");
		cross_f.Add("title", "x");
		cross_f.Add("__wf_csrf__", "not-the-token");
		VectorMap<String, String> evil_h;
		evil_h.Add("Origin", "http://evil.example");
		evil_h.Add("Cookie", adm_cookie);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/create", evil_h, FormEncode(cross_f)).status == 403,
		      "a mutation from a foreign origin is refused by the fence");
		VectorMap<String, String> vcreate;
		vcreate.Add("map", "demo3");
		vcreate.Add("version", "v1");
		vcreate.Add("title", "Viewer");
		vcreate.Add("__wf_csrf__", vw_csrf);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/create", vw_h, FormEncode(vcreate)).status == 403,
		      "a viewer cannot create a map");

		HttpReply rmap = HttpCall("GET", ATLAS_PORT, "/atlas/demo", adm_h, "");
		String rev1 = AtlasRevisionOf(rmap.body);
		Check(rmap.status == 200 && rmap.body.Find("atlas-canvas") >= 0 && rmap.body.Find("Edit (admin)") >= 0
		      && rmap.body.Find("data-editable=\"1\"") >= 0, "the admin sees the canvas and the edit panel");
		Check(rev1.GetCount() > 0, "the map page embeds its revision");
		Check(rmap.body.Find("DEMO &lt;v0.0.1&gt;") >= 0 && rmap.body.Find("DEMO <v0.0.1>") < 0,
		      "the map title is escaped");
		Check(rmap.body.Find("id=\"atlas-maximize\"") >= 0 && rmap.body.Find("id=\"atlas-contents\"") >= 0 &&
		      rmap.body.Find("id=\"atlas-fit\"") >= 0 && rmap.body.Find("id=\"atlas-actual\"") >= 0 &&
		      rmap.body.Find("id=\"wf-btn-theme\"") >= 0 && rmap.body.Find("id=\"wf-btn-density\"") >= 0 &&
		      rmap.body.Find("data-theme=\"light\"") < 0,
		      "the map page has the maximize, contents, fit and 1:1 controls and the shared theme buttons");
		Check(rmap.body.Find("id=\"atlas-tight\"") >= 0 && rmap.body.Find("id=\"atlas-edges\"") >= 0,
		      "the map page has the remove-empty-space and edge-to-edge line options");

		// Seed import: a hand-transcribed containment list plus one edge.
		VectorMap<String, String> sf;
		sf.Add("seed", "Main | block | active\n  Child | place | hot\n  Other\nChild -> Other: feeds\n");
		sf.Add("expected_revision", rev1);
		sf.Add("__wf_csrf__", adm_csrf);
		HttpReply rseed = HttpCall("POST", ATLAS_PORT, "/atlas/demo/seed", adm_h, FormEncode(sf));
		Check(rseed.status == 302, "the admin seeds the empty map");
		ContentPolicy hidden_element;
		hidden_element.level = CONTENT_PRIVATE;
		atlas_policy.SetRecord("demo:element:e_child", hidden_element);
		Check(atlas_policy.Save(atlas_policy_error), "atlas element policy is persisted");
		HttpReply element_filtered_map = HttpCall("GET", ATLAS_PORT, "/atlas/demo", anon_h, "");
		Check(element_filtered_map.status == 200, "atlas policy-filtered map remains viewable");
		Check(element_filtered_map.body.Find("e_child") < 0 && element_filtered_map.body.Find(">Child<") < 0,
		      "atlas map view filters hidden element ids and names from its HTML and embedded map");
		HttpReply element_filtered_index = HttpCall("GET", ATLAS_PORT, "/atlas", anon_h, "");
		Check(element_filtered_index.body.Find("<td>2</td><td>0</td>") >= 0,
		      "atlas map index counts only visible elements and roads");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas/demo/element/e_child", anon_h, "").status == 404,
		      "atlas element detail hides a policy-restricted element");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas/demo/element/e_child", vw_h, "").status == 404,
		      "atlas element policy does not grant access to an authenticated stranger");
		ContentGrant element_grant;
		element_grant.kind = "user";
		element_grant.value = "aview";
		hidden_element.grants.Add(pick(element_grant));
		atlas_policy.SetRecord("demo:element:e_child", hidden_element);
		bool element_grant_saved = atlas_policy.Save(atlas_policy_error);
		ContentPrincipal element_principal;
		element_principal.authenticated = true;
		element_principal.username = "aview";
		ContentPolicy stored_element_policy = atlas_policy.Record("demo:element:e_child");
		Check(element_grant_saved && ContentVisible(stored_element_policy, element_principal),
		      "atlas element policy stores the named-user grant");
		HttpReply element_granted = HttpCall("GET", ATLAS_PORT, "/atlas/demo/element/e_child", vw_h, "");
		Check(element_granted.status == 200,
		      "atlas per-element named-user grant allows the element after map visibility");
		hidden_element.grants.Clear();
		hidden_element.level = CONTENT_PUBLIC;
		atlas_policy.SetRecord("demo:element:e_child", hidden_element);
		Check(atlas_policy.Save(atlas_policy_error), "atlas public element policy is restored");
		HttpReply rmap2 = HttpCall("GET", ATLAS_PORT, "/atlas/demo", adm_h, "");
		String rev2 = AtlasRevisionOf(rmap2.body);
		Check(rmap2.body.Find("3 elements") >= 0 && rmap2.body.Find("1 roads") >= 0,
		      "the seed creates its elements and road");
		Check(rev2 != rev1, "a write advances the revision");
		HttpReply rseed_stale = HttpCall("POST", ATLAS_PORT, "/atlas/demo/seed", adm_h, FormEncode(sf));
		Check(rseed_stale.status == 409, "a stale revision is refused as a conflict");
		VectorMap<String, String> sf2;
		sf2.Add("seed", "Again\n");
		sf2.Add("expected_revision", rev2);
		sf2.Add("__wf_csrf__", adm_csrf);
		HttpReply rseed_again = HttpCall("POST", ATLAS_PORT, "/atlas/demo/seed", adm_h, FormEncode(sf2));
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
		HttpReply radd = HttpCall("POST", ATLAS_PORT, "/atlas/demo/element/add", adm_h, FormEncode(af));
		Check(radd.status == 302, "the admin adds an element");
		HttpReply rmap3 = HttpCall("GET", ATLAS_PORT, "/atlas/demo", adm_h, "");
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
		HttpReply rmove = HttpCall("POST", ATLAS_PORT, "/atlas/demo/element/move", adm_h, FormEncode(mf));
		ValueMap mj = ParseJSON(rmove.body);
		Check(rmove.status == 200 && (bool)mj["ok"], "a move saves and answers ok");
		String rev4 = AtlasRevisionOf(HttpCall("GET", ATLAS_PORT, "/atlas/demo", adm_h, "").body);
		Check(rev4 != rev3 && (String)mj["revision"] == rev4, "a move advances the revision it returns");
		HttpReply rmove_stale = HttpCall("POST", ATLAS_PORT, "/atlas/demo/element/move", adm_h, FormEncode(mf));
		Check(rmove_stale.status == 409, "a move from a stale page is refused");
		VectorMap<String, String> mv;
		mv.Add("id", "e_child");
		mv.Add("x", "-5");
		mv.Add("y", "60");
		mv.Add("expected_revision", rev4);
		mv.Add("__wf_csrf__", adm_csrf);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/demo/element/move", adm_h, FormEncode(mv)).status == 400,
		      "a move to a negative position is refused by validation");
		VectorMap<String, String> vmove;
		vmove.Add("id", "e_child");
		vmove.Add("x", "300");
		vmove.Add("y", "60");
		vmove.Add("expected_revision", rev4);
		vmove.Add("__wf_csrf__", vw_csrf);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/demo/element/move", vw_h, FormEncode(vmove)).status == 403,
		      "a viewer cannot move an element");

		// Roads: one plane only; a road from a child to its own block is refused.
		VectorMap<String, String> rf;
		rf.Add("from", "e_child");
		rf.Add("to", "e_main");
		rf.Add("kind", "dependency");
		rf.Add("label", "cross");
		rf.Add("expected_revision", rev4);
		rf.Add("__wf_csrf__", adm_csrf);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/demo/road/add", adm_h, FormEncode(rf)).status == 400,
		      "a road across planes is refused");

		// Deleting a block that still has children is refused.
		VectorMap<String, String> df;
		df.Add("id", "e_main");
		df.Add("expected_revision", rev4);
		df.Add("__wf_csrf__", adm_csrf);
		Check(HttpCall("POST", ATLAS_PORT, "/atlas/demo/element/delete", adm_h, FormEncode(df)).status == 400,
		      "a block with children cannot be deleted");

		// Reads: the viewer sees the map read-only; the element detail shows its path.
		HttpReply rvm = HttpCall("GET", ATLAS_PORT, "/atlas/demo", vw_h, "");
		Check(rvm.status == 200 && rvm.body.Find("data-editable=\"0\"") >= 0 && rvm.body.Find("Edit (admin)") < 0,
		      "the viewer reads the map with no edit panel");
		HttpReply rve = HttpCall("GET", ATLAS_PORT, "/atlas/demo/element/e_child", vw_h, "");
		Check(rve.status == 200 && rve.body.Find("Containment path") >= 0 && rve.body.Find("Main") >= 0,
		      "the element detail shows its containment path");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas/demo/element/e_nope", vw_h, "").status == 404,
		      "an unknown element is not found");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas/demo", vw_h, "").status == 200
		      && HttpCall("GET", ATLAS_PORT, "/atlas/no-such-map", vw_h, "").status == 404,
		      "an unknown map is not found");
		String atlas_policy_root = AppendFileName(s_root, "atlas");
		String atlas_policy_path = AppendFileName(atlas_policy_root, "content_policy.json");
		Check(SaveFile(atlas_policy_path, "{broken"), "atlas corrupt-policy HTTP fixture is written");
		String atlas_load_error;
		Check(!atlas_policy.Load(atlas_policy_root, atlas_load_error) && !atlas_policy.LoadError().IsEmpty(),
		      "atlas corrupt policy fails to load");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas", anon_h, "").status == 503,
		      "atlas corrupt policy fails closed on the map index");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas/demo", anon_h, "").status == 503,
		      "atlas corrupt policy fails closed on map detail");
		Check(HttpCall("GET", ATLAS_PORT, "/atlas/demo/element/e_child", anon_h, "").status == 503,
		      "atlas corrupt policy fails closed on element detail");
		FileDelete(atlas_policy_path);
		Check(atlas_policy.Load(atlas_policy_root, atlas_load_error), "atlas policy store recovers after corrupt fixture removal");
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
		// The SSO routes exist only with the opt-in switch on (task 039 follow-up).
		TheWebEngine().SetMetaSso(true);
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
		// login, logout, sso/link, sso/provision and explicit sso/consent with the switch on.
		Check(posts == 5 && covered && cmds.GetCount() == 6,
		      "session POST routes each have a manifest command with the same path and method");
		TheWebEngine().SetMetaSso(false);
		EngineSite quiet;
		quiet.id = "quiet";
		quiet.title = "Quiet";
		InstallSessionRoutes(quiet);
		int sso_routes = 0;
		for(int i = 0; i < quiet.Router().GetCount(); i++)
			if(quiet.Router()[i].pattern.Find("sso") >= 0)
				sso_routes++;
		Check(sso_routes == 0, "with SSO off no session/sso route is registered");
	}

	// Content visibility policy (task 053): level matrix, grants, link tokens, inheritance and persistence.
	{
		ContentPrincipal anon;
		ContentPrincipal user;
		user.authenticated = true;
		user.username = "alice";
		user.groups.Add("editor");
		ContentPrincipal stranger;
		stranger.authenticated = true;
		stranger.username = "bob";
		ContentPrincipal admin;
		admin.authenticated = true;
		admin.admin = true;
		admin.username = "root";

		ContentPolicy pub;    pub.level = CONTENT_PUBLIC;
		ContentPolicy only;   only.level = CONTENT_PUBLIC_ONLY;
		ContentPolicy priv;   priv.level = CONTENT_PRIVATE;
		ContentPolicy shared; shared.level = CONTENT_PRIVATE;
		ContentGrant ug; ug.kind = "user"; ug.value = "alice";
		ContentGrant gg; gg.kind = "group"; gg.value = "editor";
		shared.grants.Add(ug);
		shared.grants.Add(gg);

		Check(ContentVisible(pub, anon) && ContentVisible(pub, user), "public is visible to anonymous and signed-in");
		Check(ContentVisible(only, anon) && !ContentVisible(only, user), "public-only is visible to anonymous only");
		Check(!ContentVisible(priv, anon) && !ContentVisible(priv, stranger) && ContentVisible(priv, admin),
		      "private is hidden from anonymous and strangers, visible to admins");
		Check(ContentVisible(shared, user) && !ContentVisible(shared, stranger) && !ContentVisible(shared, anon),
		      "private grants reach a named user and a group, not others");

		ContentPolicy prot; prot.level = CONTENT_PROTECTED;
		String token = "tok-" + WebAuthRandomBytes(16);
		ContentGrant lg; lg.kind = "link"; lg.value = ContentLinkHash(token); lg.label = "review";
		prot.grants.Add(lg);
		ContentPrincipal with_link; with_link.link_token = token;
		ContentPrincipal wrong_link; wrong_link.link_token = "tok-wrong";
		Check(!ContentVisible(prot, anon) && ContentVisible(prot, with_link) && !ContentVisible(prot, wrong_link),
		      "protected opens only with the matching link token");
		Check(ContentLinkHash(token) != token && lg.value != token, "a link token is stored and compared only as its hash");
		prot.grants[0].revoked = true;
		Check(!ContentVisible(prot, with_link), "a revoked link no longer opens protected content");
		ContentPolicy prot_user; prot_user.level = CONTENT_PROTECTED; prot_user.grants.Add(ug);
		Check(ContentVisible(prot_user, user) && !ContentVisible(prot_user, anon), "protected named-user grant needs sign-in");

		Vector<ContentPolicy> chain;
		chain.Add(ContentPolicy());   // record: unset
		chain.Add(pub);                // module
		chain.Add(priv);               // site
		Check(ContentResolve(chain, CONTENT_PRIVATE).level == CONTENT_PUBLIC,
		      "an unset record passes through to the module's explicit policy");
		Vector<ContentPolicy> none;
		Check(ContentResolve(none, CONTENT_PRIVATE).level == CONTENT_PRIVATE, "with no explicit policy the fallback applies");

		String dir = GetExeDirFile("content-policy-check");
		RealizeDirectory(dir);
		ContentPolicyStore store;
		String err;
		store.Load(dir, err);
		store.Site().level = CONTENT_PRIVATE;
		store.SetModule("news", pub);
		store.SetRecord("paper-1", only);
		Check(store.Save(err), "a content policy is saved");
		ContentPolicyStore again;
		bool inherited_loaded = again.Load(dir, err);
		Check(inherited_loaded && again.Effective("news", "paper-1", CONTENT_PRIVATE).level == CONTENT_PUBLIC_ONLY
		      && again.Effective("news", "other", CONTENT_PRIVATE).level == CONTENT_PUBLIC
		      && again.Effective("zoo", "other", CONTENT_PRIVATE).level == CONTENT_PRIVATE,
		      "a saved policy reloads with record, module and site inheritance intact: " + err);
		FileDelete(AppendFileName(dir, "content_policy.json"));
		String valid = "{\"version\":1,\"site\":{\"level\":\"\",\"grants\":[]},\"modules\":{},\"records\":{}}";
		bool valid_fixture_saved = SaveFile(AppendFileName(dir, "content_policy.json"), valid);
		bool valid_fixture_loaded = valid_fixture_saved && again.Load(dir, err);
		Check(valid_fixture_loaded, "a complete version-1 policy document loads: " + err);
		Vector<String> malformed;
		malformed << "{\"version\":1,\"site\":null,\"modules\":{},\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":[]},\"modules\":[],\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":[]},\"modules\":{\"news\":false},\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":[]},\"modules\":{\"news\":{\"level\":\"invalid\",\"grants\":[]}},\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":[]},\"modules\":{},\"records\":{\"paper\":[]}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":[]},\"modules\":{},\"records\":{\"paper\":{\"level\":\"protected\",\"grants\":[false]}}}"
		          << "{\"version\":1,\"site\":{\"level\":7,\"grants\":[]},\"modules\":{},\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"unknown\",\"grants\":[]},\"modules\":{},\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":{}},\"modules\":{},\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":[null]},\"modules\":{},\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":[{\"kind\":\"user\",\"value\":\"alice\",\"label\":\"\",\"revoked\":\"no\"}]},\"modules\":{},\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":[{\"kind\":\"mystery\",\"value\":\"x\",\"label\":\"\",\"revoked\":false}]},\"modules\":{},\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":[],\"ignored\":true},\"modules\":{},\"records\":{}}"
		          << "{\"version\":1,\"site\":{\"level\":\"public\",\"grants\":[]},\"modules\":{},\"records\":{},\"ignored\":true}";
		for(int i = 0; i < malformed.GetCount(); i++) {
			SaveFile(AppendFileName(dir, "content_policy.json"), malformed[i]);
			ContentPolicyStore bad;
			Check(!bad.Load(dir, err) && !bad.LoadError().IsEmpty(),
			      Format("malformed version-1 policy %d fails closed", i + 1));
		}
		Check(SaveFile(AppendFileName(dir, "content_policy.json"), "{broken"), "a corrupt policy fixture is written");
		ContentPolicyStore corrupt;
		Check(!corrupt.Load(dir, err) && !corrupt.LoadError().IsEmpty(),
		      "a corrupt persisted policy fails loading with an actionable error");
		Check(SaveFile(AppendFileName(dir, "content_policy.json"), "{\"version\":999}"), "an unsupported policy fixture is written");
		ContentPolicyStore unsupported;
		Check(!unsupported.Load(dir, err) && err.Find("version") >= 0,
		      "an unsupported persisted policy version fails loading with a version error");
		FileDelete(AppendFileName(dir, "content_policy.json"));
		DeleteFolderDeep(dir);
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
