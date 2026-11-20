#ifndef _Atlas_AtlasModel_h_
#define _Atlas_AtlasModel_h_

#include <Core/Core.h>

NAMESPACE_UPP

// Atlas map model: plain data, validation, containment queries, the seed
// parser and the deterministic seed layout. Pure: no storage, no HTTP.
// Storage and routes live in AtlasSite.cpp.

bool AtlasIdValid(const String& id);          // [A-Za-z0-9_-], 1..64
bool AtlasKindValid(const String& kind);      // continent | block | place | sea
// A package name in U++ style: CapitalCase letters and digits, starting with a capital. "" is allowed (no package).
bool AtlasPkgValid(const String& pkg);

struct AtlasLocalAtomCheck : Moveable<AtlasLocalAtomCheck> {
	String status;
	String path;
};

// Checks only package directory evidence for built:/realized: atoms. Other atoms are explicitly not checked.
AtlasLocalAtomCheck AtlasCheckLocalAtom(const String& workspace, const String& atom);
String AtlasPlacementReason(const Vector<String>& entry);
bool AtlasStatusValid(const String& status);  // hot | active | planned | not-ready | future | settled | trouble
bool AtlasRoadKindValid(const String& kind);  // progress | communication | dependency

struct AtlasGoal : Moveable<AtlasGoal> {
	bool   picked = false;
	String note;

	void Jsonize(JsonIO& io) { io("picked", picked)("note", note); }
};

// One thing a user can do with the features a minor group delivers: a CLI command, a GUI control, an API call or an
// overlay action. Carried by the element that is the group's user-interface block.
struct AtlasUiAction : Moveable<AtlasUiAction> {
	String name;      // the action as the user knows it, an id such as train-omaha4
	String text;      // what it does, one line
	String medium;    // cli | gui | api | overlay
	String feature;   // the contract feature (its element name) the action exposes

	void Jsonize(JsonIO& io) { io("name", name)("text", text)("medium", medium)("feature", feature); }
};

struct AtlasElement : Moveable<AtlasElement> {
	String    id;
	String    kind;
	String    name;
	String    parent;      // "" = directly on the world plane
	String    status;
	bool      protected_block = false; // writes need an authorized unlock event
	int       x = 0, y = 0, w = 0, h = 0; // relative to the parent plane
	AtlasGoal goal;
	String    notes;
	String    updated_at;
	String    pkg;         // the U++ package the element implements (CapitalCase); a virtual group for data; "" for structure
	Vector<AtlasUiAction> ui; // the user-interface block: what a user can do with the features this element delivers

	void Jsonize(JsonIO& io) {
		io("id", id)("kind", kind)("name", name)("parent", parent)("status", status)("protected", protected_block)
		  ("x", x)("y", y)("w", w)("h", h)("goal", goal)("notes", notes)("updated_at", updated_at)("pkg", pkg)("ui", ui);
	}
};

// A copy of an element; a Vector is not copied by assignment, so the ui actions are copied one by one.
AtlasElement AtlasCopyElement(const AtlasElement& e);

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

// The version list of one map family ("demo"). versions are ascending by code. The last one is the
// head: the head document holds its topology, and every earlier version is rebuilt by replaying events.
struct AtlasFamily : Moveable<AtlasFamily> {
	String               head_key;   // key of the head map, the family name, e.g. "demo"
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
	bool processing = false;  // task: a machine run (dataset processing, training, evaluation), not worker work

	void Jsonize(JsonIO& io) {
		io("id", id)("at", at)("actor", actor)("vid", vid)("subject_kind", subject_kind)
		  ("subject_id", subject_id)("type", type)("stage", stage)("status", status)("text", text)
		  ("name", name)("from", from)("to", to)("road_kind", road_kind)("label", label)
		  ("requires", requires)("effects", effects)("action", action)("target", target)("link", link)
		  ("assignee", assignee)("processing", processing);
	}
};

// A short-lived worker claim is a separate revisioned record, not an AtlasEvent.
struct AtlasClaim : Moveable<AtlasClaim> {
	String element_id;
	String target_vid;
	String owner;
	Time   acquired_at;
	int    lease_seconds = 0;
	String revision; // WebStorage envelope revision; not part of the JSON payload

	void Jsonize(JsonIO& io) {
		io("element_id", element_id)("target_vid", target_vid)("owner", owner)
		  ("acquired_at", acquired_at)("lease_seconds", lease_seconds);
	}
};

bool AtlasClaimLeaseAlive(const AtlasClaim& claim, Time now);
bool AtlasClaimWriteAuthorized(bool authenticated, bool can_edit);
bool AtlasValidateProtectedWrite(const AtlasMap& before, const AtlasMap& after, const Vector<AtlasEvent>& events,
                                 const Vector<AtlasClaim>& claims, const String& actor, Time now, String& error);

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

