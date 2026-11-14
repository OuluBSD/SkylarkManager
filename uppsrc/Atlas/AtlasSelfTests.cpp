#include "AtlasSite.h"

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
		"# PKR slice\n"
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

static void StoreChecks(const String& scratch)
{
	String root = AppendFileName(scratch, "store");
	RealizeDirectory(root);
	String key = AtlasDocKey("pkr", "v0.0.1");
	Check(key == "pkr-v0.0.1", "a document key joins the map and the version");

	AtlasMap m = SmallMap();
	m.map = "pkr";
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
	Check(!AtlasLoadMap(root, "pkr-v9.9.9", none, none_rev, err) && err.Find("map not found") >= 0,
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
	      "-1 is no version, and 0.0.0 is a code but not an insertable version");

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
	Check(AtlasInsertVersion(f, "0.1.0", "v6", err) && LabelOf(f, "v6") == "0.1.0" && LabelOf(f, "v5") == "0.2.0",
	      "inserting 0.1.0 again moves 0.1.x to 0.2.x");
	Check(AtlasInsertVersion(f, "1.0.0", "v7", err) && f.Head() == 6 && f.versions[6].vid == "v7",
	      "1.0.0 becomes the head");

	AtlasFamily full;
	full.versions.Add(pick(Ver("top", "0.0.999")));
	Check(!AtlasInsertVersion(full, "0.0.999", "x", err) && err.Find("past 999") >= 0 && LabelOf(full, "top") == "0.0.999",
	      "a bump past 999 is refused and changes nothing");
	Check(!AtlasInsertVersion(f, "0.0.0", "z1", err) && !AtlasInsertVersion(f, "1.2", "z2", err)
	      && !AtlasInsertVersion(f, "0.0.2", "v2", err), "0.0.0, a short label and a repeated vid are refused");

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
	f.head_key = "pkr-v0.0.1";
	f.versions.Add(pick(Ver("v1", "0.0.1")));
	String rev, next, err;
	Check(AtlasCreateFamily(root, "pkr", f, rev, err) && !rev.IsEmpty(), "a new version list is created");
	Check(!AtlasCreateFamily(root, "pkr", f, next, err) && err.Find("already exists") >= 0,
	      "a second version list with the same name is refused, not overwritten");

	AtlasFamily back;
	String loaded_rev;
	Check(AtlasLoadFamily(root, "pkr", back, loaded_rev, err) && back.head_key == "pkr-v0.0.1"
	      && back.versions.GetCount() == 1 && loaded_rev == rev, "the version list loads with its revision");
	Check(!AtlasLoadFamily(root, "none", back, loaded_rev, err) && err.Find("initialise") >= 0,
	      "a missing version list says how to create it");

	AtlasEvent e = Ev("ev-store-1", "v1", "element", "e_a", "note");
	e.text = "stored";
	Check(AtlasPutEvent(root, "pkr", e, err), "an event saves");
	Check(!AtlasPutEvent(root, "pkr", e, err), "an event id cannot be written twice");
	AtlasEvent other = Ev("ev-store-2", "v1", "element", "e_b", "note");
	AtlasPutEvent(root, "other", other, err);
	Vector<AtlasEvent> got;
	Check(AtlasLoadEvents(root, "pkr", got, err) && got.GetCount() == 1 && got[0].text == "stored",
	      "a family lists only its own events, with their text");
}

static bool HasMessage(const Vector<AtlasRoadmapMessage>& m, const char *severity, const char *rule)
{
	for(const AtlasRoadmapMessage& x : m)
		if(x.severity == severity && x.rule == rule)
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

	int errors = 0, at_head = 0, other = 0;
	for(const AtlasRoadmapMessage& m : out) {
		if(m.severity == "error") {
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

bool RunAtlasSelfTests(const String& scratch_dir)
{
	s_checks = 0;
	s_failed = 0;
	ModelChecks();
	SeedChecks();
	StoreChecks(scratch_dir);
	VersionChecks();
	EventChecks();
	RoadmapStoreChecks(scratch_dir);
	RoadmapChecks();
	DemoChecks();
	MarkdownChecks();
	Cout() << "Atlas self-tests: " << (s_checks - s_failed) << "/" << s_checks << " checks passed\n";
	return s_failed == 0;
}

END_UPP_NAMESPACE
