#include "AtlasSite.h"
#include "AtlasDiagram.h"

NAMESPACE_UPP

String AtlasRoadmapScript();

// The roadmap pages of one map family: versions, timeline, rules, tasks and checks. They read the
// same stored documents and replay as the JSON API, and every change a form makes goes through that
// API (api/atlas/...), so a page and an agent take the same path.

struct RoadmapData {
	AtlasFamily             f;
	AtlasMap                head;
	AtlasRules              rules;
	Vector<AtlasEvent>      events;   // in replay order after LoadRoadmap
	Vector<AtlasRoadmapMessage> messages;   // the report: findings, ledger errors, cut-off
	Vector<AtlasRoadmapMessage> findings;   // every finding, for the warnings page
	Vector<AtlasWarningAck> acks;
	Vector<Vector<String>>  worlds;
};

static bool LoadRoadmap(WebRequest& r, const String& family, RoadmapData& d, String& err)
{
	String frev, hrev, rrev;
	if(!AtlasLoadFamily(r.Site().data_root, family, d.f, frev, err))
		return false;
	if(!AtlasLoadMap(r.Site().data_root, d.f.head_key, d.head, hrev, err))
		return false;
	if(!AtlasLoadRules(r.Site().data_root, family, d.rules, rrev, err))
		return false;
	if(!AtlasLoadEvents(r.Site().data_root, family, d.events, err))
		return false;
	AtlasSortEvents(d.events, d.f);
	String ack_err;
	AtlasLoadAcks(r.Site().data_root, family, d.acks, ack_err);
	AtlasRoadmapReport(d.head, d.f, d.rules, d.events, d.acks, d.messages, d.worlds);
	AtlasRoadmapFindings(d.head, d.f, d.rules, d.events, d.findings, d.worlds);
	return true;
}

static String Api(WebRequest& r, const String& family, const String& path)
{
	return r.Site().Url("api/atlas/" + family + path);
}

static String VidLabel(const AtlasFamily& f, const String& vid)
{
	int i = f.Find(vid);
	return i >= 0 ? AtlasVersionLabel(f.versions[i].code) : vid;
}

static String VidLabel(const RoadmapData& d, const String& vid)
{
	return VidLabel(d.f, vid);
}

// A form that posts to an API route. The page script sends it and reloads on success.
static String ApiForm(const String& action, const String& cls = "ad-form")
{
	return "<form class=\"" + cls + "\" data-api=\"" + AtlasEsc(action) + "\">";
}

static String Field(const String& label, const String& name, const String& value, const String& cls = String())
{
	return "<label>" + AtlasEsc(label) + " <input name=\"" + name + "\" value=\"" + AtlasEsc(value) + "\""
	       + (cls.GetCount() ? " class=\"" + cls + "\"" : String()) + "></label>";
}

static String VersionOptions(const RoadmapData& d, const String& selected, bool blank)
{
	StringBuffer b;
	if(blank)
		b << "<option value=\"\">none</option>";
	for(const AtlasVersion& v : d.f.versions)
		b << "<option value=\"" << AtlasEsc(v.vid) << "\"" << (v.vid == selected ? " selected" : String()) << ">"
		  << AtlasEsc(AtlasVersionLabel(v.code)) << "</option>";
	return String(b);
}

static bool HasName(const Vector<String>& v, const String& name)
{
	for(const String& s : v)
		if(s == name)
			return true;
	return false;
}

static String JoinComma(const Vector<String>& v)
{
	String out;
	for(const String& s : v) {
		if(out.GetCount())
			out << ", ";
		out << s;
	}
	return out;
}

static String Pills(const Vector<String>& v, const char *cls)
{
	StringBuffer b;
	for(const String& s : v)
		b << "<span class=\"ad-pill " << cls << "\">" << AtlasEsc(s) << "</span>";
	return String(b);
}

// ---------------------------------------------------------------------------
// Versions: the ordered list, each version's contract, and the insert form.
// ---------------------------------------------------------------------------

static String VersionsBody(WebRequest& r, const RoadmapData& d, bool edit, const String& family)
{
	StringBuffer b;
	int contracts = 0;
	for(const AtlasVersion& v : d.f.versions)
		if(v.requires.GetCount())
			contracts++;
	int errors = 0, warnings = 0;
	for(const AtlasRoadmapMessage& m : d.messages)
		if(m.severity == "error") errors++; else warnings++;
	b << "<div class=\"ad-cards\">"
	  << "<div class=\"ad-card\"><div class=\"ad-card-value\">" << d.f.versions.GetCount() << "</div><div class=\"ad-card-label\">versions</div></div>"
	  << "<div class=\"ad-card\"><div class=\"ad-card-value\">" << AtlasEsc(VidLabel(d, d.f.versions.IsEmpty() ? String() : d.f.versions[d.f.Head()].vid))
	  << "</div><div class=\"ad-card-label\">head</div></div>"
	  << "<div class=\"ad-card\"><div class=\"ad-card-value\">" << contracts << "</div><div class=\"ad-card-label\">with a contract</div></div>"
	  << "<div class=\"ad-card\"><div class=\"ad-card-value\">" << errors << "</div><div class=\"ad-card-label\">errors</div></div>"
	  << "</div>";

	b << "<section class=\"ad-panel\"><header><h2>Versions</h2><p>Each version is one step. The contract is what must hold when the version is entered.</p></header>"
	  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Version</th><th>Code</th><th>Contract on entry</th><th>Map</th>"
	  << (edit ? "<th>Change</th>" : "") << "</tr>";
	for(const AtlasVersion& v : d.f.versions) {
		b << "<tr><td>" << AtlasEsc(AtlasVersionLabel(v.code)) << (v.vid == d.f.versions[d.f.Head()].vid ? String(" <span class=\"ad-pill is-info\">head</span>") : String())
		  << "</td><td class=\"ad-mono\">" << v.code << "</td><td>";
		b << (v.requires.GetCount() ? Pills(v.requires, "is-warn") : String("<span class=\"ad-empty\">none</span>"));
		if(edit)
			b << ApiForm(Api(r, family, "/contract"))
			  << "<input type=\"hidden\" name=\"vid\" value=\"" << AtlasEsc(v.vid) << "\">"
			  << "<label>requires <input name=\"requires\" class=\"ad-wide\" value=\"" << AtlasEsc(JoinComma(v.requires)) << "\"></label>"
			  << "<button class=\"ad-btn\" type=\"submit\">Save contract</button></form>";
		b << "</td><td><a href=\"" << AtlasEsc(r.Site().Url("atlas/" + d.f.head_key + "?vid=" + v.vid)) << "\">open at this version</a></td>";
		if(edit) {
			b << "<td>" << ApiForm(Api(r, family, "/versions/rename"), "ad-inline")
			  << "<input type=\"hidden\" name=\"vid\" value=\"" << AtlasEsc(v.vid) << "\">"
			  << "<input name=\"label\" value=\"" << AtlasEsc(AtlasVersionLabel(v.code)) << "\" aria-label=\"New label\">"
			  << "<button class=\"ad-btn\" type=\"submit\">Rename</button></form>";
			if(v.vid != d.f.versions[d.f.Head()].vid)
				b << ApiForm(Api(r, family, "/versions/delete"), "ad-inline")
				  << "<input type=\"hidden\" name=\"vid\" value=\"" << AtlasEsc(v.vid) << "\">"
				  << "<button class=\"ad-btn is-danger\" type=\"submit\" data-confirm=\"Delete version " << AtlasEsc(AtlasVersionLabel(v.code))
				  << "? Only possible when no event uses it.\">Delete</button></form>";
			b << "</td>";
		}
		b << "</tr>";
	}
	b << "</table></div>";
	if(edit)
		b << ApiForm(Api(r, family, "/versions/insert"))
		  << Field("Insert version", "label", "0.0.2", "") << "<button class=\"ad-btn is-primary\" type=\"submit\">Insert</button></form>"
		  << "<p class=\"ad-note\">A version inserted at an existing number takes it, and the later versions move up. Insert it where it belongs in the order.</p>";
	b << "</section>";
	return String(b);
}

// ---------------------------------------------------------------------------
// Timeline: every event of the family, newest first, with its version.
// ---------------------------------------------------------------------------

static String EventDetail(const AtlasEvent& e)
{
	StringBuffer b;
	if(e.type == "stage")
		b << "stage " << AtlasEsc(e.stage);
	if(e.type == "status")
		b << "status " << AtlasEsc(e.status);
	if(e.type == "proposed")
		b << "task " << AtlasEsc(e.name) << " for " << AtlasEsc(e.target);
	if(e.action.GetCount())
		b << (b.GetCount() ? String(" &middot; ") : String()) << "action " << AtlasEsc(e.action);
	if(e.requires.GetCount())
		b << (b.GetCount() ? String(" &middot; ") : String()) << "needs " << Pills(e.requires, "is-info");
	if(e.effects.GetCount())
		b << (b.GetCount() ? String(" &middot; ") : String()) << "sets " << Pills(e.effects, "is-ok");
	if(e.text.GetCount())
		b << "<div class=\"ad-text\">" << AtlasEsc(e.text) << "</div>";
	return String(b);
}

