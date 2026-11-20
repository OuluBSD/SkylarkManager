#ifndef _MultiAtlas_MultiAtlasApp_h_
#define _MultiAtlas_MultiAtlasApp_h_

#include <WebAppHost/WebAppHost.h>

NAMESPACE_UPP

// The MultiAtlas webapp for WebServer. Registered as "multiatlas" by MultiAtlasApp.icpp; the site
// itself is InstallMultiAtlasSite (MultiAtlasSite.h).
class MultiAtlasApp : public WebApp {
public:
	String         Id() const override { return "multiatlas"; }
	String         Title() const override { return "MultiAtlas"; }
	int            DefaultPort() const override { return 18219; }
	Vector<String> Roles() const override { Vector<String> r; r.Add("admin"); return r; }
	void           Install(EngineSite& site, WebAppContext& ctx) override;
};

END_UPP_NAMESPACE

#endif
