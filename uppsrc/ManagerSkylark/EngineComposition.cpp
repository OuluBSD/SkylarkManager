#include "EngineComposition.h"
#include "CommandManifestRoutes.h"
#ifdef flagPOSIX
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
NAMESPACE_UPP

EngineComposition EngineComposition::Defaults()
{
	EngineComposition cfg;
	cfg.privatehub_workspace = GetHomeDirFile("Dev");
	cfg.science_corpus = GetHomeDirFile("Dev/AnthologyPrivate/site/data");
	cfg.agentipc_python = GetHomeDirFile(".venvs/agentipc/bin/python");
	cfg.agentipc_client = GetHomeDirFile("OuluLinux/AgentIPC/bin/agentipc");
	cfg.agentipc_home = GetHomeDirFile(".agentipc-skylark");
	return cfg;
}

static bool IsValidExternalOrigin(const String& origin)
{
	String lower = ToLower(origin);
	int prefix = lower.StartsWith("https://") ? 8 : lower.StartsWith("http://") ? 7 : 0;
	if(!prefix || origin.GetCount() <= prefix)
		return false;
	for(int i = prefix; i < origin.GetCount(); i++) {
		int c = (byte)origin[i];
		if(!(IsAlpha(c) || IsDigit(c) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']'))
			return false;
	}
	return true;
}

static bool IsLoopbackHost(const String& host)
{
	String h = ToLower(host);
	return h == "localhost" || h.EndsWith(".localhost") || h == "::1" || h == "[::1]" ||
	       h == "0.0.0.0" || h.StartsWith("127.");
}

static bool IsPrivateIpv4(const String& address)
{
	if(address.StartsWith("192.168.") || address.StartsWith("10."))
		return true;
	if(!address.StartsWith("172."))
		return false;
	Vector<String> parts = Split(address, '.');
	return parts.GetCount() > 1 && ScanInt(parts[1]) >= 16 && ScanInt(parts[1]) <= 31;
}

// The first up IPv4 address that is not loopback, preferring private (LAN) ranges.
static bool DetectLanAddress(String& address)
{
#ifdef flagPOSIX
	struct ifaddrs *list = nullptr;
	if(getifaddrs(&list) != 0)
		return false;
	String fallback;
	for(struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next) {
		if(!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET || !(ifa->ifa_flags & IFF_UP))
			continue;
		char buf[INET_ADDRSTRLEN];
		const struct sockaddr_in *sin = (const struct sockaddr_in *)ifa->ifa_addr;
		if(!inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)))
			continue;
		String candidate = buf;
		if(IsLoopbackHost(candidate))
			continue;
		if(IsPrivateIpv4(candidate)) {
			address = candidate;
			freeifaddrs(list);
			return true;
		}
		if(fallback.IsEmpty())
			fallback = candidate;
	}
	freeifaddrs(list);
	address = fallback;
	return !address.IsEmpty();
#else
	return false;
#endif
}

bool ResolvePublicHost(const String& flag_value, String& host, String& error)
{
	host = flag_value;
	if(host.IsEmpty())
		host = GetEnv("MANAGERSKYLARK_PUBLIC_HOST");
	if(host.IsEmpty() && !DetectLanAddress(host)) {
		error = "no LAN address found; pass --public-host <address> or set MANAGERSKYLARK_PUBLIC_HOST";
		return false;
	}
	if(IsLoopbackHost(host)) {
		error = "public host '" + host + "' is loopback; pass this machine's LAN or public address";
		return false;
	}
	if(!IsValidExternalOrigin("http://" + host + ":1")) {
		error = "invalid public host '" + host + "'; expected a host name or IP address";
		return false;
	}
	return true;
}

// Builds http://host:port; IPv6 literals are bracketed.
static String PublicOrigin(const String& host, int port)
{
	String authority = host.Find(':') >= 0 ? "[" + host + "]" : host;
	return "http://" + authority + ":" + AsString(port);
}

static bool ResolveExternalOrigins(VectorMap<String, String>& origins, const EngineComposition& cfg, String& error)
{
	struct SiteOriginName { const char *id; };
	static const SiteOriginName sites[] = {
		{ "privatehub" }, { "meta" }, { "science" }, { "taskfoundry" },
		{ "channels" }, { "financials" }, { "atlas" }, { "youtubeplanner" }, { "company" }, { "multiatlas" },
	};
	const int ports[] = { cfg.privatehub_port, cfg.meta_port, cfg.science_port, cfg.taskfoundry_port,
	                      cfg.channels_port, cfg.financials_port, cfg.atlas_port, cfg.youtubeplanner_port,
	                      cfg.company_port, cfg.multiatlas_port };
	for(int i = 0; i < __countof(sites); i++) {
		String id = sites[i].id;
		String env = "MANAGERSKYLARK_ORIGIN_" + ToUpper(id);
		String origin = GetEnv(env);
		if(origin.IsEmpty())
			origin = PublicOrigin(cfg.public_host, ports[i]);
		if(!IsValidExternalOrigin(origin)) {
			error = "invalid external origin in " + env + "; expected http(s)://host[:port] without path";
			return false;
		}
		origins.Add(id, origin);
	}
	return true;
}

