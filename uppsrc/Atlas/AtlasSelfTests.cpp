#include "AtlasImport.h"
#include "AtlasDiagram.h"

NAMESPACE_UPP

// Atlas self-tests: the pure model, the seed parser, and one store round trip
// under a scratch directory. Printed as "ok"/"FAIL" lines, like the other
// ManagerSkylark self-test sections.

static int s_checks = 0;
static int s_failed = 0;

static void Check(bool ok, const char *what)
{
	s_checks++;
	if(!ok)
		s_failed++;
	Cout() << (ok ? "ok   " : "FAIL ") << what << "\n";
}

static AtlasElement Element(const char *id, const char *name, const char *kind, const char *parent,
                            int x, int y, int w, int h)
{
	AtlasElement e;
	e.id = id;
	e.name = name;
	e.kind = kind;
	e.parent = parent;
	e.status = "planned";
	e.x = x;
	e.y = y;
	e.w = w;
	e.h = h;
	return e;
}

static AtlasMap SmallMap()
{
	AtlasMap m;
	m.map = "t";
	m.version = "v0.0.1";
	m.title = "Test";
	m.elements.Add(Element("a", "Alpha", "block", "", 40, 40, 600, 400));
	m.elements.Add(Element("b", "Beta", "block", "a", 20, 40, 300, 200));
	m.elements.Add(Element("c", "Gamma", "place", "b", 20, 40, 120, 80));
	m.elements.Add(Element("d", "Delta", "sea", "", 700, 40, 300, 300));
	AtlasRoad r;
	r.id = "r_1";
	r.from = "a";
	r.to = "d";
	r.kind = "progress";
	m.roads.Add(pick(r));
	return m;
}

static bool HasError(const AtlasCheck& c, const char *part)
{
	for(const String& e : c.errors)
		if(e.Find(part) >= 0)
			return true;
	return false;
}

static void ModelChecks()
{
	Check(AtlasIdValid("e_main-2") && !AtlasIdValid("") && !AtlasIdValid("a b") && !AtlasIdValid("a.b"),
	      "ids allow [A-Za-z0-9_-] only");
	String longid;
	for(int i = 0; i < 65; i++)
		longid.Cat('a');
	Check(!AtlasIdValid(longid) && AtlasIdValid(longid.Left(64)), "ids are at most 64 characters");
	Check(AtlasKindValid("sea") && !AtlasKindValid("ocean"), "kinds are continent, block, place and sea");
	Check(AtlasStatusValid("not-ready") && !AtlasStatusValid("done"), "statuses are the seven listed states");
	Check(AtlasRoadKindValid("dependency") && !AtlasRoadKindValid("road"), "road kinds are progress, communication and dependency");

	AtlasMap m;
	m.map = "t";
	m.version = "v0.0.1";
	String first = AtlasUniqueElementId(m, "MainComputer");
	m.elements.Add(Element(first, "MainComputer", "place", "", 0, 0, 10, 10));
	Check(first == "e_maincomputer", "a name becomes an id slug");
	Check(AtlasUniqueElementId(m, "MainComputer") == "e_maincomputer_2", "a repeated name gets a numeric suffix");
	Check(AtlasUniqueElementId(m, "  ** ") == "e_element", "a name with no slug characters falls back to e_element");

	AtlasCheck ok;
	AtlasValidate(SmallMap(), ok);
	Check(ok.Ok() && ok.warnings.IsEmpty(), "a well-formed map validates with no errors or warnings");

	AtlasMap dup = SmallMap();
	dup.elements[1].id = "a";
	AtlasCheck c1;
	AtlasValidate(dup, c1);
	Check(HasError(c1, "duplicate element id"), "validation refuses duplicate element ids");

	AtlasMap orphan = SmallMap();
	orphan.elements[2].parent = "missing";
	AtlasCheck c2;
	AtlasValidate(orphan, c2);
	Check(HasError(c2, "missing parent"), "validation refuses a parent that does not exist");

	AtlasMap selfp = SmallMap();
	selfp.elements[0].parent = "a";
	AtlasCheck c3;
	AtlasValidate(selfp, c3);
	Check(HasError(c3, "its own parent"), "validation refuses an element as its own parent");

	AtlasMap cycle = SmallMap();
	cycle.elements[0].parent = "c";
	AtlasCheck c4;
	AtlasValidate(cycle, c4);
	Check(HasError(c4, "cycle"), "validation refuses a containment cycle");

	AtlasMap cross = SmallMap();
	cross.roads[0].to = "c";
	AtlasCheck c5;
	AtlasValidate(cross, c5);
	Check(HasError(c5, "same plane"), "validation refuses roads that cross planes");

	AtlasMap badroad = SmallMap();
	badroad.roads[0].kind = "bogus";
	badroad.roads[0].to = "nowhere";
	AtlasCheck c6;
	AtlasValidate(badroad, c6);
	Check(HasError(c6, "unknown kind") && HasError(c6, "unknown endpoint"), "validation refuses bad road kinds and endpoints");

	AtlasMap sized = SmallMap();
	sized.elements[2].w = 0;
	sized.elements[2].status = "done";
	AtlasCheck c7;
	AtlasValidate(sized, c7);
	Check(HasError(c7, "positive size") && HasError(c7, "unknown status"), "validation refuses zero sizes and unknown statuses");

	AtlasMap spill = SmallMap();
	spill.elements[2].x = 500;
	AtlasCheck c8;
	AtlasValidate(spill, c8);
	Check(c8.Ok() && c8.warnings.GetCount() == 1, "a child past its parent's box is a warning, not an error");

	AtlasMap chain = SmallMap();
	Check(AtlasDepth(chain, "a") == 0 && AtlasDepth(chain, "c") == 2, "containment depth counts the ancestors");
	Vector<String> path = AtlasPath(chain, "c");
	Check(path.GetCount() == 3 && path[0] == "a" && path[2] == "c", "the containment path runs root to element");
	Check(AtlasChildCount(chain, "a") == 1 && AtlasChildCount(chain, "c") == 0, "child counts are direct children only");
	Vector<String> world = AtlasPlaneElements(chain, "");
	Check(world.GetCount() == 2 && world[0] == "a" && world[1] == "d", "the world plane lists its top-level elements");
	Check(AtlasDepth(orphan, "c") == -1 && AtlasPath(orphan, "c").IsEmpty(),
	      "a broken parent chain has no depth and no path");
	Check(AtlasDepth(cycle, "a") == -1, "a cycle has no depth");
}

static void SeedChecks()
{
	const char *good =
		"# DEMO slice\n"
		"MainComputer | block | active\n"
		"  MainProgram | block\n"
		"    BusyLoop\n"
		"    VideoInput | place | hot\n"
		"  DesktopParser | place | not-ready | parser lane\n"
		"BusyLoop -> VideoInput: frames\n"
		"MainProgram => DesktopParser: needs\n"
		"VideoInput ~> BusyLoop\n";
	AtlasMap m;
	m.map = "t";
	m.version = "v0.0.1";
	AtlasCheck c;
	bool ok = AtlasParseSeed(good, m, c);
	Check(ok && c.Ok() && c.warnings.IsEmpty(), "a well-formed seed imports with no errors or warnings");
	Check(m.elements.GetCount() == 5 && m.roads.GetCount() == 3, "the seed creates every element and edge");
	{
		bool inside = true;
		for(const AtlasElement& e : m.elements)
			inside = inside && e.x + e.w <= m.world.width && e.y + e.h <= m.world.height;
		Check(inside && m.world.width > 0 && m.world.height > 0, "the seed world covers every laid-out element");
		AtlasMap small;
		small.map = "t";
		small.version = "v0.0.1";
		small.world.width = 10;
		small.world.height = 10;
		AtlasElement big;
		big.id = "e_big";
		big.x = 500;
		big.y = 300;
		big.w = 200;
		big.h = 100;
		small.elements.Add(pick(big));
		AtlasCoverWorld(small);
		Check(small.world.width == 740 && small.world.height == 440,
		      "a stored world smaller than its layout grows to cover it with the margin");
	}
	if(m.elements.GetCount() == 5) {
		int main = m.FindElement("e_maincomputer");
		int prog = m.FindElement("e_mainprogram");
		int busy = m.FindElement("e_busyloop");
		Check(main >= 0 && m.elements[main].kind == "block" && m.elements[main].status == "active",
		      "a named root with children is a block with its stated status");
		Check(prog >= 0 && m.elements[prog].parent == "e_maincomputer", "indentation makes the parent");
		Check(busy >= 0 && m.elements[busy].kind == "place" && m.elements[busy].status == "planned",
		      "a leaf defaults to place and planned");
		Check(busy >= 0 && AtlasDepth(m, "e_busyloop") == 2, "a grandchild sits two levels down");
		Check(m.FindElement("e_videoinput") >= 0 && m.elements[m.FindElement("e_videoinput")].status == "hot",
		      "a hot status survives the seed");
	}
	if(m.roads.GetCount() == 3) {
		Check(m.roads[0].kind == "progress" && m.roads[1].kind == "dependency" && m.roads[2].kind == "communication",
		      "the three arrows map to progress, dependency and communication");
		Check(m.roads[0].from == "e_busyloop" && m.roads[0].to == "e_videoinput" && m.roads[0].label == "frames",
		      "an edge joins the named elements with its label");
	}

	// Siblings sit on a grid with no overlap, inside their parent's box.
	bool overlap = false;
	for(int i = 0; i < m.elements.GetCount(); i++)
		for(int j = i + 1; j < m.elements.GetCount(); j++) {
			const AtlasElement& a = m.elements[i];
			const AtlasElement& b = m.elements[j];
			if(a.parent != b.parent)
				continue;
			if(a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h)
				overlap = true;
		}
	Check(!overlap, "siblings never overlap on the seed grid");

	struct Bad { const char *text; const char *what; };
	static const Bad bad[] = {
		{ "A\n\tB", "a tab in indentation" },
		{ "A\n   B", "an odd indentation" },
		{ "A\n    B", "an indentation that jumps two levels" },
		{ "  A", "an indented first element" },
		{ "A | spaceship", "an unknown kind" },
		{ "A | place | bogus", "an unknown status" },
		{ "A\nA", "a duplicate name" },
		{ "A\nA -> Z", "an edge to an unknown element" },
		{ "A\n-> A", "an edge with one endpoint" },
		{ "A | place | planned | note | extra", "too many fields" },
		{ "A:B", "a name with a colon" },
		{ "| place", "an empty name" },
	};
	for(const Bad& b : bad) {
		AtlasMap target;
		target.map = "t";
		target.version = "v0.0.1";
		AtlasCheck bc;
		Check(!AtlasParseSeed(b.text, target, bc) && !bc.errors.IsEmpty() && target.elements.IsEmpty(), b.what);
	}

	AtlasMap filled = SmallMap();
	AtlasCheck fc;
	Check(!AtlasParseSeed("X", filled, fc) && HasError(fc, "empty map") && filled.elements.GetCount() == 4,
	      "a seed refuses a map that already has elements and leaves it untouched");
}

static String ProjectJson(const char *name, const char *repo, const char *store, const char *level, const char *editors)
{
	return String("{\"projects\": [{\"name\": \"") + name + "\", \"repo\": \"" + repo + "\", \"store\": \"" + store
	     + "\", \"level\": \"" + level + "\", \"editors\": [" + editors + "]}]}";
}

