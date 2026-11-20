#include <Core/Core.h>
#include <Atlas/AtlasModel.h>
#include <Atlas/AtlasSite.h>

using namespace Upp;

// atlasver: prints what an implementation worker needs from an Atlas store. It only reads the store's JSON; nothing is
// written, so it is safe on the live store. Usage:
//   atlasver <store data dir> --family <family>             the versions, one line each
//   atlasver <store data dir> --family <family> --version 0.0.3  one version, in full
//   atlasver <store data dir> --family <family> --items 0.0 every version of a minor group: the items a release needs
//   atlasver <store data dir> --family <family> --version 0.1.999 --check-artifacts manifest.json  check gate artifacts
//
// A version is a work item list. Work is an element whose latest stage event sits in that version; its entry is the
// action's requires and its outcome is the action's effects. A x.y.999 version holds test gates only.


static String Label(int code)
{
	return AsString(code / 1000000) + "." + AsString(code / 1000 % 1000) + "." + AsString(code % 1000);
}

// "a.b" is the code of the minor group's first version; "a.b.c" is one version. Returns false on any other text.
static bool ParseCode(const String& s, int& code, bool& is_group)
{
	Vector<String> p = Split(s, '.');
	if(p.GetCount() != 2 && p.GetCount() != 3)
		return false;
	for(const String& part : p)
		if(part.IsEmpty() || !IsDigit(part[0]))
			return false;
	code = StrInt(p[0]) * 1000000 + StrInt(p[1]) * 1000 + (p.GetCount() == 3 ? StrInt(p[2]) : 0);
	is_group = p.GetCount() == 2;
	return true;
}

static ValueMap ReadDoc(const String& path)
{
	return ParseJSON(LoadFile(path));
}

struct Ver : Moveable<Ver> {
	int            code = 0;
	String         vid;
	Vector<String> contract;   // the version's requires: what must hold before it is released
};

struct Work : Moveable<Work> {
	String          element, name, pkg, action, vid;
	bool            processing = false;
	Vector<String>  entry, placement_entry, outcome, feature_lines;
	bool            entry_none = false;
};

struct Task : Moveable<Task> {
	String id, title, target, link, vid;
};

struct VersionStore {
	Vector<Ver>  versions;
	Vector<Work> work;     // one per element that is staged and not removed
	Vector<Task> tasks;    // proposed and not dropped
};

static String Str(const ValueMap& m, const char *key)
{
	return m.Get(key).ToString();
}

static Vector<String> Strings(const ValueMap& m, const char *key)
{
	Vector<String> out;
	ValueArray a = m.Get(key);
	for(int i = 0; i < a.GetCount(); i++)
		out.Add(a[i].ToString());
	return out;
}

static bool HasString(const Vector<String>& strings, const String& value)
{
	for(const String& s : strings)
		if(s == value)
			return true;
	return false;
}

