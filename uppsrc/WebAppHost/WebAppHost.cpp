#include "WebAppHost.h"
#include <WebAccounts/WebAccounts.h>
#include <WebEngine/SessionRoutes.h>
#include <WebEngine/CommandConsole.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>

NAMESPACE_UPP

String WebAppContext::SiteRoot(const String& id) const
{
	return AppendFileName(AppendFileName(site_dir, "engine"), id);
}

ArrayMap<String, WebAppNewFn>& WebAppFactories()
{
	static ArrayMap<String, WebAppNewFn> factories;
	return factories;
}

Vector<String> WebappNames()
{
	Vector<String> names;
	const ArrayMap<String, WebAppNewFn>& factories = WebAppFactories();
	for(int i = 0; i < factories.GetCount(); i++)
		names.Add(factories.GetKey(i));
	return names;
}

WebApp* CreateWebapp(const String& name)
{
	const ArrayMap<String, WebAppNewFn>& factories = WebAppFactories();
	int i = factories.Find(name);
	return i < 0 ? nullptr : factories[i]();
}

// Builds http://host:port; IPv6 literals are bracketed.
static String PublicOrigin(const String& host, int port)
{
	String authority = host.Find(':') >= 0 ? "[" + host + "]" : host;
	return "http://" + authority + ":" + AsString(port);
}

static String EnvNameFor(const String& id, const char *suffix)
{
	String part = ToUpper(id);
	part.Replace("-", "_");
	return "WEBAPP_" + part + suffix;
}

WebServer::WebServer()
{
	site_dir = GetHomeDirFile(".config/webapphost");
}

WebServer::~WebServer()
{
	for(WebApp *app : apps)
		delete app;
}

WebApp* WebServer::FindApp(const String& id) const
{
	for(WebApp *app : apps)
		if(app->Id() == id)
			return app;
	return nullptr;
}

bool WebServer::Enable(const String& name, String& error)
{
	for(const String& existing : names)
		if(existing == name)
			return true;
	WebApp *app = CreateWebapp(name);
	if(!app) {
		error = "unknown webapp '" + name + "'; it is not registered";
		return false;
	}
	names.Add(name);
	apps.Add(app);
	return true;
}

Vector<String> WebServer::EnabledIds() const
{
	Vector<String> ids;
	for(WebApp *app : apps)
		ids.Add(app->Id());
	return ids;
}

void WebServer::SetAgentIpc(const AgentIpcConfig& cfg)
{
	agentipc = cfg;
	has_agentipc = true;
}

void WebServer::SetAppPort(const String& id, int listen_port)
{
	app_ports.GetAdd(id) = listen_port;
}

void WebServer::UseGrants(WebGrants* shared)
{
	shared_grants = shared;
}

bool WebServer::ParseArgs(const Vector<String>& args, String& error)
{
	for(int i = 0; i < args.GetCount(); i++) {
		if(args[i] != "--enable" || i + 1 >= args.GetCount())
			continue;
		Vector<String> list = Split(args[i + 1], ',', false);
		for(String name : list) {
			name = TrimBoth(name);
			if(!name.IsEmpty() && !Enable(name, error))
				return false;
		}
		i++;
	}

	CommandLineArguments cla;
	cla.SetDescription("WebServer - composes the enabled webapps on one engine host");
	cla.AddArg("port", 'p', "Stock listener port", true, "port");
	cla.AddArg("site-dir", 0, "Site directory; per-site data lives under <site-dir>/engine/<id>", true, "directory");
	cla.AddArg("public-host", 0, "Address other machines use for site links; never loopback (default WEBAPP_PUBLIC_HOST)", true, "host");
	cla.AddArg("enable", 0, "Enable registered webapps by name, comma separated", true, "name[,name]");
	for(WebApp *app : apps) {
		String id = app->Id();
		cla.AddArg(id + "-port", 0, "Listener port for webapp " + id, true, "port");
		cla.AddArg("set-" + id + "-user", 0, "Create or reset an account on " + id + " and exit (password from " +
		           EnvNameFor(id, "_PASSWORD") + " or a hidden prompt)", true, "username");
		app->AddArgs(cla);
	}
	if(!cla.Parse(args)) {
		error = "invalid arguments";
		return false;
	}

	String p = cla.GetArg('p');
	if(p.IsEmpty())
		p = cla.GetArg("port");
	if(!p.IsEmpty()) {
		int value = ScanInt(p);
		if(value < 1 || value > 65535) {
			error = "invalid --port " + p + "; expected a port from 1 to 65535";
			return false;
		}
		port = value;
	}
	if(!cla.GetArg("site-dir").IsEmpty())
		site_dir = NormalizePath(cla.GetArg("site-dir"));
	public_host = cla.GetArg("public-host");
	if(public_host.IsEmpty())
		public_host = TrimBoth(GetEnv("WEBAPP_PUBLIC_HOST"));

	for(WebApp *app : apps) {
		String id = app->Id();
		String ports = cla.GetArg(id + "-port");
		if(!ports.IsEmpty()) {
			int value = ScanInt(ports);
			if(value < 1 || value > 65535) {
				error = "invalid --" + id + "-port " + ports + "; expected a port from 1 to 65535";
				return false;
			}
			app_ports.GetAdd(id) = value;
		}
		String user = cla.GetArg("set-" + id + "-user");
		if(!user.IsEmpty())
			set_users.GetAdd(id) = user;
	}
	return true;
}

