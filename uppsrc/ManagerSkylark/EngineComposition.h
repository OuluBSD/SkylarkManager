#ifndef _reference_ManagerSkylark_EngineComposition_h_
#define _reference_ManagerSkylark_EngineComposition_h_

#include <Core/Core.h>
#include <WebEngine/Host.h>

NAMESPACE_UPP

// Ports and data locations for the engine sites composed into this process.
struct EngineComposition {
	int    privatehub_port = 18211;
	int    science_port = 18212;
	String privatehub_workspace; // checkouts root (default ~/Dev)
	String science_corpus;       // flat corpus + article sidecars (default ~/Dev/AnthologyPrivate/site/data)

	static EngineComposition Defaults();
};

// Creates the PrivateHub and Science engine sites, binds their listener ports,
// registers their broker services, and creates the grant store under
// <site_dir>/engine. Called once, before the app runs, for the real server.
void ComposeEngineSites(WebEngineHost& host, const String& site_dir, const EngineComposition& cfg);

// Engine gateway handler ids: the SKYLARK catch-alls in WebEngine/Host.cpp.
bool IsEngineGatewayHandler(const String& handler_id);

END_UPP_NAMESPACE

#endif
