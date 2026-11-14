#include "AtlasImport.h"
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

static String ProjectsPath(const String& data_root)
{
	return AppendFileName(data_root, "projects.json");
}

static String JsonString(const ValueMap& v, const char *key)
{
	Value x = v[key];
	return x.Is<String>() ? String((String)x) : String();
}

static bool SafeRelativePath(const String& path)
{
	if(path.IsEmpty() || IsFullPath(path) || path[0] == '/' || path[0] == '\\')
		return false;
	return path.Find("..") < 0;
}

static bool ProjectFromJson(const ValueMap& v, AtlasProject& p, String& error)
{
	p.name = JsonString(v, "name");
	p.repo = JsonString(v, "repo");
	p.store = JsonString(v, "store");
	p.title = JsonString(v, "title");
	p.level = JsonString(v, "level");
	if(p.title.IsEmpty())
		p.title = p.name;
	if(p.level.IsEmpty())
		p.level = "private";
	ContentLevel level;
	if(!AtlasIdValid(p.name)) {
		error = "project name must be a valid map id: " + p.name;
		return false;
	}
	if(!IsFullPath(p.repo)) {
		error = "project " + p.name + ": repo must be an absolute path";
		return false;
	}
	if(p.store.GetCount() && !SafeRelativePath(p.store)) {
		error = "project " + p.name + ": store must be a relative path inside the repository";
		return false;
	}
	if(!ContentLevelParse(p.level, level)) {
		error = "project " + p.name + ": level must be public, protected or private";
		return false;
	}
	Value list = v["editors"];
	if(list.Is<ValueArray>()) {
		ValueArray a = list;
		for(int i = 0; i < a.GetCount(); i++) {
			Value x = a[i];
			if(x.Is<String>())
				p.editors.Add(ToLower((String)x));
		}
	}
	return true;
}

bool AtlasLoadProjects(const String& data_root, Vector<AtlasProject>& out, String& error)
{
	out.Clear();
	String path = ProjectsPath(data_root);
	if(!FileExists(path))
		return true;
	Value root = ParseJSON(LoadFile(path));
	if(!root.Is<ValueMap>()) {
		error = "projects file is not a JSON object: " + path;
		return false;
	}
	ValueMap top = root;
	Value list = top["projects"];
	if(!list.Is<ValueArray>())
		return true;
	ValueArray a = list;
	for(int i = 0; i < a.GetCount(); i++) {
		Value x = a[i];
		if(!x.Is<ValueMap>()) {
			error = "projects file: entry " + AsString(i + 1) + " is not an object";
			return false;
		}
		AtlasProject p;
		if(!ProjectFromJson(ValueMap(x), p, error))
			return false;
		for(const AtlasProject& other : out)
			if(other.name == p.name) {
				error = "projects file: project " + p.name + " is listed twice";
				return false;
			}
		out.Add(pick(p));
	}
	return true;
}

int AtlasFindProject(const Vector<AtlasProject>& all, const String& name)
{
	for(int i = 0; i < all.GetCount(); i++)
		if(all[i].name == name)
			return i;
	return -1;
}

String AtlasStoreRoot(const String& data_root, const String& family)
{
	Vector<AtlasProject> all;
	String error;
	if(!AtlasLoadProjects(data_root, all, error))
		return data_root;
	int i = AtlasFindProject(all, family);
	if(i < 0 || all[i].store.IsEmpty())
		return data_root;
	return AppendFileName(all[i].repo, all[i].store);
}

bool AtlasProjectEditor(const String& data_root, const String& family, const String& username)
{
	if(family.IsEmpty() || username.IsEmpty())
		return false;
	Vector<AtlasProject> all;
	String error;
	if(!AtlasLoadProjects(data_root, all, error))
		return false;
	int i = AtlasFindProject(all, family);
	if(i < 0)
		return false;
	for(const String& e : all[i].editors)
		if(e == ToLower(username))
			return true;
	return false;
}

static AtlasImportResult Fail(int status, const String& error)
{
	AtlasImportResult r;
	r.status = status;
	r.error = error;
	return r;
}

AtlasImportResult AtlasCreateMap(const String& data_root, const AtlasMap& m, const String& level)
{
	AtlasCheck check;
	AtlasValidate(m, check);
	String key = AtlasDocKey(m.map, m.version);
	if(!JsonFileStore::ValidId(key))
		check.errors.Add("map version has characters the store does not allow: " + m.version);
	if(!check.Ok())
		return Fail(400, Join(check.errors, "\n"));

	AtlasMap existing;
	String rev, err;
	if(AtlasLoadMap(data_root, key, existing, rev, err))
		return Fail(409, "a map with that id and version already exists");

	if(level.GetCount()) {
		ContentLevel parsed;
		if(!ContentLevelParse(level, parsed))
			return Fail(400, "level must be public, protected or private");
		ContentPolicyStore& store = ContentPolicyFor(data_root);
		if(store.LoadError().GetCount())
			return Fail(500, store.LoadError());
		ContentPolicy p = store.Record(key);
		p.level = parsed;
		store.SetRecord(key, p);
		if(!store.Save(err))
			return Fail(500, err);
	}

	AtlasImportResult out;
	out.key = key;
	if(!AtlasSaveMap(data_root, key, m, String(), out.revision, err))
		return Fail(500, err);
	return out;
}

AtlasImportResult AtlasSeedMap(const String& data_root, const String& key, const String& seed)
{
	AtlasMap m;
	String rev, err;
	if(!AtlasLoadMap(data_root, key, m, rev, err))
		return Fail(404, err);
	AtlasCheck check;
	if(!AtlasParseSeed(seed, m, check))
		return Fail(400, Join(check.errors, "\n"));
	AtlasImportResult out;
	out.key = key;
	out.elements = m.elements.GetCount();
	out.roads = m.roads.GetCount();
	if(!AtlasSaveMap(data_root, key, m, rev, out.revision, err))
		return Fail(err.Find("revision") >= 0 ? 409 : 500, err);
	return out;
}

END_UPP_NAMESPACE