String WebServer::SetUserFor(const String& id) const
{
	int i = set_users.Find(id);
	return i < 0 ? String() : set_users[i];
}

int WebServer::PortFor(const String& id) const
{
	int i = app_ports.Find(id);
	if(i >= 0)
		return app_ports[i];
	WebApp *app = FindApp(id);
	return app ? app->DefaultPort() : 0;
}

bool WebServer::InstallOrder(Vector<String>& ids, String& error) const
{
	ids.Clear();
	Vector<String> enabled = EnabledIds();
	for(int i = 0; i < enabled.GetCount(); i++) {
		for(int j = 0; j < i; j++)
			if(enabled[i] == enabled[j]) {
				error = "duplicate webapp id '" + enabled[i] + "'";
				return false;
			}
		if(enabled[i].IsEmpty()) {
			error = "a webapp has an empty id";
			return false;
		}
	}
	for(int i = 0; i < apps.GetCount(); i++) {
		int port_value = PortFor(enabled[i]);
		if(port_value == port) {
			error = "webapp '" + enabled[i] + "' uses port " + AsString(port_value) + ", the stock listener port";
			return false;
		}
		for(int j = 0; j < i; j++)
			if(PortFor(enabled[j]) == port_value) {
				error = "webapps '" + enabled[j] + "' and '" + enabled[i] + "' both use port " + AsString(port_value);
				return false;
			}
	}

	// requirements, checked against the enabled ids
	Vector<Vector<String>> needs;
	for(WebApp *app : apps) {
		Vector<String> req = app->Requires();
		for(const String& r : req)
			if(FindIndex(enabled, r) < 0) {
				error = "webapp '" + app->Id() + "' requires '" + r + "', which is not an enabled webapp id";
				return false;
			}
		needs.Add(pick(req));
	}

	// stable topological order: repeatedly take the first app whose requirements are placed
	Vector<bool> placed;
	placed.SetCount(apps.GetCount(), false);
	while(ids.GetCount() < apps.GetCount()) {
		bool progress = false;
		for(int i = 0; i < apps.GetCount(); i++) {
			if(placed[i])
				continue;
			bool ready = true;
			for(const String& r : needs[i])
				if(FindIndex(ids, r) < 0) {
					ready = false;
					break;
				}
			if(!ready)
				continue;
			placed[i] = true;
			ids.Add(enabled[i]);
			progress = true;
		}
		if(!progress) {
			String cycle;
			for(int i = 0; i < apps.GetCount(); i++)
				if(!placed[i])
					cycle << (cycle.IsEmpty() ? "" : ", ") << enabled[i];
			error = "requires cycle among webapps: " + cycle;
			ids.Clear();
			return false;
		}
	}
	return true;
}

