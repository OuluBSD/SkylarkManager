#include "DataStoreInternal.h"
#include <stdio.h>
#include <errno.h>
#ifdef PLATFORM_POSIX
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>
#endif

NAMESPACE_UPP

namespace {

String& RootRegistrationPath()
{
	static String path;
	return path;
}

}

static bool HasSymlinkAncestor(const String& path)
{
	String current = NormalizePath(path);
	while(!current.IsEmpty()) {
		FindFile ff(current);
		if(ff && ff.IsSymLink())
			return true;
		String parent = GetFileFolder(current);
		if(parent.IsEmpty() || parent == current)
			break;
		current = parent;
	}
	return false;
}

bool DataStore::IsSafeRoot(const String& path)
{
	String normalized = NormalizePath(path);
	return !normalized.IsEmpty() && IsFullPath(normalized.ToStd().c_str()) &&
	       DirectoryExists(normalized) && !HasSymlinkAncestor(normalized);
}

bool IsSafeSourceId(const String& id)
{
	if(id.IsEmpty() || id.GetCount() > 64)
		return false;
	for(int i = 0; i < id.GetCount(); i++) {
		char c = id[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		     (c >= '0' && c <= '9') || c == '-' || c == '_'))
			return false;
	}
	return true;
}

bool GetTypedField(const ValueMap& map, const String& key, Value& value)
{
	int at = map.Find(key);
	if(at < 0)
		return false;
	value = map.GetValue(at);
	return true;
}

static bool ReadRootRegistration(DataSourceRegistration& registration)
{
	String cfg = RootRegistrationPath();
	if(cfg.IsEmpty() || !FileExists(cfg))
		return false;
	int64 size = GetFileLength(cfg);
	if(size < 0 || size > 64 * 1024)
		return false;
	return DataStore::ParseRootRegistrationContent(LoadFile(cfg), registration);
}

void DataStore::SetRootRegistrationPath(const String& path)
{
	RootRegistrationPath() = path.IsEmpty() ? String() : NormalizePath(path);
}

bool DataStore::ReadConfiguredRegistration(DataSourceRegistration& registration)
{
	return ReadRootRegistration(registration);
}

bool DataStore::RegistrationMatches(const DataSourceRegistration& registration, const String& registered_source_id, const String& normalized_root)
{
	return registration.enabled && registration.write_enabled && !registration.read_only &&
	       IsSafeSourceId(registration.id) && registration.id == registered_source_id &&
	       NormalizePath(registration.root) == normalized_root;
}

bool DataStore::HasCurrentWriteRegistration() const
{
	if(synthetic_write_source)
		return RegistrationMatches(synthetic_registration, source_id, base_dir);
	DataSourceRegistration registration;
	return ReadConfiguredRegistration(registration) && RegistrationMatches(registration, source_id, base_dir);
}

void DataStore::SetSyntheticRegistrationForTests(const DataSourceRegistration& registration) const
{
	synthetic_write_source = true;
	synthetic_registration = registration;
}

String DataStore::ContentDigest(const String& content)
{
	byte digest[32];
	SHA256(digest, content);
	static const char hex[] = "0123456789abcdef";
	String result;
	for(int i = 0; i < 32; i++) {
		result.Cat(hex[digest[i] >> 4]);
		result.Cat(hex[digest[i] & 15]);
	}
	return result;
}

String DataStore::RecordSourceRevision(const String& rel_path, const String& content)
{
	return rel_path + ":" + ContentDigest(content);
}

bool DataStore::ExceedsSourceBudget(int64 file_bytes, int64 bytes_scanned)
{
	return file_bytes <= 0 || file_bytes > MAX_SOURCE_FILE_BYTES ||
	       bytes_scanned > MAX_TOTAL_SOURCE_BYTES - file_bytes;
}

static String SourceFingerprint(const String& rel_path, const String& bytes)
{
	return DataStore::RecordSourceRevision(rel_path, bytes);
}

DataStore::DataStore()
	: source_read_only(true)
	, files_scanned(0)
	, entries_scanned(0)
	, source_bytes_scanned(0)
	, source_errors(0)
	, invalid_count(0)
	, quarantine_count(0)
{
}

DataStore::DataStore(const DataStore& o) { *this = o; }

