#include "EngineComposition.h"
#include "CommandManifestRoutes.h"
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

// The Company module, as the broker sees it. The host owns the adapter; the
// module owns the store. The host never reaches the store, only Handle().
class CompanyBrokerSite : public IBrokerService {
public:
	explicit CompanyBrokerSite(CompanyComposedModule& module) : module(module) {}

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
	String engine_root = AppendFileName(site_dir, "engine");
	RealizeDirectory(engine_root);

	String ph_root = AppendFileName(engine_root, "privatehub");
	RealizeDirectory(ph_root);
	EngineSite& ph = TheWebEngine().AddSite("privatehub", "PrivateHub",
		Format("http://127.0.0.1:%d", cfg.privatehub_port), ph_root);
	owned.privatehub.Create(cfg.privatehub_workspace, ph_root);
	InstallPrivateHubSite(ph, *owned.privatehub);
	InstallSessionRoutes(ph);
	InstallCommandConsole(ph);
	host.BindSite(cfg.privatehub_port, "privatehub");
	String ph_broker_root = AppendFileName(ph_root, "broker");
	RealizeDirectory(ph_broker_root);
	String ph_owner_error;
	if(!RegisterPrivateHubService(TheWebEngine().Broker(), "privatehub", *owned.privatehub, ph_broker_root,
	                              ph_owner_error)) {
		error = ph_owner_error;
		return false;
	}

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
		Format("http://127.0.0.1:%d", cfg.meta_port), meta_root);
	InstallMetaAccountSite(meta, *owned.grants);
	InstallSessionRoutes(meta);
	InstallCommandConsole(meta);
	RegisterMetaAccountService("meta", meta.Accounts(), *owned.grants);
	host.BindSite(cfg.meta_port, "meta");

	String sci_root = AppendFileName(engine_root, "science");
	RealizeDirectory(sci_root);
	EngineSite& sci = TheWebEngine().AddSite("science", "Science",
		Format("http://127.0.0.1:%d", cfg.science_port), sci_root);
	// Article sidecars are written next to the records (the corpus is git-backed).
	owned.science.Create(cfg.science_corpus, cfg.science_corpus);
	InstallScienceSite(sci, *owned.science, ~owned.grants);
	InstallSessionRoutes(sci);
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
		Format("http://127.0.0.1:%d", cfg.taskfoundry_port), tf_root);
	owned.taskfoundry.Create(TheWebEngine().Broker(), tf_root);
	InstallTaskFoundrySite(tf, *owned.taskfoundry, ~owned.grants);
	InstallSessionRoutes(tf);
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
		Format("http://127.0.0.1:%d", cfg.channels_port), ch_root);
	InstallChannelsSite(ch, ipc);
	InstallSessionRoutes(ch);
	InstallCommandConsole(ch);
	host.BindSite(cfg.channels_port, "channels");

	// The financials site: the read-only window over the Company money flows,
	// through the broker only. Roles: viewer (aggregates) and finance (rows).
	String fin_root = AppendFileName(engine_root, "financials");
	RealizeDirectory(fin_root);
	EngineSite& fin = TheWebEngine().AddSite("financials", "Financials",
		Format("http://127.0.0.1:%d", cfg.financials_port), fin_root);
	InstallFinancialsSite(fin);
	InstallSessionRoutes(fin);
	InstallCommandConsole(fin);
	host.BindSite(cfg.financials_port, "financials");

	// The atlas site: the topological map of the project (task 029). Its own
	// accounts; the admin role edits, every other role reads.
	String atlas_root = AppendFileName(engine_root, "atlas");
	RealizeDirectory(atlas_root);
	EngineSite& atl = TheWebEngine().AddSite("atlas", "Atlas",
		Format("http://127.0.0.1:%d", cfg.atlas_port), atlas_root);
	InstallAtlasSite(atl);
	InstallSessionRoutes(atl);
	InstallCommandConsole(atl);
	host.BindSite(cfg.atlas_port, "atlas");
	RegisterComposedBrokerCommands();
	return true;
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

bool IsEngineGatewayHandler(const String& id)
{
	return id == "EngineGatewayGet" || id == "EngineGatewayPost" || id == "EngineGatewayPut" ||
	       id == "EngineGatewayPatch" || id == "EngineGatewayHead" || id == "EngineGatewayOptions";
}

END_UPP_NAMESPACE
