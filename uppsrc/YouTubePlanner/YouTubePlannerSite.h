#ifndef _YouTubePlanner_YouTubePlannerSite_h_
#define _YouTubePlanner_YouTubePlannerSite_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>

NAMESPACE_UPP

// Installs the YouTubePlanner stub routes on a host-created EngineSite. The stub is anonymous
// and has no data: it shows the six planning levels and what belongs at each one.
void InstallYouTubePlannerSite(EngineSite& site);

// The six planning levels, in the order the site presents them.
int    YouTubePlannerLevelCount();
String YouTubePlannerLevelName(int index);
String YouTubePlannerLevelSlug(int index);   // the route segment, e.g. "mockup-image"
int    YouTubePlannerLevelIndex(const String& slug);  // -1 when unknown

// The page bodies, without the site chrome. They hold links and text only: no form, input,
// textarea or button, so the stub offers no write path.
String YouTubePlannerOverviewBody();
String YouTubePlannerLevelBody(int index);

// Self-tests: the level table, the overview and each level body, and the no-write boundary.
// Run from ManagerSkylark --test.
bool RunYouTubePlannerSelfTests();

END_UPP_NAMESPACE

#endif