DataStore& DataStore::operator=(const DataStore& o)
{
	if(this == &o) return *this;
	base_dir = o.base_dir; source_id = o.source_id; source_read_only = o.source_read_only;
	source_write_enabled = o.source_write_enabled; synthetic_write_source = o.synthetic_write_source;
	synthetic_registration = o.synthetic_registration;
	schemas = clone(o.schemas); docs = clone(o.docs); diagram_sources = clone(o.diagram_sources);
	source_diagnostics = clone(o.source_diagnostics); source_fingerprints = clone(o.source_fingerprints); kind_order.Clear();
	for(int i = 0; i < o.kind_order.GetCount(); i++) kind_order.FindAdd(o.kind_order[i]);
	files_scanned = o.files_scanned; entries_scanned = o.entries_scanned; source_bytes_scanned = o.source_bytes_scanned;
	source_errors = o.source_errors; source_incomplete = o.source_incomplete; invalid_count = o.invalid_count; quarantine_count = o.quarantine_count;
	return *this;
}

void DataStore::Clear()
{
	base_dir.Clear();
	source_id.Clear();
	source_read_only = true;
	source_write_enabled = false;
	synthetic_write_source = false;
	synthetic_registration = DataSourceRegistration();
	schemas.Clear();
	docs.Clear();
	diagram_sources.Clear();
	kind_order.Clear();
	files_scanned = 0;
	entries_scanned = 0;
	source_bytes_scanned = 0;
	source_errors = 0;
	source_incomplete = false;
	invalid_count = 0;
	quarantine_count = 0;
	source_diagnostics.Clear();
	source_fingerprints.Clear();
}

String DataStore::ReadRootConfig()
{
	DataSourceRegistration registration;
	return ReadRootRegistration(registration) ? registration.root : String();
}

String DataStore::ParseRootConfigContent(const String& content)
{
	DataSourceRegistration registration;
	return ParseRootRegistrationContent(content, registration) ? registration.root : String();
}

bool DataStore::ParseRootRegistrationContent(const String& content, DataSourceRegistration& registration)
{
	registration = DataSourceRegistration();
	if(content.IsEmpty())
		return false;
	String trimmed = TrimBoth(content);
	if(trimmed.IsEmpty())
		return false;
	String path;
	if(trimmed[0] == '{' || trimmed[0] == '"') {
		Value v;
		try {
			v = ParseJSON(trimmed);
		}
		catch(Exc) {
			return false;
		}
		if(v.IsError())
			return false;
		if(v.Is<ValueMap>()) {
			ValueMap map = v;
			Value sources;
			if(GetTypedField(map, "sources", sources)) {
				if(!sources.Is<ValueArray>() || sources.GetCount() == 0)
					return false;
				Index<String> ids;
				int enabled_count = 0;
				DataSourceRegistration selected;
				ValueArray array = sources;
				for(int i = 0; i < array.GetCount(); i++) {
					if(!array[i].Is<ValueMap>())
						return false;
					ValueMap entry = array[i];
					Value id_value, root_value, enabled_value, read_only_value;
					if(!GetTypedField(entry, "id", id_value) || !IsString(id_value) ||
					   !GetTypedField(entry, "root", root_value) || !IsString(root_value) ||
					   !GetTypedField(entry, "enabled", enabled_value) || !enabled_value.Is<bool>() ||
					   !GetTypedField(entry, "read_only", read_only_value) || !read_only_value.Is<bool>())
						return false;
					String id = (String)id_value;
					if(!IsSafeSourceId(id) || ids.Find(ToLower(id)) >= 0)
						return false;
					ids.Add(ToLower(id));
					String entry_root = TrimBoth((String)root_value);
					Value write_value;
					bool write_enabled = false;
					if(GetTypedField(entry, "write_enabled", write_value)) {
						if(!write_value.Is<bool>()) return false;
						write_enabled = (bool)write_value;
					}
					if(!IsSafeRoot(entry_root) || (!(bool)read_only_value && !write_enabled))
						return false;
					if((bool)enabled_value) {
						selected.id = id;
						selected.root = NormalizePath(entry_root);
						selected.enabled = true;
						selected.read_only = (bool)read_only_value;
						selected.write_enabled = write_enabled;
						enabled_count++;
					}
				}
				if(enabled_count != 1)
					return false;
				registration = selected;
				return true;
			}
			Value root;
			if(!GetTypedField(map, "root", root) || !IsString(root))
				return false;
			path = TrimBoth((String)root);
			registration.id = "configured-root";
		}
		else if(IsString(v)) {
			path = TrimBoth((String)v);
			registration.id = "configured-root";
		}
		else
			return false;
	}
	else {
		// Accept the existing plain-text config form only when it is absolute.
		path = trimmed;
		registration.id = "configured-root";
	}
	if(!IsSafeRoot(path))
		return false;
	registration.root = NormalizePath(path);
	registration.enabled = true;
	registration.read_only = true;
	return true;
}

