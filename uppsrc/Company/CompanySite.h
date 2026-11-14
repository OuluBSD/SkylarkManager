#ifndef _Company_CompanySite_h_
#define _Company_CompanySite_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include "CompanyStore.h"

NAMESPACE_UPP

// Installs the Company routes on a host-created EngineSite: the web pages, the
// REST API (/api/...) and the v2 platform API (/api/v2/...), with the JSON
// contracts of CompanySkylark. The host registers the session routes
// (InstallSessionRoutes) and any other site extras. The store is owned by the
// host and must outlive the site. Sign-in uses the site's WebAccounts; roles and
// scopes come from the Company store.
void InstallCompanySite(EngineSite& site, CompanyStore& store);

// Host-side provisioning (no HTTP, no actor): creates or updates the Company
// user record in `store` and its WebAccounts login on the installed site. Roles
// must be valid CompanyAllRoles(). An existing login gets the new password.
// Requires InstallCompanySite(site, store) to have run over the same store.
bool SetCompanyUser(CompanyStore& store, const String& username, const String& password,
                    const Vector<String>& roles, String& error);

END_UPP_NAMESPACE

#endif
