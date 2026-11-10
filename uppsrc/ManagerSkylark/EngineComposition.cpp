#include "EngineComposition.h"
#include "CommandManifestRoutes.h"
NAMESPACE_UPP

EngineComposition EngineComposition::Defaults()
{
	EngineComposition cfg;
	cfg.privatehub_workspace = GetHomeDirFile("Dev");
	cfg.science_corpus = GetHomeDirFile("Dev/AnthologyPrivate/site/data");
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
	host.BindSite(cfg.privatehub_port, "privatehub");
	String ph_broker_root = AppendFileName(ph_root, "broker");
	RealizeDirectory(ph_broker_root);
	String ph_owner_error;
	if(!RegisterPrivateHubService(TheWebEngine().Broker(), "privatehub", *owned.privatehub, ph_broker_root,
	                              ph_owner_error)) {
		error = ph_owner_error;
		return false;
	}

	String sci_root = AppendFileName(engine_root, "science");
	RealizeDirectory(sci_root);
	EngineSite& sci = TheWebEngine().AddSite("science", "Science",
		Format("http://127.0.0.1:%d", cfg.science_port), sci_root);
	// Article sidecars are written next to the records (the corpus is git-backed).
	owned.science.Create(cfg.science_corpus, cfg.science_corpus);
	String grants_root = AppendFileName(engine_root, "grants");
	RealizeDirectory(grants_root);
	owned.grants.Create("meta", grants_root);
	InstallScienceSite(sci, *owned.science, ~owned.grants);
	InstallSessionRoutes(sci);
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
	host.BindSite(cfg.taskfoundry_port, "taskfoundry");
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

bool IsEngineGatewayHandler(const String& id)
{
	return id == "EngineGatewayGet" || id == "EngineGatewayPost" || id == "EngineGatewayPut" ||
	       id == "EngineGatewayPatch" || id == "EngineGatewayHead" || id == "EngineGatewayOptions";
}

END_UPP_NAMESPACE
