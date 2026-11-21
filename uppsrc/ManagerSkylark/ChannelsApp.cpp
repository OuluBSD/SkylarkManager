#include "ChannelsApp.h"

NAMESPACE_UPP

bool ChannelsApp::Install(EngineSite& site, WebAppContext& ctx, String& error)
{
	if(!ctx.agentipc) {
		error = "channels: no AgentIPC config";
		return false;
	}
	String root = ctx.SiteRoot(SiteDir());
	if(!DirectoryExists(root)) {
		error = "channels site root is not a directory: " + root;
		return false;
	}
	InstallChannelsSite(site, *ctx.agentipc);
	return true;
}

END_UPP_NAMESPACE
