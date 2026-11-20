#include "AtlasDiagram.h"

NAMESPACE_UPP

String AtlasMinorGroup(const String& label)
{
	int dot = label.ReverseFind('.');
	return dot < 0 ? label : label.Left(dot);
}

struct AtlasDiagramUnit : Moveable<AtlasDiagramUnit> {
	String element;
	String vid;
	String action;
	Vector<String> entry;
};

static const AtlasElement *FindElement(const AtlasMap& head, const String& id)
{
	for(const AtlasElement& e : head.elements)
		if(e.id == id)
			return &e;
	return NULL;
}

// The element an accomplished literal (done:<name>) names, by element name. Empty when there is none.
static String ElementNamed(const AtlasMap& head, const String& name)
{
	for(const AtlasElement& e : head.elements)
		if(e.name == name)
			return e.id;
	return String();
}

static String LocalPackageLine(const String& pkg, const String& workspace)
{
	if(pkg.IsEmpty() || workspace.IsEmpty())
		return String();
	AtlasLocalAtomCheck check = AtlasCheckLocalAtom(workspace, "built:" + pkg);
	return "local: built:" + pkg + " " + check.status;
}

static bool IsHidden(const Vector<String>& hidden, const String& id)
{
	return !id.IsEmpty() && FindIndex(hidden, id) >= 0;
}

// The delivered features of one minor group, in version order. A x.999 gate version repeats the group's features as its
// contract, so it is left out.
static String GroupDelivers(const AtlasFamily& f, const String& group)
{
	String out;
	for(const AtlasVersion& v : f.versions) {
		if(AtlasMinorGroup(AtlasVersionLabel(v.code)) != group || v.code % 1000 == 999)
			continue;
		for(const String& lit : v.requires)
			if(lit.StartsWith("done:"))
				out << (out.IsEmpty() ? String() : String(", ")) << lit.Mid(5);
	}
	return out.IsEmpty() ? String("no feature declared") : out;
}

// The file path, then one signature line for each class or function span that the element declares; empty when none.
static Vector<String> DeclLines(const Vector<AtlasFile>& files, const String& element)
{
	Vector<String> out;
	for(const AtlasFile& f : files) {
		bool first = true;
		for(const AtlasFileSpan& sp : f.spans) {
			if(sp.element != element || sp.kind.IsEmpty())
				continue;
			if(first)
				out.Add(f.path);
			first = false;
			out.Add("  " + sp.signature);
		}
	}
	return out;
}

// The version label of a version id, or "" when the family has no such version.
static String LabelOfVid(const AtlasFamily& f, const String& vid)
{
	int vi = f.Find(vid);
	return vi < 0 ? String() : AtlasVersionLabel(f.versions[vi].code);
}

// "; declares N classes, M functions" for the visible units of a minor group; "" when the group declares nothing.
static String GroupDeclares(const AtlasFamily& f, const Vector<AtlasDiagramUnit>& units, const Vector<AtlasFile>& files,
                            const Vector<String>& hidden, const String& group)
{
	int classes = 0, functions = 0;
	for(const AtlasDiagramUnit& u : units) {
		if(IsHidden(hidden, u.element) || AtlasMinorGroup(LabelOfVid(f, u.vid)) != group)
			continue;
		for(const AtlasFile& file : files)
			for(const AtlasFileSpan& sp : file.spans) {
				if(sp.element != u.element)
					continue;
				if(sp.kind == "class")
					classes++;
				if(sp.kind == "function")
					functions++;
			}
	}
	if(classes + functions == 0)
		return String();
	return "; declares " + AsString(classes) + " classes, " + AsString(functions) + " functions";
}

// The bubble that produces an entry literal of the work bubble i: the latest contract literal (gate or accomplished)
// of its own version or an earlier one, or the latest effect of a work bubble above it. Work bubbles of the same
// version count only when they come before i, since the version's work runs in order. -1 when nothing produces it.
static int FindProducer(const AtlasVersionDiagram& g, int i, const String& lit)
{
	int r = g.bubbles[i].row;
	for(int q = r; q >= 0; q--) {
		int found = -1;
		for(int j : g.rows[q].bubbles) {
			const AtlasDiagramBubble& b = g.bubbles[j];
			if((b.kind == "gate" || b.kind == "accomplished") && b.literal == lit)
				found = j;
			if(b.kind == "work" && j < i && FindIndex(b.effects, lit) >= 0)
				found = j;
		}
		if(found >= 0)
			return found;
	}
	return -1;
}

