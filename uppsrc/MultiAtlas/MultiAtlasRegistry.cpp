#include "MultiAtlasRegistry.h"
#include "MultiAtlasContract.h"
#include <filesystem>

NAMESPACE_UPP

static String RegistryPath(const String& site_dir)
{
	return AppendFileName(AppendFileName(AppendFileName(site_dir, "engine"), "multiatlas"), "projects.json");
}

static String ReadRegistryFile(const String& path, String& error)
{
	FileIn in(path);
	if(!in.IsOpen()) {
		error = "cannot read registry file: " + path;
		return String();
	}
	String data;
	while(!in.IsEof()) {
		int count = min(8192, (int)in.GetLeft());
		if(count <= 0) break;
		data.Cat(in.Get(count));
		if(data.GetCount() > 4 * 1024 * 1024) {
			error = "registry file exceeds 4 MiB limit: " + path;
			return String();
		}
	}
	return data;
}

static String JsonString(const ValueMap& map, const char *key)
{
	Value v = map.Get(key);
	return v.Is<String>() ? (String)v : String();
}

static String CanonicalPath(const String& path)
{
	std::error_code ec;
	std::filesystem::path canonical = std::filesystem::canonical(std::filesystem::u8path(~path), ec);
	if(ec) return String();
	std::string utf8 = canonical.generic_u8string();
	return String(utf8.c_str(), (int)utf8.size());
}

static bool IsInside(const String& root, const String& path)
{
	if(path == root)
		return true;
	return path.StartsWith(root) && path.GetCount() > root.GetCount() &&
	       (path[root.GetCount()] == '/' || path[root.GetCount()] == '\\');
}

static bool SafeStoreRelative(const String& rel)
{
	if(rel.IsEmpty() || IsFullPath(rel) || rel[0] == '/' || rel[0] == '\\')
		return false;
	String normalized;
	for(int i = 0; i < rel.GetCount(); i++)
		normalized.Cat(rel[i] == '\\' ? '/' : rel[i]);
	Vector<String> parts = Split(normalized, '/');
	for(const String& part : parts)
		if(part.IsEmpty() || part == "." || part == "..")
			return false;
	return true;
}

static bool HasMultiAtlasPrefix(const String& rel)
{
	return rel == "multiatlas" || (rel.StartsWith("multiatlas/") && rel.GetCount() > 11);
}

static bool SamePathComponent(const String& a, const String& b)
{
#ifdef _WIN32
	return ToLower(a) == ToLower(b);
#else
	return a == b;
#endif
}

static bool InsideMultiAtlasNamespace(const String& repo, const String& store)
{
	if(!IsInside(repo, store)) return false;
	String rel = store.Mid(repo.GetCount() + 1);
	Vector<String> parts = Split(rel, '/');
	return parts.GetCount() > 1 && SamePathComponent(parts[0], "multiatlas");
}

static bool ExistingDirectoryInside(const String& repo, const String& store, String& error)
{
	if(!IsFullPath(repo)) {
		error = "repo must be an absolute path";
		return false;
	}
	String repo_real = CanonicalPath(repo);
	if(repo_real.IsEmpty() || !DirectoryExists(repo_real)) {
		error = "repo directory does not exist: " + repo;
		return false;
	}
	if(!SafeStoreRelative(store)) {
		error = "store must be a safe relative path under its repository: " + store;
		return false;
	}
	String normalized_store;
	for(int i = 0; i < store.GetCount(); i++)
		normalized_store.Cat(store[i] == '\\' ? '/' : store[i]);
	if(!HasMultiAtlasPrefix(normalized_store)) {
		error = "store must use the multiatlas/ namespace: " + store;
		return false;
	}
	String candidate = AppendFileName(repo_real, normalized_store);
	String store_real = CanonicalPath(candidate);
	if(store_real.IsEmpty() || !DirectoryExists(store_real)) {
		error = "store directory does not exist: " + candidate;
		return false;
	}
	if(!InsideMultiAtlasNamespace(repo_real, store_real)) {
		error = "store path resolves outside the multiatlas/ namespace through a symlink: " + store;
		return false;
	}
	return true;
}