static bool Load(const String& data, const String& family, VersionStore& st)
{
	ValueMap fam = ReadDoc(AppendFileName(AppendFileName(data, "families"), family + ".json")).Get("data");
	ValueArray vers = fam.Get("versions");
	for(int i = 0; i < vers.GetCount(); i++) {
		ValueMap v = vers[i];
		Ver x;
		x.code = (int)v.Get("code");
		x.vid = Str(v, "vid");
		x.contract = Strings(v, "requires");
		st.versions.Add(pick(x));
	}
	for(int i = 1; i < st.versions.GetCount(); i++)
		for(int k = i; k > 0 && st.versions[k - 1].code > st.versions[k].code; k--) {
			Ver tmp = pick(st.versions[k - 1]);
			st.versions[k - 1] = pick(st.versions[k]);
			st.versions[k] = pick(tmp);
		}

	ValueMap map = ReadDoc(AppendFileName(AppendFileName(data, "maps"), family + ".json")).Get("data");
	ValueArray elements = map.Get("elements");
	ValueMap rules = ReadDoc(AppendFileName(AppendFileName(data, "rules"), family + ".json")).Get("data");
	ValueArray actions = rules.Get("actions");

	// Events in id order: the ids are zero-padded, so a string sort is the event order.
	Vector<ValueMap> events;
	FindFile ff(AppendFileName(AppendFileName(data, "events"), family + ".ev-*.json"));
	while(ff) {
		events.Add(ReadDoc(ff.GetPath()).Get("data"));
		ff.Next();
	}
	for(int i = 1; i < events.GetCount(); i++)
		for(int k = i; k > 0 && Str(events[k - 1], "id") > Str(events[k], "id"); k--) {
			ValueMap tmp = events[k - 1];
			events[k - 1] = events[k];
			events[k] = tmp;
		}

	// The latest stage of each element decides where it is. A removed element has no work.
	VectorMap<String, ValueMap> staged;
	Index<String> removed;
	VectorMap<String, ValueMap> tasks;
	for(const ValueMap& e : events) {
		String kind = Str(e, "subject_kind"), id = Str(e, "subject_id"), type = Str(e, "type");
		if(kind == "element") {
			if(type == "stage")
				staged.GetAdd(id) = e;
			else if(type == "removed")
				removed.FindAdd(id);
		}
		else if(kind == "task") {
			if(type == "proposed")
				tasks.GetAdd(id) = e;
			else if(type == "dropped")
				tasks.RemoveKey(id);
		}
	}

	for(int k = 0; k < staged.GetCount(); k++) {
		const String& id = staged.GetKey(k);
		if(removed.Find(id) >= 0)
			continue;
		const ValueMap& ev = staged[k];
		Work w;
		w.element = id;
		w.vid = Str(ev, "vid");
		w.action = Str(ev, "action");
		w.placement_entry = Strings(ev, "requires");
		for(int i = 0; i < elements.GetCount(); i++) {
			ValueMap el = elements[i];
			if(Str(el, "id") != id)
				continue;
			w.name = Str(el, "name");
			w.pkg = Str(el, "pkg");
			ValueArray ui = el.Get("ui");
			for(int u = 0; u < ui.GetCount(); u++) {
				ValueMap a = ui[u];
				w.feature_lines.Add(Str(a, "name") + " (" + Str(a, "medium") + "): " + Str(a, "text") +
				                    (Str(a, "feature").IsEmpty() ? String() : " [" + Str(a, "feature") + "]"));
			}
		}
		if(w.action.IsEmpty())   // the older stage events name no action: the action is named after its element
			w.action = w.name;
		for(int i = 0; i < actions.GetCount(); i++) {
			ValueMap a = actions[i];
			if(Str(a, "name") != w.action)
				continue;
			w.entry = Strings(a, "requires");
			for(const String& lit : w.entry)
				if(!HasString(w.placement_entry, lit))
					w.placement_entry.Add(lit);
			w.outcome = Strings(a, "effects");
			w.entry_none = a.Get("entry_none").ToString() == "true";
			w.processing = a.Get("processing").ToString() == "true";
		}
		st.work.Add(pick(w));
	}

	for(int k = 0; k < tasks.GetCount(); k++) {
		const ValueMap& ev = tasks[k];
		Task t;
		t.id = tasks.GetKey(k);
		t.title = Str(ev, "name");
		t.target = Str(ev, "target");
		t.link = Str(ev, "link");
		t.vid = Str(ev, "vid");
		st.tasks.Add(pick(t));
	}
	return true;
}

static int IndexOfVid(const VersionStore& st, const String& vid)
{
	for(int i = 0; i < st.versions.GetCount(); i++)
		if(st.versions[i].vid == vid)
			return i;
	return -1;
}

static String LabelOfVid(const VersionStore& st, const String& vid)
{
	int i = IndexOfVid(st, vid);
	return i < 0 ? String("?") : Label(st.versions[i].code);
}

static String Pad(const String& s, int width)
{
	return s + String(' ', max(0, width - s.GetCount()));
}

static String Join(const Vector<String>& v)
{
	String s;
	for(const String& x : v)
		s << (s.IsEmpty() ? "" : ", ") << x;
	return s;
}

