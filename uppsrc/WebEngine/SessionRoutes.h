#ifndef _WebEngine_SessionRoutes_h_
#define _WebEngine_SessionRoutes_h_

#include "Site.h"

NAMESPACE_UPP

// Adds the session routes every composed site needs, using the site's own
// accounts and session cookie under session/: GET session/login (form), session/login:POST (JSON for API
// callers, redirect to / for browser forms) and session/logout:POST.
void InstallSessionRoutes(EngineSite& site);

END_UPP_NAMESPACE

#endif
