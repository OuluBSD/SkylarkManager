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

// A version is one int32 code: major*1000000 + minor*1000 + patch, each part 0..999, so the largest
// is 999.999.999 = 999999999. -1 is "no version". v1.0.0 = 1000000, v1.2.3 = 1002003.
const int ATLAS_VERSION_UNSET = -1;
const int ATLAS_VERSION_MAX   = 999999999;

// "a.b.c", with an optional leading 'v'. Returns the code, or ATLAS_VERSION_UNSET when malformed.
int    AtlasVersionCode(const String& label);
// "a.b.c" for a valid code.
String AtlasVersionLabel(int code);
bool   AtlasVersionValid(int code);

// One version of a map family. The vid is stable; the code can change, and events refer to vid only.
struct AtlasVersion : Moveable<AtlasVersion> {
	String         vid;
	int            code = ATLAS_VERSION_UNSET;
	Vector<String> requires;   // the contract: literals that must hold on entry to this version

	void Jsonize(JsonIO& io) { io("vid", vid)("code", code)("requires", requires); }
};

// The version list of one map family ("pkr"). versions are ascending by code. The last one is the
// head: the head document holds its topology, and every earlier version is rebuilt by replaying events.
struct AtlasFamily : Moveable<AtlasFamily> {
	String               head_key;   // document key of the head map, e.g. "pkr-v0.0.1"
	Vector<AtlasVersion> versions;

	void Jsonize(JsonIO& io) { io("head", head_key)("versions", versions); }

	int Find(const String& vid) const;  // index in versions, -1 when absent
	int Head() const { return versions.GetCount() - 1; }
};

// Inserts a label, bumping every version at or above it at the insert level: inserting 0.0.2 moves
// the old 0.0.2 to 0.0.3 and the old 0.0.3 to 0.0.4; inserting 0.1.0 moves 0.1.x to 0.2.x. Nothing
// changes when the label is malformed, the vid exists, or a bump would pass 999 in a part.
bool AtlasInsertVersion(AtlasFamily& f, const String& label, const String& vid, String& err);
// Relabels one version. Refused, with nothing changed, when another version already has the code.
bool AtlasRenameVersion(AtlasFamily& f, const String& vid, const String& label, String& err);

// One change to one subject at one version. The snapshot fields keep a removed or disconnected
// subject readable at the versions before it went away, since its document entry is gone.
struct AtlasEvent : Moveable<AtlasEvent> {
	String id;
	String at;            // ISO-8601 UTC
	String actor;
	String vid;
	String subject_kind;  // element | road
	String subject_id;
	String type;          // created | removed | stage | status | note | connected | disconnected | modified
	String stage;         // for stage events
	String status;        // for status events
	String text;          // multi-line note
	String name;          // snapshot: element name
	String from, to;      // snapshot: road endpoints
	String road_kind;     // snapshot: road kind
	String label;         // snapshot: road label
	Vector<String> requires;  // atom literals the event needs; "!name" needs it false
	Vector<String> effects;   // atom literals the event sets; "!name" clears it
	String action;            // a named action from the rules; its requires/effects apply too
	String target;            // task: the version the task must be done for
	String link;              // task: "element:<id>" or "road:<id>"
	String assignee;          // task: who is responsible (a username or a bot session); free text

	void Jsonize(JsonIO& io) {
		io("id", id)("at", at)("actor", actor)("vid", vid)("subject_kind", subject_kind)
		  ("subject_id", subject_id)("type", type)("stage", stage)("status", status)("text", text)
		  ("name", name)("from", from)("to", to)("road_kind", road_kind)("label", label)
		  ("requires", requires)("effects", effects)("action", action)("target", target)("link", link)
		  ("assignee", assignee);
	}
};

// Stages, in the pull-request order: planned -> implementation_research -> implementation -> trials -> merge_to_master.
bool AtlasStageValid(const String& stage);
int  AtlasStageRank(const String& stage);     // position in the order, -1 when not a stage
bool AtlasEventTypeValid(const String& subject_kind, const String& type);

// Markdown for notes and task threads, rendered to HTML. The text is escaped first, so no markup from
// it survives; the subset is headings, paragraphs, lists, emphasis, code and http(s) links only.
String AtlasMarkdown(const String& text);

// Planning rules. Atoms are named booleans; a literal is "name" (must hold) or "!name" (must not).
// Derived: "stage:<element>:<stage>" holds when the element is at that stage or later; task atoms
// "task:<id>:accepted" and "task:<id>:done" are set by task events. Undeclared atoms only warn.
struct AtlasAtomDef : Moveable<AtlasAtomDef> {
	String name;
	String text;
	bool   gate = false;   // may appear in a version's contract

	void Jsonize(JsonIO& io) { io("name", name)("text", text)("gate", gate); }
};

struct AtlasAction : Moveable<AtlasAction> {
	String         name;
	String         text;
	Vector<String> requires;
	Vector<String> effects;

