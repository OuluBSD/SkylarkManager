#include "FinancialsApp.h"
#include "FinancialsSite.h"

NAMESPACE_UPP

void FinancialsApp::Install(EngineSite& site, WebAppContext& ctx)
{
	InstallFinancialsSite(site);
}

END_UPP_NAMESPACE