// A task as the event log states it now. A task is planned work, not progress: its accepted, done and note events
// are left out, and a dropped task is no longer planned (reopened brings it back).
struct AtlasDiagramTask : Moveable<AtlasDiagramTask> {
    String id;
    String title;
    String target;    // the version id the task must be done for
    String link;      // "element:<id>" or "road:<id>"
    String vid;       // the version the task was logged in
    String assignee;
    bool   processing = false;
    bool   dropped = false;
};

static Vector<AtlasDiagramTask> CollectTasks(const Vector<AtlasEvent>& events)
{
    Vector<AtlasDiagramTask> out;
    for(const AtlasEvent& ev : events) {
        if(ev.subject_kind != "task")
            continue;
        int i = -1;
        for(int k = 0; k < out.GetCount(); k++)
            if(out[k].id == ev.subject_id)
                i = k;
        if(ev.type == "proposed") {
            if(i < 0) {
                AtlasDiagramTask t;
                t.id = ev.subject_id;
                out.Add(pick(t));
                i = out.GetCount() - 1;
            }
            out[i].title = ev.name;
            out[i].target = ev.target;
            out[i].link = ev.link;
            out[i].vid = ev.vid;
            out[i].processing = ev.processing;
            out[i].dropped = false;
        }
        else if(i >= 0 && ev.type == "assigned")
            out[i].assignee = ev.assignee;
        else if(i >= 0 && ev.type == "dropped")
            out[i].dropped = true;
        else if(i >= 0 && ev.type == "reopened")
            out[i].dropped = false;
    }
    return out;
}

// The element ids a task links to: "element:<id>" items, comma separated (one task can serve several features).
static Vector<String> TaskElements(const String& link)
{
    Vector<String> out;
    for(const String& item : Split(link, ',')) {
        String t = TrimBoth(item);
        if(t.StartsWith("element:"))
            out.Add(t.Mid(8));
    }
    return out;
}

// True when every element a task links to is hidden from the viewer. A task with no element link is never hidden.
static bool TaskHidden(const Vector<String>& hidden, const Vector<String>& elements)
{
    if(elements.IsEmpty())
        return false;
    for(const String& id : elements)
        if(!IsHidden(hidden, id))
            return false;
    return true;
}

// The element names a task links to, joined: "Omaha4Trainer, Omaha5Trainer".
static String TaskNames(const AtlasMap& head, const Vector<String>& elements)
{
    String out;
    for(const String& id : elements) {
        const AtlasElement *el = FindElement(head, id);
        if(el)
            out << (out.IsEmpty() ? "" : ", ") << el->name;
    }
    return out;
}

// The task bubble (or task group) that holds an entry literal task:<id>:done; -1 when no visible task has it.
static int FindTask(const AtlasVersionDiagram& g, const String& lit)
{
    for(int j = 0; j < g.bubbles.GetCount(); j++) {
        const AtlasDiagramBubble& b = g.bubbles[j];
        if(b.kind == "task" && b.literal == lit)
            return j;
        if(b.kind == "taskgroup" && FindIndex(b.literals, lit) >= 0)
            return j;
    }
    return -1;
}

