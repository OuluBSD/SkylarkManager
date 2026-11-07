#include "ManagerSkylark.h"
#include "SystemAuth.h"
#include "HostPlatform.h"
#include "CatalogIndex.h"

NAMESPACE_UPP

ManagerSkylarkApp* ManagerSkylarkApp::s_instance = NULL;
static thread_local CatalogReadHandle s_request_catalog;

static bool AuthorizeManagerWebChat(Http& http)
{
	return SystemAuthService::AuthorizeRequest(http);
}

static String Sha256Fingerprint(const String& value)
{
	return DataStore::ContentDigest(value);
}

static String RootIdentityFingerprint(const String& root)
{
	// U++ GetFullPath is lexical normalization, not physical canonicalization.
	// Safe roots reject symlink components, so this fingerprints normalized path identity.
	return Sha256Fingerprint(NormalizePath(root));
}

ManagerSkylarkApp::ManagerSkylarkApp(const String& isolated_test_root)
	: test_mode(!isolated_test_root.IsEmpty())
	, catalog_index_enabled(false)
	, test_fixture_root(isolated_test_root.IsEmpty() ? String() : NormalizePath(isolated_test_root))
	, custom_port(8080)
	, catalog_generation(0)
{
	s_instance = this;
	SetWebChatAuthorizationCallback(AuthorizeManagerWebChat);
	root = "";
	port = 8080;
	ip = "127.0.0.1";
	threads = 2;
	prefork = 0;
	use_caching = false;
}

EffectivePolicy ManagerSkylarkApp::ResolveEffectivePolicy(const PolicyInput& input) const
{
	return ScopedPolicyService::Resolve(instance_policy, policy_workspace_settings,
	                                    policy_operator_settings, trusted_policy_registry, input);
}

bool ManagerSkylarkApp::IsAssessmentSourceVisible(const String& workspace_id, const String& source_identity) const
{
	if(workspace_id.IsEmpty()||source_identity.IsEmpty()) return false;
	for(const PolicyProviderFact& provider:trusted_policy_registry.providers) {
		PolicyInput input; input.workspace.workspace_id=workspace_id; input.workspace.object_id=workspace_id;
		PolicyIdentity source; source.workspace_id=workspace_id; source.object_id=source_identity; input.sources.Add(source);
		input.provider=provider.identity;
		EffectivePolicy policy=ResolveEffectivePolicy(input);
		if(policy.status=="evaluated"&&policy.source_visible) return true;
	}
	return false;
}

bool ManagerSkylarkApp::InstallSyntheticPolicyContextForTests(
	const PolicySettings& instance, const PolicySettings& workspace,
	const PolicySettings& operator_preferences, const TrustedPolicyRegistry& registry)
{
	if(!IsIsolatedTestMode()) return false;
	String error;
	if(!registry.Validate(error) || !registry.configured ||
	   !ScopedPolicyService::Validate(instance, error) || !instance.configured ||
	   (workspace.configured && !ScopedPolicyService::Validate(workspace, error)) ||
	   (operator_preferences.configured && !ScopedPolicyService::Validate(operator_preferences, error))) return false;
	instance_policy = instance;
	policy_workspace_settings = workspace;
	policy_operator_settings = operator_preferences;
	trusted_policy_registry.configured = registry.configured;
	trusted_policy_registry.sources.Clear();
	trusted_policy_registry.ancestry.Clear();
	trusted_policy_registry.providers.Clear();
	trusted_policy_registry.workspaces.Clear();
	trusted_policy_registry.protected_ancestry.Clear();
	for(const PolicySourceFact& fact : registry.sources) trusted_policy_registry.sources.Add(fact);
	for(const PolicyAncestryFact& fact : registry.ancestry) trusted_policy_registry.ancestry.Add(fact);
	for(const PolicyProviderFact& fact : registry.providers) trusted_policy_registry.providers.Add(fact);
	for(const PolicyWorkspaceFact& fact : registry.workspaces) trusted_policy_registry.workspaces.Add(fact);
	for(const PolicyIdentity& fact : registry.protected_ancestry) trusted_policy_registry.protected_ancestry.Add(fact);
	return true;
}

void ManagerSkylarkApp::ResetSyntheticPolicyContextForTests()
{
	if(!IsIsolatedTestMode()) return;
	instance_policy = PolicySettings();
	policy_workspace_settings = PolicySettings();
	policy_operator_settings = PolicySettings();
	trusted_policy_registry.configured = false;
	trusted_policy_registry.sources.Clear();
	trusted_policy_registry.ancestry.Clear();
	trusted_policy_registry.providers.Clear();
	trusted_policy_registry.workspaces.Clear();
	trusted_policy_registry.protected_ancestry.Clear();
}

CatalogGeneration::CatalogGeneration(uint64 n, One<V2Store>&& v_store,
	                                 One<DataStore>&& data_store,
	                                 bool vok, bool dok, const String& load_error,
	                                 const Vector<String>& load_diagnostics)
	: v2_owner(std::move(v_store))
	, data_owner(std::move(data_store))
	, v2(*v2_owner)
	, data(*data_owner)
	, generation(n)
{
	const V2Store& v = v2;
	const DataStore& d = data;
	if(!vok || !dok || !load_error.IsEmpty() || v.IsLoadIncomplete() || d.IsLoadIncomplete()) {
		status = "load_error";
		v2_status = data_status = "load_error";
		if(!load_error.IsEmpty()) diagnostics.Add("catalog:" + load_error);
		for(const String& e : load_diagnostics) diagnostics.Add(e);
		if(v.IsLoadIncomplete()) diagnostics.Add("v2:load_incomplete");
		if(d.IsLoadIncomplete()) diagnostics.Add("data:load_incomplete");
	}
	else {
		v2_status = v.GetSourceErrorCount() ? "loaded_with_errors" : "loaded";
		data_status = d.GetSourceErrorCount() ? "loaded_with_errors" : "loaded";
		status = v2_status == "loaded_with_errors" || data_status == "loaded_with_errors" ? "loaded_with_errors" : "loaded";
	}
	Vector<String> revisions;
	revisions.Add("v2-source:" + v.GetSourceId());
	revisions.Add("data-source:" + d.GetSourceId());
	revisions.Add("v2-root-sha256:" + RootIdentityFingerprint(v.GetRootDir()));
	revisions.Add("data-root-sha256:" + RootIdentityFingerprint(d.GetRootDir()));
	for(const V2Goal& x : v.GetGoals()) revisions.Add("g:" + x.qualified_id + ":" + x.source_revision);
	for(const V2Issue& x : v.GetIssues()) revisions.Add("i:" + x.qualified_id + ":" + x.source_revision);
	for(const V2Release& x : v.GetReleases()) revisions.Add("r:" + x.qualified_id + ":" + x.source_revision);
	for(const DataDocument& x : d.GetDocuments()) revisions.Add("d:" + x.qualified_id + ":" + x.rel_path + ":" + x.source_revision);
	for(const String& x : d.GetSourceFingerprints()) revisions.Add("f:" + x);
	for(const String& x : v.GetSourceDiagnostics()) revisions.Add("ve:" + x);
	for(const String& x : d.GetSourceDiagnostics()) revisions.Add("de:" + x);
	revisions.Add("v2-errors:" + AsString(v.GetSourceErrorCount()));
	revisions.Add("data-errors:" + AsString(d.GetSourceErrorCount()));
	revisions.Add("data-scanned:" + AsString(d.GetScannedCount()));
	Sort(revisions);
	String combined;
	for(const String& x : revisions) combined << x << "\n";
	combined << "v2=" << v2_status << ";data=" << data_status;
	source_revision = status == "load_error" ? "unavailable" : Sha256Fingerprint(combined);
	for(const String& e : v.GetSourceDiagnostics()) diagnostics.Add("v2:" + e);
	for(const String& e : d.GetSourceDiagnostics()) diagnostics.Add("data:" + e);
}

