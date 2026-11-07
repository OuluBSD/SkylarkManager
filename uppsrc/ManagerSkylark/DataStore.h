#ifndef _ManagerSkylark_DataStore_h_
#define _ManagerSkylark_DataStore_h_

#include <Core/Core.h>

NAMESPACE_UPP

class ManagerSkylarkApp;

// ValueMap lookup with default (ValueMap has no Get(key, default) member).
inline Value MapGet(const ValueMap& m, const String& key, const Value& def = Value())
{
	int i = m.Find(key);
	return i >= 0 ? m.GetValue(i) : def;
}

inline String MapGetString(const ValueMap& m, const String& key, const String& def = "")
{
	Value v = MapGet(m, key);
	if(v.IsNull() || !IsString(v))
		return def;
	return (String)v;
}

// Trusted, code-defined subset of the record schema contract.
struct SchemaCard : Moveable<SchemaCard> {
	struct Field : Moveable<Field> {
		String name;
		String type;
		bool required = false;
		Vector<String> enum_values;
		String item_type;
		int max_utf8_bytes = -1;
		int max_items = -1;
		int item_max_utf8_bytes = -1;
		int64 min_integer = INT64_MIN;
		int64 max_integer = INT64_MAX;
		bool allow_additional_properties = true;
		Vector<Field> properties;
		Vector<Field> item_properties;
		String reference_kind;
		Field() {}
		Field(const Field& o) : name(o.name), type(o.type), required(o.required), enum_values(clone(o.enum_values)), item_type(o.item_type), max_utf8_bytes(o.max_utf8_bytes), max_items(o.max_items), item_max_utf8_bytes(o.item_max_utf8_bytes), min_integer(o.min_integer), max_integer(o.max_integer), allow_additional_properties(o.allow_additional_properties), properties(clone(o.properties)), item_properties(clone(o.item_properties)), reference_kind(o.reference_kind) {}
		Field& operator=(const Field& o) {
			if(this != &o) { name = o.name; type = o.type; required = o.required; enum_values = clone(o.enum_values); item_type = o.item_type; max_utf8_bytes = o.max_utf8_bytes; max_items = o.max_items; item_max_utf8_bytes = o.item_max_utf8_bytes; min_integer = o.min_integer; max_integer = o.max_integer; allow_additional_properties = o.allow_additional_properties; properties = clone(o.properties); item_properties = clone(o.item_properties); reference_kind = o.reference_kind; }
			return *this;
		}
	};
	String         kind;
	String         schema_id;
	String         schema_version;
	String         id_pattern;
	Vector<String> required;
	Vector<Field>  fields;
	String         collection;
	String         path;

 SchemaCard() {}
 SchemaCard(const SchemaCard& o) : kind(o.kind), schema_id(o.schema_id), schema_version(o.schema_version), id_pattern(o.id_pattern), required(clone(o.required)), fields(clone(o.fields)), collection(o.collection), path(o.path) {}
	SchemaCard& operator=(const SchemaCard& o) {
		if(this != &o) {
   kind = o.kind;
   schema_id = o.schema_id; schema_version = o.schema_version;
   id_pattern = o.id_pattern;
   required = clone(o.required);
   fields = clone(o.fields);
			collection = o.collection;
			path = o.path;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
  io("kind", kind)("schema_id", schema_id)("schema_version", schema_version)("id_pattern", id_pattern)("required", required);
	}
};

// Generic data document: every *.json file with a "kind" field anywhere under
// the scan root becomes one document. The full parsed payload is retained so
// generic views can render any schema without per-schema C++ declarations.
struct DataDocument : Moveable<DataDocument> {
	String   source_id;       // stable alias for the explicitly configured scan root
	bool     read_only = true;
	String   id;
	String   qualified_id;
	String   workspace_id;
 String   kind;
 String   schema_id;
 String   schema_version;
  String   revision;
  String   source_revision;
 String   visibility;
 String   provenance_ref;
	String   title;
	String   status;
	String   description;
	String   collection;    // top-level directory under the scan root
	String   scope;         // domain directory (second path level)
	String   parent;        // immediate parent directory name
	String   path;          // absolute file path
 String   rel_path;      // scan-root relative path
 ValueMap data;          // full parsed document
 String   raw_json;       // retained only when parser fidelity is lossy/uncertain
 bool     valid;         // required fields (per schema card) present
 bool     validated;
 String   validation_state;
 Vector<String> diagnostics;
	String   invalid_reason;