// One event's full detail, shown in the split pane when its row is selected.
static String EventTemplate(WebRequest& r, const RoadmapData& d, const AtlasEvent& e, bool edit, const String& family)
{
	StringBuffer b;
	b << "<template id=\"" << AtlasEsc(e.id) << "\"><h2>" << AtlasEsc(e.type) << " on " << AtlasEsc(e.subject_kind)
	  << " " << AtlasEsc(e.subject_id) << "</h2>"
	  << "<dl class=\"ad-dl\"><dt>Version</dt><dd>" << AtlasEsc(VidLabel(d, e.vid)) << "</dd>"
	  << "<dt>At</dt><dd class=\"ad-mono\">" << AtlasEsc(e.at) << "</dd>"
	  << "<dt>By</dt><dd>" << AtlasEsc(e.actor) << "</dd>"
	  << "<dt>Id</dt><dd class=\"ad-mono\">" << AtlasEsc(e.id) << "</dd>";
	if(e.stage.GetCount()) b << "<dt>Stage</dt><dd>" << AtlasEsc(e.stage) << "</dd>";
	if(e.status.GetCount()) b << "<dt>Status</dt><dd>" << AtlasEsc(e.status) << "</dd>";
	if(e.name.GetCount()) b << "<dt>Name</dt><dd>" << AtlasEsc(e.name) << "</dd>";
	if(e.from.GetCount() || e.to.GetCount())
		b << "<dt>Road</dt><dd>" << AtlasEsc(e.from) << " &rarr; " << AtlasEsc(e.to)
		  << (e.label.GetCount() ? " (" + AtlasEsc(e.label) + ")" : String()) << "</dd>";
	if(e.action.GetCount()) b << "<dt>Action</dt><dd>" << AtlasEsc(e.action) << "</dd>";
	if(e.requires.GetCount()) b << "<dt>Needs</dt><dd>" << Pills(e.requires, "is-info") << "</dd>";
	if(e.effects.GetCount()) b << "<dt>Sets</dt><dd>" << Pills(e.effects, "is-ok") << "</dd>";
	if(e.target.GetCount()) b << "<dt>Target</dt><dd>" << AtlasEsc(VidLabel(d, e.target)) << "</dd>";
	if(e.link.GetCount()) b << "<dt>About</dt><dd class=\"ad-mono\">" << AtlasEsc(e.link) << "</dd>";
	b << "</dl>";
	if(e.text.GetCount())
		b << "<div class=\"ad-text ad-note-text\">" << AtlasEsc(e.text) << "</div>";
	if(e.subject_kind == "element" && e.subject_id.GetCount())
		b << "<p><a href=\"" << AtlasEsc(r.Site().Url("atlas/" + d.f.head_key + "/element/" + e.subject_id))
		  << "\">Open " << AtlasEsc(e.subject_id) << " on the map</a></p>";
	if(edit && e.subject_kind == "element" && e.subject_id.GetCount())
		b << ApiForm(Api(r, family, "/event/add"))
		  << "<input type=\"hidden\" name=\"subject_kind\" value=\"element\">"
		  << "<input type=\"hidden\" name=\"subject_id\" value=\"" << AtlasEsc(e.subject_id) << "\">"
		  << "<input type=\"hidden\" name=\"type\" value=\"note\">"
		  << "<label>Add a note on this element <textarea name=\"text\"></textarea></label>"
		  << "<button class=\"ad-btn\" type=\"submit\">Add note</button></form>";
	b << "</template>";
	return String(b);
}

static String TimelineBody(WebRequest& r, const RoadmapData& d, bool edit, const String& family)
{
	StringBuffer b;
	b << "<p class=\"ad-note\">" << d.events.GetCount()
	  << " events. Events are only added, never rewritten. Click a row to see it; drag the bar to resize.</p>"
	  << "<div class=\"ad-split-tools\"><input type=\"search\" placeholder=\"Filter events\" data-filter-for=\"timeline-table\" aria-label=\"Filter events\">"
	  << "<button class=\"ad-btn\" type=\"button\" data-split-toggle=\"timeline\">Layout</button></div>"
	  << "<div class=\"ad-split\" data-split=\"timeline\" data-orient=\"horizontal\">"
	  << "<div class=\"ad-pane-list\"><div class=\"ad-table-wrap\"><table class=\"ad-table\" id=\"timeline-table\" data-sortable>"
	  << "<tr><th data-sort=\"num\" data-sort-default=\"descending\" aria-sort=\"descending\">Version</th>"
	  << "<th data-sort=\"text\">At</th><th data-sort=\"text\">By</th><th data-sort=\"text\">Subject</th>"
	  << "<th data-sort=\"text\">Event</th><th>Detail</th></tr>";
	StringBuffer templates;
	for(int i = d.events.GetCount() - 1; i >= 0; i--) {
		const AtlasEvent& e = d.events[i];
		int code = d.f.Find(e.vid) >= 0 ? d.f.versions[d.f.Find(e.vid)].code : 0;
		String subject = AtlasEsc(e.subject_kind) + ": " + AtlasEsc(e.subject_id);
		b << "<tr data-detail=\"" << AtlasEsc(e.id) << "\" tabindex=\"0\"><td data-v=\"" << code << "\">"
		  << AtlasEsc(VidLabel(d, e.vid)) << "</td><td class=\"ad-mono\" data-v=\"" << AtlasEsc(e.at) << "\">"
		  << AtlasEsc(e.at) << "</td><td>" << AtlasEsc(e.actor) << "</td><td>" << subject << "</td><td>"
		  << AtlasEsc(e.type) << "</td><td>" << EventDetail(e) << "</td></tr>";
		templates << EventTemplate(r, d, e, edit, family);
	}
	b << "</table></div></div>"
	  << "<div class=\"ad-gutter\" tabindex=\"0\" role=\"separator\" aria-orientation=\"vertical\" aria-label=\"Resize the panes\"></div>"
	  << "<div class=\"ad-pane-detail\"><p class=\"ad-empty\">Select an event to see its details.</p></div>"
	  << "</div>" << String(templates);
	return String(b);
}

// ---------------------------------------------------------------------------
// Rules: atoms (with gates and the starting world), actions and constraints.
// ---------------------------------------------------------------------------