static Vector<String> ArtifactNamesForVersion(const VersionStore& st, const Ver& v)
{
	Vector<String> names;
	for(const Work& w : st.work) {
		if(w.vid != v.vid)
			continue;
		for(const String& atom : w.entry) {
			if(!atom.StartsWith("artifact:"))
				continue;
			String name = atom.Mid(9);
			if(!HasString(names, name))
				names.Add(name);
		}
	}
	return names;
}

static void PrintArtifactChecks(const Vector<AtlasArtifactCheck>& checks)
{
	for(const AtlasArtifactCheck& check : checks) {
		Cout() << "    artifact: " << check.name;
		if(check.kind == "file_list")
			Cout() << " file_list";
		Cout() << " " << check.status << " path=" << check.path;
		if(!check.reason.IsEmpty())
			Cout() << " (" << check.reason << ")";
		Cout() << "\n";
	}
	Cout() << "  " << AtlasArtifactSummaryLine(checks) << "\n";
}

static void PrintVersion(const VersionStore& st, const Ver& v, const String& workspace,
                         bool check_artifacts, const Vector<AtlasArtifactCheck>& artifacts)
{
	bool gate = v.code % 1000 == 999;
	Cout() << "== " << Label(v.code) << "  (" << v.vid << ")" << (gate ? "  test gates only: a beta of the next minor" : "") << "\n";
	Cout() << "  contract: " << (v.contract.IsEmpty() ? String("none") : Join(v.contract)) << "\n";
	int n = 0;
	for(const Work& w : st.work)
		if(w.vid == v.vid)
			n++;
	Cout() << "  work (" << n << "):\n";
	for(const Work& w : st.work) {
		if(w.vid != v.vid)
			continue;
		Cout() << "    " << w.name << (w.pkg.IsEmpty() ? String() : "  [" + w.pkg + "]")
		       << (w.processing ? "  processing" : "") << "\n";
		Cout() << "      entry:   " << (!w.entry.IsEmpty() ? Join(w.entry) : w.entry_none ? String("none") : String("NOT DECLARED")) << "\n";
		Cout() << "      placement: " << AtlasPlacementReason(w.placement_entry) << "\n";
		if(!workspace.IsEmpty())
			for(const String& atom : w.placement_entry) {
				AtlasLocalAtomCheck check = AtlasCheckLocalAtom(workspace, atom);
				Cout() << "      local:    " << atom << "  " << check.status << "  " << check.path << "\n";
			}
		Cout() << "      outcome: " << (w.outcome.IsEmpty() ? String("NOT DECLARED") : Join(w.outcome)) << "\n";
		for(const String& f : w.feature_lines)
			Cout() << "      user action: " << f << "\n";
	}
	n = 0;
	for(const Task& t : st.tasks)
		if(t.vid == v.vid)
			n++;
	Cout() << "  tasks logged here (" << n << "):\n";
	for(const Task& t : st.tasks) {
		if(t.vid != v.vid)
			continue;
		Cout() << "    " << t.id << ": " << t.title << "  (needed by " << LabelOfVid(st, t.target) << ", for " << t.link << ")\n";
	}
	if(check_artifacts)
		PrintArtifactChecks(artifacts);
	Cout() << "\n";
}

static void PrintList(const VersionStore& st)
{
	Cout() << "version      vid    work  tasks  contract\n";
	for(const Ver& v : st.versions) {
		int work = 0, tasks = 0;
		for(const Work& w : st.work)
			work += w.vid == v.vid;
		for(const Task& t : st.tasks)
			tasks += t.vid == v.vid;
		Cout() << Pad(Label(v.code), 12) << " " << Pad(v.vid, 6) << " " << Pad(AsString(work), 4) << "  "
		       << Pad(AsString(tasks), 5) << "  " << v.contract.GetCount() << "\n";
	}
}

