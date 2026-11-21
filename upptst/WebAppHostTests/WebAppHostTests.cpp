#include <WebAppHost/WebAppHost.h>
#include <WebStorage/WebStorage.h>
#include <PrivateHubWorkspace/PrivateHubApp.h>
#include <PrivateHubWorkspace/PrivateHubService.h>
#include <Science/ScienceApp.h>
#include <initializer_list>

using namespace Upp;

NAMESPACE_UPP
Vector<String>& WebAppTestLog();
END_UPP_NAMESPACE

static int checks = 0;
static int failures = 0;

static bool Check(bool cond, const String& what)
{
	checks++;
	if(cond)
		Cout() << "ok    " << what << "\n";
	else {
		failures++;
		Cout() << "FAILED: " << what << "\n";
	}
	return cond;
}

static Vector<String> Words(std::initializer_list<String> list)
{
	Vector<String> v;
	for(const String& s : list)
		v.Add(s);
	return v;
}

static String JoinIds(const Vector<String>& ids)
{
	String s;
	for(const String& id : ids)
		s << (s.IsEmpty() ? "" : ",") << id;
	return s;
}

static void TestRegistry()
{
	Vector<String> names = WebappNames();
	Check(FindIndex(names, String("AlphaApp")) >= 0 && FindIndex(names, String("GammaApp")) >= 0,
	      "registry: classes registered from the .icpp INITBLOCK are listed");
	WebApp *alpha = CreateWebapp("AlphaApp");
	Check(alpha && alpha->Id() == "alpha" && alpha->Title() == "Alpha" && alpha->DefaultPort() == 18301,
	      "registry: a registered name creates its app");
	Check(CreateWebapp("NoSuchApp") == nullptr, "registry: an unknown name is refused");
	delete alpha;

	WebServer server;
	String error;
	Check(!server.Enable("NoSuchApp", error) && error.Find("unknown webapp") >= 0,
	      "enable: an unknown name is refused with an error");
	Check(server.Enable("AlphaApp", error) && server.Enable("AlphaApp", error) && server.EnabledIds().GetCount() == 1,
	      "enable: enabling the same name twice keeps one app");
}

static void TestOrderAndRefusals()
{
	String error;
	Vector<String> ids;

	{
		WebServer s;
		s.Enable("GammaApp", error);
		s.Enable("AlphaApp", error);
		s.Enable("BetaApp", error);
		Check(s.InstallOrder(ids, error) && JoinIds(ids) == "alpha,beta,gamma",
		      "requires order: alpha, beta, gamma regardless of enable order");
	}
	{
		WebServer s;
		s.Enable("BetaApp", error);
		Check(!s.InstallOrder(ids, error) && error.Find("requires 'alpha'") >= 0,
		      "requires order: a required id that is not enabled is refused");
	}
	{
		WebServer s;
		s.Enable("MissingReqApp", error);
		Check(!s.InstallOrder(ids, error) && error.Find("requires 'nowhere'") >= 0,
		      "requires order: an unknown required id is refused");
	}
	{
		WebServer s;
		s.Enable("CycleOneApp", error);
		s.Enable("CycleTwoApp", error);
		Check(!s.InstallOrder(ids, error) && error.Find("cycle") >= 0 && ids.IsEmpty(),
		      "requires order: a cycle is refused");
	}
	{
		WebServer s;
		s.Enable("DupIdA", error);
		s.Enable("DupIdB", error);
		Check(!s.InstallOrder(ids, error) && error.Find("duplicate webapp id 'dup'") >= 0,
		      "duplicate id: two apps with one id are refused");
	}
	{
		WebServer s;
		s.Enable("PortOneApp", error);
		s.Enable("PortTwoApp", error);
		Check(!s.InstallOrder(ids, error) && error.Find("both use port 18310") >= 0,
		      "duplicate port: two apps on one port are refused");
	}
	{
		WebServer s;
		s.Enable("StockPortApp", error);
		Check(!s.InstallOrder(ids, error) && error.Find("stock listener") >= 0,
		      "stock port: an app on the stock listener port is refused");
	}
	{
		WebServer s;
		s.Enable("ZetaApp", error);
		Check(s.InstallOrder(ids, error) && JoinIds(ids) == "zeta",
		      "order: a lone enabled app is ordered alone");
	}
}

