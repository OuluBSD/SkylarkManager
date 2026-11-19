#include <Core/Core.h>
#include <MultiAtlas/MultiAtlasRegistry.h>
#include <MultiAtlas/MultiAtlasReplayLoader.h>
#include <MultiAtlas/MultiAtlasReplay.h>
#include <MultiAtlas/MultiAtlasScheduler.h>
#include <MultiAtlas/MultiAtlasStore.h>
#include <MultiAtlas/MultiAtlasContract.h>
#include <MultiAtlas/MultiAtlasWarningLedger.h>

using namespace Upp;

namespace {
struct CliArgs {
	String site, ensemble, track, version, task, note, category, severity, commit, milestone;
	String command;
	bool json=false, unresolved=false;
};
String J(const String& s) { return AsJSON(s); }
int Error(const String& s, int code=1) { Cout()<<s<<"\n"; SetExitCode(code); return code; }
String Stamp() { Time t=GetSysTime(); return Format("%04d%02d%02d",t.year,t.month,t.day)+"t"+Format("%02d%02d%02d",t.hour,t.minute,t.second); }
String Actor() { String s=GetEnv("USER"); if(s.IsEmpty()) s=GetEnv("USERNAME"); return s.IsEmpty()?"cli-agent":s; }
String EventId(const String& prefix) { return ToLower(prefix)+"-"+Stamp()+"-"+AsString(GetTickCount()); }
const MultiAtlasTrackDef *Track(const MultiAtlasEnsembleDef& e,const String& id) { for(const auto& t:e.tracks) if(t.track_id==id)return &t; return NULL; }
String Usage()
{
	return "usage: MultiAtlasVersion <site_dir> --ensemble <name> <command>\n"
	       "  --meta-check                   Show the next milestone and unmet requirements\n"
	       "  --meta-trial-pass <version>    Record committed evidence for a .999 meta trial\n"
	       "  --meta-advance <milestone-id>  Record the next eligible milestone\n"
	       "  --meta-record-gate <track> <version>  Record a verified child gate\n"
	       "  --status | --check | --deps | --version <version> | --next\n"
	       "  --task-done <task> | --gate-check <version> | --gate-pass <version>\n"
	       "  --feedback | --feedback-list";
}
int Parse(CliArgs& o, const Vector<String>& a) {
	if(a.IsEmpty()) return Error(Usage());
	o.site=a[0];int commands=0;
	for(int i=1;i<a.GetCount();i++) {
		String x=a[i];
		if(x=="--ensemble"&&i+1<a.GetCount())o.ensemble=a[++i];
		else if(x=="--track"&&i+1<a.GetCount())o.track=a[++i];
		else if(x=="--version"&&i+1<a.GetCount()){o.version=a[++i];if(o.command.IsEmpty()){o.command="--version";commands++;}}
		else if(x=="--task-done"&&i+1<a.GetCount()){o.command=x;o.task=a[++i];commands++;}
		else if(x=="--note"&&i+1<a.GetCount())o.note=a[++i];
		else if(x=="--category"&&i+1<a.GetCount())o.category=a[++i];
		else if(x=="--severity"&&i+1<a.GetCount())o.severity=a[++i];
		else if(x=="--commit"&&i+1<a.GetCount())o.commit=a[++i];
		else if(x=="--meta-trial-pass"&&i+1<a.GetCount()){o.command=x;o.version=a[++i];commands++;}
		else if(x=="--meta-advance"&&i+1<a.GetCount()){o.command=x;o.milestone=a[++i];commands++;}
		else if(x=="--meta-record-gate"&&i+2<a.GetCount()){o.command=x;o.track=a[++i];o.version=a[++i];commands++;}
		else if(x=="--gate-pass"&&i+1<a.GetCount()){o.command=x;o.version=a[++i];commands++;}
		else if(x=="--gate-check"&&i+1<a.GetCount()){o.command=x;o.version=a[++i];commands++;}
		else if(x=="--status"||x=="--check"||x=="--deps"||x=="--next"||x=="--meta-check"||x=="--feedback-list"||x=="--feedback") {o.command=x;commands++;}
		else if(x=="--json")o.json=true;
		else if(x=="--unresolved")o.unresolved=true;
		else return Error("unknown or incomplete argument: "+x);
	}
	if(o.ensemble.IsEmpty()||o.command.IsEmpty()||commands!=1)return Error("--ensemble and exactly one command are required");
	if((o.command=="--next"||o.command=="--task-done"||o.command=="--gate-check"||o.command=="--gate-pass"||o.command=="--feedback")&&o.track.IsEmpty())return Error("--track is required for this command");
	if(o.command=="--task-done"&&(o.note.IsEmpty()||o.task.IsEmpty()))return Error("--task-done requires --note");
	if(o.command=="--feedback"&&(o.note.IsEmpty()||o.category.IsEmpty()||o.severity.IsEmpty()))return Error("--feedback requires --category, --severity, and --note");
	if(o.command=="--meta-record-gate"&&(o.track.IsEmpty()||o.version.IsEmpty()))return Error("--meta-record-gate requires track and gate version");
	return 0;
}
bool Load(const CliArgs& o,MultiAtlasEnsembleDef& e,MultiAtlasReplayInput& in,Vector<MultiAtlasWarningAck>& acks,String& err) {
	if(!MultiAtlasFindEnsemble(o.site,o.ensemble,e,err))return false;
	return MultiAtlasLoadReplayInput(e,in,acks,err);
}
String VLabel(int code) { return Format("%d.%d.%d",code/1000000,(code/1000)%1000,code%1000); }
int TrackIndex(const MultiAtlasReplayInput& in,const String& id) { for(int i=0;i<in.tracks.GetCount();i++)if(in.tracks[i].track_id==id)return i; return -1; }
bool Has(const Vector<String>& v,const String& x) { return FindIndex(v,x)>=0; }
bool SafeGate(const String& s) { if(s.IsEmpty()||s.GetCount()>64)return false;for(int i=0;i<s.GetCount();i++){byte c=s[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'))return false;}return true; }
String GateCommit(const String& repo,String& branch) {
	LocalProcess p; Vector<String> a; a.Add("-C");a.Add(repo);a.Add("rev-parse");a.Add("HEAD");
	if(!p.Start2("git",a))return String();p.CloseWrite();String out,er,chunk,errors;while(p.Read2(chunk,errors)){out<<chunk;er<<errors;}String head=TrimBoth(out);if(p.GetExitCode()!=0)return String();
	a.Clear();a.Add("-C");a.Add(repo);a.Add("symbolic-ref");a.Add("--short");a.Add("-q");a.Add("HEAD");
	LocalProcess q;if(!q.Start2("git",a))return String();q.CloseWrite();out.Clear();errors.Clear();while(q.Read2(chunk,er)){out<<chunk;errors<<er;}branch=TrimBoth(out);return q.GetExitCode()==0?head:String();
}
bool Git(const String& repo, const Vector<String>& args, String& output)
{
	Vector<String> argv = {"--no-replace-objects", "-C", repo};
	for(const String& arg : args) argv.Add(arg);
	LocalProcess process;
	process.ConvertCharset(false);
	if(!process.Start2("git", argv)) return false;
	process.CloseWrite();
	output.Clear();
	uint32 started = GetTickCount();
	for(;;) {
		String chunk, errors;
		bool running = process.Read2(chunk, errors);
		output << chunk;
		if(!running) break;
		if(GetTickCount() - started > 10000 || output.GetCount() > 16 * 1024 * 1024) {
			process.Kill();
			return false;
		}
		Sleep(2);
	}
	return process.GetExitCode() == 0;
}
bool VerifyProof(const MultiAtlasEnsembleDef& ens,const MultiAtlasReplayInput& in,const String& tid,const String& gate,const String& supplied,String& err) {
	const auto*t=Track(ens,tid);if(!t){err="unknown track";return false;}if(!SafeGate(gate)){err="gate version contains unsafe characters";return false;}String branch,head=GateCommit(t->repo,branch);
	if(head.IsEmpty()||branch!=t->branch){err="configured worktree branch/head is unavailable or mismatched";return false;}
	if(!supplied.IsEmpty()&&ToLower(supplied)!=ToLower(head)){err="--commit must equal current worktree HEAD";return false;}
	Vector<String> cmd;String json;String marker=".multiatlas/gates/"+gate+".json";cmd.Add("show");cmd.Add(head+":"+marker);if(!Git(t->repo,cmd,json)){err="committed gate evidence is missing at "+head+":"+marker+" in "+t->repo;return false;}
	Value proof;try{proof=ParseJSON(json);}catch(...){err="committed gate evidence is invalid JSON";return false;}if(!proof.Is<ValueMap>()){err="committed gate evidence must be an object";return false;}ValueMap p(proof);
	String event_id=p.Get("event_id").ToString(),source=p.Get("source_commit").ToString(),vid=p.Get("child_vid").ToString();
	if(p.Get("track_id").ToString()!=tid||p.Get("gate_version").ToString()!=gate||p.Get("result").ToString()!="passed"||event_id.IsEmpty()||source.GetCount()!=40){err="gate evidence fields do not match requested gate";return false;}
	for(int i=0;i<source.GetCount();i++)if(!IsXDigit(source[i])){err="invalid source_commit";return false;}
	cmd.Clear();cmd.Add("merge-base");cmd.Add("--is-ancestor");cmd.Add(source);cmd.Add(head);if(!Git(t->repo,cmd,json)){err="tested source commit is not an ancestor of current HEAD";return false;}
	cmd.Clear();cmd.Add("show");cmd.Add(head+":"+t->store+"/data/events/"+t->family+".ev-"+event_id+".json");if(!Git(t->repo,cmd,json)){err="gate event is not committed at HEAD";return false;}
	MultiAtlasEvent ev;String rev;if(!MultiAtlasLoadRecord(ev,json,rev)||ev.id!=event_id||ev.track_id!=tid||ev.vid!=vid||ev.type!="gate-pass"||ev.action!="GatePass"||ev.text!="Local gate verified at source commit "+ToLower(source)||!Has(ev.effects,"tested:"+gate)){err="committed gate event does not match proof";return false;}
	int ti=TrackIndex(in,tid);bool version=false;if(ti>=0)for(const auto&v:in.tracks[ti].versions)if(v.vid==vid&&(v.vid==gate||VLabel(v.code)==gate))version=true;
	MultiAtlasReplayContext ctx;MultiAtlasReplayEnsemble(in,ctx);if(!version||ctx.exported_atoms_by_track.Find(tid)<0||!Has(ctx.exported_atoms_by_track.Get(tid),"tested:"+gate)){err="replay does not confirm the requested gate version";return false;}
	return true;
}
String NextMeta(const MultiAtlasReplayInput& in) {
	for(const auto&v:in.meta.versions){bool seen=false;for(const auto&e:in.meta.events)if(e.vid==v.vid&&e.type=="milestone")seen=true;if(!seen)return v.vid;}
	return String();
}
bool IsTrial(const MultiAtlasVersion& version)
{
	return version.code >= 0 && version.code % 1000 == 999;
}

String TrialAtom(const MultiAtlasVersion& version)
{
	return "tested:" + VLabel(version.code);
}

String TrialId(const MultiAtlasVersion& version)
{
	return "meta-trial-" + VLabel(version.code);
}

bool TrialRecorded(const MultiAtlasReplayInput& in, const MultiAtlasVersion& version)
{
	for(const MultiAtlasEvent& event : in.meta.events)
		if(event.id == TrialId(version) && event.track_id == "meta" &&
		   event.vid == version.vid && event.type == "gate-pass" &&
		   event.action == "MetaTrialPass" && Has(event.effects, TrialAtom(version)))
			return true;
	return false;
}

bool MetaReady(MultiAtlasReplayInput& in, const String& vid, Vector<String>& missing,
               String& err, bool recording_trial = false)
{
	int target = -1, trial = -1;
	Vector<Vector<int>> trial_requirements;
	trial_requirements.SetCount(in.meta.versions.GetCount());
	for(int i = 0; i < in.meta.versions.GetCount(); i++) {
		const MultiAtlasVersion& version = in.meta.versions[i];
		if(IsTrial(version) && (version.vid != vid || !recording_trial))
			trial = i;
		if(version.vid == vid) {
			target = i;
			break;
		}
	}
	if(target < 0) {
		err = "unknown meta milestone";
		return false;
	}

	// A trial's own atom is an exit requirement. Replay checks entry
	// requirements before it can apply the trial event in that version.
	for(int i = 0; i <= target; i++) {
		MultiAtlasVersion& version = in.meta.versions[i];
		if(!IsTrial(version)) continue;
		String atom = TrialAtom(version);
		for(int j = version.requires.GetCount() - 1; j >= 0; j--)
			if(version.requires[j] == atom) {
				trial_requirements[i].Add(j);
				version.requires.Remove(j);
			}
	}
	MultiAtlasReplayContext replay;
	MultiAtlasReplayEnsemble(in, replay);
	for(int i = 0; i <= target; i++)
		for(int j = trial_requirements[i].GetCount() - 1; j >= 0; j--)
			in.meta.versions[i].requires.Insert(trial_requirements[i][j], TrialAtom(in.meta.versions[i]));

	if(!replay.fatal_error.IsEmpty()) {
		err = replay.fatal_error;
		return false;
	}
	Index<String> relevant_versions;
	for(int i = 0; i <= target; i++)
		relevant_versions.Add(in.meta.versions[i].vid);
	for(const MultiAtlasRoadmapMessage& message : replay.messages)
		if(message.track_id == "meta" &&
		   (message.vid.IsEmpty() || relevant_versions.Find(message.vid) >= 0)) {
			if(message.rule == "contract-gate") {
				if(!Has(missing, message.literal)) missing.Add(message.literal);
			}
			else if(message.severity == "error")
				err = message.text;
		}
	if(trial >= 0) {
		const MultiAtlasVersion& gate = in.meta.versions[trial];
		int meta = replay.version_worlds_by_track.Find("meta");
		String atom = TrialAtom(gate);
		if((!TrialRecorded(in, gate) || meta < 0 ||
		    !Has(replay.version_worlds_by_track[meta].Get(vid), atom)) &&
		   !Has(missing, atom))
			missing.Add(atom);
	}
	return missing.IsEmpty() && err.IsEmpty();
}

bool HexDigest(const String& digest, int size)
{
	if(digest.GetCount() != size) return false;
	for(byte c : digest)
		if(!IsXDigit(c)) return false;
	return true;
}

bool ReadCommittedFile(const String& repo, const String& head, const String& path, String& content)
{
	String entry;
	return Git(repo, {"ls-tree", head, "--", ":(literal)" + path}, entry) &&
	       entry.StartsWith("100644 blob ") &&
	       Git(repo, {"cat-file", "blob", head + ":" + path}, content);
}

bool SafeReportName(const String& name)
{
	return SafeGate(name) && name[0] != '.' && name.EndsWith(".json") && name != "evidence.json";
}

bool ReadJsonObject(const String& json, ValueMap& object)
{
	Value value = ParseJSON(json);
	if(!value.Is<ValueMap>()) return false;
	object = ValueMap(value);
	return true;
}

struct MetaTrialProof {
	String head, source, marker, report, report_hash;
};

bool VerifyMetaTrial(const MultiAtlasEnsembleDef& ens, const MultiAtlasVersion& version,
                     MetaTrialProof& proof, String& err)
{
	const String& repo = ens.meta_repo;
	String branch, output, marker_json, report_json;
	String label = VLabel(version.code);
	String directory = ".multiatlas/meta-trials/" + label + "/";
	proof.marker = directory + "evidence.json";
	proof.head = GateCommit(repo, branch);
	if(proof.head.IsEmpty() || branch != "main" ||
	   !Git(repo, {"rev-parse", "--show-prefix"}, output) || !TrimBoth(output).IsEmpty()) {
		err = "configured meta repository must be a worktree root on main";
		return false;
	}
	if(!ReadCommittedFile(repo, proof.head, proof.marker, marker_json)) {
		err = "committed meta trial evidence is missing or is not a regular file: " + proof.marker;
		return false;
	}
	ValueMap marker;
	if(!ReadJsonObject(marker_json, marker) || marker.Get("schema") != "multiatlas-meta-trial/v1" ||
	   marker.Get("ensemble") != ens.name || marker.Get("track_id") != "meta" ||
	   marker.Get("version") != label || marker.Get("meta_vid") != version.vid ||
	   marker.Get("result") != "passed") {
		err = "meta trial evidence identity, version, or passing result does not match";
		return false;
	}
	proof.source = marker.Get("source_commit").ToString();
	if(!HexDigest(proof.source, proof.head.GetCount()) ||
	   !Git(repo, {"rev-list", "--parents", "-n", "1", proof.head}, output) ||
	   TrimBoth(output) != proof.head + " " + proof.source) {
		err = "meta trial source commit must be the sole parent of current main HEAD";
		return false;
	}
	String report_name = marker.Get("report").ToString();
	proof.report_hash = marker.Get("report_sha256").ToString();
	if(!SafeReportName(report_name) || !HexDigest(proof.report_hash, 64)) {
		err = "meta trial report name or SHA-256 is invalid";
		return false;
	}
	proof.report = directory + report_name;
	if(!Git(repo, {"diff", "--name-only", "--no-renames", "-z", proof.source, proof.head, "--"}, output)) {
		err = "could not compare meta trial source and evidence commits";
		return false;
	}
	bool marker_changed = false, report_changed = false;
	for(int pos = 0; pos < output.GetCount();) {
		int end = pos;
		while(end < output.GetCount() && output[end]) end++;
		String path = output.Mid(pos, end - pos);
		pos = end + 1;
		if(path == proof.marker) marker_changed = true;
		else if(path == proof.report) report_changed = true;
		else {
			err = "meta trial source is stale because the evidence commit also changes " + path;
			return false;
		}
	}
	if(!marker_changed || !report_changed ||
	   !ReadCommittedFile(repo, proof.head, proof.report, report_json)) {
		err = "meta trial marker and regular report must both be committed in the evidence commit";
		return false;
	}
	if(!Git(repo, {"diff", "--quiet", proof.head, "--", proof.marker, proof.report}, output)) {
		err = "meta trial evidence has uncommitted changes";
		return false;
	}
	if(SHA256String(report_json) != ToLower(proof.report_hash)) {
		err = "meta trial report SHA-256 does not match evidence";
		return false;
	}
	ValueMap report;
	if(!ReadJsonObject(report_json, report) || report.Get("source_commit") != proof.source ||
	   report.Get("version") != label || report.Get("result") != "passed" ||
	   !report.Get("checks").Is<ValueArray>()) {
		err = "meta trial report does not match the tested source, version, or passing result";
		return false;
	}
	ValueArray checks = report.Get("checks");
	Index<String> names;
	for(const Value& value : checks) {
		if(!value.Is<ValueMap>()) {
			err = "meta trial check must be an object";
			return false;
		}
		ValueMap check(value);
		String name = check.Get("name").ToString();
		if(!check.Get("name").Is<String>() || TrimBoth(name).IsEmpty() ||
		   names.Find(name) >= 0 || check.Get("result") != "passed") {
			err = "every meta trial check must have a unique name and a passing result";
			return false;
		}
		names.Add(name);
	}
	if(checks.IsEmpty()) {
		err = "meta trial report must include at least one passing check";
		return false;
	}
	return true;
}

void RecordMetaTrial(const MultiAtlasEnsembleDef& ens, MultiAtlasReplayInput& in,
                     const String& requested)
{
	const MultiAtlasVersion *target = NULL;
	for(const MultiAtlasVersion& version : in.meta.versions)
		if(version.vid == requested || VLabel(version.code) == requested) {
			target = &version;
			break;
		}
	if(!target || !IsTrial(*target)) {
		Error("meta trial pass requires a configured .999 meta version");
		return;
	}
	String err;
	MetaTrialProof proof;
	if(!VerifyMetaTrial(ens, *target, proof, err)) {
		Error("meta trial pass refused: " + err);
		return;
	}
	String next = NextMeta(in);
	if(target->vid != next) {
		Error("trial version is not the next unrecorded milestone: " + next);
		return;
	}
	Vector<String> missing;
	if(!MetaReady(in, target->vid, missing, err, true)) {
		Error(err.IsEmpty() ? "meta trial prerequisites are missing" : err);
		for(const String& requirement : missing) Cout() << "missing " << requirement << '\n';
		return;
	}
	String branch;
	if(GateCommit(ens.meta_repo, branch) != proof.head || branch != "main") {
		Error("meta repository main changed while verifying trial evidence");
		return;
	}
	MultiAtlasEvent event;
	event.id = TrialId(*target);
	event.at = Stamp();
	event.actor = Actor();
	event.track_id = "meta";
	event.vid = target->vid;
	event.subject_kind = "milestone";
	event.subject_id = target->vid;
	event.type = "gate-pass";
	event.action = "MetaTrialPass";
	event.text = "Meta trial passed at source commit " + proof.source + " with evidence "
	             + proof.head + ":" + proof.marker + " and report " + proof.report +
	             " (SHA-256 " + ToLower(proof.report_hash) + ")";
	for(const String& requirement : target->requires)
		if(requirement != TrialAtom(*target)) event.requires.Add(requirement);
	event.effects.Add(TrialAtom(*target));
	if(!MultiAtlasPutTrackEvent(MultiAtlasResolveTrackStore(ens, "meta"), ens.meta_family,
	                           "meta", event, err)) {
		Error(err);
		return;
	}
	Cout() << "recorded meta trial " << VLabel(target->code) << " at " << proof.head << '\n';
}
bool Emit(const MultiAtlasEnsembleDef& ens,const String& tid,const String& family,const String& action,const String& subject,const String& text,const String& vid,const Vector<String>& effects,String& err) {
	String root=MultiAtlasResolveTrackStore(ens,tid); if(root.IsEmpty()){err="store unavailable";return false;}
	MultiAtlasEvent ev;ev.id=EventId(action);ev.at=Stamp();ev.actor=Actor();ev.track_id=tid;ev.vid=vid;ev.subject_kind=action=="TaskDone"?"task":"feedback";ev.subject_id=subject;ev.type=action=="TaskDone"?"done":"feedback";ev.action=action;ev.text=text;ev.effects<<=effects;
	if(!MultiAtlasPutTrackEvent(root,family,tid,ev,err)){err<<" (family="<<family<<", id="<<ev.id<<", key-length="<<(family.GetCount()+4+ev.id.GetCount())<<")";return false;}return true;
}
}

CONSOLE_APP_MAIN {
	Vector<String> raw;for(int i=0;i<CommandLine().GetCount();i++)raw.Add(CommandLine()[i]);
	if(raw.GetCount() == 1 && raw[0] == "--help") {
		Cout() << Usage() << '\n';
		return;
	}
	CliArgs o;if(Parse(o,raw))return;
	MultiAtlasEnsembleDef ens;MultiAtlasReplayInput input;Vector<MultiAtlasWarningAck> acks;String err;
	// Status can inspect registered paths even when stores have not been initialized.
	if(o.command=="--status") {
		if(!MultiAtlasFindEnsemble(o.site,o.ensemble,ens,err)){Error(err);return;}
		bool loaded=MultiAtlasLoadReplayInput(ens,input,acks,err);MultiAtlasReplayContext replay;if(loaded)MultiAtlasReplayEnsemble(input,replay);
		if(o.json)Cout()<<"{\"ensemble\":"<<J(o.ensemble)<<",\"tracks\":[";
		else Cout()<<"TRACK\tBRANCH\tHEAD\tACTIVE\tGATES\n";
		for(int i=0;i<ens.tracks.GetCount();i++){const auto& t=ens.tracks[i];String branch,head=GateCommit(t.repo,branch);
			String active;ValueArray gates;if(loaded){int ti=TrackIndex(input,t.track_id);if(ti>=0&&!input.tracks[ti].versions.IsEmpty()){const auto&v=input.tracks[ti].versions.Top();active=v.vid+" ("+VLabel(v.code)+")";}int ei=replay.exported_atoms_by_track.Find(t.track_id);if(ei>=0)for(const String&atom:replay.exported_atoms_by_track[ei])if(atom.StartsWith("tested:"))gates.Add(atom.Mid(7));}
			if(o.json){if(i)Cout()<<",";Cout()<<"{\"track\":"<<J(t.track_id)<<",\"branch\":"<<J(branch)<<",\"head\":"<<(head.IsEmpty()?"null":J(head))<<",\"active_version\":"<<(active.IsEmpty()?"null":J(active))<<",\"passed_gates\":"<<AsJSON(gates)<<"}";}
			else {String gate_text;for(int k=0;k<gates.GetCount();k++)gate_text<<(k?",":"")<<gates[k];Cout()<<t.track_id<<"\t"<<branch<<"\t"<<head<<"\t"<<(active.IsEmpty()?"-":active)<<"\t"<<(gate_text.IsEmpty()?"-":gate_text)<<"\n";}
		}if(o.json)Cout()<<"]}\n";return;
	}
	if(!Load(o,ens,input,acks,err)){Error(err);return;}
	if(o.command=="--check"){
		MultiAtlasReplayContext c;bool ok=MultiAtlasReplayEnsemble(input,c);if(o.json){Cout()<<"{\"ok\":"<<(ok?"true":"false")<<",\"fatal_error\":"<<J(c.fatal_error)<<",\"total_errors\":"<<c.total_errors<<",\"total_warnings\":"<<c.total_warnings<<",\"messages\":[";for(int i=0;i<c.messages.GetCount();i++){const auto&m=c.messages[i];if(i)Cout()<<",";Cout()<<"{\"severity\":"<<J(m.severity)<<",\"track_id\":"<<J(m.track_id)<<",\"text\":"<<J(m.text)<<"}";}Cout()<<"]}\n";}else{for(const auto&m:c.messages)Cout()<<m.severity<<" ["<<m.track_id<<"] "<<m.text<<"\n";Cout()<<(ok?"PASS":"FAIL")<<" errors="<<c.total_errors<<" warnings="<<c.total_warnings<<"\n";}SetExitCode(ok?0:1);return;
	}
	if(o.command=="--deps") {MultiAtlasTrackGraph g;Vector<String> order;String cycle;if(!MultiAtlasBuildDependencyGraph(input,g,err)||!MultiAtlasTopologicalSort(g,order,cycle)){Error(err.IsEmpty()?cycle:err);return;}Cout()<<"order: ";for(int i=0;i<order.GetCount();i++)Cout()<<(i?" -> ":"")<<order[i];Cout()<<"\n";for(const auto&e:g.edges)Cout()<<e.from_track<<" -> "<<e.to_track<<" ["<<e.contract<<"]\n";return;}
	if(o.command=="--version") {int ti=TrackIndex(input,o.track);const auto*td=Track(ens,o.track);if(ti<0||!td){Error("--version requires a known --track");return;}bool found=false;String actual;const auto&t=input.tracks[ti];for(const auto&v:t.versions)if(v.vid==o.version||VLabel(v.code)==o.version){found=true;actual=v.vid;Cout()<<"version "<<VLabel(v.code)<<" ("<<v.vid<<") requirements: ";for(int i=0;i<v.requires.GetCount();i++)Cout()<<(i?", ":"")<<v.requires[i];Cout()<<"\n";}if(!found){Error("version not found: "+o.version);return;}String root=MultiAtlasResolveTrackStore(ens,o.track),rev;MultiAtlasMap map;if(!MultiAtlasLoadTrackMap(root,td->family,map,rev,err)){Error(err);return;}for(const auto&e:t.events)if(e.vid==actual){String title=e.subject_id;if(e.subject_kind=="element"){int ei=map.FindElement(e.subject_id);if(ei>=0)title=map.elements[ei].name;}Cout()<<e.subject_kind<<" "<<title<<" ["<<e.action<<"]\n  entry: ";for(int i=0;i<e.requires.GetCount();i++)Cout()<<(i?", ":"")<<e.requires[i];Cout()<<"\n  outcome: ";for(int i=0;i<e.effects.GetCount();i++)Cout()<<(i?", ":"")<<e.effects[i];Cout()<<"\n  "<<e.text<<"\n";}return;}
	if(o.command=="--next") {int ti=TrackIndex(input,o.track);if(ti<0){Error("unknown track: "+o.track);return;}const auto&t=input.tracks[ti];MultiAtlasReplayContext c;MultiAtlasReplayEnsemble(input,c);for(const auto&ev:t.events)if(ev.subject_kind=="task"&&ev.type=="proposed"){bool done=false,blocked=false;for(const auto&x:t.events)if(x.subject_kind=="task"&&x.subject_id==ev.subject_id&&(x.type=="done"||x.type=="dropped"))done=true;for(const String&req:ev.requires){MultiAtlasContractRef ref;String pe;if(!MultiAtlasParseContract(req,ref,pe)){blocked=true;continue;}String tid=ref.track_id.IsEmpty()?o.track:ref.track_id;if(c.exported_atoms_by_track.Find(tid)<0||!Has(c.exported_atoms_by_track.Get(tid),ref.atom_name))blocked=true;}if(!done&&!blocked){if(o.json)Cout()<<"{\"id\":"<<J(ev.subject_id)<<",\"title\":"<<J(ev.text)<<",\"track\":"<<J(o.track)<<"}\n";else Cout()<<ev.subject_id<<": "<<ev.text<<"\n";return;}}Error("no unblocked open task");SetExitCode(2);return;}
	if(o.command=="--task-done"){const auto*t=Track(ens,o.track);int ti=TrackIndex(input,o.track);if(!t||ti<0){Error("unknown track");return;}const auto&rt=input.tracks[ti];const MultiAtlasEvent*task=NULL;for(const auto&e:rt.events)if(e.subject_kind=="task"&&e.subject_id==o.task&&e.type=="proposed")task=&e;if(!task){Error("unknown task: "+o.task);return;}for(const auto&e:rt.events)if(e.subject_kind=="task"&&e.subject_id==o.task&&(e.type=="done"||e.type=="dropped")){Error("task is already closed");return;}MultiAtlasReplayContext c;MultiAtlasReplayEnsemble(input,c);for(const String&req:task->requires){MultiAtlasContractRef ref;String pe;if(!MultiAtlasParseContract(req,ref,pe)){Error(pe);return;}String tid=ref.track_id.IsEmpty()?o.track:ref.track_id;if(c.exported_atoms_by_track.Find(tid)<0||!Has(c.exported_atoms_by_track.Get(tid),ref.atom_name)){Error("task is blocked by "+req);return;}}String vid=o.version;if(vid.IsEmpty()){if(rt.versions.IsEmpty()){Error("track has no version to attach task result");return;}vid=rt.versions.Top().vid;}Vector<String> fx;fx.Add("done:"+o.task);if(!Emit(ens,o.track,t->family,"TaskDone",o.task,o.note,vid,fx,err)){Error(err);return;}Cout()<<"task marked done: "<<o.task<<"\n";return;}
	if(o.command=="--gate-check") {MultiAtlasReplayContext c;MultiAtlasReplayEnsemble(input,c);int ti=TrackIndex(input,o.track);bool version=false,blocked=false;if(ti>=0)for(const auto&v:input.tracks[ti].versions)if(v.vid==o.version||VLabel(v.code)==o.version)version=true;for(const auto&m:c.messages)if(m.track_id==o.track&&m.severity=="error")blocked=true;bool pass=version&&!blocked&&c.exported_atoms_by_track.Find(o.track)>=0&&Has(c.exported_atoms_by_track.Get(o.track),"tested:"+o.version);if(o.json)Cout()<<"{\"ok\":"<<(pass?"true":"false")<<",\"track\":"<<J(o.track)<<",\"gate\":"<<J(o.version)<<",\"version_found\":"<<(version?"true":"false")<<",\"blocked\":"<<(blocked?"true":"false")<<"}\n";else Cout()<<"gate "<<o.version<<" "<<(pass?"PASS":"BLOCKED")<<"\n";if(!pass)SetExitCode(1);return;}
	if(o.command=="--gate-pass") {if(!VerifyProof(ens,input,o.track,o.version,o.commit,err)){Error("gate pass refused: "+err);return;}Cout()<<"verified gate proof "<<o.track<<"/"<<o.version<<" at current HEAD\n";return;}
	if(o.command=="--meta-check") {String next=NextMeta(input);Vector<String> missing;bool ok=!next.IsEmpty()&&MetaReady(input,next,missing,err);if(o.json){Cout()<<"{\"ok\":"<<(ok?"true":"false")<<",\"next_milestone\":"<<J(next)<<",\"missing\":[";for(int i=0;i<missing.GetCount();i++)Cout()<<(i?",":"")<<J(missing[i]);Cout()<<"],\"error\":"<<J(err)<<"}\n";}else{Cout()<< (ok?"PASS":"BLOCKED")<<" next="<<(next.IsEmpty()?"none":next)<<"\n";for(const auto&m:missing)Cout()<<"missing "<<m<<"\n";}if(!ok)SetExitCode(1);return;}
	if(o.command=="--meta-record-gate") {if(!VerifyProof(ens,input,o.track,o.version,o.commit,err)){Error("meta gate record refused: "+err);return;}String head,branch=head=GateCommit(Track(ens,o.track)->repo,branch);String id="gate-"+o.track+"-"+o.version+"-"+ToLower(head);String root=MultiAtlasResolveTrackStore(ens,"meta");MultiAtlasEvent ev;ev.id=id;ev.at=Stamp();ev.actor=Actor();ev.track_id="meta";ev.vid=NextMeta(input);ev.subject_kind="element";ev.subject_id="e_track_"+o.track;ev.type="stage";ev.action="RecordGatePass";ev.text="Track "+o.track+" passed gate "+o.version+" at commit "+head;ev.requires.Add(o.track+":tested:"+o.version);if(!MultiAtlasPutTrackEvent(root,ens.meta_family,"meta",ev,err)){Error(err);return;}Cout()<<"recorded child gate "<<o.track<<"/"<<o.version<<"\n";return;}
	if(o.command=="--meta-trial-pass") { RecordMetaTrial(ens, input, o.version); return; }
	if(o.command=="--meta-advance") {
		String next = NextMeta(input);
		if(next.IsEmpty() || next != o.milestone) {
			Error("milestone is not the next unrecorded milestone: " + next);
			return;
		}
		Vector<String> missing;
		if(!MetaReady(input, next, missing, err)) {
			Error(err.IsEmpty() ? "meta milestone prerequisites are missing" : err);
			for(const String& requirement : missing) Cout() << "missing " << requirement << '\n';
			return;
		}
		MultiAtlasEvent event;
		event.id = "milestone-" + SHA256String(next).Left(24);
		event.at = Stamp();
		event.actor = Actor();
		event.track_id = "meta";
		event.vid = next;
		event.subject_kind = "milestone";
		event.subject_id = next;
		event.type = "milestone";
		event.action = "Advance";
		event.text = "Integrated milestone " + next;
		for(const MultiAtlasVersion& version : input.meta.versions)
			if(version.vid == next && IsTrial(version)) {
				event.requires.Add(TrialAtom(version));
				break;
			}
		if(!MultiAtlasPutTrackEvent(MultiAtlasResolveTrackStore(ens, "meta"), ens.meta_family,
		                           "meta", event, err)) {
			Error(err);
			return;
		}
		Cout() << "recorded milestone " << next << '\n';
		return;
	}

	if(o.command=="--feedback") {const auto*t=Track(ens,o.track);if(!t){Error("unknown track");return;}if(o.severity!="blocker"&&o.severity!="friction"&&o.severity!="suggestion"){Error("invalid feedback severity");return;}Vector<String> fx;fx.Add("category:"+o.category);fx.Add("severity:"+o.severity);if(!Emit(ens,o.track,t->family,"Feedback",EventId("feedback"),o.note,o.version,fx,err)){Error(err);return;}Cout()<<"feedback recorded\n";return;}
	if(o.command=="--feedback-list"){int count=0;if(o.json)Cout()<<"[";for(const auto&t:input.tracks)if(o.track.IsEmpty()||o.track==t.track_id)for(const auto&e:t.events)if(e.type=="feedback"){bool resolved=false;for(const auto&x:t.events)if(x.action=="ResolveFeedback"&&x.subject_id==e.subject_id)resolved=true;if(o.unresolved&&resolved)continue;if(o.json){if(count)Cout()<<",";Cout()<<"{\"track\":"<<J(t.track_id)<<",\"id\":"<<J(e.subject_id)<<",\"note\":"<<J(e.text)<<",\"resolved\":"<<(resolved?"true":"false")<<"}";}else Cout()<<t.track_id<<" "<<e.subject_id<<": "<<e.text<<(resolved?" [resolved]":" [open]")<<"\n";count++;}if(o.json)Cout()<<"]\n";else if(!count)Cout()<<"no feedback\n";return;}
	Error("unsupported command");
}