void AtlasBuildDiagram(const AtlasFamily& f, const AtlasMap& head, const AtlasRules& rules,
                       const Vector<AtlasEvent>& events, const Vector<AtlasFile>& files, const Vector<String>& hidden,
                       AtlasVersionDiagram& out, const String& workspace)
{
	out.rows.Clear();
	out.bubbles.Clear();
	out.arrows.Clear();
	// A work unit is an element's action: the action named after the element runs in the version of the element's
	// first planned stage event. An event that names its own action overrides the name.
	Vector<AtlasDiagramUnit> units;
	for(const AtlasElement& el : head.elements) {
		String vid, action = rules.FindAction(el.name) >= 0 ? el.name : String();
		Vector<String> entry;
		for(const AtlasEvent& ev : events) {
			if(ev.type != "stage" || ev.subject_id != el.id || ev.stage != "planned")
				continue;
			vid = ev.vid;
			if(!ev.action.IsEmpty())
				action = ev.action;
			for(const String& lit : ev.requires)
				entry.Add(lit);
			break;
		}
		if(vid.IsEmpty() || action.IsEmpty())
			continue;
		AtlasDiagramUnit u;
		u.element = el.id;
		u.vid = vid;
		u.action = action;
		u.entry = pick(entry);
		units.Add(pick(u));
	}

	const Vector<AtlasDiagramTask> tasks = CollectTasks(events);

	String group_prev;
	for(int v = 0; v < f.versions.GetCount(); v++) {
		const AtlasVersion& ver = f.versions[v];
		AtlasDiagramRow row;
		row.vid = ver.vid;
		row.label = AtlasVersionLabel(ver.code);
		row.beta = ver.code % 1000 == 999;
		String group = AtlasMinorGroup(row.label);
		row.group_start = group != group_prev;
		group_prev = group;
		int r = out.rows.GetCount();
		out.rows.Add(pick(row));

		if(row.group_start) {
			AtlasDiagramBubble banner;
			banner.kind = "banner";
			banner.row = r;
			banner.text = group + ": " + GroupDelivers(f, group) + GroupDeclares(f, units, files, hidden, group);
			out.rows[r].bubbles.Add(out.bubbles.GetCount());
			out.bubbles.Add(pick(banner));
		}

		for(const String& lit : ver.requires) {
			AtlasDiagramBubble b;
			b.row = r;
			b.literal = lit;
			if(lit.StartsWith("done:")) {
				b.kind = "accomplished";
				b.text = lit.Mid(5);
				b.element = ElementNamed(head, b.text);
				if(IsHidden(hidden, b.element))
					continue;
			}
			else {
				b.kind = "gate";
				b.text = lit;
			}
			out.rows[r].bubbles.Add(out.bubbles.GetCount());
			out.bubbles.Add(pick(b));
		}

		// Task bubbles sit in the row of the version they were logged in; a task whose logging version is not in the
		// family sits in the row of its target instead. More than four in one row become one task group.
		Vector<int> here;
		for(int k = 0; k < tasks.GetCount(); k++) {
			const AtlasDiagramTask& t = tasks[k];
			if(t.dropped || TaskHidden(hidden, TaskElements(t.link)))
				continue;
			bool logged_here = f.Find(t.vid) >= 0 ? t.vid == ver.vid : t.target == ver.vid;
			if(logged_here)
				here.Add(k);
		}
		// Worker tasks and processing tasks are grouped apart, so a group never mixes their colours.
		for(bool processing : { false, true }) {
		Vector<int> kind;
		for(int k : here)
			if(tasks[k].processing == processing)
				kind.Add(k);
		if(kind.GetCount() > 4) {
			AtlasDiagramBubble g;
			g.kind = "taskgroup";
			g.row = r;
			g.processing = processing;
			g.text = String(processing ? "Processing tasks (" : "Worker tasks (") + AsString(kind.GetCount()) + ")";
			for(int k : kind) {
				const AtlasDiagramTask& t = tasks[k];
				String names = TaskNames(head, TaskElements(t.link));
				g.literals.Add("task:" + t.id + ":done");
				g.declares.Add((t.title.IsEmpty() ? t.id : t.title) + (names.IsEmpty() ? String() : " (" + names + ")"));
			}
			out.rows[r].bubbles.Add(out.bubbles.GetCount());
			out.bubbles.Add(pick(g));
		}
		else
			for(int k : kind) {
				const AtlasDiagramTask& t = tasks[k];
				Vector<String> linked = TaskElements(t.link);
				String names = TaskNames(head, linked);
				AtlasDiagramBubble b;
				b.kind = "task";
				b.row = r;
				b.literal = "task:" + t.id + ":done";
				b.text = t.title.IsEmpty() ? t.id : t.title;
				b.element = linked.IsEmpty() ? String() : linked[0];
				String needed = LabelOfVid(f, t.target);
				b.detail = (needed.IsEmpty() ? String("no version") : "needed by " + needed)
						+ (names.IsEmpty() ? String() : ", for " + names);
				b.assignee = t.assignee;
				b.processing = t.processing;
				out.rows[r].bubbles.Add(out.bubbles.GetCount());
				out.bubbles.Add(pick(b));
			}
		}
		for(int u = 0; u < units.GetCount(); u++) {
			if(units[u].vid != ver.vid || IsHidden(hidden, units[u].element))
				continue;
			AtlasDiagramBubble b;
			b.kind = "work";
			b.row = r;
			b.element = units[u].element;
			const AtlasElement *el = FindElement(head, b.element);
			b.text = el ? el->name : b.element;
			b.pkg = el ? el->pkg : String();
			b.action = units[u].action;
			for(const String& lit : units[u].entry)
				b.requires.Add(lit);
			int ai = rules.FindAction(b.action);
			if(ai >= 0) {
				const AtlasAction& a = rules.actions[ai];
				b.processing = a.processing;
				b.detail = a.text;
				for(const String& lit : a.requires)
					if(FindIndex(b.requires, lit) < 0)
						b.requires.Add(lit);
				for(const String& lit : a.effects)
					b.effects.Add(lit);
			}
			b.placement = AtlasPlacementReason(b.requires);
			b.local = LocalPackageLine(b.pkg, workspace);
			out.rows[r].bubbles.Add(out.bubbles.GetCount());
			out.bubbles.Add(pick(b));

			// The user-interface block: the user actions the element exposes, one line each.
			if(el && el->ui.GetCount()) {
				AtlasDiagramBubble ui;
				ui.kind = "interface";
				ui.row = r;
				ui.element = units[u].element;
				ui.text = el->name;
				for(const AtlasUiAction& a : el->ui)
					ui.declares.Add(a.medium + ": " + a.name + " - " + a.text + (a.feature.GetCount() ? " (" + a.feature + ")" : String()));
				out.rows[r].bubbles.Add(out.bubbles.GetCount());
				out.bubbles.Add(pick(ui));
			}

			Vector<String> decl = DeclLines(files, units[u].element);
			if(decl.GetCount()) {
				AtlasDiagramBubble d;
				d.kind = "declaration";
				d.row = r;
				d.element = units[u].element;
				d.text = el ? el->name : d.element;
				d.declares = pick(decl);
				out.rows[r].bubbles.Add(out.bubbles.GetCount());
				out.bubbles.Add(pick(d));
			}
		}
	}

	// Arrows. An entry literal is fed by FindProducer; a work bubble's effects feed the later contract literals they
	// name. An entry that waits for a task is listed on the bubble, not drawn: a task is not a version literal.
	for(int i = 0; i < out.bubbles.GetCount(); i++) {
		AtlasDiagramBubble& w = out.bubbles[i];
		if(w.kind != "work")
			continue;
		for(const String& lit : w.requires) {
			if(lit.StartsWith("task:")) {
				int from = FindTask(out, lit);
				if(from < 0)
					w.waits.Add(lit);
				else {
					AtlasDiagramArrow a;
					a.from = from;
					a.to = i;
					out.arrows.Add(pick(a));
				}
				continue;
			}
			if(FindIndex(rules.initial, lit) >= 0)
				continue;
			int from = FindProducer(out, i, lit);
			if(from < 0) {
				w.unmet = true;
				continue;
			}
			AtlasDiagramArrow a;
			a.from = from;
			a.to = i;
			out.arrows.Add(pick(a));
		}
		for(const String& lit : w.effects) {
			for(int q = w.row + 1; q < out.rows.GetCount(); q++) {
				int to = -1;
				for(int j : out.rows[q].bubbles)
					if(out.bubbles[j].kind != "work" && out.bubbles[j].kind != "banner" && out.bubbles[j].literal == lit)
						to = j;
				if(to >= 0) {
					AtlasDiagramArrow a;
					a.from = i;
					a.to = to;
					out.arrows.Add(pick(a));
					break;
				}
			}
		}
	}
}

END_UPP_NAMESPACE