 DataDocument() : valid(false), validated(false), validation_state("nonvalidated") {}
	 DataDocument(const DataDocument& o) : source_id(o.source_id), read_only(o.read_only), id(o.id), qualified_id(o.qualified_id), workspace_id(o.workspace_id), kind(o.kind), schema_id(o.schema_id), schema_version(o.schema_version), revision(o.revision), source_revision(o.source_revision), visibility(o.visibility), provenance_ref(o.provenance_ref), title(o.title), status(o.status), description(o.description), collection(o.collection), scope(o.scope), parent(o.parent), path(o.path), rel_path(o.rel_path), data(o.data), raw_json(o.raw_json), valid(o.valid), validated(o.validated), validation_state(o.validation_state), diagnostics(clone(o.diagnostics)), invalid_reason(o.invalid_reason) {}
	DataDocument& operator=(const DataDocument& o) {
		if(this != &o) {
   source_id = o.source_id; read_only = o.read_only; id = o.id; qualified_id = o.qualified_id; workspace_id = o.workspace_id; kind = o.kind; schema_id = o.schema_id; schema_version = o.schema_version; revision = o.revision; source_revision = o.source_revision; visibility = o.visibility; provenance_ref = o.provenance_ref; title = o.title; status = o.status; description = o.description;
			collection = o.collection; scope = o.scope; parent = o.parent; path = o.path; rel_path = o.rel_path;
   data = o.data; raw_json = o.raw_json; valid = o.valid; validated = o.validated; validation_state = o.validation_state; diagnostics = clone(o.diagnostics); invalid_reason = o.invalid_reason;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
	  io("source_id", source_id)("read_only", read_only)("id", id)("qualified_id", qualified_id)("kind", kind)
	  ("workspace_id", workspace_id)("schema_id", schema_id)("schema_version", schema_version)("revision", revision)("source_revision", source_revision)("visibility", visibility)("provenance_ref", provenance_ref)
		  ("title", title)("status", status)("description", description)("collection", collection)("scope", scope)("rel_path", rel_path)
		  ("valid", valid)("validated", validated)("validation_state", validation_state)("diagnostics", diagnostics);
		if(!invalid_reason.IsEmpty())
			io("invalid_reason", invalid_reason);
	}
};

struct DataSourceRegistration : Moveable<DataSourceRegistration> {
	String id;
	String root;
	bool enabled = true;
	bool read_only = true;
	bool write_enabled = false;
};

struct DataWriteAuthorization : Moveable<DataWriteAuthorization> {
	DataWriteAuthorization() {}
	bool IsAllowed() const { return authenticated && action_scope == "record:write" && !principal.IsEmpty() && !source_id.IsEmpty() && !workspace_id.IsEmpty(); }
	bool AllowsWorkspace(const String& workspace) const { return IsAllowed() && !workspace.IsEmpty() && workspace_id == workspace; }
	bool AllowsSource(const String& workspace, const String& source) const { return AllowsWorkspace(workspace) && !source.IsEmpty() && source_id == source; }
	String GetPrincipal() const { return principal; }
	private:
	bool authenticated = false;
	String action_scope;
	String principal;
	String source_id;
	String workspace_id;
	friend class DataStore;
	friend class SystemAuthService;
	friend class ManagerSkylarkApp;
};

struct DataWriteResult : Moveable<DataWriteResult> {
	String status;
	String revision;
	String source_revision;
};

struct JournalRecord : Moveable<JournalRecord> {
	String action;
	String source_id;
	String record_id;
	String rel_path;
	String expected_revision;
	String new_revision;
	String backup_ref;
	String payload_ref;
	String old_hash;
	String new_hash;
	String state;
};

struct JournalManifest : Moveable<JournalManifest> {
	int version = 0;
	String operation_id;
	String source_id;
	String workspace_id;
	String state;
	String request_digest;
	Vector<JournalRecord> records;
};

struct JournalStageRecord : Moveable<JournalStageRecord> {
	String action = "update";
	String qualified_id;
	String rel_path;
	String expected_revision;
	String new_revision;
	String old_payload;
	String new_payload;
};

struct JournalStageResult : Moveable<JournalStageResult> {
	String status;
	String operation_id;
};

enum DataBatchAction {
	DATA_BATCH_UPDATE,
	DATA_BATCH_CREATE
};

// Update is the default; creates must be explicitly selected and carry no expected revisions.
struct DataBatchChange : Moveable<DataBatchChange> {
	DataBatchAction action = DATA_BATCH_UPDATE;
	String qualified_id;
	String expected_revision;
	String expected_source_revision;
	ValueMap proposed;
};

struct DataBatchResult : Moveable<DataBatchResult> {
	String status;
	String operation_id;
	Vector<String> changed_ids;
	Vector<String> revisions;
};

enum DataWriteTestFailpoint {
	DATA_WRITE_FAIL_NONE,
	DATA_WRITE_FAIL_TEMP_CREATE,
	DATA_WRITE_FAIL_TEMP_WRITE,
	DATA_WRITE_FAIL_REPLACE,
	DATA_WRITE_FAIL_JOURNAL_PAYLOAD,
	DATA_WRITE_FAIL_JOURNAL_MANIFEST,
	DATA_WRITE_FAIL_JOURNAL_ROOT_SYNC,
	DATA_WRITE_FAIL_JOURNAL_OPERATION_SYNC,
	DATA_WRITE_FAIL_JOURNAL_PRE_MANIFEST_SYNC,
	DATA_WRITE_FAIL_JOURNAL_CLEANUP,
	DATA_WRITE_FAIL_JOURNAL_DIRECTORY_CLOSE,
	DATA_WRITE_FAIL_RECOVERY_BEFORE_RECORD,
	DATA_WRITE_FAIL_RECOVERY_AFTER_RECORD,
	DATA_WRITE_FAIL_RECOVERY_BEFORE_CLEANUP,
	DATA_WRITE_FAIL_BATCH_BEFORE_REPLACE,
	DATA_WRITE_FAIL_BATCH_AFTER_REPLACE,
	DATA_WRITE_FAIL_BATCH_COMMIT_MANIFEST,
	DATA_WRITE_FAIL_BATCH_TEST_CHANGE_REFERENCE,
	DATA_WRITE_FAIL_BATCH_TEST_DELETE_REFERENCE,
	DATA_WRITE_FAIL_BATCH_TEST_CHANGE_RECORD,
	DATA_WRITE_FAIL_BATCH_TEST_REGISTRATION_CHANGE,
	DATA_WRITE_FAIL_BATCH_TEST_REVOKE_BEFORE_RECEIPT,
	DATA_WRITE_FAIL_BATCH_TEST_REDIRECT_BEFORE_RECEIPT
};

struct JournalRecoveryResult : Moveable<JournalRecoveryResult> {
	String status;
	int recovered_records = 0;
	int purged_orphans = 0;
};

// Generic recursive JSON data store.
//
// Discovery model (no per-schema declaration required):
//   - A scan root is read only from the private root registration;
//     there is no caller-selected or implicit home/workspace fallback.
//   - Any file that parses to a JSON object carrying a "kind" field is indexed
//     as a DataDocument; directories named on the skip list and hidden dirs
//     are pruned.
 //   - Schema definitions are trusted code data, never source documents.
//   - *.puml files are recorded as diagram sources (rendered by the diagram
//     viewer pages).
class DataStore {
public:
	static const int MAX_DEPTH = 5;
	static const int MAX_FILES = 1000;
	static const int64 MAX_SOURCE_FILE_BYTES = 8LL * 1024 * 1024;
	static const int64 MAX_TOTAL_SOURCE_BYTES = 512LL * 1024 * 1024;

