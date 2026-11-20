#ifndef _WebAppHost_WebAppHost_h_
#define _WebAppHost_WebAppHost_h_

#include <Core/Core.h>
#include <WebEngine/Host.h>
#include <WebAccounts/WebAccountsGrants.h>
#include <WebBroker/WebBroker.h>
#include <Channels/AgentIpcClient.h>

NAMESPACE_UPP

// Shared services a webapp receives in Install and RegisterBroker.
class WebAppContext {
public:
	String          site_dir;                // per-site data lives under <site_dir>/engine/<id>
	String          public_host;             // address for site links; never loopback
	WebGrants*      grants = nullptr;        // shared grant store (MetaAccount layout)
	WebBroker*      broker = nullptr;        // the engine's cross-site broker
	AgentIpcConfig* agentipc = nullptr;      // optional; set by the program

	String SiteRoot(const String& id) const;
};

// One website hosted by WebServer. An app owns its backend; the host owns the
// listener, the site and the fixed route sequence.
class WebApp {
public:
	virtual ~WebApp() {}

	virtual String         Id() const = 0;          // site id, [A-Za-z0-9_-]; unique among enabled apps
	virtual String         Title() const = 0;
	virtual int            DefaultPort() const = 0;
	virtual Vector<String> Roles() const { return Vector<String>(); }   // first role is used by --set-<id>-user
	virtual Vector<String> Requires() const { return Vector<String>(); } // ids installed before this app
	virtual void           AddArgs(CommandLineArguments& cla) const {}  // app-specific flags
	virtual void           Install(EngineSite& site, WebAppContext& ctx) = 0;
	virtual void           RegisterBroker(WebAppContext& ctx) {}        // called after Install
	virtual bool           ContentPolicy() const { return true; }       // false: no content policy routes
};

// Registry: an app class registers under a name from an INITBLOCK in an .icpp file.
typedef WebApp* (*WebAppNewFn)();
ArrayMap<String, WebAppNewFn>& WebAppFactories();
template <class T> WebApp* NewWebApp() { return new T(); }
template <class T> void RegisterWebapp(const char* name) { WebAppFactories().GetAdd(name) = &NewWebApp<T>; }
Vector<String> WebappNames();
WebApp*        CreateWebapp(const String& name);   // nullptr when the name is not registered

// Blob routes for one site: POST blobs/<id> (upload, with the write roles) and GET blobs/<id> (read, with
// the read roles). Bytes live in the site's <collection> blob store, capped at max_bytes per upload.
// write_roles and read_roles are comma-separated role names; an empty list admits nobody.
void InstallBlobRoutes(EngineSite& site, const String& collection, int64 max_bytes,
                       const String& write_roles, const String& read_roles);

// The engine host with the listener settings WebServer sets (SkylarkApp keeps them protected).
class WebServerHost : public WebEngineHost {
public:
	void Listen(int listen_port, const String& listen_ip, int listen_threads) {
		port = listen_port;
		ip = listen_ip;
		threads = listen_threads;
	}
};

// Composes the enabled webapps into one WebEngine host.
class WebServer {
public:
	WebServer();
	~WebServer();
	WebServer(const WebServer&) = delete;
	WebServer& operator=(const WebServer&) = delete;

	bool           Enable(const String& name, String& error);  // registry name; refused when unknown
	Vector<String> EnabledIds() const;
	void           SetAgentIpc(const AgentIpcConfig& cfg);
	void           SetAppPort(const String& id, int listen_port);   // overrides DefaultPort(), as --<id>-port does
	void           UseGrants(WebGrants* shared);                    // borrowed grant store; not created or owned here

	// Reads --port/-p, --site-dir, --public-host, --enable NAME[,NAME], --<id>-port, --set-<id>-user
	// and each enabled app's own flags. --enable names are enabled before the flag set is built.
	bool ParseArgs(const Vector<String>& args, String& error);

	// Install order: Requires() ordering, stable in enable order. Refused on unknown ids, cycles,
	// duplicate ids, duplicate ports and a port equal to the stock port.
	bool InstallOrder(Vector<String>& ids, String& error) const;
	int  PortFor(const String& id) const;   // --<id>-port, else DefaultPort()
	String SetUserFor(const String& id) const;   // username from --set-<id>-user, or empty

	// Creates each enabled app's site in install order: AddSite, Install, RegisterBroker,
	// InstallSessionRoutes, InstallContentPolicyRoutes (unless ContentPolicy() is false),
	// InstallCommandConsole, BindSite. The caller owns the listener (see Run).
	bool Compose(WebEngineHost& host, String& error);
	// Creates or resets an account on a composed app's site.
	bool SetUser(const String& id, const String& username, const String& password, String& error);

	// Parses, composes, then either provisions --set-<id>-user accounts (returns 0) or runs the listeners.
	int  Run(const Vector<String>& args);

	int    port = 8081;                 // stock listener
	String site_dir;
	String public_host;
	VectorMap<String, String> origins;  // external origin per id; default http://public_host:<port>
	Vector<String>            trusted_proxy_peers; // copied to every composed site

private:
	WebApp* FindApp(const String& id) const;

	Vector<String>            names;    // registry names, enable order
	Vector<WebApp*>           apps;     // parallel to names
	VectorMap<String, int>    app_ports;
	VectorMap<String, String> set_users;
	One<WebGrants>            grants;
	WebGrants*                shared_grants = nullptr;
	AgentIpcConfig            agentipc;
	bool                      has_agentipc = false;
};

END_UPP_NAMESPACE

#endif
