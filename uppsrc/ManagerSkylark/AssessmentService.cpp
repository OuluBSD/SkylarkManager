#include "AssessmentService.h"

NAMESPACE_UPP

static const int MAX_DEPTH=16, MAX_NODES=30000, MAX_BYTES=2*1024*1024, MAX_TEXT=65536;
static const int MAX_QUALITATIVE_TEXT=4096;
static Value V(const ValueMap& m,const String& k){int i=m.Find(k);return i<0?Value():m.GetValue(i);}
static String S(const ValueMap& m,const String& k){Value v=V(m,k);return IsString(v)?(String)v:String();}
static bool IsS(const ValueMap& m,const String& k){return IsString(V(m,k));}
static ValueArray A(const ValueMap& m,const String& k){Value v=V(m,k);return v.Is<ValueArray>()?(ValueArray)v:ValueArray();}
static bool Bounded(const Value& v,int d,int& nodes,int64& bytes){
	if(d>MAX_DEPTH||++nodes>MAX_NODES)return false;
	if(v.Is<ValueMap>()){ValueMap m=v;if(m.GetCount()>4096)return false;for(int i=0;i<m.GetCount();i++){if(!IsString(m.GetKey(i)))return false;String k=m.GetKey(i);bytes+=k.GetCount();if(!Bounded(m.GetValue(i),d+1,nodes,bytes))return false;}}
	else if(v.Is<ValueArray>()){ValueArray a=v;if(a.GetCount()>AssessmentService::MAX_INPUT_ITEMS)return false;for(int i=0;i<a.GetCount();i++)if(!Bounded(a[i],d+1,nodes,bytes))return false;}
	else if(IsString(v)){String s=v;if(s.GetCount()>MAX_TEXT)return false;bytes+=s.GetCount();}
	else if(v.Is<int>()||v.Is<int64>()||v.Is<bool>()||IsNull(v))bytes+=16;else return false;
	return bytes<=MAX_BYTES;
}
static String Json(const Value& v){return AsJSON(v,true);}
static bool ExactInteger(const Value& v,int64& n){if(v.Is<int>()){n=(int)v;return true;}if(v.Is<int64>()){n=(int64)v;return true;}return false;}
static bool Decimal(const String& s,int64& n){if(s.IsEmpty()||s.GetCount()>10)return false;n=0;for(int i=0;i<s.GetCount();i++){if(s[i]<'0'||s[i]>'9')return false;n=n*10+s[i]-'0';}return true;}
static bool AssessmentTimestamp(const String& s){
	if(s.GetCount()!=20||s[4]!='-'||s[7]!='-'||s[10]!='T'||s[13]!=':'||s[16]!=':'||s[19]!='Z')return false;
	for(int i=0;i<s.GetCount();i++)if(i!=4&&i!=7&&i!=10&&i!=13&&i!=16&&i!=19&&(s[i]<'0'||s[i]>'9'))return false;
	int year=(s[0]-'0')*1000+(s[1]-'0')*100+(s[2]-'0')*10+s[3]-'0';
	int month=(s[5]-'0')*10+s[6]-'0',day=(s[8]-'0')*10+s[9]-'0',hour=(s[11]-'0')*10+s[12]-'0',minute=(s[14]-'0')*10+s[15]-'0',second=(s[17]-'0')*10+s[18]-'0';
	if(month<1||month>12||hour>23||minute>59||second>59)return false;
	static const int month_days[]={31,28,31,30,31,30,31,31,30,31,30,31};int max_day=month_days[month-1];
	if(month==2&&(year%400==0||(year%4==0&&year%100!=0)))max_day++;
	return year>0&&day>=1&&day<=max_day;
}
static bool Member(const Index<String>& x,const String& s){return !s.IsEmpty()&&x.Find(s)>=0;}
static bool In(const String& s,std::initializer_list<const char*> vals){for(const char* v:vals)if(s==v)return true;return false;}
static ValueMap Env(const String& kind,const String& id,const String& ws,const String& prov){ValueMap e;e.Add("schema_id","skylark/"+kind);e.Add("schema_version","1");e.Add("workspace_id",ws);e.Add("stable_id",id);e.Add("record_kind",kind);e.Add("revision","1");e.Add("visibility","private");e.Add("provenance_ref",prov);return e;}
static ValueMap Base(const String& kind,const String& id,const String& ws,const String& prov="computed"){ValueMap m;m.Add("id",id);m.Add("kind",kind);m.Add("workspace_id",ws);m.Add("revision","1");m.Add("provenance",prov);m.Add("envelope",Env(kind,id,ws,prov));return m;}
static bool PushRequiredArray(const ValueMap& m,const String& k,ValueArray& a){Value v=V(m,k);if(!v.Is<ValueArray>())return false;a=v;return a.GetCount()<=AssessmentService::MAX_INPUT_ITEMS;}
static int FindString(const Vector<String>& v,const String& s){for(int i=0;i<v.GetCount();i++)if(v[i]==s)return i;return -1;}

struct T61Dimension : Moveable<T61Dimension> {String id;int64 weight,lo,hi;int evidence;};
static bool ValidateRubric(const ValueMap& r,const String& ws,Vector<T61Dimension>& dims,Index<String>& dim_ids,ValueArray& tiers,String& total){
	if(!IsS(r,"id")||!IsS(r,"revision")||S(r,"id").IsEmpty()||S(r,"revision").IsEmpty()||!IsS(r,"workspace_id")||S(r,"workspace_id")!=ws)return false;
	Value dv=V(r,"dimensions"),cv=V(r,"coverage_required"),tv=V(r,"tiers");if(!dv.Is<ValueArray>()||!cv.Is<ValueArray>()||!tv.Is<ValueArray>())return false;
	ValueArray da=dv,coverage=cv;tiers=tv;if(da.GetCount()!=6||coverage.GetCount()!=6||tiers.IsEmpty()||tiers.GetCount()>64)return false;
	String scale=S(r,"score_scale");int64 scale_lo=0,scale_hi=0;if(scale=="integer-0-5")scale_hi=5;else if(scale=="integer-0-10")scale_hi=10;else if(scale=="integer-1-5"){scale_lo=1;scale_hi=5;}else if(scale=="integer-1-10"){scale_lo=1;scale_hi=10;}else return false;
	total=S(r,"total_rule");if(total!="weighted-sum"&&total!="weighted-mean")return false;
	int64 weight_sum=0,possible_lo=0,possible_hi=0;
	for(int i=0;i<6;i++){if(!da[i].Is<ValueMap>())return false;ValueMap d=da[i];String id=S(d,"id");int64 w,lo,hi,c;
		if(!IsS(d,"id")||id.IsEmpty()||id.GetCount()>128||!ExactInteger(V(d,"weight"),w)||!ExactInteger(V(d,"minimum_score"),lo)||!ExactInteger(V(d,"maximum_score"),hi)||!ExactInteger(V(d,"minimum_evidence_count"),c)||w<1||w>1000000||lo<scale_lo||hi>scale_hi||lo>hi||c<1||c>AssessmentService::MAX_INPUT_ITEMS||dim_ids.Find(id)>=0)return false;
		T61Dimension x;x.id=id;x.weight=w;x.lo=lo;x.hi=hi;x.evidence=(int)c;dims.Add(x);dim_ids.Add(id);weight_sum+=w;possible_lo+=w*lo;possible_hi+=w*hi;
	}
	Index<String> covered;for(int i=0;i<coverage.GetCount();i++){if(!IsString(coverage[i]))return false;String id=coverage[i];if(!Member(dim_ids,id)||covered.Find(id)>=0)return false;covered.Add(id);}if(covered.GetCount()!=6)return false;
	int64 last_un=0,last_ud=1;Index<String> tier_ids;
	for(int i=0;i<tiers.GetCount();i++){if(!tiers[i].Is<ValueMap>())return false;ValueMap t=tiers[i];String id=S(t,"id");int64 ln,ld,un,ud;
		if(!IsS(t,"id")||id.IsEmpty()||tier_ids.Find(id)>=0||!ExactInteger(V(t,"minimum_numerator"),ln)||!ExactInteger(V(t,"minimum_denominator"),ld)||!ExactInteger(V(t,"maximum_numerator"),un)||!ExactInteger(V(t,"maximum_denominator"),ud)||ld<1||ud<1||ln<0||un<0||ln>1000000||un>1000000||ld>1000000||ud>1000000||ln*ud>=un*ld)return false;
		if(i&&ln*last_ud<last_un*ld)return false;
		int64 achievable_low_num=total=="weighted-sum"?possible_lo:possible_lo,achievable_low_den=total=="weighted-sum"?1:weight_sum;
		int64 achievable_high_num=total=="weighted-sum"?possible_hi:possible_hi,achievable_high_den=total=="weighted-sum"?1:weight_sum;
		if(ln*achievable_low_den<achievable_low_num*ld||un*achievable_high_den>achievable_high_num*ud)return false;
		tier_ids.Add(id);last_un=un;last_ud=ud;
	}
	return true;
}

