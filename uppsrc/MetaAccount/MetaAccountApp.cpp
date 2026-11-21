#include "MetaAccountApp.h"

NAMESPACE_UPP

bool MetaAccountApp::Install(EngineSite& s, WebAppContext& ctx, String& error)
{
	if(!ctx.grants) {
		error = "meta: no shared grant store";
		return false;
	}
	String root = ctx.SiteRoot(SiteDir());
	if(!DirectoryExists(root)) {
		error = "meta site root is not a directory: " + root;
		return false;
	}
	site = &s;
	grants = ctx.grants;
	InstallMetaAccountSite(s, *grants);
	return true;
}

bool MetaAccountApp::RegisterBroker(WebAppContext& ctx, String& error)
{
	if(!site || !grants) {
		error = "meta broker: the site is not installed";
		return false;
	}
	RegisterMetaAccountService(Id(), site->Accounts(), *grants);
	return true;
}

END_UPP_NAMESPACE
