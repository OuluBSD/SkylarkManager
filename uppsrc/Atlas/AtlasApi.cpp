#include "AtlasSite.h"
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// The roadmap JSON API, for agents (SiteCli) and the web UI alike. Routes sit under
// api/atlas/<family>/. Reads need a signed-in account and a readable map; every
// write needs the "admin" role. A write validates the whole change first, then saves the
// head document under a revision check, then appends its events. Events never
// take the head's revision: notes and stages from several agents do not collide.
// "expected_revision" is optional: when given it must match the current head revision
// (409 otherwise); when left out, the write applies to the current revision.

// SiteCli sends the api/ routes a JSON body, which the request does not merge into its parameters.
// P and IntP read the form or query first, then the JSON body, so both kinds of client work.
static ValueMap ApiBody(WebRequest& r)
{
	String body = r.Body();
	if(body.IsEmpty())
		return ValueMap();
	Value v = ParseJSON(body);
	return v.Is<ValueMap>() ? ValueMap(v) : ValueMap();
}

static String P(WebRequest& r, const char *name)
{
	String v = r.Param(name);
	if(v.GetCount())
		return v;
	ValueMap b = ApiBody(r);
	Value x = b[name];
	if(x.IsNull())
		return String();
	if(x.Is<String>())
		return (String)x;
	if(x.Is<int>() || x.Is<int64>())
		return AsString((int64)x);
	if(x.Is<double>())
		return AsString((double)x);
	return String();
}

static int IntP(WebRequest& r, const char *name)
{
	String v = P(r, name);
	return v.GetCount() ? StrInt(v) : 0;
}

static void ApiFail(WebRequest& r, int code, const String& error)
{
	ValueMap m;
	m.Add("ok", false);
	m.Add("error", error);
	r.Json(m, code);
}

static String ApiJoin(const Vector<String>& v)
{
	StringBuffer b;
	for(int i = 0; i < v.GetCount(); i++) {
		if(i)
			b << "; ";
		b << v[i];
	}
	return String(b);
}

static bool ApiAuth(WebRequest& r, bool edit)
{
	if(!r.HasUser()) {
		ApiFail(r, 401, "not signed in");
		return false;
	}
	if(edit && !AtlasCanEdit(r)) {
		ApiFail(r, 403, "editing needs the admin role on this site");
		return false;
	}
	return true;
}

// Compares the optional expected revision with the current one; empty means "current".
static bool RevisionOk(WebRequest& r, const String& current)
{
	String expected = P(r, "expected_revision");
	if(expected.IsEmpty() || expected == current)
		return true;
	ApiFail(r, 409, "the map changed since you read it; read it again and retry");
	return false;
}

static bool LoadFamily(WebRequest& r, AtlasFamily& f, String& frev)
{
	String err;
	if(!AtlasLoadFamily(r.Site().data_root, r.PathArg(0), f, frev, err)) {
		ApiFail(r, 404, err);
		return false;
	}
	return true;
}

static bool LoadHead(WebRequest& r, const AtlasFamily& f, AtlasMap& head, String& hrev)
{
	String err;
	if(!AtlasLoadMap(r.Site().data_root, f.head_key, head, hrev, err)) {
		ApiFail(r, 500, err);
		return false;
	}
	return true;
}

// The version an event or a state read names; empty "vid" means the head.
static String VersionParam(WebRequest& r, const AtlasFamily& f)
{
	String vid = P(r, "vid");
	if(vid.IsEmpty() && f.versions.GetCount())
		vid = f.versions[f.versions.GetCount() - 1].vid;
	return vid;
}

static String FreshVid(const AtlasFamily& f)
{
	for(int n = f.versions.GetCount() + 1;; n++) {
		String vid = Format("v%d", n);
		if(f.Find(vid) < 0)
			return vid;
	}
}

static AtlasEvent NewEvent(WebRequest& r, const String& vid, const String& subject_kind,
                           const String& subject_id, const String& type)
{
	AtlasEvent e;
	e.id = "ev-" + Format64Hex((int64)(Random64() & 0x7fffffffffffffffULL));
	e.at = AtlasNowIso();
	e.actor = r.User().username;
	e.vid = vid;
	e.subject_kind = subject_kind;
	e.subject_id = subject_id;
	e.type = type;
	return e;
}

static Vector<String> ListParam(WebRequest& r, const char *name)
{
	Vector<String> out;
	for(const String& part : Split(P(r, name), ',')) {
		String p = TrimBoth(part);
		if(p.GetCount())
			out.Add(p);
	}
	return out;
}

static ValueArray WarningsOf(const AtlasCheck& check)
{
	ValueArray w;
	for(const String& s : check.warnings)
		w.Add(s);
	return w;
}

// The family with each version's label beside its code, so no client derives one from the other.
static void ReplyFamily(WebRequest& r, const AtlasFamily& f, const String& rev)
{
	ValueArray list;
	for(const AtlasVersion& v : f.versions) {
		ValueMap row;
		row.Add("vid", v.vid);
		row.Add("code", v.code);
		row.Add("label", AtlasVersionLabel(v.code));
		ValueArray contract;
		for(const String& lit : v.requires)
			contract.Add(lit);
		row.Add("requires", contract);
		list.Add(row);
	}
	ValueMap family;
	family.Add("head_key", f.head_key);
	family.Add("versions", list);
	ValueMap out;
	out.Add("ok", true);
	out.Add("revision", rev);
	out.Add("head", f.versions.IsEmpty() ? String() : f.versions[f.versions.GetCount() - 1].vid);
	out.Add("family", family);
	r.Json(out);
}

