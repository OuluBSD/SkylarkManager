#include "ReviewDiscussion.h"
#include "SystemAuth.h"

NAMESPACE_UPP

static Value RDGet(const ValueMap& m, const char *key) { int i=m.Find(key); return i<0?Value():m.GetValue(i); }
static String RDString(const ValueMap& m, const char *key) { Value v=RDGet(m,key); return IsString(v)?(String)v:String(); }
static ValueArray RDArray(const ValueMap& m, const char *key) { Value v=RDGet(m,key); return v.Is<ValueArray>()?(ValueArray)v:ValueArray(); }
static String RDQualified(const String& ws,const String& id) { return id.Find(':')>=0?id:ws+":"+id; }
static bool RDValidCommentBody(const String& source,String& normalized);

bool ReviewDiscussionService::IsPrivateKind(const String& kind)
{
	return kind=="review-package" || kind=="review-package-revision" || kind=="review-thread" || kind=="review-comment";
}

static const DataDocument *RDExact(const CatalogGeneration& c,const String& qid,const String& kind,const String& ws,int& count)
{
	const DataDocument *found=NULL; count=0;
	for(const DataDocument& d:c.data.GetDocuments()) if(d.qualified_id==qid&&d.workspace_id==ws&&(kind.IsEmpty()||d.kind==kind)) { found=&d; count++; }
	return found;
}

static bool RDCanonical(const DataDocument& d)
{
	return d.valid&&d.validated&&d.validation_state=="valid"&&!d.workspace_id.IsEmpty()&&
	       d.qualified_id==d.workspace_id+":"+d.id&&RDString(d.data,"id")==d.id&&RDString(d.data,"kind")==d.kind&&
	       RDString(d.data,"workspace_id")==d.workspace_id;
}

static bool RDPrivateCanonical(const DataDocument& d,const char *kind)
{
	Value envelope=RDGet(d.data,"envelope");
	return RDCanonical(d)&&d.kind==kind&&envelope.Is<ValueMap>()&&
	       RDString((ValueMap)envelope,"workspace_id")==d.workspace_id&&
	       RDString((ValueMap)envelope,"stable_id")==d.id&&
	       RDString((ValueMap)envelope,"schema_id")==String("skylark/")+kind&&
	       RDString((ValueMap)envelope,"record_kind")==kind&&
	       RDString((ValueMap)envelope,"visibility")=="private";
}

static bool RDLinkedVisible(const ManagerSkylarkApp& app,const DataDocument *d,const String& ws)
{
	if(!d||d->workspace_id!=ws||!RDCanonical(*d)||d->source_id.IsEmpty()||!app.IsAssessmentSourceVisible(ws,d->source_id)) return false;
	String identity=RDString(d->data,"source_identity");
	return identity.IsEmpty()||app.IsAssessmentSourceVisible(ws,identity);
}

static bool RDVisible(const ManagerSkylarkApp& app,const CatalogGeneration& c,const DataDocument& d);

static bool RDTopologyScopeVisible(const ManagerSkylarkApp& app,const CatalogGeneration& catalog,const String& ws,const String& scope)
{
	if(scope.IsEmpty()||scope.Find('/')>=0||scope.Find('\\')>=0||scope.Find("..")>=0) return false;
	String prefix="topology/"+scope+"/"; int count=0;
	for(const DataDocument& d:catalog.data.GetDocuments()) if(d.workspace_id==ws&&d.kind=="topology"&&d.id.StartsWith(prefix)) {
		count++;
		if(!RDLinkedVisible(app,&d,ws)||!RDVisible(app,catalog,d)) return false;
	}
	return count>0;
}

static Vector<const V2Goal*> RDGoalMatches(const ManagerSkylarkApp& app,const CatalogGeneration& catalog,const String& ws,const String& goal_id)
{
	Vector<const V2Goal*> exact,alias;
	for(const V2Goal& goal:catalog.v2.GetGoals()) {
		bool same_workspace=RDString(goal.source_data,"workspace_id")==ws||goal.repo==ws;
		if(!same_workspace) continue;
		String identity=RDString(goal.source_data,"source_identity"),visibility=RDString(goal.source_data,"visibility");
		if((!visibility.IsEmpty()&&visibility!="visible")||(!identity.IsEmpty()&&!app.IsAssessmentSourceVisible(ws,identity))) continue;
		if(goal.qualified_id==goal_id) exact.Add(&goal);
		else if(goal.id==goal_id) alias.Add(&goal);
	}
	Vector<const V2Goal*> matches;
	if(exact.IsEmpty()) for(const V2Goal *goal:alias) matches.Add(goal);
	else for(const V2Goal *goal:exact) matches.Add(goal);
	return matches;
}