ValueMap AssessmentService::Project(const ValueMap& rubric,const ValueMap& in){
	ValueMap out;out.Add("status","unconfigured");String ws=S(in,"workspace_id");out.Add("workspace_id",ws);
	int nodes=0;int64 bytes=0;if(!Bounded(in,0,nodes,bytes)||!Bounded(rubric,0,nodes,bytes)||!IsS(in,"workspace_id")||ws.IsEmpty()){out.Set("status","unknown");return out;}
	Vector<T61Dimension> dims;Index<String> dim_ids;ValueArray tiers;String total;if(!ValidateRubric(rubric,ws,dims,dim_ids,tiers,total))return out;
	ValueArray sources,evidence,scores,relationships,pins;
	if(!PushRequiredArray(in,"sources",sources)||!PushRequiredArray(in,"evidence",evidence)||!PushRequiredArray(in,"scores",scores)||!PushRequiredArray(in,"relationships",relationships)||!PushRequiredArray(in,"input_revisions",pins)){out.Set("status","unknown");return out;}
	Index<String> pinned_keys;for(int i=0;i<pins.GetCount();i++){if(!pins[i].Is<ValueMap>()){out.Set("status","unknown");return out;}ValueMap p=pins[i];if(p.GetCount()!=3||!IsS(p,"workspace_id")||S(p,"workspace_id")!=ws||!IsS(p,"id")||S(p,"id").IsEmpty()||!IsS(p,"revision")||S(p,"revision").IsEmpty()){out.Set("status","unknown");return out;}pinned_keys.FindAdd(S(p,"id")+"\n"+S(p,"revision"));}
	Index<String> source_ids,source_revisions,evidence_ids,candidates,source_endpoint_ids;
	ValueMap evidence_by_id;Vector<String> evidence_snapshots;
	for(int i=0;i<sources.GetCount();i++){if(!sources[i].Is<ValueMap>()){out.Set("status","unknown");return out;}ValueMap s=sources[i];if(s.GetCount()!=3||!IsS(s,"workspace_id")||S(s,"workspace_id")!=ws||!IsS(s,"identity")||S(s,"identity").IsEmpty()||!IsS(s,"revision")||S(s,"revision").IsEmpty()||!Member(pinned_keys,S(s,"identity")+"\n"+S(s,"revision"))){out.Set("status","unknown");return out;}source_ids.FindAdd(S(s,"identity"));source_revisions.FindAdd(S(s,"identity")+"\n"+S(s,"revision"));source_endpoint_ids.FindAdd(S(s,"identity"));}
	for(int i=0;i<evidence.GetCount();i++){if(!evidence[i].Is<ValueMap>()){out.Set("status","unknown");return out;}ValueMap e=evidence[i];if(e.GetCount()!=7||!IsS(e,"workspace_id")||S(e,"workspace_id")!=ws||!IsS(e,"item_id")||S(e,"item_id").IsEmpty()||!IsS(e,"snapshot_id")||S(e,"snapshot_id").IsEmpty()||!IsS(e,"source_identity")||!IsS(e,"source_revision")||!IsS(e,"resolution")||!IsS(e,"visibility")||!Member(pinned_keys,S(e,"snapshot_id")+"\n"+S(e,"source_revision"))||!In(S(e,"resolution"),{"resolved","unresolved","invalid","ambiguous","denied","retracted"})||!In(S(e,"visibility"),{"visible","restricted"})){out.Set("status","unknown");return out;}
		String id=S(e,"item_id");int prev=evidence_ids.Find(id);if(prev>=0){if(Json(V(evidence_by_id,id))!=Json(e)){out.Set("status","unknown");return out;}}else{evidence_ids.Add(id);evidence_by_id.Add(id,e);evidence_snapshots.Add(S(e,"snapshot_id"));}}
	Index<String> usable_evidence;Vector<String> ordered_evidence;for(int i=0;i<evidence_ids.GetCount();i++){String id=evidence_ids[i];ValueMap e=V(evidence_by_id,id);bool pinned_source=false;for(int s=0;s<sources.GetCount();s++){ValueMap src=sources[s];if(S(src,"identity")==S(e,"source_identity")&&S(src,"revision")==S(e,"source_revision"))pinned_source=true;}if(!pinned_source){out.Set("status","unknown");return out;}if(S(e,"resolution")=="resolved"&&S(e,"visibility")=="visible"){usable_evidence.Add(id);ordered_evidence.Add(id);}}
	Sort(ordered_evidence);
	// Scores are validated in full before any category or candidate projection is emitted.
 	for(int i=0;i<scores.GetCount();i++){if(!scores[i].Is<ValueMap>()){out.Set("status","unknown");return out;}ValueMap s=scores[i];if(s.GetCount()<6||s.GetCount()>9||!IsS(s,"workspace_id")||S(s,"workspace_id")!=ws||!IsS(s,"candidate_id")||S(s,"candidate_id").IsEmpty()||!IsS(s,"dimension_id")||S(s,"dimension_id").IsEmpty()||!IsS(s,"provenance")||!IsS(s,"score")||!V(s,"evidence_ids").Is<ValueArray>()){out.Set("status","unknown");return out;}for(int k=0;k<s.GetCount();k++){String key=s.GetKey(k);if(!In(key,{"workspace_id","candidate_id","dimension_id","score","provenance","evidence_ids","rationale","crowding_judgment","opportunity_gap"})){out.Set("status","unknown");return out;}if(In(key,{"rationale","crowding_judgment","opportunity_gap"})&&(!IsString(s.GetValue(k))||((String)s.GetValue(k)).GetCount()>MAX_QUALITATIVE_TEXT)){out.Set("status","unknown");return out;}}ValueArray refs=A(s,"evidence_ids");for(int x=0;x<refs.GetCount();x++)if(!IsString(refs[x])){out.Set("status","unknown");return out;}candidates.FindAdd(S(s,"candidate_id"));}
	for(int i=0;i<relationships.GetCount();i++){if(!relationships[i].Is<ValueMap>()){out.Set("status","unknown");return out;}ValueMap r=relationships[i];String lk=S(r,"left_kind"),rk=S(r,"right_kind"),left=S(r,"left"),right=S(r,"right"),type=S(r,"type");
		if(r.GetCount()!=6||!IsS(r,"workspace_id")||S(r,"workspace_id")!=ws||!IsS(r,"left")||!IsS(r,"right")||!IsS(r,"left_kind")||!IsS(r,"right_kind")||!IsS(r,"type")||left.IsEmpty()||right.IsEmpty()||!In(lk,{"candidate","source"})||!In(rk,{"candidate","source"})||!In(type,{"overlap","related","duplicate","depends-on"})) {out.Set("status","unknown");return out;}
		bool left_known=lk=="candidate"?Member(candidates,left):Member(source_endpoint_ids,left),right_known=rk=="candidate"?Member(candidates,right):Member(source_endpoint_ids,right);
		if(!left_known||!right_known||(lk==rk&&left==right)||((type=="overlap"||type=="duplicate")&&lk!=rk)||(type=="depends-on"&&(lk!="candidate"||rk!="source"))){out.Set("status","unknown");return out;}}
	if((!V(in,"tool_config").IsVoid()&&!IsS(in,"tool_config"))||(!V(in,"model_config").IsVoid()&&!IsS(in,"model_config"))){out.Set("status","unknown");return out;}
	out.Set("status","configured");out.Add("rubric_id",S(rubric,"id"));out.Add("rubric_revision",S(rubric,"revision"));out.Add("tool_config",IsS(in,"tool_config")?S(in,"tool_config"):"unconfigured");out.Add("model_config",IsS(in,"model_config")?S(in,"model_config"):"unconfigured");
	out.Add("unique_source_count",source_ids.GetCount());out.Add("unique_source_revision_count",source_revisions.GetCount());out.Add("unique_evidence_count",usable_evidence.GetCount());ValueArray evkeys;for(const String& id:ordered_evidence)evkeys.Add(id);out.Add("evidence_keys",evkeys);
	ValueArray ordered_rels;Vector<String> rel_json;for(int i=0;i<relationships.GetCount();i++)rel_json.Add(Json(relationships[i]));Sort(rel_json);for(const String& s:rel_json)ordered_rels.Add(ParseJSON(s));out.Add("relationships",ordered_rels);
	// Only explicitly typed overlap links join candidates; other relationship types remain preserved.
	Index<String> graph_nodes;for(int i=0;i<candidates.GetCount();i++)graph_nodes.FindAdd("candidate:"+candidates[i]);for(int i=0;i<source_endpoint_ids.GetCount();i++)graph_nodes.FindAdd("source:"+source_endpoint_ids[i]);
	Vector<String> entity_ids;for(int i=0;i<graph_nodes.GetCount();i++)entity_ids.Add(graph_nodes[i]);Sort(entity_ids);Vector<int> parent;for(int i=0;i<entity_ids.GetCount();i++)parent.Add(i);
	auto root=[&](int x){while(parent[x]!=x){parent[x]=parent[parent[x]];x=parent[x];}return x;};
	for(int i=0;i<relationships.GetCount();i++){ValueMap rel=relationships[i];if(S(rel,"type")!="overlap")continue;String a=S(rel,"left_kind")+":"+S(rel,"left"),b=S(rel,"right_kind")+":"+S(rel,"right");int ia=FindString(entity_ids,a),ib=FindString(entity_ids,b);int ra=root(ia),rb=root(ib);if(ra!=rb){if(ra<rb)parent[rb]=ra;else parent[ra]=rb;}}
	ValueArray groups;Index<int> seen;for(int i=0;i<entity_ids.GetCount();i++){int rr=root(i);if(seen.Find(rr)>=0)continue;ValueArray members;for(int j=0;j<entity_ids.GetCount();j++)if(root(j)==rr)members.Add(entity_ids[j]);if(members.GetCount()<2)continue;seen.Add(rr);ValueMap g;g.Add("group_id","overlap-"+DataStore::ContentDigest(Json(members)).Left(24));g.Add("members",members);groups.Add(g);}out.Add("overlap_groups",groups);
	ValueArray cand_order;Vector<String> csorted;for(int i=0;i<candidates.GetCount();i++)csorted.Add(candidates[i]);Sort(csorted);for(const String& c:csorted)cand_order.Add(c);
	ValueArray assessments,categories,findings;Vector<String> category_json;
	for(int i=0;i<scores.GetCount();i++){ValueMap s=scores[i];String prov=S(s,"provenance"),dim_id=S(s,"dimension_id");bool provenance_known=In(prov,{"computed","authored","ai","historical","override"});String record_prov=provenance_known?prov:"unknown";ValueMap rec=Base("category-assessment","pending",ws,record_prov);rec.Add("candidate_id",S(s,"candidate_id"));rec.Add("analysis_run","pending");rec.Add("assessment",s);category_json.Add(Json(rec));if(!provenance_known||!Member(dim_ids,dim_id)){ValueMap f;f.Add("code",provenance_known?"unknown_dimension":"unknown_provenance");f.Add("candidate_id",S(s,"candidate_id"));f.Add("dimension_id",dim_id);findings.Add(f);}}
	for(const String& candidate:csorted){ValueArray dims_out;bool complete=true;int64 numerator=0,denominator=0;
		for(int d=0;d<dims.GetCount();d++){T61Dimension dim=dims[d];Index<String> values,covered;bool invalid_score=false;
			for(int i=0;i<scores.GetCount();i++){ValueMap s=scores[i];if(S(s,"candidate_id")!=candidate)continue;String prov=S(s,"provenance"),dim_id=S(s,"dimension_id");
				if(!In(prov,{"computed","authored","ai","historical","override"})||!Member(dim_ids,dim_id)||dim_id!=dim.id||prov!="computed")continue;
				int64 score;String score_text=S(s,"score");if(!Decimal(score_text,score)||score<dim.lo||score>dim.hi){invalid_score=true;continue;}values.FindAdd(score_text);
				Value refs_value=V(s,"evidence_ids");if(!refs_value.Is<ValueArray>()){invalid_score=true;continue;}ValueArray refs=refs_value;for(int q=0;q<refs.GetCount();q++){if(!IsString(refs[q])){invalid_score=true;continue;}String ref=refs[q];if(usable_evidence.Find(ref)>=0)covered.FindAdd(ref);else if(evidence_ids.Find(ref)<0){ValueMap f;f.Add("code","unknown_evidence_reference");f.Add("candidate_id",candidate);f.Add("dimension_id",dim.id);f.Add("evidence_id",ref);findings.Add(f);}}
			}
			bool conflict=values.GetCount()>1;bool has_score=values.GetCount()==1&&!invalid_score;int64 score=0;if(has_score&&!Decimal(values[0],score))has_score=false;
			ValueMap da;da.Add("dimension_id",dim.id);da.Add("score",has_score&&!conflict?AsString(score):"unknown");da.Add("score_state",conflict?"conflict":has_score?"known":"unknown");da.Add("evidence_count",covered.GetCount());da.Add("coverage_state",covered.GetCount()>=dim.evidence?"covered":"incomplete");dims_out.Add(da);
			bool covered_ok=covered.GetCount()>=dim.evidence;if(!has_score||conflict||!covered_ok)complete=false;
			if(conflict){ValueMap f;f.Add("code","contradictory_computed_score");f.Add("candidate_id",candidate);f.Add("dimension_id",dim.id);findings.Add(f);}else if(!has_score){ValueMap f;f.Add("code","missing_or_invalid_score");f.Add("candidate_id",candidate);f.Add("dimension_id",dim.id);findings.Add(f);}else if(!covered_ok){ValueMap f;f.Add("code","insufficient_dimension_evidence");f.Add("candidate_id",candidate);f.Add("dimension_id",dim.id);ValueArray es;for(int j=0;j<covered.GetCount();j++)es.Add(covered[j]);f.Add("evidence_ids",es);findings.Add(f);}
			if(has_score&&!conflict){numerator+=score*dim.weight;denominator+=dim.weight;}
		}
		ValueMap a;a.Add("candidate_id",candidate);a.Add("provenance","computed");a.Add("dimensions",dims_out);a.Add("uncertainty",complete?"complete":"insufficient_or_conflicted");if(complete){int64 n=numerator,d=total=="weighted-sum"?1:denominator;String tier;int matches=0;for(int i=0;i<tiers.GetCount();i++){ValueMap t=tiers[i];int64 ln=(int64)V(t,"minimum_numerator"),ld=(int64)V(t,"minimum_denominator"),un=(int64)V(t,"maximum_numerator"),ud=(int64)V(t,"maximum_denominator");if(n*ld>=ln*d&&n*ud<un*d){tier=S(t,"id");matches++;}}if(matches==1){a.Add("total_numerator",AsString(n));a.Add("total_denominator",AsString(d));a.Add("tier",tier);}else{a.Add("total_state","unknown");a.Add("tier","unknown");a.Set("uncertainty","tier_boundary_unresolved");}}else{a.Add("total_state","unknown");a.Add("tier","unknown");}assessments.Add(a);
	}
	Sort(category_json);ValueArray categories_sorted;String last_category;for(const String& s:category_json){if(s==last_category)continue;last_category=s;ValueMap c=ParseJSON(s);String id="category-assessment-"+DataStore::ContentDigest(S(c,"candidate_id")+"\n"+Json(V(c,"assessment"))).Left(32);c.Set("id",id);c.Set("envelope",Env("category-assessment",id,ws,S(c,"provenance")));categories_sorted.Add(c);}
	// Input-level narrative findings are retained with authored provenance and bounded source links.
	Value narrative=V(in,"findings");if(!narrative.IsVoid()&&!narrative.IsNull()){if(!narrative.Is<ValueArray>()){out.Set("status","unknown");return out;}ValueArray nf=narrative;for(int i=0;i<nf.GetCount();i++){if(!nf[i].Is<ValueMap>()){out.Set("status","unknown");return out;}ValueMap f=nf[i];if(!IsS(f,"workspace_id")||S(f,"workspace_id")!=ws||!IsS(f,"code")||!IsS(f,"provenance")){out.Set("status","unknown");return out;}for(int k=0;k<f.GetCount();k++){String key=f.GetKey(k);if(!In(key,{"workspace_id","code","provenance","candidate_id","dimension_id","evidence_id","evidence_ids","supporting_evidence_ids","counterevidence_ids","source_ids"})){out.Set("status","unknown");return out;}if(In(key,{"evidence_ids","supporting_evidence_ids","counterevidence_ids","source_ids"})){Value v=f.GetValue(k);if(!v.Is<ValueArray>()){out.Set("status","unknown");return out;}ValueArray ids=v;for(int j=0;j<ids.GetCount();j++)if(!IsString(ids[j])){out.Set("status","unknown");return out;}}else if(!IsString(f.GetValue(k))){out.Set("status","unknown");return out;}}findings.Add(f);}}
	ValueArray sorted_findings;Vector<String> fj;for(int i=0;i<findings.GetCount();i++)fj.Add(Json(findings[i]));Sort(fj);for(const String& s:fj)sorted_findings.Add(ParseJSON(s));
	Vector<String> pj;for(int i=0;i<pins.GetCount();i++)pj.Add(Json(pins[i]));Sort(pj);ValueArray sorted_pins;for(const String& s:pj)sorted_pins.Add(ParseJSON(s));
	Vector<String> sj;for(int i=0;i<sources.GetCount();i++)sj.Add(Json(sources[i]));Sort(sj);ValueArray sorted_sources;for(const String& s:sj)sorted_sources.Add(ParseJSON(s));
	out.Add("candidate_assessments",assessments);out.Add("category_assessments",categories_sorted);out.Add("findings",sorted_findings);out.Add("input_revisions",sorted_pins);out.Add("source_provenance",sorted_sources);
	bool all_complete=!assessments.IsEmpty();for(int i=0;i<assessments.GetCount();i++)if(S((ValueMap)assessments[i],"uncertainty")!="complete")all_complete=false;
	out.Add("uncertainty",csorted.IsEmpty()?"no_candidates":usable_evidence.IsEmpty()?"no_usable_evidence":all_complete?"complete_supplied_evidence":"insufficient_coverage");return out;
}

