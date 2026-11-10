#ifndef _WebEngine_SessionRoutes_h_
#define _WebEngine_SessionRoutes_h_

#include "Site.h"

NAMESPACE_UPP

// Adds the session routes every composed site needs, using the site's own
// accounts and session cookie: GET login (form), login:POST (JSON for API
// callers, redirect to / for browser forms) and logout:POST.
void InstallSessionRoutes(EngineSite& site);

END_UPP_NAMESPACE

#endif