static CatalogRecordRead CatalogReadFailure(const CatalogGeneration& catalog, const String& status)
{
	CatalogRecordRead result;
	result.status = status;
	result.response.Add("status", status);
	result.response.Add("catalog_generation", (int64)catalog.generation);
	result.response.Add("source_status", catalog.status);
	result.response.Add("v2_status", catalog.v2_status);
	result.response.Add("data_status", catalog.data_status);
	return result;
}

static bool ContainsAbsolutePath(const Value& value)
{
	if(IsString(value)) {
		String text = value;
		if(text.StartsWith("/") || (!text.IsEmpty() && text[0] == '\\') ||
		   (text.GetCount() > 2 && IsAlpha(text[0]) && text[1] == ':' && (text[2] == '/' || text[2] == '\\')))
			return true;
	}
	if(value.Is<ValueMap>()) {
		ValueMap map = value;
		for(int i = 0; i < map.GetCount(); i++)
			if(ContainsAbsolutePath(map.GetKey(i)) || ContainsAbsolutePath(map.GetValue(i))) return true;
	}
	if(value.Is<ValueArray>()) {
		ValueArray array = value;
		for(const Value& item : array) if(ContainsAbsolutePath(item)) return true;
	}
	return false;
}

CatalogRecordRead ReadCatalogRecord(const CatalogGeneration& catalog, const String& id,
	                                bool allow_unique_unqualified)
{
	if(catalog.status == "load_error")
		return CatalogReadFailure(catalog, "unavailable");

	const V2Goal* goal = NULL;
	const V2Issue* issue = NULL;
	const V2Release* release = NULL;
	const DataDocument* document = NULL;
	auto generic_visible = [](const DataDocument& d) {
		return d.kind != "review-package" && d.kind != "review-package-revision" &&
		       d.kind != "review-thread" && d.kind != "review-comment";
	};
	int exact_count = 0;
	auto add_exact = [&](int matched, auto* record, auto*& selected) {
		if(matched) { exact_count++; selected = record; }
	};
	for(const V2Goal& x : catalog.v2.GetGoals()) add_exact(x.qualified_id == id, &x, goal);
	for(const V2Issue& x : catalog.v2.GetIssues()) add_exact(x.qualified_id == id, &x, issue);
	for(const V2Release& x : catalog.v2.GetReleases()) add_exact(x.qualified_id == id, &x, release);
	for(const DataDocument& x : catalog.data.GetDocuments()) if(generic_visible(x)) add_exact(x.qualified_id == id, &x, document);
	if(exact_count > 1) return CatalogReadFailure(catalog, "ambiguous");

	bool is_unqualified = false;
	if(exact_count == 0) {
		int alias_count = 0;
		auto add_alias = [&](int matched, auto* record, auto*& selected) {
			if(matched) { alias_count++; selected = record; }
	};
		for(const V2Goal& x : catalog.v2.GetGoals())
			add_alias(x.id == id || x.id.EndsWith("/" + id), &x, goal);
		for(const V2Issue& x : catalog.v2.GetIssues())
			add_alias(x.id == id || x.id.EndsWith("/" + id), &x, issue);
		for(const V2Release& x : catalog.v2.GetReleases())
			add_alias(x.id == id || x.id.EndsWith("/" + id), &x, release);
		for(const DataDocument& x : catalog.data.GetDocuments()) if(generic_visible(x))
			add_alias(x.id == id || x.id.EndsWith("/" + id), &x, document);
		if(alias_count > 1) return CatalogReadFailure(catalog, "ambiguous");
		if(alias_count == 0) return CatalogReadFailure(catalog, "missing");
		is_unqualified = true;
		if(!allow_unique_unqualified) {
			CatalogRecordRead result = CatalogReadFailure(catalog, "unqualified");
			if(goal) result.response.Add("qualified_id", goal->qualified_id);
			else if(issue) result.response.Add("qualified_id", issue->qualified_id);
			else if(release) result.response.Add("qualified_id", release->qualified_id);
			else if(document) result.response.Add("qualified_id", document->qualified_id);
			return result;
		}
	}

	CatalogRecordRead result;
	result.status = "ok";
	String qualified_id, type, record_revision, source_revision, validation_state;
	Value record;
	bool valid = true, validated = false;
	if(goal) {
		qualified_id = goal->qualified_id; type = "goal"; record = goal->source_data;
		record_revision = source_revision = goal->source_revision; validation_state = "typed";
	}
	else if(issue) {
		qualified_id = issue->qualified_id; type = "issue"; record = issue->source_data;
		record_revision = source_revision = issue->source_revision; validation_state = "typed";
	}
	else if(release) {
		qualified_id = release->qualified_id; type = "release"; record = release->source_data;
		record_revision = source_revision = release->source_revision; validation_state = "typed";
	}
			else if(document) {
				qualified_id = document->qualified_id; type = "data_document"; record = document->data;
		record_revision = document->revision; source_revision = document->source_revision;
		validation_state = document->validation_state; valid = document->valid; validated = document->validated;
	}
	else
		return CatalogReadFailure(catalog, "missing");
	if(ContainsAbsolutePath(record) || ContainsAbsolutePath(Value(qualified_id)))
		return CatalogReadFailure(catalog, "unsafe_metadata");

	result.response.Add("status", "ok");
	result.response.Add("qualified_id", qualified_id);
	result.response.Add("record_type", type);
	result.response.Add("record", record);
	result.response.Add("record_revision", record_revision);
	result.response.Add("source_revision", source_revision);
	result.response.Add("catalog_generation", (int64)catalog.generation);
	result.response.Add("catalog_source_revision", catalog.source_revision);
	result.response.Add("validation_state", validation_state);
	result.response.Add("valid", valid);
	result.response.Add("validated", validated);
	result.response.Add("unqualified_lookup", is_unqualified);
	return result;
}

int CatalogRecordExitCode(const CatalogRecordRead& read)
{
	if(read.status == "ok") return 0;
	if(read.status == "unavailable") return 3;
	if(read.status == "missing") return 4;
	if(read.status == "ambiguous") return 5;
	return 2;
}

CatalogReadHandle ManagerSkylarkApp::PinCatalog() const
{
	if(s_request_catalog) return s_request_catalog;
	return PinLatestCatalog();
}

CatalogReadHandle ManagerSkylarkApp::PinLatestCatalog() const
{
	Mutex::Lock lock(catalog_mutex);
	return CatalogReadHandle(catalog);
}

CatalogRequestPin::CatalogRequestPin(ManagerSkylarkApp& app)
	: previous(s_request_catalog)
{
	s_request_catalog = app.PinLatestCatalog();
}

CatalogRequestPin::~CatalogRequestPin()
{
	s_request_catalog = previous;
}

