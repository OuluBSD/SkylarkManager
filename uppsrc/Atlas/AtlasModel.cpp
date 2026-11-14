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

// ---------------------------------------------------------------------------
// Version codes and the version list.
// ---------------------------------------------------------------------------

static const int VERSION_PART_MAX = 999;

// Part k of a code: 0 major, 1 minor, 2 patch.
static int PartOf(int code, int level)
{
	if(level == 0)
		return code / 1000000;
	if(level == 1)
		return (code / 1000) % 1000;
	return code % 1000;
}

// The code step of one part: a bump at level k adds this to a code.
static int StepOf(int level)
{
	return level == 0 ? 1000000 : level == 1 ? 1000 : 1;
}

bool AtlasVersionValid(int code)
{
	return code >= 0 && code <= ATLAS_VERSION_MAX;
}

int AtlasVersionCode(const String& label)
{
	String text = label.StartsWith("v") ? label.Mid(1) : label;
	Vector<String> p = Split(text, '.');
	if(p.GetCount() != 3)
		return ATLAS_VERSION_UNSET;
	int part[3];
	for(int i = 0; i < 3; i++) {
		const String& d = p[i];
		if(d.IsEmpty() || d.GetCount() > 3)
			return ATLAS_VERSION_UNSET;
		int v = 0;
		for(int k = 0; k < d.GetCount(); k++) {
			if(d[k] < '0' || d[k] > '9')
				return ATLAS_VERSION_UNSET;
			v = v * 10 + (d[k] - '0');
		}
		if(v > VERSION_PART_MAX)
			return ATLAS_VERSION_UNSET;
		part[i] = v;
	}
	return part[0] * 1000000 + part[1] * 1000 + part[2];
}

String AtlasVersionLabel(int code)
{
	return Format("%d.%d.%d", PartOf(code, 0), PartOf(code, 1), PartOf(code, 2));
}

int AtlasFamily::Find(const String& vid) const
{
	for(int i = 0; i < versions.GetCount(); i++)
		if(versions[i].vid == vid)
			return i;
	return -1;
}

// Sorts the list ascending by code. Codes are integers, so this is one comparison.
static void SortVersions(Vector<AtlasVersion>& list)
{
	for(int i = 1; i < list.GetCount(); i++)
		for(int k = i; k > 0 && list[k - 1].code > list[k].code; k--) {
			AtlasVersion tmp = pick(list[k - 1]);
			list[k - 1] = pick(list[k]);
			list[k] = pick(tmp);
		}
}

bool AtlasInsertVersion(AtlasFamily& f, const String& label, const String& vid, String& err)
{
	int code = AtlasVersionCode(label);
	if(code == ATLAS_VERSION_UNSET) {
		err = "version label must be a.b.c with each part 0..999: " + label;
		return false;
	}
	if(code == 0) {
		err = "0.0.0 is not a version";
		return false;
	}
	if(!AtlasIdValid(vid)) {
		err = "version id is not valid: " + vid;
		return false;
	}
	if(f.Find(vid) >= 0) {
		err = "version id already exists: " + vid;
		return false;
	}
	// The insert level is the last non-zero part: inserting 0.1.0 bumps the minor, 0.0.2 the patch.
	int level = PartOf(code, 0) ? 0 : PartOf(code, 1) ? 1 : 2;
	int unit = StepOf(level) * 1000;  // the codes that share the higher parts
	// Bump into a copy first, so a refusal leaves f untouched.
	Vector<AtlasVersion> next;
	for(const AtlasVersion& v : f.versions) {
		AtlasVersion copy;
		copy.vid = v.vid;
		copy.code = v.code;
		bool same_prefix = v.code / unit == code / unit;
		if(same_prefix && PartOf(v.code, level) >= PartOf(code, level)) {
			if(PartOf(v.code, level) + 1 > VERSION_PART_MAX) {
				err = "inserting " + label + " would move " + AtlasVersionLabel(v.code) + " past 999";
				return false;
			}
			copy.code = v.code + StepOf(level);
		}
		next.Add(pick(copy));
	}
	AtlasVersion added;
	added.vid = vid;
	added.code = code;
	next.Add(pick(added));
	SortVersions(next);
	f.versions = pick(next);
	return true;
}

