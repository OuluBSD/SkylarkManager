#ifndef _Company_CompanyApp_h_
#define _Company_CompanyApp_h_

#include <WebAppHost/WebAppHost.h>
#include "CompanyComposed.h"
#include "CompanySite.h"

NAMESPACE_UPP

// The Company webapp for WebServer. Registered as "company" by CompanyApp.icpp. The site lives in
// <site root>/company-site (SiteDir), so the store under the "store" setting (default engine/company) is its
// own folder and never shares a root with the site. Install opens the module over the store and installs the
// Company routes; RegisterBroker registers the Company broker surface. The site has no content policy routes,
// as before. Users are provisioned with SetCompanyUser, which takes the roles from CompanyAllRoles.
class CompanyApp : public WebApp {
public:
	String         Id() const override { return "company"; }
	String         Title() const override { return "Company"; }
	int            DefaultPort() const override { return 18219; }
	String         SiteDir() const override { return "company-site"; }
	bool           ContentPolicy() const override { return false; }
	bool           Install(EngineSite& site, WebAppContext& ctx, String& error) override;
	bool           RegisterBroker(WebAppContext& ctx, String& error) override;

	bool           Installed() const { return module; }
	CompanyStore&  Store() { return module->Store(); }   // requires Installed()

private:
	One<CompanyComposedModule> module;
};

END_UPP_NAMESPACE

#endif