bool ManagerSkylarkApp::RefreshCatalog()
{
	Mutex::Lock refresh_lock(catalog_refresh_mutex);
	int source_lock_fd = -1;
	DataSourceRegistration captured;
	bool explicit_registration = test_mode && test_registration_provider;
	bool have_registration = explicit_registration ? ReadCurrentCatalogRegistration(captured) :
	                         test_mode || DataStore::ReadConfiguredRegistration(captured);
	bool registration_ok = have_registration;
	if(explicit_registration && test_registration_change_point == 1)
		test_registration = test_registration_change;
	if(registration_ok && have_registration && (!test_mode || explicit_registration)) {
		DataSourceRegistration current;
		registration_ok = ReadCurrentCatalogRegistration(current) && SameCatalogRegistration(captured, current);
	}
	if(registration_ok && captured.enabled && (captured.read_only || captured.write_enabled)) {
		bool create_lock = captured.write_enabled && !captured.read_only;
		if(!DataStore::AcquireSourceLock(NormalizePath(captured.root), true, create_lock, source_lock_fd)) registration_ok = false;
	}
	if(explicit_registration && test_registration_change_point == 3)
		test_registration = test_registration_change;
	if(registration_ok && have_registration && (!test_mode || explicit_registration)) {
		DataSourceRegistration current;
		registration_ok = ReadCurrentCatalogRegistration(current) && SameCatalogRegistration(captured, current);
	}
	One<V2Store> next_v2;
	One<DataStore> next_data;
	String load_error;
	Vector<String> load_diagnostics;
	bool loaded = registration_ok && LoadCatalogCandidates(next_v2, next_data, load_error, load_diagnostics,
	                                    have_registration && (!test_mode || explicit_registration) ? &captured : NULL);
	if(explicit_registration && test_registration_change_point == 2)
		test_registration = test_registration_change;
	if(!test_mode || explicit_registration) {
		DataSourceRegistration current;
		bool current_ok = have_registration && ReadCurrentCatalogRegistration(current);
		if(!have_registration || !registration_ok || !current_ok || !SameCatalogRegistration(captured, current)) {
			loaded = false;
			load_error = have_registration ? "source_registration_changed" : "source_registration_unavailable";
			load_diagnostics.Add(have_registration ? "catalog:source_registration_changed" : "catalog:source_registration_unavailable");
			next_v2.Create();
			next_data.Create();
			next_v2->Clear();
			next_data->Clear();
		}
	}
	uint64 generation = ++catalog_generation;
	Shared<CatalogGeneration> next(new CatalogGeneration(generation, pick(next_v2), pick(next_data),
	                                                   loaded, loaded, load_error, load_diagnostics));
	{
		Mutex::Lock lock(catalog_mutex);
		catalog = next;
	}
	DataStore::ReleaseSourceLock(source_lock_fd);
	return loaded;
}

CatalogIndexResult ManagerSkylarkApp::RebuildCatalogIndex()
{
	CatalogIndexResult disabled;
	disabled.status = "disabled";
	if(!catalog_index_enabled.load()) return disabled;
	CatalogReadHandle snapshot = PinLatestCatalog();
	if(!snapshot) {
		CatalogIndexResult unavailable;
		unavailable.status = "unavailable";
		return unavailable;
	}
	return CatalogIndex::Rebuild(*snapshot);
}

Vector<const DataDocument*> ManagerSkylarkApp::QueryCatalogDocuments(const CatalogGeneration& catalog,
	                                                                 const String& kind,
	                                                                 const String& collection,
	                                                                 const String& scope,
	                                                                 const String& workspace,
	                                                                 String *query_status) const
{
	String status;
	Vector<const DataDocument*> result;
	if(!catalog.IsComplete()) {
		if(query_status) *query_status = "incomplete";
		return result;
	}
	auto matches = [&](const DataDocument& d) {
		return (kind.IsEmpty() || d.kind == kind) && (collection.IsEmpty() || d.collection == collection) &&
		       (scope.IsEmpty() || d.scope == scope) && (workspace.IsEmpty() || d.workspace_id == workspace);
	};
	auto canonical = [&]() {
		result.Clear();
		for(const DataDocument& d : catalog.data.GetDocuments())
			if(matches(d)) result.Add(&d);
		Sort(result, [](const DataDocument *a, const DataDocument *b) {
			if(a->source_id != b->source_id) return a->source_id < b->source_id;
			if(a->workspace_id != b->workspace_id) return a->workspace_id < b->workspace_id;
			return a->qualified_id < b->qualified_id;
		});
	};
	if(!catalog_index_enabled.load()) {
		canonical();
		if(query_status) *query_status = "disabled_json";
		return result;
	}
	CatalogIndexPin pin = CatalogIndex::Pin(catalog, status);
	if(!pin.IsValid()) {
		canonical();
		if(query_status) *query_status = "fallback_json:" + status;
		return result;
	}
	CatalogIndexFilter filter;
	filter.kind = kind;
	filter.collection = collection;
	filter.scope = scope;
	filter.workspace_id = workspace;
	filter.source_id = catalog.data.GetSourceId();
	filter.data_documents_only = true;
	Vector<const DataDocument*> indexed;
	for(int offset = 0;; offset += 500) {
		Vector<CatalogIndexRow> page = CatalogIndex::Page(pin, filter, offset, 500, status);
		if(status != "ready") {
			canonical();
			if(query_status) *query_status = "fallback_json:" + status;
			return result;
		}
		for(const CatalogIndexRow& row : page) {
			const DataDocument *document = catalog.FindDocument(row.qualified_id);
			if(!document || document->source_id != row.source_id || document->workspace_id != row.workspace_id ||
			   document->revision != row.revision || document->source_revision != row.source_revision || document->kind != row.kind ||
			   document->collection != row.collection || document->scope != row.scope || !matches(*document)) {
				canonical();
				if(query_status) *query_status = "fallback_json:projection_mismatch";
				return result;
			}
			indexed.Add(document);
		}
		if(page.GetCount() < 500) break;
	}
	result = pick(indexed);
	if(query_status) *query_status = "sqlite_pinned";
	return result;
}

ManagerSkylarkApp::BatchFinalizationResult ManagerSkylarkApp::FinalizeBatch(
	const DataWriteAuthorization& authorization, const String& operation_id,
	const Vector<DataBatchChange>& changes, DataWriteTestFailpoint test_failpoint)
{
	BatchFinalizationResult result;
	result.operation_id = operation_id;
	result.status = "invalid_request";
	if(!authorization.IsAllowed() || operation_id.IsEmpty() || changes.IsEmpty()) return result;

	CatalogReadHandle starting = PinLatestCatalog();
	if(!starting || starting->status == "load_error") { result.status = "catalog_unavailable"; return result; }
	const DataStore& store = starting->data;
	if(store.GetSourceId() != authorization.source_id) { result.status = "scope_denied"; return result; }
	bool scoped_record = false;
	for(const DataDocument& document : store.GetDocuments())
		if(document.workspace_id == authorization.workspace_id && document.source_id == authorization.source_id) {
			scoped_record = true;
			break;
		}
	if(!scoped_record) { result.status = "scope_denied"; return result; }

	DataBatchResult published = store.PublishBatch(authorization, operation_id, changes, test_failpoint);
	if(published.status == "journal_recovery_required") {
		DataWriteTestFailpoint recovery_failpoint = test_mode ? test_batch_recovery_failpoint : DATA_WRITE_FAIL_NONE;
		JournalRecoveryResult pending_recovery = store.RecoverPendingBatches(authorization, recovery_failpoint);
		if(pending_recovery.status != "clean" && pending_recovery.status != "recovered") {
			result.status = "commit_outcome_unknown";
			result.notification_status = "recovery_pending";
			return result;
		}
		// Recovery resolved the blocker. Retry the identical request exactly once;
		// T46 decides whether this is a durable receipt replay or a safe new commit.
		published = store.PublishBatch(authorization, operation_id, changes, test_failpoint);
	}
	if(published.status != "committed" && published.status != "already_committed") {
		result.status = published.status;
		return result;
	}
	result.committed = true;
	result.changed_ids = clone(published.changed_ids);
	result.revisions = clone(published.revisions);
	Sort(result.changed_ids);
	// PublishBatch emits aligned pairs in canonical qualified-ID order.
	// Reorder revisions with their IDs for a deterministic result.
	for(int i = 0; i < result.changed_ids.GetCount(); i++) {
		int at = -1;
		for(int n = 0; n < published.changed_ids.GetCount(); n++)
			if(published.changed_ids[n] == result.changed_ids[i]) { at = n; break; }
		if(at >= 0) result.revisions[i] = published.revisions[at];
	}
	ValueArray ids, revisions;
	for(int i = 0; i < result.changed_ids.GetCount(); i++) {
		ids.Add(result.changed_ids[i]);
		revisions.Add(result.revisions[i]);
	}
	result.event.Add("type", "data.batch.committed");
	result.event.Add("operation_id", operation_id);
	result.event.Add("changed_ids", ids);
	result.event.Add("revisions", revisions);
	result.event.Add("catalog_generation", (int64)0);
	result.event.Add("source_revision", String());

	DataWriteTestFailpoint recovery_failpoint = test_mode ? test_batch_recovery_failpoint : DATA_WRITE_FAIL_NONE;
	JournalRecoveryResult recovered = store.RecoverPendingBatches(authorization, recovery_failpoint);
	if(recovered.status != "clean" && recovered.status != "recovered") {
		result.status = "committed_notification_pending";
		result.notification_status = "recovery_pending";
		return result;
	}
	if(test_mode && test_batch_notification_failpoint == 1) {
		result.status = "committed_notification_pending";
		result.notification_status = "refresh_failed";
		return result;
	}
	if(!RefreshCatalog()) {
		result.status = "committed_notification_pending";
		result.notification_status = "refresh_failed";
		return result;
	}
	CatalogReadHandle refreshed = PinLatestCatalog();
	if(!refreshed || refreshed->status == "load_error" ||
	   refreshed->data.GetSourceId() != authorization.source_id) {
		result.status = "committed_notification_pending";
		result.notification_status = "refresh_failed";
		return result;
	}
	result.generation = refreshed->generation;
	result.source_revision = refreshed->source_revision;
	result.notification_status = "published";
	result.event.Set("catalog_generation", (int64)result.generation);
	result.event.Set("source_revision", result.source_revision);
	if(test_mode && test_batch_notification_failpoint == 2) {
		result.status = "committed_notification_pending";
		result.notification_status = "event_pending";
		return result;
	}
	result.status = published.status;
	return result;
}

