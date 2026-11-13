#ifndef _WebEngine_Engine_h_
#define _WebEngine_Engine_h_

#include <Core/Core.h>
#include <WebBroker/WebBroker.h>
#include "Router.h"
#include "Site.h"
#include "Request.h"
#include "Manifest.h"

NAMESPACE_UPP

// The website engine: owns all virtual websites of one process, the listener
// port bindings, and the cross-site broker. There is exactly one engine per
// process (mirroring the SkylarkApp singleton), but all per-site state lives
// in EngineSite instances -- the engine itself holds no request-scoped
// globals.
// A site bound to a listener port, for portal listings (task 033).
struct EngineSiteListing : Moveable<EngineSiteListing> {
	String id;
	String title;
	int    port = 0;
};

class WebEngine {
public:
	EngineSite& AddSite(const String& id, const String& title, const String& external_origin,
	                    const String& data_root, int session_ttl_hours = 12);
	void        BindPort(int port, const String& site_id);      // extra listener port
	void        BindDefaultPort(int port, const String& site_id); // stock Skylark listener port

	EngineSite* FindSite(const String& id);
	EngineSite* FindSiteByPort(int port); // 0 = stock listener -> default binding

	// Every site bound to a port, in creation order (task 033).
	Vector<EngineSiteListing> ListSites();

	WebBroker&  Broker()                  { return broker; }

	// Opt-in MetaAccount single sign-on (--enable-meta-sso). Off by default: no auto sign-in, no SSO
	// routes, no link control and no sign-out marker, so per-site sign-in behaves as before SSO.
	void SetMetaSso(bool on)              { meta_sso = on; }
	bool MetaSso() const                  { return meta_sso; }
	CommandManifest& Manifest()           { return manifest; }
	String      DiagnosticsJSON(int max_entries = 100) { return broker.DiagnosticsJSON(max_entries); }

	// Gateway entry, called by the SKYLARK catch-alls in Host.cpp.
	void Gateway(Http& http);

	static int  CurrentListenerPort();
	static void SetCurrentListenerPort(int port);

private:
	Vector<One<EngineSite>> sites;
	VectorMap<int, int>     port_site;   // port -> sites index
	int                     default_port_site = -1;
	WebBroker               broker;
	bool                    meta_sso = false;
	CommandManifest         manifest;
	Mutex                          mutex;
};

WebEngine& TheWebEngine();

END_UPP_NAMESPACE

#endif
