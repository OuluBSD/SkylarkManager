#include "WebStorage.h"

#ifdef PLATFORM_WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#endif

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// JsonFileStore
// ---------------------------------------------------------------------------

void JsonFileStore::FileLock::Init()
{
#ifdef PLATFORM_WIN32
	handle = NULL;
#else
	fd = -1;
#endif
	locked = false;
}

bool JsonFileStore::FileLock::Acquire(const String& lock_path, String& error)
{
	RealizePath(lock_path);
#ifdef PLATFORM_WIN32
	HANDLE h = CreateFileW((const wchar_t *)~ToSystemCharsetW(lock_path), GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
	                       FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE || h == NULL) {
		error = "cannot open lock file: " + lock_path;
		return false;
	}
	OVERLAPPED ov;
	memset(&ov, 0, sizeof(ov));
	if(!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &ov)) {
		CloseHandle(h);
		error = "cannot lock file: " + lock_path;
		return false;
	}
	handle = h;
#else
	int f = open(~lock_path, O_CREAT | O_RDWR, 0644);
	if(f < 0) {
		error = "cannot open lock file: " + lock_path + " (" + AsString(errno) + ")";
		return false;
	}
	if(flock(f, LOCK_EX) != 0) {
		close(f);
		error = "cannot lock file: " + lock_path + " (" + AsString(errno) + ")";
		return false;
	}
	fd = f;
#endif
	locked = true;
	return true;
}

void JsonFileStore::FileLock::Release()
{
	if(!locked)
		return;
#ifdef PLATFORM_WIN32
	if(handle) {
		OVERLAPPED ov;
		memset(&ov, 0, sizeof(ov));
		UnlockFileEx((HANDLE)handle, 0, 1, 0, &ov);
		CloseHandle((HANDLE)handle);
	}
	handle = NULL;
#else
	if(fd >= 0) {
		flock(fd, LOCK_UN);
		close(fd);
	}
	fd = -1;
#endif
	locked = false;
}