// Validates the change, saves the head when it changed (revision-checked), then appends the
// events. Nothing is written unless the whole change validates.
static void Commit(WebRequest& r, const AtlasFamily& f, const String& family, AtlasMap& head,
                   const String& hrev, Vector<AtlasEvent>& events, bool head_changed)
{
	AtlasCheck check;
	if(head_changed)
		AtlasValidate(head, check);
	for(const AtlasEvent& e : events)
		AtlasValidateEvent(e, f, head, check);
	if(!check.Ok()) {
		ApiFail(r, 400, ApiJoin(check.errors));
		return;
	}
	String rev = hrev;
	String err;
	if(head_changed && !AtlasSaveMap(r.Site().data_root, f.head_key, head, hrev, rev, err)) {
		ApiFail(r, err.Find("revision conflict") >= 0 ? 409 : 500, err);
		return;
	}
	ValueArray ids;
	for(const AtlasEvent& e : events) {
		if(!AtlasPutEvent(r.Site().data_root, family, e, err)) {
			ApiFail(r, 500, "the map was saved but event " + e.id + " was not stored: " + err);
			return;
		}
		ids.Add(e.id);
	}
	ValueMap out;
	out.Add("ok", true);
	out.Add("revision", rev);
	out.Add("events", ids);
	out.Add("warnings", WarningsOf(check));
	r.Json(out);
}

// The head document names its version. Keep it equal to the head's label after any change to the list.
static bool SyncHeadVersion(WebRequest& r, const AtlasFamily& f)
{
	AtlasMap head;
	String hrev, err;
	if(!AtlasLoadMap(r.Site().data_root, f.head_key, head, hrev, err)) {
		ApiFail(r, 500, err);
		return false;
	}
	String want = "v" + AtlasVersionLabel(f.versions[f.Head()].code);
	if(head.version == want)
		return true;
	head.version = want;
	String nrev;
	if(!AtlasSaveMap(r.Site().data_root, f.head_key, head, hrev, nrev, err)) {
		ApiFail(r, 500, "the version list changed but the head document did not: " + err);
		return false;
	}
	return true;
}

// Whether an element or road in a state or event listing may be shown to this reader.
static bool SubjectVisible(WebRequest& r, const String& head_key, const String& kind, const String& id,
                           const String& from, const String& to)
{
	if(kind == "task")
		return true;
	if(kind == "element")
		return AtlasElementVisibleTo(r, head_key, id);
	return AtlasElementVisibleTo(r, head_key, from) && AtlasElementVisibleTo(r, head_key, to);
}

// ---------------------------------------------------------------------------
// Planning rules, tasks and the check.
// ---------------------------------------------------------------------------

// The family's rules; a missing document is an empty rule set.
static bool LoadRules(WebRequest& r, AtlasRules& rules, String& rrev)
{
	String err;
	if(!AtlasLoadRules(r.Site().data_root, r.PathArg(0), rules, rrev, err)) {
		ApiFail(r, 500, err);
		return false;
	}
	return true;
}

// Optional revision check for the rules, under its own parameter so it never confuses the head's.
static bool RulesRevisionOk(WebRequest& r, const String& current)
{
	String expected = P(r, "expected_rules");
	if(expected.IsEmpty() || expected == current)
		return true;
	ApiFail(r, 409, "the rules changed since you read them; read them again and retry");
	return false;
}

static bool SaveRules(WebRequest& r, const AtlasRules& rules, const String& rrev)
{
	String nrev, err;
	if(!AtlasSaveRules(r.Site().data_root, r.PathArg(0), rules, rrev, nrev, err)) {
		ApiFail(r, err.Find("revision conflict") >= 0 ? 409 : 500, err);
		return false;
	}
	ValueMap out;
	out.Add("ok", true);
	out.Add("rules_revision", nrev);
	out.Add("rules", ParseJSON(StoreAsJson(rules)));
	r.Json(out);
	return true;
}

// The rules of the family, with their revision, for a reader.
static void RulesGet(WebRequest& r)
{
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	if(!AtlasMapReadable(r, f.head_key)) {
		ApiFail(r, 404, "map not found");
		return;
	}
	AtlasRules rules;
	String rrev;
	if(!LoadRules(r, rules, rrev))
		return;
	ValueMap out;
	out.Add("ok", true);
	out.Add("rules_revision", rrev);
	out.Add("rules", ParseJSON(StoreAsJson(rules)));
	r.Json(out);
}

static int IndexOfName(const Vector<String>& v, const String& name)
{
	for(int i = 0; i < v.GetCount(); i++)
		if(v[i] == name)
			return i;
	return -1;
}

static bool ValidName(const String& name)
{
	return AtlasIdValid(name) && name.Find(':') < 0;
}

static void AtomSet(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasRules rules;
	String rrev;
	if(!LoadRules(r, rules, rrev) || !RulesRevisionOk(r, rrev))
		return;
	String name = P(r, "name");
	if(!ValidName(name)) {
		ApiFail(r, 400, "an atom name is letters, digits, '_' or '-': " + name);
		return;
	}
	int i = rules.FindAtom(name);
	if(i < 0) {
		AtlasAtomDef a;
		a.name = name;
		rules.atoms.Add(pick(a));
		i = rules.atoms.GetCount() - 1;
	}
	if(P(r, "text").GetCount())
		rules.atoms[i].text = P(r, "text");
	if(P(r, "gate").GetCount())
		rules.atoms[i].gate = P(r, "gate") == "1";
	String start = P(r, "initial");
	int at = IndexOfName(rules.initial, name);
	if(start == "1" && at < 0)
		rules.initial.Add(name);
	if(start == "0" && at >= 0)
		rules.initial.Remove(at);
	SaveRules(r, rules, rrev);
}

static void AtomDelete(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasRules rules;
	String rrev;
	if(!LoadRules(r, rules, rrev) || !RulesRevisionOk(r, rrev))
		return;
	String name = P(r, "name");
	int i = rules.FindAtom(name);
	if(i < 0) {
		ApiFail(r, 404, "unknown atom: " + name);
		return;
	}
	rules.atoms.Remove(i);
	int at = IndexOfName(rules.initial, name);
	if(at >= 0)
		rules.initial.Remove(at);
	SaveRules(r, rules, rrev);
}