static String RulesBody(WebRequest& r, const RoadmapData& d, bool edit, const String& family)
{
	StringBuffer b;
	b << "<section class=\"ad-panel\"><header><h2>Atoms</h2><p>Named facts. Gates may appear in a version's contract. The starting world is the set of atoms true before the first version.</p></header>"
	  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Atom</th><th>Gate</th><th>Starts true</th><th>Meaning</th><th></th></tr>";
	for(const AtlasAtomDef& a : d.rules.atoms) {
		b << "<tr><td class=\"ad-mono\">" << AtlasEsc(a.name) << "</td><td>" << (a.gate ? "<span class=\"ad-pill is-ok\">gate</span>" : String())
		  << "</td><td>" << (HasName(d.rules.initial, a.name) ? "<span class=\"ad-pill is-ok\">yes</span>" : String("no"))
		  << "</td><td>" << AtlasEsc(a.text) << "</td><td>";
		if(edit)
			b << ApiForm(Api(r, family, "/rules/atom/delete"), "ad-inline")
			  << "<input type=\"hidden\" name=\"name\" value=\"" << AtlasEsc(a.name) << "\"><button class=\"ad-btn is-danger\" type=\"submit\">Remove</button></form>";
		b << "</td></tr>";
	}
	b << "</table></div>";
	if(edit)
		b << ApiForm(Api(r, family, "/rules/atom"))
		  << Field("Atom", "name", "", "") << Field("Meaning", "text", "", "ad-wide")
		  << "<label>Gate <select name=\"gate\"><option value=\"0\">no</option><option value=\"1\">yes</option></select></label>"
		  << "<label>Starts true <select name=\"initial\"><option value=\"0\">no</option><option value=\"1\">yes</option></select></label>"
		  << "<button class=\"ad-btn is-primary\" type=\"submit\">Save atom</button></form>";
	b << "</section>";

	b << "<section class=\"ad-panel\"><header><h2>Actions</h2><p>A named step. An event that names it needs its requirements and sets its effects.</p></header>"
	  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Action</th><th>Needs</th><th>Sets</th><th>Meaning</th><th></th></tr>";
	for(const AtlasAction& a : d.rules.actions) {
		b << "<tr><td class=\"ad-mono\">" << AtlasEsc(a.name) << "</td><td>" << Pills(a.requires, "is-info") << "</td><td>"
		  << Pills(a.effects, "is-ok") << "</td><td>" << AtlasEsc(a.text) << "</td><td>";
		if(edit)
			b << ApiForm(Api(r, family, "/rules/action/delete"), "ad-inline")
			  << "<input type=\"hidden\" name=\"name\" value=\"" << AtlasEsc(a.name) << "\"><button class=\"ad-btn is-danger\" type=\"submit\">Remove</button></form>";
		b << "</td></tr>";
	}
	b << "</table></div>";
	if(edit)
		b << ApiForm(Api(r, family, "/rules/action"))
		  << Field("Action", "name", "", "") << Field("Needs (comma separated)", "requires", "", "ad-wide")
		  << Field("Sets (comma separated)", "effects", "", "ad-wide") << Field("Meaning", "text", "", "ad-wide")
		  << "<button class=\"ad-btn is-primary\" type=\"submit\">Save action</button></form>";
	b << "</section>";

	b << "<section class=\"ad-panel\"><header><h2>Constraints</h2><p>When every \"when\" atom holds, every \"must\" atom has to hold. Checked after each version's events, inside its scope.</p></header>"
	  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Constraint</th><th>When</th><th>Must</th><th>Scope</th><th>Meaning</th><th></th></tr>";
	for(const AtlasConstraint& c : d.rules.constraints) {
		String scope = (c.from_vid.GetCount() ? VidLabel(d, c.from_vid) : String("start")) + " to "
		             + (c.to_vid.GetCount() ? VidLabel(d, c.to_vid) : String("end"));
		b << "<tr><td class=\"ad-mono\">" << AtlasEsc(c.id) << "</td><td>" << Pills(c.when, "is-info") << "</td><td>"
		  << Pills(c.must, "is-warn") << "</td><td>" << AtlasEsc(scope) << "</td><td>" << AtlasEsc(c.text) << "</td><td>";
		if(edit)
			b << ApiForm(Api(r, family, "/rules/constraint/delete"), "ad-inline")
			  << "<input type=\"hidden\" name=\"id\" value=\"" << AtlasEsc(c.id) << "\"><button class=\"ad-btn is-danger\" type=\"submit\">Remove</button></form>";
		b << "</td></tr>";
	}
	b << "</table></div>";
	if(edit)
		b << ApiForm(Api(r, family, "/rules/constraint"))
		  << Field("Id", "id", "", "") << Field("When (comma separated)", "when", "", "ad-wide")
		  << Field("Must (comma separated)", "must", "", "ad-wide")
		  << "<label>From <select name=\"from_vid\">" << VersionOptions(d, String(), true) << "</select></label>"
		  << "<label>To <select name=\"to_vid\">" << VersionOptions(d, String(), true) << "</select></label>"
		  << Field("Meaning", "text", "", "ad-wide")
		  << "<button class=\"ad-btn is-primary\" type=\"submit\">Save constraint</button></form>";
	b << "</section>";
	return String(b);
}

// ---------------------------------------------------------------------------
// Tasks: work that must be done for a target version, with its state from the events.
// ---------------------------------------------------------------------------

struct TaskRow : Moveable<TaskRow> {
	String        id, title, target, link, state, assignee;
	Vector<int>   notes;   // indices into the events: the task's note thread, oldest first
};

// The tasks, with their state, assignee and note thread, read from the events in replay order.
static void CollectTasks(const RoadmapData& d, Vector<TaskRow>& out)
{
	for(int k = 0; k < d.events.GetCount(); k++) {
		const AtlasEvent& e = d.events[k];
		if(e.subject_kind != "task")
			continue;
		int i = -1;
		for(int j = 0; j < out.GetCount(); j++)
			if(out[j].id == e.subject_id)
				i = j;
		if(i < 0) {
			TaskRow t;
			t.id = e.subject_id;
			out.Add(pick(t));
			i = out.GetCount() - 1;
		}
		TaskRow& t = out[i];
		if(e.type == "proposed") {
			t.title = e.name;
			t.target = e.target;
			t.link = e.link;
			t.state = "proposed";
			t.assignee = e.assignee;
		}
		else if(e.type == "assigned")
			t.assignee = e.assignee;
		else if(e.type == "accepted" || e.type == "reopened")
			t.state = "accepted";
		else if(e.type == "done")
			t.state = "done";
		else if(e.type == "dropped")
			t.state = "dropped";
		else if(e.type == "note")
			t.notes.Add(k);
	}
}

// One task's detail: its state, who it is for, the note thread (markdown) and the forms to change it.
static String TaskTemplate(WebRequest& r, const RoadmapData& d, const TaskRow& t, bool edit, const String& family)
{
	String hidden = "<input type=\"hidden\" name=\"id\" value=\"" + AtlasEsc(t.id) + "\">";
	StringBuffer b;
	b << "<template id=\"" << AtlasEsc(t.id) << "\"><h2>" << AtlasEsc(t.title.IsEmpty() ? t.id : t.title) << "</h2>"
	  << "<p><span class=\"ad-mono\">" << AtlasEsc(t.id) << "</span> <span class=\"ad-pill "
	  << (t.state == "done" ? "is-ok" : t.state == "dropped" ? "is-muted" : "is-warn") << "\">" << AtlasEsc(t.state)
	  << "</span> must be done for <strong>" << AtlasEsc(VidLabel(d, t.target)) << "</strong></p>"
	  << "<dl class=\"ad-dl\"><dt>Assignee</dt><dd>" << (t.assignee.IsEmpty() ? String("<span class=\"ad-empty\">nobody yet</span>") : AtlasEsc(t.assignee))
	  << "</dd><dt>About</dt><dd class=\"ad-mono\">" << (t.link.IsEmpty() ? String("&mdash;") : AtlasEsc(t.link)) << "</dd></dl>";
	if(edit) {
		b << ApiForm(Api(r, family, "/task/assign"), "ad-form") << hidden
		  << Field("Responsible (name or bot session)", "assignee", t.assignee, "ad-wide")
		  << "<button class=\"ad-btn\" type=\"submit\">Save assignee</button></form>";
	}
	b << "<h3>Notes</h3>";
	if(!t.notes.GetCount())
		b << "<p class=\"ad-empty\">No notes yet.</p>";
	for(int k : t.notes) {
		const AtlasEvent& e = d.events[k];
		b << "<div class=\"ad-note-item\"><div class=\"ad-note-meta ad-mono\">" << AtlasEsc(e.at) << " &middot; "
		  << AtlasEsc(e.actor) << "</div><div class=\"ad-md\">" << AtlasMarkdown(e.text) << "</div></div>";
	}
	if(edit) {
		b << ApiForm(Api(r, family, "/task/note"), "ad-form") << hidden
		  << "<label>Add a note (markdown) <textarea name=\"text\" class=\"ad-note-input\"></textarea></label>"
		  << "<button class=\"ad-btn\" type=\"submit\">Add note</button></form>";
		b << "<div class=\"ad-form\">";
		if(t.state == "proposed")
			b << ApiForm(Api(r, family, "/task/accept"), "ad-inline") << hidden << "<button class=\"ad-btn\" type=\"submit\">Accept</button></form>";
		if(t.state == "accepted")
			b << ApiForm(Api(r, family, "/task/done"), "ad-inline") << hidden << "<button class=\"ad-btn is-primary\" type=\"submit\">Done</button></form>";
		if(t.state == "proposed" || t.state == "accepted")
			b << ApiForm(Api(r, family, "/task/drop"), "ad-inline") << hidden << "<button class=\"ad-btn is-danger\" type=\"submit\">Drop</button></form>";
		if(t.state == "done" || t.state == "dropped")
			b << ApiForm(Api(r, family, "/task/reopen"), "ad-inline") << hidden << "<button class=\"ad-btn\" type=\"submit\">Reopen</button></form>";
		b << "</div>";
	}
	b << "</template>";
	return String(b);
}

