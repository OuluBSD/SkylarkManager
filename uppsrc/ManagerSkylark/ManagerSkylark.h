#ifndef _reference_ManagerSkylark_ManagerSkylark_h_
#define _reference_ManagerSkylark_ManagerSkylark_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include <Manager/Manager.h>
#include <SweAgent/SweAgent.h>
#include <WebChat/WebChat.h>
#include <WebFramework/WebFramework.h>
#include "V2Model.h"
#include "DataStore.h"
#include "ScopedPolicy.h"
#include "GitHubPlatform.h"
#include <atomic>

NAMESPACE_UPP

struct CatalogIndexResult;
class CatalogIndexPin;
struct CatalogGeneration;

class ManagerSkylarkApp : public SkylarkApp {
public:
	struct BatchFinalizationResult : Moveable<BatchFinalizationResult> {
		String status;
		bool committed = false;
		String operation_id;
		Vector<String> changed_ids;
		Vector<String> revisions;
		uint64 generation = 0;
		String source_revision;
		String notification_status;
		// Return payload for the authorized caller; T31 owns durable/broadcast activity.
		ValueMap event;
	};
	explicit ManagerSkylarkApp(const String& isolated_test_root = String());

	bool Initialize(const Vector<String>& args);
	bool InitApp();

	ManagerStore& GetLocalStore()   { static StaticMutex sMutex; Mutex::Lock __(sMutex); if(!test_mode) local_store.LoadAll(); return local_store; }

	static ManagerSkylarkApp& Get();

	void StopServer();
	bool IsIsolatedTestMode() const {
		return test_mode && ip == "127.0.0.1" && !test_fixture_root.IsEmpty() && DirectoryExists(test_fixture_root);
	}
	static bool IsolatedTestModeActive() { return s_instance && s_instance->IsIsolatedTestMode(); }
	void EnableCatalogIndex(bool enabled) { catalog_index_enabled.store(enabled); }
	bool IsCatalogIndexEnabled() const { return catalog_index_enabled.load(); }
	const PolicySettings& GetInstancePolicy() const { return instance_policy; }
	EffectivePolicy ResolveEffectivePolicy(const PolicyInput& input) const;
	bool IsAssessmentSourceVisible(const String& workspace_id, const String& source_identity) const;
	bool InstallSyntheticPolicyContextForTests(const PolicySettings& instance,
	                                           const PolicySettings& workspace,
	                                           const PolicySettings& operator_preferences,
	                                           const TrustedPolicyRegistry& registry);
	void ResetSyntheticPolicyContextForTests();

	static bool RunSelfTests(int port = 18080);
	class CatalogReadHandle PinCatalog() const;
	class CatalogReadHandle PinLatestCatalog() const;
	bool RefreshCatalog();
	CatalogIndexResult RebuildCatalogIndex();
	Vector<const DataDocument*> QueryCatalogDocuments(const CatalogGeneration& catalog,
	                                                  const String& kind = String(),
	                                                  const String& collection = String(),
	                                                  const String& scope = String(),
	                                                  const String& workspace = String(),
	                                                  String *query_status = NULL) const;
	BatchFinalizationResult FinalizeBatch(const DataWriteAuthorization& authorization,
	                                      const String& operation_id,
	                                      const Vector<DataBatchChange>& changes,
	                                      DataWriteTestFailpoint test_failpoint = DATA_WRITE_FAIL_NONE);

	// Shared HTML rendering components
	static String RenderHtmlPage(const String& title, const String& active_nav, const String& body_content, const Http* http = nullptr);
	static PageBuilder RenderDataPageChrome(const String& title, const String& active_nav, const String& body_content, const TreeModel& tree);
	static String GetPageCustomStyles();
	static void   AddMarkdownSupport(PageBuilder& pb);
	static String RenderBadge(const String& status);
	static String RenderProgressBar(double percent);
	static NavigationModel BuildNavigation(const String& active_nav = "");
	static TreeModel       BuildMeshFleetTree(const String& active_nav = "", const String& active_node_id = "");
	static TreeModel       BuildGoalsTree(const V2Store& store, const String& active_id = "");
	static TreeModel       BuildProjectsTree(ManagerStore& store, const String& active_slug = "");
	static TreeModel       BuildDataTree(const DataStore& store, const String& active_id = "");

private:
	friend class SystemAuthService;
	static ManagerSkylarkApp* s_instance;
	static bool LoadCatalogRegistration(const DataSourceRegistration& registration,
	                                   One<V2Store>& v, One<DataStore>& d,
	                                   String& error, Vector<String>& source_errors);
	static bool RejectIncompleteCatalog(One<V2Store>& v, One<DataStore>& d,
	                                    String& error, Vector<String>& source_errors);
	bool LoadCatalogCandidates(One<V2Store>& v, One<DataStore>& d,
	                          String& error, Vector<String>& source_errors,
	                          const DataSourceRegistration* captured_registration = NULL);
	bool ReadCurrentCatalogRegistration(DataSourceRegistration& registration);
	static bool SameCatalogRegistration(const DataSourceRegistration& a, const DataSourceRegistration& b);