// A unit of work. Its entry is requires, or entry_none when it truly needs nothing; its outcome is effects.
struct AtlasAction : Moveable<AtlasAction> {
	String         name;
	String         text;
	Vector<String> requires;
	Vector<String> effects;
	bool           entry_none = false;
	bool           processing = false;   // a machine run (dataset processing, training, evaluation), not coding work

	void Jsonize(JsonIO& io) {
		io("name", name)("text", text)("requires", requires)("effects", effects)("entry_none", entry_none)
		  ("processing", processing);
	}
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

// Thresholds of the strictness pass (AtlasStrictness). The owner sets them in the rules data, not in code.
//   max_outcomes           most effects one action may have before "work-too-broad"
//   max_entry_none_share   share of actions allowed to declare entry_none before "entry-none-heavy"
//   max_errors             errors after which the playback stops reporting ("too-many-errors")
//   max_step_elements      how many elements one version may move to merge_to_master before "big-bang"
//   min_delta              the smallest gain a measure series must show to count as improved
struct AtlasStrict : Moveable<AtlasStrict> {
	int    max_outcomes          = 3;
	double max_entry_none_share  = 0.2;
	int    max_errors            = 20;
	int    max_step_elements     = 5;
	double min_delta             = 0.01;

	void Jsonize(JsonIO& io) {
		io("max_outcomes", max_outcomes)("max_entry_none_share", max_entry_none_share)("max_errors", max_errors)
		  ("max_step_elements", max_step_elements)("min_delta", min_delta);
	}
};

struct AtlasRules : Moveable<AtlasRules> {
	Vector<String>          initial;      // atoms true at the start
	Vector<AtlasAtomDef>    atoms;
	Vector<AtlasAction>     actions;
	Vector<AtlasConstraint> constraints;
	AtlasStrict             strict;

	void Jsonize(JsonIO& io) {
		io("initial", initial)("atoms", atoms)("actions", actions)("constraints", constraints)("strict", strict);
	}

	int FindAtom(const String& name) const;
	int FindAction(const String& name) const;
	int FindConstraint(const String& id) const;
};

// A file of the project's repository that the roadmap refers to. Its text is read live from the source root
// (see AtlasFiles.cpp); the record keeps what the roadmap says about it. spans say which lines of the baseline text
// evidence an element, or which lines the element will write (role destination). edits are a patch series: each
// one replaces lines start..end of the text as it stands at its version, and an insert has end = start - 1. A
// marker edit (start 0, end -1, no text) only says that the version writes the file.
struct AtlasFileSpan : Moveable<AtlasFileSpan> {
	String id;
	String element;   // the element the lines evidence or implement
	String role;      // "evidence" or "destination"
	int    start = 0; // 1-based, inclusive; 0 for a file with no text
	int    end = 0;
	String symbol;    // the definition the lines belong to, when known
	String kind;      // "" for lines of text, "class" or "function" for a declaration
	String signature; // one declaration line, names and types only; needs a class or function kind

	void Jsonize(JsonIO& io) {
		io("id", id)("element", element)("role", role)("start", start)("end", end)("symbol", symbol)
		  ("kind", kind)("signature", signature);
	}
};

// The span kinds: "" (lines of text), "class" and "function". They are kept apart from element kinds.
bool AtlasSpanKindValid(const String& kind);
// A signature is one declaration line: no body, so no braces.
bool AtlasSignatureValid(const String& signature);

struct AtlasFileEdit : Moveable<AtlasFileEdit> {
	String vid;
	String element;
	int    start = 0;
	int    end = -1;
	String text;      // the new lines, joined by '\n'
	String note;

	void Jsonize(JsonIO& io) { io("vid", vid)("element", element)("start", start)("end", end)("text", text)("note", note); }
};

struct AtlasFile : Moveable<AtlasFile> {
	String                id;      // the file's id within its family, a store id
	String                source;  // alias of a source root in the project's registry
	String                path;    // relative to the source root
	String                kind;    // "code" (text read from the source) or "artifact" (a file with no text)
	String                commit;  // the source commit the spans were derived at; "" when not known
	Vector<AtlasFileSpan> spans;
	Vector<AtlasFileEdit> edits;

