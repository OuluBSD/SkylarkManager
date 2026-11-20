#ifndef _YouTubePlanner_YouTubePlannerApp_h_
#define _YouTubePlanner_YouTubePlannerApp_h_

#include <WebAppHost/WebAppHost.h>

NAMESPACE_UPP

// The YouTubePlanner webapp for WebServer. Registered as "youtubeplanner" by YouTubePlannerApp.icpp; the site
// itself is InstallYouTubePlannerSite (YouTubePlannerSite.h).
class YouTubePlannerApp : public WebApp {
public:
	String         Id() const override { return "youtubeplanner"; }
	String         Title() const override { return "YouTubePlanner"; }
	int            DefaultPort() const override { return 18218; }
	bool           ContentPolicy() const override { return false; }   // the stub has no content policy routes
	void           Install(EngineSite& site, WebAppContext& ctx) override;
};

END_UPP_NAMESPACE

#endif
