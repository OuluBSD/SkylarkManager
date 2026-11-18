#include "MultiAtlasSite.h"
#include <WebEngine/Request.h>

NAMESPACE_UPP

static void MultiAtlasStatus(WebRequest& r)
{
	ValueMap status;
	status.Add("app", "MultiAtlas");
	status.Add("status", "bootstrap OK");
	status.Add("version", "0.1.0");
	r.Json(status);
}

void InstallMultiAtlasSite(EngineSite& site)
{
	site.Router().Add("", MultiAtlasStatus);
}

END_UPP_NAMESPACE