bool ManagerSkylarkApp::ReadCurrentCatalogRegistration(DataSourceRegistration& registration)
{
	if(test_mode && test_registration_provider) {
		registration = test_registration;
		return true;
	}
	return DataStore::ReadConfiguredRegistration(registration);
}

bool ManagerSkylarkApp::SameCatalogRegistration(const DataSourceRegistration& a, const DataSourceRegistration& b)
{
	return a.id == b.id && NormalizePath(a.root) == NormalizePath(b.root) &&
	       a.enabled == b.enabled && a.read_only == b.read_only && a.write_enabled == b.write_enabled;
}

bool ManagerSkylarkApp::LoadCatalogCandidates(One<V2Store>& v,
	                                         One<DataStore>& d, String& error,
	                                         Vector<String>& load_diagnostics,
	                                         const DataSourceRegistration* captured_registration)
{
	v.Create();
	d.Create();
	bool vok = false, dok = false;
	if(captured_registration) {
		if(test_mode && test_registration_provider)
			test_registration_scanned_root = NormalizePath(captured_registration->root);
		bool loaded = LoadCatalogRegistration(*captured_registration, v, d, error, load_diagnostics);
		if(loaded && test_mode && test_registration_provider && captured_registration->write_enabled && !captured_registration->read_only)
			d->SetSyntheticRegistrationForTests(*captured_registration);
		return loaded;
	}
	if(test_mode) {
		const String captured_fixture_root = test_fixture_root;
		vok = v->LoadFixtureForTests(AppendFileName(captured_fixture_root, "v2"));
		dok = d->LoadFixtureForTests(AppendFileName(captured_fixture_root, "data"));
	}
	else {
		error = "source_registration_unavailable";
		load_diagnostics.Add("v2:source_registration_unavailable");
		load_diagnostics.Add("data:source_registration_unavailable");
	}
	if(vok && dok && RejectIncompleteCatalog(v, d, error, load_diagnostics)) return false;
	if(!vok || !dok) {
		if(error.IsEmpty()) {
			if(!vok && !dok) error = "both_sources_unavailable";
			else if(!vok) error = "version_source_unavailable";
			else error = "structured_source_unavailable";
		}
		if(!vok) load_diagnostics.Add("v2:source_load_failed");
		if(!dok) load_diagnostics.Add("data:source_load_failed");
		for(const String& e : v->GetSourceDiagnostics()) load_diagnostics.Add("v2:" + e);
		for(const String& e : d->GetSourceDiagnostics()) load_diagnostics.Add("data:" + e);
		if(vok) {
			load_diagnostics.Add("v2:records_discarded_due_to_catalog_load_error");
		}
		if(dok) {
			load_diagnostics.Add("data:records_discarded_due_to_catalog_load_error");
		}
		v->Clear();
		d->Clear();
		return false;
	}
	return true;
}

bool ManagerSkylarkApp::LoadCatalogRegistration(const DataSourceRegistration& registration,
	                                            One<V2Store>& v,
	                                            One<DataStore>& d,
	                                            String& error,
	                                            Vector<String>& load_diagnostics)
{
	if(!v) v.Create();
	if(!d) d.Create();
	const String captured_root = registration.root;
	const String captured_id = registration.id;
	bool source_readable = registration.enabled && (registration.read_only || registration.write_enabled);
	bool vok = source_readable && v->LoadRoot(captured_root, captured_id);
	bool dok = source_readable && d->LoadRoot(captured_root, captured_id,
	                                         registration.write_enabled && !registration.read_only);
	if(vok && dok && RejectIncompleteCatalog(v, d, error, load_diagnostics)) return false;
	if(!vok || !dok) {
		error = !registration.enabled ? "source_registration_disabled" :
		        (!registration.read_only && !registration.write_enabled) ? "source_registration_not_read_only" :
		        (!vok && !dok ? "both_sources_unavailable" : (!vok ? "version_source_unavailable" : "structured_source_unavailable"));
		if(!vok) load_diagnostics.Add("v2:source_load_failed");
		if(!dok) load_diagnostics.Add("data:source_load_failed");
		for(const String& e : v->GetSourceDiagnostics()) load_diagnostics.Add("v2:" + e);
		for(const String& e : d->GetSourceDiagnostics()) load_diagnostics.Add("data:" + e);
		if(vok) {
			load_diagnostics.Add("v2:records_discarded_due_to_catalog_load_error");
		}
		if(dok) {
			load_diagnostics.Add("data:records_discarded_due_to_catalog_load_error");
		}
		v->Clear();
		d->Clear();
		return false;
	}
	return true;
}

bool ManagerSkylarkApp::RejectIncompleteCatalog(One<V2Store>& v,
	                                            One<DataStore>& d,
	                                            String& error,
	                                            Vector<String>& load_diagnostics)
{
	bool v2_incomplete = v->IsLoadIncomplete();
	bool data_incomplete = d->IsLoadIncomplete();
	if(!v2_incomplete && !data_incomplete) return false;
	error = "source_budget_exhausted";
	if(v2_incomplete) load_diagnostics.Add("v2:load_incomplete");
	else load_diagnostics.Add("v2:records_discarded_due_to_catalog_load_error");
	if(data_incomplete) load_diagnostics.Add("data:load_incomplete");
	else load_diagnostics.Add("data:records_discarded_due_to_catalog_load_error");
	for(const String& diagnostic : v->GetSourceDiagnostics()) load_diagnostics.Add("v2:" + diagnostic);
	for(const String& diagnostic : d->GetSourceDiagnostics()) load_diagnostics.Add("data:" + diagnostic);
	v->Clear();
	d->Clear();
	return true;
}