ValueArray AssessmentService::BuildRecords(const ValueMap& p){
	ValueArray out;if(S(p,"status")!="configured"||S(p,"workspace_id").IsEmpty())return out;String ws=S(p,"workspace_id"),run_id="analysis-run-"+DataStore::ContentDigest(Json(p)).Left(32);
	ValueMap run=Base("analysis-run",run_id,ws);for(const String& k:{"status","rubric_id","rubric_revision","tool_config","model_config","input_revisions","unique_source_count","unique_source_revision_count","unique_evidence_count","evidence_keys","relationships","overlap_groups","uncertainty","source_provenance"})run.Add(k,V(p,k));out.Add(run);
	ValueArray findings=A(p,"findings");for(int i=0;i<findings.GetCount();i++){if(!findings[i].Is<ValueMap>())continue;ValueMap f=findings[i];String id="assessment-finding-"+DataStore::ContentDigest(run_id+"\n"+Json(f)).Left(32);ValueMap r=Base("assessment-finding",id,ws,S(f,"provenance").IsEmpty()?"computed":S(f,"provenance"));r.Add("analysis_run",run_id);r.Add("finding",f);out.Add(r);}
	ValueArray categories=A(p,"category_assessments");for(int i=0;i<categories.GetCount();i++){ValueMap c=categories[i];String prov=S(c,"provenance"),id="category-assessment-"+DataStore::ContentDigest(run_id+"\n"+Json(c)).Left(32);ValueMap r=Base("category-assessment",id,ws,prov);r.Add("candidate_id",S(c,"candidate_id"));r.Add("analysis_run",run_id);r.Add("assessment",V(c,"assessment"));out.Add(r);}
	ValueArray candidates=A(p,"candidate_assessments");for(int i=0;i<candidates.GetCount();i++){ValueMap a=candidates[i];String id="candidate-assessment-"+DataStore::ContentDigest(run_id+"\n"+S(a,"candidate_id")).Left(32);ValueMap r=Base("candidate-assessment",id,ws);r.Add("candidate_id",S(a,"candidate_id"));r.Add("analysis_run",run_id);r.Add("assessment",a);out.Add(r);}return out;
}

