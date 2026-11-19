#include "MultiAtlasSite.h"
#include "MultiAtlasApi.h"
#include "MultiAtlasRoadmapPages.h"

NAMESPACE_UPP

void InstallMultiAtlasSite(EngineSite& site)
{
	InstallMultiAtlasApi(site);
	InstallMultiAtlasRoadmapPages(site);
}

END_UPP_NAMESPACE
