#include "AtlasModel.h"

NAMESPACE_UPP

static const int LEAF_W = 180;
static const int LEAF_H = 100;
static const int GAP = 24;
static const int PAD = 40;        // world margin
static const int INNER_TOP = 40;  // room for the element's title inside its box
static const int INNER_PAD = 20;

bool AtlasIdValid(const String& id)
{
	if(id.IsEmpty() || id.GetCount() > 64)
		return false;
	for(int i = 0; i < id.GetCount(); i++) {
		int c = id[i];
		bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if(!alnum && c != '_' && c != '-')
			return false;
	}
	return true;
}

bool AtlasKindValid(const String& kind)
{
	return kind == "continent" || kind == "block" || kind == "place" || kind == "sea";
}

bool AtlasStatusValid(const String& status)
{
	return status == "hot" || status == "active" || status == "planned" || status == "not-ready"
	    || status == "future" || status == "settled" || status == "trouble";
}

bool AtlasRoadKindValid(const String& kind)
{
	return kind == "progress" || kind == "communication" || kind == "dependency";
}

int AtlasMap::FindElement(const String& id) const
{
	for(int i = 0; i < elements.GetCount(); i++)
		if(elements[i].id == id)
			return i;
	return -1;
}

int AtlasMap::FindRoad(const String& id) const
{
	for(int i = 0; i < roads.GetCount(); i++)
		if(roads[i].id == id)
			return i;
	return -1;
}

String AtlasUniqueElementId(const AtlasMap& m, const String& base)
{
	StringBuffer slug;
	bool last_sep = true;
	for(int i = 0; i < base.GetCount() && slug.GetCount() < 48; i++) {
		int c = base[i];
		if(c >= 'A' && c <= 'Z')
			c = c - 'A' + 'a';
		if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
			slug.Cat(c);
			last_sep = false;
		}
		else if(!last_sep) {
			slug.Cat('_');
			last_sep = true;
		}
	}
	String sl = slug;
	int len = sl.GetCount();
	while(len > 0 && sl[len - 1] == '_')
		len--;
	String s = sl.Left(len);
	String root = "e_" + (s.IsEmpty() ? String("element") : s);
	String id = root;
	for(int n = 2; m.FindElement(id) >= 0; n++)
		id = Format("%s_%d", root, n);
	return id;
}

String AtlasUniqueRoadId(const AtlasMap& m)
{
	for(int n = m.roads.GetCount() + 1; ; n++) {
		String id = Format("r_%d", n);
		if(m.FindRoad(id) < 0)
			return id;
	}
	return String();
}

int AtlasDepth(const AtlasMap& m, const String& id)
{
	int depth = 0;
	String cur = id;
	for(;;) {
		int i = m.FindElement(cur);
		if(i < 0)
			return -1;
		const String& parent = m.elements[i].parent;
		if(parent.IsEmpty())
			return depth;
		if(++depth > m.elements.GetCount())
			return -1; // cycle
		cur = parent;
	}
	return -1;
}

int AtlasChildCount(const AtlasMap& m, const String& id)
{
	int n = 0;
	for(const AtlasElement& e : m.elements)
		if(e.parent == id)
			n++;
	return n;
}

Vector<String> AtlasPath(const AtlasMap& m, const String& id)
{
	Vector<String> path;
	String cur = id;
	for(int guard = 0; guard <= m.elements.GetCount(); guard++) {
		int i = m.FindElement(cur);
		if(i < 0) {
			path.Clear();
			return path;
		}
		path.Insert(0, cur);
		if(m.elements[i].parent.IsEmpty())
			return path;
		cur = m.elements[i].parent;
	}
	path.Clear();
	return path;
}

Vector<String> AtlasPlaneElements(const AtlasMap& m, const String& plane)
{
	Vector<String> ids;
	for(const AtlasElement& e : m.elements)
		if(e.parent == plane)
			ids.Add(e.id);
	return ids;
}