static ValueArray RDRelatedReferences(const ManagerSkylarkApp& app,const CatalogGeneration& catalog,
	                                   const DataDocument& package,const char *field,const char *type)
{
	ValueArray out; bool unavailable=false;
	for(const Value& value:RDArray(package.data,field)) {
		if(!IsString(value)) { unavailable=true; continue; }
		String ref=(String)value; int n=0;
		const DataDocument *d=NULL; String topology_scope;
		if(String(type)=="topology") {
			d=RDExact(catalog,RDQualified(package.workspace_id,ref),type,package.workspace_id,n);
			if(n>1) { unavailable=true; continue; }
			if(d&&n==1) {
				String prefix="topology/"; String rest=d->id.StartsWith(prefix)?d->id.Mid(prefix.GetCount()):String(); int slash=rest.Find('/');
				if(slash<=0) { unavailable=true; continue; }
				topology_scope=rest.Left(slash);
			}
			else if(n==0) {
				topology_scope=ref;
				if(!RDTopologyScopeVisible(app,catalog,package.workspace_id,topology_scope)) topology_scope.Clear();
			}
			if(topology_scope.IsEmpty()||!RDTopologyScopeVisible(app,catalog,package.workspace_id,topology_scope)) { unavailable=true; continue; }
		}
		else {
			d=RDExact(catalog,RDQualified(package.workspace_id,ref),type,package.workspace_id,n);
			if(n!=1||!d||!RDLinkedVisible(app,d,package.workspace_id)||!RDVisible(app,catalog,*d)) { unavailable=true; continue; }
		}
		String title=String(type)=="topology"?topology_scope:d->title;
		if(String(type)=="topology") for(const DataDocument& topology:catalog.data.GetDocuments()) if(topology.workspace_id==package.workspace_id&&topology.kind=="topology"&&topology.id.StartsWith("topology/"+topology_scope+"/")) { title=topology.title.IsEmpty()?RDString(topology.data,"name"):topology.title; break; }
		ValueMap item; item.Add("id",String(type)=="topology"?topology_scope:d->id); item.Add("kind",String(type)); item.Add("title",title);
		if(String(type)=="evidence-item") {
			String snapshot=RDString(d->data,"snapshot"); int sn=0;
			const DataDocument *s=RDExact(catalog,RDQualified(package.workspace_id,snapshot),"evidence-snapshot",package.workspace_id,sn);
			if(sn!=1||!s||!RDLinkedVisible(app,s,package.workspace_id)||!RDVisible(app,catalog,*s)||
			   RDString(d->data,"visibility")!="visible"||RDString(d->data,"resolution")!="resolved"||RDString(d->data,"classification")!="public"||
			   RDString(s->data,"visibility")!="visible"||RDString(s->data,"resolution")!="resolved"||RDString(s->data,"classification")!="public"||
			   (bool)RDGet(s->data,"retracted")||RDString(d->data,"source_identity")!=RDString(s->data,"source_identity")||
			   RDString(d->data,"upstream_revision")!=RDString(s->data,"upstream_revision")||AsJSON(RDGet(d->data,"locator"),true)!=AsJSON(RDGet(s->data,"locator"),true)) { unavailable=true; continue; }
			item.Add("snapshot_id",s->id); item.Add("snapshot_revision",s->revision); item.Add("snapshot_source_revision",s->source_revision);
			item.Add("upstream_revision",RDString(d->data,"upstream_revision")); item.Add("evidence_revision",d->revision); item.Add("evidence_source_revision",d->source_revision);
			item.Add("locator",RDGet(d->data,"locator")); item.Add("excerpt",RDString(d->data,"excerpt"));
			item.Add("anchor","review-evidence-"+UrlEncode(d->id));
		}
		else if(String(type)=="topology") item.Add("url","/data/diagram/scope/"+UrlEncode(topology_scope)+"?workspace="+UrlEncode(package.workspace_id)+"&package="+UrlEncode(RDString(package.data,"package_id"))+"&revision="+UrlEncode(package.id));
		out.Add(item);
	}
	if(unavailable) { ValueMap marker; marker.Add("unavailable",true); out.Add(marker); }
	return out;
}

static bool RDCommentBound(const ManagerSkylarkApp& app,const CatalogGeneration& c,const DataDocument& comment)
{
	if(!RDPrivateCanonical(comment,"review-comment")) return false;
	String ws=comment.workspace_id, thread_id=RDString(comment.data,"thread_id"); int n=0;
	const DataDocument *thread=RDExact(c,RDQualified(ws,thread_id),"review-thread",ws,n);
	int revisions=0; const DataDocument *revision=RDExact(c,RDQualified(ws,RDString(comment.data,"revision_id")),"review-package-revision",ws,revisions);
	return n==1&&thread&&RDPrivateCanonical(*thread,"review-thread")&&revisions==1&&revision&&RDPrivateCanonical(*revision,"review-package-revision")&&
	       app.IsAssessmentSourceVisible(ws,revision->source_id)&&
	       RDString(revision->data,"package_id")==RDString(comment.data,"package_id")&&
	       RDString(revision->data,"patch_sha256")==RDString(comment.data,"revision_hash")&&
	       app.IsAssessmentSourceVisible(ws,thread->source_id)&&RDString(thread->data,"package_id")==RDString(comment.data,"package_id")&&
	       RDString(thread->data,"revision_id")==RDString(comment.data,"revision_id")&&
	       RDString(thread->data,"revision_hash")==RDString(comment.data,"revision_hash");
}

