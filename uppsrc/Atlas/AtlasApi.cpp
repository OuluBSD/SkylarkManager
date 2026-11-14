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
	if(kind == "element")
		return AtlasElementVisibleTo(r, head_key, id);
	return AtlasElementVisibleTo(r, head_key, from) && AtlasElementVisibleTo(r, head_key, to);
}

// ---------------------------------------------------------------------------
// Version list.
// ---------------------------------------------------------------------------

static void VersionsGet(WebRequest& r)
{
	if(!ApiAuth(r, false))
		return;
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
	if(!ApiAuth(r, false))
		return;
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
	ValueArray list;
	for(const AtlasSubjectState& s : states)
		if(s.present && SubjectVisible(r, f.head_key, s.kind, s.id, s.from, s.to))
			list.Add(ParseJSON(StoreAsJson(s)));
	ValueMap out;
	out.Add("ok", true);
	out.Add("vid", vid);
	out.Add("revision", hrev);
	out.Add("subjects", list);
	r.Json(out);
}

static void EventsGet(WebRequest& r)
{
	if(!ApiAuth(r, false))
		return;
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
	String kind = P(r, "subject_kind");
	String subject = P(r, "subject_id");
	String vid = VersionParam(r, f);
	AtlasEvent e = NewEvent(r, vid, kind, subject, type);
	e.stage = P(r, "stage");
	e.status = P(r, "status");
	e.text = P(r, "text");
	e.requires = ListParam(r, "requires");
	e.effects = ListParam(r, "effects");
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

void InstallAtlasApi(EngineSite& site)
{
	SiteRouter& rt = site.Router();
	rt.Add("api/atlas/*/versions", VersionsGet);
	rt.Add("POST", "api/atlas/*/init", VersionsInit);
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

	CommandManifest& cm = TheWebEngine().Manifest();
	const char *read = "session account";
	const char *write = "admin";
	cm.Add(site.id, site.title, HttpCommand("atlas-versions", "GET", "api/atlas/*/versions", read,
	        "json", "The version list of a map family and its head", { "map:string:required" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-init", "POST", "api/atlas/*/init", write,
	        "json", "Start a family's version list from one document", { "map:string:required", "head:string:required" }));
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
	        "json", "Add a note, a stage or a status event to an element or road",
	        { "map:string:required", "subject_kind:string:required", "subject_id:string:required", "type:string:required",
	          "vid:string", "stage:string", "status:string", "text:text", "requires:string", "effects:string" }));
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
}

END_UPP_NAMESPACE