static String CanonicalStore(const String& repo, const String& store)
{
	if(!IsFullPath(repo) || !SafeStoreRelative(store)) return String();
	String root = CanonicalPath(repo);
	if(root.IsEmpty()) return String();
	String path = CanonicalPath(NormalizePath(AppendFileName(root, store)));
	return path.IsEmpty() || !InsideMultiAtlasNamespace(root, path) ? String() : path;
}

static bool Required(const String& value, const String& field, String& error)
{
	if(!value.IsEmpty())
		return true;
	error = "missing or invalid required field: " + field;
	return false;
}

static bool ParseTrack(const ValueMap& value, const String& ensemble, MultiAtlasTrackDef& track,
	                   String& error)
{
	track.track_id = JsonString(value, "track_id");
	track.family = JsonString(value, "family");
	track.repo = JsonString(value, "repo");
	track.branch = JsonString(value, "branch");
	track.store = JsonString(value, "store");
	if(!Required(track.track_id, "track_id", error) || !MultiAtlasIsValidTrackId(track.track_id)) {
		if(error.IsEmpty()) error = "ensemble " + ensemble + ": invalid track_id: " + track.track_id;
		return false;
	}
	if(!Required(track.family, "family", error) || !Required(track.branch, "branch", error) ||
	   !Required(track.repo, "repo", error) || !Required(track.store, "store", error))
		return false;
	if(!ExistingDirectoryInside(track.repo, track.store, error)) {
		error = "ensemble " + ensemble + " track " + track.track_id + ": " + error;
		return false;
	}
	return true;
}

static bool ParseEnsemble(const ValueMap& value, MultiAtlasEnsembleDef& ensemble, String& error)
{
	ensemble.name = JsonString(value, "name");
	ensemble.title = JsonString(value, "title");
	if(!Required(ensemble.name, "ensemble name", error) || !Required(ensemble.title, "ensemble " + ensemble.name + " title", error))
		return false;
	Value meta_value = value.Get("meta");
	if(!meta_value.Is<ValueMap>()) {
		error = "ensemble " + ensemble.name + ": meta must be an object";
		return false;
	}
	ValueMap meta = meta_value;
	ensemble.meta_family = JsonString(meta, "family");
	ensemble.meta_repo = JsonString(meta, "repo");
	ensemble.meta_store = JsonString(meta, "store");
	if(!Required(ensemble.meta_family, "ensemble " + ensemble.name + " meta.family", error) ||
	   !Required(ensemble.meta_repo, "ensemble " + ensemble.name + " meta.repo", error) ||
	   !Required(ensemble.meta_store, "ensemble " + ensemble.name + " meta.store", error))
		return false;
	if(!ExistingDirectoryInside(ensemble.meta_repo, ensemble.meta_store, error)) {
		error = "ensemble " + ensemble.name + " meta: " + error;
		return false;
	}
	Value tracks_value = value.Get("tracks");
	if(!tracks_value.Is<ValueArray>()) {
		error = "ensemble " + ensemble.name + ": tracks must be an array";
		return false;
	}
	ValueArray tracks = tracks_value;
	for(int i = 0; i < tracks.GetCount(); i++) {
		Value item = tracks[i];
		if(!item.Is<ValueMap>()) {
			error = "ensemble " + ensemble.name + ": track " + AsString(i + 1) + " must be an object";
			return false;
		}
		MultiAtlasTrackDef track;
		if(!ParseTrack(ValueMap(item), ensemble.name, track, error))
			return false;
		for(const MultiAtlasTrackDef& prior : ensemble.tracks)
			if(prior.track_id == track.track_id) {
				error = "ensemble " + ensemble.name + ": duplicate track_id: " + track.track_id;
				return false;
			}
		ensemble.tracks.Add(pick(track));
	}
	return true;
}

