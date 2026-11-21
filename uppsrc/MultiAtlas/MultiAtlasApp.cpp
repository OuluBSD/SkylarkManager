#include "MultiAtlasApp.h"
#include "MultiAtlasSite.h"

NAMESPACE_UPP

bool MultiAtlasApp::Install(EngineSite& site, WebAppContext& ctx, String& error)
{
	InstallMultiAtlasSite(site);
	return true;
}

END_UPP_NAMESPACE