static void TestFlags()
{
	String error;
	String scratch = GetTempFileName("WebAppHostFlags");
	WebServer s;
	s.Enable("AlphaApp", error);
	s.Enable("BetaApp", error);

	Check(s.ParseArgs(Words({ "--port", "8090", "--site-dir", scratch, "--public-host", "192.168.1.50", "--enable", "ZetaApp",
		"--beta-port", "18400", "--set-alpha-user", "bob" }), error),
	      "flags: a full set of server, per-app and enable flags parses");
	Check(s.port == 8090 && s.public_host == "192.168.1.50" && s.site_dir == NormalizePath(scratch),
	      "flags: --port, --public-host and --site-dir are read");
	Check(s.PortFor("beta") == 18400 && s.PortFor("alpha") == 18301 && s.PortFor("zeta") == 18304,
	      "flags: --beta-port overrides, others keep DefaultPort, --enable adds the app");
	Check(s.SetUserFor("alpha") == "bob" && s.SetUserFor("beta").IsEmpty(),
	      "flags: --set-alpha-user names the account for alpha");

	WebServer bad1;
	Check(!bad1.ParseArgs(Words({ "--bogus" }), error), "flags: an unknown flag is refused");
	WebServer bad2;
	Check(!bad2.ParseArgs(Words({ "--port", "70000" }), error), "flags: a port out of range is refused");
	WebServer bad3;
	Check(!bad3.ParseArgs(Words({ "--enable", "NoSuchApp" }), error), "flags: --enable of an unknown name is refused");
	DeleteFolderDeep(scratch);
}

static void TestCompose()
{
	String error;
	String scratch = GetTempFileName("WebAppHostCompose");
	RealizeDirectory(scratch);

	WebServer s;
	s.Enable("GammaApp", error);
	s.Enable("AlphaApp", error);
	s.Enable("BetaApp", error);
	Vector<String> flags = Words({ "--site-dir", scratch, "--public-host", "192.168.1.50" });
	Check(s.ParseArgs(flags, error), "compose: flags for a scratch site parse");

	WebAppTestLog().Clear();
	One<WebServerHost> host;
	host.Create();
	Check(s.Compose(*host, error), "compose: enabled apps compose without error (" + error + ")");
	Check(JoinIds(WebAppTestLog()) == "alpha,beta,gamma", "compose: apps install in Requires order");
	Check(FindIndex(WebAppTestLog(), String("missing-context")) < 0, "compose: apps receive the shared context");
	Check(TheWebEngine().FindSite("zeta") == nullptr, "compose: a disabled app is not installed");
	EngineSite *gamma = TheWebEngine().FindSite("gamma");
	Check(gamma && gamma->title == "Gamma" && gamma->external_origin == "http://192.168.1.50:18303",
	      "compose: the site has its title and public origin on its port");
	Check(s.SetUser("alpha", "bob", "synthetic-password", error), "compose: an account is written on the alpha site");
	WebAccountUser user;
	Check(TheWebEngine().FindSite("alpha") && TheWebEngine().FindSite("alpha")->Accounts().FindUserByUsername("bob", user),
	      "compose: the written account can be found");
	Check(!s.SetUser("zeta", "bob", "synthetic-password", error), "compose: an account on a disabled app is refused");

	WebServer again;
	again.Enable("AlphaApp", error);
	again.ParseArgs(flags, error);
	Check(!again.Compose(*host, error) && error.Find("already composed") >= 0,
	      "compose: a site id that is already composed is refused");

	DeleteFolderDeep(scratch);
}

struct BlobReply {
	int    status = 0;
	String content_type;
	String nosniff;
	String disposition;
	String csp;
	String session_cookie;   // "name=value" of the first Set-Cookie of a wfs_ cookie
	String body;
};