ManagerSkylarkApp& ManagerSkylarkApp::Get()
{
	ASSERT(s_instance);
	return *s_instance;
}

bool ManagerSkylarkApp::InitApp()
{
	if(test_mode) {
		String temp_root = NormalizePath(GetTempDirectory());
		if(test_fixture_root.IsEmpty() ||
		   !test_fixture_root.StartsWith(temp_root + DIR_SEP) ||
		   !DataStore::IsSafeRoot(test_fixture_root) ||
		   local_store.RootDir() != NormalizePath(AppendFileName(test_fixture_root, "mgr"))) {
			RLOG("Self-test local store root does not match its isolated fixture.");
			return false;
		}
	}
	{
		int source_lock_fd = -1;
		DataSourceRegistration captured;
		bool explicit_registration = test_mode && test_registration_provider;
		bool have_registration = explicit_registration ? ReadCurrentCatalogRegistration(captured) :
		                         test_mode || DataStore::ReadConfiguredRegistration(captured);
		bool registration_ok = have_registration;
		bool registration_boundary_failed = false;
		if(explicit_registration && test_registration_change_point == 1)
			test_registration = test_registration_change;
		if(registration_ok && have_registration && (!test_mode || explicit_registration)) {
			DataSourceRegistration current;
			registration_ok = ReadCurrentCatalogRegistration(current) && SameCatalogRegistration(captured, current);
			registration_boundary_failed = !registration_ok;
		}
		if(registration_ok && have_registration && captured.enabled && (captured.read_only || captured.write_enabled) &&
		   (!test_mode || explicit_registration)) {
			bool create_lock = captured.write_enabled && !captured.read_only;
			if(!DataStore::AcquireSourceLock(NormalizePath(captured.root), true, create_lock, source_lock_fd)) {
				registration_ok = false;
				registration_boundary_failed = true;
			}
		}
		if(explicit_registration && test_registration_change_point == 3)
			test_registration = test_registration_change;
		if(registration_ok && have_registration && (!test_mode || explicit_registration)) {
			DataSourceRegistration current;
			registration_ok = ReadCurrentCatalogRegistration(current) && SameCatalogRegistration(captured, current);
			registration_boundary_failed = !registration_ok;
		}
		One<V2Store> initial_v2;
		One<DataStore> initial_data;
		String load_error;
		Vector<String> load_diagnostics;
		bool loaded = registration_ok && LoadCatalogCandidates(initial_v2, initial_data, load_error, load_diagnostics,
		                                    have_registration && (!test_mode || explicit_registration) ? &captured : NULL);
		if(explicit_registration && test_registration_change_point == 2)
			test_registration = test_registration_change;
		if(!test_mode || explicit_registration) {
			DataSourceRegistration current;
			bool current_ok = have_registration && ReadCurrentCatalogRegistration(current);
			if(!have_registration || !registration_ok || !current_ok || !SameCatalogRegistration(captured, current)) {
				if(have_registration) registration_boundary_failed = true;
				registration_ok = false;
				loaded = false;
				load_error = have_registration ? "source_registration_changed" : "source_registration_unavailable";
				load_diagnostics.Add(have_registration ? "catalog:source_registration_changed" : "catalog:source_registration_unavailable");
				initial_v2.Create();
				initial_data.Create();
				initial_v2->Clear();
				initial_data->Clear();
			}
		}
		if(!loaded)
			RLOG("Catalog sources unavailable; publishing an explicit load-error generation.");
		uint64 generation = ++catalog_generation;
		Shared<CatalogGeneration> initial(new CatalogGeneration(generation, pick(initial_v2), pick(initial_data),
		                                                   loaded, loaded, load_error, load_diagnostics));
		Mutex::Lock lock(catalog_mutex);
		catalog = initial;
		DataStore::ReleaseSourceLock(source_lock_fd);
		if(registration_boundary_failed) return false;
	}
	local_store.LoadAll();
	return true;
}

void ManagerSkylarkApp::StopServer()
{
	Quit();
	for(int i = 0; i < threads + 2; i++) {
		HttpRequest req(Format("http://127.0.0.1:%d/quit", port));
		req.Timeout(200);
		req.Execute();
	}
}

bool ManagerSkylarkApp::Initialize(const Vector<String>& args)
{
	CommandLineArguments cla;
	cla.SetDescription("ManagerSkylark - Structured workspace and operator oversight web application");
	cla.AddArg("help", 'h', "Show help options", false);
	cla.AddArg("port", 'p', "HTTP server port", true, "port");
	cla.AddArg("test", 0, "Run automated HTTP and store verification", false);
	cla.AddArg("enable-catalog-index", 0, "Explicitly allow private catalog index queries", false);
	cla.AddArg("catalog-index-rebuild", 0, "Rebuild the private catalog index and exit (requires explicit opt-in)", false);

	if(!cla.Parse(args))
		return false;

	if(cla.IsArg('h') || cla.IsArg("help")) {
		cla.PrintHelp();
		return false;
	}

	// Test mode is entered only by the isolated self-test dispatcher, which
	// provides fixture roots before constructing/initializing the app.
	test_mode = false;
	catalog_index_enabled.store(cla.IsArg("enable-catalog-index"));

	String p_str = cla.GetArg('p');
	if(p_str.IsEmpty())
		p_str = cla.GetArg("port");

	if(!p_str.IsEmpty()) {
		custom_port = ScanInt(p_str);
		if(custom_port > 0 && custom_port < 65536)
			port = custom_port;
	}

	return InitApp();
}

String ManagerSkylarkApp::RenderBadge(const String& status)
{
	String s = ToLower(status);
	String bg = "var(--wf-surface-hover, #2d3748)", fg = "var(--wf-fg-normal, #d3dae3)";
	if(s == "done" || s == "released" || s == "resolved") {
		bg = "var(--wf-accent-positive, #00aa7f)"; fg = "#ffffff";
	}
	else if(s == "active" || s == "in-progress") {
		bg = "var(--wf-accent-focus, #00a1c5)"; fg = "#ffffff";
	}
	else if(s == "open" || s == "planned" || s == "accepted" || s == "verified") {
		bg = "var(--wf-accent-hover, #0088a6)"; fg = "#ffffff";
	}
	else if(s == "blocked" || s == "rejected") {
		bg = "var(--wf-accent-negative, #c83771)"; fg = "#ffffff";
	}
	else if(s == "proposed" || s == "draft") {
		bg = "var(--wf-accent-neutral, #6d55aa)"; fg = "#ffffff";
	}

	return Format("<span class=\"wf-badge\" style=\"display:inline-block; padding:3px 8px; border-radius:12px; font-size:12px; font-weight:600; color:%s; background-color:%s;\">%s</span>",
	              fg, bg, EscapeHtml(status));
}

String ManagerSkylarkApp::RenderProgressBar(double percent)
{
	if(percent < 0) percent = 0;
	if(percent > 100) percent = 100;
	return Format("<div style=\"width:100%%; background-color:var(--wf-bg-alt, #1c202f); border-radius:6px; height:12px; overflow:hidden; border:1px solid var(--wf-border-color, rgba(0,161,197,0.2));\">"
	              "<div style=\"width:%.1f%%; background-color:var(--wf-accent-focus, #00a1c5); height:100%%; border-radius:5px;\"></div>"
	              "</div><span style=\"font-size:11px; color:var(--wf-fg-inactive, #8b919d);\">%.0f%%</span>",
	              percent, percent);
}

