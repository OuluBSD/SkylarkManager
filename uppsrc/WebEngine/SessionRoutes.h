#ifndef _WebEngine_SessionRoutes_h_
#define _WebEngine_SessionRoutes_h_

#include "Site.h"

NAMESPACE_UPP

// Adds the session routes every composed site needs, using the site's own
// accounts and session cookie under session/: GET session/login (form), session/login:POST (JSON for API
// callers, redirect to / for browser forms) and session/logout:POST.
void InstallSessionRoutes(EngineSite& site);
void InstallAuditRoutes(EngineSite& site);

class WebRequest;

// True when a MetaAccount return target is an allowlisted URL on this request's host (task 039).
bool SsoReturnAllowed(WebRequest& r, const String& url);
// The MetaAccount central session cookie name, and the username behind a central session id (task 050).
String SsoCentralCookieName();
bool SsoPortAllowed(int port);
bool SsoCentralUsername(const String& session_id, String& username);

// Shared MetaAccount sign-in (task 039): signs a guest in from a live central session
// when the account is linked to this site. Called by the engine dispatch for every
// composed request before its handler; GET and HEAD only.
void SsoAutoSignIn(WebRequest& r);

// The signed-in account and its sign-out and link controls, for a composed page's header (task 039).
String SessionChromeHtml(WebRequest& r);
// Admin-only content policy routes and manifest commands for a site (task 053).
void InstallContentPolicyRoutes(EngineSite& site, bool allow_secret_links);
// Host-wide theme and density for a composed page (task 052): data attributes from the wf_theme / wf_density
// cookies, whitelisted, and a small script that applies a shell broadcast from the same host.
String ThemeHtmlAttrs(WebRequest& r);
String ThemeHeadScript();

END_UPP_NAMESPACE

#endif
