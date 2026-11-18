#include "MultiAtlasStore.h"
#include "MultiAtlasContract.h"
#include <WebStorage/WebStorage.h>
#include <filesystem>
#ifdef PLATFORM_POSIX
#include <sys/stat.h>
#include <errno.h>
#endif
#ifdef PLATFORM_WIN32
#include <windows.h>
#endif

NAMESPACE_UPP

namespace {

// 0: absent, 1: requested ordinary object, -1: symlink or wrong kind.
int Inspect(const String& path, bool directory)
{
#ifdef PLATFORM_POSIX
	struct stat st;
	if(lstat(~path, &st) != 0) return errno == ENOENT ? 0 : -1;
	if(S_ISLNK(st.st_mode)) return -1;
	return directory ? (S_ISDIR(st.st_mode) ? 1 : -1) : (S_ISREG(st.st_mode) ? 1 : -1);
#elif defined(PLATFORM_WIN32)
	WString p = ToSystemCharsetW(path);
	DWORD attr = GetFileAttributesW(~p);
	if(attr == INVALID_FILE_ATTRIBUTES)
		return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND ? 0 : -1;
	if(attr & FILE_ATTRIBUTE_REPARSE_POINT) return -1;
	return directory ? ((attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : -1) :
	                   ((attr & FILE_ATTRIBUTE_DIRECTORY) ? -1 : 1);
#else
	return (directory ? DirectoryExists(path) : FileExists(path)) ? 1 : 0;
#endif
}

String Utf8Path(const std::filesystem::path& p)
{
	std::string s = p.generic_u8string();
	return String(s.c_str(), (int)s.size());
}

bool SafeRelative(const String& value)
{
	if(value.IsEmpty() || IsFullPath(value) || value[0] == '/' || value[0] == '\\') return false;
	String normalized;
	for(int i = 0; i < value.GetCount(); i++) normalized.Cat(value[i] == '\\' ? '/' : value[i]);
	Vector<String> parts = Split(normalized, '/');
	for(const String& part : parts)
		if(part.IsEmpty() || part == "." || part == "..") return false;
	return true;
}

bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& path)
{
	auto ri = root.begin(), pi = path.begin();
	for(; ri != root.end(); ++ri, ++pi)
		if(pi == path.end() || *ri != *pi) return false;
	return true;
}

// Validate and optionally realize the path components beneath the final
// multiatlas namespace. No path component in the store subtree may be a link.
bool CheckNamespacePath(const String& target, bool create, String& error)
{
	if(!IsFullPath(target)) { error = "store path must be absolute"; return false; }
	std::filesystem::path p = std::filesystem::u8path(~target).lexically_normal();
	std::vector<std::filesystem::path> parts;
	for(const auto& part : p)
		if(part != p.root_name() && part != p.root_directory()) parts.push_back(part);
	int ns = -1;
	for(int i = 0; i < (int)parts.size(); i++)
		if(Utf8Path(parts[i]) == "multiatlas") ns = i;
	bool legacy_parent = false;
	for(int i = 0; i < ns; i++)
		if(Utf8Path(parts[i]) == "atlas") legacy_parent = true;
	if(ns < 0 || legacy_parent || (int)parts.size() - ns < 5 ||
	   Utf8Path(parts[parts.size() - 3]) != "store" ||
	   Utf8Path(parts[parts.size() - 2]) != "data") {
		error = "store path must be nested under a multiatlas/ namespace";
		return false;
	}
	std::filesystem::path parent = p.root_path();
	for(int i = 0; i < ns; i++) parent /= parts[i];
	std::error_code ec;
	std::filesystem::path canonical_parent = std::filesystem::canonical(parent, ec);
	if(ec) { error = "multiatlas namespace parent does not exist"; return false; }
	String namespace_path = Utf8Path(canonical_parent / "multiatlas");
	if(Inspect(namespace_path, true) != 1) {
		error = "multiatlas namespace is missing, symlinked, or not a directory";
		return false;
	}
	std::filesystem::path canonical_ns = std::filesystem::canonical(std::filesystem::u8path(~namespace_path), ec);
	if(ec) { error = "cannot resolve multiatlas namespace"; return false; }
	// Resolve the configured parent before appending the namespace subtree.
	std::filesystem::path walk = canonical_ns;
	for(int i = ns + 1; i < (int)parts.size(); i++) {
		walk /= parts[i];
		int state = Inspect(Utf8Path(walk), true);
		if(state == 0 && create) {
			std::error_code create_error;
			std::filesystem::create_directory(walk, create_error);
			if(create_error && create_error != std::errc::file_exists) {
				error = "cannot create MultiAtlas directory: " + Utf8Path(walk);
				return false;
			}
			state = Inspect(Utf8Path(walk), true);
		}
		if(state < 0) {
			error = "MultiAtlas storage directory is a symlink or not a directory: " + Utf8Path(walk);
			return false;
		}
		if(state == 0) return true; // absent read: do not realize any later path.
	}
	if(!IsWithin(canonical_ns, walk)) {
		error = "MultiAtlas storage path escapes its namespace";
		return false;
	}
	return true;
}

bool CheckRootAndCollection(const String& data_root, const String& collection,
	                        const String& key, bool create, bool& absent, String& error)
{
	absent = false;
	if(!JsonFileStore::ValidId(collection) || !JsonFileStore::ValidId(key)) {
		error = "invalid MultiAtlas collection or record id";
		return false;
	}
	if(GetFileName(data_root) != "data") {
		error = "store root must be the resolved <repo>/<store>/data path";
		return false;
	}
	String store_root = GetFileFolder(data_root);
	if(Inspect(store_root, true) != 1) {
		error = "MultiAtlas store root is missing, symlinked, or not a directory";
		return false;
	}
	String coll = AppendFileName(AppendFileName(data_root, collection), key + ".json");
	if(!CheckNamespacePath(AppendFileName(data_root, collection), create, error)) return false;
	int record_state = Inspect(coll, false);
	if(record_state < 0) {
		error = "MultiAtlas record is a symlink or not a regular file: " + coll;
		return false;
	}
	if(!create && record_state == 0) { absent = true; return true; }
	return true;
}

String StoreRootParent(const String& data_root) { return GetFileFolder(data_root); }

template <class T>
bool LoadOne(const String& data_root, const String& collection, const String& key,
             T& out, String& revision, String& error)
{
	error.Clear(); revision.Clear();
	bool absent = false;
	if(!CheckRootAndCollection(data_root, collection, key, false, absent, error)) return false;
	if(absent) { error = "record not found: " + key; return false; }
	JsonFileStore store("multiatlas", StoreRootParent(data_root), collection);
	String json;
	StoreResult got = store.Get(key, json, revision);
	if(!got) { error = got.error; return false; }
	String envelope_revision;
	if(!MultiAtlasLoadRecord(out, json, envelope_revision)) {
		error = "MultiAtlas record is unreadable: " + key;
		return false;
	}
	// WebStorage returns the envelope revision separately. Keep this check so
	// reads always report the revision attached to the persisted data record.
	if(!envelope_revision.IsEmpty() && envelope_revision != revision) {
		error = "MultiAtlas record envelope revision mismatch: " + key;
		return false;
	}
	return true;
}

template <class T>
bool SaveOne(const String& data_root, const String& collection, const String& key,
             const T& in, const String& expected_revision, String& error)
{
	error.Clear();
	if(expected_revision.IsEmpty()) {
		error = "expected revision is required (use RecordAbsent() when creating)";
		return false;
	}
	bool absent = false;
	if(!CheckRootAndCollection(data_root, collection, key, true, absent, error)) return false;
	JsonFileStore store("multiatlas", StoreRootParent(data_root), collection);
	String new_revision;
	StoreResult put = store.Put(key, StoreAsJson(in), expected_revision, new_revision);
	if(!put) {
		error = put.conflict ? String("revision conflict") : put.error;
		return false;
	}
	return true;
}

String EventKey(const String& family, const String& id)
{
	if(!JsonFileStore::ValidId(family) || !JsonFileStore::ValidId(id)) return String();
	String key = family + ".ev-" + id;
	return JsonFileStore::ValidId(key) ? key : String();
}

} // namespace

