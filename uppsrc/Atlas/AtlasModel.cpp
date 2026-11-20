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

bool AtlasSpanKindValid(const String& kind)
{
	return kind.IsEmpty() || kind == "class" || kind == "function";
}

bool AtlasSignatureValid(const String& signature)
{
	return signature.Find('{') < 0 && signature.Find('}') < 0 && signature.Find('\n') < 0;
}

bool AtlasPkgValid(const String& pkg)
{
	if(pkg.IsEmpty())
		return true;
	if(pkg.GetCount() > 64 || !IsUpper(pkg[0]))
		return false;
	for(int i = 1; i < pkg.GetCount(); i++)
		if(!IsUpper(pkg[i]) && !IsLower(pkg[i]) && !IsDigit(pkg[i]))
			return false;
	return true;
}

AtlasLocalAtomCheck AtlasCheckLocalAtom(const String& workspace, const String& atom)
{
	AtlasLocalAtomCheck result;
	String pkg;
	if(atom.StartsWith("built:"))
		pkg = atom.Mid(6);
	else if(atom.StartsWith("realized:"))
		pkg = atom.Mid(9);
	else {
		result.status = "NOT CHECKED";
		result.path = atom.StartsWith("artifact:") ? "artifact atom (not inspected)" : "atom not inspected locally";
		return result;
	}

	String dir = AppendFileName(AppendFileName(workspace, "uppsrc"), pkg);
	result.path = AppendFileName(dir, "*.upp");
	bool has_package_file = false;
	if(!pkg.IsEmpty() && AtlasPkgValid(pkg) && DirectoryExists(dir)) {
		FindFile ff(result.path);
		has_package_file = ff;
	}
	result.status = has_package_file ? "OK" : "MISSING";
	return result;
}