	void Jsonize(JsonIO& io) {
		io("id", id)("source", source)("path", path)("kind", kind)("commit", commit)("spans", spans)("edits", edits);
	}
};

// A relative path inside a source root: not empty, not absolute, no "..", no backslash, no control character.
bool AtlasSafeSourcePath(const String& path);

struct AtlasArtifactCheck : Moveable<AtlasArtifactCheck> {
	String name;
	String kind = "file"; // file | file_list
	String status;   // OK | MISSING | BAD | NOT DECLARED
	String path;     // the path inspected, or a declaration note
	String reason;
	int64  size = -1;
	int64  files_found = 0;
	int64  min_files = 0;
	int64  undersized_files = 0;
};

// Checks only the named artifact atoms against roots and relative paths declared in a manifest. It never writes.
bool AtlasCheckArtifactManifest(const Vector<String>& names, const String& manifest_path,
                               Vector<AtlasArtifactCheck>& out, String& error);
String AtlasArtifactSummaryLine(const Vector<AtlasArtifactCheck>& checks);

// Splits text into lines on '\n'; "" is no lines, and a trailing newline adds no line.
Vector<String> AtlasSplitLines(const String& text);

// Applies one edit to lines, in place. Refused, and lines untouched, when the range does not fit the text as it
// stands. A marker edit changes nothing.
bool AtlasApplyEdit(Vector<String>& lines, const AtlasFileEdit& e, String& err);

// The text of a file at its last edit: base with every edit applied in order.
bool AtlasApplyEdits(const Vector<String>& base, const Vector<AtlasFileEdit>& edits, Vector<String>& out, String& err);

// Checks a file record against the head map and the family: safe id, source and path; ordered spans with an
// existing element; edits of known versions in version order, with existing elements; and a series that applies to
// base, when base is given. An artifact or a planned file (no base) is checked without the series.
bool AtlasValidateFile(const AtlasFile& f, const AtlasMap& head, const AtlasFamily& fam,
                       const Vector<String>& base, bool has_base, AtlasCheck& out);

// One finding of the replay. Errors and warnings are advisory: nothing is refused for them.
struct AtlasRoadmapMessage : Moveable<AtlasRoadmapMessage> {
	String severity;      // error | warning
	String vid;
	String subject_kind;
	String subject_id;
	String rule;          // contract-gate | contract-task | requires | constraint | stage-order | dependency-order | dependency-cycle | unscheduled | undeclared-atom | orphan | task-state
	                      // strictness: entry-undeclared | entry-conflict | outcome-missing | work-too-broad | entry-none-heavy |
	                      //             outcome-never-used | vacuous-version | atom-never-required | atom-never-set | big-bang | task-done-no-note
	                      // report: too-many-errors | unacknowledged-warning
	String text;

	void Jsonize(JsonIO& io) {
		io("severity", severity)("vid", vid)("subject_kind", subject_kind)("subject_id", subject_id)
		  ("rule", rule)("text", text);
	}
};

// A comment from a person on one warning. The id is the warning's stable id (AtlasWarningId), so the comment
// stays attached to the same finding across re-checks, and goes stale when the finding changes or goes away.
struct AtlasWarningAck : Moveable<AtlasWarningAck> {
	String warning_id;
	String rule;
	String subject_kind;
	String subject_id;
	String vid;
	String text;
	String comment;
	String actor;
	String at;

	void Jsonize(JsonIO& io) {
		io("warning_id", warning_id)("rule", rule)("subject_kind", subject_kind)("subject_id", subject_id)
		  ("vid", vid)("text", text)("comment", comment)("actor", actor)("at", at);
	}
};

// Replays the roadmap: versions in order, each one's contract on entry, then its events, then its constraints.
// worlds[i] holds the true atoms after version i. Messages are appended to out; nothing is refused.
// The result is cut off after rules.strict.max_errors errors.
void AtlasReplayRoadmap(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                        const Vector<AtlasEvent>& events, Vector<AtlasRoadmapMessage>& out,
                        Vector<Vector<String>>& worlds);
// The replay and the strictness pass, with every finding and no cut-off. The warnings page reads this.
void AtlasRoadmapFindings(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                          const Vector<AtlasEvent>& events, Vector<AtlasRoadmapMessage>& out,
                          Vector<Vector<String>>& worlds);
// The one report that pages and the API show: the findings, then the ledger (a warning without a comment is an
// unacknowledged-warning error at the head), then the cut-off.
void AtlasRoadmapReport(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                        const Vector<AtlasEvent>& events, const Vector<AtlasWarningAck>& acks,
                        Vector<AtlasRoadmapMessage>& out, Vector<Vector<String>>& worlds);
// The stable id of a finding: FNV-1a over its rule, subject, version and text, as 16 hex digits.
String AtlasWarningId(const AtlasRoadmapMessage& m);
// The warning id syntax accepted by the API and local operator tools.
bool AtlasWarningIdValid(const String& id);
// Validates a current warning and builds the stored acknowledgement. http_status preserves the API's 400/404 result.
bool AtlasBuildWarningAck(const Vector<AtlasRoadmapMessage>& findings, const String& id, const String& comment,
                          const String& actor, const String& at, AtlasWarningAck& out, int& http_status, String& error);
// Comments whose warning is no longer among the findings: the plan changed, or the finding was fixed.
Vector<AtlasWarningAck> AtlasStaleAcks(const Vector<AtlasRoadmapMessage>& findings, const Vector<AtlasWarningAck>& acks);
// The strictness pass: appends messages about the rules themselves: unit-of-work shape, unread outcomes, and
// decoration. The replay calls it after itself, so it is never called from inside the replay.
void AtlasStrictness(const AtlasMap& head, const AtlasFamily& f, const AtlasRules& rules,
                     const Vector<AtlasEvent>& events, Vector<AtlasRoadmapMessage>& out);
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
