#ifndef _MetaAccount_MetaAccountApp_h_
#define _MetaAccount_MetaAccountApp_h_

#include <WebAppHost/WebAppHost.h>
#include "MetaAccountSite.h"

NAMESPACE_UPP

// The MetaAccount webapp for WebServer. Registered as "meta" by MetaAccountApp.icpp. Install adds the
// meta-account routes over the shared grant store; RegisterBroker registers the meta broker service on
// the site's accounts and grants. The site has no content policy routes, as before.
class MetaAccountApp : public WebApp {
public:
	String         Id() const override { return "meta"; }
	String         Title() const override { return "MetaAccount"; }
	int            DefaultPort() const override { return 18214; }
	bool           ContentPolicy() const override { return false; }
	bool           Install(EngineSite& site, WebAppContext& ctx, String& error) override;
	bool           RegisterBroker(WebAppContext& ctx, String& error) override;

private:
	EngineSite*  site = nullptr;
	WebGrants*   grants = nullptr;
};

END_UPP_NAMESPACE

#endif
