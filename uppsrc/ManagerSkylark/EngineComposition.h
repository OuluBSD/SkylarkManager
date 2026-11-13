#ifndef _reference_ManagerSkylark_EngineComposition_h_
#define _reference_ManagerSkylark_EngineComposition_h_

#include <Core/Core.h>
#include <WebEngine/Host.h>
#include <WebEngine/CommandConsole.h>
#include <Atlas/AtlasSite.h>
#include <Channels/ChannelsSite.h>
#include <Financials/FinancialsSite.h>
#include <MetaAccount/MetaAccountSite.h>
#include <PrivateHubWorkspace/PrivateHubSite.h>
#include <Science/ScienceSite.h>
#include <Science/ScienceService.h>
#include <TaskFoundry/TaskFoundrySite.h>
#include <YouTubePlanner/YouTubePlannerSite.h>
#include <CompanyComposed/CompanyComposed.h>
#include "PrivateHubService.h"

NAMESPACE_UPP

// Ports and data locations for the engine sites composed into this process.
struct EngineComposition {
	int    privatehub_port = 18211;
	int    science_port = 18212;
	int    taskfoundry_port = 18213;
	int    meta_port = 18214;
	int    channels_port = 18215;
	int    financials_port = 18216;
	int    atlas_port = 18217;
	int    youtubeplanner_port = 18218;
	bool   enable_meta_sso = false;   // --enable-meta-sso: opt-in MetaAccount SSO (task 039)
	// The read-only AgentIPC bridge (Channels). Empty strings keep the
	// defaults of AgentIpcConfig / AgentIpcDefaultClientArgv().
	String agentipc_direct = "127.0.0.1:7440";
	String agentipc_server = "skylark";
	String agentipc_user = "channels";
	String agentipc_python;
	String agentipc_client;
	String agentipc_home;
	String privatehub_workspace; // checkouts root (default ~/Dev)
	String science_corpus;       // flat corpus + article sidecars (default ~/Dev/AnthologyPrivate/site/data)
	String company_root;         // Company store root (default <site_dir>/engine/company)
	Vector<String> company_repos; // repositories Company may propose against; empty permits none

	static EngineComposition Defaults();
};

// Backends the composed sites borrow. Owned by the application object so they
// are destroyed in main, not during static teardown after the engine is gone.
struct EngineSiteBackends {
	One<PrivateHubWorkspaceBackend> privatehub;
	One<ScienceCorpus>              science;
	One<WebGrants>                  grants;
	One<CompanyComposedModule>      company;
	One<TaskFoundryService>         taskfoundry;
};

// Creates the PrivateHub, Science, TaskFoundry and MetaAccount engine sites,
// opens the composed Company module, binds the listener ports, registers the
// broker services (privatehub, science, company, meta), and creates the grant
// store under <site_dir>/engine. Called once, before the app runs, for the
// real server. Returns false, with error set, when a composed store cannot be
// opened.
bool ComposeEngineSites(WebEngineHost& host, EngineSiteBackends& owned, const String& site_dir,
                        const EngineComposition& cfg, String& error);

// Creates or updates a PrivateHub account (role "member") on the composed
// PrivateHub site. Requires ComposeEngineSites to have run.
// role is the one PrivateHub role given to the account; "member" may author, anything else may read only.
bool SetPrivateHubPassword(const String& username, const String& password, String& error,
                           const String& role = "member");

// Creates or resets a Science account with the science-author role, which is
// what authoring needs. Requires ComposeEngineSites to have run.
bool SetScienceUserPassword(const String& username, const String& password, String& error);

// Creates or updates a TaskFoundry account (role "member") on the composed
// TaskFoundry site. Requires ComposeEngineSites to have run.
bool SetTaskFoundryPassword(const String& username, const String& password, String& error);

// Creates or updates a MetaAccount account on the composed meta site (the
// ecosystem identity a client site links to for consent).
// Requires ComposeEngineSites to have run.
bool SetMetaAccountPassword(const String& username, const String& password, String& error);

// Creates or updates a Channels viewer account on the composed channels site
// (read-only frontend). Requires ComposeEngineSites to have run.
bool SetChannelsPassword(const String& username, const String& password, String& error);

// Creates or updates a Financials account (role "viewer", or "finance" via
// the financials role flag). Requires ComposeEngineSites to have run.
bool SetFinancialsPassword(const String& username, const String& role,
                           const String& password, String& error);

// Creates or updates an Atlas account on the composed atlas site. The role
// "admin" may edit maps; any other role reads only. Requires ComposeEngineSites.
bool SetAtlasPassword(const String& username, const String& role, const String& password, String& error);

// Engine gateway handler ids: the SKYLARK catch-alls in WebEngine/Host.cpp.
bool IsEngineGatewayHandler(const String& handler_id);

END_UPP_NAMESPACE

#endif