	std::atomic<bool> test_mode;
	std::atomic<bool> catalog_index_enabled;
	String       test_fixture_root;
	PolicySettings instance_policy;
	PolicySettings policy_workspace_settings;
	PolicySettings policy_operator_settings;
	TrustedPolicyRegistry trusted_policy_registry;
	bool         test_registration_provider = false;
	DataSourceRegistration test_registration;
	DataSourceRegistration test_registration_change;
	int          test_registration_change_point = 0;
	int          test_batch_notification_failpoint = 0;
	DataWriteTestFailpoint test_batch_recovery_failpoint = DATA_WRITE_FAIL_NONE;
	String       test_registration_scanned_root;
	ManagerStore local_store;
	int          custom_port;
	mutable Mutex catalog_mutex;
	Mutex catalog_refresh_mutex;
	Shared<struct CatalogGeneration> catalog;
	std::atomic<uint64> catalog_generation;
};

class CatalogReadHandle {
	Shared<class CatalogGeneration> pin;
	explicit CatalogReadHandle(const Shared<class CatalogGeneration>& p) : pin(p) {}
	friend class ManagerSkylarkApp;
public:
	CatalogReadHandle() {}
	const class CatalogGeneration* Get() const { return pin.Get(); }
	const class CatalogGeneration* operator->() const { return Get(); }
	const class CatalogGeneration& operator*() const { ASSERT(Get()); return *Get(); }
	operator bool() const { return !pin.IsEmpty(); }
};

class CatalogRequestPin {
public:
	CatalogRequestPin(ManagerSkylarkApp& app);
	~CatalogRequestPin();
private:
	CatalogReadHandle previous;
};

struct CatalogGeneration : Pte<CatalogGeneration> {
	One<V2Store> v2_owner;
	One<DataStore> data_owner;
	const V2Store& v2;
	const DataStore& data;
	uint64 generation = 0;
	String source_revision;
	String status;
	String v2_status;
	String data_status;
	Vector<String> diagnostics;
	CatalogGeneration(uint64 n, One<V2Store>&& v, One<DataStore>&& d,
	                  bool vok, bool dok, const String& load_error = String(),
	                  const Vector<String>& load_diagnostics = Vector<String>());
	bool IsComplete() const { return status == "loaded" || status == "loaded_with_errors"; }
	const V2Goal* FindGoal(const String& qualified_id) const { return v2.FindGoalExact(qualified_id); }
	const V2Issue* FindIssue(const String& qualified_id) const { return v2.FindIssueExact(qualified_id); }
	const V2Release* FindRelease(const String& qualified_id) const { return v2.FindReleaseExact(qualified_id); }
	const DataDocument* FindDocument(const String& qualified_id) const { return data.FindDocument(qualified_id); }
};

struct CatalogRecordRead {
	String status;
	ValueMap response;
};

CatalogRecordRead ReadCatalogRecord(const CatalogGeneration& catalog, const String& id,
	                                  bool allow_unique_unqualified = false);
int CatalogRecordExitCode(const CatalogRecordRead& read);

void RegisterManagerSkylarkHandler(void (*handler)(Http&), const char *id, const char *path,
	                               int (*progress)(int, Http&, int) = NULL);
void RegisterManagerSkylarkProgressHandler(void (*handler)(Http&), const char *id, const char *path,
	                                      int (*progress)(int, Http&, int));

END_UPP_NAMESPACE

#include "SystemAuth.h"

#undef SKYLARK
#undef SKYLARK_PROGRESS
#define SKYLARK(name, path) \
	void name(Http& http); \
	void name##_ManagerAuth(Http& http) { if(UPP::SystemAuthService::AuthorizeRequest(http, path)) { UPP::CatalogRequestPin catalog_request(UPP::ManagerSkylarkApp::Get()); name(http); } } \
	INITBLOCK { UPP::RegisterManagerSkylarkHandler(name##_ManagerAuth, #name, path); } \
	void name(Http& http)
#define SKYLARK_PROGRESS(name, path, progressCb) \
	void name(Http& http); \
	void name##_ManagerAuth(Http& http) { if(UPP::SystemAuthService::AuthorizeRequest(http, path)) { UPP::CatalogRequestPin catalog_request(UPP::ManagerSkylarkApp::Get()); name(http); } } \
	int name##_ManagerAuthProgress(int reason, Http& http, int count) { return progressCb(reason, http, count); } \
	INITBLOCK { UPP::RegisterManagerSkylarkProgressHandler(name##_ManagerAuth, #name, path, name##_ManagerAuthProgress); } \
	void name(Http& http)

#endif
