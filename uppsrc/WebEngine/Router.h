#ifndef _WebEngine_Router_h_
#define _WebEngine_Router_h_

#include <Core/Core.h>

NAMESPACE_UPP

class WebRequest;

// One route in a site's tree. Pattern syntax mirrors Skylark:
//   ""            root
//   "page"        literal segment
//   "user/*"      single-argument segment
//   "files/**"    trailing catch-all (consumes the rest)
// Method: "GET" (default), "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS".
struct SiteRoute : Moveable<SiteRoute> {
	String                 method;
	String                 pattern;
	Callback1<WebRequest&> handler;
};

class SiteRouter {
public:
	void Add(const char *method, const char *pattern, Callback1<WebRequest&> handler);
	void Add(const char *method, const char *pattern, void (*fn)(WebRequest&));
	void Add(const char *pattern, Callback1<WebRequest&> handler) { Add("GET", pattern, handler); }
	void Add(const char *pattern, void (*fn)(WebRequest&))        { Add("GET", pattern, fn); }

	// Best-match: most literal segments wins, then more matched parts, then
	// fewer wildcards. Returns false when no route matches.
	bool Match(const String& method, const Vector<String>& segments,
	           Vector<String>& args, SiteRoute& out) const;

	int  GetCount() const { return routes.GetCount(); }
	const SiteRoute& operator[](int i) const { return routes[i]; }

private:
	Vector<SiteRoute> routes;
};

END_UPP_NAMESPACE

#endif
