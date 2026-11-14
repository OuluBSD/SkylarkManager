#ifndef _Atlas_AtlasModel_h_
#define _Atlas_AtlasModel_h_

#include <Core/Core.h>

NAMESPACE_UPP

// Atlas map model: plain data, validation, containment queries, the seed
// parser and the deterministic seed layout. Pure: no storage, no HTTP.
// Storage and routes live in AtlasSite.cpp.

bool AtlasIdValid(const String& id);          // [A-Za-z0-9_-], 1..64
bool AtlasKindValid(const String& kind);      // continent | block | place | sea
bool AtlasStatusValid(const String& status);  // hot | active | planned | not-ready | future | settled | trouble
bool AtlasRoadKindValid(const String& kind);  // progress | communication | dependency

struct AtlasGoal : Moveable<AtlasGoal> {
	bool   picked = false;
	String note;

	void Jsonize(JsonIO& io) { io("picked", picked)("note", note); }
};

struct AtlasElement : Moveable<AtlasElement> {
	String    id;
	String    kind;
	String    name;
	String    parent;      // "" = directly on the world plane
	String    status;
	int       x = 0, y = 0, w = 0, h = 0; // relative to the parent plane
	AtlasGoal goal;
	String    notes;
	String    updated_at;

	void Jsonize(JsonIO& io) {
		io("id", id)("kind", kind)("name", name)("parent", parent)("status", status)
		  ("x", x)("y", y)("w", w)("h", h)("goal", goal)("notes", notes)("updated_at", updated_at);
	}
};

struct AtlasRoad : Moveable<AtlasRoad> {
	String id;
	String from;
	String to;
	String kind;     // progress | communication | dependency
	String label;

	void Jsonize(JsonIO& io) {
		io("id", id)("from", from)("to", to)("kind", kind)("label", label);
	}
};

struct AtlasWorld : Moveable<AtlasWorld> {
	int width = 2400;
	int height = 1600;

	void Jsonize(JsonIO& io) { io("width", width)("height", height); }
};

// One map file per project version: the document is the version-tied record.
struct AtlasMap : Moveable<AtlasMap> {
	String                map;
	String                version;
	String                title;
	AtlasWorld            world;
	Vector<AtlasElement>  elements;
	Vector<AtlasRoad>     roads;

	void Jsonize(JsonIO& io) {
		io("map", map)("version", version)("title", title)("world", world)
		  ("elements", elements)("roads", roads);
	}

	int FindElement(const String& id) const;   // -1 when absent
	int FindRoad(const String& id) const;      // -1 when absent
};

// A fresh element id derived from a name or an arbitrary base: "e_<slug>",
// suffixed "_2", "_3", ... until it is unique within the map.
String AtlasUniqueElementId(const AtlasMap& m, const String& base);
String AtlasUniqueRoadId(const AtlasMap& m);

// Validation: errors refuse a write; warnings are reported but allowed.
struct AtlasCheck {
	Vector<String> errors;
	Vector<String> warnings;

	bool Ok() const { return errors.IsEmpty(); }
};

void AtlasValidate(const AtlasMap& m, AtlasCheck& out);
// Grows the world so every element lies inside it, with the layout margin. The stored size is a floor.
void AtlasCoverWorld(AtlasMap& m);

// Containment: depth 0 on the world plane; -1 when an ancestor is missing or
// the parent chain cycles.
int  AtlasDepth(const AtlasMap& m, const String& id);
int  AtlasChildCount(const AtlasMap& m, const String& id);
// Root-to-element id chain, inclusive. Empty when the element is unknown.
Vector<String> AtlasPath(const AtlasMap& m, const String& id);
// Element ids whose parent is the given plane ("" = world plane).
Vector<String> AtlasPlaneElements(const AtlasMap& m, const String& plane);

// Seed import. The transcription format (one element or edge per line):
//   * '#' lines and blank lines are ignored.
//   * Containment: 2 spaces per level, then "Name | kind | status | note".
//     Kind defaults to "block" when the element has children, else "place";
//     status defaults to "planned". Names are unique within the seed and may
//     not contain ':' or '|'.
//   * Edges (names, not ids): "A -> B: label" is progress, "A => B: label"
//     is dependency, "A ~> B: label" is communication. The label is optional.
// The seed fills an empty map only, on a deterministic grid, and the result is
// validated. Errors leave m untouched.
bool AtlasParseSeed(const String& text, AtlasMap& m, AtlasCheck& out);

END_UPP_NAMESPACE

#endif
