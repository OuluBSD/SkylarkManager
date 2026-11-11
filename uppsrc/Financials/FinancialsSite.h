#ifndef _Financials_FinancialsSite_h_
#define _Financials_FinancialsSite_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>

NAMESPACE_UPP

// Installs the read-only Financials routes on a host-created EngineSite.
// Roles on the site's own accounts: "viewer" (aggregates) and "finance"
// (ledger rows and contracts). The site maps roles to Company scopes per
// caller; the Company module enforces them.
void InstallFinancialsSite(EngineSite& site);

END_UPP_NAMESPACE

#endif
