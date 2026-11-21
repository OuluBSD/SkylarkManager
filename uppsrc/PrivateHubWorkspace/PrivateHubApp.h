#ifndef _PrivateHubWorkspace_PrivateHubApp_h_
#define _PrivateHubWorkspace_PrivateHubApp_h_

#include <WebAppHost/WebAppHost.h>
#include "PrivateHubWorkspace.h"

NAMESPACE_UPP

// The PrivateHub webapp for WebServer. Registered as "privatehub" by PrivateHubApp.icpp. Install
// opens the workspace backend over <site root>; RegisterBroker registers the PrivateHub broker
// service under <site root>/broker. Settings: "workspace" is the checkouts root.
class PrivateHubApp : public WebApp {
public:
	String         Id() const override { return "privatehub"; }
	String         Title() const override { return "PrivateHub"; }
	int            DefaultPort() const override { return 18211; }
	bool           Install(EngineSite& site, WebAppContext& ctx, String& error) override;
	bool           RegisterBroker(WebAppContext& ctx, String& error) override;

private:
	One<PrivateHubWorkspaceBackend> backend;
	String                          site_root;
};

END_UPP_NAMESPACE

#endif