static String TasksBody(WebRequest& r, const RoadmapData& d, bool edit, const String& family)
{
	Vector<TaskRow> tasks;
	CollectTasks(d, tasks);
	String head_vid = d.f.versions.IsEmpty() ? String() : d.f.versions[d.f.Head()].vid;
	StringBuffer b;
	b << "<p class=\"ad-note\">Work that has to be done before its target version can be entered. Done at a later version than its target is too late.</p>"
	  << "<div class=\"ad-split-tools\"><input type=\"search\" placeholder=\"Filter tasks (state, assignee, target)\" data-filter-for=\"tasks-table\" aria-label=\"Filter tasks\">"
	  << "<button class=\"ad-btn\" type=\"button\" data-split-toggle=\"tasks\">Layout</button></div>"
	  << "<div class=\"ad-split\" data-split=\"tasks\" data-orient=\"horizontal\">"
	  << "<div class=\"ad-pane-list\"><div class=\"ad-table-wrap\"><table class=\"ad-table\" id=\"tasks-table\" data-sortable>"
	  << "<tr><th data-sort=\"text\">Task</th><th data-sort=\"num\">Target</th><th data-sort=\"text\">State</th>"
	  << "<th data-sort=\"text\">Assignee</th><th>About</th></tr>";
	StringBuffer templates;
	for(const TaskRow& t : tasks) {
		int code = d.f.Find(t.target) >= 0 ? d.f.versions[d.f.Find(t.target)].code : 0;
		String cls = t.state == "done" ? String("is-ok") : t.state == "dropped" ? String("is-muted") : String("is-warn");
		b << "<tr data-detail=\"" << AtlasEsc(t.id) << "\" tabindex=\"0\"><td><span class=\"ad-mono\">" << AtlasEsc(t.id)
		  << "</span> " << AtlasEsc(t.title) << "</td><td data-v=\"" << code << "\">" << AtlasEsc(VidLabel(d, t.target))
		  << "</td><td><span class=\"ad-pill " << cls << "\">" << AtlasEsc(t.state) << "</span></td><td data-v=\""
		  << AtlasEsc(t.assignee) << "\">" << (t.assignee.IsEmpty() ? String("<span class=\"ad-empty\">&mdash;</span>") : AtlasEsc(t.assignee))
		  << "</td><td class=\"ad-mono\">" << AtlasEsc(t.link) << "</td></tr>";
		templates << TaskTemplate(r, d, t, edit, family);
	}
	if(!tasks.GetCount())
		b << "<tr><td colspan=\"5\" class=\"ad-empty\">No tasks yet.</td></tr>";
	b << "</table></div>";
	if(edit)
		b << ApiForm(Api(r, family, "/task/propose"))
		  << Field("Id", "id", "", "") << Field("Title", "title", "", "ad-wide")
		  << "<label>Must be done for <select name=\"target\">" << VersionOptions(d, head_vid, false) << "</select></label>"
		  << Field("About (element:id or road:id)", "link", "", "")
		  << Field("Responsible", "assignee", "", "")
		  << "<label>Details <textarea name=\"text\"></textarea></label>"
		  << "<button class=\"ad-btn is-primary\" type=\"submit\">Propose task</button></form>";
	b << "</div>"
	  << "<div class=\"ad-gutter\" tabindex=\"0\" role=\"separator\" aria-orientation=\"vertical\" aria-label=\"Resize the panes\"></div>"
	  << "<div class=\"ad-pane-detail\"><p class=\"ad-empty\">Select a task to see its notes and change it.</p></div></div>"
	  << String(templates);
	return String(b);
}

// ---------------------------------------------------------------------------
// Checks: the replay's findings. They report and never block a write.
// ---------------------------------------------------------------------------

static String ChecksBody(WebRequest& r, const RoadmapData& d)
{
	int errors = 0, warnings = 0;
	for(const AtlasRoadmapMessage& m : d.messages)
		if(m.severity == "error") errors++; else warnings++;
	StringBuffer b;
	b << "<div class=\"ad-cards\">"
	  << "<div class=\"ad-card\"><div class=\"ad-card-value\">" << errors << "</div><div class=\"ad-card-label\">errors</div></div>"
	  << "<div class=\"ad-card\"><div class=\"ad-card-value\">" << warnings << "</div><div class=\"ad-card-label\">warnings</div></div>"
	  << "</div><p class=\"ad-note\">Checks never block a change. A red finding is a plan that does not hold yet; fix it or record why it is acceptable.</p>";
	b << "<section class=\"ad-panel\"><header><h2>Findings</h2></header>"
	  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Severity</th><th>Version</th><th>Subject</th><th>Rule</th><th>Finding</th></tr>";
	for(const AtlasRoadmapMessage& m : d.messages) {
		String subject = AtlasEsc(m.subject_kind) + ": " + AtlasEsc(m.subject_id);
		if(m.subject_kind == "element" && m.subject_id.GetCount())
			subject = "<a href=\"" + AtlasEsc(r.Site().Url("atlas/" + d.f.head_key + "/element/" + m.subject_id)) + "\">" + subject + "</a>";
		b << "<tr><td><span class=\"ad-pill " << (m.severity == "error" ? "is-err" : "is-warn") << "\">" << AtlasEsc(m.severity)
		  << "</span></td><td>" << AtlasEsc(VidLabel(d, m.vid)) << "</td><td>" << subject << "</td><td class=\"ad-mono\">"
		  << AtlasEsc(m.rule) << "</td><td>" << AtlasEsc(m.text) << "</td></tr>";
	}
	if(!d.messages.GetCount())
		b << "<tr><td colspan=\"5\" class=\"ad-empty\">Nothing to report.</td></tr>";
	b << "</table></div></section>";
	return String(b);
}

// ---------------------------------------------------------------------------
// Subjects at one version: the map's elements and roads as they stood then, with the checker's findings
// and, for admins, the forms that change them. Used by the map page when it shows a version.
// ---------------------------------------------------------------------------

static const char *StatusNames[] = { "hot", "active", "planned", "not-ready", "future", "settled", "trouble" };
static const char *StageNames[] = { "planned", "implementation_research", "implementation", "trials", "merge_to_master" };
static const char *KindNames[] = { "continent", "block", "place", "sea" };
static const char *RoadKindNames[] = { "progress", "communication", "dependency" };

static String Options(const char *const *names, int n, const String& selected)
{
	StringBuffer b;
	for(int i = 0; i < n; i++)
		b << "<option value=\"" << names[i] << "\"" << (selected == names[i] ? " selected" : String()) << ">"
		  << names[i] << "</option>";
	return String(b);
}

static String StatusPill(const String& status)
{
	return "<span class=\"ad-pill st-" + AtlasEsc(status) + "\">" + AtlasEsc(status) + "</span>";
}

static String StagePill(const String& stage)
{
	if(stage.IsEmpty())
		return String("<span class=\"ad-empty\">&mdash;</span>");
	return "<span class=\"ad-pill stg-" + AtlasEsc(stage) + "\" data-rank=\"" + AsString(AtlasStageRank(stage)) + "\">" + AtlasEsc(stage) + "</span>";
}