NavigationModel ManagerSkylarkApp::BuildNavigation(const String& active_nav)
{
	NavigationModel nav;

	// Host and repository details are configuration-driven; unsafe operations
	// stay visibly unavailable until they have a bounded implementation.
	NavCategory& cat_host = nav.AddCategory("host", "Local System", "server", "Read-only host telemetry and explicitly configured local sources");
	NavSection& sec_host_local = cat_host.AddSection("Local System");
	sec_host_local.Add("host", "Host Overview", "/host", "server", "Read-only hardware metrics and system status").Active(active_nav == "host" || active_nav == "host_overview");
	sec_host_local.Add("host_files", "File Browser (Disabled)", "/host/files", "folder", "Local file browsing is disabled pending scoped access controls").Badge("Disabled", "neutral").Active(active_nav == "host_files");
	sec_host_local.Add("host_repos", "Repository Browser (Disabled)", "/host/repos", "git-branch", "Local repository discovery requires an explicitly configured source root").Badge("Disabled", "neutral").Active(active_nav == "host_repos");
	sec_host_local.Add("host_storage", "Disk Storage", "/host/storage", "database", "Read-only mount and storage summaries").Active(active_nav == "host_storage");

	NavSection& sec_host_sync = cat_host.AddSection("Configured Nodes");
	sec_host_sync.Add("host_fleet", "Fleet Operations (Disabled)", "/host/fleet", "refresh-cw", "Remote synchronization and builds are disabled until reviewed and explicitly configured").Badge("Disabled", "neutral").Active(active_nav == "host_fleet");
	sec_host_sync.Add("host_nodes", "Node Configuration", "/host/nodes", "cpu", "Explicitly configured nodes; empty by default").Active(active_nav == "host_nodes");
	sec_host_sync.Add("host_settings", "Node Settings", "/host/settings", "settings", "Manage explicitly configured nodes").Active(active_nav == "host_settings");
	// External source-specific routes stay out of the global navigation until
	// backed by a configured repository/source registry.

	// Pillar 4: Local AI Hub
	NavCategory& cat_ai = nav.AddCategory("sweagent", "Work and Review", "robot", "Structured work, review state and local chat prototype");
	NavSection& sec_ai_ops = cat_ai.AddSection("Workspaces");
	sec_ai_ops.Add("assessments", "Research Assessments", "/assessments", "search", "Stored evidence-backed findings and candidate comparisons").Active(active_nav.StartsWith("assessment"));
	sec_ai_ops.Add("reviews", "Review Packages", "/reviews", "Private revision-pinned package discussion; not an approval decision").Active(active_nav.StartsWith("review"));
	sec_ai_ops.Add("chat", "Local Chat Prototype", "/chat", "chat", "Escaped, local-only conversation preview; no model is connected").Badge("Prototype", "neutral").Active(active_nav == "chat");
	sec_ai_ops.Add("sweagent", "Agent Runs (Disabled)", "/sweagent", "terminal", "External agent dispatch is disabled pending provider policy").Badge("Disabled", "neutral").Active(active_nav == "sweagent");
	sec_ai_ops.Add("projects", "Local Workspaces", "/projects", "layers", "Structured local implementation records").Active(active_nav == "projects");
	sec_ai_ops.Add("goals", "Goals", "/goals", "flag", "Structured outcomes and acceptance criteria").Active(active_nav == "goals");

	NavSection& sec_ai_api = cat_ai.AddSection("API & Diagnostics");
	sec_ai_api.Add("spec", "Framework Spec", "/api/framework/spec", "code", "Semantic JSON tree and API surface for agents").Badge("JSON", "cyan").Active(active_nav == "spec");
	sec_ai_api.Add("health", "Health Status", "/api/health", "server", "Application uptime and health metrics").Badge("HTTP 200", "emerald").Active(active_nav == "health");

	// Pillar 5: Generic Data Web
	NavCategory& cat_data = nav.AddCategory("data", "Data Web", "database", "Generic workspace data discovery and interactive architecture diagrams");
	NavSection& sec_data = cat_data.AddSection("Data & Diagrams");
	sec_data.Add("data", "Data Explorer", "/data", "database", "Generic JSON document discovery across the workspace root").Active(active_nav == "data");
	sec_data.Add("data_diagrams", "Topology Diagrams", "/data/diagrams", "cpu", "Interactive architecture topology maps and diagram sources").Active(active_nav == "data_diagrams");

	if(active_nav.StartsWith("host"))
		nav.SetActive("host", active_nav);
	else if(active_nav.StartsWith("node"))
		nav.SetActive("host", active_nav);
	else if(active_nav.StartsWith("repo") || active_nav == "user" || active_nav.StartsWith("gh"))
		nav.SetActive("data", "data");
	else if(active_nav.StartsWith("data"))
		nav.SetActive("data", active_nav);
	else if(active_nav == "chat" || active_nav == "sweagent" || active_nav == "projects" || active_nav == "goals" || active_nav == "issues" || active_nav == "releases" || active_nav == "spec" || active_nav == "health")
		nav.SetActive("sweagent", active_nav);

	return nav;
}

TreeModel ManagerSkylarkApp::BuildProjectsTree(ManagerStore& store, const String& active_slug)
{
	TreeModel tree;
	const Vector<Project>& projs = store.Projects();

	for(int i = 0; i < projs.GetCount(); i++) {
		const Project& p = projs[i];
		bool p_act = (p.slug == active_slug);

		TreeNode& p_node = tree.Add(p.slug, p.title, "/project/" + p.slug, "folder")
		                       .Icon("folder", "folder-open")
		                       .Expanded(p_act || projs.GetCount() <= 3)
		                       .Selected(p_act)
		                       .Badge(p.status, p.status == "active" ? "emerald" : "cyan")
		                       .Tooltip(p.description);

		Vector<const Phase*> phases = store.PhasesFor(p.slug);
		for(int ph = 0; ph < phases.GetCount(); ph++) {
			const Phase* phase = phases[ph];
			TreeNode& ph_node = p_node.AddChild(p.slug + "-ph-" + AsString(phase->seq),
			                                    phase->title,
			                                    "/project/" + p.slug,
			                                    "layout")
			                          .Expanded(p_act);

			Vector<const Task*> tasks = store.TasksForPhase(p.slug, phase->seq);
			for(int t = 0; t < tasks.GetCount(); t++) {
				const Task* task = tasks[t];
				TreeNode& t_node = ph_node.AddChild(p.slug + "-tsk-" + task->slug,
				                                   task->title,
				                                   "/project/" + p.slug,
				                                   "check");
				t_node.Badge(task->status, task->status == "done" ? "emerald" : "neutral");
			}
		}
	}

	return tree;
}

