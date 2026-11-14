#include "AtlasSite.h"

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
	Vector<AtlasRoadmapMessage> messages;
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
	AtlasReplayRoadmap(d.head, d.f, d.rules, d.events, d.messages, d.worlds);
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

static const char *RoadmapTitle(const String& section)
{
	if(section == "timeline") return "Timeline";
	if(section == "rules") return "Rules";
	if(section == "tasks") return "Tasks";
	if(section == "checks") return "Checks";
	return "Versions";
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
	bool edit = AtlasCanEdit(r);
	String body;
	if(section == "timeline") body = TimelineBody(r, d, edit, family);
	else if(section == "rules") body = RulesBody(r, d, edit, family);
	else if(section == "tasks") body = TasksBody(r, d, edit, family);
	else if(section == "checks") body = ChecksBody(r, d);
	else body = VersionsBody(r, d, edit, family);
	if(!edit && section != "checks" && section != "timeline")
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