	DataStore();
	DataStore(const DataStore& o);
	DataStore& operator=(const DataStore& o);

	// Loads only the explicitly configured source registration; there is no
	// caller-selected root parameter in the production API.
	bool Load();
	void Clear();

	// Reads and validates the configured source root. Returns "" when absent or invalid.
	static String FindDefaultRoot();
	static String ReadRootConfig();
	static bool   ReadConfiguredRegistration(DataSourceRegistration& registration);
	static bool   RegistrationMatches(const DataSourceRegistration& registration, const String& source_id, const String& normalized_root);
	static String ContentDigest(const String& content);
	static String RecordSourceRevision(const String& rel_path, const String& content);
	static bool ParseJournalManifest(const String& bytes, JournalManifest& manifest);
	static bool   ExceedsSourceBudget(int64 file_bytes, int64 bytes_scanned);
	static String ParseRootConfigContent(const String& content);
	static bool   ParseRootRegistrationContent(const String& content, DataSourceRegistration& registration);
	static bool   IsSafeRoot(const String& path);
	// POSIX source lock used by writers (exclusive) and coherent catalog readers (shared).
	// create_if_missing is false for read-only registrations; absent locks then succeed without side effects.
	// Unsupported platforms allow no-create reads only; write-enabled callers fail closed.
	static bool   AcquireSourceLock(const String& root, bool shared, bool create_if_missing, int& lock_fd);
	static void   ReleaseSourceLock(int& lock_fd);
	DataWriteResult WriteRecord(const DataWriteAuthorization& authorization,
	                                   const String& qualified_id,
	                                   const String& expected_record_revision,
	                                   const String& expected_source_revision,
	                                   const String& idempotency_key,
	                                   const ValueMap& proposed,
	                                   DataWriteTestFailpoint test_failpoint = DATA_WRITE_FAIL_NONE);
	JournalStageResult PrepareJournal(const DataWriteAuthorization& authorization,
	                                  const String& operation_id,
	                                  const Vector<JournalStageRecord>& records,
	                                  DataWriteTestFailpoint test_failpoint = DATA_WRITE_FAIL_NONE);
	JournalRecoveryResult RecoverPendingBatches(const DataWriteAuthorization& authorization,
	                                             DataWriteTestFailpoint test_failpoint = DATA_WRITE_FAIL_NONE) const;
	DataBatchResult PublishBatch(const DataWriteAuthorization& authorization,
	                             const String& operation_id,
	                             const Vector<DataBatchChange>& changes,
	                             DataWriteTestFailpoint test_failpoint = DATA_WRITE_FAIL_NONE) const;