bool JsonFileStore::WriteAtomic(const String& final_path, const String& data, String& error)
{
#ifdef PLATFORM_WIN32
	HANDLE h = CreateFileW(ToSystemCharsetW(final_path + ".tmp"), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
	                       FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE || h == NULL) {
		error = "cannot create temp file for " + final_path;
		return false;
	}
	dword written = 0;
	if(!WriteFile(h, ~data, data.GetCount(), &written, NULL) || written != (dword)data.GetCount()) {
		CloseHandle(h);
		error = "cannot write temp file for " + final_path;
		return false;
	}
	if(!FlushFileBuffers(h)) {
		CloseHandle(h);
		error = "cannot flush temp file for " + final_path;
		return false;
	}
	CloseHandle(h);
	if(!MoveFileExW((const wchar_t *)~ToSystemCharsetW(final_path + ".tmp"), (const wchar_t *)~ToSystemCharsetW(final_path),
	                MOVEFILE_REPLACE_EXISTING)) {
		DeleteFileW((const wchar_t *)~ToSystemCharsetW(final_path + ".tmp"));
		error = "cannot replace file " + final_path;
		return false;
	}
	return true;
#else
	String tmp = final_path + ".tmp-" + AsString((int)getpid()) + "-" + AsString((int)Random(100000000));
	int f = open(~tmp, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if(f < 0) {
		error = "cannot create temp file " + tmp + " (" + AsString(errno) + ")";
		return false;
	}
	const char* p = ~data;
	size_t remaining = data.GetCount();
	while(remaining > 0) {
		ssize_t n = write(f, p, remaining);
		if(n < 0) {
			if(errno == EINTR)
				continue;
			close(f);
			DeleteFile(~tmp);
			error = "cannot write temp file " + tmp + " (" + AsString(errno) + ")";
			return false;
		}
		p += n;
		remaining -= (size_t)n;
	}
	if(fsync(f) != 0) {
		close(f);
		DeleteFile(~tmp);
		error = "cannot fsync temp file " + tmp + " (" + AsString(errno) + ")";
		return false;
	}
	close(f);
	if(rename(~tmp, ~final_path) != 0) {
		DeleteFile(~tmp);
		error = "cannot replace file " + final_path + " (" + AsString(errno) + ")";
		return false;
	}
	return true;
#endif
}

bool JsonFileStore::ValidId(const String& id)
{
	if(id.IsEmpty() || id.GetCount() > 200)
		return false;
	for(int i = 0; i < id.GetCount(); i++) {
		int c = id[i];
		bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if(!alnum && c != '.' && c != '_' && c != '-')
			return false;
	}
	return id != "." && id != "..";
}

JsonFileStore::JsonFileStore(const String& site_id, const String& data_root, const String& collection)
{
	site = site_id;
	ASSERT_(site.GetCount() && ValidId(site), "invalid site id for storage");
	ASSERT_(ValidId(collection), "invalid collection id: " + collection);
	dir = AppendFileName(AppendFileName(data_root, "data"), collection);
	RealizeDirectory(dir);
}

String JsonFileStore::RecordPath(const String& id)
{
	return AppendFileName(dir, id + ".json");
}

void JsonFileStore::SetSchema(const String& sid, const String& sver)
{
	schema_id = sid;
	schema_version = sver;
}

void JsonFileStore::SetDefaults(const String& wspace, const String& kind,
                                const String& vis, const String& prov)
{
	workspace_id = wspace;
	record_kind = kind;
	visibility = vis;
	provenance_ref = prov;
}

void JsonFileStore::Quarantine(const String& id, const String& reason)
{
	String path = RecordPath(id);
	if(FileExists(path)) {
		String bad = path + ".corrupt-" + AsString((int64)msecs());
		FileMove(path, bad);
	}
	LOG("WebStorage: quarantined record " << site << "/" << id << ": " << reason);
}

StoreResult JsonFileStore::ReadRecord(const String& id, ValueMap& envelope, ValueMap& data, String& error)
{
	String path = RecordPath(id);
	if(!FileExists(path)) {
		error = "record not found";
		return false;
	}
	String raw = LoadFile(path);
	Value v;
	try {
		v = ParseJSON(raw);
	}
	catch(Exc& e) {
		Quarantine(id, String("JSON parse error: ") + e);
		error = "record is corrupted and was quarantined: " + e;
		return false;
	}
	if(!v.Is<ValueMap>()) {
		Quarantine(id, "record is not a JSON object");
		error = "record is corrupted and was quarantined";
		return false;
	}
	ValueMap m = (ValueMap)v;
	if(m.Find("envelope") < 0 || m.Find("data") < 0 || !m["envelope"].Is<ValueMap>()) {
		Quarantine(id, "missing envelope/data");
		error = "record is corrupted (missing envelope/data)";
		return false;
	}
	envelope = (ValueMap)m["envelope"];
	if(m["data"].Is<ValueMap>())
		data = (ValueMap)m["data"];
	return true;
}

StoreResult JsonFileStore::Locked(const String& id, const String& expected_revision, const String& op,
                                  const Function<StoreResult(const ValueMap& current, String& new_revision)>& step,
                                  String& new_revision)
{
	if(!ValidId(id))
		return StoreResult("invalid record id: " + id);
	mutex.Enter();
	String lock_error;
	FileLock lock;
	if(!lock.Acquire(AppendFileName(dir, ".lock"), lock_error)) {
		mutex.Leave();
		return StoreResult("storage lock failed for '" + id + "' (" + op + "): " + lock_error);
	}
	ValueMap current_env, current_data;
	String read_error;
	bool exists = ReadRecord(id, current_env, current_data, read_error).ok;
	if(!exists && read_error != "record not found") {
		lock.Release();
		mutex.Leave();
		return StoreResult(read_error);
	}
	if(!expected_revision.IsEmpty()) {
		String cur = exists ? AsString(current_env.Get("revision")) : String();
		if(cur != expected_revision) {
			lock.Release();
			mutex.Leave();
			return StoreResult(Format("revision conflict on '%s' (%s): expected %s, current %s",
			              ~id, ~op, ~expected_revision, exists ? ~cur : "~none~"));
		}
	}
	StoreResult r = step(exists ? current_env : ValueMap(), new_revision);
	lock.Release();
	mutex.Leave();
	return r;
}

StoreResult JsonFileStore::Get(const String& id, String& json, String& revision)
{
	if(!ValidId(id))
		return "invalid record id: " + id;
	ValueMap env, data;
	String error;
	if(!ReadRecord(id, env, data, error))
		return error;
	json = AsJSON(Value(data), true);
	revision = AsString(env.Get("revision"));
	return true;
}

StoreResult JsonFileStore::Put(const String& id, const String& json,
                               const String& expected_revision, String& new_revision)
{
	Value parsed;
	try {
		parsed = ParseJSON(json);
	}
	catch(Exc& e) {
		return "record data is not valid JSON: " + e;
	}
	if(!parsed.Is<ValueMap>())
		return "record data must be a JSON object";
	ValueMap data = (ValueMap)parsed;

	String new_rev;
	StoreResult r = Locked(id, expected_revision, "put",
	                       [&](const ValueMap& current, String& out_rev) -> StoreResult {
		int next = 1;
		if(!IsNull(current.Get("revision")))
			next = ScanInt(AsString(current.Get("revision"))) + 1;
		RecordEnvelope env;
		env.schema_id = schema_id;
		env.schema_version = schema_version;
		env.workspace_id = workspace_id;
		env.stable_id = id;
		env.record_kind = record_kind;
		env.revision = AsString(next);
		env.visibility = visibility;
		env.provenance_ref = provenance_ref;

		if(schema_id.GetCount()) {
			SchemaCard card;
			if(SchemaRegistry::Find(schema_id, card)) {
				String err;
				if(!SchemaRegistry::Validate(card, data, err))
					return StoreResult("schema validation failed: " + err);
			}
		}

		ValueMap doc;
		Value env_v = ParseJSON(StoreAsJson(env));
		doc.Add("envelope", env_v);
		doc.Add("data", data);
		String error;
		if(!WriteAtomic(RecordPath(id), AsJSON(doc, true), error))
			return StoreResult("atomic write failed for '" + id + "': " + error);
		out_rev = env.revision;
		return StoreResult(true);
	}, new_rev);
	new_revision = new_rev;
	return r;
}

StoreResult JsonFileStore::Delete(const String& id, const String& expected_revision)
{
	String ignore_rev;
	return Locked(id, expected_revision, "delete", [&](const ValueMap& current, String&) -> StoreResult {
		String path = RecordPath(id);
		if(FileExists(path) && !FileDelete(path))
			return StoreResult("cannot delete record file " + path);
		return StoreResult(true);
	}, ignore_rev);
}

StoreResult JsonFileStore::List(Vector<String>& ids)
{
	ids.Clear();
	for(const FindFile& ff : FindFile(dir + "/*.json")) {
		String name = ff.GetName();
		if(name.Find(".corrupt-") < 0 && name.Find(".tmp-") < 0)
			ids.Add(name.Mid(0, name.GetCount() - 5));
	}
	Sort(ids);
	return true;
}

StoreResult JsonFileStore::PutOpaque(const String& id, const String& envelope_json,
                                     const String& data_json, const String& expected_revision,
                                     String& new_revision)
{
	Value ev, dv;
	try {
		ev = ParseJSON(envelope_json);
		dv = ParseJSON(data_json);
	}
	catch(Exc& e) {
		return "opaque record is not valid JSON: " + e;
	}
	String new_rev;
	StoreResult r = Locked(id, expected_revision, "put-opaque",
	                       [&](const ValueMap& current, String& out_rev) -> StoreResult {
		int next = 1;
		if(!IsNull(current.Get("revision")))
			next = ScanInt((String)current.Get("revision")) + 1;
		ValueMap env = ev.Is<ValueMap>() ? (ValueMap)ev : ValueMap();
		if(env.Find("revision") >= 0)
			env.Set("revision", AsString(next));
		else
			env.Add("revision", AsString(next));
		ValueMap doc;
		doc.Add("envelope", env);
		doc.Add("data", dv);
		String error;
		if(!WriteAtomic(RecordPath(id), StoreAsJson(doc, true), error))
			return StoreResult("atomic write failed for '" + id + "': " + error);
		out_rev = AsString(next);
		return StoreResult(true);
	}, new_rev);
	new_revision = new_rev;
	return r;
}

// ---------------------------------------------------------------------------
// MemoryStore
// ---------------------------------------------------------------------------

StoreResult MemoryStore::Get(const String& id, String& json, String& revision)
{
	mutex.Enter();
	int q = json.Find(id);
	if(q < 0) {
		mutex.Leave();
		return "record not found";
	}
	json = this->json[q];
	revision = AsString(rev[q]);
	mutex.Leave();
	return true;
}

StoreResult MemoryStore::Put(const String& id, const String& data,
                             const String& expected_revision, String& new_revision)
{
	Value parsed;
	try {
		parsed = ParseJSON(data);
	}
	catch(Exc& e) {
		return "record data is not valid JSON: " + e;
	}
	mutex.Enter();
	int q = json.Find(id);
	int next = q < 0 ? 1 : rev[q] + 1;
	if(!expected_revision.IsEmpty()) {
		int cur = q < 0 ? 0 : rev[q];
		if(expected_revision != AsString(cur)) {
			mutex.Leave();
			return Format("revision conflict on '%s': expected %s, current %d",
			              ~id, ~expected_revision, cur);
		}
	}
	if(q < 0) {
		json.Add(id, StoreAsJson(parsed, true));
		rev.Add(id, next);
	}
	else {
		json[q] = StoreAsJson(parsed, true);
		rev[q] = next;
	}
	new_revision = AsString(next);
	mutex.Leave();
	return true;
}

StoreResult MemoryStore::Delete(const String& id, const String& expected_revision)
{
	mutex.Enter();
	int q = json.Find(id);
	if(q >= 0) {
		if(!expected_revision.IsEmpty() && expected_revision != AsString(rev[q])) {
			mutex.Leave();
			return Format("revision conflict on '%s': expected %s, current %d",
			              ~id, ~expected_revision, rev[q]);
		}
		json.Remove(q);
		rev.Remove(q);
	}
	mutex.Leave();
	return true;
}

StoreResult MemoryStore::List(Vector<String>& ids)
{
	mutex.Enter();
	ids <<= json.GetKeys();
	Sort(ids);
	mutex.Leave();
	return true;
}

// ---------------------------------------------------------------------------
// SchemaRegistry
// ---------------------------------------------------------------------------

Vector<SchemaCard>& SchemaRegistry::Cards()
{
	static Vector<SchemaCard> x;
	return x;
}

void SchemaRegistry::Register(const SchemaCard& card)
{
	Vector<SchemaCard>& cards = Cards();
	for(int i = 0; i < cards.GetCount(); i++)
		if(cards[i].schema_id == card.schema_id) {
			cards[i] = card;
			return;
		}
	cards.Add(card);
}

bool SchemaRegistry::Find(const String& schema_id, SchemaCard& out)
{
	Vector<SchemaCard>& cards = Cards();
	for(int i = 0; i < cards.GetCount(); i++)
		if(cards[i].schema_id == schema_id) {
			out = cards[i];
			return true;
		}
	return false;
}

bool SchemaRegistry::Validate(const SchemaCard& card, const ValueMap& doc, String& error)
{
	for(const SchemaField& f : card.fields) {
		int q = doc.Find(f.name);
		bool present = q >= 0 && !doc.GetValue(q).IsNull();
		if(f.required && !present) {
			error = "missing required field '" + f.name + "'";
			return false;
		}
		if(!present)
			continue;
		Value v = doc.GetValue(q);
		if(f.type == "string") {
			if(!v.Is<String>()) {
				error = "field '" + f.name + "' must be a string";
				return false;
			}
			String sv = v;
			if(f.max_len > 0 && sv.GetCount() > f.max_len) {
				error = Format("field '%s' exceeds %d bytes", ~f.name, f.max_len);
				return false;
			}
			if(f.enum_values.GetCount() && FindIndex(f.enum_values, (String)v) < 0) {
				error = "field '" + f.name + "' has invalid enum value";
				return false;
			}
		}
		else if(f.type == "int") {
			if(!IsNumber(v) && !v.Is<int>()) {
				error = "field '" + f.name + "' must be a number";
				return false;
			}
		}
		else if(f.type == "bool") {
			if(!v.Is<bool>()) {
				error = "field '" + f.name + "' must be a bool";
				return false;
			}
		}
		else if(f.type == "double") {
			if(!IsNumber(v)) {
				error = "field '" + f.name + "' must be a number";
				return false;
			}
		}
		else if(f.type == "array") {
			if(!v.Is<ValueArray>()) {
				error = "field '" + f.name + "' must be an array";
				return false;
			}
		}
		else if(f.type == "object") {
			if(!v.Is<ValueMap>()) {
				error = "field '" + f.name + "' must be an object";
				return false;
			}
		}
	}
	return true;
}

bool SchemaRegistry::Validate(const String& schema_id, const ValueMap& doc, String& error)
{
	SchemaCard card;
	if(!Find(schema_id, card)) {
		error = "unknown schema '" + schema_id + "'";
		return false;
	}
	return Validate(card, doc, error);
}

// ---------------------------------------------------------------------------
// StoreManager
// ---------------------------------------------------------------------------

StoreManager& TheStoreManager()
{
	static StoreManager x;
	return x;
}

IRecordCollection& StoreManager::JsonCollection(const String& site, const String& data_root, const String& collection)
{
	String key = site + "\x01" + data_root + "\x01" + collection;
	mutex.Enter();
	for(JsonEntry& e : json_stores)
		if(e.key == key) {
			IRecordCollection& r = *e.store;
			mutex.Leave();
			return r;
		}
	JsonEntry& e = json_stores.Add();
	e.key = key;
	e.store.Create(site, data_root, collection);
	IRecordCollection& r = *e.store;
	mutex.Leave();
	return r;
}

MemoryStore& StoreManager::MemoryCollection(const String& site, const String& collection)
{
	String key = site + "\x01" + collection;
	mutex.Enter();
	for(MemEntry& e : mem_stores)
		if(e.key == key) {
			MemoryStore& r = *e.store;
			mutex.Leave();
			return r;
		}
	MemEntry& e = mem_stores.Add();
	e.key = key;
	e.store.Create();
	MemoryStore& r = *e.store;
	mutex.Leave();
	return r;
}

END_UPP_NAMESPACE
