#ifndef _WebEngine_Site_h_
#define _WebEngine_Site_h_

#include <Core/Core.h>
#include <WebAccounts/WebAccounts.h>
#include "Router.h"

NAMESPACE_UPP

// One fenced virtual website: its own identity realm, storage root, sessions,
// cookies, and route tree. EngineSite is held via One<> (it contains mutexes
// and is therefore not moveable).
// The Host header of the request this thread is serving. The gateway sets it
// for the duration of one dispatch (RequestHostScope) and clears it after.
void   SetRequestHost(const String& host);
String RequestHost();

// Sets the request host for its lifetime; clears it on exit, also on early return.
class RequestHostScope {
public:
	explicit RequestHostScope(const String& host);
	~RequestHostScope();
};

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

	// The fence: the given Origin (or Referer origin) must be plain http and
	// its authority must equal the authority this request is served on, which is
	// the request's Host header when that names an IP literal or localhost, and
	// the declared external_origin otherwise (no Host header, or a DNS name that
	// DNS rebinding could point at the listener).
	bool          OriginAllowed(const String& origin) const;

private:
	SiteRouter router;
	One<SiteAccounts> accounts;
};

END_UPP_NAMESPACE

#endif