static void ActionSet(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasRules rules;
	String rrev;
	if(!LoadRules(r, rules, rrev) || !RulesRevisionOk(r, rrev))
		return;
	String name = P(r, "name");
	if(!ValidName(name)) {
		ApiFail(r, 400, "an action name is letters, digits, '_' or '-': " + name);
		return;
	}
	int i = rules.FindAction(name);
	if(i < 0) {
		AtlasAction a;
		a.name = name;
		rules.actions.Add(pick(a));
		i = rules.actions.GetCount() - 1;
	}
	if(P(r, "text").GetCount())
		rules.actions[i].text = P(r, "text");
	if(P(r, "requires").GetCount())
		rules.actions[i].requires = ListParam(r, "requires");
	if(P(r, "effects").GetCount())
		rules.actions[i].effects = ListParam(r, "effects");
	SaveRules(r, rules, rrev);
}

static void ActionDelete(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasRules rules;
	String rrev;
	if(!LoadRules(r, rules, rrev) || !RulesRevisionOk(r, rrev))
		return;
	int i = rules.FindAction(P(r, "name"));
	if(i < 0) {
		ApiFail(r, 404, "unknown action: " + P(r, "name"));
		return;
	}
	rules.actions.Remove(i);
	SaveRules(r, rules, rrev);
}

static void ConstraintSet(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasRules rules;
	String rrev;
	if(!LoadRules(r, rules, rrev) || !RulesRevisionOk(r, rrev))
		return;
	String id = P(r, "id");
	if(!ValidName(id)) {
		ApiFail(r, 400, "a constraint id is letters, digits, '_' or '-': " + id);
		return;
	}
	for(const char *bound : { "from_vid", "to_vid" })
		if(P(r, bound).GetCount() && f.Find(P(r, bound)) < 0) {
			ApiFail(r, 400, String("unknown version in ") + bound + ": " + P(r, bound));
			return;
		}
	int i = rules.FindConstraint(id);
	if(i < 0) {
		AtlasConstraint c;
		c.id = id;
		rules.constraints.Add(pick(c));
		i = rules.constraints.GetCount() - 1;
	}
	AtlasConstraint& c = rules.constraints[i];
	if(P(r, "text").GetCount()) c.text = P(r, "text");
	if(P(r, "when").GetCount()) c.when = ListParam(r, "when");
	if(P(r, "must").GetCount()) c.must = ListParam(r, "must");
	if(P(r, "from_vid").GetCount()) c.from_vid = P(r, "from_vid");
	if(P(r, "to_vid").GetCount()) c.to_vid = P(r, "to_vid");
	SaveRules(r, rules, rrev);
}

static void ConstraintDelete(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasRules rules;
	String rrev;
	if(!LoadRules(r, rules, rrev) || !RulesRevisionOk(r, rrev))
		return;
	int i = rules.FindConstraint(P(r, "id"));
	if(i < 0) {
		ApiFail(r, 404, "unknown constraint: " + P(r, "id"));
		return;
	}
	rules.constraints.Remove(i);
	SaveRules(r, rules, rrev);
}

// A version's contract: the literals that must hold on entry to it. Saved with the version list.
static void ContractSet(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev) || !RevisionOk(r, frev))
		return;
	int i = f.Find(P(r, "vid"));
	if(i < 0) {
		ApiFail(r, 404, "unknown version: " + P(r, "vid"));
		return;
	}
	f.versions[i].requires = ListParam(r, "requires");
	String nrev, err;
	if(!AtlasSaveFamily(r.Site().data_root, r.PathArg(0), f, frev, nrev, err)) {
		ApiFail(r, err.Find("revision conflict") >= 0 ? 409 : 500, err);
		return;
	}
	ReplyFamily(r, f, nrev);
}

// A task event: proposed (with its target), accepted, done, dropped, reopened, or a note.
static void TaskEvent(WebRequest& r, const char *type)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasMap head;
	String hrev;
	if(!LoadHead(r, f, head, hrev))
		return;
	String id = P(r, "id");
	AtlasEvent e = NewEvent(r, VersionParam(r, f), "task", id, type);
	e.text = P(r, "text");
	if(String(type) == "proposed") {
		e.name = P(r, "title");
		e.target = P(r, "target");
		e.link = P(r, "link");
	}
	if(String(type) == "proposed" || String(type) == "assigned")
		e.assignee = P(r, "assignee");
	Vector<AtlasEvent> events;
	events.Add(pick(e));
	Commit(r, f, r.PathArg(0), head, hrev, events, false);
}

static void TaskPropose(WebRequest& r) { TaskEvent(r, "proposed"); }
static void TaskAccept(WebRequest& r)  { TaskEvent(r, "accepted"); }
static void TaskDone(WebRequest& r)    { TaskEvent(r, "done"); }
static void TaskDrop(WebRequest& r)    { TaskEvent(r, "dropped"); }
static void TaskReopen(WebRequest& r)  { TaskEvent(r, "reopened"); }
static void TaskAssign(WebRequest& r)  { TaskEvent(r, "assigned"); }
static void TaskNote(WebRequest& r)    { TaskEvent(r, "note"); }

