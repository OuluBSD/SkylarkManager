#ifndef _ManagerSkylark_ChannelsApp_h_
#define _ManagerSkylark_ChannelsApp_h_

#include <WebAppHost/WebAppHost.h>
#include <Channels/ChannelsSite.h>

NAMESPACE_UPP

// The Channels webapp for WebServer: the read-only frontend over the AgentIPC node. It lives in the
// ManagerSkylark package, not in Channels, because WebAppHost depends on Channels (for AgentIpcConfig),
// so Channels cannot depend on WebAppHost. Registered as "channels" by ChannelsApp.icpp. Install gives the
// site the AgentIpcConfig that WebServer::SetAgentIpc holds; the site has no broker service and no grants.
class ChannelsApp : public WebApp {
public:
	String         Id() const override { return "channels"; }
	String         Title() const override { return "Channels"; }
	int            DefaultPort() const override { return 18215; }
	bool           Install(EngineSite& site, WebAppContext& ctx, String& error) override;
};

// Synthetic checks for the Channels adapter, run from the --test suite. scratch_dir is a directory the
// checks may create and remove.
bool RunChannelsAppSelfTests(const String& scratch_dir);

END_UPP_NAMESPACE

#endif