bool ResolvePortalExternalOrigin(const String& public_host, int port, String& origin, String& error)
{
	origin = GetEnv("MANAGERSKYLARK_ORIGIN_PORTAL");
	if(origin.IsEmpty())
		origin = PublicOrigin(public_host, port);
	if(!IsValidExternalOrigin(origin)) {
		error = "invalid external origin in MANAGERSKYLARK_ORIGIN_PORTAL; expected http(s)://host[:port] without path";
		return false;
	}
	return true;
}

static bool IsIpv4Literal(const String& address)
{
	Vector<String> octets = Split(address, '.');
	if(octets.GetCount() != 4)
		return false;
	for(const String& octet : octets) {
		if(octet.IsEmpty() || octet.GetCount() > 3)
			return false;
		int value = 0;
		for(int i = 0; i < octet.GetCount(); i++) {
			if(!IsDigit((byte)octet[i]))
				return false;
			value = value * 10 + octet[i] - '0';
		}
		if(value > 255)
			return false;
	}
	return true;
}

static bool ResolveTrustedProxyPeers(Vector<String>& peers, String& error)
{
	String configured = TrimBoth(GetEnv("MANAGERSKYLARK_TRUSTED_PROXY_PEERS"));
	if(configured.IsEmpty())
		return true;
	Vector<String> entries = Split(configured, ',');
	for(String peer : entries) {
		peer = TrimBoth(peer);
		if(!IsIpv4Literal(peer)) {
			error = "MANAGERSKYLARK_TRUSTED_PROXY_PEERS accepts exact IPv4 peer addresses only";
			return false;
		}
		bool duplicate = false;
		for(const String& existing : peers)
			if(existing == peer) {
				duplicate = true;
				break;
			}
		if(!duplicate)
			peers.Add(peer);
	}
	return true;
}

bool IsTrustedManagerProxyPeer(const String& peer)
{
	Vector<String> peers;
	String error;
	if(!ResolveTrustedProxyPeers(peers, error)) return false;
	return FindIndex(peers, peer) >= 0;
}

// The Company module, as the broker sees it. The host owns the adapter; the
// module owns the store. The host never reaches the store, only Handle().
class CompanyBrokerSite : public IBrokerService {
public:
	explicit CompanyBrokerSite(CompanyComposedModule& module) : module(module) {}
	String AuditOperation(const String& op) const override {
		if(op == COMPANY_BROKER_CREATE_DRAFT || op == COMPANY_BROKER_STATUS ||
		   op == COMPANY_BROKER_LEDGER_SUMMARY || op == COMPANY_BROKER_LEDGER_RECENT ||
		   op == COMPANY_BROKER_LEDGER_BALANCES || op == COMPANY_BROKER_CONTRACTS_LIST)
			return op;
		return "unclassified";
	}

	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override {
		CompanyBrokerResult r = module.Handle(call.op, ctx.scopes, ctx.caller_id, call.payload);
		BrokerResult out;
		out.ok = r.ok;
		out.status = r.status;
		out.payload = r.payload;
		out.error = r.error;
		return out;
	}

private:
	CompanyComposedModule& module;
};

