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

// The roadmap file, format 2, written by scripts/roadmap.py in the BrownBear repository:
//
//   {"format": 2,
//    "versions": [{"label": "0.0.1", "requires": []}, ..., {"label": "1.0.0", "requires": ["done:Leaf"]}],
//    "rules": {"initial": [...], "atoms": [{"name", "text", "gate"}],
//              "actions": [{"name", "text", "requires", "effects"}],
//              "constraints": [{"id", "text", "when", "must", "from_vid", "to_vid"}]},
//    "events": [{"id", "at", "version", "type", "element" | "task", "name", "stage", "action", "text",
//                "task_type", "title", "target", "link"}]}
//
// Everything is checked before anything is written. The versions ascend and the last one is the project's version;
// every event names a known version, element and task; each event is valid; and the replay of the rules over the
// events reports no error and no warning. A roadmap that leaves a dependency out, or orders it wrongly, is refused
// with the replay's messages, so a family that exists has been replayed clean.
static bool ReadStrings(const ValueMap& m, const char *key, Vector<String>& out, String& error, const String& where)
{
	out.Clear();
	Value v = m[key];
	if(v.IsNull())
		return true;
	if(!v.Is<ValueArray>()) {
		error = where + ": " + key + " must be a list";
		return false;
	}
	ValueArray a = v;
	for(int i = 0; i < a.GetCount(); i++) {
		Value x = a[i];
		if(!x.Is<String>()) {
			error = where + ": " + key + " must hold strings";
			return false;
		}
		out.Add((String)x);
	}
	return true;
}

static int FindVersionLabel(const Vector<String>& labels, const String& label)
{
	for(int i = 0; i < labels.GetCount(); i++)
		if(labels[i] == label)
			return i;
	return -1;
}

static int FindHeadElement(const AtlasMap& head, const String& name)
{
	for(int i = 0; i < head.elements.GetCount(); i++)
		if(head.elements[i].name == name)
			return i;
	return -1;
}