void AtlasValidate(const AtlasMap& m, AtlasCheck& out)
{
	if(!AtlasIdValid(m.map))
		out.errors.Add("map id is not valid: " + m.map);
	if(m.version.IsEmpty())
		out.errors.Add("version is required");
	if(m.world.width <= 0 || m.world.height <= 0)
		out.errors.Add("world size must be positive");

	Index<String> ids;
	for(int i = 0; i < m.elements.GetCount(); i++) {
		const AtlasElement& e = m.elements[i];
		if(!AtlasIdValid(e.id))
			out.errors.Add("element id is not valid: " + e.id);
		else if(ids.Find(e.id) >= 0)
			out.errors.Add("duplicate element id: " + e.id);
		else
			ids.Add(e.id);
		if(e.name.IsEmpty())
			out.errors.Add("element " + e.id + " has no name");
		if(!AtlasKindValid(e.kind))
			out.errors.Add("element " + e.id + " has an unknown kind: " + e.kind);
		if(!AtlasStatusValid(e.status))
			out.errors.Add("element " + e.id + " has an unknown status: " + e.status);
		if(e.w <= 0 || e.h <= 0)
			out.errors.Add("element " + e.id + " must have a positive size");
		if(e.x < 0 || e.y < 0)
			out.errors.Add("element " + e.id + " has a negative position");
	}

	for(int i = 0; i < m.elements.GetCount(); i++) {
		const AtlasElement& e = m.elements[i];
		if(e.parent.IsEmpty()) {
			if(e.x + e.w > m.world.width || e.y + e.h > m.world.height)
				out.warnings.Add("element " + e.id + " extends past the world plane");
			continue;
		}
		int p = m.FindElement(e.parent);
		if(p < 0) {
			out.errors.Add("element " + e.id + " has a missing parent: " + e.parent);
			continue;
		}
		if(p == i) {
			out.errors.Add("element " + e.id + " is its own parent");
			continue;
		}
		if(AtlasDepth(m, e.id) < 0) {
			out.errors.Add("element " + e.id + " is in a containment cycle");
			continue;
		}
		const AtlasElement& pe = m.elements[p];
		if(e.x + e.w > pe.w || e.y + e.h > pe.h)
			out.warnings.Add("element " + e.id + " extends past its parent " + pe.id);
	}

	Index<String> road_ids;
	for(const AtlasRoad& r : m.roads) {
		if(!AtlasIdValid(r.id))
			out.errors.Add("road id is not valid: " + r.id);
		else if(road_ids.Find(r.id) >= 0)
			out.errors.Add("duplicate road id: " + r.id);
		else
			road_ids.Add(r.id);
		if(!AtlasRoadKindValid(r.kind))
			out.errors.Add("road " + r.id + " has an unknown kind: " + r.kind);
		int f = m.FindElement(r.from);
		int t = m.FindElement(r.to);
		if(f < 0 || t < 0) {
			out.errors.Add("road " + r.id + " has an unknown endpoint");
			continue;
		}
		if(f == t)
			out.errors.Add("road " + r.id + " joins an element to itself");
		else if(m.elements[f].parent != m.elements[t].parent)
			out.errors.Add("road " + r.id + " must join elements on the same plane");
	}
}

// ---------------------------------------------------------------------------
// Seed layout: children sit on a square grid in their parent's plane, every
// sibling gets the largest sibling's slot, and a parent's box is sized to fit
// its grid. Leaves are LEAF_W x LEAF_H.
// ---------------------------------------------------------------------------

static int GridCols(int n)
{
	int c = 1;
	while(c * c < n)
		c++;
	return c;
}

static void PlaceGroup(AtlasMap& m, const Vector<int>& group, int cols, int x0, int y0, int slot_w, int slot_h)
{
	for(int k = 0; k < group.GetCount(); k++) {
		AtlasElement& e = m.elements[group[k]];
		e.x = x0 + (k % cols) * (slot_w + GAP);
		e.y = y0 + (k / cols) * (slot_h + GAP);
	}
}

static void MeasureElement(AtlasMap& m, const Vector<Vector<int>>& kids, int i)
{
	AtlasElement& e = m.elements[i];
	if(kids[i].IsEmpty()) {
		e.w = LEAF_W;
		e.h = LEAF_H;
		return;
	}
	int slot_w = 0, slot_h = 0;
	for(int c : kids[i]) {
		MeasureElement(m, kids, c);
		slot_w = max(slot_w, m.elements[c].w);
		slot_h = max(slot_h, m.elements[c].h);
	}
	int n = kids[i].GetCount();
	int cols = GridCols(n);
	int rows = (n + cols - 1) / cols;
	PlaceGroup(m, kids[i], cols, INNER_PAD, INNER_TOP, slot_w, slot_h);
	m.elements[i].w = 2 * INNER_PAD + cols * slot_w + (cols - 1) * GAP;
	m.elements[i].h = INNER_TOP + rows * slot_h + (rows - 1) * GAP + INNER_PAD;
}