TreeModel ManagerSkylarkApp::BuildMeshFleetTree(const String& active_nav, const String& active_node_id)
{
	TreeModel tree;
	Vector<DistributedNode> nodes = HostPlatformService::GetConfiguredNodes(false);

	// Fleet Root Node; the node list comes only from explicit runtime config.
	TreeNode& fleet_root = tree.Add("fleet-root", "Configured Nodes", "/host/nodes", "cpu")
	                           .Icon("cpu", "cpu")
	                           .Expanded(true);

	for(int n = 0; n < nodes.GetCount(); n++) {
		const DistributedNode& node = nodes[n];
		bool is_act_node = (active_node_id == node.id || active_nav.Find(node.id) >= 0 ||
		                   (node.is_local && (active_nav == "host" || active_nav.StartsWith("host_"))));

		String base_url = node.is_local ? "/host" : ("/node/" + node.id);
		String node_title = node.name + " (" + node.hostname + ")";

		bool is_overview_act = (is_act_node && active_nav.Find("overview") >= 0);
		bool is_files_act = (is_act_node && active_nav.Find("files") >= 0);
		bool is_repos_act = (is_act_node && active_nav.Find("repos") >= 0);
		bool is_storage_act = (is_act_node && active_nav.Find("storage") >= 0);

		TreeNode& n_node = fleet_root.AddChild("node-" + node.id, node_title, base_url + "/overview", node.is_local ? "monitor" : "server")
		                             .Icon(node.is_local ? "monitor" : "server", node.is_local ? "monitor" : "server")
		                             .Expanded(is_act_node || nodes.GetCount() <= 3)
		                             .Selected(is_overview_act)
		                             .Badge(node.status, node.status == "online" ? "emerald" : "cyan");

		n_node.AddChild("node-" + node.id + "-overview", "Overview", base_url + "/overview", "activity")
		      .Selected(is_overview_act);

		n_node.AddChild("node-" + node.id + "-files", "Home Files", base_url + "/files", "folder")
		      .Selected(is_files_act);

		TreeNode& repos_node = n_node.AddChild("node-" + node.id + "-repos", "Repositories", base_url + "/repos", "git-branch")
		                             .Icon("git-branch", "git-branch")
		                             .Expanded(is_repos_act || is_act_node)
		                             .Selected(is_repos_act);

		Vector<HostRepoInfo> repos = HostPlatformService::GetRepositoriesForNode(node.id);
		repos_node.Badge(AsString(repos.GetCount()), is_repos_act ? "emerald" : "neutral");

		// If this node is active or viewing repos, list the actual repositories!
		if(is_act_node || is_repos_act) {
			for(int r = 0; r < repos.GetCount(); r++) {
				const HostRepoInfo& repo = repos[r];
				String repo_url = is_repos_act ? (base_url + "/repos#repo-" + repo.name)
				                               : (base_url + "/files/Dev/" + repo.name);
				TreeNode& r_child = repos_node.AddChild("repo-" + node.id + "-" + repo.name, repo.name, repo_url, "code");
				if(!repo.branch.IsEmpty()) {
					r_child.Badge(repo.branch, (repo.branch == "master" || repo.branch == "main") ? "neutral" : "emerald");
				}
			}
		}

		n_node.AddChild("node-" + node.id + "-storage", "Disk Storage", base_url + "/storage", "database")
		      .Selected(is_storage_act);
	}

	// Mesh Operations
	TreeNode& ops_node = tree.Add("fleet-ops", "Cluster Operations", "/host/fleet", "settings")
	                         .Icon("settings", "settings")
	                         .Expanded(active_nav == "host_fleet" || active_nav == "host_nodes" || active_nav == "host_settings" || active_nav == "mesh_fleet" || active_nav == "mesh_nodes" || active_nav == "mesh_settings");

	ops_node.AddChild("ops-fleet", "Fleet Sync & Build", "/host/fleet", "refresh-cw")
	        .Selected(active_nav == "host_fleet" || active_nav == "mesh_fleet");
	ops_node.AddChild("ops-topology", "Mesh Topology & Latency", "/host/nodes", "activity")
	        .Selected(active_nav == "host_nodes" || active_nav == "mesh_nodes");
	ops_node.AddChild("ops-settings", "Mesh Settings & Credentials", "/host/settings", "sliders")
	        .Selected(active_nav == "host_settings" || active_nav == "mesh_settings");

	return tree;
}

TreeModel ManagerSkylarkApp::BuildGoalsTree(const V2Store& store, const String& active_id)
{
	TreeModel tree;
	const Vector<V2Goal>& goals = store.GetGoals();

	TreeNode& root = tree.Add("goals-root", "V2 Architecture Goals", "/goals", "flag")
	                     .Icon("flag", "flag")
	                     .Expanded(true);

	for(int g = 0; g < goals.GetCount(); g++) {
		const V2Goal& goal = goals[g];
		bool g_act = (goal.qualified_id == active_id);

		TreeNode& g_node = root.AddChild("goal-" + goal.qualified_id, goal.title, "/goal/" + goal.qualified_id, "flag")
		                       .Icon("flag", "flag")
		                       .Expanded(g_act || goals.GetCount() <= 4)
		                       .Selected(g_act)
		                       .Badge(Format("%.0f%%", goal.GetProgressPercent()), "cyan");

		for(int t = 0; t < goal.tasks.GetCount(); t++) {
			const V2GoalNode& task = goal.tasks[t];
			TreeNode& t_node = g_node.AddChild("task-" + goal.qualified_id + "-" + task.id,
			                                   task.title,
			                                   "/goal/" + goal.qualified_id + "#task-" + task.id,
			                                   task.kind == "phase" ? "layout" : "check");
			t_node.Badge(task.status, task.status == "done" ? "emerald" : "neutral");
		}
	}

	return tree;
}

String ManagerSkylarkApp::RenderHtmlPage(const String& title, const String& active_nav, const String& body_content, const Http* http)
{
	NavigationModel nav = BuildNavigation(active_nav);
	String rendered_body = body_content;
	if(active_nav == "goals" || active_nav == "issues" || active_nav == "releases") {
		auto catalog = Get().PinCatalog();
		if(catalog && catalog->v2_status != "loaded")
			rendered_body = "<div role=\"status\" class=\"card\">Version source status: " + EscapeHtml(catalog->v2_status) + ". Source diagnostics are available through the authorized catalog API.</div>" + rendered_body;
	}

	PageBuilder pb;
	pb.Title(title + " - Manager Skylark")
	  .Brand("Manager Skylark", "/", "cube")
	  .Navigation(nav)
	  .Template("megamenu")
	  .Theme("vivid-dark")
	  .Density(active_nav == "chat" ? DENSITY_NONE : DENSITY_COMFORTABLE)
	  .TrustedContent(rendered_body);

	// User auth menu in navbar
	if(http) {
		UserSession sess = SystemAuthService::GetCurrentSession(*http);
		pb.TrustedNavActions(SystemAuthService::RenderUserMenu(sess));
	} else {
		UserSession guest;
		pb.TrustedNavActions(SystemAuthService::RenderUserMenu(guest));
	}

	// Contextual Left Tree configuration
	if(active_nav != "home" && active_nav != "chat" && active_nav != "login") {
		TreeModel tree;
		String tree_title = "Navigation";
		String tree_id = "wf-tree";

		if(active_nav.StartsWith("node") || active_nav.StartsWith("host") || active_nav.StartsWith("mesh")) {
			String active_node_id = "";
			if(active_nav.StartsWith("node_")) {
				Vector<String> parts = Split(active_nav, '_');
				if(parts.GetCount() >= 2)
					active_node_id = parts[1];
			} else if(active_nav.StartsWith("host")) {
				active_node_id = "";
			}
			tree = BuildMeshFleetTree(active_nav, active_node_id);
			tree_title = "Compute Fleet & Repositories";
			tree_id = "mesh-tree";
		}
		else if(active_nav == "projects" || active_nav.StartsWith("project")) {
			tree = BuildProjectsTree(Get().GetLocalStore(), "");
			tree_title = "Manager Task Queue";
			tree_id = "mgr-tree";
		}
		else if(active_nav == "goals" || active_nav.StartsWith("goal")) {
			auto request_catalog = Get().PinCatalog();
			tree = BuildGoalsTree(request_catalog->v2, "");
			tree_title = "Milestones & Goals";
			tree_id = "goals-tree";
		}
		else {
			tree = TreeGenerators::FromNavigation(nav);
			tree_title = "Workspace Navigation";
			tree_id = "nav-tree";
		}

		if(!tree.IsEmpty()) {
			pb.LeftTree(tree, tree_title, tree_id);
			pb.AutoOutline(true); // Automatically builds right-side TOC outline
		}
	}

	pb.TrustedFooter("Manager Skylark &bull; Structured work and local operator oversight");

	// Add manager custom component styling integrated with Vivid Plasma palette variables
	pb.TrustedExtraHead("<style>\n" + GetPageCustomStyles() + "</style>\n");
	AddMarkdownSupport(pb);

	return pb.Build();
}

