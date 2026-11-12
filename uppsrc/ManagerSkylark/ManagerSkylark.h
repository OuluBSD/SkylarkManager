#ifndef _reference_ManagerSkylark_ManagerSkylark_h_
#define _reference_ManagerSkylark_ManagerSkylark_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include <WebEngine/Host.h>
#include <Manager/Manager.h>
#include <SweAgent/SweAgent.h>
#include <WebChat/WebChat.h>
#include <WebFramework/WebFramework.h>
#include "V2Model.h"
#include "DataStore.h"
#include "ScopedPolicy.h"
#include "CandidateLineage.h"
#include "GitHubPlatform.h"
#include "SourceForgePlatform.h"
#include "SystemAuth.h"
#include "EngineComposition.h"

NAMESPACE_UPP

// The host portal links are served on, from a request's Host header: the name
// without its port (loopback when the header is empty). A LAN browser gets LAN
// links (task 032). Declared here so the self-tests can reach it.
String PortalLinkHost(const String& host_header);

class ManagerSkylarkApp : public WebEngineHost {
public:
	ManagerSkylarkApp();
	~ManagerSkylarkApp() override;

	bool Initialize(const Vector<String>& args);
	void InitApp();
	bool FilterHandler(Http& http) override;

	bool IsDemoSite() const { return demo_site; }
	bool IsTestMode() const { return test_mode; }
	// True when engine sites are composed; their singletons outlive main, so the process ends with _exit.
	bool IsEngineComposed() const { return engine_composed; }
	bool HasExplicitSiteDir() const { return custom_site_dir; }
	String GetSiteDir() const { return site_dir; }
	// The docs portal allowlist ("label=path,..."); set from --docs-repos.
	String& DocsReposSpec() { return docs_repos_spec; }

	V2Store&      GetV2Store()      { return v2_store; }
	ManagerStore& GetLocalStore()   { static StaticMutex sMutex; Mutex::Lock __(sMutex); local_store.LoadAll(); return local_store; }
	const DataStore& GetDataStore() const { return data_store; }
	EffectivePolicy ResolveEffectivePolicy(const PolicyInput& input) const;
	bool IsAssessmentSourceVisible(const String& workspace_id, const String& source_identity) const;
	static bool SeedSyntheticCandidateWorkflow(const String& data_root, const String& workspace_id, const String& candidate_id, const String& source_identity);
	static bool SeedSyntheticReviewData(const String& data_root, const String& workspace_id, const String& source_identity);

	static ManagerSkylarkApp& Get();

	void StopServer();
	void LoadSitePolicy();

	static bool RunSelfTests(int port = 18080);

	// Shared HTML rendering components
	static String RenderHtmlPage(const String& title, const String& active_nav, const String& body_content, const Http* http = nullptr);
	static void   AddMarkdownSupport(PageBuilder& pb);
	static String RenderBadge(const String& status);
	static String RenderProgressBar(double percent);
	static NavigationModel BuildNavigation(const String& active_nav = "", const Http* http = nullptr);
	static TreeModel       BuildMeshFleetTree(const String& active_nav = "", const String& active_node_id = "");
	static TreeModel       BuildGoalsTree(V2Store& store, const String& active_id = "");
	static TreeModel       BuildProjectsTree(ManagerStore& store, const String& active_slug = "");

private:
	EngineSiteBackends engine_backends; // owns the composed sites' backends
	bool engine_composed = false;
	static ManagerSkylarkApp* s_instance;

	V2Store      v2_store;
	DataStore    data_store;
	ManagerStore local_store;
	PolicySettings instance_policy;
	PolicySettings workspace_policy;
	PolicySettings operator_policy;
	TrustedPolicyRegistry trusted_policy_registry;
	bool         test_mode;
	bool         demo_site;
	bool         custom_site_dir;
	String       site_dir;
	int          custom_port;
	String       docs_repos_spec; // "label=path,..." for the docs portal; empty = the default allowlist
};

END_UPP_NAMESPACE

#endif