// The replay's messages. ?vid= keeps the ones at one version. Never refuses anything: findings only.
static void CheckGet(WebRequest& r)
{
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	if(!AtlasMapReadable(r, f.head_key)) {
		ApiFail(r, 404, "map not found");
		return;
	}
	AtlasMap head;
	String hrev, err;
	if(!LoadHead(r, f, head, hrev))
		return;
	AtlasRules rules;
	String rrev;
	if(!LoadRules(r, rules, rrev))
		return;
	Vector<AtlasEvent> events;
	if(!AtlasLoadEvents(r.Site().data_root, r.PathArg(0), events, err)) {
		ApiFail(r, 500, err);
		return;
	}
	Vector<AtlasRoadmapMessage> messages;
	Vector<Vector<String>> worlds;
	AtlasReplayRoadmap(head, f, rules, events, messages, worlds);
	String only = P(r, "vid");
	ValueArray list;
	int errors = 0, warnings = 0;
	for(const AtlasRoadmapMessage& m : messages) {
		if(only.GetCount() && m.vid != only)
			continue;
		if(m.severity == "error") errors++; else warnings++;
		list.Add(ParseJSON(StoreAsJson(m)));
	}
	ValueMap out;
	out.Add("ok", true);
	out.Add("errors", errors);
	out.Add("warnings", warnings);
	out.Add("messages", list);
	r.Json(out);
}

// ---------------------------------------------------------------------------
// Version list.
// ---------------------------------------------------------------------------

static void VersionsGet(WebRequest& r)
{
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	if(!AtlasMapReadable(r, f.head_key)) {
		ApiFail(r, 404, "map not found");
		return;
	}
	ReplyFamily(r, f, frev);
}

// Creates a family from one existing document, which becomes the head as version v1.
static void VersionsInit(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	String family = r.PathArg(0);
	String head_key = P(r, "head");
	AtlasMap doc;
	String drev, err;
	if(!AtlasLoadMap(r.Site().data_root, head_key, doc, drev, err)) {
		ApiFail(r, 404, err);
		return;
	}
	if(doc.map != family) {
		ApiFail(r, 400, "the head document belongs to map " + doc.map + ", not " + family);
		return;
	}
	int code = AtlasVersionCode(doc.version);
	if(code == ATLAS_VERSION_UNSET || code == 0) {
		ApiFail(r, 400, "the head document's version must read v<a.b.c> with parts 0..999: " + doc.version);
		return;
	}
	AtlasFamily f;
	f.head_key = head_key;
	AtlasVersion first;
	first.vid = "v1";
	first.code = code;
	f.versions.Add(pick(first));
	String rev;
	if(!AtlasCreateFamily(r.Site().data_root, family, f, rev, err)) {
		ApiFail(r, err.Find("already exists") >= 0 ? 409 : 500, err);
		return;
	}
	ReplyFamily(r, f, rev);
}

static void VersionsInsert(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev) || !RevisionOk(r, frev))
		return;
	String vid = P(r, "vid");
	if(vid.IsEmpty())
		vid = FreshVid(f);
	String err;
	if(!AtlasInsertVersion(f, P(r, "label"), vid, err)) {
		ApiFail(r, 400, err);
		return;
	}
	String nrev;
	if(!AtlasSaveFamily(r.Site().data_root, r.PathArg(0), f, frev, nrev, err)) {
		ApiFail(r, err.Find("revision conflict") >= 0 ? 409 : 500, err);
		return;
	}
	if(SyncHeadVersion(r, f))
		ReplyFamily(r, f, nrev);
}

static void VersionsRename(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev) || !RevisionOk(r, frev))
		return;
	String err;
	if(!AtlasRenameVersion(f, P(r, "vid"), P(r, "label"), err)) {
		ApiFail(r, 400, err);
		return;
	}
	String nrev;
	if(!AtlasSaveFamily(r.Site().data_root, r.PathArg(0), f, frev, nrev, err)) {
		ApiFail(r, err.Find("revision conflict") >= 0 ? 409 : 500, err);
		return;
	}
	if(SyncHeadVersion(r, f))
		ReplyFamily(r, f, nrev);
}

// Deletes a non-head version that no event names; the head holds the topology and cannot go.
static void VersionsDelete(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev) || !RevisionOk(r, frev))
		return;
	String vid = P(r, "vid");
	int i = f.Find(vid);
	if(i < 0) {
		ApiFail(r, 404, "unknown version: " + vid);
		return;
	}
	if(i == f.Head()) {
		ApiFail(r, 400, "the head version holds the topology and cannot be deleted");
		return;
	}
	Vector<AtlasEvent> events;
	String err;
	if(!AtlasLoadEvents(r.Site().data_root, r.PathArg(0), events, err)) {
		ApiFail(r, 500, err);
		return;
	}
	int used = 0;
	for(const AtlasEvent& e : events)
		if(e.vid == vid)
			used++;
	if(used) {
		ApiFail(r, 409, Format("%d events are at %s; move or remove them first", used, vid));
		return;
	}
	f.versions.Remove(i);
	String nrev;
	if(!AtlasSaveFamily(r.Site().data_root, r.PathArg(0), f, frev, nrev, err)) {
		ApiFail(r, err.Find("revision conflict") >= 0 ? 409 : 500, err);
		return;
	}
	ReplyFamily(r, f, nrev);
}

// ---------------------------------------------------------------------------
// Reads: the state at a version and the event list.
// ---------------------------------------------------------------------------

static void StateGet(WebRequest& r)
{
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	if(!AtlasMapReadable(r, f.head_key)) {
		ApiFail(r, 404, "map not found");
		return;
	}
	AtlasMap head;
	String hrev, err;
	if(!LoadHead(r, f, head, hrev))
		return;
	Vector<AtlasEvent> events;
	if(!AtlasLoadEvents(r.Site().data_root, r.PathArg(0), events, err)) {
		ApiFail(r, 500, err);
		return;
	}
	String vid = VersionParam(r, f);
	Vector<AtlasSubjectState> states;
	if(!AtlasStateAt(head, f, events, vid, states, err)) {
		ApiFail(r, 400, err);
		return;
	}
	AtlasRules rules;
	String rrev;
	if(!LoadRules(r, rules, rrev))
		return;
	Vector<AtlasRoadmapMessage> messages;
	Vector<Vector<String>> worlds;
	AtlasReplayRoadmap(head, f, rules, events, messages, worlds);
	ValueArray list;
	for(const AtlasSubjectState& s : states)
		if(s.present && SubjectVisible(r, f.head_key, s.kind, s.id, s.from, s.to))
			list.Add(ParseJSON(StoreAsJson(s)));
	ValueArray atoms;
	int at = f.Find(vid);
	if(at >= 0)
		for(const String& a : worlds[at])
			atoms.Add(a);
	ValueMap out;
	out.Add("ok", true);
	out.Add("vid", vid);
	out.Add("revision", hrev);
	out.Add("subjects", list);
	out.Add("worldstate", atoms);
	r.Json(out);
}