static void ProjectChecks(const String& scratch)
{
	String root = AppendFileName(scratch, "projects");
	RealizeDirectory(root);
	String path = AppendFileName(root, "projects.json");
	String err;
	Vector<AtlasProject> all;
	Check(AtlasLoadProjects(AppendFileName(scratch, "no-projects-file"), all, err) && all.IsEmpty(),
	      "a data root without projects.json has no projects");

	SaveFile(path, ProjectJson("demo", "/srv/project", "atlas/demo", "private", "\"Alice\", \"bob\""));
	bool loaded = AtlasLoadProjects(root, all, err);
	Check(loaded && all.GetCount() == 1 && all[0].store == "atlas/demo" && all[0].level == "private"
	      && all[0].editors.GetCount() == 2 && all[0].editors[0] == "alice",
	      "a project loads with its store, and its editors in lower case");
	Check(AtlasProjectEditor(root, "demo", "ALICE") && !AtlasProjectEditor(root, "demo", "carol")
	      && !AtlasProjectEditor(root, "other", "alice") && !AtlasProjectEditor(root, "demo", ""),
	      "an editor is matched without case, and only for their own project");

	SaveFile(path, ProjectJson("demo", "/srv/project", "", "private", ""));
	Check(AtlasLoadProjects(root, all, err) && all.GetCount() == 1 && all[0].store.IsEmpty(),
	      "a project without a store is listed, and has no records");
	SaveFile(path, ProjectJson("demo", "/srv/project", "../secrets", "private", ""));
	Check(!AtlasLoadProjects(root, all, err) && err.Find("store") >= 0, "a store outside the repository is refused");
	SaveFile(path, ProjectJson("demo", "/srv/project", "/etc", "private", ""));
	Check(!AtlasLoadProjects(root, all, err) && err.Find("store") >= 0, "an absolute store path is refused");
	SaveFile(path, ProjectJson("demo", "relative/project", "atlas/demo", "private", ""));
	Check(!AtlasLoadProjects(root, all, err) && err.Find("repo") >= 0, "a repository path must be absolute");
	SaveFile(path, ProjectJson("demo", "/srv/project", "atlas/demo", "shared", ""));
	Check(!AtlasLoadProjects(root, all, err) && err.Find("level") >= 0, "a project level must be a known visibility");
	SaveFile(path, "{\"projects\": [{\"name\": \"demo\", \"repo\": \"/srv/project\", \"store\": \"\", \"level\": \"private\", "
	               "\"sources\": [{\"alias\": \"demo\", \"path\": \"/home/x/DEMO\"}]}]}");
	Check(AtlasLoadProjects(root, all, err) && all.GetCount() == 1 && all[0].sources.GetCount() == 1
	      && AtlasSourceRoot(root, "demo", "demo") == "/home/x/DEMO" && AtlasSourceRoot(root, "demo", "nope").IsEmpty()
	      && AtlasSourceRoot(root, "other", "demo").IsEmpty(),
	      "a project's source alias resolves to its absolute root; an unknown alias or project resolves to nothing");
	SaveFile(path, "{\"projects\": [{\"name\": \"demo\", \"repo\": \"/srv/project\", \"sources\": [{\"alias\": \"bad alias\", \"path\": \"/x\"}]}]}");
	Check(!AtlasLoadProjects(root, all, err) && err.Find("alias") >= 0, "a source alias must be a valid id");
	SaveFile(path, "{\"projects\": [{\"name\": \"demo\", \"repo\": \"/srv/project\", \"sources\": [{\"alias\": \"demo\", \"path\": \"DEMO\"}]}]}");
	Check(!AtlasLoadProjects(root, all, err) && err.Find("absolute") >= 0, "a source path must be absolute");
	SaveFile(path, "{\"projects\": [{\"name\": \"demo\", \"repo\": \"/srv/project\", \"sources\": [{\"alias\": \"demo\", \"path\": \"/a\"}, {\"alias\": \"demo\", \"path\": \"/b\"}]}]}");
	Check(!AtlasLoadProjects(root, all, err) && err.Find("listed twice") >= 0, "a source alias may be listed once");
	SaveFile(path, "[1, 2]");
	Check(!AtlasLoadProjects(root, all, err) && err.Find("not a JSON object") >= 0, "a projects file must be a JSON object");
}

// A project's records are read and written in its repository's store, not in the data root, so the repository is the
// source of truth and nothing is imported.
static void ProjectStoreChecks(const String& scratch)
{
	String data = AppendFileName(scratch, "routing-data");
	String repo = AppendFileName(scratch, "routing-repo");
	RealizeDirectory(data);
	RealizeDirectory(repo);
	SaveFile(AppendFileName(data, "projects.json"), ProjectJson("stored", repo, "atlas/stored", "private", "\"ed\""));
	String store = AppendFileName(repo, "atlas/stored");
	Check(AtlasStoreRoot(data, "stored") == store, "a project's family is stored in its repository's store");
	Check(AtlasStoreRoot(data, "unregistered") == data, "a family without a project is stored in the data root");
	Check(AtlasFamilyOfKey("stored") == "stored" && AtlasMapKey("stored") == "stored",
	      "a map key is its family name");
	Check(AtlasFamilyOfKey("stored-v1.0.0") == "stored" && AtlasFamilyOfKey("my-map-v0.0.2") == "my-map",
	      "an old key with a version still names its family");
	Check(AtlasFamilyOfKey("my-v-map") == "my-v-map", "a family name that contains -v is not cut");

	AtlasMap m;
	m.map = "stored";
	m.version = "v1.0.0";
	AtlasImportResult made = AtlasCreateMap(data, m, "private");
	Check(made.Ok() && FileExists(AppendFileName(store, "data/maps/stored.json"))
	      && !FileExists(AppendFileName(data, "data/maps/stored.json")),
	      "a map of a project is written to its repository's store, not to the data root");
	AtlasImportResult again = AtlasCreateMap(data, m, "private");
	Check(!again.Ok() && again.status == 409, "a second map for the same family is refused");

	AtlasFamily f;
	String rev, err;
	Check(AtlasCreateFamily(data, "stored", f, rev, err) && FileExists(AppendFileName(store, "data/families/stored.json"))
	      && !FileExists(AppendFileName(data, "data/families/stored.json")),
	      "a family of a project is written to its repository's store");

	AtlasRules rules;
	Check(AtlasSaveRules(data, "stored", rules, String(), rev, err) && FileExists(AppendFileName(store, "data/rules/stored.json")),
	      "the rules of a project are written to its repository's store");

	Vector<String> keys;
	AtlasListMapKeys(data, keys);
	int count = 0;
	for(const String& k : keys)
		if(k == "stored")
			count++;
	Check(count == 1, "the map list names the project's map once, from its store");
}

static void StoreChecks(const String& scratch)
{
	String root = AppendFileName(scratch, "store");
	RealizeDirectory(root);
	String key = AtlasMapKey("demo");
	Check(key == "demo", "a map record is keyed by its family, with no version");

	AtlasMap m = SmallMap();
	m.map = "demo";
	m.version = "v0.0.1";
	String rev, err;
	Check(AtlasSaveMap(root, key, m, String(), rev, err) && !rev.IsEmpty(), "a new map saves and returns a revision");

	AtlasMap back;
	String loaded_rev;
	bool loaded = AtlasLoadMap(root, key, back, loaded_rev, err);
	Check(loaded && loaded_rev == rev && back.title == "Test", "the saved map loads with its revision");
	Check(loaded && back.elements.GetCount() == 4 && back.roads.GetCount() == 1 && back.elements[1].parent == "a",
	      "elements, roads and containment survive the round trip");

	String next;
	bool stale = AtlasSaveMap(root, key, back, "999", next, err);
	Check(!stale && err.Find("revision conflict") >= 0, "a stale revision is refused as a conflict");

	String next2;
	Check(AtlasSaveMap(root, key, back, loaded_rev, next2, err) && next2 != loaded_rev,
	      "a current revision saves and advances the revision");

	AtlasMap none;
	String none_rev;
	Check(!AtlasLoadMap(root, "demo-v9.9.9", none, none_rev, err) && err.Find("map not found") >= 0,
	      "a missing map is reported as not found");
}

static AtlasVersion Ver(const char *vid, const char *label)
{
	AtlasVersion v;
	v.vid = vid;
	v.code = AtlasVersionCode(label);
	return v;
}

static String LabelOf(const AtlasFamily& f, const char *vid)
{
	int i = f.Find(vid);
	return i < 0 ? String("<none>") : AtlasVersionLabel(f.versions[i].code);
}

// Version codes: parsing, formatting and the bump rule with its refusals.
static void VersionChecks()
{
	Check(AtlasVersionCode("0.0.1") == 1 && AtlasVersionCode("v1.0.0") == 1000000,
	      "a label becomes a code: 0.0.1 is 1, v1.0.0 is 1000000");
	Check(AtlasVersionCode("1.2.3") == 1002003 && AtlasVersionLabel(1002003) == "1.2.3",
	      "1.2.3 is code 1002003 and back");
	Check(AtlasVersionCode("999.999.999") == ATLAS_VERSION_MAX && AtlasVersionValid(ATLAS_VERSION_MAX),
	      "999.999.999 is the largest code, 999999999");
	Check(AtlasVersionCode("1.2") == ATLAS_VERSION_UNSET && AtlasVersionCode("1.2.1000") == ATLAS_VERSION_UNSET
	      && AtlasVersionCode("1000.0.0") == ATLAS_VERSION_UNSET && AtlasVersionCode("1.x.0") == ATLAS_VERSION_UNSET,
	      "a label needs three parts, each 0..999");
	Check(!AtlasVersionValid(ATLAS_VERSION_UNSET) && AtlasVersionCode("0.0.0") == 0,
	      "-1 is no version, and 0.0.0 is a code");

	AtlasFamily f;
	f.head_key = "t-v0.0.1";
	String err;
	Check(AtlasInsertVersion(f, "0.0.1", "v1", err) && f.versions.GetCount() == 1, "the first version is added");
	Check(AtlasInsertVersion(f, "0.0.2", "v2", err) && LabelOf(f, "v2") == "0.0.2" && f.Head() == 1,
	      "a version after the head is added without bumping");

	Check(AtlasInsertVersion(f, "0.0.2", "v3", err) && LabelOf(f, "v3") == "0.0.2" && LabelOf(f, "v2") == "0.0.3",
	      "inserting 0.0.2 moves the old 0.0.2 to 0.0.3");
	Check(AtlasInsertVersion(f, "0.0.3", "v4", err) && LabelOf(f, "v4") == "0.0.3" && LabelOf(f, "v2") == "0.0.4"
	      && LabelOf(f, "v3") == "0.0.2", "inserting 0.0.3 moves 0.0.3 to 0.0.4 and leaves 0.0.2");
	Check(f.versions[0].vid == "v1" && f.versions[1].vid == "v3" && f.versions[2].vid == "v4" && f.versions[3].vid == "v2",
	      "versions stay sorted by label after an insert");

	Check(AtlasInsertVersion(f, "0.1.0", "v5", err) && LabelOf(f, "v5") == "0.1.0" && LabelOf(f, "v2") == "0.0.4",
	      "inserting 0.1.0 leaves the 0.0.x versions alone");

	// 0.0.0 is inserted first and bumps none of the versions after it.
	AtlasFamily z;
	z.head_key = "t-v0.0.0";
	Check(AtlasInsertVersion(z, "0.0.1", "v1", err) && AtlasInsertVersion(z, "0.0.0", "v0", err)
	      && LabelOf(z, "v0") == "0.0.0" && LabelOf(z, "v1") == "0.0.1" && z.versions[0].vid == "v0",
	      "0.0.0 is inserted before 0.0.1 and bumps nothing");
	Check(AtlasInsertVersion(f, "0.1.0", "v6", err) && LabelOf(f, "v6") == "0.1.0" && LabelOf(f, "v5") == "0.2.0",
	      "inserting 0.1.0 again moves 0.1.x to 0.2.x");
	Check(AtlasInsertVersion(f, "1.0.0", "v7", err) && f.Head() == 6 && f.versions[6].vid == "v7",
	      "1.0.0 becomes the head");

	AtlasFamily full;
	full.versions.Add(pick(Ver("top", "0.0.999")));
	Check(!AtlasInsertVersion(full, "0.0.999", "x", err) && err.Find("past 999") >= 0 && LabelOf(full, "top") == "0.0.999",
	      "a bump past 999 is refused and changes nothing");
	Check(!AtlasInsertVersion(f, "1.2", "z2", err) && !AtlasInsertVersion(f, "0.0.2", "v2", err),
	      "a short label and a repeated vid are refused");

	AtlasFamily r;
	r.versions.Add(pick(Ver("a", "0.0.1")));
	r.versions.Add(pick(Ver("b", "0.0.2")));
	Check(!AtlasRenameVersion(r, "b", "0.0.1", err) && LabelOf(r, "b") == "0.0.2",
	      "a rename onto another version's label is refused");
	Check(AtlasRenameVersion(r, "b", "0.0.5", err) && LabelOf(r, "b") == "0.0.5", "a rename to a free label applies");
}

static AtlasEvent Ev(const char *id, const char *vid, const char *kind, const char *subject, const char *type)
{
	AtlasEvent e;
	e.id = id;
	e.at = "2026-01-01T00:00:00Z";
	e.vid = vid;
	e.subject_kind = kind;
	e.subject_id = subject;
	e.type = type;
	return e;
}

static AtlasSubjectState StateOf(const Vector<AtlasSubjectState>& v, const char *kind, const char *id)
{
	for(const AtlasSubjectState& s : v)
		if(s.kind == kind && s.id == id)
			return s;
	return AtlasSubjectState();
}

