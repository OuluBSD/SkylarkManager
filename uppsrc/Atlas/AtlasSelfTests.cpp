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

bool RunAtlasSelfTests(const String& scratch_dir)
{
	s_checks = 0;
	s_failed = 0;
	ModelChecks();
	SeedChecks();
	StoreChecks(scratch_dir);
	Cout() << "Atlas self-tests: " << (s_checks - s_failed) << "/" << s_checks << " checks passed\n";
	return s_failed == 0;
}

END_UPP_NAMESPACE