String DataStore::FindDefaultRoot()
{
	DataSourceRegistration registration;
	return ReadRootRegistration(registration) ? registration.root : String();
}

bool DataStore::Load()
{
	DataSourceRegistration registration;
	if(!ReadConfiguredRegistration(registration)) {
		Clear();
		return false;
	}
	return LoadRoot(registration.root, registration.id, registration.write_enabled && !registration.read_only);
}

bool DataStore::LoadFixtureForTests(const String& fixture_root)
{
	String temp_root = NormalizePath(GetTempDirectory());
	String root = NormalizePath(fixture_root);
	if(!root.StartsWith(temp_root + DIR_SEP) || !IsSafeRoot(root))
		return false;
	return LoadRoot(root, "self-test-fixture");
}

bool DataStore::LoadWriteFixtureForTests(const String& fixture_root, const String& synthetic_source_id)
{
	String temp_root = NormalizePath(GetTempDirectory());
	String root = NormalizePath(fixture_root);
	if(!root.StartsWith(temp_root + DIR_SEP) || !IsSafeRoot(root) || !IsSafeSourceId(synthetic_source_id))
		return false;
	return LoadRoot(root, synthetic_source_id, true, true);
}

bool DataStore::LoadRoot(const String& root_dir, const String& registered_source_id, bool writable, bool test_source)
{
	Clear();
	if(!IsSafeRoot(root_dir))
		return false;
	base_dir = NormalizePath(root_dir);
	source_id = registered_source_id;
	source_read_only = !writable;
	source_write_enabled = writable;
	synthetic_write_source = test_source;
	synthetic_registration = DataSourceRegistration();
	if(test_source) {
		synthetic_registration.id = registered_source_id;
		synthetic_registration.root = base_dir;
		synthetic_registration.enabled = true;
		synthetic_registration.read_only = !writable;
		synthetic_registration.write_enabled = writable;
	}
	// Trusted schemas are available to explicit recovery even when catalog
	// loading stops at a pending journal; this does not expose indexed records.
	const Vector<SchemaCard>& defaults = GetDefaultSchemaCards();
	for(int i = 0; i < defaults.GetCount(); i++)
		schemas.Add(defaults[i]);
	// Until an authorized recovery path resolves the journal, never expose a
	// catalog from a source containing journal state. Detection is conservative.
	String journal_dir = AppendFileName(base_dir, ".mgr-journal");
	FindFile journal_status(journal_dir);
	FindFile journal_entry(AppendFileName(journal_dir, "*"));
	if((journal_status && journal_status.IsSymLink()) || DirectoryExists(journal_dir) || journal_entry) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add("journal:recovery_required");
		return false;
	}

	// Scan top-level entries; each top-level directory is a collection.
	FindFile ff(AppendFileName(base_dir, "*"));
	while(ff) {
		if(entries_scanned >= MAX_FILES) {
			source_incomplete = true;
			source_errors++;
			source_diagnostics.Add("root:entry_limit");
			break;
		}
		entries_scanned++;
		String name = ff.GetName();
		if(name == "." || name == "..") {
			ff.Next();
			continue;
		}
		if(ff.IsSymLink()) {
			ff.Next();
			continue;
		}
		if(ff.IsFolder()) {
			if(!name.StartsWith(".") && !IsSkipDir(name))
				ScanDir(ff.GetPath(), name, name, 1);
		}
		else {
			String ext = ToLower(GetFileExt(name));
			if(ff.IsFile() && ext == ".json" && files_scanned < MAX_FILES)
				IndexFile(ff.GetPath(), name, "root");
			else if(ff.IsFile() && ext == ".puml" && diagram_sources.GetCount() < MAX_FILES)
				IndexDiagramSource(ff.GetPath(), name);
			else if(ff.IsFile() && ((ext == ".json" && files_scanned >= MAX_FILES) || (ext == ".puml" && diagram_sources.GetCount() >= MAX_FILES))) {
				source_incomplete = true;
				source_errors++;
				source_diagnostics.Add(name + ":entry_limit");
			}
		}
		ff.Next();
	}
	// Mark every member of an identity collision; no scan-order winner exists.
	for(int i = 0; i < docs.GetCount(); i++) {
		for(int j = i + 1; j < docs.GetCount(); j++) {
			if(docs[i].qualified_id == docs[j].qualified_id) {
				auto mark = [&](DataDocument& d) {
					d.valid = false;
					if(d.validated) d.validation_state = "invalid";
				bool noted = false;
				for(const String& diagnostic : d.diagnostics) if(diagnostic == "duplicate_identity") noted = true;
				if(!noted) d.diagnostics.Add("duplicate_identity");
					if(d.invalid_reason.IsEmpty()) d.invalid_reason = "duplicate_identity";
				};
				mark(docs[i]); mark(docs[j]);
			}
		}
	}
	for(int i = 0; i < docs.GetCount(); i++) {
		DataDocument& document = docs[i];
		if(!document.validated) continue;
		const SchemaCard* schema = FindSchemaCard(document.kind);
		if(!schema) continue;
		for(const SchemaCard::Field& field : schema->fields) {
			if(field.reference_kind.IsEmpty()) continue;
			Value reference_value;
			if(!GetTypedField(document.data, field.name, reference_value) || !IsString(reference_value)) continue;
			String reference_id = reference_value;
			int matches = 0;
			const DataDocument* target = NULL;
			for(const DataDocument& candidate : docs)
				if(candidate.workspace_id == document.workspace_id && candidate.id == reference_id) {
					matches++;
					target = &candidate;
				}
			String error;
			if(matches == 0) error = "reference_missing:" + field.name;
			else if(matches > 1) error = "reference_ambiguous:" + field.name;
			else if(target->kind != field.reference_kind) error = "reference_wrong_kind:" + field.name;
			else if(target->validation_state != "valid" || !target->valid) error = "reference_target_invalid:" + field.name;
			if(!error.IsEmpty()) {
				document.valid = false;
				document.validation_state = "invalid";
				document.diagnostics.Add(error);
				if(document.invalid_reason.IsEmpty()) document.invalid_reason = error;
			}
		}
	}
	invalid_count = 0;
	quarantine_count = 0;
	for(const DataDocument& d : docs) {
		if(d.validated && !d.valid) invalid_count++;
		if(d.validation_state == "quarantined") quarantine_count++;
	}
	return true;
}

