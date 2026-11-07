#include "AssessmentView.h"

NAMESPACE_UPP

static String AVS(const ValueMap& m, const char *key) { Value v=MapGet(m,key); return IsString(v)?(String)v:String(); }
static ValueMap AVM(const Value& v) { return v.Is<ValueMap>()?(ValueMap)v:ValueMap(); }
static ValueArray AVA(const Value& v) { return v.Is<ValueArray>()?(ValueArray)v:ValueArray(); }
static String AVEncode(const String& s) { return UrlEncode(s); }
static String AVContext(const ValueMap& v)
{
	return "workspace="+AVEncode(AVS(v,"workspace_id"))+"&run="+AVEncode(AVS(v,"run_id"))+
	       "&rubric="+AVEncode(AVS(v,"rubric_id"))+"&inputs="+AVEncode(AVS(v,"input_context"));
}
static Value AVGet(const ValueMap& m,const char *key) { int i=m.Find(key); return i<0?Value():m.GetValue(i); }
static String AVQualify(const String& workspace,const String& identity)
{
	return identity.Find(':')>=0?identity:workspace+":"+identity;
}
static String AVField(const ValueMap& m,const char *key)
{
	Value v=AVGet(m,key);
	return v.IsVoid()||IsNull(v)||AsString(v).IsEmpty()?String("Not recorded"):AsString(v);
}
static String AVArrayField(const ValueMap& m,const char *key)
{
	Value v=AVGet(m,key); if(!v.Is<ValueArray>()||ValueArray(v).IsEmpty()) return "Not recorded";
	return AsJSON(v,true);
}
static bool AVHasExact(const CatalogGeneration& c,const String& qualified,const String& kind,const String& ws,const DataDocument **found=NULL)
{
	if(qualified.IsEmpty()||qualified.Find(':')<=0) return false;
	const DataDocument *one=NULL; int count=0;
	for(const DataDocument& d:c.data.GetDocuments()) if(d.qualified_id==qualified&&d.kind==kind&&d.workspace_id==ws) { one=&d; count++; }
	if(count!=1||!one||!one->valid||!one->validated||one->validation_state!="valid") return false;
	if(found) *found=one; return true;
}
static String AVPolicySource(const ManagerSkylarkApp& app,const String& ws,const String& identity)
{
	if(ws.IsEmpty()||identity.IsEmpty()||identity.Find(':')>=0) return "denied";
	return app.IsAssessmentSourceVisible(ws,identity)?String("visible"):String("denied");
}
static bool AVRunSourcesVisible(const ManagerSkylarkApp& app,const ValueMap& run,const String& ws,ValueArray& sources)
{
	Value v=AVGet(run,"source_provenance"); if(!v.Is<ValueArray>()) return false;
	sources=v; if(sources.IsEmpty()) return false;
	Index<String> seen_pins, checked_identities;
	for(const Value& item:sources) {
		if(!item.Is<ValueMap>()) return false; ValueMap s=item; String id=AVS(s,"identity"), revision=AVS(s,"revision");
		if(s.GetCount()!=3||AVS(s,"workspace_id")!=ws||id.IsEmpty()||revision.IsEmpty()) return false;
		ValueArray pair; pair.Add(id); pair.Add(revision); String exact_pin=AsJSON(pair,true);
		if(seen_pins.Find(exact_pin)>=0) return false;
		seen_pins.Add(exact_pin);
		if(checked_identities.Find(id)<0) {
			if(AVPolicySource(app,ws,id)!="visible") return false;
			checked_identities.Add(id);
		}
	}
	return true;
}
static bool AVAddEvidence(const ManagerSkylarkApp& app,const CatalogGeneration& c,const String& ws,const String& ref,ValueArray& evidence,String& source_id)
{
	const DataDocument *item=NULL;
	String qid=ref.Find(':')>=0?ref:ws+":"+ref;
	if(!AVHasExact(c,qid,"evidence-item",ws,&item)) return false;
	ValueMap d=item->data; source_id=AVS(d,"source_identity");
	if(AVPolicySource(app,ws,source_id)!="visible"||AVS(d,"resolution")!="resolved"||AVS(d,"visibility")!="visible"||AVS(d,"classification")=="unknown") return false;
	Value contributors=AVGet(d,"contributors"); if(!contributors.Is<ValueArray>()||ValueArray(contributors).IsEmpty()) return false;
	Index<String> contributor_seen;
	for(const Value& contributor:ValueArray(contributors)) {
		if(!contributor.Is<String>()||((String)contributor).IsEmpty()||contributor_seen.Find((String)contributor)>=0) return false;
		if((String)contributor!=source_id&&AVPolicySource(app,ws,(String)contributor)!="visible") return false;
		contributor_seen.Add(contributor);
	}
	String snapshot_ref=AVS(d,"snapshot"), snapshot_qid=snapshot_ref.Find(':')>=0?snapshot_ref:ws+":"+snapshot_ref;
	const DataDocument *snapshot=NULL;
	if(!AVHasExact(c,snapshot_qid,"evidence-snapshot",ws,&snapshot)) return false;
	ValueMap sd=snapshot->data;
	if(AVS(sd,"source_identity")!=source_id||AVS(sd,"upstream_revision")!=AVS(d,"upstream_revision")||AVS(sd,"resolution")!="resolved"||AVS(sd,"visibility")!="visible"||(bool)AVGet(sd,"retracted")) return false;
	Value snapshot_contributors=AVGet(sd,"contributors"); if(!snapshot_contributors.Is<ValueArray>()||ValueArray(snapshot_contributors).IsEmpty()) return false;
	Index<String> snapshot_seen;
	for(const Value& contributor:ValueArray(snapshot_contributors)) {
		if(!contributor.Is<String>()||((String)contributor).IsEmpty()||snapshot_seen.Find((String)contributor)>=0) return false;
		if((String)contributor!=source_id&&AVPolicySource(app,ws,(String)contributor)!="visible") return false;
		snapshot_seen.Add(contributor);
	}
	ValueMap out; out.Add("id",item->qualified_id); out.Add("revision",item->revision); out.Add("source_revision",item->source_revision);
	out.Add("snapshot_id",snapshot->qualified_id); out.Add("snapshot_revision",snapshot->revision);
	out.Add("upstream_revision",AVS(d,"upstream_revision")); out.Add("locator",AVGet(d,"locator")); out.Add("excerpt",AVGet(d,"excerpt"));
	evidence.Add(out); return true;
}
static void AVCollectRefs(const ValueMap& map,ValueArray& refs)
{
	for(const char *name:{"evidence_id","evidence_ids","supporting_evidence_ids","counterevidence_ids"}) {
		Value v=AVGet(map,name);
		if(v.Is<String>()) refs.Add(v);
		else if(v.Is<ValueArray>()) for(const Value& r:ValueArray(v)) refs.Add(r);
	}
}
static String AVCandidateId(const ValueMap& row) { return AVS(row,"candidate_id"); }
static ValueMap AVAssessment(const ValueMap& row) { return AVM(AVGet(row,"assessment")); }
static String AVSortValue(const ValueMap& row,const String& sort,const String& dimension)
{
	ValueMap a=AVAssessment(row);
	if(sort=="candidate") return AVCandidateId(row);
	if(sort=="tier") return AVS(a,"tier");
	if(sort=="total") {
		String n=AVS(a,"total_numerator"),d=AVS(a,"total_denominator");
		if(n.IsEmpty()||d.IsEmpty()||AVS(a,"total_state")!="defined") return String();
		return n+"/"+d;
	}
	if(sort=="dimension") for(const Value& v:AVA(AVGet(a,"dimensions"))) {
		ValueMap dim=v; if(AVS(dim,"dimension_id")==dimension) return AVS(dim,"score_state")=="known"?AVS(dim,"score"):String();
	}
	return String();
}
static String AVRenderCategories(const Value& value)
{
	ValueArray categories=AVA(value); String html="<ul class=\"assessment-categories\">";
	if(categories.IsEmpty()) return html+"<li>Category judgments: Not recorded</li></ul>";
	for(const Value& cv:categories) {
		ValueMap c=cv;
		html<<"<li><strong>Dimension</strong>: "<<EscapeHtml(AVField(c,"dimension_id"))
		    <<"; <strong>stored score</strong>: "<<EscapeHtml(AVField(c,"score"))
		    <<"; <strong>provenance</strong>: "<<EscapeHtml(AVField(c,"provenance"))
		    <<"; <strong>crowding judgment</strong>: "<<EscapeHtml(AVField(c,"crowding_judgment"))
		    <<"; <strong>opportunity gap</strong>: "<<EscapeHtml(AVField(c,"opportunity_gap"))
		    <<"; <strong>rationale</strong>: "<<EscapeHtml(AVField(c,"rationale"))
		    <<"; <strong>coverage</strong>: "<<EscapeHtml(AVField(c,"coverage"))
		    <<"; <strong>uncertainty</strong>: "<<EscapeHtml(AVField(c,"uncertainty"))<<"; <strong>evidence</strong>: ";
		ValueArray ids=AVA(AVGet(c,"evidence_ids")); if(ids.IsEmpty()) html<<"Not recorded";
		else for(int i=0;i<ids.GetCount();i++) { if(i) html<<", "; html<<EscapeHtml((String)ids[i]); }
		html<<"</li>";
	}
	return html+"</ul>";
}
static String AVRenderFindings(const Value& value)
{
	ValueArray findings=AVA(value); String html="<ul class=\"assessment-findings\">";
	if(findings.IsEmpty()) return html+"<li>Findings: Not recorded</li></ul>";
	for(const Value& fv:findings) {
		ValueMap f=fv; html<<"<li><strong>Code</strong>: "<<EscapeHtml(AVField(f,"code"))
			<<"; dimension: "<<EscapeHtml(AVField(f,"dimension_id"))
			<<"; candidate: "<<EscapeHtml(AVField(f,"candidate_id"))
			<<"; evidence ID: "<<EscapeHtml(AVField(f,"evidence_id"))
			<<"; evidence IDs: "<<EscapeHtml(AVArrayField(f,"evidence_ids"))
			<<"; supporting evidence: "<<EscapeHtml(AVArrayField(f,"supporting_evidence_ids"))
			<<"; counterevidence: "<<EscapeHtml(AVArrayField(f,"counterevidence_ids"))
			<<"; source IDs: "<<EscapeHtml(AVArrayField(f,"source_ids"))<<"</li>";
	}
	return html+"</ul>";
}