bool MultiAtlasLoadRegistry(const String& site_dir, Vector<MultiAtlasEnsembleDef>& out, String& err)
{
	out.Clear();
	err.Clear();
	String path = RegistryPath(site_dir);
	if(!FileExists(path))
		return true;
	String json = ReadRegistryFile(path, err);
	if(!err.IsEmpty()) return false;
	if(json.IsEmpty()) {
		err = "cannot read registry file: " + path;
		return false;
	}
	Value root;
	try {
		root = ParseJSON(json);
	}
	catch(...) {
		err = "registry file is not valid JSON: " + path;
		return false;
	}
	if(root.IsError()) {
		err = "registry file is not valid JSON: " + path;
		return false;
	}
	if(!root.Is<ValueMap>()) {
		err = "registry root must be an object: " + path;
		return false;
	}
	ValueMap top = root;
	Value list = top.Get("ensembles");
	if(!list.Is<ValueArray>()) {
		err = "registry field ensembles must be an array: " + path;
		return false;
	}
	ValueArray ensembles = list;
	for(int i = 0; i < ensembles.GetCount(); i++) {
		Value item = ensembles[i];
		if(!item.Is<ValueMap>()) {
			err = "registry ensemble " + AsString(i + 1) + " must be an object";
			out.Clear();
			return false;
		}
		MultiAtlasEnsembleDef ensemble;
		if(!ParseEnsemble(ValueMap(item), ensemble, err)) {
			out.Clear();
			return false;
		}
		Vector<String> stores;
		stores.Add(CanonicalStore(ensemble.meta_repo, ensemble.meta_store));
		for(const MultiAtlasTrackDef& track : ensemble.tracks)
			stores.Add(CanonicalStore(track.repo, track.store));
		for(int a = 0; a < stores.GetCount(); a++)
			for(int b = 0; b < a; b++)
				if(!stores[a].IsEmpty() && stores[a] == stores[b]) {
				err = "ensemble " + ensemble.name + ": meta and tracks resolve to the same canonical store";
				out.Clear();
				return false;
			}
		// Ensemble names are unique independently of physical store alias checks.
		for(const MultiAtlasEnsembleDef& prior : out)
			if(prior.name == ensemble.name) {
				err = "registry has duplicate ensemble name: " + ensemble.name;
				out.Clear();
				return false;
			}
		// Detect aliases across ensembles too; stores are isolated by registration identity.
		for(const MultiAtlasEnsembleDef& prior : out) {
			Vector<String> prior_stores;
			prior_stores.Add(CanonicalStore(prior.meta_repo, prior.meta_store));
			for(const MultiAtlasTrackDef& track : prior.tracks)
				prior_stores.Add(CanonicalStore(track.repo, track.store));
			for(const String& current : stores)
				for(const String& previous : prior_stores)
					if(!current.IsEmpty() && current == previous) {
						err = "registry ensembles resolve to the same canonical store: " + prior.name + " and " + ensemble.name;
						out.Clear();
						return false;
					}
		}
		out.Add(pick(ensemble));
	}
	return true;
}

bool MultiAtlasFindEnsemble(const String& site_dir, const String& name,
	                        MultiAtlasEnsembleDef& out, String& err)
{
	Vector<MultiAtlasEnsembleDef> all;
	if(!MultiAtlasLoadRegistry(site_dir, all, err))
		return false;
	for(const MultiAtlasEnsembleDef& ensemble : all)
		if(ensemble.name == name) {
			out.name = ensemble.name;
			out.title = ensemble.title;
			out.meta_family = ensemble.meta_family;
			out.meta_repo = ensemble.meta_repo;
			out.meta_store = ensemble.meta_store;
			out.tracks.Clear();
			for(const MultiAtlasTrackDef& track : ensemble.tracks) {
				MultiAtlasTrackDef copy;
				copy.track_id = track.track_id; copy.family = track.family;
				copy.repo = track.repo; copy.branch = track.branch; copy.store = track.store;
				out.tracks.Add(pick(copy));
			}
			return true;
		}
	err = "ensemble not found: " + name;
	return false;
}

END_UPP_NAMESPACE