// Event validation and the state replay over a two-version family.
static void EventChecks()
{
	String now = AtlasNowIso();
	Check(now.GetCount() == 20 && now[10] == 'T' && now[19] == 'Z' && now.Find('<') < 0,
	      "a timestamp reads YYYY-MM-DDTHH:MM:SSZ with no format leftovers");
	AtlasMap head = SmallMap();
	AtlasFamily f;
	f.head_key = "t-v0.0.2";
	f.versions.Add(pick(Ver("v1", "0.0.1")));
	f.versions.Add(pick(Ver("v2", "0.0.2")));

	AtlasCheck c;
	AtlasEvent good = Ev("ev-ok", "v2", "element", "b", "note");
	good.text = "two lines\nof detail";
	AtlasValidateEvent(good, f, head, c);
	Check(c.Ok(), "a note on a head element is valid");

	c = AtlasCheck();
	AtlasValidateEvent(Ev("ev-v", "vX", "element", "b", "note"), f, head, c);
	Check(HasError(c, "unknown version"), "an event at an unknown version is refused");

	c = AtlasCheck();
	AtlasValidateEvent(Ev("ev-s", "v2", "element", "nope", "note"), f, head, c);
	Check(HasError(c, "not in the head map"), "a note on an element absent from the head is refused");

	c = AtlasCheck();
	AtlasValidateEvent(Ev("ev-r", "v2", "element", "nope", "removed"), f, head, c);
	Check(c.Ok(), "a removed event may name an element the head no longer has");

	AtlasEvent bad_stage = Ev("ev-st", "v2", "element", "b", "stage");
	bad_stage.stage = "shipping";
	c = AtlasCheck();
	AtlasValidateEvent(bad_stage, f, head, c);
	Check(HasError(c, "unknown stage"), "an unknown stage is refused");

	AtlasEvent trial = Ev("ev-t", "v2", "element", "b", "stage");
	trial.stage = "trials";
	Check(AtlasStageRank("trials") == 3 && AtlasStageRank("merge_to_master") == 4 && AtlasStageRank("x") < 0
	      && AtlasStageValid(trial.stage), "stages rank in the pull-request order");

	c = AtlasCheck();
	AtlasEvent road_no_ends = Ev("ev-rc", "v2", "road", "r_1", "connected");
	AtlasValidateEvent(road_no_ends, f, head, c);
	Check(HasError(c, "both endpoints"), "a connected road event needs its endpoints in the snapshot");

	c = AtlasCheck();
	AtlasValidateEvent(Ev("ev-bad", "v2", "road", "r_1", "stage"), f, head, c);
	Check(HasError(c, "does not apply"), "a stage cannot be recorded on a road");

	// The replay: element b is created at v1; c is created at v1 and removed at v2; road r_1 is
	// connected at v1 and disconnected at v2; element a and the road are unscheduled up to the head.
	AtlasMap h = SmallMap();
	h.elements.Remove(h.FindElement("c"));
	Vector<AtlasEvent> events;
	AtlasEvent ev1 = Ev("ev-1", "v1", "element", "b", "created");
	ev1.name = "Beta";
	events.Add(pick(ev1));
	AtlasEvent ev2 = Ev("ev-2", "v2", "element", "b", "stage");
	ev2.stage = "trials";
	events.Add(pick(ev2));
	AtlasEvent ev3 = Ev("ev-3", "v2", "element", "b", "note");
	events.Add(pick(ev3));
	AtlasEvent ev4 = Ev("ev-4", "v1", "element", "c", "created");
	ev4.name = "Gamma";
	events.Add(pick(ev4));
	AtlasEvent ev5 = Ev("ev-5", "v2", "element", "c", "removed");
	ev5.name = "Gamma";
	events.Add(pick(ev5));
	AtlasEvent ev6 = Ev("ev-6", "v1", "road", "r_1", "connected");
	ev6.from = "a";
	ev6.to = "d";
	ev6.road_kind = "progress";
	events.Add(pick(ev6));
	AtlasEvent ev7 = Ev("ev-7", "v2", "road", "r_1", "disconnected");
	ev7.from = "a";
	ev7.to = "d";
	events.Add(pick(ev7));
	c = AtlasCheck();
	for(const AtlasEvent& e : events)
		AtlasValidateEvent(e, f, h, c);
	// Removal and disconnection need no head subject; the others were checked above against h.
	Check(c.Ok(), "a history with removal and disconnection validates against the head");

	Vector<AtlasSubjectState> at1, at2, bad;
	String err;
	Check(AtlasStateAt(h, f, events, "v1", at1, err) && AtlasStateAt(h, f, events, "v2", at2, err),
	      "the state replays at both versions");
	Check(!StateOf(at1, "element", "a").present && StateOf(at2, "element", "a").present,
	      "an unscheduled element exists at the head only");
	Check(!StateOf(at1, "element", "b").present == false && StateOf(at1, "element", "b").present,
	      "an element created at v1 exists at v1");
	Check(StateOf(at1, "element", "b").stage == "" && StateOf(at2, "element", "b").stage == "trials",
	      "a stage appears from the version it was set at");
	Check(StateOf(at1, "element", "b").notes == 0 && StateOf(at2, "element", "b").notes == 1,
	      "a note counts from its own version");
	Check(StateOf(at1, "element", "c").present && !StateOf(at2, "element", "c").present
	      && StateOf(at1, "element", "c").name == "Gamma",
	      "a removed element stays readable before its removal");
	Check(StateOf(at1, "road", "r_1").present && !StateOf(at2, "road", "r_1").present
	      && StateOf(at1, "road", "r_1").from == "a" && StateOf(at1, "road", "r_1").to == "d",
	      "a disconnected road keeps its endpoints before it went away");
	Check(!AtlasStateAt(h, f, events, "vX", bad, err) && err.Find("unknown version") >= 0,
	      "a state at an unknown version is refused");
}

// The version list and the event log, stored per family, round-trip through the store.
static void RoadmapStoreChecks(const String& scratch)
{
	String root = AppendFileName(scratch, "roadmap");
	RealizeDirectory(root);
	AtlasFamily f;
	f.head_key = "demo";
	f.versions.Add(pick(Ver("v1", "0.0.1")));
	String rev, next, err;
	Check(AtlasCreateFamily(root, "demo", f, rev, err) && !rev.IsEmpty(), "a new version list is created");
	Check(!AtlasCreateFamily(root, "demo", f, next, err) && err.Find("already exists") >= 0,
	      "a second version list with the same name is refused, not overwritten");

	AtlasFamily back;
	String loaded_rev;
	Check(AtlasLoadFamily(root, "demo", back, loaded_rev, err) && back.head_key == "demo"
	      && back.versions.GetCount() == 1 && loaded_rev == rev, "the version list loads with its revision");
	Check(!AtlasLoadFamily(root, "none", back, loaded_rev, err) && err.Find("initialise") >= 0,
	      "a missing version list says how to create it");

	AtlasEvent e = Ev("ev-store-1", "v1", "element", "e_a", "note");
	e.text = "stored";
	Check(AtlasPutEvent(root, "demo", e, err), "an event saves");
	Check(!AtlasPutEvent(root, "demo", e, err), "an event id cannot be written twice");
	AtlasEvent other = Ev("ev-store-2", "v1", "element", "e_b", "note");
	AtlasPutEvent(root, "other", other, err);
	Vector<AtlasEvent> got;
	Check(AtlasLoadEvents(root, "demo", got, err) && got.GetCount() == 1 && got[0].text == "stored",
	      "a family lists only its own events, with their text");
}

static bool HasMessage(const Vector<AtlasRoadmapMessage>& m, const char *severity, const char *rule)
{
	for(const AtlasRoadmapMessage& x : m)
		if(x.severity == severity && x.rule == rule)
			return true;
	return false;
}

static bool HasRuleOn(const Vector<AtlasRoadmapMessage>& m, const char *rule, const char *subject)
{
	for(const AtlasRoadmapMessage& x : m)
		if(x.rule == rule && x.subject_id == subject)
			return true;
	return false;
}

static bool HasAtom(const Vector<String>& world, const char *atom)
{
	for(const String& a : world)
		if(a == atom)
			return true;
	return false;
}

static void TwoVersions(AtlasFamily& f)
{
	f.head_key = "t-v0.0.2";
	f.versions.Add(pick(Ver("v1", "0.0.1")));
	f.versions.Add(pick(Ver("v2", "0.0.2")));
}

// The planning rules the checks use: a gate and a design atom, an action that needs the design and
// sets trial_run, and a constraint that a trial run needs the gate.
static void PlanRules(AtlasRules& r)
{
	AtlasAtomDef gate;
	gate.name = "gate_ready";
	gate.gate = true;
	r.atoms.Add(pick(gate));
	AtlasAtomDef design;
	design.name = "design_done";
	r.atoms.Add(pick(design));
	AtlasAction run;
	run.name = "run_trial";
	run.requires.Add("design_done");
	run.effects.Add("trial_run");
	r.actions.Add(pick(run));
	AtlasConstraint c;
	c.id = "trial_needs_gate";
	c.when.Add("trial_run");
	c.must.Add("gate_ready");
	r.constraints.Add(pick(c));
}

static AtlasMap PlanHead()
{
	AtlasMap h;
	h.map = "t";
	h.version = "v0.0.2";
	h.title = "Plan";
	h.elements.Add(Element("x", "Xray", "block", "", 0, 0, 100, 100));
	h.elements.Add(Element("y", "Yankee", "block", "", 200, 0, 100, 100));
	AtlasRoad dep;
	dep.id = "r_dep";
	dep.from = "x";
	dep.to = "y";
	dep.kind = "dependency";
	h.roads.Add(pick(dep));
	return h;
}