static void EventsGet(WebRequest& r)
{
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	if(!AtlasMapReadable(r, f.head_key)) {
		ApiFail(r, 404, "map not found");
		return;
	}
	AtlasMap head;
	String hrev, err;
	if(!LoadHead(r, f, head, hrev))
		return;
	Vector<AtlasEvent> events;
	if(!AtlasLoadEvents(r.Site().data_root, r.PathArg(0), events, err)) {
		ApiFail(r, 500, err);
		return;
	}
	String kind = P(r, "subject_kind"), id = P(r, "subject_id"), vid = P(r, "vid");
	Vector<AtlasEvent> kept;
	for(AtlasEvent& e : events) {
		if(kind.GetCount() && e.subject_kind != kind)
			continue;
		if(id.GetCount() && e.subject_id != id)
			continue;
		if(vid.GetCount() && e.vid != vid)
			continue;
		String from = e.from, to = e.to;
		if(from.IsEmpty() && e.subject_kind == "road") {
			int i = head.FindRoad(e.subject_id);
			if(i >= 0) {
				from = head.roads[i].from;
				to = head.roads[i].to;
			}
		}
		if(!SubjectVisible(r, f.head_key, e.subject_kind, e.subject_id, from, to))
			continue;
		kept.Add(pick(e));
	}
	AtlasSortEvents(kept, f);
	ValueArray list;
	for(const AtlasEvent& e : kept)
		list.Add(ParseJSON(StoreAsJson(e)));
	ValueMap out;
	out.Add("ok", true);
	out.Add("events", list);
	r.Json(out);
}

// ---------------------------------------------------------------------------
// Writes: events on their own, and changes to the topology with their events.
// ---------------------------------------------------------------------------

// An event on an element or road: a note, a stage, or a status. Topology changes have their own routes.
static void EventAdd(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasMap head;
	String hrev;
	if(!LoadHead(r, f, head, hrev))
		return;
	String type = P(r, "type");
	if(type != "note" && type != "stage" && type != "status") {
		ApiFail(r, 400, "event/add takes note, stage or status; created, removed, connected, disconnected and modified come from the element and road routes");
		return;
	}
	AtlasRules rules;
	String rrev;
	if(!LoadRules(r, rules, rrev))
		return;
	String kind = P(r, "subject_kind");
	String subject = P(r, "subject_id");
	String vid = VersionParam(r, f);
	AtlasEvent e = NewEvent(r, vid, kind, subject, type);
	e.stage = P(r, "stage");
	e.status = P(r, "status");
	e.text = P(r, "text");
	e.requires = ListParam(r, "requires");
	e.effects = ListParam(r, "effects");
	e.action = P(r, "action");
	if(e.action.GetCount() && rules.FindAction(e.action) < 0) {
		ApiFail(r, 400, "unknown action " + e.action + "; declare it with rules/action first");
		return;
	}
	bool head_changed = false;
	// A status at the head is also the head's visible status, so the head document follows it.
	if(type == "status" && f.versions.GetCount() && vid == f.versions[f.Head()].vid && kind == "element") {
		int i = head.FindElement(subject);
		if(i >= 0) {
			if(!RevisionOk(r, hrev))
				return;
			head.elements[i].status = e.status;
			head.elements[i].updated_at = AtlasNowIso();
			head_changed = true;
		}
	}
	Vector<AtlasEvent> events;
	events.Add(pick(e));
	Commit(r, f, r.PathArg(0), head, hrev, events, head_changed);
}

static void ElementCreate(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasMap head;
	String hrev;
	if(!LoadHead(r, f, head, hrev) || !RevisionOk(r, hrev))
		return;
	String vid = VersionParam(r, f);
	AtlasElement el;
	el.name = P(r, "name");
	el.id = AtlasUniqueElementId(head, el.name);
	el.kind = P(r, "kind");
	el.status = P(r, "status");
	el.parent = P(r, "parent");
	el.x = IntP(r, "x");
	el.y = IntP(r, "y");
	el.w = P(r, "w").GetCount() ? IntP(r, "w") : 180;
	el.h = P(r, "h").GetCount() ? IntP(r, "h") : 100;
	el.updated_at = AtlasNowIso();
	head.elements.Add(el);  // a copy: the event below still reads the element's id and name
	AtlasEvent e = NewEvent(r, vid, "element", el.id, "created");
	e.name = el.name;
	e.text = P(r, "text");
	Vector<AtlasEvent> events;
	events.Add(pick(e));
	Commit(r, f, r.PathArg(0), head, hrev, events, true);
}

// Layout and descriptive fields. No event: an element's geometry is not history. Status and stage are events.
static void ElementUpdate(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasMap head;
	String hrev;
	if(!LoadHead(r, f, head, hrev) || !RevisionOk(r, hrev))
		return;
	int i = head.FindElement(P(r, "id"));
	if(i < 0) {
		ApiFail(r, 404, "element not found");
		return;
	}
	AtlasElement& el = head.elements[i];
	if(P(r, "name").GetCount()) el.name = P(r, "name");
	if(P(r, "kind").GetCount()) el.kind = P(r, "kind");
	if(P(r, "parent").GetCount()) el.parent = P(r, "parent");
	if(P(r, "x").GetCount()) el.x = IntP(r, "x");
	if(P(r, "y").GetCount()) el.y = IntP(r, "y");
	if(P(r, "w").GetCount()) el.w = IntP(r, "w");
	if(P(r, "h").GetCount()) el.h = IntP(r, "h");
	if(P(r, "notes").GetCount()) el.notes = P(r, "notes");
	if(P(r, "goal_picked").GetCount()) el.goal.picked = P(r, "goal_picked") == "1";
	if(P(r, "goal_note").GetCount()) el.goal.note = P(r, "goal_note");
	el.updated_at = AtlasNowIso();
	Vector<AtlasEvent> none;
	Commit(r, f, r.PathArg(0), head, hrev, none, true);
}

