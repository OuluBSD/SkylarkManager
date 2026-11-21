#include "FinancialsApp.h"
#include "FinancialsSite.h"

NAMESPACE_UPP

bool FinancialsApp::Install(EngineSite& site, WebAppContext& ctx, String& error)
{
	InstallFinancialsSite(site);
	return true;
}

END_UPP_NAMESPACE
