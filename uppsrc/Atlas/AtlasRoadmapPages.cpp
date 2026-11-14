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

static String VidLabel(const RoadmapData& d, const String& vid)
{
	int i = d.f.Find(vid);
	return i >= 0 ? AtlasVersionLabel(d.f.versions[i].code) : vid;
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
	  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Version</th><th>Code</th><th>Contract on entry</th><th>Map</th></tr>";
	for(const AtlasVersion& v : d.f.versions) {
		b << "<tr><td>" << AtlasEsc(AtlasVersionLabel(v.code)) << (v.vid == d.f.versions[d.f.Head()].vid ? String(" <span class=\"ad-pill is-info\">head</span>") : String())
		  << "</td><td class=\"ad-mono\">" << v.code << "</td><td>";
		b << (v.requires.GetCount() ? Pills(v.requires, "is-warn") : String("<span class=\"ad-empty\">none</span>"));
		if(edit)
			b << ApiForm(Api(r, family, "/contract"))
			  << "<input type=\"hidden\" name=\"vid\" value=\"" << AtlasEsc(v.vid) << "\">"
			  << "<label>requires <input name=\"requires\" class=\"ad-wide\" value=\"" << AtlasEsc(JoinComma(v.requires)) << "\"></label>"
			  << "<button class=\"ad-btn\" type=\"submit\">Save contract</button></form>";
		b << "</td><td><a href=\"" << AtlasEsc(r.Site().Url("atlas/" + d.f.head_key + "?vid=" + v.vid)) << "\">open at this version</a></td></tr>";
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

static String TimelineBody(WebRequest& r, const RoadmapData& d, bool edit, const String& family)
{
	StringBuffer b;
	b << "<section class=\"ad-panel\"><header><h2>Timeline</h2><p>" << d.events.GetCount()
	  << " events, newest first. Events are only added, never rewritten.</p></header>"
	  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Version</th><th>At</th><th>By</th><th>Subject</th><th>Event</th><th>Detail</th></tr>";
	for(int i = d.events.GetCount() - 1; i >= 0; i--) {
		const AtlasEvent& e = d.events[i];
		String subject = AtlasEsc(e.subject_kind) + ": " + AtlasEsc(e.subject_id);
		b << "<tr><td>" << AtlasEsc(VidLabel(d, e.vid)) << "</td><td class=\"ad-mono\">" << AtlasEsc(e.at)
		  << "</td><td>" << AtlasEsc(e.actor) << "</td><td>" << subject << "</td><td>" << AtlasEsc(e.type)
		  << "</td><td>" << EventDetail(e) << "</td></tr>";
	}
	b << "</table></div></section>";
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
	String id, title, target, link, state;
};

static void CollectTasks(const RoadmapData& d, Vector<TaskRow>& out)
{
	for(const AtlasEvent& e : d.events) {
		if(e.subject_kind != "task")
			continue;
		int i = -1;
		for(int k = 0; k < out.GetCount(); k++)
			if(out[k].id == e.subject_id)
				i = k;
		if(i < 0) {
			TaskRow t;
			t.id = e.subject_id;
			out.Add(t);
			i = out.GetCount() - 1;
		}
		TaskRow& t = out[i];
		if(e.type == "proposed") {
			t.title = e.name;
			t.target = e.target;
			t.link = e.link;
			t.state = "proposed";
		}
		else if(e.type == "accepted" || e.type == "reopened")
			t.state = "accepted";
		else if(e.type == "done")
			t.state = "done";
		else if(e.type == "dropped")
			t.state = "dropped";
	}
}

static String TasksBody(WebRequest& r, const RoadmapData& d, bool edit, const String& family)
{
	Vector<TaskRow> tasks;
	CollectTasks(d, tasks);
	String head_vid = d.f.versions.IsEmpty() ? String() : d.f.versions[d.f.Head()].vid;
	StringBuffer b;
	b << "<section class=\"ad-panel\"><header><h2>Tasks</h2><p>Work that has to be done before its target version can be entered. Done at a later version than its target is too late.</p></header>"
	  << "<div class=\"ad-table-wrap\"><table class=\"ad-table\"><tr><th>Task</th><th>Target</th><th>About</th><th>State</th><th></th></tr>";
	for(const TaskRow& t : tasks) {
		b << "<tr><td><span class=\"ad-mono\">" << AtlasEsc(t.id) << "</span> " << AtlasEsc(t.title) << "</td><td>"
		  << AtlasEsc(VidLabel(d, t.target)) << "</td><td class=\"ad-mono\">" << AtlasEsc(t.link) << "</td><td>";
		String cls = t.state == "done" ? String("is-ok") : t.state == "dropped" ? String("is-muted") : String("is-warn");
		b << "<span class=\"ad-pill " << cls << "\">" << AtlasEsc(t.state) << "</span></td><td>";
		if(edit) {
			String hidden = "<input type=\"hidden\" name=\"id\" value=\"" + AtlasEsc(t.id) + "\">";
			if(t.state == "proposed")
				b << ApiForm(Api(r, family, "/task/accept"), "ad-inline") << hidden << "<button class=\"ad-btn\" type=\"submit\">Accept</button></form>";
			if(t.state == "accepted")
				b << ApiForm(Api(r, family, "/task/done"), "ad-inline") << hidden << "<button class=\"ad-btn is-primary\" type=\"submit\">Done</button></form>";
			if(t.state == "proposed" || t.state == "accepted")
				b << ApiForm(Api(r, family, "/task/drop"), "ad-inline") << hidden << "<button class=\"ad-btn is-danger\" type=\"submit\">Drop</button></form>";
			if(t.state == "done" || t.state == "dropped")
				b << ApiForm(Api(r, family, "/task/reopen"), "ad-inline") << hidden << "<button class=\"ad-btn\" type=\"submit\">Reopen</button></form>";
		}
		b << "</td></tr>";
	}
	if(!tasks.GetCount())
		b << "<tr><td colspan=\"5\" class=\"ad-empty\">No tasks yet.</td></tr>";
	b << "</table></div>";
	if(edit)
		b << ApiForm(Api(r, family, "/task/propose"))
		  << Field("Id", "id", "", "") << Field("Title", "title", "", "ad-wide")
		  << "<label>Must be done for <select name=\"target\">" << VersionOptions(d, head_vid, false) << "</select></label>"
		  << Field("About (element:id or road:id)", "link", "", "")
		  << "<label>Details <textarea name=\"text\"></textarea></label>"
		  << "<button class=\"ad-btn is-primary\" type=\"submit\">Propose task</button></form>";
	b << "</section>";
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
  var csrf = document.body.getAttribute('data-csrf') || '';
  var forms = document.querySelectorAll('form[data-api]');
  for (var i = 0; i < forms.length; i++) {
    forms[i].addEventListener('submit', function (ev) {
      ev.preventDefault();
      var form = this;
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
          window.location.reload();
        } else {
          window.alert('Not saved: ' + ((res.body && res.body.error) || res.status));
        }
      }).catch(function () {
        window.alert('Not saved: the server did not answer.');
      });
    });
  }
})();)ARS";
}

END_UPP_NAMESPACE