// Removes an element and the roads on it. Each road gets a disconnected event, the element a removed one.
static void ElementRemove(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasMap head;
	String hrev;
	if(!LoadHead(r, f, head, hrev) || !RevisionOk(r, hrev))
		return;
	String id = P(r, "id");
	int i = head.FindElement(id);
	if(i < 0) {
		ApiFail(r, 404, "element not found");
		return;
	}
	if(AtlasChildCount(head, id) > 0) {
		ApiFail(r, 400, "remove the element's children first");
		return;
	}
	String vid = VersionParam(r, f);
	Vector<AtlasEvent> events;
	for(int k = head.roads.GetCount() - 1; k >= 0; k--) {
		const AtlasRoad& rd = head.roads[k];
		if(rd.from != id && rd.to != id)
			continue;
		AtlasEvent d = NewEvent(r, vid, "road", rd.id, "disconnected");
		d.from = rd.from;
		d.to = rd.to;
		d.road_kind = rd.kind;
		d.label = rd.label;
		d.text = "removed with element " + id;
		events.Add(pick(d));
		head.roads.Remove(k);
	}
	AtlasEvent e = NewEvent(r, vid, "element", id, "removed");
	e.name = head.elements[i].name;
	e.text = P(r, "text");
	events.Add(pick(e));
	head.elements.Remove(i);
	Commit(r, f, r.PathArg(0), head, hrev, events, true);
}

static void RoadCreate(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasMap head;
	String hrev;
	if(!LoadHead(r, f, head, hrev) || !RevisionOk(r, hrev))
		return;
	AtlasRoad rd;
	rd.id = AtlasUniqueRoadId(head);
	rd.from = P(r, "from");
	rd.to = P(r, "to");
	rd.kind = P(r, "kind");
	rd.label = P(r, "label");
	head.roads.Add(rd);  // a copy: the event below still reads the road's endpoints
	String vid = VersionParam(r, f);
	AtlasEvent e = NewEvent(r, vid, "road", rd.id, "connected");
	e.from = rd.from;
	e.to = rd.to;
	e.road_kind = rd.kind;
	e.label = rd.label;
	e.text = P(r, "text");
	Vector<AtlasEvent> events;
	events.Add(pick(e));
	Commit(r, f, r.PathArg(0), head, hrev, events, true);
}

static void RoadDisconnect(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasMap head;
	String hrev;
	if(!LoadHead(r, f, head, hrev) || !RevisionOk(r, hrev))
		return;
	int i = head.FindRoad(P(r, "id"));
	if(i < 0) {
		ApiFail(r, 404, "road not found");
		return;
	}
	AtlasEvent e = NewEvent(r, VersionParam(r, f), "road", head.roads[i].id, "disconnected");
	e.from = head.roads[i].from;
	e.to = head.roads[i].to;
	e.road_kind = head.roads[i].kind;
	e.label = head.roads[i].label;
	e.text = P(r, "text");
	head.roads.Remove(i);
	Vector<AtlasEvent> events;
	events.Add(pick(e));
	Commit(r, f, r.PathArg(0), head, hrev, events, true);
}

// Changes a road's kind or label. Its endpoints stay: reconnecting is a disconnect plus a create.
static void RoadModify(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasFamily f;
	String frev;
	if(!LoadFamily(r, f, frev))
		return;
	AtlasMap head;
	String hrev;
	if(!LoadHead(r, f, head, hrev) || !RevisionOk(r, hrev))
		return;
	int i = head.FindRoad(P(r, "id"));
	if(i < 0) {
		ApiFail(r, 404, "road not found");
		return;
	}
	AtlasRoad& rd = head.roads[i];
	if(P(r, "kind").GetCount()) rd.kind = P(r, "kind");
	if(P(r, "label").GetCount()) rd.label = P(r, "label");
	AtlasEvent e = NewEvent(r, VersionParam(r, f), "road", rd.id, "modified");
	e.from = rd.from;
	e.to = rd.to;
	e.road_kind = rd.kind;
	e.label = rd.label;
	e.text = P(r, "text");
	Vector<AtlasEvent> events;
	events.Add(pick(e));
	Commit(r, f, r.PathArg(0), head, hrev, events, true);
}

// ---------------------------------------------------------------------------
// Routes and SiteCli commands. Every command takes the family first ("map").
// ---------------------------------------------------------------------------