// Raw HTTP/1.1 client for one exchange (Connection: close). The Host is the site public authority, which the
// engine gateway pins; the socket connects to loopback. Retries while the listener comes up.
static BlobReply BlobCall(int port, const String& method, const String& path, const String& cookie,
                          const String& body, const char *ctype)
{
	BlobReply r;
	for(int attempt = 0; attempt < 100 && r.status == 0; attempt++) {
		TcpSocket s;
		s.Timeout(10000);
		if(!s.Connect("127.0.0.1", port)) {
			Sleep(100);
			continue;
		}
		StringBuffer req;
		req << method << ' ' << path << " HTTP/1.1\r\nHost: 192.168.1.50:" << port << "\r\nConnection: close\r\n";
		req << "Content-Type: " << ctype << "\r\n";
		if(cookie.GetCount())
			req << "Cookie: " << cookie << "\r\n";
		if(body.GetCount())
			req << "Content-Length: " << body.GetCount() << "\r\n";
		req << "\r\n" << body;
		s.Put(req);
		StringBuffer all;
		String data;
		while((data = s.Get(4096)).GetCount())
			all << data;
		String text = all;
		int q = text.Find("\r\n\r\n");
		if(q < 0) {
			Sleep(100);
			continue;
		}
		r.body = text.Mid(q + 4);
		Vector<String> lines = Split(text.Left(q), '\n');
		if(lines.GetCount()) {
			Vector<String> st = Split(lines[0], ' ');
			if(st.GetCount() >= 2)
				r.status = ScanInt(st[1]);
		}
		for(int i = 1; i < lines.GetCount(); i++) {
			String line = TrimBoth(lines[i]);
			int c = line.Find(':');
			if(c < 0)
				continue;
			String key = ToLower(TrimBoth(line.Left(c)));
			String value = TrimBoth(line.Mid(c + 1));
			if(key == "content-type")
				r.content_type = value;
			else if(key == "x-content-type-options")
				r.nosniff = value;
			else if(key == "content-disposition")
				r.disposition = value;
			else if(key == "content-security-policy")
				r.csp = value;
			else if(key == "set-cookie" && value.StartsWith("wfs_") && r.session_cookie.IsEmpty())
				r.session_cookie = TrimBoth(value.Left(value.Find(';') < 0 ? value.GetCount() : value.Find(';')));
		}
	}
	return r;
}

// The blob test runs the host the way the program does: Run() finalizes the Skylark views (the engine
// gateway is one of them) and starts the listeners on a worker thread. Its stock port is a test port,
// never the live portal port.
static const int BLOB_STOCK_PORT = 18320;
static One<WebServerHost> *s_blob_host = nullptr;   // kept for the life of the process; see main
static void BlobHostMain() { (*s_blob_host)->Run(); }

// The PrivateHub adapter, driven directly: its Install refuses a site root that is not a directory, and
// its broker registration holds the data root, so a rival registration on the same root is refused.
// The adapter is driven directly because WebServer::Compose adds the site before Install runs.
static void TestPrivateHubAdapter()
{
	String error;
	String scratch = GetTempFileName("WebAppHostPrivateHub");
	RealizeDirectory(scratch);

	// A bad root: the engine's privatehub directory is a plain file, so Install refuses it.
	String bad_dir = AppendFileName(scratch, "bad");
	RealizeDirectory(AppendFileName(bad_dir, "engine"));
	SaveFile(AppendFileName(AppendFileName(bad_dir, "engine"), "privatehub"), "not a directory");

	// A valid root: the site is added the way WebServer adds it, then the adapter installs on it.
	String good_dir = AppendFileName(scratch, "good");
	String good_root = AppendFileName(AppendFileName(good_dir, "engine"), "privatehub");
	RealizeDirectory(good_root);
	String workspace = AppendFileName(scratch, "workspace");
	RealizeDirectory(workspace);

	EngineSite& site = TheWebEngine().AddSite("privatehub", "PrivateHub", "http://192.168.1.50:18211", good_root);
	static PrivateHubApp *app = new PrivateHubApp;   // lives for the process: the broker keeps its backend

	WebAppContext bad_ctx;
	bad_ctx.site_dir = bad_dir;
	bad_ctx.settings.Add("workspace", workspace);
	Check(!app->Install(site, bad_ctx, error) && error.Find("not a directory") >= 0,
	      "privatehub: an Install on a root that is not a directory is refused (" + error + ")");

	error.Clear();
	WebAppContext ctx;
	ctx.site_dir = good_dir;
	ctx.settings.Add("workspace", workspace);
	ctx.broker = &TheWebEngine().Broker();
	Check(app->Install(site, ctx, error), "privatehub: an Install on a valid root succeeds (" + error + ")");
	Check(app->RegisterBroker(ctx, error), "privatehub: the broker registration succeeds (" + error + ")");
	Check(DirectoryExists(AppendFileName(good_root, "broker")), "privatehub: the broker data root is created under the site");

	WebBroker rival;
	String rival_error;
	String broker_root = AppendFileName(good_root, "broker");
	PrivateHubWorkspaceBackend rival_backend(workspace, good_root);
	Check(!RegisterPrivateHubService(rival, "privatehub", rival_backend, broker_root, rival_error) &&
	      !rival_error.IsEmpty(),
	      "privatehub: a rival registration on the adapter's data root is refused (" + rival_error + ")");

	DeleteFolderDeep(scratch);
}