static bool ReadRoadmap(const String& text, const String& head_label, const AtlasMap& head,
                        AtlasFamily& f, AtlasRules& rules, Vector<AtlasEvent>& events, String& error)
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
	Value format = top["format"];
	if(!IsNumber(format) || (int)format != 2) {
		error = "must be format 2 (regenerate it with scripts/roadmap.py)";
		return false;
	}

	// Versions: ascending, the contracts listed, and the last one is the project's version.
	Value vl = top["versions"];
	if(!vl.Is<ValueArray>() || ValueArray(vl).GetCount() == 0) {
		error = "versions must be a non-empty list";
		return false;
	}
	ValueArray vers = vl;
	Vector<String> labels;
	f.versions.Clear();
	int last_code = -1;
	for(int i = 0; i < vers.GetCount(); i++) {
		Value x = vers[i];
		if(!x.Is<ValueMap>()) {
			error = "each version must be an object";
			return false;
		}
		ValueMap vm = x;
		String label = JsonString(vm, "label");
		int code = AtlasVersionCode(label);
		if(code == ATLAS_VERSION_UNSET) {
			error = "version " + label + " must read a.b.c";
			return false;
		}
		if(code <= last_code) {
			error = "versions must ascend without repeats; " + label + " does not follow the one before";
			return false;
		}
		last_code = code;
		AtlasVersion v;
		v.vid = "v" + AsString(i + 1);
		v.code = code;
		if(!ReadStrings(vm, "requires", v.requires, error, "version " + label))
			return false;
		labels.Add(label);
		f.versions.Add(pick(v));
	}
	if(AtlasVersionCode(labels.Top()) != AtlasVersionCode(head_label)) {
		error = "the last version must be the project's version " + head_label;
		return false;
	}

	// Rules: the atoms, the actions and the constraints the replay checks.
	Value rv = top["rules"];
	if(!rv.Is<ValueMap>()) {
		error = "rules must be an object";
		return false;
	}
	ValueMap rm = rv;
	if(!ReadStrings(rm, "initial", rules.initial, error, "rules"))
		return false;
	rules.atoms.Clear();
	rules.actions.Clear();
	rules.constraints.Clear();
	Value av = rm["atoms"];
	Value acv = rm["actions"];
	Value cv = rm["constraints"];
	if(!av.Is<ValueArray>() || !acv.Is<ValueArray>() || !cv.Is<ValueArray>()) {
		error = "rules needs the lists atoms, actions and constraints";
		return false;
	}
	ValueArray atoms = av;
	for(int i = 0; i < atoms.GetCount(); i++) {
		ValueMap am = atoms[i];
		AtlasAtomDef d;
		d.name = JsonString(am, "name");
		d.text = JsonString(am, "text");
		Value g = am["gate"];
		d.gate = !g.IsNull() && (bool)g;
		rules.atoms.Add(pick(d));
	}
	ValueArray actions = acv;
	for(int i = 0; i < actions.GetCount(); i++) {
		ValueMap xm = actions[i];
		AtlasAction a;
		a.name = JsonString(xm, "name");
		a.text = JsonString(xm, "text");
		if(!ReadStrings(xm, "requires", a.requires, error, "action " + a.name) ||
		   !ReadStrings(xm, "effects", a.effects, error, "action " + a.name))
			return false;
		rules.actions.Add(pick(a));
	}
	ValueArray constraints = cv;
	for(int i = 0; i < constraints.GetCount(); i++) {
		ValueMap xm = constraints[i];
		AtlasConstraint c;
		c.id = JsonString(xm, "id");
		c.text = JsonString(xm, "text");
		c.from_vid = JsonString(xm, "from_vid");
		c.to_vid = JsonString(xm, "to_vid");
		if(!ReadStrings(xm, "when", c.when, error, "constraint " + c.id) ||
		   !ReadStrings(xm, "must", c.must, error, "constraint " + c.id))
			return false;
		rules.constraints.Add(pick(c));
	}

	// Events: each names a known version and element (or task); targets and links are resolved to ids here.
	Value evv = top["events"];
	if(!evv.Is<ValueArray>()) {
		error = "events must be a list";
		return false;
	}
	ValueArray evs = evv;
	events.Clear();
	for(int i = 0; i < evs.GetCount(); i++) {
		Value x = evs[i];
		if(!x.Is<ValueMap>()) {
			error = "each event must be an object";
			return false;
		}
		ValueMap em = x;
		String id = JsonString(em, "id");
		String version = JsonString(em, "version");
		int vi = FindVersionLabel(labels, version);
		if(vi < 0) {
			error = "event " + id + " names an unknown version " + version;
			return false;
		}
		AtlasEvent e;
		e.id = id;
		e.at = JsonString(em, "at");
		e.actor = "roadmap";
		e.vid = f.versions[vi].vid;
		String type = JsonString(em, "type");
		if(type == "task") {
			e.subject_kind = "task";
			e.subject_id = JsonString(em, "task");
			e.type = JsonString(em, "task_type");
			if(e.type == "proposed") {
				int ti = FindVersionLabel(labels, JsonString(em, "target"));
				if(ti < 0) {
					error = "task " + e.subject_id + " targets an unknown version";
					return false;
				}
				e.name = JsonString(em, "title");
				e.target = f.versions[ti].vid;
				String link = JsonString(em, "link");
				int hi = FindHeadElement(head, link);
				if(hi < 0) {
					error = "task " + e.subject_id + " links to " + link + ", which is not in the goal map";
					return false;
				}
				e.link = "element:" + head.elements[hi].id;
				e.text = JsonString(em, "text");
			}
		}
		else {
			String element = JsonString(em, "element");
			int hi = FindHeadElement(head, element);
			if(hi < 0) {
				error = "event " + id + " names " + element + ", which is not in the goal map";
				return false;
			}
			e.subject_kind = "element";
			e.subject_id = head.elements[hi].id;
			e.type = type;
			e.name = JsonString(em, "name");
			e.stage = JsonString(em, "stage");
			e.action = JsonString(em, "action");
			e.text = JsonString(em, "text");
		}
		events.Add(pick(e));
	}

	// Each event is valid on its own, and the replay over the whole family is clean: no error, no warning.
	for(const AtlasEvent& e : events) {
		AtlasCheck check;
		AtlasValidateEvent(e, f, head, check);
		if(!check.Ok()) {
			error = "event " + e.id + ": " + Join(check.errors, "; ");
			return false;
		}
	}
	AtlasSortEvents(events, f);
	Vector<AtlasRoadmapMessage> messages;
	Vector<Vector<String>> worlds;
	AtlasReplayRoadmap(head, f, rules, events, messages, worlds);
	Vector<String> findings;
	for(const AtlasRoadmapMessage& msg : messages)
		findings.Add(msg.severity + " " + msg.vid + " " + msg.rule + ": " + msg.text);
	if(findings.GetCount()) {
		error = AsString(findings.GetCount()) + " finding(s) from the replay; the first: " + findings[0];
		return false;
	}
	return true;
}

AtlasImportResult AtlasImportProject(const String& data_root, const AtlasProject& p)
{
	AtlasFamily existing;
	String frev, err;
	if(AtlasLoadFamily(data_root, p.name, existing, frev, err))
		return Fail(409, "the family " + p.name + " already exists");

	// The seed, the roadmap and its replay are checked before anything is written, so a bad file leaves nothing behind.
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
	AtlasRules rules;
	Vector<AtlasEvent> events;
	if(p.roadmap.GetCount()) {
		String roadmap_path = AppendFileName(p.repo, p.roadmap);
		if(!FileExists(roadmap_path))
			return Fail(404, "roadmap not found: " + roadmap_path);
		if(!ReadRoadmap(LoadFile(roadmap_path), p.version, m, f, rules, events, err))
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
	String rev, rules_rev;
	if(!AtlasCreateFamily(data_root, p.name, f, rev, err))
		return Fail(500, err);
	if(!AtlasSaveRules(data_root, p.name, rules, String(), rules_rev, err))
		return Fail(500, err);
	for(const AtlasEvent& e : events)
		if(!AtlasPutEvent(data_root, p.name, e, err))
			return Fail(500, err);
	out.revision = rev;
	out.events = events.GetCount();
	return out;
}

END_UPP_NAMESPACE