	void Jsonize(JsonIO& io) { io("name", name)("text", text)("requires", requires)("effects", effects); }
};

// When every "when" literal holds, every "must" literal must hold. from_vid and to_vid bound the
// versions it applies to (empty means open).
struct AtlasConstraint : Moveable<AtlasConstraint> {
	String         id;
	String         text;
	Vector<String> when;
	Vector<String> must;
	String         from_vid;
	String         to_vid;

	void Jsonize(JsonIO& io) {
		io("id", id)("text", text)("when", when)("must", must)("from_vid", from_vid)("to_vid", to_vid);
	}
};

struct AtlasRules : Moveable<AtlasRules> {
	Vector<String>          initial;      // atoms true at the start
	Vector<AtlasAtomDef>    atoms;
	Vector<AtlasAction>     actions;
	Vector<AtlasConstraint> constraints;

	void Jsonize(JsonIO& io) {
		io("initial", initial)("atoms", atoms)("actions", actions)("constraints", constraints);
	}

	int FindAtom(const String& name) const;
	int FindAction(const String& name) const;
	int FindConstraint(const String& id) const;
};

// One finding of the replay. Errors and warnings are advisory: nothing is refused for them.
struct AtlasRoadmapMessage : Moveable<AtlasRoadmapMessage> {
	String severity;      // error | warning
	String vid;
	String subject_kind;
	String subject_id;
	String rule;          // contract-gate | contract-task | requires | constraint | stage-order | dependency-order | dependency-cycle | unscheduled | undeclared-atom | orphan | task-state
	String text;

	void Jsonize(JsonIO& io) {
		io("severity", severity)("vid", vid)("subject_kind", subject_kind)("subject_id", subject_id)
		  ("rule", rule)("text", text);
	}
};

// Replays the roadmap: versions in order, each one's contract on entry, then its events, then its constraints.
// worlds[i] holds the true atoms after version i. Messages are appended to out; nothing is refused.
void AtlasReplayRoadmap(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                        const Vector<AtlasEvent>& events, Vector<AtlasRoadmapMessage>& out,
                        Vector<Vector<String>>& worlds);
void AtlasValidateEvent(const AtlasEvent& e, const AtlasFamily& f, const AtlasMap& head, AtlasCheck& out);

// The state of one subject at one version.
struct AtlasSubjectState : Moveable<AtlasSubjectState> {
	String kind, id, name;
	String from, to, road_kind, label;
	String stage, status;
	bool   present = false;   // exists at the version
	bool   in_head = false;   // exists in the head document
	bool   scheduled = false; // has at least one event; unscheduled subjects exist at the head only
	int    notes = 0;         // note events up to the version

	void Jsonize(JsonIO& io) {
		io("kind", kind)("id", id)("name", name)("from", from)("to", to)("road_kind", road_kind)
		  ("label", label)("stage", stage)("status", status)("present", present)("in_head", in_head)
		  ("scheduled", scheduled)("notes", notes);
	}
};

// The map as it stood at one version: the events of versions up to and including vid, replayed over
// the head document. Subjects removed from the head still appear when events show them.
// Sorts events by version, then time, then id: the order the replay applies them in.
void AtlasSortEvents(Vector<AtlasEvent>& events, const AtlasFamily& f);

bool AtlasStateAt(const AtlasMap& head, const AtlasFamily& f, const Vector<AtlasEvent>& events,
                  const String& vid, Vector<AtlasSubjectState>& out, String& err);

// What is needed to reach the versions after one. A condition is one literal, one accepted task, or one
// constraint of a version's entry; how lists the steps that would meet it, when it is not met.
struct AtlasNextCondition : Moveable<AtlasNextCondition> {
	String         kind;      // gate | atom | stage | task | constraint
	String         literal;   // the literal, or the task id, or the constraint id
	String         text;      // what is needed, in words
	bool           met = false;
	Vector<String> how;
};
struct AtlasNextVersion : Moveable<AtlasNextVersion> {
	String                       vid;
	String                       label;
	String                       state;   // ready | blocked
	Vector<AtlasNextCondition>   conditions;
};
struct AtlasNextTask : Moveable<AtlasNextTask> {
	String id, title, target, state, assignee;   // state: proposed | accepted | done | dropped
};
struct AtlasNext : Moveable<AtlasNext> {
	String                   from;      // the version the plan starts from; "" is the start of the roadmap
	Vector<AtlasNextVersion> versions;  // the versions after from, in order
	Vector<AtlasNextTask>    todo;      // open tasks for those versions, by target
};
// The plan from a version. An empty from_vid picks the version before the first blocked entry (the head
// when nothing is blocked). Events must be in replay order, as LoadRoadmap leaves them.
bool AtlasWhatsNext(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                    const Vector<AtlasEvent>& events, const String& from_vid, AtlasNext& out, String& err);

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
