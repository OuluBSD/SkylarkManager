#include "EngineComposition.h"
#include <PrivateHubWorkspace/PrivateHubSite.h>
#include <Science/ScienceSite.h>
#include <Science/ScienceService.h>

NAMESPACE_UPP

static PrivateHubWorkspaceBackend *s_privatehub = NULL;
static ScienceCorpus              *s_science = NULL;
static WebGrants                  *s_grants = NULL;

EngineComposition EngineComposition::Defaults()
{
	EngineComposition cfg;
	cfg.privatehub_workspace = GetHomeDirFile("Dev");
	cfg.science_corpus = GetHomeDirFile("Dev/AnthologyPrivate/site/data");
	return cfg;
}

void ComposeEngineSites(WebEngineHost& host, const String& site_dir, const EngineComposition& cfg)
{
	String engine_root = AppendFileName(site_dir, "engine");
	RealizeDirectory(engine_root);

	String ph_root = AppendFileName(engine_root, "privatehub");
	RealizeDirectory(ph_root);
	EngineSite& ph = TheWebEngine().AddSite("privatehub", "PrivateHub",
		Format("http://127.0.0.1:%d", cfg.privatehub_port), ph_root);
	s_privatehub = new PrivateHubWorkspaceBackend(cfg.privatehub_workspace, ph_root);
	InstallPrivateHubSite(ph, *s_privatehub);
	host.BindSite(cfg.privatehub_port, "privatehub");

	String sci_root = AppendFileName(engine_root, "science");
	RealizeDirectory(sci_root);
	EngineSite& sci = TheWebEngine().AddSite("science", "Science",
		Format("http://127.0.0.1:%d", cfg.science_port), sci_root);
	// Article sidecars are written next to the records (the corpus is git-backed).
	s_science = new ScienceCorpus(cfg.science_corpus, cfg.science_corpus);
	String grants_root = AppendFileName(engine_root, "grants");
	RealizeDirectory(grants_root);
	s_grants = new WebGrants("meta", grants_root);
	InstallScienceSite(sci, *s_science, s_grants);
	host.BindSite(cfg.science_port, "science");
	TheWebEngine().Broker().Register("science", new ScienceService(*s_science));
}

bool IsEngineGatewayHandler(const String& id)
{
	return id == "EngineGatewayGet" || id == "EngineGatewayPost" || id == "EngineGatewayPut" ||
	       id == "EngineGatewayPatch" || id == "EngineGatewayHead" || id == "EngineGatewayOptions";
}

END_UPP_NAMESPACE
