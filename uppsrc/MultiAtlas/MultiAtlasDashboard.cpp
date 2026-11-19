#include "MultiAtlasDashboard.h"
#include "MultiAtlasRegistry.h"
#include "MultiAtlasReplayLoader.h"
#include "MultiAtlasStore.h"
#include <WebEngine/Request.h>
#include <WebsiteTemplates/WebsiteTemplates.h>

NAMESPACE_UPP

namespace {
String Esc(const String& s) {
	StringBuffer b;
	for(int i=0;i<s.GetCount();i++) switch(s[i]) { case '&': b<<"&amp;"; break; case '<': b<<"&lt;"; break; case '>': b<<"&gt;"; break; case '"': b<<"&quot;"; break; case '\'': b<<"&#39;"; break; default: b.Cat(s[i]); }
	return String(b);
}
String JsonSafe(String s) { s.Replace("<", "\\u003c"); s.Replace(">", "\\u003e"); s.Replace("&", "\\u0026"); return s; }
String SiteRoot(const EngineSite& s) { return GetFileFolder(GetFileFolder(s.data_root)); }
Vector<String> SplitWhitespace(const String& text) {
	Vector<String> words; int i = 0;
	while(i < text.GetCount()) {
		while(i < text.GetCount() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) i++;
		int start = i;
		while(i < text.GetCount() && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n') i++;
		if(i > start) words.Add(text.Mid(start, i - start));
	}
	return words;
}
bool Git(const String& repo, const Vector<String>& args, String& out) {
	if(repo.IsEmpty() || !DirectoryExists(repo)) return false;
	String root = NormalizePath(repo);
	Vector<String> argv; argv.Add("-C"); argv.Add(root); for(const String& a:args) argv.Add(a);
	LocalProcess p; if(!p.Start2("git", argv)) return false; p.CloseWrite(); out.Clear();
 uint32 start=GetTickCount(); for(;;) { String a,b; bool more=p.Read2(a,b); out<<a; if(out.GetCount()>65536) { p.Kill(); return false; } if(!more) break; if(GetTickCount()-start>2000) { p.Kill(); return false; } Sleep(1); }
	return p.GetExitCode()==0;
}
Value GitStatus(const MultiAtlasTrackDef& t) {
	ValueMap v; String out; Vector<String> a; a.Add("rev-parse"); a.Add("--show-toplevel");
	if(!Git(t.repo,a,out)) { v.Add("available",false); return v; }
	String top=NormalizePath(TrimBoth(out));
	// Registry validation establishes repo containment; require git's resolved root to match it.
	if(top != NormalizePath(t.repo)) { v.Add("available",false); return v; }
	a.Clear(); a.Add("symbolic-ref"); a.Add("--short"); a.Add("-q"); a.Add("HEAD"); bool branch_ok=Git(t.repo,a,out); String branch=TrimBoth(out);
	a.Clear(); a.Add("rev-parse"); a.Add("--verify"); a.Add("HEAD"); bool head_ok=Git(t.repo,a,out); String head=TrimBoth(out);
	a.Clear(); a.Add("status"); a.Add("--porcelain"); bool status_ok=Git(t.repo,a,out); bool dirty=!TrimBoth(out).IsEmpty();
	String ahead="?", behind="?"; a.Clear(); a.Add("rev-list"); a.Add("--left-right"); a.Add("--count"); a.Add("main...HEAD");
	if(Git(t.repo,a,out)) { Vector<String> words=SplitWhitespace(TrimBoth(out)); if(words.GetCount()>=2) { behind=words[0]; ahead=words[1]; } }
	v.Add("available",head_ok && status_ok); v.Add("branch",branch_ok?branch:String("unavailable")); v.Add("head",head_ok?head:String()); v.Add("dirty",status_ok?dirty:false); v.Add("ahead_main",ahead); v.Add("behind_main",behind);
	return v;
}
String Label(int code) { return Format("%d.%d.%d",code/1000000,(code/1000)%1000,code%1000); }
Value MakeTimeline(const MultiAtlasEnsembleDef& def, String& error) {
	MultiAtlasReplayInput input; Vector<MultiAtlasWarningAck> acks;
	if(!MultiAtlasLoadReplayInput(def,input,acks,error)) return ValueMap();
	MultiAtlasReplayContext context; MultiAtlasReplayEnsemble(input,context);
	ValueMap root; root.Add("name",def.name); root.Add("title",def.title); root.Add("replay_error",context.fatal_error);
	ValueArray releases, tracks;
	Vector<String> required_labels={"0.1.0","0.2.0","0.3.0","0.4.0","0.5.0","0.7.0","1.0.0"};
	for(const String& wanted:required_labels) {
		ValueMap x; int code=-1;
		for(const MultiAtlasVersion& v:input.meta.versions) if(Label(v.code)==wanted) { code=v.code; ValueArray req; for(const String& atom:v.requires) req.Add(atom); x.Add("requires",req); break; }
		x.Add("label",wanted); x.Add("code",code); releases.Add(x);
	}
	root.Add("releases",releases);
	for(const MultiAtlasTrackDef& d:def.tracks) {
		ValueMap t; t.Add("id",d.track_id); t.Add("branch_expected",d.branch); t.Add("git",GitStatus(d)); ValueArray versions, gates;
		for(const MultiAtlasTrackReplayInput& src:input.tracks) if(src.track_id==d.track_id) {
			for(const MultiAtlasVersion& v:src.versions) { ValueMap x; x.Add("label",Label(v.code)); x.Add("code",v.code); versions.Add(x); }
			int exported=context.exported_atoms_by_track.Find(d.track_id);
			if(exported>=0) for(const String& atom:context.exported_atoms_by_track[exported]) if(atom.StartsWith("tested:")) { ValueMap g; g.Add("gate",atom.Mid(7)); gates.Add(g); }
		}
		t.Add("versions",versions); t.Add("gates",gates); tracks.Add(t);
	}
	root.Add("tracks",tracks); return root;
}
void Timeline(WebRequest& r) {
	MultiAtlasEnsembleDef def; String error;
	if(!MultiAtlasFindEnsemble(SiteRoot(r.Site()),r.PathArg(0),def,error)) { r.Status(error.Find("not found")>=0?404:500,"MultiAtlas ensemble unavailable"); return; }
	if(!def.public_read && !r.HasUser()) { r.Status(404,"not found"); return; }
	Value model=MakeTimeline(def,error); if(model.IsNull()) { r.Status(500,"Timeline data unavailable"); return; }
	String json=JsonSafe(AsJSON(model));
	String body=R"HTML(<main class="mat-page"><h1>)HTML"+Esc(def.title)+R"HTML( · Release timeline</h1><p>Snapshot-based synchronization view. Drag the release slider to inspect precomputed gate readiness.</p><p><a href="/multiatlas/)HTML"+Esc(def.name)+R"HTML(">Ensemble overview</a></p>
<section class="mat-panel"><label for="mat-scrub">Release position <output id="mat-selected"></output></label><input id="mat-scrub" type="range" min="0" max="6" value="0" step="1" aria-describedby="mat-help"><p id="mat-help">Use arrow keys to move between release targets.</p><div id="mat-rail" class="mat-rail" aria-hidden="true"></div></section>
<section class="mat-panel"><h2>Track synchronization</h2><div id="mat-tracks" class="mat-tracks"></div></section><p id="mat-critical" role="status" aria-live="polite"></p>
<script type="application/json" id="mat-data">)HTML"+json+R"HTML(</script></main>
<style>
.mat-page{max-width:1200px;margin:auto;padding:1rem}.mat-panel{padding:1rem;margin:1rem 0;border:1px solid #8792a2;border-radius:.7rem;background:var(--surface,#fff)}.mat-rail,.mat-gate-grid{display:grid;grid-template-columns:repeat(7,minmax(4rem,1fr));gap:.25rem;margin-top:1rem}.mat-release{text-align:center;padding:.65rem .3rem;border-bottom:4px solid #718096;font-weight:650}.mat-release[aria-current=true]{border-color:#1565c0;background:#eaf3ff}.mat-tracks{display:grid;gap:.7rem}.mat-track{display:grid;grid-template-columns:minmax(6rem,1fr) minmax(8rem,1fr) 4fr minmax(12rem,2fr);align-items:center;gap:.6rem;padding:.7rem;border-left:5px solid #8a929d;background:#f5f7fa}.mat-track.pass{border-color:#268344}.mat-track.active{border-color:#b47700}.mat-track.fail{border-color:#b42318}.mat-track.future{border-color:#89919c}.mat-track[ data-blocked="true"]{outline:2px solid #b42318}.mat-status{font-weight:650}.mat-gate-grid{margin:0}.mat-gate{min-height:1.8rem;display:grid;place-items:center;border-bottom:2px solid #c4cbd4;color:#606a77;font-size:.8rem}.mat-gate.required{border-color:#b42318;color:#9b1c13;font-weight:700}.mat-gate.passed{border-color:#268344;color:#1d6d37}.mat-git{font-size:.88rem;overflow-wrap:anywhere}@media(max-width:760px){.mat-rail,.mat-gate-grid{grid-template-columns:repeat(4,minmax(0,1fr))}.mat-track{grid-template-columns:1fr;gap:.4rem}.mat-git{font-size:.82rem}}
</style><script>
(()=>{const root=document.getElementById('mat-data');if(!root)return;const d=JSON.parse(root.textContent),tracks=d.tracks||[],releases=d.releases||[],slider=document.getElementById('mat-scrub'),selected=document.getElementById('mat-selected'),rail=document.getElementById('mat-rail'),rows=document.getElementById('mat-tracks'),critical=document.getElementById('mat-critical');
const s=document.createElement('style');s.textContent='.mat-page .mat-panel{box-sizing:border-box}';document.head.appendChild(s);
releases.forEach((r,i)=>{const el=document.createElement('div');el.className='mat-release';el.textContent=r.label;el.dataset.index=i;rail.appendChild(el)});
const esc=x=>String(x==null?'':x);function update(){const i=+slider.value,release=releases[i];selected.textContent=release?release.label:'No release';[...rail.children].forEach((x,n)=>x.setAttribute('aria-current',n===i?'true':'false'));rows.replaceChildren();const blockers=[];tracks.forEach(t=>{const versions=t.versions||[],gates=t.gates||[],target=release?release.code:-1,done=versions.filter(v=>target>=0&&v.code<=target).length,requiredAtoms=release.requires||[],relevant=requiredAtoms.filter(atom=>atom.startsWith(t.id+':')||atom.startsWith(t.id+'@')),required=relevant.length>0,gateAtRelease=gates.some(g=>relevant.some(a=>a.includes('tested:'+g.gate)||a.endsWith(':'+g.gate))),hasGate=required&&gateAtRelease,state=(d.replay_error||required&&!hasGate)?'fail':hasGate?'pass':done?'active':'future';if(required&&!hasGate)blockers.push(t.id+' ('+relevant.join(', ')+')');const row=document.createElement('div');row.className='mat-track '+state;row.dataset.blocked=(!hasGate&&required)?'true':'false';const name=document.createElement('a');name.href='/multiatlas/'+encodeURIComponent(d.name)+'/track/'+encodeURIComponent(t.id);name.textContent=t.id+' · '+t.branch_expected;const status=document.createElement('span');status.className='mat-status';status.textContent=d.replay_error?'Replay error':release.code<0?'Release target not configured':hasGate?'Gate passed':required?'Required gate not passed':done?'Work in progress':'Future / unstarted';const gatesEl=document.createElement('div');gatesEl.className='mat-gate-grid';releases.forEach((rel,ri)=>{const atoms=rel.requires||[],mapped=atoms.filter(a=>a.startsWith(t.id+':')||a.startsWith(t.id+'@')),passed=(t.gates||[]).some(g=>mapped.some(a=>a.includes('tested:'+g.gate)||a.endsWith(':'+g.gate))),cell=document.createElement('span');cell.className='mat-gate'+(mapped.length?' required':'')+(passed?' passed':'');cell.textContent=mapped.length?(passed?'✓':'!'):'·';cell.setAttribute('aria-label',rel.label+': '+(mapped.length?(passed?'required gate passed':'required gate pending'):'no gate mapped'));cell.title=rel.label+': '+(mapped.length?mapped.join(', '):'no gate mapped');gatesEl.appendChild(cell)});const git=document.createElement('div');git.className='mat-git';if(t.git.available){git.textContent=t.git.branch+' · '+t.git.head.slice(0,12)+' | '+t.git.ahead_main+' ahead / '+t.git.behind_main+' behind main | '+(t.git.dirty?'uncommitted changes':'clean');git.title='Full commit: '+t.git.head;git.setAttribute('aria-label',t.git.branch+' full commit '+t.git.head+', '+t.git.ahead_main+' ahead and '+t.git.behind_main+' behind main, '+(t.git.dirty?'uncommitted changes':'clean'))}else git.textContent='Git status unavailable';row.append(name,status,gatesEl,git);rows.appendChild(row)});critical.textContent=blockers.length?'Critical path for '+(release&&release.label)+': '+blockers.join('; '):'No required track gates are blocking this release.';if(release.code<0)critical.textContent='Release '+release.label+' is not configured in the meta-map; gate state is pending.';if(d.replay_error)critical.textContent+=' Replay error: '+d.replay_error;}
slider.max=Math.max(0,releases.length-1);slider.addEventListener('input',update);update();})();
</script>)HTML";
	WebsiteShell shell; shell.title=def.title+" - Release timeline"; shell.brand="MultiAtlas"; shell.brand_href="/multiatlas/"+def.name; shell.html_attributes=" lang=\"en\""+WebsiteThemeHtmlAttributes(String(),String()); shell.head_script=WebsiteThemeHeadScript(); shell.topbar_html=WebsiteThemeControlsHtml(); shell.body=body; r.Html(RenderAdminShell(shell));
}
}

void InstallMultiAtlasDashboard(EngineSite& site) { site.Router().Add("GET","multiatlas/*/timeline",Timeline); }
END_UPP_NAMESPACE