static bool TrustedSnapshot(const DataStore& store, const DataWriteAuthorization& auth, const String& ws){
	return store.GetLoadStatus()=="loaded" && !store.IsReadOnly() && auth.AllowsSource(ws,store.GetSourceId());
}
static bool HasKindId(const DataStore& store,const String& ws,const String& id,const String& kind){
	const DataDocument *doc=store.FindDocument(ws+":"+id);
	return doc&&doc->valid&&doc->validated&&doc->validation_state=="valid"&&doc->workspace_id==ws&&doc->id==id&&doc->kind==kind;
}
static DataBatchChange CreateAssessmentChange(const String& ws,const ValueMap& record){
	DataBatchChange c;c.action=DATA_BATCH_CREATE;c.qualified_id=ws+":"+S(record,"id");c.proposed=record;return c;
}
static String CleanOperation(const String& stem,const String& digest){return stem+"-"+digest.Left(40);}
static Index<String> RunCandidates(const DataStore& store,const String& ws,const String& run_id){
	Index<String> ids;for(const DataDocument& d:store.GetDocuments())if(d.valid&&d.validated&&d.workspace_id==ws&&d.kind=="candidate-assessment"&&S(d.data,"analysis_run")==run_id)ids.FindAdd(S(d.data,"candidate_id"));return ids;
}
static Index<String> ProjectionCandidates(const ValueArray& records,const String& ws){
	Index<String> ids;for(const Value& v:records){ValueMap d=v;if(S(d,"kind")=="candidate-assessment"&&S(d,"workspace_id")==ws)ids.FindAdd(S(d,"candidate_id"));}return ids;
}
static ValueMap SourceRevisionMap(const ValueArray& sources){
	ValueMap out;for(const Value& v:sources){ValueMap s=v;String id=S(s,"identity"),revision=S(s,"revision");if(id.IsEmpty()||revision.IsEmpty())continue;
		ValueArray versions;int at=out.Find(id);if(at>=0)versions=out.GetValue(at);bool duplicate=false;for(const Value& old:versions)if((String)old==revision)duplicate=true;if(!duplicate)versions.Add(revision);
		Vector<String> sorted;for(const Value& version:versions)sorted.Add(version);Sort(sorted);ValueArray normalized;for(const String& version:sorted)normalized.Add(version);out.Set(id,normalized);
	}return out;
}
static bool SharedSource(const ValueMap& old_sources,const ValueMap& new_sources){
	for(int i=0;i<old_sources.GetCount();i++)if(new_sources.Find(old_sources.GetKey(i))>=0)return true;return false;
}
static void DiffSourceDependencies(const ValueMap& old_sources,const ValueMap& new_sources,bool candidate_overlap,ValueMap& flags){
	bool set_changed=false,revision_changed=false;
	for(int i=0;i<old_sources.GetCount();i++){String id=old_sources.GetKey(i);int n=new_sources.Find(id);if(n<0)set_changed=true;else if(old_sources.GetValue(i)!=new_sources.GetValue(n))revision_changed=true;}
	for(int i=0;i<new_sources.GetCount();i++)if(old_sources.Find(new_sources.GetKey(i))<0)set_changed=true;
	flags.Set("source_dependency_set_changed",candidate_overlap&&set_changed);
	flags.Set("source_revision_changed",revision_changed);
}
static bool RelevantPinsChanged(const ValueArray& old_pins,const ValueArray& new_pins,bool candidate_overlap,const ValueMap& old_sources,const ValueMap& new_sources){
	if(candidate_overlap)return AsJSON(old_pins,true)!=AsJSON(new_pins,true);
	ValueArray old_shared,new_shared;
	for(const Value& v:old_pins){ValueMap pin=v;String id=S(pin,"id");if(old_sources.Find(id)>=0&&new_sources.Find(id)>=0)old_shared.Add(pin);}
	for(const Value& v:new_pins){ValueMap pin=v;String id=S(pin,"id");if(old_sources.Find(id)>=0&&new_sources.Find(id)>=0)new_shared.Add(pin);}
	return AsJSON(old_shared,true)!=AsJSON(new_shared,true);
}
static bool TargetCurrent(const DataStore& pinned,const ValueMap& rubric,const ValueMap& inputs,const DataDocument& target,String& status,ValueMap& projection){
	String ws=S(inputs,"workspace_id");status="stale_target";
	if(pinned.GetLoadStatus()!="loaded"||ws.IsEmpty()||S(rubric,"workspace_id")!=ws||target.workspace_id!=ws)return false;
	projection=AssessmentService::Project(rubric,inputs);if(S(projection,"status")!="configured"){status="invalid_input";return false;}
	String run_id=S(target.data,"analysis_run");const DataDocument *run=pinned.FindDocument(ws+":"+run_id);
	if(!run||!run->valid||!run->validated||run->kind!="analysis-run"||run->workspace_id!=ws){status="stale_target";return false;}
	if(S(run->data,"rubric_id")!=S(projection,"rubric_id")||S(run->data,"rubric_revision")!=S(projection,"rubric_revision")||
	   S(run->data,"tool_config")!=S(projection,"tool_config")||S(run->data,"model_config")!=S(projection,"model_config")||
	   AsJSON(A(run->data,"input_revisions"),true)!=AsJSON(A(projection,"input_revisions"),true))return false;
	bool candidate_present=false;ValueArray current_candidates=A(projection,"candidate_assessments");for(const Value& v:current_candidates){ValueMap candidate=v;if(S(candidate,"candidate_id")==S(target.data,"candidate_id")){candidate_present=true;if(AsJSON(V(target.data,"assessment"),true)!=AsJSON(candidate,true))return false;}}
	if(!candidate_present)return false;
	for(const DataDocument& marker:pinned.GetDocuments())if(marker.kind=="assessment-staleness"&&marker.workspace_id==ws&&S(marker.data,"prior_run_id")==run_id)return false;
	status="current";return true;
}

