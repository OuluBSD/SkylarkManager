#ifndef _SiteCli_SiteCli_h_
#define _SiteCli_SiteCli_h_

#include <Core/Core.h>

NAMESPACE_UPP

// skylarkctl: a dumb client. It reads the command manifest of a Skylark server,
// binds the words on the command line to one descriptor's arguments, sends one
// HTTP request, and prints the reply. It holds no business logic; the site's
// own route enforces every check.

struct SiteCliOptions {
	String                    server;        // manifest host, e.g. http://127.0.0.1:8080
	VectorMap<String, String> site_urls;     // site id -> base URL; a site without one uses server
	String                    session_file;  // cookie jar; empty keeps nothing between runs
	String                    password_env;  // login password from this variable instead of a prompt
	bool                      toon = false;  // print the reply as TOON (for AI agents)
	bool                      trace = false; // print the last request and reply on stderr
	String                    bearer_file;   // token file sent as Authorization: Bearer
	bool                      bearer_login = false; // login uses api/auth/login and keeps the token in the jar
};

// One HTTP request, built from a manifest descriptor and the typed words.
struct SiteCliRequest {
	String method;
	String url_path;      // site-relative path, percent-encoded, plus any query string
	String body;
	String content_type;  // empty when there is no body
};

// Binds the words after "<section> <command>" to the descriptor's arguments.
// Words are "name=value" or positional, positional words fill the arguments
// in declared order. The first N declared arguments fill the N "*" segments
// of api_path. api/ routes take a JSON body; the others take a form body.
bool SiteCliBuildRequest(const ValueMap& command, const Vector<String>& words,
                         SiteCliRequest& out, String& err);

// Names from the list that are close to what was typed, for "did you mean".
Vector<String> SiteCliSuggest(const String& typed, const Vector<String>& names);

// Runs the in-process checks of the pure helpers. Returns the failure count.
int SiteCliSelfTests();

END_UPP_NAMESPACE

#endif
