#ifndef _Atlas_AtlasApp_h_
#define _Atlas_AtlasApp_h_

#include <WebAppHost/WebAppHost.h>

NAMESPACE_UPP

// The Atlas webapp for WebServer. Registered as "atlas" by AtlasApp.icpp; the site
// itself is InstallAtlasSite (AtlasSite.h).
class AtlasApp : public WebApp {
public:
	String         Id() const override { return "atlas"; }
	String         Title() const override { return "Atlas"; }
	int            DefaultPort() const override { return 18217; }
	Vector<String> Roles() const override { Vector<String> r; r.Add("admin"); return r; }
	void           Install(EngineSite& site, WebAppContext& ctx) override;
};

END_UPP_NAMESPACE

#endif