bool ComposeEngineSites(WebEngineHost& host, EngineSiteBackends& owned, const String& site_dir,
                        const EngineComposition& cfg, String& error)
{
	TheWebEngine().SetMetaSso(cfg.enable_meta_sso);
	VectorMap<String, String> origins;
	if(!ResolveExternalOrigins(origins, cfg, error))
		return false;
	Vector<String> trusted_proxy_peers;
	if(!ResolveTrustedProxyPeers(trusted_proxy_peers, error))
		return false;
	String engine_root = AppendFileName(site_dir, "engine");
	RealizeDirectory(engine_root);


	// The shared meta grant store (Science, TaskFoundry and the meta site use
	// it). Created first: the meta and science installs below need it.
	String grants_root = AppendFileName(engine_root, "grants");
	RealizeDirectory(grants_root);
	owned.grants.Create("meta", grants_root);

	// The meta-account site: ecosystem identities and the consent page. Its
	// accounts are what client sites (TaskFoundry) link to for consent grants.
	String meta_root = AppendFileName(engine_root, "meta");
	RealizeDirectory(meta_root);
	EngineSite& meta = TheWebEngine().AddSite("meta", "MetaAccount",
		origins.Get("meta"), meta_root);
	meta.trusted_proxy_peers <<= trusted_proxy_peers;
	InstallMetaAccountSite(meta, *owned.grants);
	InstallSessionRoutes(meta);
	InstallCommandConsole(meta);
	RegisterMetaAccountService("meta", meta.Accounts(), *owned.grants);
	host.BindSite(cfg.meta_port, "meta");

	String sci_root = AppendFileName(engine_root, "science");
	RealizeDirectory(sci_root);
	EngineSite& sci = TheWebEngine().AddSite("science", "Science",
		origins.Get("science"), sci_root);
	sci.trusted_proxy_peers <<= trusted_proxy_peers;
	// Article sidecars are written next to the records (the corpus is git-backed).
	owned.science.Create(cfg.science_corpus, cfg.science_corpus);
	InstallScienceSite(sci, *owned.science, ~owned.grants);
	InstallSessionRoutes(sci);
	InstallContentPolicyRoutes(sci, true);
	InstallCommandConsole(sci);
	host.BindSite(cfg.science_port, "science");
	// The broker takes ownership of the service it is given.
	TheWebEngine().Broker().Register("science", new ScienceService(*owned.science));

	// Company: the module owns its store under its own root. The host only
	// registers the broker surface, so Company data is reached through the broker.
	String company_root = cfg.company_root.IsEmpty() ? AppendFileName(engine_root, "company") : cfg.company_root;
	owned.company.Create();
	if(!owned.company->Open(company_root, cfg.company_repos, error))
		return false;
	TheWebEngine().Broker().Register("company", new CompanyBrokerSite(*owned.company));

	String tf_root = AppendFileName(engine_root, "taskfoundry");
	RealizeDirectory(tf_root);
	EngineSite& tf = TheWebEngine().AddSite("taskfoundry", "TaskFoundry",
		origins.Get("taskfoundry"), tf_root);
	tf.trusted_proxy_peers <<= trusted_proxy_peers;
	owned.taskfoundry.Create(TheWebEngine().Broker(), tf_root);
	InstallTaskFoundrySite(tf, *owned.taskfoundry, ~owned.grants);
	InstallSessionRoutes(tf);
	InstallContentPolicyRoutes(tf, true);
	InstallCommandConsole(tf);
	host.BindSite(cfg.taskfoundry_port, "taskfoundry");

	// The channels site: the read-only frontend over the AgentIPC node. It
	// reaches the node only through the argv-only client; the node identity's
	// password is read once from the environment and held in memory.
	String ch_root = AppendFileName(engine_root, "channels");
	RealizeDirectory(ch_root);
	AgentIpcConfig ipc;
	ipc.direct = cfg.agentipc_direct;
	ipc.server = cfg.agentipc_server;
	ipc.user = cfg.agentipc_user;
	if(!cfg.agentipc_python.IsEmpty() || !cfg.agentipc_client.IsEmpty()) {
		ipc.client_argv.Add(cfg.agentipc_python.IsEmpty()
			? GetHomeDirFile(".venvs/agentipc/bin/python") : cfg.agentipc_python);
		ipc.client_argv.Add(cfg.agentipc_client.IsEmpty()
			? GetHomeDirFile("OuluLinux/AgentIPC/bin/agentipc") : cfg.agentipc_client);
	}
	else
		ipc.client_argv = AgentIpcDefaultClientArgv();
	ipc.records_path = AppendFileName(cfg.agentipc_home, "mesh/records.jsonl");
	ipc.password = GetEnv("MANAGERSKYLARK_AGENTIPC_PASSWORD");
	EngineSite& ch = TheWebEngine().AddSite("channels", "Channels",
		origins.Get("channels"), ch_root);
	ch.trusted_proxy_peers <<= trusted_proxy_peers;
	InstallChannelsSite(ch, ipc);
	InstallSessionRoutes(ch);
	InstallContentPolicyRoutes(ch, true);
	InstallCommandConsole(ch);
	host.BindSite(cfg.channels_port, "channels");

	// The Company webapp: proposals, work items, reviews and the ledger over the
	// same store the broker serves. Its session and console come from the engine.
	String company_site_root = AppendFileName(engine_root, "company-site");
	RealizeDirectory(company_site_root);
	EngineSite& co = TheWebEngine().AddSite("company", "Company",
		origins.Get("company"), company_site_root);
	co.trusted_proxy_peers <<= trusted_proxy_peers;
	InstallCompanySite(co, owned.company->Store());
	InstallSessionRoutes(co);
	InstallCommandConsole(co);
	host.BindSite(cfg.company_port, "company");
	RegisterComposedBrokerCommands();
	return true;
}

