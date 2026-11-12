#ifndef _WebStorage_WebStorage_h_
#define _WebStorage_WebStorage_h_

#include <Core/Core.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// WebStorage: generic, Core-only record persistence for fenced websites.
//
// A store instance is always rooted at ONE site's data root; there is no
// global store. Cross-site access goes through WebBroker service contracts,
// never through shared store objects.
// ---------------------------------------------------------------------------

struct StoreResult {
	bool   ok;
	String error;

	StoreResult(bool ok_ = true) : ok(ok_) {}
	StoreResult(const char* err) : ok(false), error(err) {}
	StoreResult(const String& err) : ok(false), error(err) {}
	explicit operator bool() const { return ok; }
};

// Envelope stored beside record data. Mirrors the Science corpus envelopes
// (schema_id, schema_version, workspace_id, stable_id, record_kind, revision,
// visibility, provenance_ref) so repository-backed JSON and engine stores can
// be reconciled later without re-inventing identity.
struct RecordEnvelope {
	String schema_id;
	String schema_version;
	String workspace_id;
	String stable_id;
	String record_kind;
	String revision;
	String visibility;
	String provenance_ref;

	void Jsonize(JsonIO& io) {
		io("schema_id", schema_id)
		  ("schema_version", schema_version)
		  ("workspace_id", workspace_id)
		  ("stable_id", stable_id)
		  ("record_kind", record_kind)
		  ("revision", revision)
		  ("visibility", visibility)
		  ("provenance_ref", provenance_ref)
		;
	}
};

class IRecordCollection {
public:
	virtual ~IRecordCollection() {}

	virtual StoreResult Get(const String& id, String& json, String& revision) = 0;
	virtual StoreResult Put(const String& id, const String& json,
	                        const String& expected_revision, String& new_revision) = 0;
	virtual StoreResult Delete(const String& id, const String& expected_revision) = 0;
	virtual StoreResult List(Vector<String>& ids) = 0;
	virtual String      Description() const = 0;
};

// ---------------------------------------------------------------------------
// JsonFileStore: one JSON file per record, under <root>/<collection>/.
//
// File layout: {"envelope": {...}, "data": {...}}. Writes are serialized by
// an in-process mutex plus an inter-process file lock; the temp file is
// fsync'd and atomically renamed. Corrupt records are quarantined.
// ---------------------------------------------------------------------------
class JsonFileStore : public IRecordCollection {
public:
	JsonFileStore(const String& site_id, const String& data_root, const String& collection);

	void SetSchema(const String& schema_id, const String& schema_version);
	void SetDefaults(const String& workspace_id, const String& record_kind,
	                 const String& visibility, const String& provenance_ref);

	StoreResult Get(const String& id, String& json, String& revision) override;
	StoreResult Put(const String& id, const String& json,
	                const String& expected_revision, String& new_revision) override;
	StoreResult Delete(const String& id, const String& expected_revision) override;
	StoreResult List(Vector<String>& ids) override;
	String      Description() const override { return "json-files:" + dir; }

	// Store raw envelope+data without parsing data as an object (used for
	// repository-reconciled records whose data is opaque).
	StoreResult PutOpaque(const String& id, const String& envelope_json,
	                      const String& data_json, const String& expected_revision,
	                      String& new_revision);

	const String& CollectionDir() const { return dir; }

	// Platform-exclusive lock, RAII (mirrors the proven CompanyStore pattern).
	class FileLock {
	public:
		FileLock() { Init(); }
		~FileLock() { Release(); }
		bool Acquire(const String& lock_path, String& error);
		void Release();
	private:
		void Init();
		#ifdef PLATFORM_WIN32
		void *handle;
		#else
		int   fd;
		#endif
		bool  locked;
	};

	static bool WriteAtomic(const String& final_path, const String& data, String& error);
	static bool ValidId(const String& id);

private:
	StoreResult Locked(const String& id, const String& expected_revision, const String& op,
	                   const Function<StoreResult(const ValueMap& current, String& new_revision)>& step,
	                   String& new_revision);
	StoreResult ReadRecord(const String& id, ValueMap& envelope, ValueMap& data, String& error);
	String      RecordPath(const String& id);
	void        Quarantine(const String& id, const String& reason);

	String      site;
	String      dir;
	Mutex  mutex;
	String      schema_id, schema_version, workspace_id, record_kind, visibility, provenance_ref;
};

// Simple in-memory store for tests and ephemeral state.
class MemoryStore : public IRecordCollection {
public:
	StoreResult Get(const String& id, String& json, String& revision) override;
	StoreResult Put(const String& id, const String& json,
	                const String& expected_revision, String& new_revision) override;
	StoreResult Delete(const String& id, const String& expected_revision) override;
	StoreResult List(Vector<String>& ids) override;
	String      Description() const override { return "memory"; }

private:
	Mutex                            mutex;
	VectorMap<String, String> json;
	VectorMap<String, int>   rev;
};

// ---------------------------------------------------------------------------
// SchemaRegistry: optional per-schema validation of record documents.
// ---------------------------------------------------------------------------
struct WebSchemaField : Moveable<WebSchemaField> {
	String        name;
	String        type; // "string","int","bool","double","array","object","any"
	bool          required = false;
	int           max_len = 0; // 0 = unlimited
	Vector<String> enum_values;

	WebSchemaField() {}
	WebSchemaField(const WebSchemaField& o)
		: name(o.name), type(o.type), required(o.required), max_len(o.max_len)
		, enum_values(clone(o.enum_values)) {}
	WebSchemaField& operator=(const WebSchemaField& o) {
		if(this != &o) {
			name = o.name;
			type = o.type;
			required = o.required;
			max_len = o.max_len;
			enum_values = clone(o.enum_values);
		}
		return *this;
	}
};

struct WebSchemaCard : Moveable<WebSchemaCard> {
	String schema_id;
	String version;
	Vector<WebSchemaField> fields;

	WebSchemaCard() {}
	WebSchemaCard(const WebSchemaCard& o)
		: schema_id(o.schema_id), version(o.version), fields(clone(o.fields)) {}
	WebSchemaCard& operator=(const WebSchemaCard& o) {
		if(this != &o) {
			schema_id = o.schema_id;
			version = o.version;
			fields = clone(o.fields);
		}
		return *this;
	}
};

class SchemaRegistry {
public:
	static void  Register(const WebSchemaCard& card);
	static bool  Find(const String& schema_id, WebSchemaCard& out);
	static bool  Validate(const WebSchemaCard& card, const ValueMap& doc, String& error);
	static bool  Validate(const String& schema_id, const ValueMap& doc, String& error);

private:
	static Vector<WebSchemaCard>& Cards();
};

// Resolves (site, collection) to a store instance; instances are per-site and
// never shared between sites.
class StoreManager {
public:
	IRecordCollection& JsonCollection(const String& site, const String& data_root, const String& collection);
	MemoryStore&       MemoryCollection(const String& site, const String& collection);

private:
	struct JsonEntry : Moveable<JsonEntry> { String key; One<JsonFileStore> store; };
	struct MemEntry : Moveable<MemEntry>  { String key; One<MemoryStore>  store; };
	Mutex          mutex;
	Vector<JsonEntry> json_stores;
	Vector<MemEntry>  mem_stores;
};

StoreManager& TheStoreManager();

END_UPP_NAMESPACE

#endif
