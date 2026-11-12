#ifndef _ManagerSkylark_PortalShell_h_
#define _ManagerSkylark_PortalShell_h_

#include <WebEngine/Engine.h>
#include "SystemAuth.h"

// One entry of the portal's left meta-list (task 034). The shell embeds mode
// "frame" descriptors; a later mode (proxy) would be handled here without
// changing the shell.
struct PortalWebapp : Moveable<PortalWebapp> {
	String id;
	String title;
	String group;   // "webapp" or "settings"
	String mode;    // "frame"
	String url;     // what the frame loads
};

// The host portal links are served on, from a request's Host header: the name
// without its port (loopback when the header is empty). A LAN browser gets LAN
// links (task 032).
String PortalLinkHost(const String& host_header);

// The descriptors for the meta-list: the manager's own home first, then one per
// site bound in this process (absolute origins on the request's host), then the
// stock settings page. Sites are a parameter so the self-tests can pass their own.
Vector<PortalWebapp> ListWebapps(const String& host_header, const Vector<EngineSiteListing>& sites);

// The user row at the foot of the meta-list.
String RenderShellAccountRow(const UserSession& sess);

// The portal page at "/": the meta-list on the left and the selected descriptor's
// frame in the main area. An unknown or empty selection falls back to the manager.
String RenderPortalShell(const String& host_header, const String& selected, const UserSession& sess,
                         const Vector<EngineSiteListing>& sites);

#endif