bool AtlasRenameVersion(AtlasFamily& f, const String& vid, const String& label, String& err)
{
	int code = AtlasVersionCode(label);
	if(code == ATLAS_VERSION_UNSET) {
		err = "version label must be a.b.c with each part 0..999: " + label;
		return false;
	}
	int i = f.Find(vid);
	if(i < 0) {
		err = "unknown version: " + vid;
		return false;
	}
	for(int k = 0; k < f.versions.GetCount(); k++)
		if(k != i && f.versions[k].code == code) {
			err = "another version already has the label " + label;
			return false;
		}
	f.versions[i].code = code;
	SortVersions(f.versions);
	return true;
}

// ---------------------------------------------------------------------------
// Events.
// ---------------------------------------------------------------------------

bool AtlasStageValid(const String& stage)
{
	return AtlasStageRank(stage) >= 0;
}

int AtlasStageRank(const String& stage)
{
	static const char *order[] = { "planned", "implementation_research", "implementation", "trials", "merge_to_master" };
	for(int i = 0; i < 5; i++)
		if(stage == order[i])
			return i;
	return -1;
}

bool AtlasEventTypeValid(const String& subject_kind, const String& type)
{
	if(subject_kind == "element")
		return type == "created" || type == "removed" || type == "stage" || type == "status" || type == "note";
	if(subject_kind == "road")
		return type == "connected" || type == "disconnected" || type == "modified" || type == "note";
	if(subject_kind == "task")
		return type == "proposed" || type == "accepted" || type == "done" || type == "dropped"
		    || type == "reopened" || type == "note";
	return false;
}

// Whether the subject must exist in the head document. A removal, a disconnection, and the creation of
// an element or road that later went away do not need it; a change to a live subject does.
static bool NeedsHeadSubject(const String& subject_kind, const String& type)
{
	if(subject_kind == "task")
		return false;
	return type == "note" || type == "stage" || type == "status" || type == "modified";
}

void AtlasValidateEvent(const AtlasEvent& e, const AtlasFamily& f, const AtlasMap& head, AtlasCheck& out)
{
	if(!AtlasIdValid(e.id))
		out.errors.Add("event id is not valid: " + e.id);
	if(f.Find(e.vid) < 0)
		out.errors.Add("event " + e.id + " names an unknown version: " + e.vid);
	if(!AtlasEventTypeValid(e.subject_kind, e.type)) {
		out.errors.Add("event " + e.id + ": type '" + e.type + "' does not apply to a " + e.subject_kind);
		return;
	}
	if(e.subject_id.IsEmpty())
		out.errors.Add("event " + e.id + " names no subject");
	else if(NeedsHeadSubject(e.subject_kind, e.type)) {
		bool found = e.subject_kind == "element" ? head.FindElement(e.subject_id) >= 0
		                                          : head.FindRoad(e.subject_id) >= 0;
		if(!found)
			out.errors.Add("event " + e.id + ": " + e.subject_kind + " " + e.subject_id + " is not in the head map");
	}
	if(e.type == "stage" && !AtlasStageValid(e.stage))
		out.errors.Add("event " + e.id + ": unknown stage '" + e.stage + "'");
	if(e.type == "status" && !AtlasStatusValid(e.status))
		out.errors.Add("event " + e.id + ": unknown status '" + e.status + "'");
	if(e.type == "proposed" && f.Find(e.target) < 0)
		out.errors.Add("task " + e.subject_id + " names an unknown target version: " + e.target);
	if(e.subject_kind == "road" && (e.type == "connected" || e.type == "disconnected")
	   && (e.from.IsEmpty() || e.to.IsEmpty()))
		out.errors.Add("event " + e.id + ": a road event needs both endpoints in the snapshot");
}

// ---------------------------------------------------------------------------
// State replay.
// ---------------------------------------------------------------------------

static int FindSubject(const Vector<AtlasSubjectState>& v, const String& kind, const String& id)
{
	for(int i = 0; i < v.GetCount(); i++)
		if(v[i].kind == kind && v[i].id == id)
			return i;
	return -1;
}