AssessmentService::OperationResult AssessmentService::Refresh(const DataStore& pinned,const ValueMap& rubric,
	const ValueMap& inputs,const DataWriteAuthorization& authorization){
	OperationResult out;out.status="denied";String ws=S(inputs,"workspace_id");
	if(ws.IsEmpty()||!TrustedSnapshot(pinned,authorization,ws)||S(rubric,"workspace_id")!=ws)return out;
	ValueMap projection=Project(rubric,inputs);if(S(projection,"status")!="configured"){out.status="invalid_input";return out;}
	ValueArray records=BuildRecords(projection);if(records.IsEmpty()){out.status="invalid_projection";return out;}
	String run_id=S((ValueMap)records[0],"id");ValueArray changes_records=records;ValueArray stale_records;
	Index<String> current_candidates=ProjectionCandidates(records,ws);ValueMap new_sources=SourceRevisionMap(A(projection,"source_provenance"));
	String rubric_id=S(projection,"rubric_id"),rubric_revision=S(projection,"rubric_revision");
	for(const DataDocument& prior:pinned.GetDocuments()){
		if(!prior.valid||!prior.validated||prior.workspace_id!=ws||prior.kind!="analysis-run"||prior.id==run_id)continue;
		Index<String> old_candidates=RunCandidates(pinned,ws,prior.id);bool candidate_overlap=false;for(const String& candidate:old_candidates)if(current_candidates.Find(candidate)>=0)candidate_overlap=true;
		ValueMap d=prior.data,old_sources=SourceRevisionMap(A(d,"source_provenance"));bool shared_source=SharedSource(old_sources,new_sources);
		if(!candidate_overlap&&!shared_source)continue;
		ValueArray old_pins=A(d,"input_revisions");bool pins_changed=RelevantPinsChanged(old_pins,A(projection,"input_revisions"),candidate_overlap,old_sources,new_sources);
		ValueMap flags;flags.Add("source_dependency_set_changed",false);flags.Add("source_revision_changed",false);flags.Add("input_revision_changed",pins_changed);
		flags.Add("rubric_revision_changed",S(d,"rubric_id")!=rubric_id||S(d,"rubric_revision")!=rubric_revision);
		flags.Add("tool_config_changed",S(d,"tool_config")!=S(projection,"tool_config"));
		flags.Add("model_config_changed",S(d,"model_config")!=S(projection,"model_config"));
		DiffSourceDependencies(old_sources,new_sources,candidate_overlap,flags);
		bool changed=false;for(int i=0;i<flags.GetCount();i++)if((bool)flags.GetValue(i))changed=true;
		if(!changed)continue;
		ValueMap marker=Base("assessment-staleness","pending",ws);marker.Add("prior_run_id",prior.id);marker.Add("replacement_run_id",run_id);marker.Add("reason_flags",flags);
		String mid="assessment-staleness-"+DataStore::ContentDigest(ws+"\n"+prior.id+"\n"+run_id+"\n"+AsJSON(flags,true)).Left(32);
		marker.Set("id",mid);marker.Set("envelope",Env("assessment-staleness",mid,ws,"computed"));stale_records.Add(marker);
	}
	for(const Value& rec:stale_records)changes_records.Add(rec);
	if(changes_records.GetCount()>MAX_REFRESH_RECORDS){out.status="batch_too_large";return out;}
	Vector<DataBatchChange> changes;for(const Value& v:changes_records){ValueMap r=v;const DataDocument *existing=pinned.FindDocument(ws+":"+S(r,"id"));if(existing){if(!existing->valid||!existing->validated||AsJSON(existing->data,true)!=AsJSON(r,true)){out.status="identity_conflict";return out;}continue;}changes.Add(CreateAssessmentChange(ws,r));}
	out.operation_id=CleanOperation("assessment-refresh",DataStore::ContentDigest(ws+"\n"+AsJSON(projection,true)+"\n"+AsJSON(stale_records,true)));
	if(changes.IsEmpty()){out.status="already_current";out.run_id=run_id;return out;}
	DataBatchResult result=pinned.PublishBatch(authorization,out.operation_id,changes);out.status=result.status;for(const String& id:result.changed_ids)out.changed_ids.Add(id);out.run_id=run_id;return out;
}

