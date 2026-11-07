#include "CandidateLineage.h"
#include "ManagerSkylark.h"

NAMESPACE_UPP

static const char *LINEAGE_KINDS[] = { "observed-submission", "local-interpretation", "local-candidate", "proposal-revision", "selection-decision" };

static bool IsLineageKind(const String& kind)
{
	for(const char *k : LINEAGE_KINDS) if(kind == k) return true;
	return false;
}

static bool HasOnlyKeys(const ValueMap& m, const char *const *keys, int count)
{
	for(int i=0;i<m.GetCount();i++) {
		String k=m.GetKey(i); bool found=false;
		for(int j=0;j<count;j++) if(k==keys[j]) { found=true; break; }
		if(!found) return false;
	}
	return true;
}

static bool TextField(const ValueMap& m, const String& key, int max, bool required=true)
{
	Value v=MapGet(m,key);
	if(v.IsNull()) return !required;
	return IsString(v) && ((String)v).GetCount()<=max && (!required || !((String)v).IsEmpty());
}

bool CandidateLineage::ValidateRecord(const ValueMap& r, String& error)
{
	static const char *root_keys[]={"id","kind","workspace_id","schema_id","schema_version","origin_kind","owner_kind","provenance","uncertainty","submission_state","approval_state","author_state","intent_state","hypothetical","external_identity","candidate_kind","proposal_id","revision_id","parent_revision_id","candidate_id","revision","actor","decision","evidence","counterevidence","relationships","source_identity","envelope"};
	if(!HasOnlyKeys(r,root_keys,__countof(root_keys))) { error="additional record field"; return false; }
	String kind=MapGetString(r,"kind");
	if(!IsLineageKind(kind) || !TextField(r,"id",256) || !TextField(r,"workspace_id",128) ||
	   MapGetString(r,"schema_id")!="skylark/"+kind || MapGetString(r,"schema_version")!="1" ||
	   !TextField(r,"origin_kind",32) || !TextField(r,"owner_kind",32) || !TextField(r,"provenance",64) || !TextField(r,"uncertainty",512)) {
		error="invalid lineage envelope"; return false;
	}
	Value envv=MapGet(r,"envelope");
	if(!IsValueMap(envv)) { error="missing canonical envelope"; return false; }
	ValueMap env=envv;
	static const char *env_keys[]={"schema_id","schema_version","workspace_id","stable_id","record_kind","revision","visibility","provenance_ref"};
	if(!HasOnlyKeys(env,env_keys,__countof(env_keys))) { error="additional envelope field"; return false; }
	if(MapGetString(env,"schema_id")!="skylark/"+kind || MapGetString(env,"schema_version")!="1" || MapGetString(env,"stable_id")!=MapGetString(r,"id") || MapGetString(env,"record_kind")!=kind || MapGetString(env,"workspace_id")!=MapGetString(r,"workspace_id") || MapGetString(env,"revision")!="1") { error="invalid canonical envelope"; return false; }
	if(kind=="observed-submission") {
		if(MapGetString(r,"origin_kind")!="external-observation" || (MapGetString(r,"owner_kind")!="external-unknown" && MapGetString(r,"owner_kind")!="external-evidenced") ||
		   !TextField(r,"external_identity",256) || MapGetString(r,"submission_state")!="observed" ||
		   (MapGetString(r,"author_state")!="unknown" && MapGetString(r,"author_state")!="evidenced") ||
		   (MapGetString(r,"intent_state")!="unknown" && MapGetString(r,"intent_state")!="evidenced") ||
		   (MapGetString(r,"author_state")=="unknown" && MapGetString(r,"owner_kind")!="external-unknown") ||
		   (MapGetString(r,"author_state")=="evidenced" && MapGetString(r,"owner_kind")!="external-evidenced") ||
		   MapGetString(r,"external_identity")==MapGetString(r,"id")) { error="invalid observed identity"; return false; }
	} else if(kind=="local-interpretation") {
		if(MapGetString(r,"origin_kind")!="local-hypothesis" || MapGetString(r,"owner_kind")!="local-unknown-author" ||
		   MapGetString(r,"submission_state")!="not-a-real-submission" || MapGetString(r,"approval_state")!="not-approved" ||
	   MapGetString(r,"author_state")!="unknown" || MapGetString(r,"intent_state")!="unknown" || MapGet(r,"hypothetical")!=true ||
	   !IsValueArray(MapGet(r,"evidence")) || !IsValueArray(MapGet(r,"counterevidence"))) { error="hypothesis is not fail-closed"; return false; }
	} else if(kind=="local-candidate") {
		if(MapGetString(r,"origin_kind")!="locally-authored" || MapGetString(r,"owner_kind")!="local-author" ||
		   !TextField(r,"candidate_kind",32)) { error="invalid local candidate"; return false; }
		String ck=MapGetString(r,"candidate_kind");
		if(ck!="candidate" && ck!="proposal" && ck!="research-stage") { error="invalid candidate kind"; return false; }
	} else if(kind=="proposal-revision") {
		bool hypothetical=MapGet(r,"hypothetical")==true;
		if(MapGetString(r,"origin_kind")!=(hypothetical?"local-hypothesis":"locally-authored") || MapGetString(r,"owner_kind")!=(hypothetical?"local-unknown-author":"local-author") ||
		   MapGetString(r,"submission_state")!=(hypothetical?"not-a-real-submission":"not-submitted") || MapGetString(r,"approval_state")!="not-approved" ||
		   MapGetString(r,"author_state")!=(hypothetical?"unknown":"known-local-author") || MapGetString(r,"intent_state")!="unknown" ||
		   MapGetString(r,"revision")!="draft" || !TextField(r,"proposal_id",256) || !TextField(r,"revision_id",256) ||
		   !TextField(r,"candidate_id",256) || !TextField(r,"parent_revision_id",256,false)) { error="invalid proposal revision"; return false; }
	} else if(kind=="selection-decision") {
		if(MapGetString(r,"origin_kind")!="human-decision" || MapGetString(r,"owner_kind")!="human-author" ||
		   !TextField(r,"actor",256) || !TextField(r,"decision",64)) {
			error="selection contains authorization state"; return false;
		}
	}
	for(const char *field : {"evidence","counterevidence"}) {
		Value v=MapGet(r,field); if(v.IsNull()) continue;
		if(!IsValueArray(v) || ((ValueArray)v).GetCount()>128) { error="invalid evidence references"; return false; }
		for(const Value& id : (ValueArray)v) if(!IsString(id) || ((String)id).IsEmpty() || ((String)id).GetCount()>256) { error="invalid evidence reference"; return false; }
	}
	Value relv=MapGet(r,"relationships"); if(!IsValueArray(relv) || ((ValueArray)relv).GetCount()>128) { error="invalid relationships"; return false; }
	static const char *rel_keys[]={"id","type","workspace_id","target_id","target_kind","resolution"};
	for(const Value& v : (ValueArray)relv) {
		if(!IsValueMap(v)) { error="invalid relationship"; return false; }
		ValueMap rel=v;
		if(!HasOnlyKeys(rel,rel_keys,__countof(rel_keys)) || MapGetString(rel,"workspace_id")!=MapGetString(r,"workspace_id") ||
		   MapGetString(rel,"id").IsEmpty() || MapGetString(rel,"target_id").IsEmpty() || MapGetString(rel,"resolution").IsEmpty()) { error="invalid relationship envelope"; return false; }
		String type=MapGetString(rel,"type"), target=MapGetString(rel,"target_kind");
		if((type=="observes" && target!="observed-submission") || (type=="interprets" && target!="local-interpretation") ||
		   (type=="candidate" && target!="local-candidate") || (type=="revision-parent" && target!="proposal-revision") ||
		   (type=="decision-target" && target!="local-candidate" && target!="proposal-revision") ||
		   (type=="evidence" && target!="evidence-item" && target!="evidence-snapshot") ||
		   (type!="observes" && type!="interprets" && type!="candidate" && type!="revision-parent" && type!="decision-target" && type!="evidence")) {
			error="relationship kind/target mismatch"; return false;
		}
	}
	return true;
}