static bool LoadWarningState(const String& data, const String& family, Vector<AtlasRoadmapMessage>& findings,
                             Vector<AtlasWarningAck>& acks, String& error)
{
	String store_root = GetFileFolder(data);
	AtlasFamily f;
	String frev;
	if(!AtlasLoadFamily(store_root, family, f, frev, error))
		return false;
	AtlasMap head;
	String hrev;
	if(!AtlasLoadMap(store_root, f.head_key, head, hrev, error))
		return false;
	AtlasRules rules;
	String rrev;
	if(!AtlasLoadRules(store_root, family, rules, rrev, error))
		return false;
	Vector<AtlasEvent> events;
	if(!AtlasLoadEvents(store_root, family, events, error))
		return false;
	Vector<Vector<String>> worlds;
	AtlasRoadmapFindings(head, f, rules, events, findings, worlds);
	return AtlasLoadAcks(store_root, family, acks, error);
}

static bool IsAcknowledged(const Vector<AtlasWarningAck>& acks, const String& id)
{
	for(const AtlasWarningAck& a : acks)
		if(a.warning_id == id)
			return true;
	return false;
}

static void PrintWarnings(const Vector<AtlasRoadmapMessage>& findings, const Vector<AtlasWarningAck>& acks)
{
	int errors = 0, warnings = 0, acknowledged = 0, open = 0;
	for(const AtlasRoadmapMessage& m : findings) {
		if(m.severity == "error") {
			errors++;
			continue;
		}
		if(m.severity != "warning")
			continue;
		warnings++;
		String id = AtlasWarningId(m);
		if(IsAcknowledged(acks, id)) {
			acknowledged++;
			continue;
		}
		open++;
		Cout() << "id=" << id << " severity=" << m.severity << " rule=" << m.rule << " text=" << m.text << "\n";
	}
	Cout() << "warnings: " << warnings << " (acknowledged " << acknowledged << ", open " << open
	       << "); finding errors: " << errors
	       << "; open warnings each raise one unacknowledged-warning error on the check\n";
}

static String OperatorName()
{
	String user = GetEnv("USER");
	if(user.IsEmpty()) user = GetEnv("LOGNAME");
	if(user.IsEmpty()) user = GetEnv("USERNAME");
	return TrimBoth(user);
}

