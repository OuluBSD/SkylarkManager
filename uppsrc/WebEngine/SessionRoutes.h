#ifndef _WebEngine_SessionRoutes_h_
#define _WebEngine_SessionRoutes_h_

#include "Site.h"

NAMESPACE_UPP

// Adds the session routes every composed site needs, using the site's own
// accounts and session cookie under session/: GET session/login (form), session/login:POST (JSON for API
// callers, redirect to / for browser forms) and session/logout:POST.
void InstallSessionRoutes(EngineSite& site);

class WebRequest;

// Shared MetaAccount sign-in (task 039): signs a guest in from a live central session
// when the account is linked to this site. Called by the engine dispatch for every
// composed request before its handler; GET and HEAD only.
void SsoAutoSignIn(WebRequest& r);

// The signed-in account and its sign-out and link controls, for a composed page's header (task 039).
String SessionChromeHtml(WebRequest& r);

END_UPP_NAMESPACE

#endif