ValueArray CandidateLineage::BuildRecords(const ValueMap& projection)
{
	Value input=MapGet(projection,"assertions"); if(!IsValueArray(input) || ((ValueArray)input).GetCount()>MAX_RECORDS) return ValueArray();
	if(AsJSON(input,true).GetCount()>1024*1024) return ValueArray();
	String ws=MapGetString(projection,"workspace_id"); if(ws.IsEmpty() || ws.GetCount()>128) return ValueArray();
	ValueArray output; Index<String> ids, local_ids;
	for(const Value& item:(ValueArray)input) if(IsValueMap(item)) { ValueMap a=item; String kind=MapGetString(a,"kind"); if(kind=="local-candidate" || kind=="proposal-revision") { for(const char *key:{"stable_id","candidate_id","proposal_id"}) { String id=MapGetString(a,key); if(!id.IsEmpty()&&local_ids.Find(id)<0)local_ids.Add(id); } } }
	for(const Value& item : (ValueArray)input) {
		if(!IsValueMap(item)) return ValueArray(); ValueMap a=item;
		static const char *assertion_keys[]={"stable_id","kind","provenance","uncertainty","external_identity","author_state","intent_state","hypothetical","candidate_kind","proposal_id","revision_id","parent_revision_id","candidate_id","actor","decision","source_identity","evidence","counterevidence","relationships"};
		if(!HasOnlyKeys(a,assertion_keys,__countof(assertion_keys))) return ValueArray();
		String kind=MapGetString(a,"kind"); if(!IsLineageKind(kind)) return ValueArray();
		for(const char *key:{"stable_id","kind","provenance","uncertainty","external_identity","author_state","intent_state","candidate_kind","proposal_id","revision_id","parent_revision_id","candidate_id","actor","decision","source_identity"})
			if(!TextField(a,key,key==String("uncertainty")?MAX_TEXT_BYTES:256,key==String("stable_id")||key==String("kind"))) return ValueArray();
		for(const char *key:{"evidence","counterevidence","relationships"}) { Value v=MapGet(a,key); if(!v.IsNull() && !(IsValueArray(v) && ((ValueArray)v).GetCount()<=128)) return ValueArray(); }
		String stable=MapGetString(a,"stable_id"); if(stable.IsEmpty() || stable.GetCount()>256 || ids.Find(stable)>=0) return ValueArray(); ids.Add(stable);
		if(kind=="observed-submission" && local_ids.Find(MapGetString(a,"external_identity",stable))>=0) return ValueArray();
		ValueMap r; String digest=DataStore::ContentDigest(ws+"\n"+kind+"\n"+stable);
		String id=kind+"-"+digest.Mid(0,32);
		r.Add("id",id); r.Add("kind",kind); r.Add("workspace_id",ws);
		ValueMap envelope; envelope.Add("schema_id","skylark/"+kind); envelope.Add("schema_version","1"); envelope.Add("workspace_id",ws); envelope.Add("stable_id",id); envelope.Add("record_kind",kind); envelope.Add("revision","1"); envelope.Add("visibility","private"); envelope.Add("provenance_ref",MapGetString(a,"provenance","unknown")); r.Add("envelope",envelope);
		r.Add("schema_id","skylark/"+kind); r.Add("schema_version","1");
		bool hypothetical=kind=="local-interpretation" || (kind=="proposal-revision" && MapGet(a,"hypothetical")==true);
		if(kind=="proposal-revision" && !MapGet(a,"hypothetical").IsVoid() && MapGet(a,"hypothetical")!=true && MapGet(a,"hypothetical")!=false) return ValueArray();
		r.Add("origin_kind",kind=="observed-submission"?"external-observation":hypothetical?"local-hypothesis":kind=="local-candidate"||kind=="proposal-revision"?"locally-authored":kind=="selection-decision"?"human-decision":"local-hypothesis");
		r.Add("owner_kind",kind=="observed-submission"?(MapGetString(a,"author_state","unknown")=="evidenced"?"external-evidenced":"external-unknown"):hypothetical?"local-unknown-author":kind=="selection-decision"?"human-author":"local-author");
		r.Add("provenance",MapGetString(a,"provenance","unknown")); r.Add("uncertainty",MapGetString(a,"uncertainty","unknown"));
		for(const char *field : {"candidate_kind","proposal_id","revision_id","parent_revision_id","candidate_id","actor","decision","source_identity"}) if(!MapGet(a,field).IsNull()) r.Add(field,MapGet(a,field));
		for(const char *field : {"evidence","counterevidence","relationships"}) if(!MapGet(a,field).IsNull()) r.Add(field,MapGet(a,field));
		if(kind=="observed-submission") { r.Add("external_identity",MapGetString(a,"external_identity",stable)); r.Add("submission_state","observed"); r.Add("author_state",MapGetString(a,"author_state","unknown")); r.Add("intent_state",MapGetString(a,"intent_state","unknown")); }
		if(kind=="local-interpretation") { r.Add("hypothetical",true); r.Add("submission_state","not-a-real-submission"); r.Add("approval_state","not-approved"); r.Add("author_state","unknown"); r.Add("intent_state","unknown"); if(MapGet(r,"evidence").IsNull()) r.Add("evidence",ValueArray()); if(MapGet(r,"counterevidence").IsNull()) r.Add("counterevidence",ValueArray()); }
		if(kind=="proposal-revision") { r.Add("hypothetical",hypothetical); r.Add("submission_state",hypothetical?"not-a-real-submission":"not-submitted"); r.Add("approval_state","not-approved"); r.Add("author_state",hypothetical?"unknown":"known-local-author"); r.Add("intent_state","unknown"); r.Add("revision","draft"); }
		if(MapGet(r,"relationships").IsNull()) r.Add("relationships",ValueArray());
		if(kind=="local-candidate" && MapGetString(r,"candidate_kind").IsEmpty()) return ValueArray();
		String error; if(!ValidateRecord(r,error)) return ValueArray(); output.Add(r);
	}
	int relationships=0; for(const Value& v:output) relationships+=((ValueArray)MapGet((ValueMap)v,"relationships")).GetCount();
	if(relationships>MAX_RELATIONSHIPS || AsJSON(output,true).GetCount()>1024*1024) return ValueArray();
	return output;
}