String AtlasPlacementReason(const Vector<String>& entry)
{
	return entry.IsEmpty() ? "placed here by the owner (no reason recorded)"
	                       : "earliest version where its entry holds";
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
		if(!AtlasPkgValid(e.pkg))
			out.errors.Add("element " + e.id + " has a package name that is not CapitalCase: " + e.pkg);
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
	// 0.0.0 comes before every version, so it bumps none of them.
	for(const AtlasVersion& v : f.versions) {
		AtlasVersion copy;
		copy.vid = v.vid;
		copy.code = v.code;
		bool same_prefix = code != 0 && v.code / unit == code / unit;
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
		return type == "created" || type == "removed" || type == "stage" || type == "status" || type == "note" || type == "unlock";
	if(subject_kind == "road")
		return type == "connected" || type == "disconnected" || type == "modified" || type == "note";
	if(subject_kind == "task")
		return type == "proposed" || type == "accepted" || type == "done" || type == "dropped"
		    || type == "reopened" || type == "assigned" || type == "note";
	return false;
}

bool AtlasClaimLeaseAlive(const AtlasClaim& claim, Time now)
{
	return claim.lease_seconds > 0 && now < claim.acquired_at + claim.lease_seconds;
}

// Whether the subject must exist in the head document. A removal, a disconnection, and the creation of
// an element or road that later went away do not need it; a change to a live subject does.
static bool NeedsHeadSubject(const String& subject_kind, const String& type)
{
	if(subject_kind == "task")
		return false;
	return type == "note" || type == "stage" || type == "status" || type == "modified" || type == "unlock";
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
	if(e.type == "unlock" && TrimBoth(e.text).IsEmpty())
		out.errors.Add("unlock event " + e.id + " needs a reason");
	if(e.type == "proposed" && f.Find(e.target) < 0)
		out.errors.Add("task " + e.subject_id + " names an unknown target version: " + e.target);
	if(e.subject_kind == "road" && (e.type == "connected" || e.type == "disconnected")
	   && (e.from.IsEmpty() || e.to.IsEmpty()))
		out.errors.Add("event " + e.id + ": a road event needs both endpoints in the snapshot");
}

bool AtlasValidateProtectedWrite(const AtlasMap& before, const AtlasMap& after, const Vector<AtlasEvent>& events,
                                 const Vector<AtlasClaim>& claims, const String& actor, Time now, String& error)
{
	Vector<String> unlocked;
	for(const AtlasEvent& event : events) {
		if(event.type != "unlock")
			continue;
		if(event.subject_kind != "element" || event.actor != actor || TrimBoth(event.text).IsEmpty()) {
			error = "unlock must name an element, the signed-in actor, and a reason";
			return false;
		}
		int old_index = before.FindElement(event.subject_id);
		int new_index = after.FindElement(event.subject_id);
		if(old_index < 0 || !before.elements[old_index].protected_block
		|| (new_index >= 0 && after.elements[new_index].protected_block)) {
			error = "unlock requires a protected element and must clear its protected flag";
			return false;
		}
		bool owns_claim = false;
		for(const AtlasClaim& claim : claims)
			if(claim.element_id == event.subject_id && claim.target_vid == event.vid && claim.owner == actor
			&& AtlasClaimLeaseAlive(claim, now)) {
				owns_claim = true;
				break;
			}
		if(!owns_claim) {
			error = "unlock requires the active claim for this element and version";
			return false;
		}
		bool recorded = false;
		for(const String& id : unlocked)
			if(id == event.subject_id)
				recorded = true;
		if(!recorded)
			unlocked.Add(event.subject_id);
	}

	for(const AtlasElement& old : before.elements) {
		if(!old.protected_block)
			continue;
		int new_index = after.FindElement(old.id);
		bool changed = new_index < 0 || StoreAsJson(old) != StoreAsJson(after.elements[new_index]);
		bool touched = false;
		for(const AtlasEvent& event : events)
			if((event.subject_kind == "element" && event.subject_id == old.id)
			|| (event.subject_kind == "road" && (event.from == old.id || event.to == old.id))) {
				touched = true;
				break;
			}
		bool has_unlock = false;
		for(const String& id : unlocked)
			if(id == old.id)
				has_unlock = true;
		if((changed || touched) && !has_unlock) {
			error = "protected element " + old.id + " requires an authorized unlock event";
			return false;
		}
	}

	for(const AtlasClaim& claim : claims) {
		if(claim.owner == actor || !AtlasClaimLeaseAlive(claim, now))
			continue;
		bool changed = false;
		int old_index = before.FindElement(claim.element_id);
		int new_index = after.FindElement(claim.element_id);
		if(old_index >= 0)
			changed = new_index < 0 || StoreAsJson(before.elements[old_index]) != StoreAsJson(after.elements[new_index]);
		for(const AtlasEvent& event : events)
			if((event.subject_kind == "element" && event.subject_id == claim.element_id && event.vid == claim.target_vid)
			|| (event.subject_kind == "road" && event.vid == claim.target_vid
			    && (event.from == claim.element_id || event.to == claim.element_id))
			|| (event.subject_kind == "task" && event.vid == claim.target_vid
			    && event.link == "element:" + claim.element_id)) {
				changed = true;
				break;
			}
		if(changed) {
			error = "element " + claim.element_id + " is claimed by " + claim.owner + " for " + claim.target_vid;
			return false;
		}
	}

	error.Clear();
	return true;
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
		else if(name.StartsWith("task:") && name.EndsWith(":done"))
			// A planned task (proposed, not dropped) meets the entry of the work that waits for it, as planned work applies
			// its expected outcome. The done atom itself stays the record of real completion, for contract-task.
			value = Has(name) || Has(name.Left(name.GetCount() - 5) + ":planned");
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

// One replay. Like a compiler, it records an error and carries on; AtlasReplayRoadmap decides how many to show.
static void ReplayRoadmap(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
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
			else if(e.type == "proposed") {
				// A proposed task is planned: it meets its gated work until it is dropped. Other proposals set no atom.
				if(e.subject_kind == "task")
					w.Apply("task:" + e.subject_id + ":planned");
			}
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
					w.Apply("!task:" + e.subject_id + ":planned");
					w.Apply("!" + done);
				}
				else if(e.type == "reopened") {
					w.Apply("!" + done);
					w.Apply("task:" + e.subject_id + ":planned");
				}
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

// Like a compiler, the playback records errors and carries on. Once max_errors errors are reached the report stops
// there: later messages are dropped and a too-many-errors error says so. The replay state is still computed in full.
static void StopAfterErrors(Vector<AtlasRoadmapMessage>& out, int max_errors)
{
	int errors = 0;
	for(int i = 0; i < out.GetCount(); i++) {
		if(out[i].severity != "error")
			continue;
		if(++errors <= max_errors)
			continue;
		out.Trim(i);
		Report(out, "error", String(), "rules", String(), "too-many-errors",
		       Format("the playback stopped after %d errors; later findings are not reported", max_errors));
		return;
	}
}

void AtlasRoadmapFindings(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                          const Vector<AtlasEvent>& events, Vector<AtlasRoadmapMessage>& out,
                          Vector<Vector<String>>& worlds)
{
	ReplayRoadmap(head, f, rules, events, out, worlds);
	AtlasStrictness(head, f, rules, events, out);
}

void AtlasReplayRoadmap(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                        const Vector<AtlasEvent>& events, Vector<AtlasRoadmapMessage>& out,
                        Vector<Vector<String>>& worlds)
{
	AtlasRoadmapFindings(head, f, rules, events, out, worlds);
	StopAfterErrors(out, rules.strict.max_errors);
}

String AtlasWarningId(const AtlasRoadmapMessage& m)
{
	String key = m.rule + "|" + m.subject_kind + "|" + m.subject_id + "|" + m.vid + "|" + m.text;
	uint64 h = 14695981039346656037ULL;  // FNV-1a offset basis
	for(int i = 0; i < key.GetCount(); i++) {
		h ^= (byte)key[i];
		h *= 1099511628211ULL;           // FNV-1a prime
	}
	StringBuffer hex;
	for(int shift = 60; shift >= 0; shift -= 4)
		hex.Cat("0123456789abcdef"[(h >> shift) & 15]);
	return String(hex);
}

bool AtlasWarningIdValid(const String& id)
{
	if(id.GetCount() != 16)
		return false;
	for(int i = 0; i < id.GetCount(); i++)
		if(String("0123456789abcdef").Find(id[i]) < 0)
			return false;
	return true;
}

bool AtlasBuildWarningAck(const Vector<AtlasRoadmapMessage>& findings, const String& raw_id, const String& raw_comment,
                          const String& actor, const String& at, AtlasWarningAck& out, int& http_status, String& error)
{
	String id = TrimBoth(raw_id);
	String comment = TrimBoth(raw_comment);
	if(!AtlasWarningIdValid(id)) {
		http_status = 400;
		error = "id must be the warning id from the warnings list";
		return false;
	}
	if(comment.IsEmpty()) {
		http_status = 400;
		error = "a warning needs a comment: say why it is acceptable, or what will change";
		return false;
	}
	for(const AtlasRoadmapMessage& m : findings) {
		if(m.severity != "warning" || AtlasWarningId(m) != id)
			continue;
		out.warning_id = id;
		out.rule = m.rule;
		out.subject_kind = m.subject_kind;
		out.subject_id = m.subject_id;
		out.vid = m.vid;
		out.text = m.text;
		out.comment = comment;
		out.actor = actor;
		out.at = at;
		http_status = 200;
		error.Clear();
		return true;
	}
	http_status = 404;
	error = "no current warning has that id";
	return false;
}

// The ledger: every warning needs a comment from a person. A warning without one is an error at the head, so
// the head stays red until each warning is acknowledged. The error is named after the warning, not its subject.
static void AddLedger(Vector<AtlasRoadmapMessage>& out, const Vector<AtlasWarningAck>& acks, const AtlasFamily& f)
{
	String head_vid = f.versions.IsEmpty() ? String() : f.versions[f.Head()].vid;
	int findings = out.GetCount();
	for(int i = 0; i < findings; i++) {
		if(out[i].severity != "warning")
			continue;
		String id = AtlasWarningId(out[i]);
		bool acked = false;
		for(const AtlasWarningAck& a : acks)
			if(a.warning_id == id)
				acked = true;
		if(!acked)
			Report(out, "error", head_vid, "warning", id, "unacknowledged-warning",
			       out[i].rule + " on " + out[i].subject_kind + " " + out[i].subject_id
			       + " has no comment yet: comment on it, or change the plan");
	}
}

void AtlasRoadmapReport(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                        const Vector<AtlasEvent>& events, const Vector<AtlasWarningAck>& acks,
                        Vector<AtlasRoadmapMessage>& out, Vector<Vector<String>>& worlds)
{
	AtlasRoadmapFindings(head, f, rules, events, out, worlds);
	StopAfterErrors(out, rules.strict.max_errors);  // the cut-off counts plan errors only
	AddLedger(out, acks, f);
}

Vector<AtlasWarningAck> AtlasStaleAcks(const Vector<AtlasRoadmapMessage>& findings, const Vector<AtlasWarningAck>& acks)
{
	Vector<String> live;
	for(const AtlasRoadmapMessage& m : findings)
		if(m.severity == "warning")
			live.Add(AtlasWarningId(m));
	Vector<AtlasWarningAck> stale;
	for(const AtlasWarningAck& a : acks)
		if(IndexOf(live, a.warning_id) < 0)
			stale.Add(a);
	return stale;
}

// ---------------------------------------------------------------------------
// Strictness: the rules themselves must be able to fail. A unit of work is an action: its entry is its requires (or
// an explicit entry_none), its outcome is its effects. Thresholds come from rules.strict.
// ---------------------------------------------------------------------------

static String AtomName(const String& lit)
{
	return lit.StartsWith("!") ? lit.Mid(1) : lit;
}

static bool IsDerivedName(const String& name)
{
	return name.StartsWith("stage:") || name.StartsWith("task:") || name.StartsWith("pool:")
	    || name.StartsWith("metric:") || name.StartsWith("approved:") || name.StartsWith("testset:");
}

static bool SameList(const Vector<String>& a, const Vector<String>& b)
{
	if(a.GetCount() != b.GetCount())
		return false;
	for(int i = 0; i < a.GetCount(); i++)
		if(a[i] != b[i])
			return false;
	return true;
}

// A copy of an element. Its ui vector is copied action by action, as a Vector is not copied by assignment.
AtlasElement AtlasCopyElement(const AtlasElement& e)
{
	AtlasElement x;
	x.id = e.id;
	x.kind = e.kind;
	x.name = e.name;
	x.parent = e.parent;
	x.status = e.status;
	x.protected_block = e.protected_block;
	x.x = e.x;
	x.y = e.y;
	x.w = e.w;
	x.h = e.h;
	x.goal = e.goal;
	x.notes = e.notes;
	x.updated_at = e.updated_at;
	x.pkg = e.pkg;
	for(const AtlasUiAction& u : e.ui) {
		AtlasUiAction c;
		c.name = u.name;
		c.text = u.text;
		c.medium = u.medium;
		c.feature = u.feature;
		x.ui.Add(pick(c));
	}
	return x;
}
void AtlasStrictness(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                     const Vector<AtlasEvent>& events, Vector<AtlasRoadmapMessage>& out)
{
	// Each unit of work: its entry and its outcome must be declared, and it must stay small.
	int entry_none = 0;
	for(const AtlasAction& a : rules.actions) {
		if(a.entry_none)
			entry_none++;
		if(a.requires.IsEmpty() && !a.entry_none)
			Report(out, "error", String(), "action", a.name, "entry-undeclared",
			       "action " + a.name + " declares no entry: give it requires, or set entry_none");
		if(a.entry_none && !a.requires.IsEmpty())
			Report(out, "error", String(), "action", a.name, "entry-conflict",
			       "action " + a.name + " sets entry_none and also has requires");
		if(a.effects.IsEmpty())
			Report(out, "error", String(), "action", a.name, "outcome-missing",
			       "action " + a.name + " declares no outcome: give it effects");
		if(a.effects.GetCount() > rules.strict.max_outcomes)
			Report(out, "error", String(), "action", a.name, "work-too-broad",
			       Format("action %s has %d outcomes; the limit is %d: split it",
			              a.name, a.effects.GetCount(), rules.strict.max_outcomes));
	}
	if(rules.actions.GetCount() && entry_none > rules.strict.max_entry_none_share * rules.actions.GetCount())
		Report(out, "warning", String(), "rules", "actions", "entry-none-heavy",
		       Format("%d of %d actions declare no entry; the limit is %d%%", entry_none, rules.actions.GetCount(),
		              int(rules.strict.max_entry_none_share * 100)));

	// An element that has child elements is a grouping, not a unit of work. One that also runs an action is too broad.
	for(const AtlasElement& el : head.elements) {
		bool has_children = false;
		for(const AtlasElement& c : head.elements)
			if(c.parent == el.id)
				has_children = true;
		if(!has_children)
			continue;
		for(const AtlasEvent& e : events)
			if(e.subject_kind == "element" && e.subject_id == el.id && e.action.GetCount()) {
				Report(out, "error", e.vid, "element", el.id, "work-too-broad",
				       "element " + el.id + " has child elements and also runs action " + e.action + ": split the work");
				break;
			}
	}

	// A version with an empty contract, or one repeating its predecessor's, changes nothing that the rules check.
	for(int i = 0; i < f.versions.GetCount(); i++) {
		const AtlasVersion& v = f.versions[i];
		String label = AtlasVersionLabel(v.code);
		if(v.requires.IsEmpty())
			Report(out, "warning", v.vid, "version", v.vid, "vacuous-version", "version " + label + " has an empty contract");
		else if(i > 0 && SameList(v.requires, f.versions[i - 1].requires))
			Report(out, "warning", v.vid, "version", v.vid, "vacuous-version",
			       "version " + label + " repeats the contract of " + AtlasVersionLabel(f.versions[i - 1].code));
	}

	// Everything the rules, contracts, events and constraints read. An outcome outside this set has no consequence.
	Vector<String> needed, set;
	auto need = [&](const String& lit) {
		String n = AtomName(lit);
		if(IndexOf(needed, n) < 0)
			needed.Add(n);
	};
	for(const AtlasVersion& v : f.versions)
		for(const String& lit : v.requires)
			need(lit);
	for(const AtlasConstraint& c : rules.constraints) {
		for(const String& lit : c.when)
			need(lit);
		for(const String& lit : c.must)
			need(lit);
	}
	for(const AtlasAction& a : rules.actions) {
		for(const String& lit : a.requires)
			need(lit);
		for(const String& lit : a.effects)
			if(!lit.StartsWith("!"))
				set.Add(lit);
	}
	for(const AtlasEvent& e : events) {
		for(const String& lit : e.requires)
			need(lit);
		for(const String& lit : e.effects)
			if(!lit.StartsWith("!"))
				set.Add(lit);
	}
	for(const String& lit : rules.initial)
		set.Add(AtomName(lit));

	// Static approximation: an outcome counts as read when any contract, constraint, event or action reads it, whatever
	// the order. A stronger test would need the replay to record when each read happens.
	for(const AtlasAction& a : rules.actions)
		for(const String& lit : a.effects)
			if(!lit.StartsWith("!") && IndexOf(needed, AtomName(lit)) < 0)
				Report(out, "warning", String(), "action", a.name, "outcome-never-used",
				       "action " + a.name + " sets " + lit + ", which no contract, constraint, event or action reads");

	// Atoms: a gate that nothing needs is decoration; an atom that is needed but set by nothing can never hold.
	for(const AtlasAtomDef& d : rules.atoms)
		if(d.gate && IndexOf(needed, d.name) < 0)
			Report(out, "warning", String(), "atom", d.name, "atom-never-required",
			       "gate " + d.name + " is required by no version, action, event or constraint");
	for(const String& n : needed)
		if(!IsDerivedName(n) && IndexOf(set, n) < 0)
			Report(out, "warning", String(), "atom", n, "atom-never-set",
			       "atom " + n + " is required, but no action, event or initial state sets it");

	// A version that moves more elements to merge_to_master than the limit is a big bang, not a step.
	for(const AtlasVersion& v : f.versions) {
		int merged = 0;
		for(const AtlasEvent& e : events)
			if(e.vid == v.vid && e.subject_kind == "element" && e.type == "stage" && e.stage == "merge_to_master")
				merged++;
		if(merged > rules.strict.max_step_elements)
			Report(out, "warning", v.vid, "version", v.vid, "big-bang",
			       Format("version %s moves %d elements to merge_to_master; the limit is %d",
			              AtlasVersionLabel(v.code), merged, rules.strict.max_step_elements));
	}

	for(const AtlasEvent& e : events)
		if(e.subject_kind == "task" && e.type == "done" && e.text.IsEmpty())
			Report(out, "warning", e.vid, "task", e.subject_id, "task-done-no-note",
			       "task " + e.subject_id + " is done with no result note");

	// Version helpers for the rules below: the label of a vid, and the element an event's subject names.
	auto labelOf = [&](const String& vid) -> String {
		int i = f.Find(vid);
		return i < 0 ? String() : AtlasVersionLabel(f.versions[i].code);
	};
	auto stagedElement = [&](const AtlasEvent& e) -> const AtlasElement * {
		if(e.subject_kind != "element" || e.type != "stage")
			return nullptr;
		for(const AtlasElement& el : head.elements)
			if(el.id == e.subject_id)
				return &el;
		return nullptr;
	};

	// A test gate: an action that sets tested:<group>.
	auto IsTestGate = [&](const AtlasRules& r, const String& name, const String& group) {
		for(const AtlasAction& a : r.actions)
			if(a.name == name && IndexOf(a.effects, "tested:" + group) >= 0)
				return true;
		return false;
	};

	// The user-interface actions name a feature a version contract declares, and their medium is one the page can show.
	Vector<String> contracted;
	for(const AtlasVersion& v : f.versions)
		for(const String& lit : v.requires)
			if(lit.StartsWith("done:"))
				contracted.Add(lit.Mid(5));
	for(const AtlasElement& el : head.elements)
		for(const AtlasUiAction& u : el.ui) {
			if(u.medium != "cli" && u.medium != "gui" && u.medium != "api" && u.medium != "overlay")
				Report(out, "error", String(), "element", el.id, "ui-invalid",
				       "user action " + u.name + " of " + el.id + " has medium '" + u.medium + "'; use cli, gui, api or overlay");
			if(u.name.IsEmpty() || u.text.IsEmpty())
				Report(out, "error", String(), "element", el.id, "ui-invalid",
				       "a user action of " + el.id + " needs a name and a text");
			if(u.feature.GetCount() && IndexOf(contracted, u.feature) < 0)
				Report(out, "error", String(), "element", el.id, "ui-feature-unknown",
				       "user action " + u.name + " exposes " + u.feature + ", which no version contract declares");
		}

	// Every minor group ends with a test gate at its x.999 version, an action that sets tested:<group>.
	Vector<int> groups;
	for(const AtlasVersion& v : f.versions) {
		bool known = false;
		for(int g : groups)
			known = known || g == v.code / 1000;
		if(!known)
			groups.Add(v.code / 1000);
	}
	for(int key : groups) {
		String glabel = Format("%d.%d", key / 1000, key % 1000);
		String gate_vid;
		for(const AtlasVersion& v : f.versions)
			if(v.code == key * 1000 + 999)
				gate_vid = v.vid;
		bool gated = false;
		for(const AtlasAction& a : rules.actions) {
			if(IndexOf(a.effects, "tested:" + glabel) < 0)
				continue;
			for(const AtlasEvent& e : events) {
				const AtlasElement *el = stagedElement(e);
				if(!el || el->name != a.name)
					continue;
				if(gate_vid.GetCount() && e.vid == gate_vid)
					gated = true;
				else
					Report(out, "error", e.vid, "action", a.name, "gate-misplaced",
					       "test gate " + a.name + " is staged in " + labelOf(e.vid) + "; it belongs at " + glabel + ".999");
			}
		}
		// A x.999 version is a beta of the next minor and holds tests only: no task, and no work that is not a test gate.
		if(gate_vid.GetCount())
			for(const AtlasEvent& e : events) {
				if(e.vid != gate_vid || e.type == "created")
					continue;
				if(e.subject_kind == "task")
					Report(out, "error", e.vid, "task", e.subject_id, "gate-version-work",
					       "task " + e.subject_id + " is logged in " + glabel + ".999; that version holds tests only");
				else if(const AtlasElement *el = stagedElement(e); el && !IsTestGate(rules, el->name, glabel))
					Report(out, "error", e.vid, "element", el->id, "gate-version-work",
					       el->name + " is staged in " + glabel + ".999; that version holds tests only");
			}
		if(!gated)
			Report(out, "error", gate_vid, "version", glabel, "group-no-gate",
			       gate_vid.GetCount() ? "group " + glabel + " has no test gate at " + glabel + ".999"
			                           : "group " + glabel + " has no " + glabel + ".999 version for its test gate");
	}

	// A user-interface block is a work element like a feature: its action requires a task, so the block has a person or
	// worker behind it.
	for(const AtlasEvent& e : events) {
		const AtlasElement *el = stagedElement(e);
		if(!el || !el->ui.GetCount())
			continue;
		bool has_task = false;
		for(const AtlasAction& a : rules.actions)
			if(a.name == el->name)
				for(const String& r : a.requires)
					has_task |= r.StartsWith("task:");
		if(!has_task)
			Report(out, "error", e.vid, "element", el->id, "ui-without-task",
			       "user-interface block " + el->name + " requires no task; give its action a task:<id>:done requirement");
	}

	// A feature is a done: literal in a contract. Unless it is a meta-feature (its action only needs other features) or
	// its work is in 0.0.0, it needs a task, so the work it waits for has a person or worker behind it.
	Vector<String> features;
	for(const AtlasVersion& v : f.versions)
		for(const String& lit : v.requires) {
			if(!lit.StartsWith("done:") || IndexOf(features, lit.Mid(5)) >= 0)
				continue;
			String name = lit.Mid(5);
			features.Add(name);
			const AtlasAction *act = nullptr;
			for(const AtlasAction& a : rules.actions)
				if(a.name == name)
					act = &a;
			if(!act)
				continue;
			bool meta = !act->requires.IsEmpty(), has_task = false;
			for(const String& r : act->requires) {
				if(!r.StartsWith("done:"))
					meta = false;
				if(r.StartsWith("task:"))
					has_task = true;
			}
			if(meta || has_task)
				continue;
			bool staged = false, in_zero = false;
			for(const AtlasEvent& e : events)
				if(const AtlasElement *el = stagedElement(e); el && el->name == name) {
					staged = true;
					if(labelOf(e.vid) == "0.0.0")
						in_zero = true;
				}
			if(staged && !in_zero)
				Report(out, "error", v.vid, "action", name, "feature-without-task",
				       "feature " + name + " has no task: give its action a task:<id>:done requirement, or make it a meta-feature");
		}
}

// ---------------------------------------------------------------------------
// Markdown: a safe subset. Escaping comes first, so the syntax below is applied to text that cannot
// contain markup of its own.
// ---------------------------------------------------------------------------

static String MdEscape(const String& s)
{
	StringBuffer o;
	for(int i = 0; i < s.GetCount(); i++) {
		switch(s[i]) {
		case '&': o << "&amp;"; break;
		case '<': o << "&lt;"; break;
		case '>': o << "&gt;"; break;
		case '"': o << "&quot;"; break;
		case '\'': o << "&#39;"; break;
		default: o.Cat(s[i]);
		}
	}
	return String(o);
}

// Inline syntax on already escaped text: `code`, **strong**, *emphasis*, [label](http or https url).
static String MdInline(const String& e)
{
	StringBuffer o;
	int n = e.GetCount();
	int i = 0;
	while(i < n) {
		char c = e[i];
		if(c == '`') {
			int j = e.Find('`', i + 1);
			if(j > i + 1) {
				o << "<code>" << e.Mid(i + 1, j - i - 1) << "</code>";
				i = j + 1;
				continue;
			}
		}
		if(c == '*' && i + 1 < n && e[i + 1] == '*') {
			int j = e.Find("**", i + 2);
			if(j > i + 2) {
				o << "<strong>" << MdInline(e.Mid(i + 2, j - i - 2)) << "</strong>";
				i = j + 2;
				continue;
			}
		}
		if(c == '*') {
			int j = e.Find('*', i + 1);
			if(j > i + 1) {
				o << "<em>" << MdInline(e.Mid(i + 1, j - i - 1)) << "</em>";
				i = j + 1;
				continue;
			}
		}
		if(c == '[') {
			int close = e.Find(']', i + 1);
			if(close > i && close + 1 < n && e[close + 1] == '(') {
				int end = e.Find(')', close + 2);
				if(end > close + 2) {
					String url = e.Mid(close + 2, end - close - 2);
					if(url.StartsWith("http://") || url.StartsWith("https://")) {
						o << "<a href=\"" << url << "\" rel=\"noopener nofollow\" target=\"_blank\">"
						  << MdInline(e.Mid(i + 1, close - i - 1)) << "</a>";
						i = end + 1;
						continue;
					}
				}
			}
		}
		o.Cat(c);
		i++;
	}
	return String(o);
}

String AtlasMarkdown(const String& text)
{
	StringBuffer out;
	StringBuffer para;
	bool in_para = false, in_list = false, in_code = false;
	auto close_para = [&]() {
		if(in_para)
			out << "<p>" << String(para) << "</p>";
		para.Clear();
		in_para = false;
	};
	auto close_list = [&]() {
		if(in_list)
			out << "</ul>";
		in_list = false;
	};
	Vector<String> lines = Split(text, '\n');
	for(String line : lines) {
		if(line.EndsWith("\r"))
			line = line.Left(line.GetCount() - 1);
		if(line.StartsWith("```")) {
			close_para();
			close_list();
			if(in_code)
				out << "</code></pre>";
			else
				out << "<pre><code>";
			in_code = !in_code;
			continue;
		}
		if(in_code) {
			out << MdEscape(line) << "\n";
			continue;
		}
		String trimmed = TrimBoth(line);
		if(trimmed.IsEmpty()) {
			close_para();
			close_list();
			continue;
		}
		if(trimmed[0] == '#') {
			close_para();
			close_list();
			int level = 0;
			while(level < trimmed.GetCount() && trimmed[level] == '#')
				level++;
			int tag = level < 1 ? 3 : level > 2 ? 5 : level + 2;  // h3 for # and ##, h5 for deeper
			out << "<h" << tag << ">" << MdInline(MdEscape(TrimBoth(trimmed.Mid(level)))) << "</h" << tag << ">";
			continue;
		}
		if(trimmed.StartsWith("- ") || trimmed.StartsWith("* ")) {
			close_para();
			if(!in_list)
				out << "<ul>";
			in_list = true;
			out << "<li>" << MdInline(MdEscape(TrimBoth(trimmed.Mid(2)))) << "</li>";
			continue;
		}
		close_list();
		if(in_para)
			para << "<br>";
		para << MdInline(MdEscape(trimmed));
		in_para = true;
	}
	close_para();
	close_list();
	if(in_code)
		out << "</code></pre>";
	return String(out);
}

// ---------------------------------------------------------------------------
// What is next: the entry conditions of the versions after one, against the world that version leaves.
// ---------------------------------------------------------------------------

// The world a version leaves: its replayed atoms and each element's stage there. The start (k < 0) is the
// initial atoms, with no stages.
static AtlasWorldState WorldLeftBy(int k, const AtlasRules& rules, const Vector<Vector<String>>& worlds,
                                   const Vector<AtlasSubjectState>& states)
{
	AtlasWorldState w;
	if(k < 0) {
		for(const String& lit : rules.initial)
			if(!lit.StartsWith("!"))
				w.atoms.Add(lit);
		return w;
	}
	if(k < worlds.GetCount())
		for(const String& name : worlds[k])
			w.atoms.Add(name);
	for(const AtlasSubjectState& st : states)
		if(st.kind == "element" && st.present)
			w.stages.GetAdd(st.id) = st.stage;
	return w;
}

static bool StatesLeftBy(int k, const AtlasMap& head, const AtlasFamily& f, const Vector<AtlasEvent>& events,
                         Vector<AtlasSubjectState>& states, String& err)
{
	states.Clear();
	if(k < 0)
		return true;
	return AtlasStateAt(head, f, events, f.versions[k].vid, states, err);
}

bool AtlasWhatsNext(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                    const Vector<AtlasEvent>& events, const String& from_vid, AtlasNext& out, String& err)
{
	out = AtlasNext();
	int n = f.versions.GetCount();
	Vector<AtlasRoadmapMessage> messages;
	Vector<Vector<String>> worlds;
	AtlasReplayRoadmap(head, f, rules, events, messages, worlds);

	int k = -1;
	if(from_vid.IsEmpty()) {
		int first_blocked = -1;
		for(int j = 0; j < n && first_blocked < 0; j++) {
			Vector<AtlasSubjectState> states;
			if(!StatesLeftBy(j - 1, head, f, events, states, err))
				return false;
			AtlasWorldState w = WorldLeftBy(j - 1, rules, worlds, states);
			for(const String& lit : f.versions[j].requires)
				if(!w.Holds(lit))
					first_blocked = j;
		}
		k = first_blocked < 0 ? n - 1 : first_blocked - 1;
	}
	else {
		k = f.Find(from_vid);
		if(k < 0) {
			err = "unknown version: " + from_vid;
			return false;
		}
	}
	out.from = k < 0 ? String() : f.versions[k].vid;

	Vector<AtlasSubjectState> states;
	if(!StatesLeftBy(k, head, f, events, states, err))
		return false;
	AtlasWorldState w = WorldLeftBy(k, rules, worlds, states);

	// Tasks as they stand at from: only the events of versions up to it count.
	Vector<AtlasNextTask> tasks;
	for(const AtlasEvent& e : events) {
		if(e.subject_kind != "task")
			continue;
		int v = f.Find(e.vid);
		if(v < 0 || v > k)
			continue;
		int i = -1;
		for(int j = 0; j < tasks.GetCount(); j++)
			if(tasks[j].id == e.subject_id)
				i = j;
		if(i < 0) {
			AtlasNextTask t;
			t.id = e.subject_id;
			tasks.Add(pick(t));
			i = tasks.GetCount() - 1;
		}
		AtlasNextTask& t = tasks[i];
		if(e.type == "proposed") {
			t.title = e.name;
			t.target = e.target;
			t.state = "proposed";
			t.assignee = e.assignee;
		}
		else if(e.type == "assigned")
			t.assignee = e.assignee;
		else if(e.type == "accepted" || e.type == "reopened")
			t.state = "accepted";
		else if(e.type == "done" || e.type == "dropped")
			t.state = e.type;
	}

	for(int j = k + 1; j < n; j++) {
		const AtlasVersion& v = f.versions[j];
		AtlasNextVersion nv;
		nv.vid = v.vid;
		nv.label = AtlasVersionLabel(v.code);
		bool ready = true;

		for(const String& lit : v.requires) {
			AtlasNextCondition c;
			c.literal = lit;
			c.met = w.Holds(lit);
			ready = ready && c.met;
			bool neg = lit.StartsWith("!");
			String name = neg ? lit.Mid(1) : lit;
			if(name.StartsWith("stage:")) {
				Vector<String> p = Split(name, ':');
				c.kind = "stage";
				c.text = (p.GetCount() == 3 ? p[1] + " at " + p[2] + " or later" : name);
				if(!c.met && p.GetCount() == 3) {
					bool present = false;
					for(const AtlasSubjectState& st : states)
						present = present || (st.kind == "element" && st.id == p[1] && st.present);
					c.how.Add(present ? "record stage " + p[2] + " for " + p[1] + " (Set stage on its row)"
					                  : "add element " + p[1] + " at a version before " + v.vid);
				}
			}
			else {
				int ai = rules.FindAtom(name);
				bool gate = ai >= 0 && rules.atoms[ai].gate;
				c.kind = gate ? "gate" : "atom";
				c.text = ai >= 0 && rules.atoms[ai].text.GetCount() ? rules.atoms[ai].text : name;
				if(!c.met && !neg) {
					for(const AtlasAction& a : rules.actions) {
						if(IndexOf(a.effects, name) < 0)
							continue;
						Vector<String> missing;
						for(const String& need : a.requires)
							if(!w.Holds(need))
								missing.Add(need);
						c.how.Add(missing.IsEmpty() ? "run " + a.name + " (its requirements hold)"
						                            : "run " + a.name + " after " + Join(missing, ", "));
					}
					if(c.how.IsEmpty())
						c.how.Add("no action in the rules sets " + name);
				}
			}
			nv.conditions.Add(pick(c));
		}

		for(const AtlasNextTask& t : tasks) {
			if(t.target != v.vid || t.state == "proposed" || t.state == "dropped")
				continue;
			AtlasNextCondition c;
			c.kind = "task";
			c.literal = t.id;
			c.text = t.id + (t.title.GetCount() ? " " + t.title : String());
			c.met = t.state == "done";
			ready = ready && c.met;
			if(!c.met)
				c.how.Add("finish " + t.id + (t.assignee.GetCount() ? " (" + t.assignee + ")" : " (nobody is assigned yet)"));
			nv.conditions.Add(pick(c));
		}

		for(const AtlasConstraint& cn : rules.constraints) {
			if(cn.from_vid.GetCount() && f.Find(cn.from_vid) > j)
				continue;
			if(cn.to_vid.GetCount() && f.Find(cn.to_vid) < j)
				continue;
			bool when = true;
			for(const String& lit : cn.when)
				when = when && w.Holds(lit);
			if(!when)
				continue;
			Vector<String> unmet;
			for(const String& lit : cn.must)
				if(!w.Holds(lit))
					unmet.Add(lit);
			if(!unmet.GetCount())
				continue;
			AtlasNextCondition c;
			c.kind = "constraint";
			c.literal = cn.id;
			c.text = cn.text.GetCount() ? cn.text : cn.id;
			c.how.Add("satisfy " + Join(unmet, ", "));
			ready = false;
			nv.conditions.Add(pick(c));
		}

		nv.state = ready ? String("ready") : String("blocked");
		out.versions.Add(pick(nv));
	}

	for(int j = k + 1; j < n; j++)
		for(const AtlasNextTask& t : tasks) {
			if(t.target != f.versions[j].vid || t.state == "done" || t.state == "dropped")
				continue;
			AtlasNextTask x = t;
			out.todo.Add(pick(x));
		}
	return true;
}

// A relative path inside a source root: not empty, not absolute, no "..", no backslash, no control character.
bool AtlasSafeSourcePath(const String& path)
{
	if(path.IsEmpty() || path[0] == '/' || path.Find("..") >= 0 || path.Find('\\') >= 0)
		return false;
	for(int i = 0; i < path.GetCount(); i++)
		if((byte)path[i] < 32)
			return false;
	return true;
}

static bool ArtifactManifestString(const ValueMap& map, const char *key, String& out)
{
	int i = map.Find(key);
	if(i < 0 || !IsString(map.GetValue(i)))
		return false;
	out = map.GetValue(i);
	return !out.IsEmpty();
}

static bool ArtifactManifestInteger(const Value& value, const String& field, int64& number, String& error)
{
	if(value.Is<int>())
		number = (int)value;
	else if(value.Is<int64>())
		number = (int64)value;
	else if(value.Is<double>()) {
		double parsed = value;
		if(parsed < -9223372036854775808.0 || parsed >= 9223372036854775808.0 || (double)(int64)parsed != parsed) {
			error = field + " must be an integer";
			return false;
		}
		number = (int64)parsed;
	}
	else {
		error = field + " must be an integer";
		return false;
	}
	return true;
}

static bool ArtifactManifestMinimum(const ValueMap& map, bool& specified, int64& minimum, String& error)
{
	int i = map.Find("min_bytes");
	specified = i >= 0;
	minimum = 0;
	if(!specified)
		return true;
	if(!ArtifactManifestInteger(map.GetValue(i), "min_bytes", minimum, error))
		return false;
	if(minimum < 0) {
		error = "min_bytes must not be negative";
		return false;
	}
	return true;
}

static bool ArtifactManifestMinFiles(const ValueMap& map, int64& minimum, String& error)
{
	int i = map.Find("min_files");
	if(i < 0) {
		error = "file_list artifact requires min_files of at least 1";
		return false;
	}
	if(!ArtifactManifestInteger(map.GetValue(i), "min_files", minimum, error))
		return false;
	if(minimum < 1) {
		error = "file_list min_files must be at least 1";
		return false;
	}
	return true;
}

bool AtlasCheckArtifactManifest(const Vector<String>& names, const String& manifest_path,
                                Vector<AtlasArtifactCheck>& out, String& error)
{
	out.Clear();
	error.Clear();
	int64 manifest_size = GetFileLength(manifest_path);
	if(manifest_size <= 0 || manifest_size > 1024 * 1024) {
		error = "manifest is missing, empty, or exceeds 1 MiB";
		return false;
	}
	Value document;
	try {
		document = ParseJSON(LoadFile(manifest_path));
	}
	catch(Exc& e) {
		error = String("invalid manifest JSON: ") + e;
		return false;
	}
	if(!document.Is<ValueMap>()) {
		error = "manifest root must be an object";
		return false;
	}
	ValueMap manifest = document;
	Value root_value = manifest.Get("roots");
	Value artifact_value = manifest.Get("artifacts");
	if(!root_value.Is<ValueMap>() || !artifact_value.Is<ValueMap>()) {
		error = "manifest must contain roots and artifacts objects";
		return false;
	}
	ValueMap roots = root_value;
	ValueMap artifacts = artifact_value;
	VectorMap<String, String> root_paths;
	for(int i = 0; i < roots.GetCount(); i++) {
		String name = roots.GetKey(i).ToString();
		Value root_path = roots.GetValue(i);
		if(!AtlasIdValid(name) || !IsString(root_path) || !IsFullPath(root_path.ToString())) {
			error = "each manifest root must map a name to an absolute path";
			return false;
		}
		root_paths.Add(name, NormalizePath(root_path.ToString()));
	}

	for(const String& name : names) {
		if(name.IsEmpty()) {
			error = "artifact name must not be empty";
			return false;
		}
		AtlasArtifactCheck check;
		check.name = name;
		int ai = artifacts.Find(name);
		if(ai < 0) {
			check.status = "NOT DECLARED";
			check.path = "(not declared)";
			check.reason = "not declared in manifest";
			out.Add(pick(check));
			continue;
		}
		Value artifact_value = artifacts.GetValue(ai);
		if(!artifact_value.Is<ValueMap>()) {
			error = "artifact " + name + " must be an object";
			return false;
		}
		ValueMap artifact = artifact_value;
		String root_name, relative;
		int kind_index = artifact.Find("kind");
		if(kind_index >= 0) {
			Value kind_value = artifact.GetValue(kind_index);
			if(!IsString(kind_value)) {
				error = "artifact " + name + " kind must be a string";
				return false;
			}
			check.kind = kind_value.ToString();
		}
		if(check.kind != "file" && check.kind != "file_list") {
			error = "artifact " + name + " kind must be file or file_list";
			return false;
		}
		if(!ArtifactManifestString(artifact, "root", root_name) ||
		   !ArtifactManifestString(artifact, "path", relative) || !AtlasSafeSourcePath(relative)) {
			error = "artifact " + name + " requires a known root and a safe relative path";
			return false;
		}
		String pattern;
		int pattern_index = artifact.Find("pattern");
		if(pattern_index >= 0) {
			Value pattern_value = artifact.GetValue(pattern_index);
			if(!IsString(pattern_value)) {
				error = "artifact " + name + " pattern must be a string suffix";
				return false;
			}
			pattern = pattern_value.ToString();
			if(pattern.IsEmpty() || pattern.Find('/') >= 0 || pattern.Find('\\') >= 0) {
				error = "artifact " + name + " pattern must be a non-empty file-name suffix";
				return false;
			}
		}
		int ri = root_paths.Find(root_name);
		if(ri < 0) {
			error = "artifact " + name + " names an undeclared root " + root_name;
			return false;
		}
		bool has_minimum = false;
		int64 minimum = 0;
		if(!ArtifactManifestMinimum(artifact, has_minimum, minimum, error))
			return false;
		if(check.kind == "file_list" && !ArtifactManifestMinFiles(artifact, check.min_files, error))
			return false;
		String root = root_paths[ri];
		check.path = NormalizePath(AppendFileName(root, relative));
		String prefix = root.EndsWith("/") ? root : root + "/";
		if(!check.path.StartsWith(prefix) && check.path != root) {
			error = "artifact " + name + " path escapes its root";
			return false;
		}
		if(!DirectoryExists(root)) {
			check.status = "MISSING";
			check.reason = check.kind == "file_list" ? "folder not reachable" : "root not reachable";
			out.Add(pick(check));
			continue;
		}
		if(check.kind == "file_list") {
			if(!DirectoryExists(check.path)) {
				check.status = "MISSING";
				check.reason = "folder not reachable";
				out.Add(pick(check));
				continue;
			}
			for(FindFile ff(AppendFileName(check.path, "*")); ff; ff.Next()) {
				if(!ff.IsFile() || (!pattern.IsEmpty() && !ff.GetName().EndsWith(pattern)))
					continue;
				check.files_found++;
				if(has_minimum && GetFileLength(ff.GetPath()) < minimum)
					check.undersized_files++;
			}
			bool too_few = check.files_found < check.min_files;
			bool too_small = check.undersized_files > 0;
			if(too_few || too_small) {
				check.status = "BAD";
				if(too_few)
					check.reason = "found " + AsString(check.files_found) + " matching files; min_files requires " + AsString(check.min_files);
				if(too_small)
					check.reason << (check.reason.IsEmpty() ? String() : "; ") << AsString(check.undersized_files)
					             << " matching files are smaller than min_bytes " << AsString(minimum);
			}
			else
				check.status = "OK";
			out.Add(pick(check));
			continue;
		}
		if(!FileExists(check.path)) {
			check.status = "MISSING";
			check.reason = "file not found";
			out.Add(pick(check));
			continue;
		}
		check.size = GetFileLength(check.path);
		if(check.size < 0) {
			check.status = "MISSING";
			check.reason = "file not reachable";
		}
		else if(has_minimum && check.size < minimum) {
			check.status = "BAD";
			check.reason = AsString(check.size) + " bytes is less than min_bytes " + AsString(minimum);
		}
		else
			check.status = "OK";
		out.Add(pick(check));
	}
	return true;
}

String AtlasArtifactSummaryLine(const Vector<AtlasArtifactCheck>& checks)
{
	int ok = 0, missing = 0, bad = 0, undeclared = 0;
	for(const AtlasArtifactCheck& check : checks) {
		if(check.status == "OK")
			ok++;
		else if(check.status == "MISSING")
			missing++;
		else if(check.status == "BAD")
			bad++;
		else if(check.status == "NOT DECLARED")
			undeclared++;
	}
	return "artifacts: " + AsString(checks.GetCount()) + " (OK " + AsString(ok) + ", MISSING " + AsString(missing) +
	       ", BAD " + AsString(bad) + ", NOT DECLARED " + AsString(undeclared) + ")";
}

static bool IsMarker(const AtlasFileEdit& e)
{
	return e.start == 0 && e.end == -1 && e.text.IsEmpty();
}

Vector<String> AtlasSplitLines(const String& text)
{
	Vector<String> out;
	String cur;
	for(int i = 0; i < text.GetCount(); i++) {
		if(text[i] == '\n') {
			out.Add(cur);
			cur.Clear();
		}
		else
			cur.Cat(text[i]);
	}
	if(!cur.IsEmpty())
		out.Add(cur);
	return out;
}

bool AtlasApplyEdit(Vector<String>& lines, const AtlasFileEdit& e, String& err)
{
	if(IsMarker(e))
		return true;
	int n = lines.GetCount();
	if(e.start < 1 || e.end < e.start - 1 || e.end > n || e.start > n + 1) {
		err = Format("edit of %s replaces lines %d..%d, but the text has %d lines", e.vid, e.start, e.end, n);
		return false;
	}
	Vector<String> out;
	for(int i = 0; i < e.start - 1; i++)
		out.Add(lines[i]);
	for(const String& l : AtlasSplitLines(e.text))
		out.Add(l);
	for(int i = e.end; i < n; i++)
		out.Add(lines[i]);
	lines = pick(out);
	return true;
}

bool AtlasApplyEdits(const Vector<String>& base, const Vector<AtlasFileEdit>& edits, Vector<String>& out, String& err)
{
	out.Clear();
	for(const String& l : base)
		out.Add(l);
	for(const AtlasFileEdit& e : edits)
		if(!AtlasApplyEdit(out, e, err))
			return false;
	return true;
}

bool AtlasValidateFile(const AtlasFile& f, const AtlasMap& head, const AtlasFamily& fam,
                       const Vector<String>& base, bool has_base, AtlasCheck& out)
{
	int before = out.errors.GetCount();
	if(!AtlasIdValid(f.id))
		out.errors.Add("file id must be a valid id: " + f.id);
	if(!AtlasIdValid(f.source))
		out.errors.Add("file " + f.id + ": source must be a valid alias: " + f.source);
	if(!AtlasSafeSourcePath(f.path))
		out.errors.Add("file " + f.id + ": path must be relative, inside the source root: " + f.path);
	if(f.kind != "code" && f.kind != "artifact")
		out.errors.Add("file " + f.id + ": kind must be code or artifact");

	int prev_end = 0;
	for(const AtlasFileSpan& sp : f.spans) {
		String where = "file " + f.id + " span " + sp.id;
		if(!AtlasIdValid(sp.id))
			out.errors.Add(where + ": id must be a valid id");
		if(head.FindElement(sp.element) < 0)
			out.errors.Add(where + ": element " + sp.element + " is not in the goal map");
		if(sp.role != "evidence" && sp.role != "destination")
			out.errors.Add(where + ": role must be evidence or destination");
		if(!AtlasSpanKindValid(sp.kind))
			out.errors.Add(where + ": kind must be empty, class or function");
		if(sp.signature.GetCount() && (sp.kind.IsEmpty() || !AtlasSignatureValid(sp.signature)))
			out.errors.Add(where + ": a signature needs a class or function kind, and holds one line with no body");
		if(sp.start == 0 && sp.end == 0)
			continue;
		if(sp.start < 1 || sp.end < sp.start)
			out.errors.Add(where + ": lines must read start >= 1 and end >= start");
		else if(sp.start <= prev_end)
			out.errors.Add(where + ": overlaps or precedes the span before it; spans are ordered and disjoint");
		else
			prev_end = sp.end;
	}

	int prev_index = -1;
	for(const AtlasFileEdit& e : f.edits) {
		String where = "file " + f.id + " edit of " + e.vid;
		int vi = fam.Find(e.vid);
		if(vi < 0)
			out.errors.Add(where + ": version " + e.vid + " is not in the family");
		else if(vi < prev_index)
			out.errors.Add(where + ": edits must follow version order");
		else
			prev_index = vi;
		if(head.FindElement(e.element) < 0)
			out.errors.Add(where + ": element " + e.element + " is not in the goal map");
		if(!IsMarker(e) && (e.start < 1 || e.end < e.start - 1))
			out.errors.Add(where + ": lines must read start >= 1 and end >= start - 1");
	}

	if(has_base && f.kind == "code") {
		Vector<String> text;
		String err;
		if(!AtlasApplyEdits(base, f.edits, text, err))
			out.errors.Add("file " + f.id + ": " + err);
	}
	return out.errors.GetCount() == before;
}

END_UPP_NAMESPACE
