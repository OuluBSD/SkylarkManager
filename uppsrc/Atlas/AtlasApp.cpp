#include "AtlasApp.h"
#include "AtlasSite.h"

NAMESPACE_UPP

void AtlasApp::Install(EngineSite& site, WebAppContext& ctx)
{
	InstallAtlasSite(site);
}

END_UPP_NAMESPACE