String AtlasSubjectsView(WebRequest& r, const AtlasMap& head, const AtlasFamily& f, const Vector<AtlasEvent>& events,
                         const Vector<AtlasSubjectState>& states, const Vector<AtlasRoadmapMessage>& messages,
                         const String& vid, bool edit)
{
	String family = head.map;
	String hidden_vid = "<input type=\"hidden\" name=\"vid\" value=\"" + AtlasEsc(vid) + "\">";
	int at = f.Find(vid);
	StringBuffer b;
	if(edit) {
		b << "<details class=\"ad-panel\"><summary>Add an element at " << AtlasEsc(VidLabel(f, vid)) << "</summary>"
		  << ApiForm(Api(r, family, "/element/create"))
		  << hidden_vid << Field("Name", "name", "", "") << Field("Parent (optional id)", "parent", "", "")
		  << "<label>Kind <select name=\"kind\">" << Options(KindNames, 4, "block") << "</select></label>"
		  << "<label>Status <select name=\"status\">" << Options(StatusNames, 7, "planned") << "</select></label>"
		  << Field("x", "x", "0", "") << Field("y", "y", "0", "") << Field("w", "w", "180", "") << Field("h", "h", "100", "")
		  << "<button class=\"ad-btn is-primary\" type=\"submit\">Add element</button></form></details>";
		b << "<details class=\"ad-panel\"><summary>Connect two elements at " << AtlasEsc(VidLabel(f, vid)) << "</summary>"
		  << ApiForm(Api(r, family, "/road/create"))
		  << hidden_vid << Field("From (element id)", "from", "", "") << Field("To (element id)", "to", "", "")
		  << "<label>Kind <select name=\"kind\">" << Options(RoadKindNames, 3, "communication") << "</select></label>"
		  << Field("Label", "label", "", "")
		  << "<button class=\"ad-btn is-primary\" type=\"submit\">Connect</button></form></details>";
	}
	b << "<div class=\"ad-split-tools\"><input type=\"search\" placeholder=\"Filter subjects\" data-filter-for=\"subjects-table\" aria-label=\"Filter subjects\">"
	  << "<button class=\"ad-btn\" type=\"button\" data-split-toggle=\"subjects\">Layout</button></div>"
	  << "<div class=\"ad-split\" data-split=\"subjects\" data-orient=\"horizontal\">"
	  << "<div class=\"ad-pane-list\"><div class=\"ad-table-wrap\"><table class=\"ad-table\" id=\"subjects-table\" data-sortable>"
	  << "<tr><th data-sort=\"text\">Subject</th><th data-sort=\"text\">Kind</th><th data-sort=\"num\">Stage</th>"
	  << "<th data-sort=\"text\">Status</th><th data-sort=\"num\">Checks</th></tr>";
	StringBuffer templates;
	for(const AtlasSubjectState& st : states) {
		if(!st.present)
			continue;
		int errors = 0, warnings = 0;
		for(const AtlasRoadmapMessage& m : messages)
			if(m.vid == vid && m.subject_kind == st.kind && m.subject_id == st.id) {
				if(m.severity == "error") errors++; else warnings++;
			}
		String id = st.kind + "-" + st.id;
		String label = st.kind == "road" ? st.from + " → " + st.to : (st.name.IsEmpty() ? st.id : st.name);
		b << "<tr data-detail=\"" << AtlasEsc(id) << "\" tabindex=\"0\"><td>" << AtlasEsc(label) << "</td><td>"
		  << AtlasEsc(st.kind) << "</td><td data-v=\"" << AtlasStageRank(st.stage) << "\">" << StagePill(st.stage)
		  << "</td><td>" << (st.status.IsEmpty() ? String() : StatusPill(st.status)) << "</td><td data-v=\""
		  << errors * 1000 + warnings << "\">"
		  << (errors ? "<span class=\"ad-pill is-err\">" + AtlasEsc(AsString(errors)) + " error</span>" : String())
		  << (warnings ? "<span class=\"ad-pill is-warn\">" + AtlasEsc(AsString(warnings)) + " warning</span>" : String())
		  << "</td></tr>";

		// The subject's own history up to this version, then the forms that change it.
		StringBuffer t;
		t << "<template id=\"" << AtlasEsc(id) << "\"><h2>" << AtlasEsc(label) << "</h2><p>" << StagePill(st.stage) << " "
		  << (st.status.IsEmpty() ? String() : StatusPill(st.status)) << "</p>";
		for(const AtlasRoadmapMessage& m : messages)
			if(m.vid == vid && m.subject_kind == st.kind && m.subject_id == st.id)
				t << "<p class=\"" << (m.severity == "error" ? "err" : "ad-note") << "\">" << AtlasEsc(m.severity) << ": "
				  << AtlasEsc(m.text) << "</p>";
		t << "<h3>History</h3>";
		int shown = 0;
		for(const AtlasEvent& e : events) {
			if(e.subject_kind != st.kind || e.subject_id != st.id || at < 0 || f.Find(e.vid) > at)
				continue;
			shown++;
			t << "<div class=\"ad-note-item\"><div class=\"ad-note-meta ad-mono\">" << AtlasEsc(VidLabel(f, e.vid))
			  << " &middot; " << AtlasEsc(e.at) << " &middot; " << AtlasEsc(e.actor) << " &middot; " << AtlasEsc(e.type);
			if(e.stage.GetCount()) t << " " << AtlasEsc(e.stage);
			if(e.status.GetCount()) t << " " << AtlasEsc(e.status);
			t << "</div>";
			if(e.text.GetCount()) t << "<div class=\"ad-md\">" << AtlasMarkdown(e.text) << "</div>";
			t << "</div>";
		}
		if(!shown)
			t << "<p class=\"ad-empty\">No events for this subject up to this version.</p>";
		if(edit) {
			String subj = "<input type=\"hidden\" name=\"subject_kind\" value=\"" + AtlasEsc(st.kind) + "\"><input type=\"hidden\" name=\"subject_id\" value=\"" + AtlasEsc(st.id) + "\">";
			t << ApiForm(Api(r, family, "/event/add")) << subj << hidden_vid << "<input type=\"hidden\" name=\"type\" value=\"note\">"
			  << "<label>Add a note <textarea name=\"text\" class=\"ad-note-input\"></textarea></label>"
			  << "<button class=\"ad-btn\" type=\"submit\">Add note</button></form>";
			t << "<div class=\"ad-form\">" << ApiForm(Api(r, family, "/event/add"), "ad-inline") << subj << hidden_vid
			  << "<input type=\"hidden\" name=\"type\" value=\"stage\"><label>Stage <select name=\"stage\">" << Options(StageNames, 5, st.stage)
			  << "</select></label><button class=\"ad-btn\" type=\"submit\">Set stage</button></form>";
			t << ApiForm(Api(r, family, "/event/add"), "ad-inline") << subj << hidden_vid
			  << "<input type=\"hidden\" name=\"type\" value=\"status\"><label>Status <select name=\"status\">" << Options(StatusNames, 7, st.status)
			  << "</select></label><button class=\"ad-btn\" type=\"submit\">Set status</button></form></div>";
			if(st.kind == "element")
				t << ApiForm(Api(r, family, "/element/remove"), "ad-inline") << hidden_vid
				  << "<input type=\"hidden\" name=\"id\" value=\"" << AtlasEsc(st.id) << "\">"
				  << "<button class=\"ad-btn is-danger\" type=\"submit\" data-confirm=\"Remove " << AtlasEsc(st.id) << " from this version on?\">Remove element</button></form>";
			else
				t << ApiForm(Api(r, family, "/road/disconnect"), "ad-inline") << hidden_vid
				  << "<input type=\"hidden\" name=\"id\" value=\"" << AtlasEsc(st.id) << "\">"
				  << "<button class=\"ad-btn is-danger\" type=\"submit\" data-confirm=\"Disconnect " << AtlasEsc(st.id) << " from this version on?\">Disconnect road</button></form>";
		}
		t << "</template>";
		templates << String(t);
	}
	if(!templates.GetCount())
		b << "<tr><td colspan=\"5\" class=\"ad-empty\">Nothing was on the map at this version.</td></tr>";
	b << "</table></div></div>"
	  << "<div class=\"ad-gutter\" tabindex=\"0\" role=\"separator\" aria-orientation=\"vertical\" aria-label=\"Resize the panes\"></div>"
	  << "<div class=\"ad-pane-detail\"><p class=\"ad-empty\">Select a subject to see its history and change it.</p></div></div>"
	  << String(templates);
	return String(b);
}

// ---------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------

// The version diagram. Y is the version (earlier at the top); X only spreads the bubbles of one row across the width.
// The bubbles are HTML, so their text can be selected and copied; the arrows are one SVG layer under them. The page is
// as tall as the versions need. Work bubbles are coloured by what they produce, not by progress: the diagram is the
// plan and everything in it is planned. Gates are dashed; accomplished features use the settled colour; a banner heads
// each minor group. Hovering a bubble highlights its arrows.
static Vector<String> WrapText(const String& text, int chars)
{
	Vector<String> lines;
	String line;
	for(String word : Split(text, ' ')) {
		if(word.IsEmpty())
			continue;
		// A word longer than a line breaks inside itself, as the page does with overflow-wrap.
		while(word.GetCount() > chars) {
			if(line.GetCount()) {
				lines.Add(line);
				line.Clear();
			}
			lines.Add(word.Left(chars));
			word = word.Mid(chars);
		}
		if(line.GetCount() && line.GetCount() + 1 + word.GetCount() > chars) {
			lines.Add(line);
			line.Clear();
		}
		if(line.GetCount())
			line << ' ';
		line << word;
	}
	if(line.GetCount())
		lines.Add(line);
	return lines;
}

// The colour of a work bubble says what it produces: a package realization, a test gate, a feature, or data.
static String WorkVar(const AtlasDiagramBubble& b)
{
	for(const String& lit : b.effects)
		if(lit.StartsWith("realized:"))
			return "--accent";
	if(b.processing)
		return "--st-planned";
	for(const String& lit : b.effects) {
		if(lit.StartsWith("tested:")) return "--st-notready";
		if(lit.StartsWith("done:")) return "--st-settled";
	}
	return "--st-active";
}

// A width in 1/1000 of the page as a percentage with one decimal: 905 -> "90.5".
static String Tenths(int units)
{
	return AsString(units / 10) + "." + AsString(units % 10);
}

// An element or action name with a break opportunity before each capital that follows a lower-case letter or digit,
// and before - and _, so long CamelCase names wrap inside a bubble instead of running out of it.
static String BreakableName(const String& text)
{
	String out;
	for(int i = 0; i < text.GetCount(); i++) {
		int c = text[i];
		bool camel = i > 0 && IsUpper(c) && (IsLower(text[i - 1]) || IsDigit(text[i - 1]));
		if(camel || c == '-' || c == '_')
			out << "<wbr>";
		out << AtlasEsc(String(c, 1));
	}
	return out;
}

static String BubbleText(const AtlasDiagramBubble& b)
{
	if(b.kind == "gate") return b.literal;
	if(b.kind == "accomplished") return "feature " + b.text;
	return b.text;
}

// A banner and a declaration take a line to themselves; other bubbles share lines.
static bool FullWidth(const String& kind)
{
	return kind == "banner" || kind == "declaration" || kind == "taskgroup";
}