// Whether event a happens before event b: version order, then time, then id.
static bool EventBefore(const AtlasEvent& a, const AtlasEvent& b, const AtlasFamily& f)
{
	int ia = f.Find(a.vid), ib = f.Find(b.vid);
	if(ia != ib)
		return ia < ib;
	if(a.at != b.at)
		return a.at < b.at;
	return a.id < b.id;
}

// Event indices of the events whose versions are known, sorted by EventBefore.
static Vector<int> OrderedEvents(const Vector<AtlasEvent>& events, const AtlasFamily& f)
{
	Vector<int> order;
	for(int i = 0; i < events.GetCount(); i++)
		if(f.Find(events[i].vid) >= 0)
			order.Add(i);
	for(int i = 1; i < order.GetCount(); i++)
		for(int k = i; k > 0 && EventBefore(events[order[k]], events[order[k - 1]], f); k--) {
			int tmp = order[k];
			order[k] = order[k - 1];
			order[k - 1] = tmp;
		}
	return order;
}

void AtlasSortEvents(Vector<AtlasEvent>& events, const AtlasFamily& f)
{
	for(int i = 1; i < events.GetCount(); i++)
		for(int k = i; k > 0 && EventBefore(events[k], events[k - 1], f); k--) {
			AtlasEvent tmp = pick(events[k]);
			events[k] = pick(events[k - 1]);
			events[k - 1] = pick(tmp);
		}
}

static void Snapshot(AtlasSubjectState& s, const AtlasEvent& e)
{
	if(e.name.GetCount())
		s.name = e.name;
	if(e.from.GetCount())
		s.from = e.from;
	if(e.to.GetCount())
		s.to = e.to;
	if(e.road_kind.GetCount())
		s.road_kind = e.road_kind;
	if(e.label.GetCount())
		s.label = e.label;
}