// Contracts, tasks, actions, constraints, stages, dependencies and atoms. Every check reports; none refuses.
static void RoadmapChecks()
{
	AtlasFamily f;
	TwoVersions(f);
	AtlasRules rules;
	PlanRules(rules);
	AtlasMap head = PlanHead();
	Vector<AtlasRoadmapMessage> out;
	Vector<Vector<String>> worlds;

	// A: a trial at v1 sets the gate, and task t7, accepted and done at v1, meets v2's contract on entry.
	f.versions[1].requires.Add("gate_ready");
	Vector<AtlasEvent> a;
	{
		AtlasEvent e = Ev("ta1", "v1", "task", "t7", "proposed");
		e.target = "v2";
		e.name = "Ship";
		a.Add(pick(e));
	}
	a.Add(pick(Ev("ta2", "v1", "task", "t7", "accepted")));
	a.Add(pick(Ev("ta3", "v1", "task", "t7", "done")));
	{
		AtlasEvent e = Ev("ta4", "v1", "element", "x", "note");
		e.effects.Add("gate_ready");
		a.Add(pick(e));
	}
	AtlasReplayRoadmap(head, f, rules, a, out, worlds);
	Check(!HasMessage(out, "error", "contract-gate") && !HasMessage(out, "error", "contract-task"),
	      "a gate set at an earlier version and a task done for the target meet the contract");
	Check(worlds.GetCount() == 2 && HasAtom(worlds[0], "gate_ready") && HasAtom(worlds[0], "task:t7:done"),
	      "the world after v1 holds the atoms its events set");
	Check(HasMessage(out, "warning", "unscheduled"), "an element with no event is reported as unscheduled");

	// B: the gate is set in v2 itself, so it is not there on entry; the accepted task is not done either.
	Vector<AtlasEvent> b;
	{
		AtlasEvent e = Ev("tb1", "v1", "task", "t7", "proposed");
		e.target = "v2";
		b.Add(pick(e));
	}
	b.Add(pick(Ev("tb2", "v1", "task", "t7", "accepted")));
	{
		AtlasEvent e = Ev("tb3", "v2", "element", "x", "note");
		e.effects.Add("gate_ready");
		b.Add(pick(e));
	}
	AtlasReplayRoadmap(head, f, rules, b, out, worlds);
	Check(HasMessage(out, "error", "contract-gate"), "a gate set in the same version does not satisfy that version's contract");
	Check(HasMessage(out, "error", "contract-task"), "an accepted task not done before its target is reported");

	// C: the action's requirement holds (design first), and the gate is set, so the constraint holds.
	Vector<AtlasEvent> c;
	{
		AtlasEvent e = Ev("tc1", "v1", "element", "x", "note");
		e.effects.Add("design_done");
		c.Add(pick(e));
	}
	{
		AtlasEvent e = Ev("tc2", "v1", "element", "x", "note");
		e.action = "run_trial";
		c.Add(pick(e));
	}
	{
		AtlasEvent e = Ev("tc3", "v1", "element", "x", "note");
		e.effects.Add("gate_ready");
		c.Add(pick(e));
	}
	AtlasReplayRoadmap(head, f, rules, c, out, worlds);
	Check(!HasMessage(out, "error", "requires") && !HasMessage(out, "error", "constraint"),
	      "an action with its requirement met and its constraint satisfied reports nothing");

	// D: the action runs before the design: its requirement fails, its effect still applies, and the constraint fails.
	Vector<AtlasEvent> d;
	{
		AtlasEvent e = Ev("td1", "v1", "element", "x", "note");
		e.action = "run_trial";
		d.Add(pick(e));
	}
	AtlasReplayRoadmap(head, f, rules, d, out, worlds);
	Check(HasMessage(out, "error", "requires"), "an action whose requirement is not yet true is reported");
	Check(HasMessage(out, "error", "constraint") && HasAtom(worlds[0], "trial_run"),
	      "a failed requirement still applies the action's effects and the constraint is checked");

	// E: y reaches implementation while its dependency x is not merged.
	Vector<AtlasEvent> e;
	e.Add(pick(Ev("te1", "v1", "element", "y", "stage")));
	e[0].stage = "implementation";
	AtlasReplayRoadmap(head, f, rules, e, out, worlds);
	Check(HasMessage(out, "warning", "dependency-order"), "a dependant reaching implementation before its dependency is merged warns");

	// F: a stage moved back is an error; a stage skipped is a warning.
	Vector<AtlasEvent> g;
	{
		AtlasEvent s1 = Ev("tf1", "v1", "element", "y", "stage");
		s1.stage = "planned";
		g.Add(pick(s1));
		AtlasEvent s2 = Ev("tf2", "v1", "element", "y", "stage");
		s2.stage = "trials";
		g.Add(pick(s2));
		AtlasEvent s3 = Ev("tf3", "v1", "element", "y", "stage");
		s3.stage = "planned";
		g.Add(pick(s3));
	}
	AtlasReplayRoadmap(head, f, rules, g, out, worlds);
	Check(HasMessage(out, "error", "stage-order") && HasMessage(out, "warning", "stage-order"),
	      "a stage moved back is an error and a skipped stage a warning");

	// G: dependency roads that loop are a cycle.
	AtlasMap loop = PlanHead();
	AtlasRoad back;
	back.id = "r_back";
	back.from = "y";
	back.to = "x";
	back.kind = "dependency";
	loop.roads.Add(pick(back));
	Vector<AtlasEvent> none;
	AtlasReplayRoadmap(loop, f, rules, none, out, worlds);
	Check(HasMessage(out, "error", "dependency-cycle"), "dependency roads that form a loop are an error");
	AtlasReplayRoadmap(head, f, rules, none, out, worlds);
	Check(!HasMessage(out, "error", "dependency-cycle"), "dependency roads without a loop are not");

	// H: a task done before it is accepted warns; a drop clears its accepted atom from that version on.
	Vector<AtlasEvent> h;
	{
		AtlasEvent p = Ev("th1", "v1", "task", "t9", "proposed");
		p.target = "v2";
		h.Add(pick(p));
	}
	h.Add(pick(Ev("th2", "v1", "task", "t9", "done")));
	h.Add(pick(Ev("th3", "v1", "task", "t9", "accepted")));
	h.Add(pick(Ev("th4", "v2", "task", "t9", "dropped")));
	AtlasReplayRoadmap(head, f, rules, h, out, worlds);
	Check(HasMessage(out, "warning", "task-state"), "a task done before it is accepted warns");
	Check(HasAtom(worlds[0], "task:t9:accepted") && !HasAtom(worlds[1], "task:t9:accepted"),
	      "a dropped task's accepted atom is cleared from the version it is dropped at");

	// I: an atom no rule declares warns, and does not stop the replay.
	AtlasRules loose;
	PlanRules(loose);
	AtlasConstraint stray;
	stray.id = "stray";
	stray.when.Add("design_done");
	stray.must.Add("mystery");
	loose.constraints.Add(pick(stray));
	AtlasReplayRoadmap(head, f, loose, none, out, worlds);
	Check(HasMessage(out, "warning", "undeclared-atom") && worlds.GetCount() == 2,
	      "an atom the rules never declare is a warning, and the replay still completes");
	bool trial_warned = false;
	for(const AtlasRoadmapMessage& m : out)
		trial_warned = trial_warned || m.text.Find("trial_run") >= 0;
	Check(!trial_warned, "an atom an action sets is declared by that action and is not reported");
}

// The demo roadmap, replayed in memory. Its findings are the ones it was written to show: 1.0.0 is not
// reached yet (two errors, its contract and its task), and one dependency is merged after a stage that
// needed it. Nothing else may be reported, or the demo history has drifted from the rules.
static bool StrictRule(const String& rule)
{
	return rule == "vacuous-version" || rule == "atom-never-required" || rule == "atom-never-set"
	    || rule == "big-bang" || rule == "task-done-no-note" || rule == "entry-undeclared"
	    || rule == "entry-conflict" || rule == "outcome-missing" || rule == "work-too-broad"
	    || rule == "entry-none-heavy" || rule == "outcome-never-used" || rule == "too-many-errors"
	    || rule == "gate-misplaced" || rule == "group-no-gate" || rule == "feature-without-task"
	    || rule == "gate-version-work" || rule == "ui-without-task" || rule == "ui-invalid" || rule == "ui-feature-unknown";
}

// The gate, zero-version and task rules: a map that breaks each one, then the repaired map.
static void GateAndTaskChecks()
{
	AtlasMap head;
	AtlasElement el;
	el.id = "e_test00";
	el.name = "Test00";
	head.elements.Add(pick(el));
	AtlasElement feat;
	feat.id = "e_feat";
	feat.name = "Feat";
	head.elements.Add(pick(feat));

	AtlasFamily f;
	f.head_key = "t-gates";
	Vector<AtlasEvent> events;
	String err;
	Check(AtlasInsertVersion(f, "0.0.0", "v0", err) && AtlasInsertVersion(f, "0.0.1", "v1", err)
	      && AtlasInsertVersion(f, "0.0.999", "v9", err), "a family with 0.0.0, 0.0.1 and the gate version 0.0.999");

	AtlasRules rules;
	AtlasAction gate;
	gate.name = "Test00";
	gate.requires.Add("done:Feat");
	gate.effects.Add("tested:0.0");
	rules.actions.Add(pick(gate));
	AtlasAction work;
	work.name = "Feat";
	work.entry_none = true;
	work.effects.Add("done:Feat");
	rules.actions.Add(pick(work));

	// Without any gate staged at 0.0.999, the group has no test gate.
	Vector<AtlasRoadmapMessage> out;
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "group-no-gate"), "a group whose x.999 version has no gate is an error");

	// The gate staged at 0.0.999 repairs it; staged in 0.0.1 it is misplaced.
	events.Add(pick(Ev("g1", "v9", "element", "e_test00", "stage")));
	events.Top().stage = "planned";
	events.Top().action = "Test00";
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(!HasMessage(out, "error", "group-no-gate") && !HasMessage(out, "error", "gate-misplaced"),
	      "a gate at 0.0.999 satisfies its group");
	events.Add(pick(Ev("g2", "v1", "element", "e_test00", "stage")));
	events.Top().stage = "planned";
	events.Top().action = "Test00";
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "gate-misplaced"), "a gate staged outside its x.999 version is misplaced");
	events.Trim(events.GetCount() - 1);

	// A feature with no task is an error; a task requirement, or work in 0.0.0, is not.
	f.versions[1].requires.Add("done:Feat");
	events.Add(pick(Ev("f1", "v1", "element", "e_feat", "stage")));
	events.Top().stage = "planned";
	events.Top().action = "Feat";
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasRuleOn(out, "feature-without-task", "Feat"), "a feature whose action has no task is an error");
	rules.actions[1].requires.Add("task:t:done");
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(!HasRuleOn(out, "feature-without-task", "Feat"), "a feature with a task requirement is not reported");
	rules.actions[1].requires.Clear();
	rules.actions[1].requires.Add("done:Other");  // a meta-feature: it needs other features only
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(!HasRuleOn(out, "feature-without-task", "Feat"), "a meta-feature needs no task");
	rules.actions[1].requires.Clear();
	rules.actions[1].entry_none = true;
	events.Top().vid = "v0";
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(!HasRuleOn(out, "feature-without-task", "Feat"), "a feature whose work is in 0.0.0 needs no task");
	events.Top().vid = "v1";

	// A task may be logged in 0.0.0: the tasks of the features that 0.0.0 prepares live there.
	AtlasEvent t = Ev("t1", "v0", "task", "t_x", "proposed");
	events.Add(pick(t));
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(!HasMessage(out, "error", "task-in-zero"), "a task logged in 0.0.0 is allowed");

	// The user-interface block: a user action has a known medium and exposes a feature a contract declares. Its element
	// is a work element, so its action needs a task.
	AtlasUiAction u;
	u.name = "run";
	u.text = "runs the gate";
	u.medium = "tty";
	u.feature = "Feat";
	head.elements[0].ui.Add(pick(u));
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "ui-invalid"), "a user action with an unknown medium is an error");
	head.elements[0].ui[0].medium = "cli";
	head.elements[0].ui[0].feature = "Nope";
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "ui-feature-unknown"), "a user action that exposes an undeclared feature is an error");
	Check(HasMessage(out, "error", "ui-without-task"), "a user-interface block whose action names no task is an error");
	rules.actions[0].requires.Add("task:t_ui:done");
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(!HasMessage(out, "error", "ui-without-task"), "a user-interface block with a task requirement is not reported");
	rules.actions[0].requires.Trim(rules.actions[0].requires.GetCount() - 1);
	head.elements[0].ui.Clear();

	// x.999 holds tests only: a task logged there, or work that sets no tested: literal, is an error.
	events.Add(pick(Ev("t9", "v9", "task", "t_x", "proposed")));
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "gate-version-work"), "a task logged in a x.999 version is an error");
	events.Trim(events.GetCount() - 1);
	events.Add(pick(Ev("w9", "v9", "element", "e_feat", "stage")));
	events.Top().stage = "planned";
	events.Top().action = "Feat";
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "gate-version-work"), "work that is not a test gate is an error at x.999");
	events.Trim(events.GetCount() - 1);
}

