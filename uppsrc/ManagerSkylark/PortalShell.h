#ifndef _ManagerSkylark_PortalShell_h_
#define _ManagerSkylark_PortalShell_h_

#include <WebEngine/Engine.h>
#include "SystemAuth.h"

// One entry of the portal's webapp list (task 034). The shell embeds mode
// "frame" descriptors; a later mode (proxy) would be handled here without
// changing the shell.
struct PortalWebapp : Moveable<PortalWebapp> {
	String id;
	String title;
	String group;   // "webapp" or "settings"
	String mode;    // "frame"
	String icon;    // a VividIcon name for the rail
	String url;     // what the frame loads
};

// The host portal links are served on, from a request's Host header: the name
// without its port (loopback when the header is empty). A LAN browser gets LAN
// links (task 032).
String PortalLinkHost(const String& host_header);

// The descriptors for the webapp list: Atlas first (task 036), then the five
// stock hubs (their roots), then the other composed sites (absolute origins on
// the request's host), then the stock settings page. Sites are a parameter so
// the self-tests can pass their own.
Vector<PortalWebapp> ListWebapps(const String& host_header, const Vector<EngineSiteListing>& sites);

// The user row at the foot of the webapp list. A guest's "Log in" opens inside the
// portal frame and returns to return_url (a stock webapp's path); an empty return_url
// (a composed webapp, which signs in inside its own frame) shows no stock sign-in.
String RenderShellAccountRow(const UserSession& sess, const String& return_url);

// The portal page at "/": a two-rail meta-nav (an icon rail and a labelled list)
// beside the selected webapp's frame. Links target the frame, so the shell stays
// mounted while webapps switch; the frame shows a loading state and a timeout
// fallback. An unknown or empty selection falls back to the first webapp.
String RenderPortalShell(const String& host_header, const String& selected, const UserSession& sess,
                         const Vector<EngineSiteListing>& sites,
                         const String& theme_cookie = Null, const String& density_cookie = Null);

#endif