static bool RDVisibleDepth(const ManagerSkylarkApp& app,const CatalogGeneration& c,const DataDocument& d,int depth,Index<String>& visiting)
{
	if(depth>4||!RDCanonical(d)||d.source_id.IsEmpty()||!app.IsAssessmentSourceVisible(d.workspace_id,d.source_id)) return false;
	if(ReviewDiscussionService::IsPrivateKind(d.kind)&&!RDPrivateCanonical(d,d.kind)) return false;
	int identity_at=d.data.Find("source_identity");
	if(identity_at>=0) { Value identity=d.data.GetValue(identity_at); if(!IsString(identity)||!app.IsAssessmentSourceVisible(d.workspace_id,(String)identity)) return false; }
	if(d.kind=="review-comment") { String normalized; String body=RDString(d.data,"body"); if(!RDValidCommentBody(body,normalized)||normalized!=body) return false; }
	if(visiting.Find(d.qualified_id)>=0) return false;
	visiting.Add(d.qualified_id);
	for(const char *key:{"artifact_refs","check_refs"}) {
		int at=d.data.Find(key); if(at<0) continue;
		Value refs=d.data.GetValue(at); if(!refs.Is<ValueArray>()) { visiting.Remove(visiting.Find(d.qualified_id)); return false; }
		for(const Value& ref:ValueArray(refs)) {
			if(!IsString(ref)) { visiting.Remove(visiting.Find(d.qualified_id)); return false; }
			if(d.kind=="review-package-revision"&&String(key)=="artifact_refs") continue;
			if(String(key)=="check_refs"&&d.kind=="review-package-revision") {
				int exact_ref=0; RDExact(c,RDQualified(d.workspace_id,(String)ref),String(),d.workspace_id,exact_ref);
				int separator=((String)ref).Find(':'); String cross_type=separator<0?String():((String)ref).Mid(separator+1);
				if(exact_ref==0&&(separator<0||cross_type.StartsWith("topology:"))) continue;
			}
			int n; const DataDocument *linked=RDExact(c,RDQualified(d.workspace_id,(String)ref),String(),d.workspace_id,n);
			if(n!=1||!RDLinkedVisible(app,linked,d.workspace_id)||!RDVisibleDepth(app,c,*linked,depth+1,visiting)) { visiting.Remove(visiting.Find(d.qualified_id)); return false; }
		}
	}
	Value artifacts=RDGet(d.data,"artifacts");
	if(!artifacts.IsVoid()&&!artifacts.Is<ValueArray>()) { visiting.Remove(visiting.Find(d.qualified_id)); return false; }
	if(artifacts.Is<ValueArray>()) for(const Value& item:ValueArray(artifacts)) {
		if(!item.Is<ValueMap>()) { visiting.Remove(visiting.Find(d.qualified_id)); return false; }
		String path=RDString(ValueMap(item),"path");
		if(path.IsEmpty()||path.StartsWith("/")||path.Find("..")>=0||path.Find('\\')>=0) { visiting.Remove(visiting.Find(d.qualified_id)); return false; }
	}
	visiting.Remove(visiting.Find(d.qualified_id));
	return true;
}

static bool RDVisible(const ManagerSkylarkApp& app,const CatalogGeneration& c,const DataDocument& d)
{
	Index<String> visiting; return RDVisibleDepth(app,c,d,0,visiting);
}

static bool RDValidCommentBody(const String& source,String& normalized)
{
	int len=source.GetCount(),at=0;
	while(at<len) {
		byte b=(byte)source[at++]; uint32 cp; int extra;
		if(b<0x80) { cp=b; extra=0; }
		else if(b>=0xc2&&b<=0xdf) { cp=b&0x1f; extra=1; }
		else if(b>=0xe0&&b<=0xef) { cp=b&0x0f; extra=2; }
		else if(b>=0xf0&&b<=0xf4) { cp=b&0x07; extra=3; }
		else return false;
		if(at+extra>len) return false;
		for(int i=0;i<extra;i++) { byte c=(byte)source[at++]; if((c&0xc0)!=0x80) return false; cp=(cp<<6)|(c&0x3f); }
		if((extra==1&&cp<0x80)||(extra==2&&cp<0x800)||(extra==3&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)) return false;
		if((cp<0x20&&cp!='\n'&&cp!='\t')||(cp>=0x7f&&cp<=0x9f)) return false;
	}
	int first=0,last=len;
	while(first<last&&(source[first]==' '||source[first]=='\t'||source[first]=='\n')) first++;
	while(last>first&&(source[last-1]==' '||source[last-1]=='\t'||source[last-1]=='\n')) last--;
	normalized=source.Mid(first,last-first);
	return !normalized.IsEmpty()&&normalized.GetCount()<=8192;
}