// The strictness rules: each fires on a map built to break it, and stays quiet on the map that repairs it.
// Three versions: v1 (empty contract), v2 (a middle version), v3 (the head).
static void StrictnessChecks()
{
	AtlasMap head = SmallMap();
	AtlasFamily f;
	TwoVersions(f);
	f.versions.Add(pick(Ver("v3", "0.0.3")));
	f.versions[0].requires.Clear();  // v1: an empty contract, so vacuous-version fires on it

	AtlasRules rules;
	AtlasAtomDef ready;
	ready.name = "ready";
	rules.atoms.Add(pick(ready));
	rules.initial.Add("ready");
	AtlasAction finish;
	finish.name = "finish_x";
	finish.requires.Add("ready");
	finish.effects.Add("done:x");
	rules.actions.Add(pick(finish));
	AtlasAction bare;
	bare.name = "bare";
	bare.effects.Add("bare_done");
	rules.actions.Add(pick(bare));

	AtlasEvent run = Ev("e1", "v1", "element", "x", "stage");
	run.stage = "planned";
	run.action = "finish_x";
	Vector<AtlasEvent> events;
	events.Add(pick(run));

	// Entry: an action with neither requires nor entry_none is an error; entry_none says it needs nothing.
	Vector<AtlasRoadmapMessage> out;
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "entry-undeclared"), "an action with no entry declared is an error");
	rules.actions[1].entry_none = true;
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(!HasMessage(out, "error", "entry-undeclared"), "an action that says entry_none is not an undeclared entry");
	Check(HasMessage(out, "warning", "entry-none-heavy"), "half the actions with entry_none is over the 20% limit");
	rules.strict.max_entry_none_share = 1.0;
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(!HasMessage(out, "warning", "entry-none-heavy"), "entry_none within the share limit is not reported");
	rules.strict.max_entry_none_share = 0.2;
	rules.actions[1].requires.Add("ready");
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "entry-conflict"), "an action with both entry_none and requires is a conflict");
	rules.actions[1].requires.Clear();
	rules.actions[1].entry_none = false;

	// Outcome: an action with no effects is an error; more than max_outcomes effects is too broad.
	AtlasAction empty;
	empty.name = "empty";
	empty.requires.Add("ready");
	rules.actions.Add(pick(empty));
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "outcome-missing"), "an action with no outcome is an error");
	rules.actions.Pop();
	rules.actions[0].effects.Add("done:y");
	rules.actions[0].effects.Add("done:z");  // finish_x now has three outcomes: done:x, done:y, done:z
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(!HasMessage(out, "error", "work-too-broad"), "three outcomes is within the default limit");
	rules.actions[0].effects.Add("done:w");  // four
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "work-too-broad"), "four outcomes is over the default limit of three");
	rules.actions[0].effects.Trim(1);

	// Outcome never used: done:x is read by the contracts below; an outcome nothing reads is reported on its action.
	// ("bare" sets bare_done, which nothing reads, so it is reported too; these checks look at finish_x and stray only.)
	f.versions[1].requires.Add("done:x");
	f.versions[2].requires.Add("done:x");
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(!HasRuleOn(out, "outcome-never-used", "finish_x"), "an outcome that a contract reads is not reported");
	AtlasAction stray;
	stray.name = "stray";
	stray.requires.Add("ready");
	stray.effects.Add("unread_x");
	rules.actions.Add(pick(stray));
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasRuleOn(out, "outcome-never-used", "stray"), "an outcome nothing reads is reported on its action");
	rules.actions.Pop();

	// A version with an empty contract is vacuous.
	Check(HasMessage(out, "warning", "vacuous-version"), "a version with an empty contract is vacuous");

	// Work element with children: "a" groups "b" in SmallMap, so an action on "a" is too broad; on "d" (no children) it is not.
	AtlasEvent on_group = Ev("e5", "v1", "element", "a", "stage");
	on_group.stage = "planned";
	on_group.action = "finish_x";
	events.Add(pick(on_group));
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "error", "work-too-broad"), "an element with child elements that runs an action is too broad");
	events.Pop();

	// Big bang: one merge over a limit of zero.
	AtlasEvent merge = Ev("e3", "v3", "element", "a", "stage");
	merge.stage = "merge_to_master";
	events.Add(pick(merge));
	rules.strict.max_step_elements = 0;
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "warning", "big-bang"), "a version that merges more elements than the limit is a big bang");
	rules.strict.max_step_elements = 5;
	events.Pop();

	// Tasks: a done task without a note.
	AtlasEvent done = Ev("e4", "v3", "task", "t1", "done");
	events.Add(pick(done));
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "warning", "task-done-no-note"), "a task done without a result note is reported");
	events.Pop();

	// Atoms: a gate that nothing requires is decoration.
	AtlasAtomDef orphan;
	orphan.name = "orphan_gate";
	orphan.gate = true;
	rules.atoms.Add(pick(orphan));
	out.Clear();
	AtlasStrictness(head, f, rules, events, out);
	Check(HasMessage(out, "warning", "atom-never-required"), "a gate that nothing requires is reported");
	rules.atoms.Pop();

	// Too many errors: with a limit of one, the second error stops the report and says so.
	AtlasAction also_bare;
	also_bare.name = "also_bare";
	rules.actions.Add(pick(also_bare));
	rules.strict.max_errors = 1;
	Vector<Vector<String>> worlds;
	out.Clear();
	AtlasReplayRoadmap(head, f, rules, events, out, worlds);
	int errors = 0;
	for(const AtlasRoadmapMessage& m : out)
		if(m.severity == "error")
			errors++;
	Check(errors == 2 && HasMessage(out, "error", "too-many-errors"),
	      "past max_errors the report keeps one more error, too-many-errors, and drops the rest");
	rules.strict.max_errors = 20;
}

// The warning ledger: a warning without a comment is an error at the head; a comment clears it; a comment whose
// finding is gone is stale. The ids are stable: the same finding gives the same id.
static void LedgerChecks()
{
	AtlasMap head = SmallMap();
	AtlasFamily f;
	TwoVersions(f);
	f.versions[0].requires.Clear();  // v1 is empty, so vacuous-version is a warning
	AtlasRules rules;
	Vector<AtlasEvent> none;
	Vector<Vector<String>> worlds;

	Vector<AtlasRoadmapMessage> findings;
	AtlasRoadmapFindings(head, f, rules, none, findings, worlds);
	Vector<AtlasRoadmapMessage> warned;
	for(const AtlasRoadmapMessage& m : findings)
		if(m.severity == "warning")
			warned.Add(pick(m));
	Check(warned.GetCount() > 0, "the ledger fixture has a warning to comment on");

	String id = AtlasWarningId(warned[0]);
	Check(id.GetCount() == 16, "a warning id is 16 hex digits");
	Check(AtlasWarningId(warned[0]) == id, "the same finding always gets the same id");
	AtlasRoadmapMessage moved = warned[0];
	moved.vid = "v9";
	Check(AtlasWarningId(moved) != id, "a finding at another version gets another id");

	Vector<AtlasWarningAck> acks;
	Vector<AtlasRoadmapMessage> out;
	AtlasRoadmapReport(head, f, rules, none, acks, out, worlds);
	int open = 0;
	bool at_head = true;
	for(const AtlasRoadmapMessage& m : out)
		if(m.rule == "unacknowledged-warning") {
			open++;
			at_head = at_head && m.vid == f.versions[f.Head()].vid && m.severity == "error";
		}
	Check(open == warned.GetCount(), "each warning without a comment is one unacknowledged-warning error");
	Check(at_head, "the unacknowledged-warning errors sit at the head");
	rules.strict.max_errors = 0;  // with no plan errors, a cut-off of zero must still leave every ledger error
	out.Clear();
	AtlasRoadmapReport(head, f, rules, none, acks, out, worlds);
	int open_at_cut = 0;
	for(const AtlasRoadmapMessage& m : out)
		if(m.rule == "unacknowledged-warning")
			open_at_cut++;
	Check(open_at_cut == warned.GetCount(), "the cut-off counts plan errors only; every open warning stays in the ledger");
	rules.strict.max_errors = 20;

	for(const AtlasRoadmapMessage& m : warned) {
		AtlasWarningAck a;
		a.warning_id = AtlasWarningId(m);
		a.comment = "checked by hand";
		acks.Add(pick(a));
	}
	out.Clear();
	AtlasRoadmapReport(head, f, rules, none, acks, out, worlds);
	Check(!HasMessage(out, "error", "unacknowledged-warning"), "a comment on every warning clears the ledger");

	AtlasWarningAck old;
	old.warning_id = "0000000000000000";
	acks.Add(pick(old));
	Check(AtlasStaleAcks(findings, acks).GetCount() == 1, "a comment on a finding that is gone is stale");
	Vector<AtlasWarningAck> current;
	for(int i = 0; i < acks.GetCount() - 1; i++)
		current.Add(acks[i]);
	Check(AtlasStaleAcks(findings, current).GetCount() == 0, "comments on current findings are not stale");
}

static void DemoChecks()
{
	AtlasFamily f;
	AtlasMap head;
	AtlasRules rules;
	Vector<AtlasEvent> events;
	AtlasDemoModel(f, head, rules, events);
	Vector<AtlasRoadmapMessage> out;
	Vector<Vector<String>> worlds;
	AtlasReplayRoadmap(head, f, rules, events, out, worlds);

	// The strictness rules are checked on their own below; this check is about the replay.
	int errors = 0, at_head = 0, other = 0;
	for(const AtlasRoadmapMessage& m : out) {
		if(m.severity == "error" && !StrictRule(m.rule)) {
			errors++;
			if(m.vid == "v6")
				at_head++;
		}
		if(m.rule == "stage-order" || m.rule == "unscheduled" || m.rule == "undeclared-atom" || m.rule == "orphan")
			other++;
	}
	Check(errors == 2 && at_head == 2, "the demo reports exactly the two errors that keep 1.0.0 from being reached");
	Check(HasMessage(out, "error", "contract-gate") && HasMessage(out, "error", "contract-task"),
	      "the demo's 1.0.0 errors are its contract gate and its task");
	Check(HasMessage(out, "warning", "dependency-order"), "the demo shows a dependency merged after its dependant");
	Check(other == 0, "the demo has no stage, scheduling, atom or orphan findings");
	Check(worlds.GetCount() == 6 && HasAtom(worlds[1], "gate_camera_calibrated") && HasAtom(worlds[2], "gate_detector_trained"),
	      "the demo's gates hold where its contracts need them");
}

// Markdown for notes: the text is escaped before any syntax applies, so hostile input stays text.
static void MarkdownChecks()
{
	String script = AtlasMarkdown("<script>alert(1)</script>");
	Check(script.Find("<script") < 0 && script.Find("&lt;script&gt;") >= 0, "markdown escapes script tags");
	String js = AtlasMarkdown("[click](javascript:alert(1))");
	Check(js.Find("<a ") < 0 && js.Find("javascript:") >= 0 && js.Find("href") < 0,
	      "markdown leaves a javascript: link as plain text");
	String link = AtlasMarkdown("[ok](https://example.com/a?b=1&c=2)");
	Check(link.Find("href=\"https://example.com/a?b=1&amp;c=2\"") >= 0 && link.Find("rel=\"noopener nofollow\"") >= 0,
	      "markdown turns an https link into a safe anchor");
	String quote = AtlasMarkdown("[x](https://a.example/\"onmouseover=alert(1))");
	Check(quote.Find("\"onmouseover") < 0, "markdown does not let a quote break out of a link attribute");
	String emph = AtlasMarkdown("**bold** and *em* and `c<d`");
	Check(emph.Find("<strong>bold</strong>") >= 0 && emph.Find("<em>em</em>") >= 0 && emph.Find("<code>c&lt;d</code>") >= 0,
	      "markdown renders strong, emphasis and code");
	Check(AtlasMarkdown("- one\n- two") == "<ul><li>one</li><li>two</li></ul>", "markdown renders a list");
	Check(AtlasMarkdown("# Title") == "<h3>Title</h3>", "markdown renders a heading");
	Check(AtlasMarkdown("```\n<b>x</b>\n```") == "<pre><code>&lt;b&gt;x&lt;/b&gt;\n</code></pre>",
	      "markdown keeps fenced code literal");
	Check(AtlasMarkdown("a\nb") == "<p>a<br>b</p>", "markdown joins lines of a paragraph");
	Check(AtlasMarkdown("2 * 3 = 6") == "<p>2 * 3 = 6</p>", "markdown leaves an unpaired asterisk alone");
	Check(AtlasEventTypeValid("task", "assigned") && AtlasEventTypeValid("task", "note"),
	      "tasks accept assignment and note events");
}

// What is next: the demo plans from 0.1.0 with 1.0.0 blocked; a small family checks the steps it suggests.
static void WhatsNextChecks()
{
	AtlasFamily f;
	AtlasMap head;
	AtlasRules rules;
	Vector<AtlasEvent> events;
	AtlasDemoModel(f, head, rules, events);
	AtlasNext next;
	String err;
	Check(AtlasWhatsNext(head, f, rules, events, String(), next, err), "what is next plans the demo");
	Check(next.from == "v5", "the demo plan starts at 0.1.0, before 1.0.0 is blocked");
	Check(next.versions.GetCount() == 1 && next.versions[0].vid == "v6" && next.versions[0].state == "blocked",
	      "1.0.0 is the only version ahead, and it is blocked");
	bool gate_how = false, task_open = false;
	if(next.versions.GetCount())
		for(const AtlasNextCondition& c : next.versions[0].conditions) {
			if(!c.met && c.kind == "gate" && c.how.GetCount())
				gate_how = true;
			if(!c.met && c.kind == "task")
				task_open = true;
		}
	Check(gate_how, "the unmet gate of 1.0.0 names the action that sets it");
	Check(task_open && next.todo.GetCount() > 0, "1.0.0 waits on an accepted task, which is in the open list");

	AtlasNext at_head;
	Check(AtlasWhatsNext(head, f, rules, events, "v6", at_head, err) && at_head.versions.IsEmpty() && at_head.todo.IsEmpty(),
	      "from the head nothing is ahead");
	AtlasNext unknown;
	Check(!AtlasWhatsNext(head, f, rules, events, "v9", unknown, err), "an unknown version is refused");

	AtlasFamily g;
	g.head_key = "gen-v0.0.3";
	AtlasVersion v1, v2, v3;
	v1.vid = "v1"; v1.code = 1;
	v2.vid = "v2"; v2.code = 2; v2.requires.Add("ready");
	v3.vid = "v3"; v3.code = 3;
	g.versions.Add(pick(v1));
	g.versions.Add(pick(v2));
	g.versions.Add(pick(v3));
	AtlasMap gh;
	gh.map = "gen";
	gh.version = "v3";
	AtlasRules gr;
	AtlasAtomDef ready;
	ready.name = "ready";
	ready.text = "the box is ready";
	ready.gate = true;
	gr.atoms.Add(pick(ready));
	AtlasAction make;
	make.name = "make_ready";
	make.effects.Add("ready");
	make.requires.Add("!broken");
	gr.actions.Add(pick(make));
	Vector<AtlasEvent> none;

	AtlasNext s;
	Check(AtlasWhatsNext(gh, g, gr, none, String(), s, err), "what is next plans a synthetic family");
	Check(s.from == "v1" && s.versions.GetCount() == 2, "the synthetic plan starts at v1 and sees two versions ahead");
	if(s.versions.GetCount() == 2) {
		Check(s.versions[0].state == "blocked" && s.versions[1].state == "ready",
		      "v2 is blocked on ready, and v3 has no entry condition");
		Check(s.versions[0].conditions.GetCount() == 1 && s.versions[0].conditions[0].kind == "gate"
		      && s.versions[0].conditions[0].how.GetCount() == 1
		      && s.versions[0].conditions[0].how[0] == "run make_ready (its requirements hold)",
		      "the gate's step is the action that sets it, with its requirements met");
	}
	AtlasNext from_v2;
	Check(AtlasWhatsNext(gh, g, gr, none, "v2", from_v2, err) && from_v2.versions.GetCount() == 1
	      && from_v2.versions[0].state == "ready", "from v2 only v3 is ahead, and it is ready");
}

