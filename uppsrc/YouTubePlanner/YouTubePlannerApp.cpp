#include "YouTubePlannerApp.h"
#include "YouTubePlannerSite.h"

NAMESPACE_UPP

bool YouTubePlannerApp::Install(EngineSite& site, WebAppContext& ctx, String& error)
{
	InstallYouTubePlannerSite(site);
	return true;
}

END_UPP_NAMESPACE
