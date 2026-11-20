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

// expected_revision value meaning "the record must not exist yet". Revisions
// are positive integers, so this sentinel can never collide with one.
inline const char* RecordAbsent() { return "absent"; }

struct StoreResult {
	bool   ok;
	bool   conflict = false; // true when expected_revision did not match
	String error;

	StoreResult(bool ok_ = true) : ok(ok_) {}
	StoreResult(const char* err) : ok(false), error(err) {}
	StoreResult(const String& err) : ok(false), error(err) {}
	explicit operator bool() const { return ok; }

	static StoreResult Conflict(const String& err) {
		StoreResult r(err);
		r.conflict = true;
		return r;
	}
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
	// The two halves of a blob write, with the path named by the caller: WriteFileDurable writes and flushes
	// one file; RenameFile moves it over the target. On failure the temp file is removed.
	static bool WriteFileDurable(const String& path, const String& data, String& error);
	static bool RenameFile(const String& from, const String& to, String& error);
	static bool ValidId(const String& id);

	// Multi-record transaction. Holds the in-process mutex and the inter-process
	// lock from construction until destruction, so several reads and writes
	// (possibly across records) commit or fail as one unit. Ok() is false when
	// the lock could not be taken; no other call then has effect.
	class Txn {
	public:
		explicit Txn(JsonFileStore& store);
		~Txn();

		bool          Ok() const { return error.IsEmpty(); }
		const String& Error() const { return error; }

		StoreResult Get(const String& id, String& json, String& revision);
		StoreResult Put(const String& id, const String& json,
		                const String& expected_revision, String& new_revision);
		StoreResult Delete(const String& id, const String& expected_revision);

	private:
		JsonFileStore& store;
		FileLock       lock;
		String         error;
	};

private:
	StoreResult Locked(const String& id, const String& expected_revision, const String& op,
	                   const Function<StoreResult(const ValueMap& current, String& new_revision)>& step,
	                   String& new_revision, bool take_lock = true);
	StoreResult LockedHeld(const String& id, const String& expected_revision, const String& op,
	                       const Function<StoreResult(const ValueMap& current, String& new_revision)>& step,
	                       String& new_revision);
	StoreResult PutRecord(const String& id, const ValueMap& data,
	                      const String& expected_revision, String& new_revision, bool take_lock);
	StoreResult DeleteRecord(const String& id, const String& expected_revision, bool take_lock);
	StoreResult ReadRecord(const String& id, ValueMap& envelope, ValueMap& data, String& error);
	String      RecordPath(const String& id);
	void        Quarantine(const String& id, const String& reason);

	String      site;
	String      dir;
	Mutex  mutex;
	String      schema_id, schema_version, workspace_id, record_kind, visibility, provenance_ref;
};

// ---------------------------------------------------------------------------
// BlobStore: binary payloads (images, uploads) kept beside the JSON records of
// one collection.
//
// The metadata of blob <id> is a JSON record in <collection> (JsonFileStore,
// so it has revisions and the same lock). The bytes are <collection dir>/<id>.blob,
// written atomically under the same lock. Reads recheck the byte size and the
// SHA-256 checksum recorded in the metadata, so damaged bytes are refused.
// ---------------------------------------------------------------------------
struct BlobRecord {
	String id;
	String checksum;          // lowercase hex SHA-256 of the bytes
	int64  byte_size = 0;
	int    width = 0;         // pixels; 0 when not an image
	int    height = 0;
	String content_type;      // e.g. "image/png"

	void Jsonize(JsonIO& io) {
		io("id", id)
		  ("checksum", checksum)
		  ("byte_size", byte_size)
		  ("width", width)
		  ("height", height)
		  ("content_type", content_type)
		;
	}
};

class BlobStore {
public:
	static const int64 DefaultMaxBytes = 8 * 1024 * 1024;
	static const int   MaxDimension = 65535;

	BlobStore(const String& site_id, const String& data_root, const String& collection,
	          int64 max_bytes = DefaultMaxBytes);

	// Stores bytes under id. expected_checksum, when not empty, must equal the SHA-256 of bytes
	// (a client-declared checksum); a mismatch is refused and nothing is written.
	// expected_revision has the JsonFileStore meaning (RecordAbsent(), a revision, or empty).
	StoreResult Put(const String& id, const String& bytes, const String& content_type,
	                int width, int height, const String& expected_checksum,
	                const String& expected_revision, BlobRecord& stored);

	// Reads the metadata and the bytes; refuses bytes whose size or checksum disagree with the metadata.
	StoreResult Get(const String& id, String& bytes, BlobRecord& meta);

	// Metadata only.
	StoreResult Meta(const String& id, BlobRecord& meta);

	int64        MaxBytes() const { return max_bytes; }
	static String Checksum(const String& bytes);
	static bool   ValidContentType(const String& content_type);

private:
	String BytesPath(const String& id) const;

	JsonFileStore meta_store;
	int64         max_bytes;
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