static String DiagramBody(WebRequest& r, const RoadmapData& d, bool edit, const String& family)
{
	Vector<String> hidden;
	for(const AtlasElement& e : d.head.elements)
		if(!AtlasElementVisibleTo(r, d.f.head_key, e.id))
			hidden.Add(e.id);
	// The file records are the declarations; a family with none shows no declaration bubbles.
	Vector<AtlasFile> files;
	String file_err;
	AtlasListFiles(r.Site().data_root, family, files, file_err);
	AtlasVersionDiagram g;
	AtlasBuildDiagram(d.f, d.head, d.rules, d.events, files, hidden, g);
	if(g.rows.IsEmpty())
		return "<p class=\"ad-empty\">No versions yet.</p>";

	// Layout units: X and width are in 1/1000 of the width; Y and heights are pixels.
	const int UNITS = 1000, LEFT = 90, RIGHT = 16, GAP = 14, TOP = 12, ROW_GAP = 40, LINE_GAP = 12, LINE_H = 19, PAD = 16;
	const int avail = UNITS - LEFT - RIGHT;
	int nb = g.bubbles.GetCount();
	Vector<int> bx, by, bw, bh;
	for(int i = 0; i < nb; i++) { bx.Add(0); by.Add(0); bw.Add(0); bh.Add(0); }
	Vector<int> row_top;
	int y = TOP;
	const int PER_LINE = 4;   // bubbles side by side; a version with more stacks them in further lines
	for(int ri = 0; ri < g.rows.GetCount(); ri++) {
		const AtlasDiagramRow& row = g.rows[ri];
		int n = row.bubbles.GetCount();
		row_top.Add(y);
		if(n == 0) {
			y += 30 + ROW_GAP;
			continue;
		}
		for(int first = 0; first < n;) {
			// A banner or a declaration takes a line to itself; other bubbles share lines of PER_LINE.
			bool full = FullWidth(g.bubbles[row.bubbles[first]].kind);
			int m = 1;
			if(!full)
				while(m < PER_LINE && first + m < n && !FullWidth(g.bubbles[row.bubbles[first + m]].kind))
					m++;
			int slot = full ? avail : min(300, (avail - (m - 1) * GAP) / m);
			int total = full ? avail : m * slot + (m - 1) * GAP;
			int x0 = full ? LEFT : LEFT + (avail - total) / 2;
			int line_h = 40;
			int chars = max(12, slot / 9);
			for(int k = 0; k < m; k++) {
				int i = row.bubbles[first + k];
				const AtlasDiagramBubble& b = g.bubbles[i];
				int lines = WrapText(BubbleText(b), chars).GetCount();
				if(b.kind == "declaration" || b.kind == "taskgroup") {
					// Three columns share the signature lines of a full-width bubble.
					int col_chars = max(12, chars / 3), n_lines = 0;
					for(const String& line : b.declares)
						n_lines += WrapText(line, col_chars).GetCount();
					lines += (n_lines + 2) / 3;
				}
				if(b.kind == "task") {
					lines += WrapText(b.detail, chars).GetCount();
					if(b.assignee.GetCount())
						lines += WrapText("assignee: " + b.assignee, chars).GetCount();
				}
				if(b.kind == "work") {
					lines += 1;   // the package name can wrap onto a line of its own
					if(b.action != b.text)
						lines += WrapText(b.action, chars).GetCount();
					lines += WrapText(b.detail, chars).GetCount();
					lines += WrapText("in: " + (b.requires.IsEmpty() ? String("none") : JoinComma(b.requires)), chars).GetCount();
					lines += WrapText("out: " + (b.effects.IsEmpty() ? String("none") : JoinComma(b.effects)), chars).GetCount();
					if(b.waits.GetCount())
						lines += WrapText("waits for " + JoinComma(b.waits), chars).GetCount();
					if(b.unmet)
						lines += WrapText("entry not produced above", chars).GetCount();
				}
				bx[i] = x0 + (full ? 0 : k * (slot + GAP));
				bw[i] = slot;
				bh[i] = max(36, PAD + LINE_H * max(1, lines) + 6);
				line_h = max(line_h, bh[i]);
			}
			for(int k = 0; k < m; k++) {
				int i = row.bubbles[first + k];
				by[i] = y + (line_h - bh[i]) / 2;
			}
			y += line_h + LINE_GAP;
			first += m;
		}
		y += ROW_GAP - LINE_GAP;
	}
	int H = y;

	StringBuffer s;
	s << "<style>"
	     ".dg-wrap{position:relative;width:100%}"
	     ".dg-b{position:absolute;box-sizing:border-box;border-radius:14px;padding:8px 10px;overflow:visible;overflow-wrap:anywhere;"
	     "font-size:13px;line-height:19px;color:var(--fg);background:var(--panel)}"
	     ".dg-b .dg-k{color:var(--muted);font-size:12px}"
	     ".dg-work{border:2px solid}"
	     ".dg-gate{border:1.5px dashed var(--line)}"
	     ".dg-accomplished{border:1.5px solid var(--st-settled)}"
	     ".dg-banner{border:2px solid var(--accent);background:var(--control);font-weight:600}"
	     ".dg-declaration{border:1.5px solid var(--st-future)}"
	     ".dg-task{border:2px solid var(--st-trouble)}"
	     ".dg-taskgroup{border:2px solid var(--st-trouble)}"
	     ".dg-unmet{border-style:dashed;border-color:var(--st-trouble) !important}"
	     ".dg-row{position:absolute;left:0;font:12px monospace;color:var(--muted)}"
	     ".dg-arrows{position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none}"
	     "</style>";
	s << "<div class=\"ad-panel\"><div class=\"ad-panel-header\">Version diagram</div>"
	  << "<p class=\"ad-hint\">Down is later. Coloured bubbles are work: in is what must hold first, out is what it sets. "
	     "Dashed bubbles are gates the version needs, settled-colour bubbles are features the version declares. Hover a bubble "
	     "to see its arrows. A red bubble is a task still to be done; an arrow runs from it to the work that needs it. A dashed red bubble needs something no version above produces; an amber line names a task that is not drawn. "
	     "A declaration bubble lists the files and the class and function signatures its work declares, with no bodies.</p>"
	  << "<p class=\"ad-hint\">Colour says what the work produces: <span class=\"dg-k\" style=\"color:var(--accent)\">package</span>, "
	     "<span class=\"dg-k\" style=\"color:var(--st-active)\">data</span>, "
	     "<span class=\"dg-k\" style=\"color:var(--st-planned)\">processing</span>, "
	     "<span class=\"dg-k\" style=\"color:var(--st-settled)\">feature</span>, "
	     "<span class=\"dg-k\" style=\"color:var(--st-notready)\">test gate</span>, "
	     "<span class=\"dg-k\" style=\"color:var(--st-trouble)\">task</span>. "
	     "The diagram is the plan: nothing here shows progress.</p>"
	  << "<div class=\"dg-wrap\" style=\"height:" << H << "px\">";

	for(int ri = 0; ri < g.rows.GetCount(); ri++)
		s << "<div class=\"dg-row\" style=\"top:" << row_top[ri] + 12 << "px\">" << AtlasEsc(g.rows[ri].label) << "</div>";

	s << "<svg class=\"dg-arrows\" viewBox=\"0 0 " << UNITS << " " << H << "\" preserveAspectRatio=\"none\" aria-hidden=\"true\">"
	     "<defs><marker id=\"dg-arrow\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"6\" markerHeight=\"6\" "
	     "orient=\"auto\"><path d=\"M0,0 L10,5 L0,10 z\" style=\"fill:var(--muted)\"/></marker></defs>";
	for(int a = 0; a < g.arrows.GetCount(); a++) {
		const AtlasDiagramArrow& ar = g.arrows[a];
		int fx = bx[ar.from] + bw[ar.from] / 2, fy = by[ar.from] + bh[ar.from];
		int tx = bx[ar.to] + bw[ar.to] / 2, ty = by[ar.to];
		String path;
		if(g.bubbles[ar.from].row == g.bubbles[ar.to].row) {
			int sx = bx[ar.from] + bw[ar.from], sy = by[ar.from] + bh[ar.from] / 2;
			int ex = bx[ar.to], ey = by[ar.to] + bh[ar.to] / 2;
			path << "M" << sx << "," << sy << " C" << (sx + ex) / 2 << "," << sy << " " << (sx + ex) / 2 << "," << ey << " " << ex << "," << ey;
		}
		else
			path << "M" << fx << "," << fy << " C" << fx << "," << (fy + ty) / 2 << " " << tx << "," << (fy + ty) / 2 << " " << tx << "," << ty;
		s << "<path class=\"dg-arrow\" data-from=\"" << ar.from << "\" data-to=\"" << ar.to << "\" d=\"" << path
		  << "\" style=\"fill:none;stroke:var(--muted);stroke-width:1.2;opacity:.7\" marker-end=\"url(#dg-arrow)\"/>";
	}
	s << "</svg>";

	for(int i = 0; i < nb; i++) {
		const AtlasDiagramBubble& b = g.bubbles[i];
		String cls = "dg-b dg-" + b.kind + (b.unmet ? " dg-unmet" : String());
		String style = "left:" + Tenths(bx[i]) + "%;top:" + AsString(by[i]) + "px;width:"
		             + Tenths(bw[i]) + "%;height:" + AsString(bh[i]) + "px;";
		if(b.kind == "work") {
			String v = WorkVar(b);
			style << "border-color:var(" << v << ");background:color-mix(in srgb, var(" << v << ") 22%, var(--panel));";
		}
		else if(b.kind == "accomplished")
			style << "background:color-mix(in srgb, var(--st-settled) 18%, var(--panel));";
		else if(b.kind == "task" || b.kind == "taskgroup")
			style << "background:color-mix(in srgb, var(--st-trouble) 14%, var(--panel));";
		else if(b.kind == "declaration")
			style << "background:color-mix(in srgb, var(--st-future) 12%, var(--panel));";
		s << "<div class=\"" << cls << "\" data-bubble=\"" << i << "\" style=\"" << style << "\">";
		if(b.kind == "work") {
			s << "<strong>" << BreakableName(b.text) << "</strong>";
			if(b.pkg.GetCount())
				s << " <span class=\"ad-mono\">" << AtlasEsc(b.pkg) << "</span>";
			if(b.action != b.text)
				s << "<div class=\"ad-mono\">" << BreakableName(b.action) << "</div>";
			if(b.detail.GetCount())
				s << "<div class=\"dg-k\">" << AtlasEsc(b.detail) << "</div>";
			s << "<div>in: " << (b.requires.IsEmpty() ? String("none") : AtlasEsc(JoinComma(b.requires))) << "</div>"
			  << "<div>out: " << (b.effects.IsEmpty() ? String("none") : AtlasEsc(JoinComma(b.effects))) << "</div>";
			if(b.waits.GetCount())
				s << "<div style=\"color:var(--st-notready)\">waits for " << AtlasEsc(JoinComma(b.waits)) << "</div>";
			if(b.unmet)
				s << "<div style=\"color:var(--st-trouble)\">entry not produced above</div>";
		}
		else if(b.kind == "task") {
			s << "<strong>" << BreakableName(b.text) << "</strong>"
			     "<div class=\"dg-k\">" << AtlasEsc(b.detail) << "</div>";
			if(b.assignee.GetCount())
				s << "<div>assignee: " << AtlasEsc(b.assignee) << "</div>";
		}
		else if(b.kind == "declaration" || b.kind == "taskgroup") {
			s << "<strong>" << BreakableName(b.text) << "</strong>";
			if(b.kind == "declaration")
				s << " <span class=\"dg-k\">declares</span>";
			s << "<div style=\"column-count:3;column-gap:18px\">";
			for(const String& line : b.declares)
				s << "<div class=\"ad-mono\" style=\"white-space:pre-wrap\">" << BreakableName(line) << "</div>";
			s << "</div>";
		}
		else
			s << AtlasEsc(BubbleText(b));
		s << "</div>";
	}
	s << "</div></div>";

	s << "<script>(function(){var wrap=document.querySelector('.dg-wrap');if(!wrap)return;"
	     "function hi(id,on){wrap.querySelectorAll('.dg-arrow').forEach(function(p){if(p.getAttribute('data-from')===id||p.getAttribute('data-to')===id){p.style.stroke=on?'var(--accent)':'var(--muted)';p.style.strokeWidth=on?'2.5':'1.2';p.style.opacity=on?'1':'.7';}});}"
	     "wrap.querySelectorAll('.dg-b').forEach(function(g){var id=g.getAttribute('data-bubble');g.addEventListener('mouseenter',function(){hi(id,true);});g.addEventListener('mouseleave',function(){hi(id,false);});});})();</script>";

	(void)edit;
	(void)family;
	return s;
}