String MultiAtlasResolveTrackStore(const MultiAtlasEnsembleDef& ens, const String& track_id)
{
	String repo, store;
	if(track_id == "meta") {
		repo = ens.meta_repo;
		store = ens.meta_store;
	}
	else {
		for(const MultiAtlasTrackDef& track : ens.tracks)
			if(track.track_id == track_id) { repo = track.repo; store = track.store; break; }
	}
	if(repo.IsEmpty() || !IsFullPath(repo) || !SafeRelative(store)) return String();
	String normalized;
	for(int i = 0; i < store.GetCount(); i++) normalized.Cat(store[i] == '\\' ? '/' : store[i]);
	if(!normalized.StartsWith("multiatlas/") || GetFileName(normalized) != "store") return String();
	std::error_code ec;
	std::filesystem::path canonical_repo = std::filesystem::canonical(std::filesystem::u8path(~repo), ec);
	if(ec || !DirectoryExists(Utf8Path(canonical_repo))) return String();
	std::filesystem::path p = canonical_repo / std::filesystem::u8path(~normalized);
	std::filesystem::path namespace_candidate = canonical_repo / "multiatlas";
	if(Inspect(Utf8Path(namespace_candidate), true) != 1) return String();
	std::filesystem::path canonical_namespace = std::filesystem::canonical(namespace_candidate, ec);
	if(ec) return String();
	if(canonical_namespace != namespace_candidate.lexically_normal()) return String();
	std::filesystem::path canonical_store = std::filesystem::canonical(p, ec);
	if(ec || !DirectoryExists(Utf8Path(canonical_store)) || !IsWithin(canonical_namespace, canonical_store)) return String();
	return Utf8Path(canonical_store / "data");
}