ValueMap ReviewPackageViewService::Build(const ManagerSkylarkApp& app,const CatalogGeneration& catalog,
	                                      const String& package_id,const String& revision_id)
{
	ValueMap out; out.Add("status","ok"); out.Add("view_state","ready"); out.Add("catalog_generation",(int64)catalog.generation);
	ValueArray packages, empty; out.Add("packages",packages); out.Add("comments",empty);
	if(catalog.status!="loaded"||catalog.data_status!="loaded") { out.Set("view_state","unavailable"); return out; }
	Vector<const DataDocument*> visible;
	for(const DataDocument& d:catalog.data.GetDocuments())
		if(d.kind=="review-package-revision"&&RDVisible(app,catalog,d)) { if(visible.GetCount()>=512) { out.Set("view_state","unavailable"); return out; } visible.Add(&d); }
	// Validate every package lineage as a single rooted chain; malformed or hidden nodes suppress the whole package.
	Index<String> package_ids; for(const DataDocument *d:visible) package_ids.FindAdd(RDString(d->data,"package_id"));
	ValueArray rows; Index<String> valid_revision_ids; const DataDocument *selected=NULL; bool selected_is_head=false;
	for(const String& pid:package_ids) {
		Vector<const DataDocument*> chain; bool valid=true;
		for(const DataDocument& d:catalog.data.GetDocuments()) if(d.kind=="review-package-revision"&&RDString(d.data,"package_id")==pid) {
			if(!RDVisible(app,catalog,d)) { valid=false; continue; }
			chain.Add(&d);
		}
		int roots=0,root=-1; Vector<int> children; children.SetCount(chain.GetCount(),0);
		for(int i=0;i<chain.GetCount();i++) {
			String parent=RDString(chain[i]->data,"previous_revision");
			if(parent=="none") { roots++; root=i; continue; }
			int match=-1,n=0; for(int j=0;j<chain.GetCount();j++) if(chain[j]->id==parent) { match=j; n++; }
			if(n!=1||++children[match]>1) valid=false;
		}
		if(roots!=1) valid=false;
		Index<int> seen; int at=root, head=-1;
		while(valid&&at>=0) { if(seen.Find(at)>=0) { valid=false; break; } seen.Add(at); if(children[at]==0) { head=at; break; } int next=-1; for(int j=0;j<chain.GetCount();j++) if(RDString(chain[j]->data,"previous_revision")==chain[at]->id) next=j; at=next; }
		if(!valid||head<0||seen.GetCount()!=chain.GetCount()) continue;
		for(const DataDocument *d:chain) valid_revision_ids.FindAdd(d->qualified_id);
		const DataDocument *h=chain[head]; ValueMap row; row.Add("package_id",pid); row.Add("revision_id",h->id); row.Add("workspace_id",h->workspace_id);
		row.Add("scope_id",RDString(h->data,"scope_id")); row.Add("base_revision",RDString(h->data,"base_revision")); row.Add("head_revision",RDString(h->data,"head_revision")); row.Add("patch_sha256",RDString(h->data,"patch_sha256")); row.Add("selected_paths",RDGet(h->data,"selected_paths")); row.Add("artifacts",RDGet(h->data,"artifacts"));
		ValueArray row_goal_links; Vector<const V2Goal*> row_goals=RDGoalMatches(app,catalog,h->workspace_id,RDString(h->data,"goal_id"));
		if(row_goals.GetCount()==1) { const V2Goal& goal=*row_goals[0]; ValueMap link; link.Add("title",goal.title); link.Add("url","/goal/"+UrlEncode(goal.qualified_id)+"?workspace="+UrlEncode(h->workspace_id)+"&package="+UrlEncode(pid)+"&revision="+UrlEncode(h->id)); row_goal_links.Add(link); }
		else if(!RDString(h->data,"goal_id").IsEmpty()) { ValueMap marker; marker.Add("unavailable",true); row_goal_links.Add(marker); }
		row.Add("goal_links",row_goal_links); row.Add("topology_links",RDRelatedReferences(app,catalog,*h,"check_refs","topology"));
		ValueArray raw_evidence=RDRelatedReferences(app,catalog,*h,"artifact_refs","evidence-item"),row_evidence;
		for(const Value& ev:raw_evidence) { ValueMap item=ev; if(!(bool)RDGet(item,"unavailable")) item.Add("url","/review-package/"+UrlEncode(pid)+"/"+UrlEncode(h->id)+"#"+RDString(item,"anchor")); row_evidence.Add(item); }
		row.Add("evidence_items",row_evidence);
		row.Add("author",RDString(h->data,"author")); row.Add("author_state",RDString(h->data,"author_state")); row.Add("capture_state",RDString(h->data,"capture_state"));
		ValueArray check_summaries; bool unavailable_check=false;
		for(const Value& ref:RDArray(h->data,"check_refs")) if(IsString(ref)) { int n=0; const DataDocument *check=RDExact(catalog,RDQualified(h->workspace_id,(String)ref),String(),h->workspace_id,n); if(n==1&&check&&check->kind!="topology"&&check->workspace_id==h->workspace_id&&RDLinkedVisible(app,check,h->workspace_id)&&RDVisible(app,catalog,*check)) { ValueMap summary; summary.Add("id",check->id); summary.Add("kind",check->kind); summary.Add("title",check->title); summary.Add("status",check->status); summary.Add("url","/data/doc/"+UrlEncode(check->qualified_id)+"?workspace="+UrlEncode(h->workspace_id)+"&package="+UrlEncode(pid)+"&revision="+UrlEncode(h->id)); check_summaries.Add(summary); } else unavailable_check=true; }
		if(unavailable_check) { ValueMap marker; marker.Add("unavailable",true); check_summaries.Add(marker); }
		row.Add("check_summaries",check_summaries);
		Vector<ValueMap> visible_comments;
		for(const DataDocument& d:catalog.data.GetDocuments()) if(d.kind=="review-comment"&&d.workspace_id==h->workspace_id&&RDString(d.data,"package_id")==pid&&RDString(d.data,"revision_id")==h->id&&RDCommentBound(app,catalog,d)&&RDVisible(app,catalog,d)) { ValueMap c; c.Add("id",d.id); visible_comments.Add(c); }
		Sort(visible_comments,[](const ValueMap& a,const ValueMap& b){ return RDString(a,"id")<RDString(b,"id"); });
		row.Add("comment_count",min(visible_comments.GetCount(),256)); rows.Add(row);
		if(!package_id.IsEmpty()&&pid==package_id&&h->id==revision_id) { selected=h; selected_is_head=true; }
	}
	out.Set("packages",rows);
	if(!package_id.IsEmpty()) {
		if(!selected) for(const DataDocument *d:visible) if(RDString(d->data,"package_id")==package_id&&d->id==revision_id&&valid_revision_ids.Find(d->qualified_id)>=0) { selected=d; break; }
		if(!selected) { out.Set("view_state","unavailable"); out.Set("packages",empty); return out; }
		ValueMap detail=selected->data; int envelope=detail.Find("envelope"); if(envelope>=0) detail.Remove(envelope); detail.Add("stale",!selected_is_head);
		ValueArray goal_links;
		String goal_id=RDString(selected->data,"goal_id"); Vector<const V2Goal*> goal_matches=RDGoalMatches(app,catalog,selected->workspace_id,goal_id);
		if(goal_matches.GetCount()==1) { const V2Goal& goal=*goal_matches[0]; ValueMap link; link.Add("id",goal.qualified_id); link.Add("title",goal.title); link.Add("url","/goal/"+UrlEncode(goal.qualified_id)+"?workspace="+UrlEncode(selected->workspace_id)+"&package="+UrlEncode(RDString(selected->data,"package_id"))+"&revision="+UrlEncode(selected->id)); goal_links.Add(link); }
		if(goal_links.IsEmpty()&&!goal_id.IsEmpty()) { ValueMap marker; marker.Add("unavailable",true); goal_links.Add(marker); }
		if(!goal_id.IsEmpty()&&(goal_links.IsEmpty()||((ValueMap)goal_links[0]).Find("unavailable")>=0)) { int raw_goal=detail.Find("goal_id"); if(raw_goal>=0) detail.Remove(raw_goal); }
		int raw_checks=detail.Find("check_refs"); if(raw_checks>=0) detail.Remove(raw_checks);
		int raw_artifacts=detail.Find("artifact_refs"); if(raw_artifacts>=0) detail.Remove(raw_artifacts);
		detail.Add("goal_links",goal_links);
		detail.Add("topology_links",RDRelatedReferences(app,catalog,*selected,"check_refs","topology"));
		detail.Add("evidence_items",RDRelatedReferences(app,catalog,*selected,"artifact_refs","evidence-item"));
		out.Add("package",detail); out.Add("patch_text",RDString(selected->data,"patch_bytes"));
		ValueArray check_summaries; bool unavailable_check=false;
		for(const Value& ref:RDArray(selected->data,"check_refs")) if(IsString(ref)) { int n=0; const DataDocument *check=RDExact(catalog,RDQualified(selected->workspace_id,(String)ref),String(),selected->workspace_id,n); if(n==1&&check&&check->kind!="topology"&&check->workspace_id==selected->workspace_id&&RDLinkedVisible(app,check,selected->workspace_id)&&RDVisible(app,catalog,*check)) { ValueMap summary; summary.Add("id",check->id); summary.Add("kind",check->kind); summary.Add("title",check->title); summary.Add("status",check->status); summary.Add("url","/data/doc/"+UrlEncode(check->qualified_id)+"?workspace="+UrlEncode(selected->workspace_id)+"&package="+UrlEncode(RDString(selected->data,"package_id"))+"&revision="+UrlEncode(selected->id)); check_summaries.Add(summary); } else unavailable_check=true; }
		if(unavailable_check) { ValueMap marker; marker.Add("unavailable",true); check_summaries.Add(marker); }
		out.Add("check_summaries",check_summaries);
		Vector<ValueMap> comment_rows;
		for(const DataDocument& d:catalog.data.GetDocuments()) if(d.kind=="review-comment"&&d.workspace_id==selected->workspace_id&&RDString(d.data,"package_id")==package_id&&RDString(d.data,"revision_id")==revision_id&&RDCommentBound(app,catalog,d)&&RDVisible(app,catalog,d)) {
			ValueMap c; c.Add("id",d.id); c.Add("thread_id",RDString(d.data,"thread_id")); c.Add("revision_id",revision_id); c.Add("revision_hash",RDString(d.data,"revision_hash")); c.Add("record_type",RDString(d.data,"record_type")); c.Add("author_kind",RDString(d.data,"author_kind")); c.Add("author",RDString(d.data,"author")); c.Add("body",RDString(d.data,"body")); c.Add("created_at",RDGet(d.data,"created_at")); c.Add("stale",!selected_is_head); c.Add("evidence_items",RDRelatedReferences(app,catalog,d,"artifact_refs","evidence-item")); comment_rows.Add(c);
		}
		Sort(comment_rows,[](const ValueMap& a,const ValueMap& b){ int64 ta=(int64)RDGet(a,"created_at"),tb=(int64)RDGet(b,"created_at"); if(ta!=tb) return ta<tb; return RDString(a,"id")<RDString(b,"id"); });
		ValueArray comments; for(int i=0;i<min(comment_rows.GetCount(),256);i++) comments.Add(comment_rows[i]);
		out.Set("comments",comments);
	}
	return out;
}