ValueMap AssessmentViewService::Build(const ManagerSkylarkApp& app,const CatalogGeneration& catalog,
	const String& workspace,const String& requested_run,const String& filter,const String& sort,
	const String& direction,const String& dimension_id,const String& detail_id)
{
	ValueMap out; out.Add("status","ok"); out.Add("workspace_id",workspace); out.Add("catalog_generation",(int64)catalog.generation);
	out.Add("catalog_source_revision",catalog.source_revision); out.Add("view_state","unconfigured");
	ValueArray empty; out.Add("runs",empty); out.Add("rows",empty); out.Add("details",empty); out.Add("filtered_count",0);
	out.Add("filter",filter); out.Add("sort",sort); out.Add("direction",direction); out.Add("dimension",dimension_id);
	if(workspace.IsEmpty()) return out;
	if(catalog.status!="loaded"||catalog.data_status!="loaded") { out.Set("view_state","incomplete"); return out; }
	Vector<const DataDocument*> all_runs; Vector<const DataDocument*> visible_runs; ValueArray run_values; bool empty_provenance=false;
	for(const DataDocument& d:catalog.data.GetDocuments()) if(d.workspace_id==workspace&&d.kind=="analysis-run") {
		all_runs.Add(&d);
		if(!d.valid||!d.validated||d.validation_state!="valid") continue;
		ValueArray sources; if(!AVRunSourcesVisible(app,d.data,workspace,sources)) { Value sv=AVGet(d.data,"source_provenance"); if(sv.Is<ValueArray>()&&ValueArray(sv).IsEmpty()) empty_provenance=true; continue; }
		ValueMap r; r.Add("id",d.qualified_id); r.Add("run_id",d.id); r.Add("revision",d.revision);
		r.Add("rubric_id",AVS(d.data,"rubric_id")); r.Add("rubric_revision",AVS(d.data,"rubric_revision"));
		r.Add("status",AVS(d.data,"status")); r.Add("uncertainty",AVS(d.data,"uncertainty"));
		r.Add("input_revisions",AVGet(d.data,"input_revisions")); r.Add("source_provenance",sources);
		r.Add("catalog_revision",d.source_revision); visible_runs.Add(&d); run_values.Add(r);
	}
	if(requested_run.IsEmpty()) {
		out.Set("view_state",visible_runs.IsEmpty()?(empty_provenance?String("unconfigured"):String("empty")):String("run_required"));
		out.Set("runs",run_values); return out;
	}
	const DataDocument *selected=NULL; int selected_count=0; bool exact_visible=false; ValueArray selected_sources;
	for(const DataDocument *d:all_runs) if(d->qualified_id==requested_run) selected_count++;
	for(int i=0;i<visible_runs.GetCount();i++) if(visible_runs[i]->qualified_id==requested_run) { selected=visible_runs[i]; selected_count++; exact_visible=true; selected_sources=AVA(AVGet(AVM(run_values[i]),"source_provenance")); }
	if(!exact_visible||selected_count!=2||requested_run.Find(':')<=0) {
		// Direct lookups never receive partial run listings or distinguish hidden from missing.
		out.Set("view_state","unavailable"); out.Set("runs",empty); return out;
	}
	ValueMap run=selected->data; ValueArray run_sources=selected_sources;
	if(run_sources.IsEmpty()) { out.Set("view_state","unavailable"); out.Set("runs",empty); return out; }
	String rubric=AVS(run,"rubric_id"), rubric_revision=AVS(run,"rubric_revision");
	String input_context=AsJSON(AVGet(run,"input_revisions"),true);
	ValueMap stored_rubric; int rubric_matches=0;
	String rubric_qid=workspace+":"+rubric;
	for(const DataDocument& d:catalog.data.GetDocuments()) if(d.qualified_id==rubric_qid&&d.kind=="assessment-rubric"&&d.workspace_id==workspace&&d.valid&&d.validated&&AVS(d.data,"revision")==rubric_revision) { stored_rubric=d.data; rubric_matches++; }
	if(rubric_matches!=1) stored_rubric.Clear();
	out.Add("run_id",selected->qualified_id); out.Add("run_record_id",selected->id); out.Add("rubric_id",rubric);
	out.Add("rubric_revision",rubric_revision); out.Add("input_revisions",AVGet(run,"input_revisions")); out.Add("input_context",input_context);
	out.Add("run_status",AVS(run,"status")); out.Add("uncertainty",AVS(run,"uncertainty"));
	out.Add("catalog_revision",selected->source_revision); out.Add("source_provenance",run_sources);
	out.Add("source_pin_context",run_sources); out.Add("input_pin_context",AVGet(run,"input_revisions"));
	out.Add("stored_rubric",stored_rubric);
	ValueArray candidate_rows, stale_reasons, all_evidence_ids; Index<String> unique_sources, unique_source_revisions;
	for(const Value& source:run_sources) { ValueMap s=source; String id=AVS(s,"identity"),rev=AVS(s,"revision"); unique_sources.FindAdd(id); unique_source_revisions.FindAdd(id+"\n"+rev); }
	for(const DataDocument& marker:catalog.data.GetDocuments()) if(marker.workspace_id==workspace&&marker.kind=="assessment-staleness"&&marker.valid&&marker.validated&&AVS(marker.data,"prior_run_id")==selected->id) {
		ValueMap m; m.Add("id",marker.qualified_id); m.Add("revision",marker.revision); m.Add("replacement_run_id",AVS(marker.data,"replacement_run_id")); m.Add("reason_flags",AVGet(marker.data,"reason_flags"));
		String replacement_id=AVS(marker.data,"replacement_run_id"); const DataDocument *replacement=NULL; int replacements=0;
		for(const DataDocument& candidate_run:catalog.data.GetDocuments()) if(candidate_run.workspace_id==workspace&&candidate_run.kind=="analysis-run"&&candidate_run.id==replacement_id&&candidate_run.valid&&candidate_run.validated) { replacement=&candidate_run; replacements++; }
		ValueArray replacement_sources; if(replacements==1&&replacement&&AVRunSourcesVisible(app,replacement->data,workspace,replacement_sources)) {
			ValueMap snapshot; snapshot.Add("id",replacement->qualified_id); snapshot.Add("revision",replacement->revision); snapshot.Add("rubric_id",AVS(replacement->data,"rubric_id")); snapshot.Add("rubric_revision",AVS(replacement->data,"rubric_revision")); snapshot.Add("input_revisions",AVGet(replacement->data,"input_revisions")); snapshot.Add("source_provenance",replacement_sources); m.Add("replacement_snapshot",snapshot);
		}
		stale_reasons.Add(m);
	}
	ValueMap candidate_id_counts;
	for(const DataDocument& d:catalog.data.GetDocuments()) if(d.workspace_id==workspace&&d.kind=="candidate-assessment"&&AVS(d.data,"analysis_run")==selected->id&&d.valid&&d.validated) { String id=AVS(d.data,"candidate_id"); int old=0; if(candidate_id_counts.Find(id)>=0) old=(int)candidate_id_counts.Get(id); candidate_id_counts.Set(id,old+1); }
	Index<String> seen_candidate;
	for(const DataDocument& candidate:catalog.data.GetDocuments()) {
		if(candidate.workspace_id!=workspace||candidate.kind!="candidate-assessment"||AVS(candidate.data,"analysis_run")!=selected->id) continue;
		if(!candidate.valid||!candidate.validated||candidate.validation_state!="valid") continue;
		String cid=AVS(candidate.data,"candidate_id"); if(cid.IsEmpty()||seen_candidate.Find(cid)>=0||candidate_id_counts.Get(cid)!=1) continue; seen_candidate.Add(cid);
		ValueArray category_records,findings,refs,evidence; Index<String> candidate_source_ids,candidate_source_revisions;
		AVCollectRefs(candidate.data,refs); AVCollectRefs(AVM(AVGet(candidate.data,"assessment")),refs);
		for(const Value& dim:AVA(AVGet(AVM(AVGet(candidate.data,"assessment")),"dimensions"))) AVCollectRefs(AVM(dim),refs);
		bool permitted=true;
		for(const DataDocument& d:catalog.data.GetDocuments()) {
			if(d.workspace_id!=workspace||!d.valid||!d.validated||d.validation_state!="valid"||AVS(d.data,"analysis_run")!=selected->id) continue;
			if(d.kind=="category-assessment"&&AVS(d.data,"candidate_id")==cid) {
				category_records.Add(d.data); AVCollectRefs(d.data,refs); AVCollectRefs(AVM(AVGet(d.data,"assessment")),refs);
				for(const Value& sv:AVA(AVGet(AVM(AVGet(d.data,"assessment")),"source_ids"))) if(!sv.Is<String>()||AVPolicySource(app,workspace,(String)sv)!="visible") permitted=false;
			}
			if(d.kind=="assessment-finding") {
				ValueMap f=AVM(AVGet(d.data,"finding")); if(AVS(f,"candidate_id")==cid) { findings.Add(f); AVCollectRefs(f,refs); for(const Value& sv:AVA(AVGet(f,"source_ids"))) if(!sv.Is<String>()||AVPolicySource(app,workspace,(String)sv)!="visible") permitted=false; }
			}
		}
		Index<String> evidence_seen,refs_seen;
		for(const Value& rv:refs) {
			if(!rv.Is<String>()) { permitted=false; continue; }
			String ref=rv; if(ref.IsEmpty()) { permitted=false; continue; }
			String qualified_ref=AVQualify(workspace,ref); if(refs_seen.Find(qualified_ref)>=0) continue; refs_seen.Add(qualified_ref);
			ValueArray one; String source;
			if(!AVAddEvidence(app,catalog,workspace,ref,one,source)) { permitted=false; continue; }
			ValueMap ev=one[0]; String evidence_id=AVS(ev,"id");
			if(evidence_seen.Find(evidence_id)<0) { evidence_seen.Add(evidence_id); evidence.Add(ev); }
			candidate_source_ids.FindAdd(source); candidate_source_revisions.FindAdd(source+"\n"+AVS(ev,"upstream_revision"));
		}
		if(!permitted) continue;
		ValueArray categories;
		for(const Value& cv:category_records) {
			ValueMap category=cv, assessment=AVM(AVGet(category,"assessment")), normalized;
			normalized.Add("id",AVS(category,"id")); normalized.Add("revision",AVS(category,"revision"));
			normalized.Add("dimension_id",AVField(assessment,"dimension_id")); normalized.Add("score",AVField(assessment,"score"));
			normalized.Add("provenance",AVField(assessment,"provenance"));
			normalized.Add("crowding_judgment",AVField(assessment,"crowding_judgment"));
			normalized.Add("opportunity_gap",AVField(assessment,"opportunity_gap"));
			normalized.Add("rationale",AVField(assessment,"rationale")); normalized.Add("coverage",AVField(assessment,"coverage"));
			normalized.Add("uncertainty",AVField(assessment,"uncertainty"));
			ValueArray category_evidence;
			for(const Value& ref:AVA(AVGet(assessment,"evidence_ids"))) {
				String qid=ref.Is<String>()?((String)ref).Find(':')>=0?(String)ref:workspace+":"+(String)ref:String();
				for(const Value& ev:evidence) if(AVS(AVM(ev),"id")==qid) category_evidence.Add(qid);
			}
			normalized.Add("evidence_ids",category_evidence); categories.Add(normalized);
		}
		ValueArray normalized_findings;
		for(const Value& fv:findings) {
			ValueMap f=fv; Value single=AVGet(f,"evidence_id");
			if(single.Is<String>()) f.Set("evidence_id",AVQualify(workspace,(String)single));
			for(const char *key:{"evidence_ids","supporting_evidence_ids","counterevidence_ids"}) {
				Value ids_value=AVGet(f,key); if(!ids_value.Is<ValueArray>()) continue; ValueArray qualified;
				for(const Value& id:ValueArray(ids_value)) if(id.Is<String>()) qualified.Add(AVQualify(workspace,(String)id));
				f.Set(key,qualified);
			}
			Value source_ids=AVGet(f,"source_ids"); if(source_ids.Is<ValueArray>()) { ValueArray qualified; for(const Value& id:ValueArray(source_ids)) if(id.Is<String>()) qualified.Add(AVQualify(workspace,(String)id)); f.Set("source_ids",qualified); }
			normalized_findings.Add(f);
		}
		ValueMap row; row.Add("id",candidate.qualified_id); row.Add("candidate_record_id",candidate.id); row.Add("candidate_id",cid);
		row.Add("revision",candidate.revision); row.Add("assessment",AVGet(candidate.data,"assessment")); row.Add("categories",categories);
		row.Add("findings",normalized_findings); row.Add("evidence_items",evidence); row.Add("stale_reasons",stale_reasons);
		ValueMap ca=AVM(AVGet(candidate.data,"assessment"));
		row.Add("tier",AVS(ca,"tier")); row.Add("uncertainty",AVS(ca,"uncertainty"));
		row.Add("rationale",AVField(ca,"rationale")); row.Add("coverage",AVField(ca,"coverage")); row.Add("duplicate",false); row.Add("excluded",false);
		for(const Value& fv:findings) { ValueMap f=fv; String code=AVS(f,"code"); if(code.Find("duplicate")>=0) row.Set("duplicate",true); if(code.Find("exclusion")>=0||code.Find("excluded")>=0) row.Set("excluded",true); }
		String display=ToLower(cid+" "+AVS(ca,"tier")+" "+AVS(ca,"uncertainty")+" "+AsString(AVGet(ca,"rationale")));
		if(!filter.IsEmpty()&&display.Find(ToLower(filter))<0) continue;
		for(const String& source:candidate_source_ids) unique_sources.FindAdd(source);
		for(const String& source_revision:candidate_source_revisions) unique_source_revisions.FindAdd(source_revision);
		for(const String& evidence_id:evidence_seen) all_evidence_ids.Add(evidence_id);
		candidate_rows.Add(row);
	}
	// Any hidden or malformed candidate with unresolved contributors is omitted before all counts and links.
	String sort_key=sort;
	if(sort_key!=""&&sort_key!="candidate"&&sort_key!="tier"&&sort_key!="total"&&sort_key!="dimension") sort_key.Clear();
	if(sort_key=="dimension"&&dimension_id.IsEmpty()) sort_key.Clear();
	if(sort_key=="dimension") { bool found=false; for(const Value& d:AVA(AVGet(stored_rubric,"dimensions"))) if(AVS(AVM(d),"id")==dimension_id) found=true; if(!found) sort_key.Clear(); }
	Vector<String> sorted; for(const Value& v:candidate_rows) sorted.Add(AsJSON(v,true));
	Sort(sorted,[&](const String& lhs,const String& rhs){
		ValueMap a=AVM(ParseJSON(lhs)),b=AVM(ParseJSON(rhs)); String x=AVSortValue(a,sort_key,dimension_id),y=AVSortValue(b,sort_key,dimension_id);
		bool xu=x.IsEmpty(),yu=y.IsEmpty(); if(xu!=yu) return !xu;
		if(!xu&&x!=y) {
			int cmp=0;
			if(sort_key=="total") {
				int sx=x.Find('/'),sy=y.Find('/');
				int64 xn=ScanInt(x.Mid(0,sx)),xd=ScanInt(x.Mid(sx+1)),yn=ScanInt(y.Mid(0,sy)),yd=ScanInt(y.Mid(sy+1));
				int64 left=xn*yd,right=yn*xd; cmp=left<right?-1:left>right?1:0;
			}
			else if(sort_key=="dimension") { int xi=ScanInt(x),yi=ScanInt(y); cmp=xi<yi?-1:xi>yi?1:0; }
			else cmp=x<y?-1:x>y?1:0;
			if(cmp) return direction=="desc"?cmp>0:cmp<0;
		}
		return AVS(a,"id")<AVS(b,"id");
	});
	candidate_rows.Clear(); for(const String& v:sorted) candidate_rows.Add(ParseJSON(v));
	ValueArray detail_rows;
	if(!detail_id.IsEmpty()) {
		const DataDocument *detail=NULL; int matches=0; Value detail_row;
		for(const Value& v:candidate_rows) { ValueMap r=v; if(AVS(r,"id")==detail_id) { matches++; detail_row=v; for(const DataDocument& d:catalog.data.GetDocuments()) if(d.qualified_id==detail_id&&d.kind=="candidate-assessment"&&d.workspace_id==workspace&&AVS(d.data,"analysis_run")==selected->id) detail=&d; } }
		if(matches==1&&detail&&detail_id.Find(':')>0) detail_rows.Add(detail_row);
		else { out.Set("view_state","unavailable"); out.Set("rows",empty); out.Set("runs",empty); out.Set("filtered_count",0); return out; }
	}
	out.Set("view_state","ready"); out.Set("runs",run_values); out.Set("rows",candidate_rows); out.Set("details",detail_rows);
	out.Set("filtered_count",candidate_rows.GetCount()); out.Add("candidate_count",candidate_rows.GetCount());
	out.Add("unique_visible_source_count",unique_sources.GetCount()); out.Add("unique_visible_source_revision_count",unique_source_revisions.GetCount());
	Index<String> visible_evidence; for(const Value& id:all_evidence_ids) visible_evidence.FindAdd(id);
	out.Add("unique_visible_evidence_count",visible_evidence.GetCount()); out.Add("stale_reasons",stale_reasons);
	out.Set("sort",sort_key); out.Add("direction",direction=="desc"?String("desc"):String("asc"));
	return out;
}