// The file records: the line helpers, the patch series, and the validator's refusals.
static Vector<String> Lines3()
{
	Vector<String> v;
	v.Add("l1");
	v.Add("l2");
	v.Add("l3");
	return v;
}

static String Joined(const Vector<String>& v)
{
	String s;
	for(int i = 0; i < v.GetCount(); i++) {
		if(i)
			s << "|";
		s << v[i];
	}
	return s;
}

static AtlasFileEdit Edit(const char *vid, const char *element, int start, int end, const char *text = "")
{
	AtlasFileEdit e;
	e.vid = vid;
	e.element = element;
	e.start = start;
	e.end = end;
	e.text = text;
	return e;
}

static AtlasFile GoodFile()
{
	AtlasFile f;
	f.id = "f1";
	f.source = "demo";
	f.path = "v1/src/x.cpp";
	f.kind = "code";
	AtlasFileSpan sp;
	sp.id = "s1";
	sp.element = "a";
	sp.role = "evidence";
	sp.start = 1;
	sp.end = 2;
	f.spans.Add(pick(sp));
	f.edits.Add(Edit("v1", "a", 0, -1));
	f.edits.Add(Edit("v2", "b", 1, 1, "new"));
	return f;
}

static void FileChecks()
{
	Check(AtlasSplitLines("a\nb\n").GetCount() == 2 && AtlasSplitLines("").IsEmpty() && AtlasSplitLines("a\n\nb").GetCount() == 3,
	      "lines split on newlines; an empty text is no lines, and a trailing newline adds none");

	Vector<String> t;
	String err;
	t = Lines3();
	Check(AtlasApplyEdit(t, Edit("v2", "b", 2, 2, "X\nY"), err) && Joined(t) == "l1|X|Y|l3",
	      "a replace puts the new lines where the old ones were");
	t = Lines3();
	Check(AtlasApplyEdit(t, Edit("v2", "b", 2, 1, "N"), err) && Joined(t) == "l1|N|l2|l3",
	      "an insert (end = start - 1) goes before the line");
	t = Lines3();
	Check(AtlasApplyEdit(t, Edit("v2", "b", 1, 2, ""), err) && Joined(t) == "l3",
	      "a delete (empty text) removes the lines");
	t = Lines3();
	Check(AtlasApplyEdit(t, Edit("v1", "a", 0, -1), err) && Joined(t) == "l1|l2|l3",
	      "a marker edit changes nothing");
	t = Lines3();
	Check(!AtlasApplyEdit(t, Edit("v2", "b", 9, 9, "x"), err) && Joined(t) == "l1|l2|l3" && err.Find("3 lines") >= 0,
	      "an edit past the end is refused, and the text is left as it was");

	Vector<String> out;
	Vector<AtlasFileEdit> series;
	series.Add(Edit("v1", "a", 1, 1, "first"));
	series.Add(Edit("v2", "b", 3, 3, "last"));
	Check(AtlasApplyEdits(Lines3(), series, out, err) && Joined(out) == "first|l2|last",
	      "the series applies in order, so each version sees the text its predecessors left");

	AtlasMap head = SmallMap();
	AtlasFamily fam;
	fam.head_key = "t";
	fam.versions.Add(pick(Ver("v1", "0.0.1")));
	fam.versions.Add(pick(Ver("v2", "1.0.0")));
	AtlasCheck c;
	AtlasFile good = GoodFile();
	Check(AtlasValidateFile(good, head, fam, Lines3(), true, c) && c.Ok(),
	      "a well-formed file validates, and its series applies to the base");

	// A declaration: a class or function span carries one signature line and no body; no text lines are needed.
	AtlasFile decl = GoodFile();
	AtlasFileSpan ds;
	ds.id = "d1";
	ds.element = "a";
	ds.role = "destination";
	ds.kind = "function";
	ds.signature = "static int Step(int n);";
	decl.spans.Add(pick(ds));
	Check(AtlasValidateFile(decl, head, fam, Lines3(), true, c) && c.Ok(),
	      "a function declaration with a one-line signature validates, with no text lines");
	AtlasFile bad_kind = GoodFile();
	bad_kind.spans[0].kind = "widget";
	Check(!AtlasValidateFile(bad_kind, head, fam, Lines3(), true, c) && !c.Ok(), "an unknown span kind is refused");
	AtlasFile body = GoodFile();
	body.spans[0].kind = "function";
	body.spans[0].signature = "int Step(int n) { return n; }";
	Check(!AtlasValidateFile(body, head, fam, Lines3(), true, c) && !c.Ok(), "a signature with a body is refused");
	AtlasFile unkinded = GoodFile();
	unkinded.spans[0].signature = "int Step(int n);";
	Check(!AtlasValidateFile(unkinded, head, fam, Lines3(), true, c) && !c.Ok(),
	      "a signature with no class or function kind is refused");

	AtlasFile f = GoodFile();
	f.path = "../secrets.cpp";
	c = AtlasCheck();
	Check(!AtlasValidateFile(f, head, fam, Lines3(), true, c) && !c.Ok(), "a path with '..' is refused");
	f = GoodFile();
	f.path = "/etc/passwd";
	c = AtlasCheck();
	Check(!AtlasValidateFile(f, head, fam, Lines3(), true, c) && !c.Ok(), "an absolute path is refused");
	f = GoodFile();
	f.kind = "other";
	c = AtlasCheck();
	Check(!AtlasValidateFile(f, head, fam, Lines3(), true, c) && !c.Ok(), "an unknown kind is refused");
	f = GoodFile();
	f.spans[0].element = "missing";
	c = AtlasCheck();
	Check(!AtlasValidateFile(f, head, fam, Lines3(), true, c) && !c.Ok(), "a span on an element not in the goal map is refused");
	f = GoodFile();
	f.edits[1].vid = "v9";
	c = AtlasCheck();
	Check(!AtlasValidateFile(f, head, fam, Lines3(), true, c) && !c.Ok(), "an edit of a version not in the family is refused");
	f = GoodFile();
	f.edits.Insert(0, Edit("v2", "b", 1, 1, "x"));
	f.edits[0].vid = "v2";
	f.edits.Add(Edit("v1", "a", 0, -1));
	c = AtlasCheck();
	Check(!AtlasValidateFile(f, head, fam, Lines3(), true, c) && !c.Ok(), "edits out of version order are refused");
	f = GoodFile();
	AtlasFileSpan again = f.spans[0];
	again.id = "s2";
	again.start = 2;
	again.end = 3;
	f.spans.Add(pick(again));
	c = AtlasCheck();
	Check(!AtlasValidateFile(f, head, fam, Lines3(), true, c) && !c.Ok(), "overlapping spans are refused");
	f = GoodFile();
	f.edits[1] = Edit("v2", "b", 9, 9, "x");
	c = AtlasCheck();
	Check(!AtlasValidateFile(f, head, fam, Lines3(), true, c) && !c.Ok(),
	      "a series that does not apply to the base is refused");
	f = GoodFile();
	f.kind = "artifact";
	f.edits.Clear();
	c = AtlasCheck();
	Check(AtlasValidateFile(f, head, fam, Vector<String>(), false, c) && c.Ok(),
	      "an artifact with no text validates without a base");
}

// A file record is written to the project's store, and reads and lists back there.
static void FileStoreChecks(const String& scratch)
{
	String data = AppendFileName(scratch, "routing-data");
	String repo = AppendFileName(scratch, "routing-repo");
	String store = AppendFileName(repo, "atlas/stored");
	AtlasFile f = GoodFile();
	f.id = "f9";
	String rev, err;
	Check(AtlasSaveFile(data, "stored", f, String(), rev, err) && !rev.IsEmpty()
	      && FileExists(AppendFileName(store, "data/files/stored.f9.json"))
	      && !FileExists(AppendFileName(data, "data/files/stored.f9.json")),
	      "a file record of a project is written to its repository's store, not to the data root");

	AtlasFile back;
	String back_rev;
	Check(AtlasLoadFile(data, "stored", "f9", back, back_rev, err) && back.path == "v1/src/x.cpp"
	      && back.spans.GetCount() == 1 && back.edits.GetCount() == 2 && back_rev == rev,
	      "a file record loads back with its spans, its edits and its revision");

	Vector<AtlasFile> all;
	Check(AtlasListFiles(data, "stored", all, err) && all.GetCount() == 1 && all[0].id == "f9",
	      "the file list of a family names its records");

	AtlasFile stale = GoodFile();
	stale.id = "f9";
	String new_rev;
	Check(!AtlasSaveFile(data, "stored", stale, "stale", new_rev, err) && err.Find("revision") >= 0,
	      "a save with a stale revision is refused, and the record stays as it was");
}

#ifndef _WIN32
#include <unistd.h>
#endif

// The source reader: a code file inside the root reads; a path that leaves it, a dot-entry, a file that is not code,
// a symlink out of the root and a file over the size cap are refused, and the text at a version is its base with the
// edits up to that version.
// A comment on a warning is stored with its family, reads back, and is removed by its id.
static void PkgChecks()
{
	Check(AtlasPkgValid("") && AtlasPkgValid("DeepCfrTrainer") && AtlasPkgValid("Omaha4Trainer") && AtlasPkgValid("Datasets"),
	      "CapitalCase package names and the empty package are valid");
	Check(!AtlasPkgValid("deepCfr") && !AtlasPkgValid("Deep-Cfr") && !AtlasPkgValid("Deep Cfr") && !AtlasPkgValid("_Deep"),
	      "a package name that is not CapitalCase is refused");
	AtlasMap m = SmallMap();
	m.elements[0].pkg = "lowercase";
	AtlasCheck c;
	AtlasValidate(m, c);
	Check(!c.Ok() && c.errors.GetCount() == 1 && c.errors[0].Find("CapitalCase") >= 0,
	      "an element with a bad package name fails the map check");
}