// Map documents for the import tools (BrownBear's scripts/atlas-import.sh): an empty map at a version, and a
// seed that fills it. Same rules as the form routes: admin only, the seed fills an empty map only.
static void MapCreate(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	AtlasMap m;
	m.map = P(r, "map");
	m.version = P(r, "version");
	m.title = P(r, "title");
	int width = IntP(r, "width"), height = IntP(r, "height");
	m.world.width = width > 0 ? width : 2400;
	m.world.height = height > 0 ? height : 1600;
	AtlasCheck check;
	AtlasValidate(m, check);
	String key = AtlasDocKey(m.map, m.version);
	if(!JsonFileStore::ValidId(key))
		check.errors.Add("map version has characters the store does not allow: " + m.version);
	if(!check.Ok()) {
		ApiFail(r, 400, ApiJoin(check.errors));
		return;
	}
	AtlasMap existing;
	String rev, err;
	if(AtlasLoadMap(r.Site().data_root, key, existing, rev, err)) {
		ApiFail(r, 409, "a map with that id and version already exists");
		return;
	}
	// The level is stored before the map, so a private map is never readable as public, not even empty.
	String level_name = P(r, "level");
	if(level_name.GetCount()) {
		ContentLevel level;
		if(!ContentLevelParse(level_name, level)) {
			ApiFail(r, 400, "level must be public, protected or private");
			return;
		}
		ContentPolicyStore& store = ContentPolicyFor(r.Site().data_root);
		if(store.LoadError().GetCount()) {
			ApiFail(r, 500, store.LoadError());
			return;
		}
		ContentPolicy p = store.Record(key);
		p.level = level;
		store.SetRecord(key, p);
		String policy_err;
		if(!store.Save(policy_err)) {
			ApiFail(r, 500, policy_err);
			return;
		}
	}
	String new_rev;
	if(!AtlasSaveMap(r.Site().data_root, key, m, String(), new_rev, err)) {
		ApiFail(r, 500, err);
		return;
	}
	ValueMap out;
	out.Add("ok", true);
	out.Add("key", key);
	out.Add("revision", new_rev);
	out.Add("level", level_name.GetCount() ? level_name : String("public"));
	r.Json(out);
}

static void MapSeed(WebRequest& r)
{
	if(!ApiAuth(r, true))
		return;
	String key = AtlasDocKey(P(r, "map"), P(r, "version"));
	AtlasMap m;
	String rev, err;
	if(!AtlasLoadMap(r.Site().data_root, key, m, rev, err)) {
		ApiFail(r, 404, err);
		return;
	}
	AtlasCheck check;
	if(!AtlasParseSeed(P(r, "seed"), m, check)) {
		ApiFail(r, 400, ApiJoin(check.errors));
		return;
	}
	String new_rev;
	if(!AtlasSaveMap(r.Site().data_root, key, m, rev, new_rev, err)) {
		ApiFail(r, err.Find("revision") >= 0 ? 409 : 500, err);
		return;
	}
	ValueMap out;
	out.Add("ok", true);
	out.Add("key", key);
	out.Add("revision", new_rev);
	out.Add("elements", m.elements.GetCount());
	out.Add("roads", m.roads.GetCount());
	r.Json(out);
}

