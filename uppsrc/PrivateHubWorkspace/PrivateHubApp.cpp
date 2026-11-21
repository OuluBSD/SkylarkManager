#include "PrivateHubApp.h"
#include "PrivateHubSite.h"
#include "PrivateHubService.h"

NAMESPACE_UPP

bool PrivateHubApp::Install(EngineSite& site, WebAppContext& ctx, String& error)
{
	site_root = ctx.SiteRoot(SiteDir());
	if(!DirectoryExists(site_root)) {
		error = "privatehub site root is not a directory: " + site_root;
		return false;
	}
	backend.Create(ctx.Setting("workspace"), site_root);
	InstallPrivateHubSite(site, *backend);
	return true;
}

bool PrivateHubApp::RegisterBroker(WebAppContext& ctx, String& error)
{
	if(!backend) {
		error = "privatehub broker: the workspace backend is not installed";
		return false;
	}
	String broker_root = AppendFileName(site_root, "broker");
	RealizeDirectory(broker_root);
	return RegisterPrivateHubService(*ctx.broker, Id(), *backend, broker_root, error);
}

END_UPP_NAMESPACE
