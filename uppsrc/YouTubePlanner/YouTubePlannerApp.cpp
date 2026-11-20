#include "YouTubePlannerApp.h"
#include "YouTubePlannerSite.h"

NAMESPACE_UPP

void YouTubePlannerApp::Install(EngineSite& site, WebAppContext& ctx)
{
	InstallYouTubePlannerSite(site);
}

END_UPP_NAMESPACE
