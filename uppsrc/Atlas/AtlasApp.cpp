#include "AtlasApp.h"
#include "AtlasSite.h"

NAMESPACE_UPP

bool AtlasApp::Install(EngineSite& site, WebAppContext& ctx, String& error)
{
	InstallAtlasSite(site);
	return true;
}

END_UPP_NAMESPACE