CONSOLE_APP_MAIN
{
	Vector<String> args;
	for(int i = 0; i < CommandLine().GetCount(); i++)
		args.Add(CommandLine()[i]);
	if(args.IsEmpty()) {
		Cout() << "usage: atlasver <store data dir> --family <family> [--version a.b.c | --items a.b | --warnings | --ack <id> --comment <text> | --unack <id>] [--check-local <workspace>] [--check-artifacts <manifest.json>]\n";
		SetExitCode(1);
		return;
	}
	String data = args[0];
	String family;
	String want_version, want_items, workspace, artifact_manifest, ack_id, unack_id, comment;
	bool list_warnings = false, do_ack = false, do_unack = false, has_comment = false, check_artifacts = false;
	for(int i = 1; i < args.GetCount(); i++) {
		if(args[i] == "--family" && i + 1 < args.GetCount())
			family = args[++i];
		else if(args[i] == "--version" && i + 1 < args.GetCount())
			want_version = args[++i];
		else if(args[i] == "--items" && i + 1 < args.GetCount())
			want_items = args[++i];
		else if(args[i] == "--check-local" && i + 1 < args.GetCount())
			workspace = args[++i];
		else if(args[i] == "--check-artifacts" && i + 1 < args.GetCount()) {
			check_artifacts = true;
			artifact_manifest = args[++i];
		}
		else if(args[i] == "--warnings")
			list_warnings = true;
		else if(args[i] == "--ack" && i + 1 < args.GetCount()) {
			do_ack = true;
			ack_id = args[++i];
		}
		else if(args[i] == "--unack" && i + 1 < args.GetCount()) {
			do_unack = true;
			unack_id = args[++i];
		}
		else if(args[i] == "--comment" && i + 1 < args.GetCount()) {
			has_comment = true;
			comment = args[++i];
		}
		else {
			Cout() << "unknown argument: " << args[i] << "\n";
			SetExitCode(1);
			return;
		}
	}
	if(family.IsEmpty()) {
		Cout() << "--family is required\n";
		SetExitCode(1);
		return;
	}

	int commands = (list_warnings ? 1 : 0) + (do_ack ? 1 : 0) + (do_unack ? 1 : 0);
	if((commands && (!want_version.IsEmpty() || !want_items.IsEmpty() || !workspace.IsEmpty() || check_artifacts))
	|| (!want_version.IsEmpty() && !want_items.IsEmpty()) || (has_comment && !do_ack)) {
		Cout() << "warning commands and version checks must be used separately; --comment requires --ack\n";
		SetExitCode(1);
		return;
	}
	if(commands > 1) {
		Cout() << "choose only one warning command\n";
		SetExitCode(1);
		return;
	}
	if(commands) {
		Vector<AtlasRoadmapMessage> findings;
		Vector<AtlasWarningAck> acks;
		String error;
		if(!LoadWarningState(data, family, findings, acks, error)) {
			Cout() << error << "\n";
			SetExitCode(1);
			return;
		}
		if(list_warnings) {
			PrintWarnings(findings, acks);
			return;
		}
		if(do_ack) {
			String actor = OperatorName();
			if(actor.IsEmpty()) {
				PrintWarnings(findings, acks);
				Cout() << "cannot determine the operating-system user name\n";
				SetExitCode(1);
				return;
			}
			AtlasWarningAck ack;
			int status;
			if(!AtlasBuildWarningAck(findings, ack_id, comment, actor, AtlasNowIso(), ack, status, error)) {
				PrintWarnings(findings, acks);
				Cout() << "refused: " << error << "\n";
				SetExitCode(1);
				return;
			}
			if(!AtlasSaveAck(GetFileFolder(data), family, ack, error)) {
				Cout() << "could not save acknowledgement: " << error << "\n";
				SetExitCode(1);
				return;
			}
			Cout() << "acknowledged " << ack.warning_id << " by " << ack.actor << " at " << ack.at << "\n";
			return;
		}
		String id = TrimBoth(unack_id);
		if(!AtlasWarningIdValid(id)) {
			Cout() << "refused: id must be the warning id from the warnings list\n";
			SetExitCode(1);
			return;
		}
		if(!AtlasDeleteAck(GetFileFolder(data), family, id, error)) {
			Cout() << "could not remove acknowledgement: " << error << "\n";
			SetExitCode(1);
			return;
		}
		Cout() << "unacknowledged " << id << "\n";
		return;
	}

	VersionStore st;
	Load(data, family, st);
	if(check_artifacts && want_version.IsEmpty()) {
		Cout() << "--check-artifacts requires --version a.b.c\n";
		SetExitCode(1);
		return;
	}
	if(!workspace.IsEmpty() && want_version.IsEmpty() && want_items.IsEmpty()) {
		Cout() << "--check-local requires --version or --items\n";
		SetExitCode(1);
		return;
	}

	if(want_version.IsEmpty() && want_items.IsEmpty()) {
		PrintList(st);
		return;
	}

	int code = 0;
	bool is_group = false;
	String asked = want_version.IsEmpty() ? want_items : want_version;
	if(!ParseCode(asked, code, is_group)) {
		Cout() << "not a version or minor group: " << asked << " (use a.b.c or a.b)\n";
		SetExitCode(1);
		return;
	}
	if(!want_version.IsEmpty() && is_group) {
		Cout() << "--version takes a.b.c; use --items a.b for a minor group\n";
		SetExitCode(1);
		return;
	}
	bool any = false;
	for(const Ver& v : st.versions) {
		bool in = is_group ? (v.code / 1000 == code / 1000) : (v.code == code);
		if(!in)
			continue;
		Vector<AtlasArtifactCheck> artifact_checks;
		if(check_artifacts) {
			Vector<String> artifact_names = ArtifactNamesForVersion(st, v);
			String error;
			if(!AtlasCheckArtifactManifest(artifact_names, artifact_manifest, artifact_checks, error)) {
				Cout() << "artifact manifest error: " << error << "\n";
				SetExitCode(1);
				return;
			}
		}
		PrintVersion(st, v, workspace, check_artifacts, artifact_checks);
		any = true;
	}
	if(!any) {
		Cout() << "no version " << asked << " in " << family << "\n";
		SetExitCode(1);
	}
}