bool DataStore::IsSkipDir(const String& name)
{
	static const char* skip[] = {
		"bin", "out", "build", "node_modules", "__pycache__", ".cache", ".uppsrc_cache",
	};
	for(int i = 0; i < (int)__countof(skip); i++)
		if(name == skip[i])
			return true;
	return false;
}

void DataStore::ScanDir(const String& dir, const String& collection, const String& rel_prefix, int depth)
{
	if(depth > MAX_DEPTH) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add(rel_prefix + ":depth_limit");
		return;
	}

	FindFile ff(AppendFileName(dir, "*"));
	while(ff) {
		if(entries_scanned >= MAX_FILES) {
			source_incomplete = true;
			source_errors++;
			source_diagnostics.Add(rel_prefix + "/" + ff.GetName() + ":entry_limit");
			break;
		}
		entries_scanned++;
		String name = ff.GetName();
		if(name == "." || name == "..") {
			ff.Next();
			continue;
		}
		String rel = rel_prefix.IsEmpty() ? name : rel_prefix + "/" + name;
		if(ff.IsSymLink()) {
			ff.Next();
			continue;
		}
		if(ff.IsFolder()) {
			if(!name.StartsWith(".") && !IsSkipDir(name))
				ScanDir(ff.GetPath(), collection, rel, depth + 1);
		}
		else {
			String ext = ToLower(GetFileExt(name));
			if(ff.IsFile() && ext == ".json" && files_scanned < MAX_FILES)
				IndexFile(ff.GetPath(), rel, collection);
			else if(ff.IsFile() && ext == ".puml" && diagram_sources.GetCount() < MAX_FILES)
				IndexDiagramSource(ff.GetPath(), rel);
			else if(ff.IsFile() && ((ext == ".json" && files_scanned >= MAX_FILES) || (ext == ".puml" && diagram_sources.GetCount() >= MAX_FILES))) {
				source_incomplete = true;
				source_errors++;
				source_diagnostics.Add(rel + ":entry_limit");
			}
		}
		ff.Next();
	}
}