// The simple sites (Atlas, MultiAtlas, Financials, YouTubePlanner) are registered
// webapps (their <Name>App.icpp). WebServer composes them on the same ports,
// origins and trusted peers as ComposeEngineSites did, and on the shared grant store.
// The backends the sites use stay in their own packages.
bool ComposeWebApps(WebEngineHost& host, EngineSiteBackends& owned, int stock_port, const String& site_dir,
                    const EngineComposition& cfg, String& error)
{
	VectorMap<String, String> origins;
	if(!ResolveExternalOrigins(origins, cfg, error))
		return false;
	Vector<String> trusted_proxy_peers;
	if(!ResolveTrustedProxyPeers(trusted_proxy_peers, error))
		return false;

	owned.webapps.Create();
	WebServer& ws = *owned.webapps;
	ws.port = stock_port;
	ws.site_dir = site_dir;
	ws.public_host = cfg.public_host;
	ws.trusted_proxy_peers <<= trusted_proxy_peers;
	for(const char *id : { "privatehub", "atlas", "multiatlas", "financials", "youtubeplanner" })
		ws.origins.GetAdd(id) = origins.Get(id);
	ws.SetAppPort("privatehub", cfg.privatehub_port);
	ws.SetSetting("privatehub", "workspace", cfg.privatehub_workspace);
	ws.SetAppPort("atlas", cfg.atlas_port);
	ws.SetAppPort("multiatlas", cfg.multiatlas_port);
	ws.SetAppPort("financials", cfg.financials_port);
	ws.SetAppPort("youtubeplanner", cfg.youtubeplanner_port);
	ws.UseGrants(~owned.grants);
	for(const char *id : { "privatehub", "atlas", "multiatlas", "financials", "youtubeplanner" })
		if(!ws.Enable(id, error))
			return false;
	return ws.Compose(host, error);
}

// Creates an account on a composed site, or resets its password when it exists.
static bool SetEngineUser(const char *site_id, const String& username, const String& password,
                          const Vector<String>& roles, String& error)
{
	EngineSite *site = TheWebEngine().FindSite(site_id);
	if(!site) {
		error = String(site_id) + " site is not composed";
		return false;
	}
	WebAccountUser existing;
	if(site->Accounts().FindUserByUsername(username, existing)) {
		StoreResult r = site->Accounts().SetPassword(existing.user_id, password);
		if(!r.ok)
			error = r.error;
		return r.ok;
	}
	String user_id;
	bool existed = false;
	StoreResult r = site->Accounts().CreateUser(username, password, username, roles, user_id, existed);
	if(!r.ok)
		error = r.error;
	return r.ok;
}

bool SetPrivateHubPassword(const String& username, const String& password, String& error, const String& role)
{
	Vector<String> roles;
	roles.Add(role);
	return SetEngineUser("privatehub", username, password, roles, error);
}

bool SetScienceUserPassword(const String& username, const String& password, String& error)
{
	Vector<String> roles;
	roles.Add(SCIENCE_AUTHOR_ROLE);
	return SetEngineUser("science", username, password, roles, error);
}

bool SetTaskFoundryPassword(const String& username, const String& password, String& error)
{
	Vector<String> roles;
	roles.Add("member");
	return SetEngineUser("taskfoundry", username, password, roles, error);
}

bool SetMetaAccountPassword(const String& username, const String& password, String& error)
{
	Vector<String> roles;
	return SetEngineUser("meta", username, password, roles, error);
}

bool SetChannelsPassword(const String& username, const String& password, String& error)
{
	Vector<String> roles;
	return SetEngineUser("channels", username, password, roles, error);
}

bool SetFinancialsPassword(const String& username, const String& role,
                           const String& password, String& error)
{
	Vector<String> roles;
	if(!role.IsEmpty())
		roles.Add(role);
	return SetEngineUser("financials", username, password, roles, error);
}

bool SetAtlasPassword(const String& username, const String& role, const String& password, String& error)
{
	Vector<String> roles;
	if(!role.IsEmpty())
		roles.Add(role);
	return SetEngineUser("atlas", username, password, roles, error);
}

bool SetMultiAtlasPassword(const String& username, const String& role, const String& password, String& error)
{
	Vector<String> roles;
	if(!role.IsEmpty()) roles.Add(role);
	return SetEngineUser("multiatlas", username, password, roles, error);
}

bool SetCompanyPassword(EngineSiteBackends& owned, const String& username, const String& role,
                        const String& password, String& error)
{
	if(!owned.company) {
		error = "company module is not composed";
		return false;
	}
	Vector<String> roles;
	roles.Add(role);
	return SetCompanyUser(owned.company->Store(), username, password, roles, error);
}

bool IsEngineGatewayHandler(const String& id)
{
	return id == "EngineGatewayGet" || id == "EngineGatewayPost" || id == "EngineGatewayPut" ||
	       id == "EngineGatewayPatch" || id == "EngineGatewayHead" || id == "EngineGatewayOptions";
}

END_UPP_NAMESPACE