ValueMap ReviewPackageViewService::BuildThread(const ManagerSkylarkApp& app,const CatalogGeneration& catalog,const String& thread_id)
{
	ValueMap unavailable; unavailable.Add("status","ok"); unavailable.Add("view_state","unavailable"); unavailable.Add("packages",ValueArray()); unavailable.Add("comments",ValueArray());
	if(catalog.status!="loaded"||catalog.data_status!="loaded") return unavailable;
	const DataDocument *thread=NULL; int matches=0;
	for(const DataDocument& d:catalog.data.GetDocuments()) if(d.kind=="review-thread"&&d.id==thread_id) { thread=&d; matches++; }
	if(matches!=1||!thread||!RDPrivateCanonical(*thread,"review-thread")||!RDVisible(app,catalog,*thread)) return unavailable;
	ValueMap view=Build(app,catalog,RDString(thread->data,"package_id"),RDString(thread->data,"revision_id"));
	if(RDString(view,"view_state")!="ready") return unavailable;
	ValueMap package=RDGet(view,"package");
	if(RDString(package,"workspace_id")!=thread->workspace_id||RDString(package,"patch_sha256")!=RDString(thread->data,"revision_hash")) return unavailable;
	ValueArray comments=RDArray(view,"comments"),matching;
	for(const Value& item:comments) if(RDString((ValueMap)item,"thread_id")==thread_id) matching.Add(item);
	view.Set("comments",matching); view.Add("thread_id",thread_id); return view;
}

