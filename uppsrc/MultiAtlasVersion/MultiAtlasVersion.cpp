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
int Parse(CliArgs& o, const Vector<String>& a) {
	if(a.IsEmpty()) return Error("usage: MultiAtlasVersion <site_dir> --ensemble <name> <command>");
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
bool Git(const String& repo,const Vector<String>& args,String& output) {
	Vector<String> a;a.Add("-C");a.Add(repo);for(const String&arg:args)a.Add(arg);LocalProcess p;if(!p.Start2("git",a))return false;p.CloseWrite();output.Clear();uint32 started=GetTickCount();for(;;){String out,er;bool more=p.Read2(out,er);output<<out;if(!more)break;if(GetTickCount()-started>10000){p.Kill();return false;}Sleep(2);}return p.GetExitCode()==0;
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
bool MetaReady(const MultiAtlasReplayInput& in,const String& vid,Vector<String>& missing,String& err) {
	MultiAtlasReplayContext ctx;MultiAtlasReplayEnsemble(in,ctx);if(!ctx.fatal_error.IsEmpty()){err=ctx.fatal_error;return false;}
	const MultiAtlasVersion *target=NULL;for(const auto&v:in.meta.versions)if(v.vid==vid||VLabel(v.code)==vid){target=&v;break;}if(!target){err="unknown meta milestone";return false;}
	for(const String& req:target->requires){MultiAtlasContractRef ref;String parse;if(!MultiAtlasParseContract(req,ref,parse)){err=parse;return false;}if(ref.track_id.IsEmpty())continue;int i=TrackIndex(in,ref.track_id);if(i<0||ctx.exported_atoms_by_track.Find(ref.track_id)<0||!Has(ctx.exported_atoms_by_track.Get(ref.track_id),ref.atom_name))missing.Add(req);}
	return missing.IsEmpty();
}
bool Emit(const MultiAtlasEnsembleDef& ens,const String& tid,const String& family,const String& action,const String& subject,const String& text,const String& vid,const Vector<String>& effects,String& err) {
	String root=MultiAtlasResolveTrackStore(ens,tid); if(root.IsEmpty()){err="store unavailable";return false;}
	MultiAtlasEvent ev;ev.id=EventId(action);ev.at=Stamp();ev.actor=Actor();ev.track_id=tid;ev.vid=vid;ev.subject_kind=action=="TaskDone"?"task":"feedback";ev.subject_id=subject;ev.type=action=="TaskDone"?"done":"feedback";ev.action=action;ev.text=text;ev.effects<<=effects;
	if(!MultiAtlasPutTrackEvent(root,family,tid,ev,err)){err<<" (family="<<family<<", id="<<ev.id<<", key-length="<<(family.GetCount()+4+ev.id.GetCount())<<")";return false;}return true;
}
}

CONSOLE_APP_MAIN {
	Vector<String> raw;for(int i=0;i<CommandLine().GetCount();i++)raw.Add(CommandLine()[i]);
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
	if(o.command=="--meta-advance"){String next=NextMeta(input);if(next!=o.milestone){Error("milestone is not the next unrecorded milestone: "+next);return;}Vector<String> missing;if(!MetaReady(input,o.milestone,missing,err)){Error(err.IsEmpty()?"required child gates are missing":err);for(const auto&m:missing)Cout()<<"missing "<<m<<"\n";return;}String root=MultiAtlasResolveTrackStore(ens,"meta");MultiAtlasEvent ev;ev.id=EventId("milestone");ev.at=Stamp();ev.actor=Actor();ev.track_id="meta";ev.vid=o.milestone;ev.subject_kind="milestone";ev.subject_id=o.milestone;ev.type="milestone";ev.action="Advance";ev.text="Integrated milestone "+o.milestone;if(!MultiAtlasPutTrackEvent(root,ens.meta_family,"meta",ev,err)){Error(err);return;}Cout()<<"recorded milestone "<<o.milestone<<"\n";return;}
	if(o.command=="--feedback") {const auto*t=Track(ens,o.track);if(!t){Error("unknown track");return;}if(o.severity!="blocker"&&o.severity!="friction"&&o.severity!="suggestion"){Error("invalid feedback severity");return;}Vector<String> fx;fx.Add("category:"+o.category);fx.Add("severity:"+o.severity);if(!Emit(ens,o.track,t->family,"Feedback",EventId("feedback"),o.note,o.version,fx,err)){Error(err);return;}Cout()<<"feedback recorded\n";return;}
	if(o.command=="--feedback-list"){int count=0;if(o.json)Cout()<<"[";for(const auto&t:input.tracks)if(o.track.IsEmpty()||o.track==t.track_id)for(const auto&e:t.events)if(e.type=="feedback"){bool resolved=false;for(const auto&x:t.events)if(x.action=="ResolveFeedback"&&x.subject_id==e.subject_id)resolved=true;if(o.unresolved&&resolved)continue;if(o.json){if(count)Cout()<<",";Cout()<<"{\"track\":"<<J(t.track_id)<<",\"id\":"<<J(e.subject_id)<<",\"note\":"<<J(e.text)<<",\"resolved\":"<<(resolved?"true":"false")<<"}";}else Cout()<<t.track_id<<" "<<e.subject_id<<": "<<e.text<<(resolved?" [resolved]":" [open]")<<"\n";count++;}if(o.json)Cout()<<"]\n";else if(!count)Cout()<<"no feedback\n";return;}
	Error("unsupported command");
}
