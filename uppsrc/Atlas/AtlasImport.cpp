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
	p.seed = JsonString(v, "seed");
	p.version = JsonString(v, "version");
	p.title = JsonString(v, "title");
	p.level = JsonString(v, "level");
	p.roadmap = JsonString(v, "roadmap");
	if(p.roadmap.GetCount() && !SafeRelativePath(p.roadmap)) {
		error = "project " + p.name + ": roadmap must be a relative path inside the repository";
		return false;
	}
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
	if(!SafeRelativePath(p.seed)) {
		error = "project " + p.name + ": seed must be a relative path inside the repository";
		return false;
	}
	if(AtlasVersionCode(p.version) == ATLAS_VERSION_UNSET) {
		error = "project " + p.name + ": version must read v<a.b.c>";
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

// The roadmap file (JSON): the family's versions in order, and each element's first version. Every element of
// the seed must be named; "from" is the version it first appears in, and "note" is kept as a note event.
//
//   {"versions": ["0.0.1", "0.0.2", "1.0.0"],
//    "elements": {"MainComputer": {"from": "0.0.1", "note": "desktop app root"}, ...}}
//
// The last version is the head, which the project's version names. An element's creation at "from" makes it
// present from that version onwards; a parent block may not start after its children. Everything is checked
// before anything is written: the events are the replay's, validated against the head.
static bool ReadRoadmap(const String& text, const String& head_label, const AtlasMap& head, AtlasFamily& f,
                        Vector<AtlasEvent>& events, String& error)
{
	Value root;
	try {
		root = ParseJSON(text);
	}
	catch(Exc&) {
		error = "not valid JSON";
		return false;
	}
	if(!root.Is<ValueMap>()) {
		error = "must be a JSON object";
		return false;
	}
	ValueMap top = root;
	Value vl = top.Get("versions");
	if(!vl.Is<ValueArray>() || ValueArray(vl).GetCount() == 0) {
		error = "versions must be a non-empty list of labels";
		return false;
	}
	ValueArray labels_json = vl;
	Vector<String> labels;
	int last_code = -1;
	for(int i = 0; i < labels_json.GetCount(); i++) {
		Value x = labels_json[i];
		if(!x.Is<String>()) {
			error = "versions must be strings";
			return false;
		}
		String label = (String)x;
		int code = AtlasVersionCode(label);
		if(code == ATLAS_VERSION_UNSET) {
			error = "version " + label + " must read v<a.b.c>";
			return false;
		}
		if(code <= last_code) {
			error = "versions must ascend without repeats; " + label + " does not follow the one before";
			return false;
		}
		last_code = code;
		labels.Add(label);
	}
	if(AtlasVersionCode(labels.Top()) != AtlasVersionCode(head_label)) {
		error = "the last version must be the project's version " + head_label;
		return false;
	}
	Value el_json = top.Get("elements");
	if(!el_json.Is<ValueMap>()) {
		error = "elements must be an object keyed by element name";
		return false;
	}
	ValueMap elements_map = el_json;
	auto LabelIndex = [&](const String& label) {
		for(int i = 0; i < labels.GetCount(); i++)
			if(labels[i] == label)
				return i;
		return -1;
	};
	// Every name in the file is an element of the head, and every element is named.
	for(int i = 0; i < elements_map.GetCount(); i++) {
		Value k = elements_map.GetKey(i);
		String name = k.Is<String>() ? String((String)k) : String();
		bool known = false;
		for(const AtlasElement& e : head.elements)
			if(e.name == name)
				known = true;
		if(!known) {
			error = "element " + name + " is not in the goal map";
			return false;
		}
	}
	Vector<int> first;   // label index per head element
	for(const AtlasElement& e : head.elements) {
		Value entry = elements_map.Get(e.name);
		if(!entry.Is<ValueMap>()) {
			error = "element " + e.name + " has no entry";
			return false;
		}
		ValueMap em = entry;
		String from = JsonString(em, "from");
		int idx = LabelIndex(from);
		if(idx < 0) {
			error = "element " + e.name + " names an unknown version: " + from;
			return false;
		}
		first.Add(idx);
	}
	for(int i = 0; i < head.elements.GetCount(); i++) {
		const AtlasElement& e = head.elements[i];
		int parent = head.FindElement(e.parent);
		if(parent >= 0 && first[parent] > first[i]) {
			error = "block " + head.elements[parent].name + " starts after its element " + e.name;
			return false;
		}
	}

	f.versions.Clear();
	for(int i = 0; i < labels.GetCount(); i++) {
		AtlasVersion v;
		v.vid = "v" + AsString(i + 1);
		v.code = AtlasVersionCode(labels[i]);
		f.versions.Add(pick(v));
	}
	events.Clear();
	for(int i = 0; i < head.elements.GetCount(); i++) {
		const AtlasElement& e = head.elements[i];
		AtlasEvent created;
		created.id = "ev-" + Format64Hex((int64)(Random64() & 0x7fffffffffffffffULL));
		created.at = AtlasNowIso();
		created.actor = "roadmap-import";
		created.vid = f.versions[first[i]].vid;
		created.subject_kind = "element";
		created.subject_id = e.id;
		created.type = "created";
		created.name = e.name;
		events.Add(pick(created));
		String note = JsonString((ValueMap)elements_map.Get(e.name), "note");
		if(note.GetCount()) {
			AtlasEvent n;
			n.id = "ev-" + Format64Hex((int64)(Random64() & 0x7fffffffffffffffULL));
			n.at = AtlasNowIso();
			n.actor = "roadmap-import";
			n.vid = f.versions[first[i]].vid;
			n.subject_kind = "element";
			n.subject_id = e.id;
			n.type = "note";
			n.text = note;
			events.Add(pick(n));
		}
	}
	for(const AtlasEvent& e : events) {
		AtlasCheck check;
		AtlasValidateEvent(e, f, head, check);
		if(!check.Ok()) {
			error = Join(check.errors, "; ");
			return false;
		}
	}
	return true;
}

AtlasImportResult AtlasImportProject(const String& data_root, const AtlasProject& p)
{
	AtlasFamily existing;
	String frev, err;
	if(AtlasLoadFamily(data_root, p.name, existing, frev, err))
		return Fail(409, "the family " + p.name + " already exists");

	// The seed and the roadmap are read and checked before anything is written, so a bad file leaves nothing behind.
	String path = AppendFileName(p.repo, p.seed);
	if(!FileExists(path))
		return Fail(404, "seed not found: " + path);
	AtlasMap m;
	m.map = p.name;
	m.version = p.version;
	m.title = p.title;
	m.world.width = 2400;
	m.world.height = 1600;
	AtlasCheck check;
	if(!AtlasParseSeed(LoadFile(path), m, check))
		return Fail(400, Join(check.errors, "\n"));

	AtlasFamily f;
	Vector<AtlasEvent> events;
	if(p.roadmap.GetCount()) {
		String roadmap_path = AppendFileName(p.repo, p.roadmap);
		if(!FileExists(roadmap_path))
			return Fail(404, "roadmap not found: " + roadmap_path);
		if(!ReadRoadmap(LoadFile(roadmap_path), p.version, m, f, events, err))
			return Fail(400, "roadmap " + p.roadmap + ": " + err);
	}
	else {
		AtlasVersion first;
		first.vid = "v1";
		first.code = AtlasVersionCode(p.version);
		f.versions.Add(pick(first));
	}

	AtlasImportResult out = AtlasCreateMap(data_root, m, p.level);
	if(!out.Ok())
		return out;
	out.elements = m.elements.GetCount();
	out.roads = m.roads.GetCount();

	f.head_key = out.key;
	String rev;
	if(!AtlasCreateFamily(data_root, p.name, f, rev, err))
		return Fail(500, err);
	for(const AtlasEvent& e : events)
		if(!AtlasPutEvent(data_root, p.name, e, err))
			return Fail(500, err);
	out.revision = rev;
	out.events = events.GetCount();
	return out;
}

END_UPP_NAMESPACE