// ---------------------------------------------------------------------------
// Seed parser.
// ---------------------------------------------------------------------------

struct SeedLine : Moveable<SeedLine> {
	int    no = 0;
	int    level = 0;
	String name;
	String kind;
	String status;
	String note;
};

struct SeedEdge : Moveable<SeedEdge> {
	int    no = 0;
	String from;
	String to;
	String kind;
	String label;
};

// The first arrow in the line, or -1. Sets the road kind it stands for.
static int FindArrow(const String& t, String& kind, int& len)
{
	static const char *arrows[] = { "->", "=>", "~>" };
	static const char *kinds[] = { "progress", "dependency", "communication" };
	int best = -1;
	for(int a = 0; a < 3; a++) {
		int p = t.Find(arrows[a]);
		if(p >= 0 && (best < 0 || p < best)) {
			best = p;
			kind = kinds[a];
			len = 2;
		}
	}
	return best;
}

static bool ValidSeedName(const String& name)
{
	return name.GetCount() && name.Find(':') < 0 && name.Find('|') < 0;
}

bool AtlasParseSeed(const String& text, AtlasMap& m, AtlasCheck& out)
{
	if(!m.elements.IsEmpty()) {
		out.errors.Add("seed import needs an empty map");
		return false;
	}

	Vector<SeedLine> lines;
	Vector<SeedEdge> edges;
	Vector<String> rows = Split(text, '\n', false);
	int prev_level = -1;
	for(int i = 0; i < rows.GetCount(); i++) {
		int no = i + 1;
		String raw = rows[i];
		TrimRight(raw);
		String t = TrimBoth(raw);
		if(t.IsEmpty() || t[0] == '#')
			continue;

		String ekind;
		int alen = 2;
		int ap = FindArrow(t, ekind, alen);
		if(ap >= 0) {
			SeedEdge e;
			e.no = no;
			e.kind = ekind;
			e.from = TrimBoth(t.Left(ap));
			String rest = t.Mid(ap + alen);
			int colon = rest.Find(':');
			e.to = TrimBoth(colon >= 0 ? rest.Left(colon) : rest);
			e.label = colon >= 0 ? TrimBoth(rest.Mid(colon + 1)) : String();
			if(e.from.IsEmpty() || e.to.IsEmpty())
				out.errors.Add(Format("line %d: an edge needs both endpoints", no));
			edges.Add(pick(e));
			continue;
		}

		int sp = 0;
		bool tab = false;
		while(sp < raw.GetCount() && (raw[sp] == ' ' || raw[sp] == '\t')) {
			if(raw[sp] == '\t')
				tab = true;
			sp++;
		}
		if(tab) {
			out.errors.Add(Format("line %d: tabs are not allowed in indentation", no));
			continue;
		}
		if(sp % 2) {
			out.errors.Add(Format("line %d: indentation must be 2 spaces per level", no));
			continue;
		}
		int level = sp / 2;
		if(prev_level < 0 && level != 0) {
			out.errors.Add(Format("line %d: the first element must not be indented", no));
			continue;
		}
		if(level > prev_level + 1) {
			out.errors.Add(Format("line %d: indentation jumps more than one level", no));
			continue;
		}
		Vector<String> parts = Split(TrimBoth(raw.Mid(sp)), '|', false);
		if(parts.GetCount() > 4) {
			out.errors.Add(Format("line %d: too many '|' fields (name | kind | status | note)", no));
			continue;
		}
		SeedLine s;
		s.no = no;
		s.level = level;
		s.name = TrimBoth(parts[0]);
		if(parts.GetCount() > 1)
			s.kind = TrimBoth(parts[1]);
		if(parts.GetCount() > 2)
			s.status = TrimBoth(parts[2]);
		if(parts.GetCount() > 3)
			s.note = TrimBoth(parts[3]);
		if(!ValidSeedName(s.name)) {
			out.errors.Add(Format("line %d: a name is required and may not contain ':'", no));
			continue;
		}
		if(s.kind.GetCount() && !AtlasKindValid(s.kind))
			out.errors.Add(Format("line %d: unknown kind '%s'", no, s.kind));
		if(s.status.GetCount() && !AtlasStatusValid(s.status))
			out.errors.Add(Format("line %d: unknown status '%s'", no, s.status));
		prev_level = level;
		lines.Add(pick(s));
	}
	if(!out.errors.IsEmpty())
		return false;

	// Build into a scratch map and commit only when it validates.
	AtlasMap built;
	built.map = m.map;
	built.version = m.version;
	built.title = m.title;
	built.world = m.world;

	Index<String> names;
	for(const SeedLine& s : lines) {
		if(names.Find(s.name) >= 0) {
			out.errors.Add(Format("line %d: duplicate name '%s'", s.no, s.name));
			continue;
		}
		names.Add(s.name);
	}
	if(!out.errors.IsEmpty())
		return false;

	// Containment: the parent of a line is the last line one level up.
	Vector<int> parent_of;
	Vector<Vector<int>> kids;
	kids.SetCount(lines.GetCount());
	Vector<int> stack;
	for(int k = 0; k < lines.GetCount(); k++) {
		int level = lines[k].level;
		stack.SetCount(level);
		parent_of.Add(level == 0 ? -1 : stack[level - 1]);
		if(level > 0)
			kids[stack[level - 1]].Add(k);
		stack.Add(k);
	}

	for(int k = 0; k < lines.GetCount(); k++) {
		const SeedLine& s = lines[k];
		AtlasElement e;
		e.id = AtlasUniqueElementId(built, s.name);
		e.name = s.name;
		e.kind = !s.kind.IsEmpty() ? s.kind : (kids[k].IsEmpty() ? String("place") : String("block"));
		e.status = !s.status.IsEmpty() ? s.status : String("planned");
		e.notes = s.note;
		built.elements.Add(pick(e));
	}
	for(int k = 0; k < lines.GetCount(); k++)
		if(parent_of[k] >= 0)
			built.elements[k].parent = built.elements[parent_of[k]].id;

	for(const SeedEdge& e : edges) {
		int f = names.Find(e.from);
		int t = names.Find(e.to);
		if(f < 0 || t < 0) {
			out.errors.Add(Format("line %d: unknown element '%s'", e.no, f < 0 ? e.from : e.to));
			continue;
		}
		AtlasRoad r;
		r.id = AtlasUniqueRoadId(built);
		r.from = built.elements[f].id;
		r.to = built.elements[t].id;
		r.kind = e.kind;
		r.label = e.label;
		built.roads.Add(pick(r));
	}
	if(!out.errors.IsEmpty())
		return false;

	// Layout: measure every root's subtree, then place the roots on the world grid.
	Vector<int> roots;
	for(int k = 0; k < lines.GetCount(); k++)
		if(parent_of[k] < 0)
			roots.Add(k);
	int slot_w = 0, slot_h = 0;
	for(int r : roots) {
		MeasureElement(built, kids, r);
		slot_w = max(slot_w, built.elements[r].w);
		slot_h = max(slot_h, built.elements[r].h);
	}
	if(roots.GetCount())
		PlaceGroup(built, roots, GridCols(roots.GetCount()), PAD, PAD, slot_w, slot_h);
	AtlasCoverWorld(built);

	AtlasCheck check;
	AtlasValidate(built, check);
	for(const String& e : check.errors)
		out.errors.Add(e);
	for(const String& w : check.warnings)
		out.warnings.Add(w);
	if(!out.errors.IsEmpty())
		return false;

	m.world = built.world;
	m.elements = pick(built.elements);
	m.roads = pick(built.roads);
	return true;
}

void AtlasCoverWorld(AtlasMap& m)
{
	int w = 0, h = 0;
	// Root elements are in world coordinates and contain their subtrees; children are relative to their parent.
	for(const AtlasElement& e : m.elements) {
		if(!e.parent.IsEmpty())
			continue;
		w = max(w, e.x + e.w);
		h = max(h, e.y + e.h);
	}
	if(w > 0)
		m.world.width = max(m.world.width, w + PAD);
	if(h > 0)
		m.world.height = max(m.world.height, h + PAD);
}

END_UPP_NAMESPACE