bool AtlasStateAt(const AtlasMap& head, const AtlasFamily& f, const Vector<AtlasEvent>& events,
                  const String& vid, Vector<AtlasSubjectState>& out, String& err)
{
	int t = f.Find(vid);
	if(t < 0) {
		err = "unknown version: " + vid;
		return false;
	}
	bool is_head = t == f.Head();
	out.Clear();
	for(const AtlasElement& e : head.elements) {
		AtlasSubjectState s;
		s.kind = "element";
		s.id = e.id;
		s.name = e.name;
		s.status = e.status;
		s.in_head = true;
		out.Add(pick(s));
	}
	for(const AtlasRoad& r : head.roads) {
		AtlasSubjectState s;
		s.kind = "road";
		s.id = r.id;
		s.from = r.from;
		s.to = r.to;
		s.road_kind = r.kind;
		s.label = r.label;
		s.in_head = true;
		out.Add(pick(s));
	}
	Vector<int> order = OrderedEvents(events, f);
	// Every subject with an event is scheduled, whatever version the event is at.
	for(int k : order) {
		const AtlasEvent& e = events[k];
		int i = FindSubject(out, e.subject_kind, e.subject_id);
		if(i < 0) {
			AtlasSubjectState s;
			s.kind = e.subject_kind;
			s.id = e.subject_id;
			out.Add(pick(s));
			i = out.GetCount() - 1;
		}
		out[i].scheduled = true;
	}
	// Unscheduled subjects exist at the head only; scheduled ones start absent and follow their events.
	for(AtlasSubjectState& s : out)
		s.present = !s.scheduled && is_head;
	for(int k : order) {
		const AtlasEvent& e = events[k];
		if(f.Find(e.vid) > t)
			continue;
		AtlasSubjectState& s = out[FindSubject(out, e.subject_kind, e.subject_id)];
		Snapshot(s, e);
		if(e.type == "created" || e.type == "connected")
			s.present = true;
		else if(e.type == "removed" || e.type == "disconnected")
			s.present = false;
		else if(e.type == "stage")
			s.stage = e.stage;
		else if(e.type == "status")
			s.status = e.status;
		else if(e.type == "note")
			s.notes++;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Planning rules: literals, the world, and the replay that reports on them.
// ---------------------------------------------------------------------------

int AtlasRules::FindAtom(const String& name) const
{
	for(int i = 0; i < atoms.GetCount(); i++)
		if(atoms[i].name == name)
			return i;
	return -1;
}

int AtlasRules::FindAction(const String& name) const
{
	for(int i = 0; i < actions.GetCount(); i++)
		if(actions[i].name == name)
			return i;
	return -1;
}

int AtlasRules::FindConstraint(const String& id) const
{
	for(int i = 0; i < constraints.GetCount(); i++)
		if(constraints[i].id == id)
			return i;
	return -1;
}

static int IndexOf(const Vector<String>& v, const String& s)
{
	for(int i = 0; i < v.GetCount(); i++)
		if(v[i] == s)
			return i;
	return -1;
}

// The true atoms at one point of the replay, and each element's stage.
struct AtlasWorldState {
	Index<String>             atoms;
	VectorMap<String, String> stages;

	bool Has(const String& name) const
	{
		return atoms.Find(name) >= 0;
	}

	String StageOf(const String& element) const
	{
		int i = stages.Find(element);
		return i >= 0 ? stages[i] : String();
	}

	bool Holds(const String& literal) const
	{
		bool neg = literal.StartsWith("!");
		String name = neg ? literal.Mid(1) : literal;
		bool value;
		if(name.StartsWith("stage:")) {
			Vector<String> p = Split(name, ':');
			int have = AtlasStageRank(p.GetCount() == 3 ? StageOf(p[1]) : String());
			value = p.GetCount() == 3 && have >= 0 && have >= AtlasStageRank(p[2]);
		}
		else
			value = Has(name);
		return neg ? !value : value;
	}

	// A plain literal sets its atom; a negated one clears it. Stage literals are not effects.
	void Apply(const String& literal)
	{
		bool neg = literal.StartsWith("!");
		String name = neg ? literal.Mid(1) : literal;
		if(name.StartsWith("stage:"))
			return;
		int i = atoms.Find(name);
		if(neg && i >= 0)
			atoms.Remove(i);
		else if(!neg && i < 0)
			atoms.Add(name);
	}
};

static bool IsDerivedAtom(const String& name)
{
	return name.StartsWith("stage:") || name.StartsWith("task:");
}

static void Report(Vector<AtlasRoadmapMessage>& out, const char *severity, const String& vid,
                   const String& kind, const String& id, const char *rule, const String& text)
{
	AtlasRoadmapMessage m;
	m.severity = severity;
	m.vid = vid;
	m.subject_kind = kind;
	m.subject_id = id;
	m.rule = rule;
	m.text = text;
	out.Add(pick(m));
}

// An atom an action sets is declared by that action, even when the registry has no entry for it.
static bool SetByAction(const AtlasRules& rules, const String& name)
{
	for(const AtlasAction& a : rules.actions)
		if(IndexOf(a.effects, name) >= 0)
			return true;
	return false;
}

// An undeclared atom is a warning: the rules may name a fact before anything sets it.
static void CheckAtomDeclared(const String& literal, const AtlasRules& rules, Vector<AtlasRoadmapMessage>& out,
                              const String& vid, const String& kind, const String& id)
{
	String name = literal.StartsWith("!") ? literal.Mid(1) : literal;
	if(IsDerivedAtom(name) || rules.FindAtom(name) >= 0 || SetByAction(rules, name))
		return;
	Report(out, "warning", vid, kind, id, "undeclared-atom", "atom " + name + " is not declared in the rules");
}

static bool InScope(const AtlasConstraint& c, int i, const AtlasFamily& f)
{
	int from = c.from_vid.IsEmpty() ? 0 : f.Find(c.from_vid);
	int to = c.to_vid.IsEmpty() ? f.versions.GetCount() - 1 : f.Find(c.to_vid);
	if(from < 0 || to < 0)
		return false;
	return i >= from && i <= to;
}

// The dependency roads run from the element they must wait for to the element that waits. Returns the
// element ids on a cycle of those roads (empty when the graph is acyclic).
static bool DependencyCycle(const AtlasMap& head, String& on)
{
	Vector<String> nodes;
	for(const AtlasRoad& r : head.roads)
		if(r.kind == "dependency") {
			if(IndexOf(nodes, r.from) < 0) nodes.Add(r.from);
			if(IndexOf(nodes, r.to) < 0) nodes.Add(r.to);
		}
	Vector<int> indegree;
	for(int i = 0; i < nodes.GetCount(); i++)
		indegree.Add(0);
	for(const AtlasRoad& r : head.roads)
		if(r.kind == "dependency")
			indegree[IndexOf(nodes, r.to)]++;
	// Kahn: repeatedly remove nodes with no unresolved dependency; whatever is left is on a cycle.
	Vector<bool> done;
	for(int i = 0; i < nodes.GetCount(); i++)
		done.Add(false);
	bool progress = true;
	while(progress) {
		progress = false;
		for(int i = 0; i < nodes.GetCount(); i++)
			if(!done[i] && indegree[i] == 0) {
				done[i] = true;
				progress = true;
				for(const AtlasRoad& r : head.roads)
					if(r.kind == "dependency" && r.from == nodes[i])
						indegree[IndexOf(nodes, r.to)]--;
			}
	}
	on.Clear();
	for(int i = 0; i < nodes.GetCount(); i++)
		if(!done[i]) {
			if(on.GetCount()) on << ", ";
			on << nodes[i];
		}
	return on.GetCount() > 0;
}

void AtlasReplayRoadmap(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                        const Vector<AtlasEvent>& events, Vector<AtlasRoadmapMessage>& out,
                        Vector<Vector<String>>& worlds)
{
	out.Clear();
	worlds.Clear();

	// Declared atoms: every literal the rules, the contracts and the events name.
	for(const String& lit : rules.initial)
		CheckAtomDeclared(lit, rules, out, String(), "rules", String());
	for(const AtlasConstraint& c : rules.constraints) {
		for(const String& lit : c.when)
			CheckAtomDeclared(lit, rules, out, c.from_vid, "constraint", c.id);
		for(const String& lit : c.must)
			CheckAtomDeclared(lit, rules, out, c.from_vid, "constraint", c.id);
	}
	for(const AtlasVersion& v : f.versions)
		for(const String& lit : v.requires)
			CheckAtomDeclared(lit, rules, out, v.vid, "version", v.vid);

	// Indices into events, in replay order (events are not copied: they hold vectors).
	Vector<int> order = OrderedEvents(events, f);

	VectorMap<String, String> target;  // task id -> the version it must be done for
	for(int k : order)
		if(events[k].subject_kind == "task" && events[k].type == "proposed")
			target.GetAdd(events[k].subject_id) = events[k].target;

	AtlasWorldState w;
	for(const String& lit : rules.initial)
		w.Apply(lit);

	for(int i = 0; i < f.versions.GetCount(); i++) {
		const AtlasVersion& v = f.versions[i];
		String label = AtlasVersionLabel(v.code);

		// On entry: the contract, then the accepted tasks targeting this version.
		for(const String& lit : v.requires) {
			String name = lit.StartsWith("!") ? lit.Mid(1) : lit;
			if(rules.FindAtom(name) >= 0 && !rules.atoms[rules.FindAtom(name)].gate)
				Report(out, "warning", v.vid, "version", v.vid, "contract-gate",
				       "version " + label + " requires " + name + ", which is not declared a gate");
			if(!w.Holds(lit))
				Report(out, "error", v.vid, "version", v.vid, "contract-gate",
				       "version " + label + " needs " + lit + " on entry");
		}
		for(int k = 0; k < target.GetCount(); k++)
			if(target[k] == v.vid) {
				String id = target.GetKey(k);
				if(w.Has("task:" + id + ":accepted") && !w.Has("task:" + id + ":done"))
					Report(out, "error", v.vid, "task", id, "contract-task",
					       "task " + id + " must be done for version " + label);
			}

		// The version's own events, in time order.
		for(int k : order) {
			const AtlasEvent& e = events[k];
			if(e.vid != v.vid)
				continue;
			Vector<String> needs, sets;
			for(const String& lit : e.requires) needs.Add(lit);
			for(const String& lit : e.effects) sets.Add(lit);
			if(e.action.GetCount()) {
				int a = rules.FindAction(e.action);
				if(a < 0)
					Report(out, "warning", e.vid, e.subject_kind, e.subject_id, "requires",
					       "event " + e.id + " names unknown action " + e.action);
				else {
					for(const String& lit : rules.actions[a].requires) needs.Add(lit);
					for(const String& lit : rules.actions[a].effects) sets.Add(lit);
				}
			}
			for(const String& lit : needs)
				if(!w.Holds(lit))
					Report(out, "error", e.vid, e.subject_kind, e.subject_id, "requires",
					       "event " + e.id + " needs " + lit + " first");
			for(const String& lit : sets)
				w.Apply(lit);

			if(e.type == "stage") {
				int now = AtlasStageRank(e.stage);
				String before = w.StageOf(e.subject_id);
				int was = AtlasStageRank(before);
				if(was >= 0 && now < was)
					Report(out, "error", e.vid, e.subject_kind, e.subject_id, "stage-order",
					       e.subject_id + " moves back from " + before + " to " + e.stage);
				else if(now > was + 1)
					Report(out, "warning", e.vid, e.subject_kind, e.subject_id, "stage-order",
					       e.subject_id + " skips stages to reach " + e.stage);
				// A dependency must be merged before its dependant reaches implementation.
				if(now >= 2)
					for(const AtlasRoad& r : head.roads)
						if(r.kind == "dependency" && r.to == e.subject_id
						   && AtlasStageRank(w.StageOf(r.from)) < 4)
							Report(out, "warning", e.vid, e.subject_kind, e.subject_id, "dependency-order",
							       e.subject_id + " reaches " + e.stage + " before " + r.from + " is merged");
				w.stages.GetAdd(e.subject_id) = e.stage;
			}
			else if(e.type == "proposed")
				;  // a proposal sets no atom; the contract reads it through the target
			else if(e.subject_kind == "task") {
				String acc = "task:" + e.subject_id + ":accepted";
				String done = "task:" + e.subject_id + ":done";
				if(target.Find(e.subject_id) < 0)
					Report(out, "warning", e.vid, "task", e.subject_id, "orphan", "task " + e.subject_id + " was never proposed");
				if(e.type == "accepted")
					w.Apply(acc);
				else if(e.type == "done") {
					if(!w.Has(acc))
						Report(out, "warning", e.vid, "task", e.subject_id, "task-state", "task " + e.subject_id + " is done before it is accepted");
					w.Apply(done);
				}
				else if(e.type == "dropped") {
					w.Apply("!" + acc);
					w.Apply("!" + done);
				}
				else if(e.type == "reopened")
					w.Apply("!" + done);
			}
		}

		// After the version's events: the constraints whose scope covers it.
		for(const AtlasConstraint& c : rules.constraints) {
			if(!InScope(c, i, f))
				continue;
			bool when = true;
			for(const String& lit : c.when)
				when = when && w.Holds(lit);
			if(!when)
				continue;
			for(const String& lit : c.must)
				if(!w.Holds(lit))
					Report(out, "error", v.vid, "constraint", c.id, "constraint",
					       "constraint " + c.id + " needs " + lit + " at version " + label);
		}

		Vector<String> snapshot;
		for(int k = 0; k < w.atoms.GetCount(); k++)
			snapshot.Add(w.atoms[k]);
		worlds.Add(pick(snapshot));
	}

	// Cycles among the dependency roads, and head subjects no event has scheduled yet.
	String cycle;
	if(f.versions.GetCount() && DependencyCycle(head, cycle))
		Report(out, "error", f.versions[f.Head()].vid, "road", cycle, "dependency-cycle",
		       "dependency roads form a cycle through " + cycle);
	String head_vid = f.versions.GetCount() ? f.versions[f.Head()].vid : String();
	for(const AtlasElement& e : head.elements) {
		bool scheduled = false;
		for(const AtlasEvent& ev : events)
			if(ev.subject_kind == "element" && ev.subject_id == e.id)
				scheduled = true;
		if(!scheduled)
			Report(out, "warning", head_vid, "element", e.id, "unscheduled",
			       "element " + e.id + " has no event; it exists at the head only");
	}
}

END_UPP_NAMESPACE
