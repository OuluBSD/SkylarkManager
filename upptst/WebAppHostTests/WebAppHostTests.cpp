#include <WebAppHost/WebAppHost.h>
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

CONSOLE_APP_MAIN
{
	TestRegistry();
	TestOrderAndRefusals();
	TestFlags();
	TestCompose();
	Cout() << "WebAppHost self-tests: " << (checks - failures) << "/" << checks << " checks passed\n";
	if(failures) {
		Cout() << "WebAppHost self-tests FAILED\n";
		fflush(stdout);
		exit(1);
	}
}