bool WebServer::Compose(WebEngineHost& host, String& error)
{
	Vector<String> order;
	if(!InstallOrder(order, error))
		return false;
	if(site_dir.IsEmpty()) {
		error = "no site directory; pass --site-dir";
		return false;
	}
	if(public_host.IsEmpty()) {
		error = "no public host; pass --public-host or set WEBAPP_PUBLIC_HOST";
		return false;
	}

	TheWebEngine().SetHostPort(port);

	String engine_root = AppendFileName(site_dir, "engine");
	RealizeDirectory(engine_root);
	if(!shared_grants) {
		String grants_root = AppendFileName(engine_root, "grants");
		RealizeDirectory(grants_root);
		grants.Create("meta", grants_root);
	}

	WebAppContext ctx;
	ctx.site_dir = site_dir;
	ctx.public_host = public_host;
	ctx.grants = shared_grants ? shared_grants : ~grants;
	ctx.broker = &TheWebEngine().Broker();
	ctx.agentipc = has_agentipc ? &agentipc : nullptr;

	for(const String& id : order) {
		WebApp *app = FindApp(id);
		if(TheWebEngine().FindSite(id)) {
			error = "site '" + id + "' is already composed";
			return false;
		}
		String root = ctx.SiteRoot(id);
		RealizeDirectory(root);
		int oi = origins.Find(id);
		String origin = oi >= 0 ? origins[oi] : PublicOrigin(public_host, PortFor(id));
		EngineSite& site = TheWebEngine().AddSite(id, app->Title(), origin, root);
		site.trusted_proxy_peers <<= trusted_proxy_peers;
		app->Install(site, ctx);
		app->RegisterBroker(ctx);
		InstallSessionRoutes(site);
		if(app->ContentPolicy())
			InstallContentPolicyRoutes(site, true);
		InstallCommandConsole(site);
		host.BindSite(PortFor(id), id);
	}
	return true;
}

bool WebServer::SetUser(const String& id, const String& username, const String& password, String& error)
{
	WebApp *app = FindApp(id);
	if(!app) {
		error = "webapp '" + id + "' is not enabled";
		return false;
	}
	EngineSite *site = TheWebEngine().FindSite(id);
	if(!site) {
		error = "site '" + id + "' is not composed";
		return false;
	}
	Vector<String> roles = app->Roles();
	Vector<String> role;
	role.Add(roles.IsEmpty() ? String("member") : roles[0]);

	WebAccountUser existing;
	StoreResult r;
	if(site->Accounts().FindUserByUsername(username, existing)) {
		r = site->Accounts().SetPassword(existing.user_id, password);
	}
	else {
		String user_id;
		bool existed = false;
		r = site->Accounts().CreateUser(username, password, username, role, user_id, existed);
	}
	if(!r.ok)
		error = r.error;
	return r.ok;
}

int WebServer::Run(const Vector<String>& args)
{
	String error;
	if(!ParseArgs(args, error)) {
		Cerr() << error << "\n";
		return 2;
	}
	One<WebServerHost> host;
	host.Create();
	host->Listen(port, "0.0.0.0", 2);
	if(!Compose(*host, error)) {
		Cerr() << "Webapp composition failed: " << error << "\n";
		return 1;
	}
	if(!set_users.IsEmpty()) {
		for(int i = 0; i < set_users.GetCount(); i++) {
			String id = set_users.GetKey(i);
			String password = GetEnv(EnvNameFor(id, "_PASSWORD"));
			if(password.IsEmpty()) {
				char buffer[4096] = {};
				String prompt = "New " + id + " password: ";
				if(EVP_read_pw_string(buffer, sizeof(buffer), prompt.Begin(), 1) != 0) {
					OPENSSL_cleanse(buffer, sizeof(buffer));
					Cerr() << "Could not read the password securely.\n";
					return 1;
				}
				password = buffer;
				OPENSSL_cleanse(buffer, sizeof(buffer));
			}
			if(!SetUser(id, set_users[i], password, error)) {
				Cerr() << "Failed to write " << id << " credentials: " << error << "\n";
				return 1;
			}
			Cout() << "Configured " << id << " account '" << set_users[i] << "'\n";
		}
		fflush(stdout);
		return 0;
	}
	host->Run();
	return 0;
}

END_UPP_NAMESPACE