// The Science adapter, driven directly as the PrivateHub test is: Install refuses a missing corpus setting,
// installs on a valid corpus with the shared grant store, and RegisterBroker registers the science service.
static void TestScienceAdapter()
{
	String error;
	String scratch = GetTempFileName("WebAppHostScience");
	RealizeDirectory(scratch);
	String corpus = AppendFileName(scratch, "corpus");
	RealizeDirectory(corpus);
	String grants_root = AppendFileName(scratch, "grants");
	RealizeDirectory(grants_root);
	WebGrants grants("meta", grants_root);
	String good_root = AppendFileName(AppendFileName(scratch, "engine"), "science");
	RealizeDirectory(good_root);

	EngineSite& site = TheWebEngine().AddSite("science", "Science", "http://192.168.1.50:18212", good_root);
	static ScienceApp *app = new ScienceApp;   // lives for the process: the broker keeps its corpus

	WebAppContext bad_ctx;
	bad_ctx.site_dir = scratch;
	bad_ctx.grants = &grants;
	bad_ctx.broker = &TheWebEngine().Broker();
	Check(!app->Install(site, bad_ctx, error) && error.Find("no corpus") >= 0,
	      "science: an Install without a corpus setting is refused (" + error + ")");

	error.Clear();
	WebAppContext ctx;
	ctx.site_dir = scratch;
	ctx.settings.Add("corpus", corpus);
	ctx.grants = &grants;
	ctx.broker = &TheWebEngine().Broker();
	Check(app->Install(site, ctx, error), "science: an Install on a valid corpus succeeds (" + error + ")");
	Check(app->RegisterBroker(ctx, error), "science: the broker registration succeeds (" + error + ")");
	Check(app->Roles().GetCount() == 1 && app->Roles()[0] == "science-author",
	      "science: the first role is the science author role");

	DeleteFolderDeep(scratch);
}

// Fallible Install, the per-app settings, the site directory and WebServer::App.
static void TestFallibleAndSettings()
{
	String error;
	String scratch = GetTempFileName("WebAppHostSettings");
	RealizeDirectory(scratch);

	WebServer refuse;
	refuse.Enable("RefusingApp", error);
	Check(refuse.ParseArgs(Words({ "--site-dir", scratch, "--public-host", "192.168.1.50" }), error),
	      "settings: flags for a refusing app parse");
	One<WebServerHost> host;
	host.Create();
	Check(!refuse.Compose(*host, error) && error.Find("synthetic install refusal") >= 0,
	      "settings: an Install that returns false refuses the composition with its error");

	WebServer s;
	s.Enable("SettingsApp", error);
	s.SetSetting("settings", "label", "from-server");
	Check(s.ParseArgs(Words({ "--site-dir", scratch, "--public-host", "192.168.1.50" }), error),
	      "settings: flags for a settings app parse");
	WebAppTestLog().Clear();
	Check(s.Compose(*host, error), "settings: the settings app composes (" + error + ")");
	Check(FindIndex(WebAppTestLog(), String("setting:from-server")) >= 0,
	      "settings: the app reads the setting its server set for it");
	Check(FindIndex(WebAppTestLog(), String("unset:")) >= 0,
	      "settings: an unset key reads as empty");
	EngineSite *site = TheWebEngine().FindSite("settings");
	Check(site && site->data_root == AppendFileName(AppendFileName(scratch, "engine"), "settings-dir"),
	      "settings: the site root comes from SiteDir(), not the id");
	Check(s.App("settings") != nullptr && s.App("zeta") == nullptr,
	      "settings: App() finds an enabled app and refuses a disabled one");

	DeleteFolderDeep(scratch);
}