bool MultiAtlasLoadTrackMap(const String& root, const String& key, MultiAtlasMap& out, String& rev, String& err)
{ return LoadOne(root, "maps", key, out, rev, err); }
bool MultiAtlasSaveTrackMap(const String& root, const String& key, const MultiAtlasMap& in, const String& prev, String& err)
{
	MultiAtlasCheck check; MultiAtlasValidate(in, check);
	if(!check.Ok()) {
		err.Clear();
		for(const String& message : check.errors) {
			if(err.GetCount()) err.Cat('\n');
			err.Cat(message);
		}
		return false;
	}
	return SaveOne(root, "maps", key, in, prev, err);
}
bool MultiAtlasLoadTrackFamily(const String& root, const String& key, MultiAtlasTrack& out, String& rev, String& err)
{ return LoadOne(root, "families", key, out, rev, err); }
bool MultiAtlasSaveTrackFamily(const String& root, const String& key, const MultiAtlasTrack& in, const String& prev, String& err)
{ return SaveOne(root, "families", key, in, prev, err); }
bool MultiAtlasLoadMetaFamily(const String& root, const String& key, MultiAtlasMetaFamily& out, String& rev, String& err)
{ return LoadOne(root, "families", key, out, rev, err); }
bool MultiAtlasSaveMetaFamily(const String& root, const String& key, const MultiAtlasMetaFamily& in, const String& prev, String& err)
{ return SaveOne(root, "families", key, in, prev, err); }
bool MultiAtlasLoadTrackRules(const String& root, const String& key, MultiAtlasRules& out, String& rev, String& err)
{ return LoadOne(root, "rules", key, out, rev, err); }
bool MultiAtlasSaveTrackRules(const String& root, const String& key, const MultiAtlasRules& in, const String& prev, String& err)
{ return SaveOne(root, "rules", key, in, prev, err); }

static bool ValidTrackScope(const String& track_id)
{
	return track_id == "meta" || MultiAtlasIsValidTrackId(track_id);
}