AssessmentService::OperationResult AssessmentService::Override(const DataStore& pinned,const ValueMap& rubric,
	const ValueMap& current_inputs,
	const String& ws,const String& target_id,const String& expected_revision,const String& dimension_id,const String& replacement,
	const String& previous_override_id,const String& reason,const String& actor,const String& timestamp,
	const String& operation_key,const DataWriteAuthorization& authorization){
	OperationResult out;out.status="denied";
	if(ws.IsEmpty()||!TrustedSnapshot(pinned,authorization,ws)||S(current_inputs,"workspace_id")!=ws||S(rubric,"workspace_id")!=ws||actor.IsEmpty()||actor!=authorization.GetPrincipal()||reason.IsEmpty()||reason.GetCount()>4096||!AssessmentTimestamp(timestamp)||operation_key.IsEmpty()||operation_key.GetCount()>256)return out;
	const DataDocument *target=pinned.FindDocument(ws+":"+target_id);
	if(!target||!target->valid||!target->validated||target->kind!="candidate-assessment"||target->workspace_id!=ws){out.status="target_invalid";return out;}
	if(target->revision!=expected_revision){out.status="stale_target";return out;}
	ValueMap current_projection;if(!TargetCurrent(pinned,rubric,current_inputs,*target,out.status,current_projection))return out;
	Vector<T61Dimension> dims;Index<String> ids;ValueArray tiers;String total;
	if(!ValidateRubric(rubric,ws,dims,ids,tiers,total)||S(target->data,"analysis_run").IsEmpty()){out.status="rubric_invalid";return out;}
	ValueMap assessment=V(target->data,"assessment");ValueArray dimensions=A(assessment,"dimensions");String current;int matches=0;
	for(const Value& v:dimensions){ValueMap dim=v;if(S(dim,"dimension_id")==dimension_id&&S(dim,"score_state")=="known"){current=S(dim,"score");matches++;}}
	int di=-1;for(int i=0;i<dims.GetCount();i++)if(dims[i].id==dimension_id)di=i;
	int64 score;if(matches!=1||di<0||!Decimal(replacement,score)||score<dims[di].lo||score>dims[di].hi){out.status="invalid_replacement";return out;}
	Index<String> heads;for(const DataDocument& d:pinned.GetDocuments())if(d.valid&&d.validated&&d.kind=="assessment-override"&&d.workspace_id==ws&&S(d.data,"target_id")==target_id&&S(d.data,"dimension_id")==dimension_id)heads.FindAdd(S(d.data,"id"));
	Index<String> has_child;for(const DataDocument& d:pinned.GetDocuments())if(d.valid&&d.validated&&d.kind=="assessment-override"&&d.workspace_id==ws&&S(d.data,"target_id")==target_id&&S(d.data,"dimension_id")==dimension_id&&S(d.data,"previous_override_id")!="none")has_child.FindAdd(S(d.data,"previous_override_id"));
	String actual_head;int head_count=0;for(const String& id:heads)if(has_child.Find(id)<0){actual_head=id;head_count++;}
	String computed=current;String op=CleanOperation("assessment-override",DataStore::ContentDigest(ws+"\n"+operation_key));
	String id="assessment-override-"+DataStore::ContentDigest(op).Left(32);ValueMap record=Base("assessment-override",id,ws,"authored");
	if(!previous_override_id.IsEmpty())for(const DataDocument& event:pinned.GetDocuments())if(event.kind=="assessment-override"&&event.id==previous_override_id&&event.workspace_id==ws&&S(event.data,"target_id")==target_id&&S(event.data,"dimension_id")==dimension_id)current=S(event.data,"replacement");
	record.Add("target_id",target_id);record.Add("target_revision",expected_revision);record.Add("candidate_id",S(target->data,"candidate_id"));record.Add("dimension_id",dimension_id);record.Add("field","score");record.Add("computed_value",computed);record.Add("prior_value",current);record.Add("replacement",replacement);record.Add("reason",reason);record.Add("actor",actor);record.Add("authority","record:write");record.Add("timestamp",timestamp);record.Add("previous_override_id",previous_override_id.IsEmpty()?"none":previous_override_id);record.Add("operation_id",op);
	const DataDocument *existing=pinned.FindDocument(ws+":"+id);
	if(existing){out.operation_id=op;if(existing->valid&&existing->validated&&existing->kind=="assessment-override"&&existing->workspace_id==ws&&AsJSON(existing->data,true)==AsJSON(record,true))out.status="already_committed";else out.status="idempotency_conflict";return out;}
	if(head_count>1||actual_head!=previous_override_id||(previous_override_id.IsEmpty()&&heads.GetCount()!=0)){out.status="override_head_conflict";return out;}
	if(!previous_override_id.IsEmpty()&&!HasKindId(pinned,ws,previous_override_id,"assessment-override")){out.status="override_head_missing";return out;}
	if(!previous_override_id.IsEmpty()){
		OverrideHistory history=GetOverrideHistory(pinned,rubric,current_inputs,target_id,expected_revision,dimension_id,previous_override_id);
		if(history.status!="ok"||history.events.IsEmpty()||S((ValueMap)history.events.Top(),"id")!=previous_override_id){out.status="override_head_conflict";return out;}
		current=S((ValueMap)history.events.Top(),"replacement");
		record.Set("prior_value",current);
	}
	Vector<DataBatchChange> changes;changes.Add(CreateAssessmentChange(ws,record));
	DataBatchResult result=pinned.PublishBatch(authorization,op,changes);out.status=result.status;out.operation_id=op;for(const String& changed:result.changed_ids)out.changed_ids.Add(changed);return out;
}