static const char *RoadmapTitle(const String& section)
{
	if(section == "diagram") return "Diagram";
	if(section == "timeline") return "Timeline";
	if(section == "rules") return "Rules";
	if(section == "tasks") return "Tasks";
	if(section == "checks") return "Checks";
	if(section == "warnings") return "Warnings";
	if(section == "next") return "What's next";
	return "Versions";
}

// What's next: pick the version you are at; the versions ahead, their entry conditions and the steps that
// would meet each one; the open tasks. Admins can accept or finish a task from here.
static String NextBody(WebRequest& r, const RoadmapData& d, bool edit, const String& family)
{
	AtlasNext next;
	String err;
	String from = r.Param("from");
	StringBuffer b;
	if(!AtlasWhatsNext(d.head, d.f, d.rules, d.events, from, next, err)) {
		b << "<p class=\"err\">" << AtlasEsc(err) << "</p>";
		return String(b);
	}
	String head_vid = d.f.versions.IsEmpty() ? String() : d.f.versions[d.f.Head()].vid;
	b << "<form method=\"get\" class=\"ad-form\"><label>I am at version <select name=\"from\">"
	  << "<option value=\"\"" << (next.from.IsEmpty() ? " selected" : String()) << ">start</option>"
	  << VersionOptions(d, next.from, false) << "</select></label>"
	  << "<button class=\"ad-btn\" type=\"submit\">Show what is next</button></form>";
	b << "<p class=\"ad-note\">"
	  << (next.from.IsEmpty() ? String("From the start") : "From " + AtlasEsc(VidLabel(d, next.from)))
	  << ". The default picks the version before the first blocked entry.</p>";
	if(!next.versions.GetCount())
		b << "<p class=\"ad-empty\">Nothing is ahead of this version.</p>";

	for(const AtlasNextVersion& v : next.versions) {
		b << "<section class=\"ad-panel\"><header><h2>" << AtlasEsc(v.label) << " <span class=\"ad-pill "
		  << (v.state == "ready" ? "is-ok" : "is-warn") << "\">" << AtlasEsc(v.state) << "</span></h2>"
		  << "<p>" << (v.conditions.GetCount() ? "Entry conditions" : "No entry conditions") << "</p></header><ul class=\"ad-checks\">";
		for(const AtlasNextCondition& c : v.conditions) {
			b << "<li class=\"" << (c.met ? "is-met" : "is-open") << "\"><span class=\"ad-mark\" aria-hidden=\"true\">"
			  << (c.met ? "&#10003;" : "&#10007;") << "</span> <span class=\"ad-pill\">" << AtlasEsc(c.kind) << "</span> "
			  << AtlasEsc(c.text) << (c.met ? String(" <span class=\"ad-empty\">met</span>") : String()) << "</li>";
			if(!c.met && c.how.GetCount()) {
				b << "<li class=\"ad-how\"><ul>";
				for(const String& h : c.how)
					b << "<li>" << AtlasEsc(h) << "</li>";
				b << "</ul></li>";
			}
		}
		b << "</ul></section>";
	}

	if(next.todo.GetCount()) {
		b << "<section class=\"ad-panel\"><header><h2>Open tasks</h2><p>Work that must be done before a version ahead can be entered, by target.</p></header>"
		  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Task</th><th>Target</th><th>State</th><th>Assignee</th>"
		  << (edit ? "<th>Change</th>" : "") << "</tr>";
		for(const AtlasNextTask& t : next.todo) {
			b << "<tr><td><span class=\"ad-mono\">" << AtlasEsc(t.id) << "</span> " << AtlasEsc(t.title) << "</td><td>"
			  << AtlasEsc(VidLabel(d, t.target)) << "</td><td><span class=\"ad-pill "
			  << (t.state == "accepted" ? "is-warn" : "") << "\">" << AtlasEsc(t.state) << "</span></td><td>"
			  << (t.assignee.IsEmpty() ? String("<span class=\"ad-empty\">nobody yet</span>") : AtlasEsc(t.assignee)) << "</td>";
			if(edit) {
				String hidden = "<input type=\"hidden\" name=\"id\" value=\"" + AtlasEsc(t.id) + "\"><input type=\"hidden\" name=\"vid\" value=\"" + AtlasEsc(head_vid) + "\">";
				b << "<td>";
				if(t.state == "proposed")
					b << ApiForm(Api(r, family, "/task/accept"), "ad-inline") << hidden << "<button class=\"ad-btn\" type=\"submit\">Accept</button></form>";
				if(t.state == "accepted")
					b << ApiForm(Api(r, family, "/task/done"), "ad-inline") << hidden << "<button class=\"ad-btn is-primary\" type=\"submit\">Done</button></form>";
				b << "</td>";
			}
			b << "</tr>";
		}
		b << "</table></div></section>";
	}
	return String(b);
}