static void DiagramChecks()
{
	AtlasFamily f;
	AtlasVersion v1, v2, v3;
	v1.vid = "v1"; v1.code = 1;
	v2.vid = "v2"; v2.code = 1000; v2.requires.Add("done:Alpha");
	v3.vid = "v3"; v3.code = 1001; v3.requires.Add("artifact:x");
	f.versions.Add(pick(v1));
	f.versions.Add(pick(v2));
	f.versions.Add(pick(v3));

	AtlasMap head = SmallMap();
	AtlasRules rules;
	AtlasAction a1;
	a1.name = "Alpha"; a1.text = "build alpha"; a1.entry_none = true; a1.effects.Add("done:Alpha");
	rules.actions.Add(pick(a1));
	AtlasAction b1;
	b1.name = "Beta"; b1.text = "build beta"; b1.requires.Add("done:Alpha"); b1.requires.Add("artifact:x");
	b1.effects.Add("done:Beta");
	rules.actions.Add(pick(b1));
	AtlasAction c1;
	c1.name = "Gamma"; c1.text = "build gamma"; c1.requires.Add("never"); c1.effects.Add("done:Gamma");
	rules.actions.Add(pick(c1));

	Vector<AtlasEvent> events;
	AtlasEvent e1 = Ev("e1", "v1", "element", "a", "stage"); e1.stage = "planned";
	AtlasEvent e2 = Ev("e2", "v3", "element", "b", "stage"); e2.stage = "planned";
	AtlasEvent e3 = Ev("e3", "v3", "element", "c", "stage"); e3.stage = "planned";
	events.Add(pick(e1));
	events.Add(pick(e2));
	events.Add(pick(e3));

	Vector<String> none;
	Vector<AtlasFile> no_files;
	AtlasVersionDiagram d;
	AtlasBuildDiagram(f, head, rules, events, no_files, none, d);
	Check(d.rows.GetCount() == 3, "the diagram has one row per version");
	Check(d.rows[0].group_start && d.rows[1].group_start && !d.rows[2].group_start,
	      "a minor group starts a row only where its minor number changes");
	int banners = 0, works = 0, unmet = 0;
	for(const AtlasDiagramBubble& b : d.bubbles) {
		banners += b.kind == "banner";
		works += b.kind == "work";
		unmet += b.unmet;
	}
	Check(banners == 2, "each minor group has one banner");
	Check(works == 3 && d.bubbles.GetCount() == 7, "work units and contract bubbles are all drawn");
	Check(unmet == 1, "only the work with an entry nothing produces is flagged as unmet");
	Check(d.arrows.GetCount() == 3, "producers are joined to what they feed: a->b, the gate->b, a->the feature");
	bool arrows_valid = true;
	for(const AtlasDiagramArrow& a : d.arrows)
		arrows_valid &= a.from >= 0 && a.to >= 0 && a.from < d.bubbles.GetCount() && a.to < d.bubbles.GetCount()
		                && a.from != a.to;
	Check(arrows_valid, "every arrow joins two existing bubbles");

	Vector<String> hidden;
	hidden.Add("b");
	AtlasVersionDiagram h;
	AtlasBuildDiagram(f, head, rules, events, no_files, hidden, h);
	works = 0;
	for(const AtlasDiagramBubble& b : h.bubbles)
		works += b.kind == "work" && b.element == "b";
	Check(works == 0 && h.bubbles.GetCount() == 6 && h.arrows.GetCount() == 1,
	      "a hidden element's work and the arrows to it are left out");
	Check(h.rows.GetCount() == 3, "a version keeps its row when all its work is hidden");

	// An initial atom is never unmet; a task literal is shown as waiting; an entry fed by work later in the same
	// version is unmet, because the version runs its work in order.
	AtlasRules r2;
	r2.initial.Add("k");
	AtlasAction ra, rb, rc, rd;
	ra.name = "Alpha"; ra.text = "build alpha"; ra.entry_none = true; ra.effects.Add("done:Alpha");
	rb.name = "Beta"; rb.text = "build beta"; rb.requires.Add("task:t1:done"); rb.effects.Add("done:Beta");
	rc.name = "Gamma"; rc.text = "build gamma"; rc.requires.Add("y:two"); rc.effects.Add("z:three");
	rd.name = "Delta"; rd.text = "build delta"; rd.requires.Add("k"); rd.effects.Add("y:two");
	r2.actions.Add(pick(ra));
	r2.actions.Add(pick(rb));
	r2.actions.Add(pick(rc));
	r2.actions.Add(pick(rd));
	Vector<AtlasEvent> ev2;
	AtlasEvent f1 = Ev("f1", "v1", "element", "a", "stage"); f1.stage = "planned";
	AtlasEvent f2 = Ev("f2", "v3", "element", "b", "stage"); f2.stage = "planned";
	AtlasEvent f3 = Ev("f3", "v3", "element", "c", "stage"); f3.stage = "planned";
	AtlasEvent f4 = Ev("f4", "v3", "element", "d", "stage"); f4.stage = "planned";
	ev2.Add(pick(f1));
	ev2.Add(pick(f2));
	ev2.Add(pick(f3));
	ev2.Add(pick(f4));
	AtlasVersionDiagram q;
	AtlasBuildDiagram(f, head, r2, ev2, no_files, none, q);
	int beta_waits = -1, delta_unmet = -1, gamma_unmet = -1;
	for(const AtlasDiagramBubble& b : q.bubbles) {
		if(b.kind != "work")
			continue;
		if(b.text == "Beta") { beta_waits = b.waits.GetCount(); Check(!b.unmet, "a task literal is shown as waiting, not as unmet"); }
		if(b.text == "Delta") delta_unmet = b.unmet;
		if(b.text == "Gamma") gamma_unmet = b.unmet;
	}
	Check(beta_waits == 1, "the waiting task is listed on its bubble");
	Check(delta_unmet == 0, "an initial atom is not unmet");
	Check(gamma_unmet == 1, "an entry fed by work later in the same version is unmet");

	// A declaration: the class and function spans of a file record show as a declaration bubble under the work that
	// declares them, and the minor group's banner counts them. A hidden element declares nothing for a viewer who may
	// not see it.
	AtlasFile df;
	df.id = "df1";
	df.source = "demo";
	df.path = "src/A/A.h";
	df.kind = "artifact";
	AtlasFileSpan s1;
	s1.id = "x1";
	s1.element = "a";
	s1.role = "destination";
	s1.kind = "function";
	s1.signature = "static int Step(int n);";
	df.spans.Add(pick(s1));
	AtlasFileSpan s2;
	s2.id = "x2";
	s2.element = "a";
	s2.role = "destination";
	s2.kind = "class";
	s2.signature = "class Alpha";
	df.spans.Add(pick(s2));
	Vector<AtlasFile> decl_files;
	decl_files.Add(pick(df));

	AtlasVersionDiagram k;
	AtlasBuildDiagram(f, head, rules, events, decl_files, none, k);
	int decls = 0;
	const AtlasDiagramBubble *decl_bubble = NULL;
	String group_text;
	for(const AtlasDiagramBubble& b : k.bubbles) {
		if(b.kind == "declaration") {
			decls++;
			decl_bubble = &b;
		}
		if(b.kind == "banner" && b.text.StartsWith("0.0:"))
			group_text = b.text;
	}
	Check(decls == 1 && decl_bubble && decl_bubble->declares.GetCount() == 3
	      && decl_bubble->declares[0] == "src/A/A.h",
	      "a declaring unit gets one declaration bubble: its file, then one line per signature");
	Check(group_text.Find("declares 1 classes, 1 functions") >= 0,
	      "the minor group's banner counts its declared classes and functions");

	Vector<String> hide_a;
	hide_a.Add("a");
	AtlasVersionDiagram kh;
	AtlasBuildDiagram(f, head, rules, events, decl_files, hide_a, kh);
	decls = 0;
	group_text.Clear();
	for(const AtlasDiagramBubble& b : kh.bubbles) {
		decls += b.kind == "declaration";
		if(b.kind == "banner" && b.text.StartsWith("0.0:"))
			group_text = b.text;
	}
	Check(decls == 0 && group_text.Find("declares") < 0,
	      "a hidden element's declarations are left out, and so are its counts");
}

// Tasks and processing work in the diagram: a planned task is a red bubble in the version it was logged in, with an
// arrow to the work that waits for it; a dropped or hidden task is left out; more than four in a version become one
// group; a processing action is marked on its work bubble.
static AtlasEvent TaskEv(const char *id, const char *vid, const char *task, const char *type)
{
	AtlasEvent e = Ev(id, vid, "task", task, type);
	return e;
}

// Multi-element tasks and the interface bubble: one task links several elements; it is hidden only when all are.
static void MultiLinkChecks()
{
	AtlasFamily f;
	AtlasVersion v1, v2;
	v1.vid = "v1"; v1.code = 1;
	v2.vid = "v2"; v2.code = 2;
	f.versions.Add(pick(v1));
	f.versions.Add(pick(v2));

	AtlasMap head = SmallMap();
	AtlasUiAction ui;
	ui.name = "run-alpha";
	ui.text = "runs alpha";
	ui.medium = "cli";
	ui.feature = "Alpha";
	head.elements[0].ui.Add(pick(ui));

	AtlasRules rules;
	AtlasAction a1, b1;
	a1.name = "Alpha"; a1.entry_none = true; a1.effects.Add("done:Alpha");
	b1.name = "Beta"; b1.requires.Add("task:m1:done"); b1.effects.Add("done:Beta");
	rules.actions.Add(pick(a1));
	rules.actions.Add(pick(b1));

	Vector<AtlasEvent> events;
	AtlasEvent s1 = Ev("s1", "v1", "element", "a", "stage"); s1.stage = "planned";
	AtlasEvent s2 = Ev("s2", "v2", "element", "b", "stage"); s2.stage = "planned";
	events.Add(pick(s1));
	events.Add(pick(s2));
	AtlasEvent p = TaskEv("p1", "v1", "m1", "proposed");
	p.name = "Make both"; p.target = "v2"; p.link = "element:a,element:b";
	events.Add(pick(p));

	Vector<String> none, hide_a, hide_both;
	hide_a.Add("a");
	hide_both.Add("a");
	hide_both.Add("b");
	Vector<AtlasFile> no_files;
	AtlasVersionDiagram d;
	AtlasBuildDiagram(f, head, rules, events, no_files, none, d);
	int task = -1, iface = -1;
	for(int i = 0; i < d.bubbles.GetCount(); i++) {
		if(d.bubbles[i].kind == "task") task = i;
		if(d.bubbles[i].kind == "interface") iface = i;
	}
	Check(task >= 0 && d.bubbles[task].detail.Find("Alpha") >= 0 && d.bubbles[task].detail.Find("Beta") >= 0,
	      "a task linked to two elements names both in its detail");
	Check(iface >= 0 && d.bubbles[iface].row == 0 && d.bubbles[iface].declares.GetCount() == 1
	      && d.bubbles[iface].declares[0].StartsWith("cli: run-alpha"),
	      "an element with user actions draws an interface bubble in its row, one line per action");

	AtlasVersionDiagram h1, h2;
	AtlasBuildDiagram(f, head, rules, events, no_files, hide_a, h1);
	AtlasBuildDiagram(f, head, rules, events, no_files, hide_both, h2);
	int shown_a = 0, shown_both = 0;
	for(const AtlasDiagramBubble& b : h1.bubbles)
		shown_a += b.kind == "task";
	for(const AtlasDiagramBubble& b : h2.bubbles)
		shown_both += b.kind == "task";
	Check(shown_a == 1, "a task stays visible while one of its linked elements is visible");
	Check(shown_both == 0, "a task is hidden only when every linked element is hidden");
}