void DataStore::IndexFile(const String& path, const String& rel_path, const String& collection)
{
	files_scanned++;

	int64 len = GetFileLength(path);
	if(ExceedsSourceBudget(len, source_bytes_scanned)) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add(rel_path + ":source_limit");
		return;
	}
	source_bytes_scanned += len;

	String json = LoadFile(path);
	source_fingerprints.Add(SourceFingerprint(rel_path, json));
	if(json.GetCount() != len) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add(rel_path + ":source_read_error");
		return;
	}
	JsonFidelity fidelity = JsonFidelityScanner(json).Scan();

	Value v;
	try {
		v = ParseJSON(json);
	}
	catch(Exc) {
		source_errors++;
		bool reported = false;
		if(fidelity.duplicate_key) { source_diagnostics.Add(rel_path + ":duplicate_key"); reported = true; }
		if(fidelity.trailing_input) { source_diagnostics.Add(rel_path + ":trailing_input"); reported = true; }
		if(fidelity.numeric_precision) { source_diagnostics.Add(rel_path + ":numeric_precision_unverified"); reported = true; }
		if(fidelity.escaped_key) { source_diagnostics.Add(rel_path + ":escaped_key_unverified"); reported = true; }
		if(!reported) source_diagnostics.Add(rel_path + ":parse_error");
		return;
	}
	if(v.IsError()) {
		source_errors++;
		bool reported = false;
		if(fidelity.duplicate_key) { source_diagnostics.Add(rel_path + ":duplicate_key"); reported = true; }
		if(fidelity.trailing_input) { source_diagnostics.Add(rel_path + ":trailing_input"); reported = true; }
		if(fidelity.numeric_precision) { source_diagnostics.Add(rel_path + ":numeric_precision_unverified"); reported = true; }
		if(fidelity.escaped_key) { source_diagnostics.Add(rel_path + ":escaped_key_unverified"); reported = true; }
		if(!reported) source_diagnostics.Add(rel_path + ":parse_error");
		return;
	}
	if(!v.Is<ValueMap>()) {
		source_errors++;
		source_diagnostics.Add(rel_path + (v.Is<ValueArray>() ? ":root_array_not_object" : (IsNull(v) ? ":root_null_not_object" : ":root_scalar_not_object")));
		return;
	}

	ValueMap m = v;
	Value kind_value = MapGet(m, "kind");
	if(!IsString(kind_value)) {
		source_errors++;
		source_diagnostics.Add(rel_path + ":kind_not_string");
		return;
	}
	String kind = kind_value;
	if(kind.IsEmpty())
		return;

	DataDocument& d = docs.Add();
	d.source_id = source_id;
	d.read_only = source_read_only;
	d.data = m;
	d.source_revision = RecordSourceRevision(rel_path, json);
	if(fidelity.duplicate_key || fidelity.trailing_input || fidelity.numeric_precision || fidelity.escaped_key || !fidelity.syntax_ok)
		d.raw_json = json;
	if(fidelity.duplicate_key) d.diagnostics.Add("parser_duplicate_key");
	if(fidelity.trailing_input) d.diagnostics.Add("parser_trailing_input");
	if(fidelity.numeric_precision) d.diagnostics.Add("parser_numeric_precision_unverified");
	if(fidelity.escaped_key) d.diagnostics.Add("parser_escaped_key_unverified");
	if(!fidelity.syntax_ok) d.diagnostics.Add("parser_fidelity_syntax_mismatch");
	d.kind = kind;
	d.id = MapGetString(m, "id");
	d.schema_id = MapGetString(m, "schema_id");
	d.schema_version = MapGetString(m, "schema_version");
	d.revision = MapGetString(m, "revision");
	d.visibility = MapGetString(m, "visibility");
	d.provenance_ref = MapGetString(m, "provenance_ref");
	d.title = MapGetString(m, "title");
	d.status = MapGetString(m, "status");
	d.description = MapGetString(m, "description");
	d.collection = collection;
	d.rel_path = rel_path;
	d.path = path;

	Vector<String> parts = Split(rel_path, '/');
	if(parts.GetCount() >= 3)
		d.scope = parts[1];
	if(parts.GetCount() >= 3)
		d.parent = parts[parts.GetCount() - 2];

	kind_order.FindAdd(kind);

	d.qualified_id = source_id + ":" + collection + ":" + (d.scope.IsEmpty() ? "_" : d.scope) + ":" + kind + ":" + d.id;
	ValidateDocument(d, FindSchemaCard(kind));
}

void DataStore::IndexDiagramSource(const String& path, const String& rel_path)
{
	int64 len = GetFileLength(path);
	if(ExceedsSourceBudget(len, source_bytes_scanned)) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add(rel_path + ":source_limit");
		return;
	}
	source_bytes_scanned += len;
	String bytes = LoadFile(path);
	if(bytes.GetCount() != len) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add(rel_path + ":source_read_error");
		return;
	}
	source_fingerprints.Add(SourceFingerprint(rel_path, bytes));
	diagram_sources.Add(rel_path);
}

END_UPP_NAMESPACE