bool MultiAtlasLoadTrackEvents(const String& root, const String& family, const String& expected_track_id,
                               Vector<MultiAtlasEvent>& out, String& err)
{
	out.Clear(); err.Clear();
	if(!JsonFileStore::ValidId(family)) { err = "invalid event family key"; return false; }
	if(!ValidTrackScope(expected_track_id)) { err = "invalid event track scope"; return false; }
	// Event collections are append-only. Avoid constructing JsonFileStore until
	// all parents exist, because its constructor realizes missing directories.
	if(GetFileName(root) != "data" || Inspect(GetFileFolder(root), true) != 1) {
		err = "MultiAtlas store root is missing or invalid"; return false;
	}
	String collection = AppendFileName(root, "events");
	if(!CheckNamespacePath(collection, false, err)) return false;
	if(Inspect(collection, true) == 0) return true;
	JsonFileStore store("multiatlas", StoreRootParent(root), "events");
	Vector<String> ids;
	StoreResult listed = store.List(ids);
	if(!listed) { err = listed.error; return false; }
	String prefix = family + ".ev-";
	for(const String& id : ids) {
		if(!id.StartsWith(prefix)) continue;
		String json, rev;
		if(Inspect(AppendFileName(collection, id + ".json"), false) != 1) {
			err = "MultiAtlas event record is a symlink or not a regular file"; out.Clear(); return false;
		}
		StoreResult got = store.Get(id, json, rev);
		if(!got) { err = got.error; out.Clear(); return false; }
		MultiAtlasEvent event; String envelope_rev;
		if(!MultiAtlasLoadRecord(event, json, envelope_rev) || event.id.IsEmpty()) {
			err = "MultiAtlas event record is unreadable: " + id; out.Clear(); return false;
		}
		if(EventKey(family, event.id) != id || event.track_id != expected_track_id) {
			err = "MultiAtlas event record does not match its family-scoped key: " + id; out.Clear(); return false;
		}
		out.Add(pick(event));
	}
	return true;
}

bool MultiAtlasPutTrackEvent(const String& root, const String& family, const String& expected_track_id,
                             const MultiAtlasEvent& event, String& err)
{
	String id = EventKey(family, event.id);
	if(id.IsEmpty()) { err = "invalid family or event id"; return false; }
	if(!ValidTrackScope(expected_track_id) || event.track_id != expected_track_id) {
		err = "event track scope does not match expected track"; return false;
	}
	return SaveOne(root, "events", id, event, RecordAbsent(), err);
}

bool MultiAtlasLoadTrackAcks(const String& root, const String& track_id,
                             Vector<MultiAtlasWarningAck>& out, String& err)
{
	out.Clear(); err.Clear();
	if(GetFileName(root) != "data") { err = "store root must be the resolved data directory"; return false; }
	if(!CheckNamespacePath(AppendFileName(root, "acks"), false, err)) return false;
	return MultiAtlasLoadAcks(GetFileFolder(root), track_id, out, err);
}

bool MultiAtlasSaveTrackAck(const String& root, const String& track_id,
                            const MultiAtlasWarningAck& ack, const String& prev,
                            String& new_rev, String& err)
{
	new_rev.Clear(); err.Clear();
	if(GetFileName(root) != "data") { err = "store root must be the resolved data directory"; return false; }
	if(prev.IsEmpty()) { err = "expected revision is required (use RecordAbsent() when creating)"; return false; }
	if(!JsonFileStore::ValidId(ack.warning_id)) { err = "invalid warning id"; return false; }
	bool absent = false;
	if(!CheckRootAndCollection(root, "acks", ack.warning_id, true, absent, err)) return false;
	bool saved = MultiAtlasSaveAck(GetFileFolder(root), track_id, ack, prev, new_rev, err);
	if(!saved && (err.Find("revision conflict") >= 0 ||
	              (prev == RecordAbsent() && err.Find("already exists") >= 0)))
		err = "revision conflict";
	return saved;
}

END_UPP_NAMESPACE
