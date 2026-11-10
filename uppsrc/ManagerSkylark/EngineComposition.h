#ifndef _reference_ManagerSkylark_EngineComposition_h_
#define _reference_ManagerSkylark_EngineComposition_h_

#include <Core/Core.h>
#include <WebEngine/Host.h>
#include <PrivateHubWorkspace/PrivateHubSite.h>
#include <Science/ScienceSite.h>
#include <Science/ScienceService.h>

NAMESPACE_UPP

// Ports and data locations for the engine sites composed into this process.
struct EngineComposition {
	int    privatehub_port = 18211;
	int    science_port = 18212;
	String privatehub_workspace; // checkouts root (default ~/Dev)
	String science_corpus;       // flat corpus + article sidecars (default ~/Dev/AnthologyPrivate/site/data)

	static EngineComposition Defaults();
};

// Backends the composed sites borrow. Owned by the application object so they
// are destroyed in main, not during static teardown after the engine is gone.
struct EngineSiteBackends {
	One<PrivateHubWorkspaceBackend> privatehub;
	One<ScienceCorpus>              science;
	One<WebGrants>                  grants;
};

// Creates the PrivateHub and Science engine sites, binds their listener ports,
// registers their broker services, and creates the grant store under
// <site_dir>/engine. Called once, before the app runs, for the real server.
void ComposeEngineSites(WebEngineHost& host, EngineSiteBackends& owned, const String& site_dir,
                        const EngineComposition& cfg);

// Creates or updates a PrivateHub account (role "member") on the composed
// PrivateHub site. Requires ComposeEngineSites to have run.
bool SetPrivateHubPassword(const String& username, const String& password, String& error);

// Creates or resets a Science account with the science-author role, which is
// what authoring needs. Requires ComposeEngineSites to have run.
bool SetScienceUserPassword(const String& username, const String& password, String& error);

// Engine gateway handler ids: the SKYLARK catch-alls in WebEngine/Host.cpp.
bool IsEngineGatewayHandler(const String& handler_id);

END_UPP_NAMESPACE

#endif
