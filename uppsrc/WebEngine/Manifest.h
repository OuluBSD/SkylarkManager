#ifndef _WebEngine_Manifest_h_
#define _WebEngine_Manifest_h_

#include <Core/Core.h>

NAMESPACE_UPP

// Command manifest: the CLI surface each site declares at install time. A
// descriptor is data only. It names how a command reaches the site (an HTTP
// route or a broker operation), what arguments it takes, and what it needs
// to run; it never executes anything. The site's own route or service still
// enforces the real checks.
//
// A site is listed only after it has registered at least one command, so
// the manifest shows what the connected server actually enables.

struct CommandArg : Moveable<CommandArg> {
	String name;
	String type;         // "string" | "int" | "number" | "text"
	bool   required = false;
	String description;
};

struct CommandDesc : Moveable<CommandDesc> {
	String name;         // CLI command word, unique within its site, e.g. "issue-create"
	String transport;    // "http" | "broker"
	String method;       // http only: "GET" | "POST"
	String api_path;     // http only: route pattern relative to the site root, e.g. "api/papers/*"
	String broker_op;    // broker only: operation name, e.g. "issue.create"
	String auth;         // who may run it, as the route or service enforces it
	String response;     // "json" | "html" | "broker"
	String description;
	Vector<CommandArg> args;
};

class CommandManifest {
public:
	// Registers one command for a site. Titles are shown in the manifest.
	void Add(const String& site_id, const String& site_title, CommandDesc cmd);

	// Descriptor for every enabled site (site_id empty) or for one site.
	// Returns the JSON-ready schema: {schema, sites: [{id, title, commands: [...]}]}.
	ValueMap Describe(const String& site_id = String()) const;

	bool HasSite(const String& site_id) const;
	int  GetCount() const;

private:
	struct Entry : Moveable<Entry> {
		String      site_id;
		String      site_title;
		CommandDesc cmd;
	};

	Vector<Entry> entries;
	mutable Mutex mutex;
};

// Shorthand for install-time declarations: argument specs are "name:type[:required]".
CommandDesc HttpCommand(const char *name, const char *method, const char *api_path, const char *auth,
                        const char *response, const char *description, const Vector<String>& args);
CommandDesc BrokerCommand(const char *name, const char *broker_op, const char *auth,
                          const char *description, const Vector<String>& args);

END_UPP_NAMESPACE

#endif