AssessmentService::OverrideHistory AssessmentService::GetOverrideHistory(const DataStore& pinned,const ValueMap& rubric,
	const ValueMap& current_inputs,const String& target_id,const String& expected_revision,const String& dimension_id,const String& head_id){
	OverrideHistory out;out.status="invalid";const DataDocument *target=NULL;
	for(const DataDocument& d:pinned.GetDocuments())if(d.id==target_id&&d.kind=="candidate-assessment"&&d.valid&&d.validated){if(target){out.status="ambiguous_target";return out;}target=&d;}
	if(!target||target->revision!=expected_revision||pinned.GetLoadStatus()!="loaded"){out.status="target_invalid";return out;}
	ValueMap current_projection;if(!TargetCurrent(pinned,rubric,current_inputs,*target,out.status,current_projection))return out;
	ValueArray dims=A(V(target->data,"assessment"),"dimensions");int found=0;for(const Value& v:dims){ValueMap d=v;if(S(d,"dimension_id")==dimension_id){out.computed_value=S(d,"score");found++;}}
	if(found!=1){out.status="dimension_invalid";return out;}
	if(head_id.IsEmpty()){out.status="head_required";return out;}
	Vector<const DataDocument*> all;int roots=0;const DataDocument *root=NULL;
	for(const DataDocument& d:pinned.GetDocuments())if(d.kind=="assessment-override"&&d.workspace_id==target->workspace_id&&S(d.data,"target_id")==target_id&&S(d.data,"dimension_id")==dimension_id){
		if(!d.valid||!d.validated){out.status="invalid_chain";return out;}all.Add(&d);
		if(S(d.data,"previous_override_id")=="none"){root=&d;roots++;}
	}
	if(all.GetCount()>MAX_OVERRIDE_HISTORY){out.status="history_limit";return out;}
	if(roots!=1){out.status=roots?"fork":"missing_history";return out;}
	Vector<const DataDocument*> chain;Index<String> chain_ids;const DataDocument *current=root;
	for(;;){
		String id=S(current->data,"id");if(chain_ids.Find(id)>=0){out.status="cycle";return out;}chain_ids.Add(id);chain.Add(current);
		const DataDocument *child=NULL;int children=0;for(const DataDocument *event:all)if(S(event->data,"previous_override_id")==id){child=event;children++;}
		if(children>1){out.status="fork";return out;}if(!children)break;
		if(chain.GetCount()>=MAX_OVERRIDE_HISTORY){out.status="history_limit";return out;}current=child;
	}
	if(S(chain.Top()->data,"id")!=head_id){out.status="invalid_head";return out;}
	if(chain.GetCount()!=all.GetCount()){
		for(const DataDocument *start:all)if(chain_ids.Find(S(start->data,"id"))<0){Index<String> path;String id=S(start->data,"id");
			for(int n=0;n<=all.GetCount();n++){
				if(chain_ids.Find(id)>=0){out.status="fork";return out;}if(path.Find(id)>=0){out.status="cycle";return out;}path.Add(id);
				const DataDocument *event=NULL;int matches=0;for(const DataDocument *candidate:all)if(S(candidate->data,"id")==id){event=candidate;matches++;}
				if(matches!=1){out.status="missing_link";return out;}String previous=S(event->data,"previous_override_id");if(previous=="none"){out.status="fork";return out;}
				bool found_parent=false;for(const DataDocument *candidate:all)if(S(candidate->data,"id")==previous)found_parent=true;if(!found_parent){out.status="missing_link";return out;}id=previous;
			}
		}
		out.status="disconnected_history";return out;
	}
	String prior_value=out.computed_value;
	for(const DataDocument *event:chain){ValueMap value=event->data;
		if(S(value,"target_revision")!=expected_revision||S(value,"candidate_id")!=S(target->data,"candidate_id")||S(value,"computed_value")!=out.computed_value||S(value,"prior_value")!=prior_value||S(value,"authority")!="record:write"||S(value,"field")!="score"||!AssessmentTimestamp(S(value,"timestamp"))||S(value,"actor").IsEmpty()||S(value,"reason").IsEmpty()||S(value,"operation_id").IsEmpty()||S(value,"replacement").IsEmpty()){out.status="invalid_chain";return out;}
		prior_value=S(value,"replacement");out.events.Add(value);
	}
	out.status="ok";return out;
}

