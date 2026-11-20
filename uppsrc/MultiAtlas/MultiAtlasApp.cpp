#include "MultiAtlasApp.h"
#include "MultiAtlasSite.h"

NAMESPACE_UPP

void MultiAtlasApp::Install(EngineSite& site, WebAppContext& ctx)
{
	InstallMultiAtlasSite(site);
}

END_UPP_NAMESPACE