ValueMap ReviewDiscussionService::AppendHumanRecord(const CatalogGeneration& catalog,ManagerSkylarkApp& app,const UserSession& session,
	const String& workspace,const String& package,const String& revision,const String& thread,const String& body,
	const String& type,const String& operation)
{
	ValueMap out; out.Add("status","denied"); const DataStore& store=catalog.data;
	if(catalog.status!="loaded"||catalog.data_status!="loaded") { out.Set("status","unavailable"); return out; }
	String normalized_body;
	if(!RDValidCommentBody(body,normalized_body)) { out.Set("status","invalid"); return out; }
	if(!session.is_authenticated||session.username.IsEmpty()||!SystemAuthService::AuthorizeWriteForSession(app,session,store.GetSourceId(),workspace)||
	   (type!="comment"&&type!="review-request")||operation.IsEmpty()||operation.GetCount()>160) return out;
	const String& accepted_body=normalized_body;
	int matches=0; const DataDocument *pkg=NULL;
	for(const DataDocument& d:store.GetDocuments()) if(d.kind=="review-package-revision"&&d.id==revision&&d.workspace_id==workspace&&RDString(d.data,"package_id")==package) { pkg=&d; matches++; }
	if(matches!=1||!pkg||!RDVisible(app,catalog,*pkg)) { out.Set("status","unavailable"); return out; }
	ValueMap pinned_view=ReviewPackageViewService::Build(app,catalog,package,revision);
	if(RDString(pinned_view,"view_state")!="ready") { out.Set("status","unavailable"); return out; }
	String hash=RDString(pkg->data,"patch_sha256"), tid=thread.IsEmpty()?"review-t-"+DataStore::ContentDigest(workspace+"\n"+package+"\n"+revision).Left(40):thread;
	if(tid!="review-t-"+DataStore::ContentDigest(workspace+"\n"+package+"\n"+revision).Left(40)) { out.Set("status","invalid"); return out; }
	String id="review-c-"+DataStore::ContentDigest(workspace+"\n"+package+"\n"+revision+"\n"+tid+"\n"+type+"\n"+session.username+"\n"+accepted_body).Left(48);
	for(const DataDocument& d:store.GetDocuments()) if(d.kind=="review-comment"&&d.qualified_id==workspace+":"+id) {
		if(RDString(d.data,"body")==accepted_body&&RDString(d.data,"author")==session.username&&RDString(d.data,"revision_hash")==hash) { out.Set("status","already_committed"); out.Add("comment_id",id); return out; }
		out.Set("status","conflict"); return out;
	}
	ValueMap thread_record; thread_record.Add("id",tid); thread_record.Add("kind","review-thread"); thread_record.Add("workspace_id",workspace); thread_record.Add("package_id",package); thread_record.Add("revision_id",revision); thread_record.Add("revision_hash",hash);
	ValueMap thread_env; thread_env.Add("schema_id","skylark/review-thread"); thread_env.Add("schema_version","1"); thread_env.Add("workspace_id",workspace); thread_env.Add("stable_id",tid); thread_env.Add("record_kind","review-thread"); thread_env.Add("revision","1"); thread_env.Add("visibility","private"); thread_env.Add("provenance_ref","authenticated-review-discussion"); thread_record.Add("envelope",thread_env);
	ValueMap record; record.Add("id",id); record.Add("kind","review-comment"); record.Add("workspace_id",workspace); record.Add("package_id",package); record.Add("revision_id",revision); record.Add("revision_hash",hash); record.Add("thread_id",tid); record.Add("record_type",type); record.Add("author_kind","human"); record.Add("author",session.username); record.Add("body",accepted_body); record.Add("created_at",(int64)GetSysTime().Get());
	ValueMap env; env.Add("schema_id","skylark/review-comment"); env.Add("schema_version","1"); env.Add("workspace_id",workspace); env.Add("stable_id",id); env.Add("record_kind","review-comment"); env.Add("revision","1"); env.Add("visibility","private"); env.Add("provenance_ref","authenticated-review-discussion"); record.Add("envelope",env);
	Vector<DataBatchChange> changes; bool thread_exists=false;
	for(const DataDocument& d:store.GetDocuments()) if(d.qualified_id==workspace+":"+tid) { if(d.kind!="review-thread"||AsJSON(d.data,true)!=AsJSON(thread_record,true)) { out.Set("status","conflict"); return out; } thread_exists=true; }
	if(!thread_exists) { DataBatchChange thread_change; thread_change.action=DATA_BATCH_CREATE; thread_change.qualified_id=workspace+":"+tid; thread_change.proposed=thread_record; changes.Add(thread_change); }
	DataBatchChange change; change.action=DATA_BATCH_CREATE; change.qualified_id=workspace+":"+id; change.proposed=record; changes.Add(change);
	ManagerSkylarkApp::BatchFinalizationResult result=SystemAuthService::FinalizeBatchForSession(app,session,store.GetSourceId(),workspace,operation,changes);
	out.Set("status",result.status=="idempotency_conflict"?String("conflict"):result.status);
	if(result.committed||result.status=="already_committed") out.Add("comment_id",id);
	return out;
}