static void TestBlobRoutes()
{
	String error;
	String scratch = GetTempFileName("WebAppHostBlobs");
	RealizeDirectory(scratch);
	WebServer s;
	s.Enable("BlobTestApp", error);
	Check(s.ParseArgs(Words({ "--port", AsString(BLOB_STOCK_PORT), "--site-dir", scratch, "--public-host", "192.168.1.50" }), error),
	      "blob: flags for a scratch site parse");
	s_blob_host = new One<WebServerHost>;
	s_blob_host->Create();
	Check(s.Compose(**s_blob_host, error), "blob: the blob test app composes (" + error + ")");
	Check(s.SetUser("blobtest", "ed", "synthetic-password", error), "blob: an editor account is written");

	EngineSite *site = TheWebEngine().FindSite("blobtest");
	Check(site != nullptr, "blob: the site is composed under its id");
	if(!site)
		return;
	// Bytes an editor could upload: markup, a vector image and a PNG, stored through the same store the route uses.
	BlobStore store(site->id, site->data_root, "blobs", 1024 * 1024);
	BlobRecord rec;
	String none = String(RecordAbsent());
	StoreResult a = store.Put("page", "<script>alert(document.cookie)</script>", "text/html", 0, 0, "", none, rec);
	StoreResult b = store.Put("vec", "<svg xmlns='http://www.w3.org/2000/svg' onload='alert(1)'/>", "image/svg+xml", 0, 0, "", none, rec);
	StoreResult c = store.Put("pic", "\x89PNG\r\n\x1a\n-bytes", "image/png", 1, 1, "", none, rec);
	Check(a.ok && b.ok && c.ok, "blob: html, svg and png blobs are stored");

	static Thread host_thread; // lives to process exit: the listener thread keeps serving until _Exit
	host_thread.Run(BlobHostMain);
	const int port = 18315;
	BlobReply login = BlobCall(port, "POST", "/session/login", "", "username=ed&password=synthetic-password",
	                           "application/x-www-form-urlencoded");
	Check(login.session_cookie.StartsWith("wfs_blobtest="), "blob: the editor signs in and gets a site cookie");

	BlobReply anon = BlobCall(port, "GET", "/blobs/page", "", "", "text/plain");
	Check(anon.status == 401, "blob: an anonymous read of an uploaded html blob is refused (status " + AsString(anon.status) + ")");

	BlobReply html = BlobCall(port, "GET", "/blobs/page", login.session_cookie, "", "text/plain");
	Check(html.status == 200 && html.content_type == "application/octet-stream" && html.disposition == "attachment",
	      "blob: an uploaded html blob is served as an attachment, not as text/html");
	Check(!ToLower(html.content_type).StartsWith("text/html"), "blob: the html blob never carries a text/html type");
	Check(html.nosniff == "nosniff", "blob: the html blob carries X-Content-Type-Options: nosniff");
	Check(html.csp.Find("default-src 'none'") >= 0 && html.csp.Find("sandbox") >= 0,
	      "blob: the html blob carries a default-src 'none'; sandbox policy");

	BlobReply svg = BlobCall(port, "GET", "/blobs/vec", login.session_cookie, "", "text/plain");
	Check(svg.status == 200 && svg.content_type == "application/octet-stream" && svg.disposition == "attachment",
	      "blob: an uploaded svg blob is served as an attachment, not as image/svg+xml");

	BlobReply png = BlobCall(port, "GET", "/blobs/pic", login.session_cookie, "", "text/plain");
	Check(png.status == 200 && png.content_type == "image/png" && png.disposition.IsEmpty(),
	      "blob: a png blob is still served inline as image/png");
	Check(png.nosniff == "nosniff", "blob: the png blob carries X-Content-Type-Options: nosniff");
	DeleteFolderDeep(scratch);
}

CONSOLE_APP_MAIN
{
	TestRegistry();
	TestOrderAndRefusals();
	TestFlags();
	TestCompose();
	TestPrivateHubAdapter();
	TestScienceAdapter();
	TestFallibleAndSettings();
	TestBlobRoutes();
	Cout() << "WebAppHost self-tests: " << (checks - failures) << "/" << checks << " checks passed\n";
	if(failures) {
		Cout() << "WebAppHost self-tests FAILED\n";
		fflush(stdout);
		_Exit(1);
	}
	fflush(stdout);
	_Exit(0);
}