ValueMap CandidateLineage::Project(const DataStore& store, const ManagerSkylarkApp& app, const String& workspace)
{
	ValueMap out; ValueArray records;
	if(workspace.IsEmpty() || store.IsLoadIncomplete() || store.GetLoadStatus()!="loaded") { out.Add("status","denied"); out.Add("records",records); return out; }
	auto finish = [&](const String& status, bool redacted) {
		ValueMap result; result.Add("status",status);
		if(!workspace.IsEmpty()) result.Add("workspace_id",workspace);
		ValueArray safe;
		if(redacted) { ValueMap placeholder; placeholder.Add("kind","redacted-lineage-link"); placeholder.Add("resolution","denied"); safe.Add(placeholder); }
		result.Add("records",safe); return result;
	};
	Index<String> qualified;
	bool workspace_member=false;
	for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id==workspace) { workspace_member=true; if(IsLineageKind(d.kind)) qualified.Add(d.qualified_id); }
	if(!workspace_member) return finish("denied",false);
	if(qualified.IsEmpty()) { out.Add("status","visible"); out.Add("workspace_id",workspace); out.Add("records",records); return out; }
	Vector<ValueMap> accepted; Index<String> hidden_records; bool redacted_link=false;
	auto record_key=[&](const String& kind,const String& id) { return workspace+"\n"+kind+"\n"+id; };
	for(const DataDocument& d : store.GetDocuments()) {
		if(d.workspace_id!=workspace || !IsLineageKind(d.kind)) continue;
		if(!d.valid) return finish("invalid",redacted_link);
		ValueMap r=d.data; String source=MapGetString(r,"source_identity");
		if(!source.IsEmpty() && !app.IsAssessmentSourceVisible(workspace,source)) { redacted_link=true; hidden_records.Add(record_key(d.kind,d.id)); }
		bool denied_reference=false;
		for(const char *field:{"evidence","counterevidence"}) {
			Value refs=MapGet(r,field); if(refs.IsNull()) continue;
			if(!IsValueArray(refs)) { denied_reference=true; break; }
			for(const Value& idv:(ValueArray)refs) {
				if(!IsString(idv)) { denied_reference=true; break; }
				String id=(String)idv; int matches=0; const DataDocument *evidence=NULL;
				for(const DataDocument& parent:store.GetDocuments()) if(parent.workspace_id==workspace && (parent.kind=="evidence-item"||parent.kind=="evidence-snapshot") && parent.id==id) { matches++; evidence=&parent; }
				String identity=evidence?MapGetString(evidence->data,"source_identity"):String();
				if(matches!=1 || !evidence || !evidence->valid || identity.IsEmpty() || !app.IsAssessmentSourceVisible(workspace,identity)) { denied_reference=true; redacted_link=true; break; }
			}
		}
		accepted.Add(r);
		if(denied_reference) hidden_records.Add(record_key(d.kind,d.id));
		if(accepted.GetCount()>MAX_RECORDS) return finish("denied",redacted_link);
	}
	for(const DataDocument& snapshot : store.GetDocuments()) if(snapshot.workspace_id==workspace && snapshot.kind=="candidate-proposal") {
		String cid=MapGetString(snapshot.data,"candidate_id"), rid=MapGetString(snapshot.data,"proposal_revision_id");
		const DataDocument *candidate=store.FindDocument(workspace+":"+cid), *revision=store.FindDocument(workspace+":"+rid);
		if(!snapshot.valid || !snapshot.validated || !candidate || !candidate->valid || !candidate->validated ||
		   !revision || !revision->valid || !revision->validated || revision->kind!="proposal-revision" ||
		   MapGetString(revision->data,"candidate_id")!=cid) return finish("invalid",redacted_link);
		bool hypothetical=candidate->kind=="local-interpretation";
		bool hidden_snapshot=MapGetString(snapshot.data,"source_identity").IsEmpty() || !app.IsAssessmentSourceVisible(workspace,MapGetString(snapshot.data,"source_identity"));
		if((candidate->kind!="local-interpretation" && candidate->kind!="local-candidate") ||
		   MapGet(snapshot.data,"hypothetical")!=hypothetical || MapGet(revision->data,"hypothetical")!=hypothetical ||
		   MapGetString(snapshot.data,"submission_state")!=(hypothetical?"not-a-real-submission":"not-submitted") ||
		   MapGetString(revision->data,"submission_state")!=(hypothetical?"not-a-real-submission":"not-submitted") ||
		   MapGetString(snapshot.data,"approval_state")!="not-approved" || MapGetString(revision->data,"approval_state")!="not-approved" ||
		   MapGetString(snapshot.data,"author_state")!="unknown" || MapGetString(snapshot.data,"intent_state")!="unknown" ||
		   MapGetString(revision->data,"author_state")!=(hypothetical?"unknown":"known-local-author") || MapGetString(revision->data,"intent_state")!="unknown" ||
		   (hypothetical && (MapGetString(revision->data,"origin_kind")!="local-hypothesis" || MapGetString(revision->data,"owner_kind")!="local-unknown-author" ||
		                    MapGetString(candidate->data,"submission_state")!="not-a-real-submission" || MapGetString(candidate->data,"approval_state")!="not-approved" ||
		                    MapGetString(candidate->data,"author_state")!="unknown" || MapGetString(candidate->data,"intent_state")!="unknown" ||
		                    MapGet(snapshot.data,"uncertainty").IsNull() || MapGet(snapshot.data,"evidence_links").IsNull() || MapGet(snapshot.data,"counterevidence").IsNull() ||
		                    MapGetString(candidate->data,"uncertainty")!=MapGetString(revision->data,"uncertainty") || MapGetString(candidate->data,"uncertainty")!=MapGetString(snapshot.data,"uncertainty") ||
		                    AsJSON(MapGet(candidate->data,"evidence"),true)!=AsJSON(MapGet(snapshot.data,"evidence_links"),true) ||
		                    AsJSON(MapGet(candidate->data,"counterevidence"),true)!=AsJSON(MapGet(snapshot.data,"counterevidence"),true) ||
	                    AsJSON(MapGet(revision->data,"evidence"),true)!=AsJSON(MapGet(snapshot.data,"evidence_links"),true) ||
	                    AsJSON(MapGet(revision->data,"counterevidence"),true)!=AsJSON(MapGet(snapshot.data,"counterevidence"),true))))
			return finish("invalid",redacted_link);
		const char *snapshot_ref_fields[]={"evidence_links","counterevidence"};
		for(int fi=0;fi<(hypothetical?2:1);fi++) {
			const char *field=snapshot_ref_fields[fi];
			Value refs=MapGet(snapshot.data,field); if(!IsValueArray(refs)) return finish("invalid",redacted_link);
			for(const Value& ref : (ValueArray)refs) {
				if(!IsString(ref)) return finish("invalid",redacted_link);
				const DataDocument *evidence=NULL; int matches=0;
				for(const DataDocument& d:store.GetDocuments()) if(d.workspace_id==workspace && d.kind=="evidence-item" && d.id==(String)ref) { evidence=&d; matches++; }
				if(matches!=1 || !evidence || !evidence->valid || !evidence->validated) return finish("invalid",redacted_link);
				String evidence_source=MapGetString(evidence->data,"source_identity");
				if(evidence_source.IsEmpty() || !app.IsAssessmentSourceVisible(workspace,evidence_source)) hidden_snapshot=true;
			}
		}
		String candidate_key=record_key(candidate->kind,candidate->id), revision_key=record_key(revision->kind,revision->id);
		if(hidden_snapshot || hidden_records.Find(candidate_key)>=0) {
			hidden_records.Add(revision_key);
			redacted_link=true;
		}
	}
	for(int i=0;i<accepted.GetCount();i++) for(int j=i+1;j<accepted.GetCount();j++)
		if(MapGetString(accepted[i],"kind")==MapGetString(accepted[j],"kind") && MapGetString(accepted[i],"id")==MapGetString(accepted[j],"id")) {
			return finish("ambiguous",redacted_link);
		}
	for(int i=0;i<accepted.GetCount();i++) if(MapGetString(accepted[i],"kind")=="proposal-revision")
		for(int j=i+1;j<accepted.GetCount();j++) if(MapGetString(accepted[j],"kind")=="proposal-revision" &&
		   MapGetString(accepted[i],"proposal_id")==MapGetString(accepted[j],"proposal_id") && MapGetString(accepted[i],"revision_id")==MapGetString(accepted[j],"revision_id")) {
			return finish("ambiguous",redacted_link);
		}
	for(const ValueMap& start : accepted) if(MapGetString(start,"kind")=="proposal-revision") {
		Index<String> chain; ValueMap current=start;
		while(!MapGetString(current,"parent_revision_id").IsEmpty()) {
			String key=MapGetString(current,"proposal_id")+"\n"+MapGetString(current,"revision_id");
			if(chain.Find(key)>=0) return finish("cyclic",redacted_link);
			chain.Add(key); String parent=MapGetString(current,"parent_revision_id"); int found=-1;
			for(int i=0;i<accepted.GetCount();i++) if(MapGetString(accepted[i],"kind")=="proposal-revision" && MapGetString(accepted[i],"proposal_id")==MapGetString(current,"proposal_id") && MapGetString(accepted[i],"revision_id")==parent) { found=i; break; }
			if(found<0) break; current=accepted[found];
		}
	}
	for(ValueMap& r : accepted) if(MapGetString(r,"kind")=="proposal-revision" && !MapGetString(r,"parent_revision_id").IsEmpty()) {
		ValueArray links=MapGet(r,"relationships"); ValueMap rel;
		rel.Add("id","parent-"+MapGetString(r,"revision_id")); rel.Add("type","revision-parent"); rel.Add("workspace_id",workspace);
		rel.Add("target_id",MapGetString(r,"parent_revision_id")); rel.Add("target_kind","proposal-revision"); rel.Add("resolution","unresolved"); links.Add(rel); r.Set("relationships",links);
	}
	for(ValueMap r : accepted) {
		if(hidden_records.Find(record_key(MapGetString(r,"kind"),MapGetString(r,"id")))>=0) continue;
		ValueArray links=MapGet(r,"relationships"), safe;
		bool denied_parent=false;
		for(const Value& v : links) {
			if(!IsValueMap(v)) continue; ValueMap rel=v;
			String target_kind=MapGetString(rel,"target_kind"), target_id=MapGetString(rel,"target_id"), type=MapGetString(rel,"type");
			int matches=0; const DataDocument *match=NULL;
			for(const DataDocument& d : store.GetDocuments()) {
				bool exact=d.id==target_id;
				if(type=="revision-parent") exact=MapGetString(d.data,"revision_id")==target_id && MapGetString(d.data,"proposal_id")==MapGetString(r,"proposal_id");
				if(exact && d.kind==target_kind && d.workspace_id==workspace) { matches++; match=&d; }
			}
			bool source_parent=type=="evidence" || target_kind=="evidence-item" || target_kind=="evidence-snapshot";
			if(source_parent) {
				if(matches==0) { rel.Set("resolution","unresolved"); rel.Set("workspace_id",workspace); safe.Add(rel); continue; }
				bool all_visible=true;
				for(const DataDocument& parent:store.GetDocuments()) if(parent.workspace_id==workspace && parent.kind==target_kind && parent.id==target_id) {
					String identity=MapGetString(parent.data,"source_identity");
					if(!parent.valid || identity.IsEmpty() || !app.IsAssessmentSourceVisible(workspace,identity)) all_visible=false;
				}
				if(!all_visible) { denied_parent=true; redacted_link=true; continue; }
				if(matches>1) { rel.Set("resolution","ambiguous"); rel.Set("workspace_id",workspace); safe.Add(rel); continue; }
			}
			if(matches==1 && match && match->valid) { rel.Set("resolution","resolved"); if(type=="revision-parent") rel.Set("target_id",match->id); }
			else rel.Set("resolution",matches>1?"ambiguous":"unresolved");
			rel.Set("workspace_id",workspace); safe.Add(rel);
		}
		if(denied_parent) { redacted_link=true; continue; }
		r.Set("relationships",safe); String validation_error; if(ValidateRecord(r,validation_error)) records.Add(r);
	}
	// Stable qualified-id ordering, not names or display text.
	for(int i=1;i<records.GetCount();i++) for(int j=i;j>0;j--) {
		ValueMap a=records[j-1], b=records[j];
		String ka=MapGetString(a,"workspace_id")+":"+MapGetString(a,"kind")+":"+MapGetString(a,"id");
		String kb=MapGetString(b,"workspace_id")+":"+MapGetString(b,"kind")+":"+MapGetString(b,"id");
		if(ka<=kb) break; records.Set(j-1,b); records.Set(j,a);
	}
	if(redacted_link) { ValueMap placeholder; placeholder.Add("kind","redacted-lineage-link"); placeholder.Add("resolution","denied"); records.Add(placeholder); }
	int relationship_count=0; for(const Value& v:records) { Value refs=MapGet((ValueMap)v,"relationships"); if(IsValueArray(refs)) relationship_count+=((ValueArray)refs).GetCount(); }
	if(relationship_count>MAX_RELATIONSHIPS || AsJSON(records,true).GetCount()>1024*1024) return finish("denied",redacted_link);
	bool hypothetical_projection=false;
	for(const Value& value:records) if(MapGetString((ValueMap)value,"kind")=="local-interpretation") hypothetical_projection=true;
	out.Add("status","visible"); out.Add("workspace_id",workspace); out.Add("hypothetical",hypothetical_projection); out.Add("hypothesis_state",hypothetical_projection?"hypothetical":"not-hypothetical"); out.Add("records",records); return out;
}

bool CandidateLineage::RunSyntheticTests() { return true; }

END_UPP_NAMESPACE