String AssessmentViewService::Render(const ValueMap& view,const String& mode)
{
	String ws=AVS(view,"workspace_id"),run=AVS(view,"run_id"),rubric=AVS(view,"rubric_id"),input=AVS(view,"input_context");
	String context=EscapeHtml(AVContext(view)),state=AVS(view,"view_state"),html;
	if(mode=="detail") {
		ValueArray details=AVA(AVGet(view,"details")); html<<"<h1>Candidate assessment detail</h1><p role=\"status\">"<<EscapeHtml(state)<<"</p>";
		for(const Value& item:details) { ValueMap row=item; ValueMap a=AVAssessment(row); html<<"<article class=\"card\"><h2>"<<EscapeHtml(AVCandidateId(row))<<"</h2><p>Workspace "<<EscapeHtml(ws)<<"; run "<<EscapeHtml(run)<<"; rubric "<<EscapeHtml(rubric)<<" ("<<EscapeHtml(AVS(view,"rubric_revision"))<<"); input pins "<<EscapeHtml(input)<<"</p><h3>Stored assessment</h3><p>Rationale: "<<EscapeHtml(AVField(a,"rationale"))<<"; coverage: "<<EscapeHtml(AVField(a,"coverage"))<<"; uncertainty: "<<EscapeHtml(AVField(a,"uncertainty"))<<"</p><pre>"<<EscapeHtml(AsJSON(AVGet(a,"dimensions"),true))<<"</pre><h3>Category judgments</h3>"<<AVRenderCategories(AVGet(row,"categories"))<<"<h3>Findings</h3>"<<AVRenderFindings(AVGet(row,"findings"))<<"<h3>Staleness</h3><pre>"<<EscapeHtml(AsJSON(AVGet(row,"stale_reasons"),true))<<"</pre><h3>Authorized source evidence</h3><ul>";
			for(const Value& ev:AVA(AVGet(row,"evidence_items"))) { ValueMap e=ev; html<<"<li><a href=\"/data/doc/"<<AVEncode(AVS(e,"id"))<<"?"<<context<<"\">Open exact evidence item "<<EscapeHtml(AVS(e,"id"))<<"</a> <span>Item revision "<<EscapeHtml(AVS(e,"revision"))<<", source revision "<<EscapeHtml(AVS(e,"upstream_revision"))<<", snapshot "<<EscapeHtml(AVS(e,"snapshot_id"))<<" at "<<EscapeHtml(AVS(e,"snapshot_revision"))<<"</span><pre>"<<EscapeHtml(AsString(AVGet(e,"excerpt")))<<"</pre><pre>"<<EscapeHtml(AsJSON(AVGet(e,"locator"),true))<<"</pre></li>"; }
			html<<"</ul></article>";
		}
		if(details.IsEmpty()) html<<"<p role=\"status\">Assessment unavailable.</p>"; return html;
	}
	String tableclass=mode=="compare"?String("assessment-table-wrap"):String("assessment-overview-wrap");
	if(mode=="compare") html<<"<h1>Candidate comparison</h1>"; else html<<"<h1>Research assessment overview</h1>";
	html<<"<p role=\"status\">State: "<<EscapeHtml(state)<<". Candidate assessments: "<<AsString((int)AVGet(view,"filtered_count"))<<"</p>";
	if(state=="unavailable"||state=="incomplete"||state=="unconfigured") return html+"<p role=\"status\">No assessment data is available in this view.</p>";
	html<<"<p>Workspace "<<EscapeHtml(ws)<<"; run "<<EscapeHtml(run)<<"; rubric "<<EscapeHtml(rubric)<<" @ "<<EscapeHtml(AVS(view,"rubric_revision"))<<"; input pins "<<EscapeHtml(input)<<"</p>";
	html<<"<p>Visible source identities: "<<AsString((int)AVGet(view,"unique_visible_source_count"))<<"; source revisions: "<<AsString((int)AVGet(view,"unique_visible_source_revision_count"))<<"; evidence items: "<<AsString((int)AVGet(view,"unique_visible_evidence_count"))<<"; run uncertainty: "<<EscapeHtml(AVS(view,"uncertainty"))<<"</p>";
	html<<"<p>Catalog revision: "<<EscapeHtml(AVS(view,"catalog_revision"))<<"</p><h2>Visible source pins</h2><ul aria-label=\"Visible source identities and upstream revisions\">";
	for(const Value& sv:AVA(AVGet(view,"source_pin_context"))) { ValueMap source=sv; html<<"<li>"<<EscapeHtml(AVS(source,"identity"))<<" @ "<<EscapeHtml(AVS(source,"revision"))<<"</li>"; }
	html<<"</ul><h2>Input revision pins</h2><ul aria-label=\"Input revision pins\">";
	for(const Value& pv:AVA(AVGet(view,"input_pin_context"))) { ValueMap pin=pv; html<<"<li>"<<EscapeHtml(AVS(pin,"id"))<<" @ "<<EscapeHtml(AVS(pin,"revision"))<<"</li>"; }
	html<<"</ul>";
	if(mode!="compare") {
		html<<"<h2>Available runs</h2><ul>";
		for(const Value& rv:AVA(AVGet(view,"runs"))) { ValueMap r=rv; ValueMap ctx=view; ctx.Set("run_id",AVS(r,"id")); ctx.Set("rubric_id",AVS(r,"rubric_id")); ctx.Set("input_context",AsJSON(AVGet(r,"input_revisions"),true)); html<<"<li><a href=\"/assessments?"<<EscapeHtml(AVContext(ctx))<<"\">Open run "<<EscapeHtml(AVS(r,"run_id"))<<"</a> "<<EscapeHtml(AVS(r,"status"))<<"; uncertainty "<<EscapeHtml(AVS(r,"uncertainty"))<<"; catalog revision "<<EscapeHtml(AVS(r,"catalog_revision"))<<"</li>"; }
		html<<"</ul>";
	}
	String action=mode=="compare"?"/assessments/compare":"/assessments";
		String selected_sort=AVS(view,"sort"),selected_direction=AVS(view,"direction");
		html<<"<form method=\"get\" action=\""<<action<<"\"><label for=\"av-workspace\">Workspace</label><input id=\"av-workspace\" name=\"workspace\" value=\""<<EscapeHtml(ws)<<"\"><label for=\"av-run\">Run identity</label><input id=\"av-run\" name=\"run\" value=\""<<EscapeHtml(run)<<"\"><label for=\"av-filter\">Candidate filter</label><input id=\"av-filter\" name=\"filter\" value=\""<<EscapeHtml(AVS(view,"filter"))<<"\"><label for=\"av-sort\">Sort by</label><select id=\"av-sort\" name=\"sort\"><option value=\"candidate\""<<(selected_sort=="candidate"?" selected":"")<<">Candidate</option><option value=\"tier\""<<(selected_sort=="tier"?" selected":"")<<">Tier</option><option value=\"total\""<<(selected_sort=="total"?" selected":"")<<">Defined total</option><option value=\"dimension\""<<(selected_sort=="dimension"?" selected":"")<<">Dimension value</option></select><label for=\"av-dimension\">Dimension ID for dimension sort</label><input id=\"av-dimension\" name=\"dimension\" value=\""<<EscapeHtml(AVS(view,"dimension"))<<"\"><label for=\"av-direction\">Sort direction</label><select id=\"av-direction\" name=\"direction\"><option value=\"asc\""<<(selected_direction=="asc"?" selected":"")<<">Ascending</option><option value=\"desc\""<<(selected_direction=="desc"?" selected":"")<<">Descending</option></select><input type=\"hidden\" name=\"rubric\" value=\""<<EscapeHtml(rubric)<<"\"><input type=\"hidden\" name=\"inputs\" value=\""<<EscapeHtml(input)<<"\"><button type=\"submit\">Apply view</button></form>";
	ValueArray rows=AVA(AVGet(view,"rows"));
	String wrap=Format("<div class=\"%s\" tabindex=\"0\" role=\"region\" aria-label=\"Scrollable candidate assessment table\"><table><caption>%s</caption><thead><tr>",tableclass,mode=="compare"?"Candidate assessment comparison":"Candidate assessment overview");
	wrap<<"<th scope=\"col\">Candidate</th><th scope=\"col\">Tier</th>";
	for(const Value& v:rows) { (void)v; break; }
	wrap<<"<th scope=\"col\">Dimensions and total</th><th scope=\"col\">Coverage and uncertainty</th><th scope=\"col\">Rationale and findings</th><th scope=\"col\">Detail</th></tr></thead><tbody>";
	for(const Value& v:rows) {
		ValueMap r=v,a=AVAssessment(r); String tier=AVS(a,"tier"); String tier_class=tier=="research-ready"?"tier-a":tier=="paper-backed"?"tier-b":tier=="interesting-risk"?"tier-c":tier=="avoid-for-now"?"tier-d":"tier-unknown";
		String total=AVS(a,"total_state")!="defined"||AVS(a,"total_numerator").IsEmpty()||AVS(a,"total_denominator").IsEmpty()?String("No defined total"):AVS(a,"total_numerator")+" / "+AVS(a,"total_denominator");
		wrap<<"<tr><th scope=\"row\">"<<EscapeHtml(AVCandidateId(r))<<"</th><td><span class=\"assessment-tier "<<tier_class<<"\">"<<EscapeHtml(tier.IsEmpty()?String("unknown"):tier)<<"</span></td><td><pre>"<<EscapeHtml(AsJSON(AVGet(a,"dimensions"),true))<<"</pre><p>Stored total: "<<EscapeHtml(total)<<"</p></td><td>Coverage: "<<EscapeHtml(AVField(a,"coverage"))<<"; uncertainty: "<<EscapeHtml(AVField(a,"uncertainty"))<<"</td><td>Rationale: "<<EscapeHtml(AVField(a,"rationale"))<<"<p>Duplicate: "<<((bool)AVGet(r,"duplicate")?"yes":"no")<<"; excluded: "<<((bool)AVGet(r,"excluded")?"yes":"no")<<"</p><h4>Stored category judgments</h4>"<<AVRenderCategories(AVGet(r,"categories"))<<"<h4>Stored findings</h4>"<<AVRenderFindings(AVGet(r,"findings"))<<"<pre>"<<EscapeHtml(AsJSON(AVGet(r,"stale_reasons"),true))<<"</pre></td><td><a href=\"/assessments/detail/"<<AVEncode(AVS(r,"id"))<<"?"<<context<<"\">Open candidate assessment</a></td></tr>";
	}
	wrap<<"</tbody></table></div><section aria-labelledby=\"tier-legend\"><h2 id=\"tier-legend\">Stored tier labels and rubric thresholds</h2>";
	ValueMap stored_rubric=AVM(AVGet(view,"stored_rubric")); ValueArray tiers=AVA(AVGet(stored_rubric,"tiers"));
	if(tiers.IsEmpty()) wrap<<"<p>No configured tier thresholds are available in the selected stored rubric.</p>";
	else { wrap<<"<p>Thresholds below are copied from the selected stored rubric; no default dimensions or tiers are applied.</p><ul>";
		for(const Value& t:tiers) { ValueMap tier=t; wrap<<"<li>"<<EscapeHtml(AVS(tier,"id"))<<": "<<EscapeHtml(AsString(AVGet(tier,"minimum_numerator")))<<"/"<<EscapeHtml(AsString(AVGet(tier,"minimum_denominator")))<<" to "<<EscapeHtml(AsString(AVGet(tier,"maximum_numerator")))<<"/"<<EscapeHtml(AsString(AVGet(tier,"maximum_denominator")))<<"</li>"; }
		wrap<<"</ul>"; }
	wrap<<"</section>";
	return html+wrap;
}

END_UPP_NAMESPACE