bool AssessmentService::RunSyntheticTests(){
	auto fail=[](const char* stage){Cerr()<<"T61 synthetic failed: "<<stage<<"\n";return false;};
	ValueMap r;r.Add("id","synthetic-rubric");r.Add("workspace_id","synthetic-ws");r.Add("revision","r1");r.Add("score_scale","integer-0-10");r.Add("total_rule","weighted-mean");ValueArray dims,cov,tiers;
	for(int i=0;i<6;i++){String id=Format("dim-%d",i);ValueMap d;d.Add("id",id);d.Add("weight",i+1);d.Add("minimum_score",0);d.Add("maximum_score",10);d.Add("minimum_evidence_count",1);dims.Add(d);cov.Add(id);}r.Add("dimensions",dims);r.Add("coverage_required",cov);
	ValueMap t;t.Add("id","tier-low");t.Add("minimum_numerator",0);t.Add("minimum_denominator",1);t.Add("maximum_numerator",5);t.Add("maximum_denominator",1);tiers.Add(t);ValueMap t2;t2.Add("id","tier-high");t2.Add("minimum_numerator",5);t2.Add("minimum_denominator",1);t2.Add("maximum_numerator",10);t2.Add("maximum_denominator",1);tiers.Add(t2);r.Add("tiers",tiers);
	ValueMap in;in.Add("workspace_id","synthetic-ws");ValueArray src,ev,sc,pins,rels;for(int i=0;i<2;i++){ValueMap s;s.Add("workspace_id","synthetic-ws");s.Add("identity","source");s.Add("revision",i?"r2":"r1");src.Add(s);ValueMap pin;pin.Add("workspace_id","synthetic-ws");pin.Add("id","source");pin.Add("revision",i?"r2":"r1");pins.Add(pin);}
	for(int i=0;i<6;i++){ValueMap e;e.Add("workspace_id","synthetic-ws");e.Add("item_id",Format("item-%d",i));e.Add("snapshot_id",Format("snap-%d",i));e.Add("source_identity","source");e.Add("source_revision","r1");e.Add("resolution","resolved");e.Add("visibility","visible");ev.Add(e);ValueMap a;a.Add("workspace_id","synthetic-ws");a.Add("candidate_id","candidate");a.Add("dimension_id",Format("dim-%d",i));a.Add("score","5");a.Add("provenance","computed");ValueArray refs;refs.Add(Format("item-%d",i));a.Add("evidence_ids",refs);sc.Add(a);ValueMap pin;pin.Add("workspace_id","synthetic-ws");pin.Add("id",Format("snap-%d",i));pin.Add("revision","r1");pins.Add(pin);}
	in.Add("sources",src);in.Add("evidence",ev);in.Add("scores",sc);in.Add("input_revisions",pins);in.Add("relationships",rels);
	ValueMap p=Project(r,in);if(S(p,"status")!="configured"||(int)V(p,"unique_source_count")!=1||(int)V(p,"unique_source_revision_count")!=2||(int)V(p,"unique_evidence_count")!=6)return fail("primary projection and unique counts");ValueMap ca=A(p,"candidate_assessments")[0];if(S(ca,"total_numerator")!="105"||S(ca,"total_denominator")!="21"||S(ca,"tier")!="tier-high")return fail("weighted mean and tier");
	ValueArray rev;for(int i=sc.GetCount()-1;i>=0;i--)rev.Add(sc[i]);ValueMap reordered=in;reordered.Set("scores",rev);if(Json(p)!=Json(Project(r,reordered)))return fail("input order invariance");
	ValueMap wrong=in;wrong.Set("evidence","bad");if(S(Project(r,wrong),"status")!="unknown")return fail("wrong array type");ValueMap missing=in;missing.Remove(missing.Find("relationships"));if(S(Project(r,missing),"status")!="unknown")return fail("missing required array");
	ValueArray authored=sc;for(const String& prov:{"authored","ai","historical","override"}){ValueMap a=(ValueMap)sc[0];a.Set("provenance",prov);a.Set("score","9");authored.Add(a);}ValueMap apin=in;apin.Set("scores",authored);ValueMap ap=Project(r,apin);if(Json(ca)!=Json(A(ap,"candidate_assessments")[0])||A(ap,"category_assessments").GetCount()!=10)return fail("non-computed score provenance separation");
	ValueArray conflict=sc;ValueMap dup=sc[0];dup.Set("score","4");conflict.Add(dup);ValueMap ci=in;ci.Set("scores",conflict);ValueMap cp=Project(r,ci);ValueArray cd=A((ValueMap)A(cp,"candidate_assessments")[0],"dimensions");if(S((ValueMap)cd[0],"score_state")!="conflict"||S((ValueMap)cd[1],"score_state")!="known")return fail("local score conflict");
	auto relation=[](const String& left,const String& left_kind,const String& right,const String& right_kind,const String& type){ValueMap e;e.Add("workspace_id","synthetic-ws");e.Add("left",left);e.Add("left_kind",left_kind);e.Add("right",right);e.Add("right_kind",right_kind);e.Add("type",type);return e;};
	ValueMap graph_in=in;ValueArray graph_scores=sc;for(const String& candidate:{"candidate-b","candidate-c","source"}){ValueMap a=(ValueMap)sc[0];a.Set("candidate_id",candidate);graph_scores.Add(a);}graph_in.Set("scores",graph_scores);ValueArray graph_sources=src;ValueMap source_b;source_b.Add("workspace_id","synthetic-ws");source_b.Add("identity","source-b");source_b.Add("revision","r1");graph_sources.Add(source_b);graph_in.Set("sources",graph_sources);ValueArray graph_pins=pins;ValueMap source_b_pin;source_b_pin.Add("workspace_id","synthetic-ws");source_b_pin.Add("id","source-b");source_b_pin.Add("revision","r1");graph_pins.Add(source_b_pin);graph_in.Set("input_revisions",graph_pins);
	ValueArray edge;edge.Add(relation("candidate","candidate","candidate-b","candidate","overlap"));edge.Add(relation("candidate-b","candidate","candidate-c","candidate","overlap"));ValueMap gi=graph_in;gi.Set("relationships",edge);ValueMap gp=Project(r,gi);ValueArray groups=A(gp,"overlap_groups");if(groups.GetCount()!=1||A((ValueMap)groups[0],"members").GetCount()!=3||((ValueArray)A((ValueMap)groups[0],"members"))[0]!="candidate:candidate")return fail("typed transitive overlap group");
	ValueArray source_relation;source_relation.Add(relation("source","source","source-b","source","related"));ValueMap sri=graph_in;sri.Set("relationships",source_relation);ValueMap srp=Project(r,sri);if(S(srp,"status")!="configured"||!A(srp,"overlap_groups").IsEmpty())return fail("valid source-only relationship");
	ValueArray candidate_source;candidate_source.Add(relation("source","candidate","source","source","related"));ValueMap namespace_in=graph_in;namespace_in.Set("relationships",candidate_source);ValueMap namespace_projection=Project(r,namespace_in);if(S(namespace_projection,"status")!="configured"||!A(namespace_projection,"overlap_groups").IsEmpty())return fail("identical raw IDs remain namespaced");
	ValueArray valid_dependency;valid_dependency.Add(relation("candidate","candidate","source","source","depends-on"));ValueMap dependency_in=graph_in;dependency_in.Set("relationships",valid_dependency);if(S(Project(r,dependency_in),"status")!="configured")return fail("candidate to source dependency");
	for(const ValueMap& bad_edge:Vector<ValueMap>{relation("candidate","candidate","absent","candidate","related"),relation("candidate","candidate","source","source","overlap"),relation("candidate","candidate","source","source","duplicate"),relation("source","source","candidate","candidate","depends-on"),relation("candidate","candidate","candidate-b","candidate","depends-on"),relation("candidate","candidate","candidate","candidate","overlap")}){ValueArray bad_rel;bad_rel.Add(bad_edge);ValueMap bad_graph=graph_in;bad_graph.Set("relationships",bad_rel);if(S(Project(r,bad_graph),"status")!="unknown")return fail("invalid relationship endpoint/type/self edge");}
	ValueMap badrub=r;badrub.Set("workspace_id","other");if(S(Project(badrub,in),"status")!="unconfigured")return fail("workspace rubric isolation");badrub=r;ValueArray unordered=tiers;unordered.Set(0,t2);unordered.Set(1,t);badrub.Set("tiers",unordered);if(S(Project(badrub,in),"status")!="unconfigured")return fail("tier order");
	ValueMap no_inputs=in;no_inputs.Set("scores",ValueArray());ValueMap no=Project(r,no_inputs);if(S(no,"uncertainty")!="no_candidates")return fail("no candidates uncertainty");ValueMap no_evidence=in;no_evidence.Set("evidence",ValueArray());if(S(Project(r,no_evidence),"uncertainty")!="no_usable_evidence")return fail("no evidence uncertainty");
	ValueMap sumrubric=r;sumrubric.Set("total_rule","weighted-sum");ValueArray sumtiers;ValueMap low;low.Add("id","below-boundary");low.Add("minimum_numerator",0);low.Add("minimum_denominator",1);low.Add("maximum_numerator",105);low.Add("maximum_denominator",1);sumtiers.Add(low);ValueMap high;high.Add("id","boundary-and-above");high.Add("minimum_numerator",105);high.Add("minimum_denominator",1);high.Add("maximum_numerator",210);high.Add("maximum_denominator",1);sumtiers.Add(high);sumrubric.Set("tiers",sumtiers);ValueMap sumproject=Project(sumrubric,in);if(S(sumproject,"status")!="configured")return fail("weighted-sum rubric");ValueMap sum_candidate=A(sumproject,"candidate_assessments")[0];if(S(sum_candidate,"total_numerator")!="105"||S(sum_candidate,"total_denominator")!="1"||S(sum_candidate,"tier")!="boundary-and-above")return fail("weighted sum and boundary tier");
	ValueMap invalid_tiers=sumrubric;ValueArray overlap;overlap.Add(low);ValueMap overlap2=high;overlap2.Set("minimum_numerator",104);overlap.Add(overlap2);invalid_tiers.Set("tiers",overlap);if(S(Project(invalid_tiers,in),"status")!="unconfigured")return fail("tier overlap");ValueMap out_range=sumrubric;ValueArray outside;ValueMap outside_tier=high;outside_tier.Set("maximum_numerator",212);outside.Add(outside_tier);out_range.Set("tiers",outside);if(S(Project(out_range,in),"status")!="unconfigured")return fail("out of range tier");
	ValueArray bad_refs=sc;ValueMap bad_score=sc[0];bad_score.Set("evidence_ids","not-array");bad_refs.Set(0,bad_score);ValueMap bad_ref_input=in;bad_ref_input.Set("scores",bad_refs);if(S(Project(r,bad_ref_input),"status")!="unknown")return fail("wrong evidence reference type");
	return true;
}

END_UPP_NAMESPACE
