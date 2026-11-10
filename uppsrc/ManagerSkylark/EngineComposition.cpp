#include "EngineComposition.h"
NAMESPACE_UPP

EngineComposition EngineComposition::Defaults()
{
	EngineComposition cfg;
	cfg.privatehub_workspace = GetHomeDirFile("Dev");
	cfg.science_corpus = GetHomeDirFile("Dev/AnthologyPrivate/site/data");
	return cfg;
}

void ComposeEngineSites(WebEngineHost& host, EngineSiteBackends& owned, const String& site_dir, const EngineComposition& cfg)
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
}

bool SetPrivateHubPassword(const String& username, const String& password, String& error)
{
	EngineSite *site = TheWebEngine().FindSite("privatehub");
	if(!site) {
		error = "privatehub site is not composed";
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
	Vector<String> roles;
	roles.Add("member");
	StoreResult r = site->Accounts().CreateUser(username, password, username, roles, user_id, existed);
	if(!r.ok)
		error = r.error;
	return r.ok;
}

bool IsEngineGatewayHandler(const String& id)
{
	return id == "EngineGatewayGet" || id == "EngineGatewayPost" || id == "EngineGatewayPut" ||
	       id == "EngineGatewayPatch" || id == "EngineGatewayHead" || id == "EngineGatewayOptions";
}

END_UPP_NAMESPACE