// The warnings: every finding that is a warning, with its comment. A warning without a comment is open, and an open
// warning is an error at the head until someone comments on it. Comments that no longer match a finding are stale.
static String WarningsBody(WebRequest& r, const RoadmapData& d, bool edit, const String& family)
{
	Vector<AtlasRoadmapMessage> warnings;
	for(const AtlasRoadmapMessage& m : d.findings)
		if(m.severity == "warning")
			warnings.Add(pick(m));
	Vector<AtlasWarningAck> stale = AtlasStaleAcks(d.findings, d.acks);
	int open = 0;
	Vector<String> ids;
	for(const AtlasRoadmapMessage& m : warnings) {
		String id = AtlasWarningId(m);
		ids.Add(id);
		bool acked = false;
		for(const AtlasWarningAck& a : d.acks)
			if(a.warning_id == id)
				acked = true;
		if(!acked)
			open++;
	}
	StringBuffer b;
	b << "<div class=\"ad-cards\">"
	  << "<div class=\"ad-card\"><div class=\"ad-card-value\">" << warnings.GetCount() << "</div><div class=\"ad-card-label\">warnings</div></div>"
	  << "<div class=\"ad-card\"><div class=\"ad-card-value\">" << open << "</div><div class=\"ad-card-label\">open</div></div>"
	  << "<div class=\"ad-card\"><div class=\"ad-card-value\">" << stale.GetCount() << "</div><div class=\"ad-card-label\">stale comments</div></div>"
	  << "</div><p class=\"ad-note\">A warning does not block a change. An open warning is an error at the head until it has a comment: say why it is acceptable, or what will change in the plan. A comment goes stale when its finding changes or goes away.</p>";

	b << "<section class=\"ad-panel\"><header><h2>Findings</h2></header>"
	  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Status</th><th>Version</th><th>Subject</th><th>Rule</th><th>Finding</th><th>Comment</th></tr>";
	for(int i = 0; i < warnings.GetCount(); i++) {
		const AtlasRoadmapMessage& m = warnings[i];
		const AtlasWarningAck *ack = nullptr;
		for(const AtlasWarningAck& a : d.acks)
			if(a.warning_id == ids[i])
				ack = &a;
		String subject = AtlasEsc(m.subject_kind) + ": " + AtlasEsc(m.subject_id);
		if(m.subject_kind == "element" && m.subject_id.GetCount())
			subject = "<a href=\"" + AtlasEsc(r.Site().Url("atlas/" + d.f.head_key + "/element/" + m.subject_id)) + "\">" + subject + "</a>";
		b << "<tr><td><span class=\"ad-pill " << (ack ? "is-ok" : "is-warn") << "\">" << (ack ? "commented" : "open")
		  << "</span></td><td>" << AtlasEsc(VidLabel(d, m.vid)) << "</td><td>" << subject << "</td><td class=\"ad-mono\">"
		  << AtlasEsc(m.rule) << "</td><td>" << AtlasEsc(m.text) << "</td><td>";
		if(ack) {
			b << AtlasEsc(ack->comment) << "<div class=\"ad-note\">" << AtlasEsc(ack->actor) << " &middot; " << AtlasEsc(ack->at) << "</div>";
			if(edit)
				b << ApiForm(Api(r, family, "/warning/unack"), "ad-inline") << "<input type=\"hidden\" name=\"id\" value=\""
				  << AtlasEsc(ids[i]) << "\"><button class=\"ad-btn\" type=\"submit\">Remove comment</button></form>";
		}
		else if(edit)
			b << ApiForm(Api(r, family, "/warning/ack"), "ad-inline") << "<input type=\"hidden\" name=\"id\" value=\""
			  << AtlasEsc(ids[i]) << "\">" << Field("Comment", "comment", String(), "ad-wide")
			  << "<button class=\"ad-btn is-primary\" type=\"submit\">Comment</button></form>";
		b << "</td></tr>";
	}
	if(!warnings.GetCount())
		b << "<tr><td colspan=\"6\" class=\"ad-empty\">No warnings.</td></tr>";
	b << "</table></div></section>";

	if(stale.GetCount()) {
		b << "<section class=\"ad-panel\"><header><h2>Stale comments</h2><p>These comments are on findings that no longer exist. Remove them, or keep them until the finding returns.</p></header>"
		  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Rule</th><th>Subject</th><th>Finding when commented</th><th>Comment</th><th>By</th></tr>";
		for(const AtlasWarningAck& a : stale) {
			b << "<tr><td class=\"ad-mono\">" << AtlasEsc(a.rule) << "</td><td>" << AtlasEsc(a.subject_kind) << ": " << AtlasEsc(a.subject_id)
			  << "</td><td>" << AtlasEsc(a.text) << "</td><td>" << AtlasEsc(a.comment) << "</td><td>" << AtlasEsc(a.actor);
			if(edit)
				b << ApiForm(Api(r, family, "/warning/unack"), "ad-inline") << "<input type=\"hidden\" name=\"id\" value=\""
				  << AtlasEsc(a.warning_id) << "\"><button class=\"ad-btn\" type=\"submit\">Remove</button></form>";
			b << "</td></tr>";
		}
		b << "</table></div></section>";
	}
	return String(b);
}

static void RoadmapSection(WebRequest& r, const String& family, const String& section)
{
	if(!AtlasFamilyReadable(r, family)) {
		r.Status(404, "roadmap not found");
		return;
	}
	RoadmapData d;
	String err;
	if(!LoadRoadmap(r, family, d, err)) {
		r.Status(404, err);
		return;
	}
	bool edit = AtlasCanEditFamily(r, family);
	String body;
	if(section == "diagram") body = DiagramBody(r, d, edit, family);
	else if(section == "timeline") body = TimelineBody(r, d, edit, family);
	else if(section == "rules") body = RulesBody(r, d, edit, family);
	else if(section == "tasks") body = TasksBody(r, d, edit, family);
	else if(section == "checks") body = ChecksBody(r, d);
	else if(section == "warnings") body = WarningsBody(r, d, edit, family);
	else if(section == "next") body = NextBody(r, d, edit, family);
	else body = VersionsBody(r, d, edit, family);
	if(!edit && section != "checks" && section != "warnings" && section != "timeline" && section != "next" && section != "diagram")
		body = "<p class=\"ad-note\">Read-only: editing needs the admin role on this site.</p>" + body;
	String attrs = " data-csrf=\"" + AtlasEsc(r.CsrfToken()) + "\"";
	body += "<script>" + AtlasRoadmapScript() + "</script>";
	AtlasPage(r, RoadmapTitle(section), body, attrs, family, section.IsEmpty() ? String("versions") : section);
}

static void RoadmapHome(WebRequest& r)
{
	RoadmapSection(r, r.PathArg(0), "versions");
}

static void RoadmapPage(WebRequest& r)
{
	RoadmapSection(r, r.PathArg(0), r.PathArg(1));
}

void InstallAtlasRoadmapPages(EngineSite& site)
{
	SiteRouter& rt = site.Router();
	rt.Add("atlas/roadmap/*", RoadmapHome);
	rt.Add("atlas/roadmap/*/*", RoadmapPage);
}

// The page script: every form with data-api posts its fields to that API route, sends the CSRF
// token the API checks, and reloads on success. A refusal is shown and the page stays as it is.
String AtlasRoadmapScript()
{
	return R"ARS((function () {
  // Bound once for the document: forms in a detail pane are built after load, and a page change
  // runs this script again, so the guard keeps a single listener.
  if (window.__atlasRoadmapBound) { return; }
  window.__atlasRoadmapBound = true;
  document.addEventListener('submit', function (ev) {
    var form = ev.target && ev.target.closest ? ev.target.closest('form[data-api]') : null;
    if (!form) { return; }
    var confirmText = ev.submitter ? ev.submitter.getAttribute('data-confirm') : null;
    if (confirmText && !window.confirm(confirmText)) { ev.preventDefault(); return; }
    ev.preventDefault();
    var csrf = document.body.getAttribute('data-csrf') || '';
    {
      var parts = [];
      for (var j = 0; j < form.elements.length; j++) {
        var el = form.elements[j];
        if (!el.name || el.disabled) { continue; }
        if ((el.type === 'checkbox' || el.type === 'radio') && !el.checked) { continue; }
        parts.push(encodeURIComponent(el.name) + '=' + encodeURIComponent(el.value));
      }
      fetch(form.getAttribute('data-api'), {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded', 'x-wf-csrf': csrf },
        body: parts.join('&')
      }).then(function (res) {
        return res.json().then(function (j) { return { status: res.status, body: j }; });
      }).then(function (res) {
        if (res.status === 200 && res.body && res.body.ok) {
          if (window.AdShell) { window.AdShell.reload(); } else { window.location.reload(); }
        } else {
          window.alert('Not saved: ' + ((res.body && res.body.error) || res.status));
        }
      }).catch(function () {
        window.alert('Not saved: the server did not answer.');
      });
    }
  });
})();)ARS";
}

END_UPP_NAMESPACE