void InstallAtlasApi(EngineSite& site)
{
	SiteRouter& rt = site.Router();
	rt.Add("api/atlas/*/versions", VersionsGet);
	rt.Add("POST", "api/atlas/*/init", VersionsInit);
	rt.Add("POST", "api/atlas/create", MapCreate);
	rt.Add("POST", "api/atlas/seed", MapSeed);
	rt.Add("POST", "api/atlas/*/versions/insert", VersionsInsert);
	rt.Add("POST", "api/atlas/*/versions/rename", VersionsRename);
	rt.Add("POST", "api/atlas/*/versions/delete", VersionsDelete);
	rt.Add("api/atlas/*/state", StateGet);
	rt.Add("api/atlas/*/events", EventsGet);
	rt.Add("POST", "api/atlas/*/event/add", EventAdd);
	rt.Add("POST", "api/atlas/*/element/create", ElementCreate);
	rt.Add("POST", "api/atlas/*/element/update", ElementUpdate);
	rt.Add("POST", "api/atlas/*/element/remove", ElementRemove);
	rt.Add("POST", "api/atlas/*/road/create", RoadCreate);
	rt.Add("POST", "api/atlas/*/road/disconnect", RoadDisconnect);
	rt.Add("POST", "api/atlas/*/road/modify", RoadModify);
	rt.Add("api/atlas/*/rules", RulesGet);
	rt.Add("POST", "api/atlas/*/rules/atom", AtomSet);
	rt.Add("POST", "api/atlas/*/rules/atom/delete", AtomDelete);
	rt.Add("POST", "api/atlas/*/rules/action", ActionSet);
	rt.Add("POST", "api/atlas/*/rules/action/delete", ActionDelete);
	rt.Add("POST", "api/atlas/*/rules/constraint", ConstraintSet);
	rt.Add("POST", "api/atlas/*/rules/constraint/delete", ConstraintDelete);
	rt.Add("POST", "api/atlas/*/contract", ContractSet);
	rt.Add("POST", "api/atlas/*/task/propose", TaskPropose);
	rt.Add("POST", "api/atlas/*/task/accept", TaskAccept);
	rt.Add("POST", "api/atlas/*/task/done", TaskDone);
	rt.Add("POST", "api/atlas/*/task/drop", TaskDrop);
	rt.Add("POST", "api/atlas/*/task/reopen", TaskReopen);
	rt.Add("POST", "api/atlas/*/task/assign", TaskAssign);
	rt.Add("POST", "api/atlas/*/task/note", TaskNote);
	rt.Add("api/atlas/*/check", CheckGet);

	CommandManifest& cm = TheWebEngine().Manifest();
	const char *read = "session account";
	const char *write = "admin";
	cm.Add(site.id, site.title, HttpCommand("atlas-versions", "GET", "api/atlas/*/versions", read,
	        "json", "The version list of a map family and its head", { "map:string:required" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-init", "POST", "api/atlas/*/init", write,
	        "json", "Start a family's version list from one document", { "map:string:required", "head:string:required" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-map-create", "POST", "api/atlas/create", write,
	        "json", "Create an empty map document at a version (admin); level sets its visibility before any content",
	        { "map:string:required", "version:string:required", "title:string", "width:int", "height:int", "level:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-map-seed", "POST", "api/atlas/seed", write,
	        "json", "Fill an empty map document with a seed (admin)",
	        { "map:string:required", "version:string:required", "seed:text:required" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-version-insert", "POST", "api/atlas/*/versions/insert", write,
	        "json", "Insert a version label, bumping the versions at or above it", { "map:string:required", "label:string:required", "vid:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-version-rename", "POST", "api/atlas/*/versions/rename", write,
	        "json", "Give a version another label", { "map:string:required", "vid:string:required", "label:string:required" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-version-delete", "POST", "api/atlas/*/versions/delete", write,
	        "json", "Delete a version no event uses (not the head)", { "map:string:required", "vid:string:required" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-state", "GET", "api/atlas/*/state", read,
	        "json", "The elements and roads present at a version (default head)", { "map:string:required", "vid:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-events", "GET", "api/atlas/*/events", read,
	        "json", "Events, filtered by subject or version", { "map:string:required", "subject_kind:string", "subject_id:string", "vid:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-event-add", "POST", "api/atlas/*/event/add", write,
	        "json", "Add a note, a stage or a status event to an element or road; action runs a named rule action",
	        { "map:string:required", "subject_kind:string:required", "subject_id:string:required", "type:string:required",
	          "vid:string", "stage:string", "status:string", "text:text", "requires:string", "effects:string",
	          "action:string", "expected_revision:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-element-create", "POST", "api/atlas/*/element/create", write,
	        "json", "Create an element at a version (writes a created event)",
	        { "map:string:required", "name:string:required", "kind:string:required", "status:string:required",
	          "parent:string", "x:int", "y:int", "w:int", "h:int", "vid:string", "text:text" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-element-update", "POST", "api/atlas/*/element/update", write,
	        "json", "Change an element's layout or description (no event)",
	        { "map:string:required", "id:string:required", "name:string", "kind:string", "parent:string",
	          "x:int", "y:int", "w:int", "h:int", "notes:text", "goal_picked:string", "goal_note:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-element-remove", "POST", "api/atlas/*/element/remove", write,
	        "json", "Remove an element (its roads are disconnected, with events)",
	        { "map:string:required", "id:string:required", "vid:string", "text:text" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-road-create", "POST", "api/atlas/*/road/create", write,
	        "json", "Connect two elements at a version (writes a connected event)",
	        { "map:string:required", "from:string:required", "to:string:required", "kind:string:required",
	          "label:string", "vid:string", "text:text" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-road-disconnect", "POST", "api/atlas/*/road/disconnect", write,
	        "json", "Disconnect a road (writes a disconnected event with its endpoints)",
	        { "map:string:required", "id:string:required", "vid:string", "text:text" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-road-modify", "POST", "api/atlas/*/road/modify", write,
	        "json", "Change a road's kind or label (writes a modified event)",
	        { "map:string:required", "id:string:required", "kind:string", "label:string", "vid:string", "text:text" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-rules", "GET", "api/atlas/*/rules", read,
	        "json", "The planning rules: atoms, initial world state, actions, constraints", { "map:string:required" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-atom-set", "POST", "api/atlas/*/rules/atom", write,
	        "json", "Declare or change an atom; initial=1 puts it in the starting world, 0 takes it out",
	        { "map:string:required", "name:string:required", "text:text", "gate:string", "initial:string", "expected_rules:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-atom-delete", "POST", "api/atlas/*/rules/atom/delete", write,
	        "json", "Remove an atom from the rules", { "map:string:required", "name:string:required", "expected_rules:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-action-set", "POST", "api/atlas/*/rules/action", write,
	        "json", "Declare or change an action: its requires and effects apply to every event that names it",
	        { "map:string:required", "name:string:required", "text:text", "requires:string", "effects:string", "expected_rules:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-action-delete", "POST", "api/atlas/*/rules/action/delete", write,
	        "json", "Remove an action", { "map:string:required", "name:string:required", "expected_rules:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-constraint-set", "POST", "api/atlas/*/rules/constraint", write,
	        "json", "Declare or change a constraint: when every when-literal holds, every must-literal must",
	        { "map:string:required", "id:string:required", "text:text", "when:string", "must:string",
	          "from_vid:string", "to_vid:string", "expected_rules:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-constraint-delete", "POST", "api/atlas/*/rules/constraint/delete", write,
	        "json", "Remove a constraint", { "map:string:required", "id:string:required", "expected_rules:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-contract-set", "POST", "api/atlas/*/contract", write,
	        "json", "Set the literals a version requires on entry (its gates and contract)",
	        { "map:string:required", "vid:string:required", "requires:string", "expected_revision:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-task-propose", "POST", "api/atlas/*/task/propose", write,
	        "json", "Propose a task that must be done for a target version",
	        { "map:string:required", "id:string:required", "title:string:required", "target:string:required",
	          "link:string", "assignee:string", "vid:string", "text:text" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-task-accept", "POST", "api/atlas/*/task/accept", write,
	        "json", "Accept a proposed task (it then counts toward its target)",
	        { "map:string:required", "id:string:required", "vid:string", "text:text" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-task-done", "POST", "api/atlas/*/task/done", write,
	        "json", "Mark an accepted task done", { "map:string:required", "id:string:required", "vid:string", "text:text" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-task-drop", "POST", "api/atlas/*/task/drop", write,
	        "json", "Drop a task: it no longer counts toward its target", { "map:string:required", "id:string:required", "vid:string", "text:text" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-task-reopen", "POST", "api/atlas/*/task/reopen", write,
	        "json", "Reopen a done task", { "map:string:required", "id:string:required", "vid:string", "text:text" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-task-assign", "POST", "api/atlas/*/task/assign", write,
	        "json", "Make someone responsible for a task (a username or a bot session; empty clears it)",
	        { "map:string:required", "id:string:required", "assignee:string", "vid:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-task-note", "POST", "api/atlas/*/task/note", write,
	        "json", "Add a note to a task's thread (markdown)",
	        { "map:string:required", "id:string:required", "text:text", "vid:string" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-check", "GET", "api/atlas/*/check", read,
	        "json", "The roadmap findings: contract, requires, constraint, stage and dependency messages (never refuses)",
	        { "map:string:required", "vid:string" }));
}

END_UPP_NAMESPACE