String ReviewPackageViewService::Render(const ValueMap& view,bool detail)
{
	if(RDString(view,"view_state")!="ready") return "<main class=\"review-view\"><h1>Review data unavailable</h1><p>The requested review data is unavailable.</p></main>";
	String html; ValueArray rows=RDArray(view,detail?"comments":"packages");
	if(detail) {
		ValueMap p=RDGet(view,"package").Is<ValueMap>()?(ValueMap)RDGet(view,"package"):ValueMap();
		html<<"<main class=\"review-view\"><h1>Review package discussion</h1>";
		auto render_refs=[&](const ValueArray& refs,const String& label) { html<<"<h2>"<<EscapeHtml(label)<<"</h2><ul>"; for(const Value& value:refs) { ValueMap ref=value; if((bool)RDGet(ref,"unavailable")) html<<"<li>Related record unavailable</li>"; else if(!RDString(ref,"url").IsEmpty()) html<<"<li><a href=\""<<EscapeHtml(RDString(ref,"url"))<<"\">"<<EscapeHtml(RDString(ref,"title"))<<"</a></li>"; else { String anchor=RDString(ref,"anchor"); html<<"<li><a href=\"#"<<EscapeHtml(anchor)<<"\">Evidence: "<<EscapeHtml(RDString(ref,"title"))<<"</a></li><li id=\""<<EscapeHtml(anchor)<<"\"><strong>External evidence</strong>: item "<<EscapeHtml(RDString(ref,"id"))<<"; evidence revision "<<EscapeHtml(RDString(ref,"evidence_revision"))<<" (source "<<EscapeHtml(RDString(ref,"evidence_source_revision"))<<"); snapshot "<<EscapeHtml(RDString(ref,"snapshot_id"))<<" @ "<<EscapeHtml(RDString(ref,"snapshot_revision"))<<" (source "<<EscapeHtml(RDString(ref,"snapshot_source_revision"))<<"); upstream "<<EscapeHtml(RDString(ref,"upstream_revision"))<<"; locator "<<EscapeHtml(AsJSON(RDGet(ref,"locator"),true))<<"; excerpt "<<EscapeHtml(RDString(ref,"excerpt"))<<"</li>"; } } html<<"</ul>"; };
		if((bool)RDGet(p,"stale")) html<<"<p role=\"status\" class=\"review-stale\">This is a stale package revision. Discussion remains bound to this exact revision and is not an approval.</p>";
		html<<"<p>Package "<<EscapeHtml(RDString(p,"package_id"))<<"; revision "<<EscapeHtml(RDString(p,"id"))<<"; goal "<<EscapeHtml(RDString(p,"goal_id"))<<"; scope "<<EscapeHtml(RDString(p,"scope_id"))<<"; base "<<EscapeHtml(RDString(p,"base_revision"))<<"; head "<<EscapeHtml(RDString(p,"head_revision"))<<"</p><p>Author: "<<EscapeHtml(RDString(p,"author"))<<"; author state: "<<EscapeHtml(RDString(p,"author_state"))<<"; capture state: "<<EscapeHtml(RDString(p,"capture_state"))<<"</p>";
		render_refs(RDArray(p,"goal_links"),"Goal"); render_refs(RDArray(p,"topology_links"),"Topology and checks"); render_refs(RDArray(p,"evidence_items"),"Evidence");
		html<<"<p>Patch SHA-256: <code>"<<EscapeHtml(RDString(p,"patch_sha256"))<<"</code></p><h2>Selected paths</h2><pre>"<<EscapeHtml(AsJSON(RDGet(p,"selected_paths"),true))<<"</pre><h2>Check summaries</h2><ul>";
		for(const Value& item:RDArray(view,"check_summaries")) { ValueMap check=item; if((bool)RDGet(check,"unavailable")) html<<"<li>Related record unavailable</li>"; else html<<"<li><a href=\""<<EscapeHtml(RDString(check,"url"))<<"\">"<<EscapeHtml(RDString(check,"title"))<<"</a> ("<<EscapeHtml(RDString(check,"status"))<<")</li>"; }
		html<<"</ul><h2>Artifact hashes</h2><pre>"<<EscapeHtml(AsJSON(RDGet(p,"artifacts"),true))<<"</pre><h2>Patch</h2><pre class=\"review-patch\">"<<EscapeHtml(RDString(view,"patch_text"))<<"</pre><h2>Discussion</h2><ol aria-label=\"Thread comments\">";
		for(const Value& item:rows) { ValueMap c=item; String kind=RDString(c,"author_kind"); String label=kind=="human"?"Human comment":kind=="agent"?"Agent note":kind=="external"?"External evidence":"Unavailable author type"; html<<"<li><strong>"<<EscapeHtml(label)<<"</strong> <span>"<<EscapeHtml(RDString(c,"record_type"))<<( (bool)RDGet(c,"stale")?" (stale revision)":"")<<"</span>"; if(kind=="external") render_refs(RDArray(c,"evidence_items"),"Linked evidence"); else html<<"<p>"<<EscapeHtml(RDString(c,"author"))<<": "<<EscapeHtml(RDString(c,"body"))<<"</p>"; html<<"</li>"; }
		return html<<"</ol><p>Discussion is not approval. No decision is recorded here.</p></main>";
	}
	html<<"<main class=\"review-view\"><h1>Review packages</h1><p>Private, revision-pinned discussion. Comments do not approve changes.</p><ul>";
	auto overview_links=[&](const ValueArray& refs,const char *label) { for(const Value& value:refs) { ValueMap ref=value; html<<"<p>"<<EscapeHtml(String(label))<<": "; if((bool)RDGet(ref,"unavailable")) html<<"Related record unavailable"; else html<<"<a href=\""<<EscapeHtml(RDString(ref,"url"))<<"\">"<<EscapeHtml(RDString(ref,"title"))<<"</a>"; html<<"</p>"; } };
	for(const Value& item:rows) { ValueMap p=item; html<<"<li><a href=\"/review-package/"<<UrlEncode(RDString(p,"package_id"))<<"/"<<UrlEncode(RDString(p,"revision_id"))<<"\">"<<EscapeHtml(RDString(p,"package_id"))<<" @ "<<EscapeHtml(RDString(p,"revision_id"))<<"</a> <span>"<<EscapeHtml(RDString(p,"scope_id"))<<"; "<<EscapeHtml(AsString(RDGet(p,"comment_count")))<<" comments</span><p>"<<EscapeHtml(RDString(p,"base_revision"))<<" to "<<EscapeHtml(RDString(p,"head_revision"))<<"; patch SHA-256 "<<EscapeHtml(RDString(p,"patch_sha256"))<<"</p>"; overview_links(RDArray(p,"goal_links"),"Goal"); overview_links(RDArray(p,"topology_links"),"Topology"); overview_links(RDArray(p,"evidence_items"),"Evidence"); html<<"<h2>Selected paths</h2><pre>"<<EscapeHtml(AsJSON(RDGet(p,"selected_paths"),true))<<"</pre><p>Check summaries: "<<EscapeHtml(AsJSON(RDGet(p,"check_summaries"),true))<<"</p><p>Artifact hashes: "<<EscapeHtml(AsJSON(RDGet(p,"artifacts"),true))<<"</p></li>"; }
	return html<<"</ul></main>";
}

END_UPP_NAMESPACE
