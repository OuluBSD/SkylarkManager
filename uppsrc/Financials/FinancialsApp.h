#ifndef _Financials_FinancialsApp_h_
#define _Financials_FinancialsApp_h_

#include <WebAppHost/WebAppHost.h>

NAMESPACE_UPP

// The Financials webapp for WebServer. Registered as "financials" by FinancialsApp.icpp; the site
// itself is InstallFinancialsSite (FinancialsSite.h).
class FinancialsApp : public WebApp {
public:
	String         Id() const override { return "financials"; }
	String         Title() const override { return "Financials"; }
	int            DefaultPort() const override { return 18216; }
	Vector<String> Roles() const override { Vector<String> r; r.Add("viewer"); return r; }
	bool           Install(EngineSite& site, WebAppContext& ctx, String& error) override;
};

END_UPP_NAMESPACE

#endif