static void TaskChecks()
{
	AtlasFamily f;
	AtlasVersion v1, v2, v3;
	v1.vid = "v1"; v1.code = 1;
	v2.vid = "v2"; v2.code = 1000;
	v3.vid = "v3"; v3.code = 1001;
	f.versions.Add(pick(v1));
	f.versions.Add(pick(v2));
	f.versions.Add(pick(v3));

	AtlasMap head = SmallMap();
	AtlasRules rules;
	AtlasAction a1, b1;
	a1.name = "Alpha"; a1.text = "build alpha"; a1.entry_none = true; a1.effects.Add("done:Alpha");
	b1.name = "Beta"; b1.text = "build beta"; b1.requires.Add("task:t1:done"); b1.effects.Add("done:Beta");
	b1.processing = true;
	rules.actions.Add(pick(a1));
	rules.actions.Add(pick(b1));

	Vector<AtlasEvent> events;
	AtlasEvent s1 = Ev("s1", "v1", "element", "a", "stage"); s1.stage = "planned";
	AtlasEvent s2 = Ev("s2", "v3", "element", "b", "stage"); s2.stage = "planned";
	events.Add(pick(s1));
	events.Add(pick(s2));

	// t1: logged in v1, needed by v3, linked to Beta, assigned to a bot session. t2 is proposed and dropped.
	AtlasEvent p1 = TaskEv("p1", "v1", "t1", "proposed"); p1.name = "Write the dataset"; p1.target = "v3"; p1.link = "element:b";
	AtlasEvent as1 = TaskEv("as1", "v1", "t1", "assigned"); as1.assignee = "bot-session-1";
	AtlasEvent p2 = TaskEv("p2", "v1", "t2", "proposed"); p2.name = "Old idea"; p2.target = "v3"; p2.link = "element:b";
	AtlasEvent d2 = TaskEv("d2", "v1", "t2", "dropped");
	events.Add(pick(p1));
	events.Add(pick(as1));
	events.Add(pick(p2));
	events.Add(pick(d2));

	Vector<String> none;
	Vector<AtlasFile> no_files;
	AtlasVersionDiagram d;
	AtlasBuildDiagram(f, head, rules, events, no_files, none, d);

	int task_bubble = -1, tasks = 0, beta = -1, alpha = -1;
	for(int i = 0; i < d.bubbles.GetCount(); i++) {
		const AtlasDiagramBubble& b = d.bubbles[i];
		if(b.kind == "task") { tasks++; task_bubble = i; }
		if(b.kind == "work" && b.text == "Beta") beta = i;
		if(b.kind == "work" && b.text == "Alpha") alpha = i;
	}
	Check(tasks == 1, "a dropped task is not drawn; a planned one is drawn once");
	Check(task_bubble >= 0 && d.bubbles[task_bubble].row == 0,
		"a task sits in the row of the version it was logged in");
	Check(task_bubble >= 0 && d.bubbles[task_bubble].literal == "task:t1:done",
		"a task bubble carries the literal the work waits for");
	Check(task_bubble >= 0 && d.bubbles[task_bubble].assignee == "bot-session-1",
		"a task bubble names its assignee");
	Check(beta >= 0 && d.bubbles[beta].waits.IsEmpty() && !d.bubbles[beta].unmet,
		"work that waits for a drawn task has no waiting line and is not unmet");
	bool arrow_from_task = false;
	for(const AtlasDiagramArrow& ar : d.arrows)
		arrow_from_task |= ar.from == task_bubble && ar.to == beta;
	Check(arrow_from_task, "an arrow runs from the task to the work that waits for it");
	Check(beta >= 0 && d.bubbles[beta].processing && alpha >= 0 && !d.bubbles[alpha].processing,
		"a processing action marks its work bubble and only that one");

	// A task whose linked element the viewer may not see is left out, and the work keeps its waiting line.
	Vector<String> hide_b;
	hide_b.Add("b");
	AtlasVersionDiagram h;
	AtlasBuildDiagram(f, head, rules, events, no_files, hide_b, h);
	int hidden_tasks = 0, hidden_beta = -1;
	for(int i = 0; i < h.bubbles.GetCount(); i++) {
		hidden_tasks += h.bubbles[i].kind == "task";
		if(h.bubbles[i].kind == "work" && h.bubbles[i].text == "Beta")
			hidden_beta = i;
	}
	Check(hidden_tasks == 0, "a task linked to a hidden element is left out");
	Check(hidden_beta < 0 || h.bubbles[hidden_beta].waits.GetCount() == 1,
		"work waiting for a task that is not drawn keeps the waiting line");

	// Five tasks in one version become one task group, and the work that waits for one of them is fed by the group.
	Vector<AtlasEvent> many;
	many.Add(pick(s1));
	many.Add(pick(s2));
	for(int k = 1; k <= 5; k++) {
		String id = "t" + AsString(k);
		AtlasEvent e = TaskEv("g", "v3", id, "proposed");
		e.id = "g" + AsString(k);
		e.name = "Task " + AsString(k);
		e.target = "v3";
		e.link = "element:a";
		many.Add(pick(e));
	}
	AtlasVersionDiagram g;
	AtlasBuildDiagram(f, head, rules, many, no_files, none, g);
	int groups = 0, group = -1, group_tasks = 0, singles = 0;
	for(int i = 0; i < g.bubbles.GetCount(); i++) {
		if(g.bubbles[i].kind == "taskgroup") { groups++; group = i; group_tasks = g.bubbles[i].literals.GetCount(); }
		singles += g.bubbles[i].kind == "task";
	}
	Check(singles == 0, "more than four tasks in a version are not drawn one by one");
	Check(groups == 1 && group_tasks == 5, "five tasks in one version become one group of five");
	Check(group >= 0 && g.bubbles[group].declares.GetCount() == 5, "the group lists one line per task");
}

// The processing flag survives saving and loading the action.
static void ProcessingFlagCheck()
{
	AtlasAction a;
	a.name = "Train"; a.processing = true;
	AtlasAction back;
	Check(LoadFromJson(back, StoreAsJson(a)) && back.processing, "the processing flag survives a save and load");
	AtlasAction old;
	Check(LoadFromJson(old, "{\"name\":\"Old\"}") && !old.processing, "an action saved before the flag loads as not processing");
}

// A planned task (proposed, not dropped) meets the entry of the work that waits for it; a dropped task does not.
static void PlannedTaskChecks()
{
	AtlasFamily f;
	TwoVersions(f);
	AtlasRules rules;
	AtlasAction g;
	g.name = "Gated"; g.text = "work that waits for t9"; g.requires.Add("task:t9:done"); g.effects.Add("gated_done");
	rules.actions.Add(pick(g));
	AtlasMap head = PlanHead();
	Vector<AtlasRoadmapMessage> out;
	Vector<Vector<String>> worlds;

	Vector<AtlasEvent> planned;
	AtlasEvent p1 = Ev("tp1", "v1", "task", "t9", "proposed");
	p1.target = "v2";
	p1.name = "Gate";
	planned.Add(pick(p1));
	AtlasEvent w1 = Ev("tp2", "v2", "element", "x", "note");
	w1.action = "Gated";
	planned.Add(pick(w1));
	AtlasReplayRoadmap(head, f, rules, planned, out, worlds);
	Check(!HasMessage(out, "error", "requires"), "a planned task meets the entry of the work that waits for it");

	Vector<AtlasEvent> dropped;
	AtlasEvent p2 = Ev("tp3", "v1", "task", "t9", "proposed");
	p2.target = "v2";
	p2.name = "Gate";
	dropped.Add(pick(p2));
	dropped.Add(pick(Ev("tp4", "v1", "task", "t9", "dropped")));
	AtlasEvent w2 = Ev("tp5", "v2", "element", "x", "note");
	w2.action = "Gated";
	dropped.Add(pick(w2));
	AtlasReplayRoadmap(head, f, rules, dropped, out, worlds);
	Check(HasMessage(out, "error", "requires"), "a dropped task does not meet the entry of the work that waits for it");
}

static void AckStoreChecks(const String& scratch)
{
	String data = AppendFileName(scratch, "ack-data");
	RealizeDirectory(data);
	AtlasWarningAck a;
	a.warning_id = "0123456789abcdef";
	a.rule = "vacuous-version";
	a.comment = "kept on purpose";
	a.actor = "ed";
	a.at = "2026-11-15T10:00:00Z";
	String err;
	Check(AtlasSaveAck(data, "ackfam", a, err), "a comment on a warning is stored");
	Vector<AtlasWarningAck> back;
	Check(AtlasLoadAcks(data, "ackfam", back, err) && back.GetCount() == 1 && back[0].comment == "kept on purpose"
	      && back[0].warning_id == a.warning_id, "a stored comment reads back with its warning id");
	Vector<AtlasWarningAck> other;
	Check(AtlasLoadAcks(data, "otherfam", other, err) && other.IsEmpty(), "a comment belongs to its own family");
	// The read cache: a change on disk by another writer shows on the next read, even when it is the same size.
	String record = AppendFileName(data, "data/acks/ackfam.0123456789abcdef.json");
	String on_disk = LoadFile(record);
	Check(on_disk.Find("kept on purpose") >= 0, "the comment is on disk where the store keeps it");
	// The file system stamps modification times per clock tick, so the change waits out one tick before it is written.
	Sleep(50);
	String changed = on_disk;
	changed.Replace("kept on purpose", "changed by hand");
	SaveFile(record, changed);
	Vector<AtlasWarningAck> seen;
	Check(AtlasLoadAcks(data, "ackfam", seen, err) && seen.GetCount() == 1 && seen[0].comment == "changed by hand",
	      "a record changed on disk reads back as changed, not from the cache");
	a.comment = "written through the store";
	Check(AtlasSaveAck(data, "ackfam", a, err), "a comment can be replaced through the store");
	Check(AtlasLoadAcks(data, "ackfam", seen, err) && seen.GetCount() == 1 && seen[0].comment == "written through the store",
	      "a record written through the store reads back at once");
	Check(AtlasDeleteAck(data, "ackfam", a.warning_id, err), "a comment is removed by its warning id");
	Check(AtlasLoadAcks(data, "ackfam", back, err) && back.IsEmpty(), "a removed comment no longer reads back");
}

static void SourceChecks(const String& scratch)
{
	String outside = AppendFileName(scratch, "source-outside");
	String root = AppendFileName(scratch, "source-root");
	RealizeDirectory(outside);
	RealizeDirectory(AppendFileName(root, "sub"));
	RealizeDirectory(AppendFileName(root, ".git"));
	SaveFile(AppendFileName(outside, "secret.cpp"), "secret\n");
	SaveFile(AppendFileName(root, "ok.cpp"), "one\ntwo\n");
	SaveFile(AppendFileName(root, "sub/x.py"), "print(1)\n");
	SaveFile(AppendFileName(root, ".git/config"), "[core]\n");
	SaveFile(AppendFileName(root, "bad.exe"), "MZ\n");
	SaveFile(AppendFileName(root, "big.cpp"), String('x', 600 * 1024));
#ifndef _WIN32
	symlink(~AppendFileName(outside, "secret.cpp"), ~AppendFileName(root, "link.cpp"));
#endif

	Vector<String> lines;
	String err;
	Check(AtlasReadSourceLines(root, "ok.cpp", lines, err) && lines.GetCount() == 2 && lines[1] == "two",
	      "a code file inside the source root is read, line by line");
	Check(AtlasReadSourceLines(root, "sub/x.py", lines, err) && lines.GetCount() == 1,
	      "a code file in a sub-directory is read");
	Check(!AtlasReadSourceLines(root, "../source-outside/secret.cpp", lines, err) && !AtlasSafeSourcePath("../secret.cpp"),
	      "a path with '..' is refused");
	Check(!AtlasReadSourceLines(root, "/etc/hostname", lines, err) && !AtlasSafeSourcePath("/etc/hostname"),
	      "an absolute path is refused");
	Check(!AtlasReadSourceLines(root, ".git/config", lines, err) && err.Find("source file") >= 0,
	      "a dot-entry such as .git is refused");
	Check(!AtlasReadSourceLines(root, "bad.exe", lines, err) && err.Find("source file") >= 0,
	      "a file that is not a code or document type is refused");
	Check(!AtlasReadSourceLines(root, "nope.cpp", lines, err) && err.Find("not found") >= 0,
	      "a missing file is refused");
#ifndef _WIN32
	Check(!AtlasReadSourceLines(root, "link.cpp", lines, err) && err.Find("escapes") >= 0,
	      "a symlink that leaves the source root is refused");
#endif
	Check(!AtlasReadSourceLines(root, "big.cpp", lines, err) && err.Find("size cap") >= 0,
	      "a file over the size cap is refused");
	Check(!AtlasReadSourceLines("", "ok.cpp", lines, err) && err.Find("not registered") >= 0,
	      "a project without the source root refuses the read");

	AtlasFamily fam;
	fam.head_key = "t";
	fam.versions.Add(pick(Ver("v1", "0.0.1")));
	fam.versions.Add(pick(Ver("v2", "1.0.0")));
	AtlasFile f = GoodFile();
	Vector<String> base = AtlasSplitLines("one\ntwo\n");
	Vector<String> at1, at2;
	Check(AtlasFileTextAt(f, base, fam, "v1", at1, err) && Joined(at1) == "one|two"
	      && AtlasFileTextAt(f, base, fam, "v2", at2, err) && Joined(at2) == "new|two",
	      "the text at a version is the base with the edits up to it, so a later edit does not show earlier");
	Check(!AtlasFileTextAt(f, base, fam, "v9", at1, err), "a version outside the family has no text");
	AtlasFile seen = AtlasFileForViewer(f, false);
	AtlasFile edited = AtlasFileForViewer(f, true);
	Check(seen.edits.GetCount() == 2 && seen.edits[1].text.IsEmpty() && seen.edits[1].element == "b"
	      && edited.edits[1].text == "new" && seen.spans.GetCount() == 1,
	      "a reader of the map sees the edits without their code; an editor sees the code too");
}

bool RunAtlasSelfTests(const String& scratch_dir)
{
	s_checks = 0;
	s_failed = 0;
	ModelChecks();
	PkgChecks();
	SeedChecks();
	StoreChecks(scratch_dir);
	VersionChecks();
	EventChecks();
	RoadmapStoreChecks(scratch_dir);
	RoadmapChecks();
	StrictnessChecks();
	GateAndTaskChecks();
	LedgerChecks();
	DiagramChecks();
	TaskChecks();
	MultiLinkChecks();
	ProcessingFlagCheck();
	PlannedTaskChecks();
	DemoChecks();
	MarkdownChecks();
	WhatsNextChecks();
	ProjectChecks(scratch_dir);
	ProjectStoreChecks(scratch_dir);
	FileChecks();
	FileStoreChecks(scratch_dir);
	AckStoreChecks(scratch_dir);
	SourceChecks(scratch_dir);
	Cout() << "Atlas self-tests: " << (s_checks - s_failed) << "/" << s_checks << " checks passed\n";
	return s_failed == 0;
}

END_UPP_NAMESPACE
