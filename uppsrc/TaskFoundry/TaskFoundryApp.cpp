#include "TaskFoundryApp.h"
#include "TaskFoundrySite.h"

NAMESPACE_UPP

bool TaskFoundryApp::Install(EngineSite& site, WebAppContext& ctx, String& error)
{
	if(!ctx.broker) {
		error = "taskfoundry: no broker to call";
		return false;
	}
	if(!ctx.grants) {
		error = "taskfoundry: no shared grant store";
		return false;
	}
	String root = ctx.SiteRoot(SiteDir());
	if(!DirectoryExists(root)) {
		error = "taskfoundry site root is not a directory: " + root;
		return false;
	}
	service.Create(*ctx.broker, root);
	InstallTaskFoundrySite(site, *service, ctx.grants);
	return true;
}

END_UPP_NAMESPACE