	const Vector<SchemaCard>&   GetSchemaCards() const     { return schemas; }
	const Vector<DataDocument>& GetDocuments() const       { return docs; }
	const Vector<String>&       GetDiagramSources() const  { return diagram_sources; }

	Vector<String> GetKinds() const;
	Vector<String> GetCollections() const;
	int            GetKindCount(const String& kind) const;
	int            GetInvalidCount() const;
	int            GetQuarantineCount() const;
	const Vector<String>& GetSourceDiagnostics() const { return source_diagnostics; }
	const Vector<String>& GetSourceFingerprints() const { return source_fingerprints; }
	int            GetScannedCount() const { return files_scanned; }
	int            GetSourceErrorCount() const { return source_errors; }
	bool           IsLoadIncomplete() const { return source_incomplete; }
	String         GetLoadStatus() const { return source_incomplete ? "load_incomplete" : source_errors ? "loaded_with_errors" : "loaded"; }
	String         GetSourceId() const { return source_id; }
	bool           IsReadOnly() const { return source_read_only; }

	Vector<const DataDocument*> GetDocumentsForKind(const String& kind) const;
	Vector<const DataDocument*> GetDocumentsForCollection(const String& collection) const;
	Vector<const DataDocument*> GetDocumentsForScope(const String& collection, const String& scope) const;
	Vector<const DataDocument*> GetInvalidDocuments() const;

	const DataDocument* FindDocument(const String& id) const;          // qualified id or unique unqualified id
	const DataDocument* FindDocumentByRelPath(const String& rel) const;
	const SchemaCard*   FindSchemaCard(const String& kind) const;

	String GetRootDir() const { return base_dir; }

	// Built-in fallback kind registry (kind -> required fields).
	static const Vector<SchemaCard>& GetDefaultSchemaCards();
	// Supports only ^[class][class]*$ or ^[class][class]+$ with ASCII literals/ranges.
	static bool MatchIdPattern(const String& id, const String& pattern, String& diagnostic);

private:
	friend class ManagerSkylarkApp;
	bool LoadFixtureForTests(const String& fixture_root);
	bool LoadWriteFixtureForTests(const String& fixture_root, const String& synthetic_source_id);
	bool LoadRoot(const String& root_dir, const String& registered_source_id, bool writable = false, bool test_source = false);
	bool HasCurrentWriteRegistration() const;
	void SetSyntheticRegistrationForTests(const DataSourceRegistration& registration) const;
	JournalStageResult PrepareJournalLocked(const DataWriteAuthorization& authorization,
	                                       const String& operation_id,
	                                       const Vector<JournalStageRecord>& records,
	                                       int root_fd,
	                                       DataWriteTestFailpoint test_failpoint) const;
	String             base_dir;
	String             source_id;
	bool               source_read_only;
	bool               source_write_enabled = false;
	mutable bool               synthetic_write_source = false;
	mutable DataSourceRegistration synthetic_registration;
	Vector<SchemaCard> schemas;
	Vector<DataDocument> docs;
	Vector<String>     diagram_sources;
	Vector<String>     source_diagnostics;
	Vector<String>     source_fingerprints;
	Index<String>      kind_order;
	int                files_scanned;
	int                entries_scanned;
	int64              source_bytes_scanned;
	int                source_errors;
	bool               source_incomplete = false;
	mutable int        invalid_count;
	int                quarantine_count;

	void   ScanDir(const String& dir, const String& collection, const String& rel_prefix, int depth);
	void   ValidateDocument(DataDocument& document, const SchemaCard* schema) const;
	void   IndexFile(const String& path, const String& rel_path, const String& collection);
	void   IndexDiagramSource(const String& path, const String& rel_path);
	static bool IsSkipDir(const String& name);
};

END_UPP_NAMESPACE

#endif
