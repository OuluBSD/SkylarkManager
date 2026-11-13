#ifndef _WebEngine_Request_h_
#define _WebEngine_Request_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include <WebAccounts/WebAccounts.h>
#include "Site.h"

NAMESPACE_UPP

// Fenced request context handed to site route handlers. The engine has
// already resolved the site, session, user, and enforced the origin/CSRF
// fence when this object reaches a handler.
class WebRequest {
public:
	// --- request facts ---
	String Method() const          { return method; }
	String Path() const            { return path; }      // canonical: "/a/b"
	const  Vector<String>& PathArgs() const { return path_args; }
	String PathArg(int i) const    { return i >= 0 && i < path_args.GetCount() ? path_args[i] : String(); }
	int    PathArgCount() const    { return path_args.GetCount(); }
	String Param(const char *name) const;                 // query/form field
	int    IntParam(const char *name) const;
	String Header(const char *name) const;
	String Body() const;                                 // raw request content (JSON bodies)
	String Cookie(const char *name) const;
	String ClientIp() const;
	bool   IsMutation() const;                            // POST/PUT/PATCH/DELETE

	EngineSite& Site() const { return *site; } // handlers manage their own site (accounts etc.)

	// --- auth state (engine-resolved) ---
	bool HasSession() const        { return has_session; }
	const WebAccountSession& Session() const { return session; }
	bool HasUser() const           { return has_user; }
	const WebAccountUser& User() const { return user; }
	String CsrfToken() const;     // server-side token for the active session

	// --- output ---
	WebRequest& operator<<(const Value& v);
	WebRequest& Html(const String& html);
	WebRequest& Plain(const String& text, const char *ctype = "text/plain; charset=UTF-8");
	WebRequest& Json(const Value& v, int code = 200);
	WebRequest& Status(int code, const String& text);
	WebRequest& Redirect(const String& url);
	WebRequest& SetHeader(const char *h, const char *v) { http.SetHeader(h, v); return *this; }

	// --- session cookie helpers ---
	WebRequest& EnsureSession();  // creates a guest session + sets the site cookie
	WebRequest& DropSession();    // logs out + clears the site cookie
	WebRequest& AdoptSession(const String& session_id); // site-stamped session (e.g. from login)
	void        SetSessionData(const ValueMap& data);

	// --- raw cookie helpers (task 039). The name is a stored (lowercase) cookie
	// name; the value is never written to a URL, a log or a diagnostic entry. ---
	String      RequestCookie(const String& name);
	WebRequest& SetCookieValue(const String& name, const String& value, Time expires);

private:
	friend class WebEngine;
	WebRequest(Http& http_, EngineSite& site_, const String& method_, const String& path_,
	           const Vector<String>& args);

	Http&        http;
	EngineSite*  site;
	String       method;
	String       path;
	Vector<String> path_args;
	WebAccountSession session;
	bool         has_session = false;
	WebAccountUser user;
	bool         has_user = false;
};

END_UPP_NAMESPACE

#endif
