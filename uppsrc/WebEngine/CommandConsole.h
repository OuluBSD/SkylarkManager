#ifndef _WebEngine_CommandConsole_h_
#define _WebEngine_CommandConsole_h_

#include <Core/Core.h>
#include "Engine.h"

NAMESPACE_UPP

// Adds GET /console to a site: the site's own manifest commands rendered as
// links and CSRF-bearing forms. The console adds no authority — the routes
// keep being the gate (session, roles, CSRF, origin). Commands whose path
// carries wildcards, and broker commands, render as documentation rows.
void InstallCommandConsole(EngineSite& site);

END_UPP_NAMESPACE

#endif