String ManagerSkylarkApp::GetPageCustomStyles()
{
	String s;
	s << "  main.wf-page-body { max-width: var(--wf-container-width, 1500px); margin: 0 auto; }\n"
	  << "  .card { background: var(--wf-bg-view, #151d2b); border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); border-radius: var(--wf-radius, 8px); padding: var(--wf-pad-card, 1.25rem); margin-bottom: var(--wf-gap-grid, 1.25rem); box-shadow: var(--wf-shadow); }\n"
	  << "  .card-header { font-size: 1.15rem; font-weight: 700; margin-bottom: 0.85rem; color: var(--wf-fg-normal, #d3dae3); border-bottom: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); padding-bottom: 0.5rem; display: flex; justify-content: space-between; align-items: center; }\n"
	  << "  .card-header a { font-size: 0.85rem; }\n"
	  << "  .assessment-table-wrap { max-width: 100%; overflow-x: auto; overscroll-behavior-x: contain; }\n"
	  << "  .assessment-table-wrap:focus-visible, a:focus-visible, button:focus-visible, input:focus-visible { outline: 3px solid var(--wf-accent-focus, #00a1c5); outline-offset: 2px; }\n"
	  << "  .assessment-tier { display: inline-block; padding: .25rem .55rem; border-radius: .35rem; font-weight: 700; }\n"
	  << "  .assessment-tier { color: #fff; }\n"
	  << "  .assessment-tier.tier-a { background: #176b45; } .assessment-tier.tier-b { background: #225b91; }\n"
	  << "  .assessment-tier.tier-c { background: #805719; } .assessment-tier.tier-d { background: #8a3744; }\n"
	  << "  .assessment-tier.tier-unknown { color: var(--wf-fg-normal, #fff); background: var(--wf-surface-hover, #39465a); }\n"
	  << "  .assessment-overview-wrap, .assessment-table-wrap { max-width: 100%; overflow-x: auto; overscroll-behavior-x: contain; }\n"
	  << "  .review-view { max-width: 72rem; margin: 0 auto; } .review-view li { overflow-wrap: anywhere; } .review-view p { line-height: 1.55; }\n"
	  << "  .review-patch { max-height: 70vh; overflow: auto; white-space: pre; tab-size: 4; }\n"
	  << "  @media (max-width: 700px) { .assessment-overview-wrap, .assessment-table-wrap { width: 100%; } .assessment-overview-wrap table, .assessment-table-wrap table { min-width: 48rem; } }\n"
	  << "  .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: var(--wf-gap-grid, 1.25rem); }\n"
	  << "  .grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: var(--wf-gap-grid, 1.25rem); }\n"
	  << "  .grid-4 { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: var(--wf-gap-grid, 1.25rem); }\n"
	  << "  .stat-card { background: var(--wf-bg-view, #151d2b); border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); border-radius: var(--wf-radius, 8px); padding: var(--wf-pad-card, 1.25rem); display: flex; flex-direction: column; gap: 4px; box-shadow: var(--wf-shadow); }\n"
	  << "  .stat-val { font-size: 2rem; font-weight: 800; color: var(--wf-accent-focus, #00a1c5); }\n"
	  << "  .stat-lbl { font-size: 0.8rem; color: var(--wf-fg-inactive, #8b919d); text-transform: uppercase; letter-spacing: 0.05em; font-weight: 600; }\n"
	  << "  table { width: 100%; border-collapse: collapse; margin-top: 0.5rem; }\n"
	  << "  th { text-align: left; padding: var(--wf-cell-pad, 10px 12px); background-color: var(--wf-bg-alt, #1c202f); color: var(--wf-fg-inactive, #8b919d); font-weight: 600; font-size: 0.82rem; border-bottom: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); text-transform: uppercase; letter-spacing: 0.04em; }\n"
	  << "  td { padding: var(--wf-cell-pad, 10px 12px); border-bottom: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); font-size: 0.9rem; color: var(--wf-fg-normal, #d3dae3); }\n"
	  << "  tr:hover td { background-color: var(--wf-surface-hover, rgba(0, 161, 197, 0.08)); }\n"
	  << "  .badge-v2 { font-size: 0.75rem; background-color: var(--wf-accent-focus, #00a1c5); color: #ffffff; padding: 2px 7px; border-radius: 4px; font-weight: 600; }\n"
	  << "  .btn { display: inline-block; padding: var(--wf-btn-pad, 6px 14px); border-radius: var(--wf-radius, 8px); font-weight: 600; text-decoration: none; border: none; cursor: pointer; font-size: 0.88rem; transition: all 0.15s; }\n"
	  << "  .btn-primary { background-color: var(--wf-accent-focus, #00a1c5); color: #ffffff; }\n"
	  << "  .btn-primary:hover { background-color: var(--wf-accent-hover, #0088a6); }\n"
	  << "  .code-box { background-color: var(--wf-bg-alt, #1c202f); color: var(--wf-accent-focus, #00a1c5); padding: 1rem; border-radius: var(--wf-radius, 8px); font-family: monospace; font-size: 0.85rem; overflow-x: auto; border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); }\n"
	  << "  .wf-badge { display: inline-block; padding: 2px 8px; border-radius: 10px; font-size: 0.72rem; font-weight: 600; }\n"
	  << "  .wf-badge-emerald { background: rgba(0, 170, 127, 0.15); color: #34d399; }\n"
	  << "  .wf-badge-cyan { background: rgba(0, 161, 197, 0.15); color: #22d3ee; }\n"
	  << "  .wf-badge-crimson { background: rgba(200, 55, 113, 0.15); color: #f87171; }\n"
	  << "  .wf-badge-neutral { background: var(--wf-bg-alt, #1c202f); color: var(--wf-fg-inactive, #8b919d); }\n";
	return s;
}

PageBuilder ManagerSkylarkApp::RenderDataPageChrome(const String& title, const String& active_nav, const String& body_content, const TreeModel& tree)
{
	auto catalog = Get().PinCatalog();
	String status_notice;
	if(catalog && (!catalog->IsComplete() || !catalog->diagnostics.IsEmpty()))
		status_notice = "<div role=\"status\" class=\"card\"><strong>Catalog generation " + AsString(catalog->generation) + "</strong>: version source " + EscapeHtml(catalog->v2_status) + ", structured-data source " + EscapeHtml(catalog->data_status) + ". Source diagnostics are available through the authorized catalog API.</div>";
	PageBuilder pb;
	pb.Title(title + " - Manager Skylark")
	  .Brand("Manager Skylark", "/", "cube")
	  .Navigation(BuildNavigation(active_nav))
	  .Template("megamenu")
	  .Theme("vivid-dark")
	  .Density(DENSITY_COMFORTABLE)
	  .TrustedContent(status_notice + body_content)
	  .LeftTree(tree, "Data Navigator", "wf-data-tree")
	  .AutoOutline(true)
	  .TrustedFooter("Manager Skylark &bull; Structured work and local operator oversight");

	// Manager custom styling + generic diagram viewer chrome
	pb.TrustedExtraHead("<style>\n" + GetPageCustomStyles() + "\n" + DiagramRenderer::GetStyles() + "\n</style>\n");
	pb.TrustedExtraScripts(DiagramRenderer::GetClientScript());
	return pb;
}

void ManagerSkylarkApp::AddMarkdownSupport(PageBuilder& pb)
{
	// Content remains escaped plain text until local, vetted renderers are wired.
	pb.TrustedExtraHead("<style>\n" + WebChatView::GetMarkdownStyles() + "\n</style>\n");
}

END_UPP_NAMESPACE
