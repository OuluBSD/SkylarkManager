#ifndef _WebEngine_Site_h_
#define _WebEngine_Site_h_

#include <Core/Core.h>
#include <WebAccounts/WebAccounts.h>
#include "Router.h"

NAMESPACE_UPP

// One fenced virtual website: its own identity realm, storage root, sessions,
// cookies, and route tree. EngineSite is held via One<> (it contains mutexes
// and is therefore not moveable).
class EngineSite {
public:
	String id;                 // safe id: [A-Za-z0-9_-]
	String title;
	String external_origin;    // e.g. "http://127.0.0.1:18101" (exact-origin fence)
	String data_root;          // site-owned storage root
	int    session_ttl_hours = 12;

	void Init(const String& site_id, const String& site_title, const String& origin,
	          const String& root, int ttl = 12);

	// Site-owned witz template and static roots, under data_root. Served only
	// while this site's request is dispatched (see WebEngineHost listeners).
	String        TemplateRoot() const { return AppendFileName(data_root, "templates"); }
	String        StaticRoot() const   { return AppendFileName(data_root, "static"); }

	SiteAccounts& Accounts();
	SiteRouter&   Router()             { return router; }
	String        CookieName() const;
	String        Url(const String& pattern, const Vector<Value>& args = Vector<Value>()) const;

	// True when the given Origin/Referer authority exactly matches
	// external_origin (case-insensitive on scheme/host).
	bool          OriginAllowed(const String& authority) const;

private:
	SiteRouter router;
	One<SiteAccounts> accounts;
};

END_UPP_NAMESPACE

#endif
