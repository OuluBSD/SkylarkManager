#include "JobService.h"

NAMESPACE_UPP

static String JS(const ValueMap& m,const char *k) { return MapGetString(m,k); }
static int64 JI(const ValueMap& m,const char *k) { Value v=MapGet(m,k); return v.Is<int64>()?(int64)v:v.Is<int>()?(int)v:v.Is<double>()?(int64)(double)v:0; }
static ValueMap JobBase(const String& kind,const String& id,const String& ws) {
	ValueMap m; m.Add("id",id); m.Add("kind",kind); m.Add("workspace_id",ws); m.Add("revision","1"); m.Add("provenance","computed");
	ValueMap env; env.Add("schema_id","skylark/"+kind); env.Add("schema_version","2"); env.Add("workspace_id",ws); env.Add("stable_id",id);
	env.Add("record_kind",kind); env.Add("revision","1"); env.Add("visibility","private"); env.Add("provenance_ref","computed"); m.Add("envelope",env);
	return m;
}
static const DataDocument *FindJob(const DataStore& s,const String& ws,const String& id) {
	const DataDocument *d=s.FindDocument(ws+":"+id); return d&&d->kind=="durable-job"&&d->workspace_id==ws&&d->valid&&d->validated?d:NULL;
}
static bool JobScope(const DataStore& s,const DataWriteAuthorization& a,const String& ws) {
	return s.GetLoadStatus()=="loaded"&&!s.IsLoadIncomplete()&&!s.GetSourceId().IsEmpty()&&
	       a.AllowsSource(ws,s.GetSourceId());
}
static String JobOperation(const String& label,const String& material) { return "job-"+label+"-"+DataStore::ContentDigest(material).Left(32); }
static ValueMap NewEvent(const String& ws,const String& job,const String& from,const String& to,int seq,int attempt,
                         const String& attempt_id,const String& key,const String& time,const String& reason) {
	String id="job-event-"+DataStore::ContentDigest(ws+"\n"+job+"\n"+AsString(seq)+"\n"+to).Left(32);
	ValueMap e=JobBase("job-event",id,ws); e.Add("job_id",job); e.Add("sequence",seq); e.Add("from_state",from); e.Add("to_state",to);
	e.Add("attempt",attempt); e.Add("attempt_id",attempt_id); e.Add("operation_key",key); e.Add("timestamp",time); e.Add("reason",reason); return e;
}
static DataBatchChange CreateJobChange(const String& ws,const ValueMap& m) {
	DataBatchChange c; c.action=DATA_BATCH_CREATE; c.qualified_id=ws+":"+JS(m,"id"); c.proposed=m; return c;
}
static DataBatchChange UpdateChange(const DataDocument& old,const ValueMap& m) {
	DataBatchChange c; c.action=DATA_BATCH_UPDATE; c.qualified_id=old.qualified_id; c.expected_revision=old.revision;
	c.expected_source_revision=old.source_revision; c.proposed=m; return c;
}
static JobService::Result CommitJob(ManagerSkylarkApp& app,const DataWriteAuthorization& auth,const String& op,
                                    const DataDocument& old,const ValueMap& job,const ValueMap& event,
                                    const ValueMap *run=NULL) {
	JobService::Result out; out.job_id=JS(job,"id"); Vector<DataBatchChange> changes;
	changes.Add(UpdateChange(old,job)); changes.Add(CreateJobChange(JS(job,"workspace_id"),event));
	if(run) changes.Add(CreateJobChange(JS(job,"workspace_id"),*run));
	ManagerSkylarkApp::BatchFinalizationResult r=app.FinalizeBatch(auth,op,changes); out.status=r.status; return out;
}
static bool BoundedValue(const Value& value,int depth,int& nodes,int64& bytes) {
	if(depth>8||++nodes>512) return false;
	if(IsString(value)) { String s=value; bytes+=s.GetCount(); return s.GetCount()<=8192&&bytes<=JobService::MAX_REQUEST_BYTES; }
	if(value.Is<int>()||value.Is<int64>()||value.Is<bool>()||value.IsNull()) { bytes+=16; return bytes<=JobService::MAX_REQUEST_BYTES; }
	if(value.Is<ValueMap>()) {
		ValueMap m=value; if(m.GetCount()>32) return false;
		for(int i=0;i<m.GetCount();i++) { if(!IsString(m.GetKey(i)))return false; String key=m.GetKey(i); if(key.IsEmpty()||key.GetCount()>64)return false; bytes+=key.GetCount(); if(!BoundedValue(m.GetValue(i),depth+1,nodes,bytes))return false; }
		return true;
	}
	if(value.Is<ValueArray>()) { ValueArray a=value; if(a.GetCount()>JobService::MAX_INPUTS)return false; for(const Value& v:a)if(!BoundedValue(v,depth+1,nodes,bytes))return false; return true; }
	return false;
}
static bool ExactKeys(const ValueMap& map,std::initializer_list<const char *> keys) {
	if(map.GetCount()!=(int)keys.size())return false;
	for(const char *key:keys)if(map.Find(key)<0)return false;
	return true;
}
static bool ExactInteger(const ValueMap& map,const char *key) { Value v=MapGet(map,key); return v.Is<int>()||v.Is<int64>(); }
static bool BoundedRequest(const ValueMap& m) {
	int nodes=0; int64 bytes=0;
	return ExactKeys(m,{"inputs","budget_ms","max_attempts","max_output_bytes","provider_id","model_config_revision"})&&
	       ExactInteger(m,"budget_ms")&&ExactInteger(m,"max_attempts")&&ExactInteger(m,"max_output_bytes")&&
	       IsString(MapGet(m,"provider_id"))&&!MapGetString(m,"provider_id").IsEmpty()&&MapGetString(m,"provider_id").GetCount()<=256&&
	       IsString(MapGet(m,"model_config_revision"))&&!MapGetString(m,"model_config_revision").IsEmpty()&&MapGetString(m,"model_config_revision").GetCount()<=128&&
	       BoundedValue(m,0,nodes,bytes)&&AsJSON(m,true).GetCount()<=JobService::MAX_REQUEST_BYTES;
}
static bool TrustedInputKind(const String& kind) {
	static const char *allowed[]={"paper","paper-analysis","task-proposal","research-candidate","topology","goal","issue","release","evidence-snapshot","evidence-item","analysis-run","assessment-finding","category-assessment","candidate-assessment","observed-submission","local-interpretation","local-candidate","proposal-revision","selection-decision","candidate-proposal","candidate-stage-event","candidate-stage-head","candidate-proposal-head"};
	for(const char *value:allowed) if(kind==value)return true;
	return false;
}
static bool PinString(const ValueMap& pin,const char *name,String& value,int limit) {
	Value v=MapGet(pin,name); if(!IsString(v))return false; value=v; return !value.IsEmpty()&&value.GetCount()<=limit;
}
static bool ValidInputPins(const DataStore& store,const String& ws,const ValueArray& pins) {
	if(pins.GetCount()>JobService::MAX_INPUTS)return false;
	Index<String> ids;
	for(const Value& value:pins) {
		if(!value.Is<ValueMap>())return false; ValueMap pin=value;
		if(!ExactKeys(pin,{"workspace_id","id","kind","revision","source_id"}))return false;
		String pin_ws,id,kind,rev,source;
		if(!PinString(pin,"workspace_id",pin_ws,128)||!PinString(pin,"id",id,128)||!PinString(pin,"kind",kind,64)||
		   !PinString(pin,"revision",rev,128)||!PinString(pin,"source_id",source,128)||pin_ws!=ws||source!=store.GetSourceId()||
		   !TrustedInputKind(kind)||ids.Find(id)>=0)return false;
		String pattern_error; if(!DataStore::MatchIdPattern(id,"^[A-Za-z0-9][A-Za-z0-9._/-]*$",pattern_error))return false;
		ids.Add(id); const DataDocument *d=store.FindDocument(ws+":"+id);
		if(!d||!d->valid||!d->validated||d->workspace_id!=ws||d->source_id!=source||d->kind!=kind||d->revision!=rev)return false;
	}
	return true;
}

static bool JobExactKeys(const ValueMap& map,std::initializer_list<const char *> keys) {
	if(map.GetCount()!=(int)keys.size()) return false;
	for(const char *key:keys) if(map.Find(key)<0) return false;
	return true;
}

static const DataDocument *ResolveJobReference(const DataStore& store,const String& ws,const String& id,
                                               const String& kind,const String& revision) {
	if(id.IsEmpty()||id.GetCount()>256) return NULL;
	const DataDocument *found=NULL; int matches=0;
	for(const DataDocument& doc:store.GetDocuments()) if(doc.id==id&&doc.workspace_id==ws) { found=&doc; matches++; }
	if(matches!=1||!found||!found->valid||!found->validated||found->workspace_id!=ws||
	   (!kind.IsEmpty()&&found->kind!=kind)||(!revision.IsEmpty()&&found->revision!=revision)) return NULL;
	return found;
}

static bool VisitJobSourceRecord(const DataStore& store,const String& ws,const DataDocument& root,
                                 Index<String>& visiting,Index<String>& visited,Index<String>& source_ids,int& nodes,int depth);

static bool AddJobSourceIdentity(Index<String>& source_ids,const String& identity) {
	if(identity.IsEmpty()||identity.GetCount()>256) return false;
	if(source_ids.Find(identity)<0) source_ids.Add(identity);
	return source_ids.GetCount()<=32;
}

static bool VisitJobReference(const DataStore& store,const String& ws,const String& id,const String& kind,const String& revision,
                              Index<String>& visiting,Index<String>& visited,Index<String>& source_ids,int& nodes,int depth) {
	const DataDocument *target=ResolveJobReference(store,ws,id,kind,revision);
	return target&&VisitJobSourceRecord(store,ws,*target,visiting,visited,source_ids,nodes,depth+1);
}

static bool AddJobIdentityArray(const Value& value,Index<String>& source_ids) {
	if(value.IsNull()) return true;
	if(!value.Is<ValueArray>()) return false;
	ValueArray ids=value; if(ids.GetCount()>256) return false;
	for(const Value& id:ids) if(!IsString(id)||!AddJobSourceIdentity(source_ids,(String)id)) return false;
	return true;
}

static bool VisitJobEvidenceArray(const DataStore& store,const String& ws,const Value& value,Index<String>& visiting,
                                 Index<String>& visited,Index<String>& source_ids,int& nodes,int depth) {
	if(value.IsNull()) return true;
	if(!value.Is<ValueArray>()) return false;
	ValueArray refs=value; if(refs.GetCount()>256) return false;
	for(const Value& ref:refs) {
		String id,revision;
		if(IsString(ref)) id=ref;
		else if(ref.Is<ValueMap>()) {
			ValueMap typed=ref;
			if(!JobExactKeys(typed,{"workspace_id","id","kind","revision"})||MapGetString(typed,"workspace_id")!=ws||
			   !IsString(MapGet(typed,"id"))||!IsString(MapGet(typed,"kind"))||MapGetString(typed,"kind")!="evidence-item"||!IsString(MapGet(typed,"revision"))) return false;
			id=MapGetString(typed,"id"); revision=MapGetString(typed,"revision");
			if(revision.IsEmpty()) return false;
		}
		else return false;
		if(!VisitJobReference(store,ws,id,"evidence-item",revision,visiting,visited,source_ids,nodes,depth)) return false;
	}
	return true;
}

static bool VisitJobNestedAssessment(const DataStore& store,const String& ws,const Value& value,Index<String>& visiting,
                                    Index<String>& visited,Index<String>& source_ids,int& nodes,int depth) {
	if(depth>32||++nodes>256) return false;
	if(value.IsNull()) return true;
	if(value.Is<ValueArray>()) {
		ValueArray items=value; if(items.GetCount()>256) return false;
		for(const Value& item:items) if(!VisitJobNestedAssessment(store,ws,item,visiting,visited,source_ids,nodes,depth+1)) return false;
		return true;
	}
	if(!value.Is<ValueMap>()) return false;
	ValueMap map=value;
	for(const char *field:{"evidence_ids","supporting_evidence_ids","counterevidence_ids"})
		if(map.Find(field)>=0&&!VisitJobEvidenceArray(store,ws,MapGet(map,field),visiting,visited,source_ids,nodes,depth)) return false;
	for(const char *field:{"contributors","source_ids"})
		if(map.Find(field)>=0&&!AddJobIdentityArray(MapGet(map,field),source_ids)) return false;
	Value dimensions=MapGet(map,"dimensions");
	return dimensions.IsNull()||VisitJobNestedAssessment(store,ws,dimensions,visiting,visited,source_ids,nodes,depth+1);
}

static bool VisitJobSourceRecord(const DataStore& store,const String& ws,const DataDocument& root,
                                 Index<String>& visiting,Index<String>& visited,Index<String>& source_ids,int& nodes,int depth) {
	if(depth>32||++nodes>256||!root.valid||!root.validated||root.workspace_id!=ws) return false;
	String record_key=root.kind+"\n"+root.id;
	if(visiting.Find(record_key)>=0) return false;
	if(visited.Find(record_key)>=0) return true;
	visiting.Add(record_key);
	Value identity=MapGet(root.data,"source_identity");
	if(!identity.IsNull()&&(!IsString(identity)||!AddJobSourceIdentity(source_ids,(String)identity))) return false;
	for(const char *field:{"source_snapshot_id","snapshot"}) {
		Value ref=MapGet(root.data,field); if(ref.IsNull()) continue;
		String id,revision;
		if(IsString(ref)) {
			id=ref;
			revision=MapGetString(root.data,String(field)=="snapshot"?"snapshot_revision":"source_snapshot_revision");
			if(revision.IsEmpty()) return false;
		}
		else if(ref.Is<ValueMap>()) {
			ValueMap typed=ref;
			if(!JobExactKeys(typed,{"workspace_id","id","kind","revision"})||MapGetString(typed,"workspace_id")!=ws||
			   MapGetString(typed,"kind")!="evidence-snapshot"||!IsString(MapGet(typed,"id"))||!IsString(MapGet(typed,"revision"))) return false;
			id=MapGetString(typed,"id"); revision=MapGetString(typed,"revision");
			if(revision.IsEmpty()) return false;
		}
		else return false;
		const DataDocument *target=ResolveJobReference(store,ws,id,"evidence-snapshot",revision);
		Value root_upstream=MapGet(root.data,"upstream_revision");
		if(!target||(!root_upstream.IsNull()&&(String)root_upstream!=(String)MapGet(target->data,"upstream_revision"))||
		   !VisitJobSourceRecord(store,ws,*target,visiting,visited,source_ids,nodes,depth+1)) return false;
	}
	for(const char *field:{"evidence_ids","supporting_evidence_ids","counterevidence_ids"})
		if(root.data.Find(field)>=0&&!VisitJobEvidenceArray(store,ws,MapGet(root.data,field),visiting,visited,source_ids,nodes,depth)) return false;
	for(const char *field:{"contributors","source_ids"})
		if(root.data.Find(field)>=0&&!AddJobIdentityArray(MapGet(root.data,field),source_ids)) return false;
	Value analysis=MapGet(root.data,"analysis_run");
	if(!analysis.IsNull()) {
		String id,revision;
		if(IsString(analysis)) id=analysis;
		else if(analysis.Is<ValueMap>()) {
			ValueMap typed=analysis;
			if(!JobExactKeys(typed,{"workspace_id","id","kind","revision"})||MapGetString(typed,"workspace_id")!=ws||MapGetString(typed,"kind")!="analysis-run"||
			   !IsString(MapGet(typed,"id"))||!IsString(MapGet(typed,"revision"))) return false;
			id=MapGetString(typed,"id"); revision=MapGetString(typed,"revision");
			if(revision.IsEmpty()) return false;
		}
		else return false;
		if(!VisitJobReference(store,ws,id,"analysis-run",revision,visiting,visited,source_ids,nodes,depth)) return false;
	}
	Value input_pins=MapGet(root.data,"input_revisions");
	if(!input_pins.IsNull()) {
		if(!input_pins.Is<ValueArray>()) return false;
		ValueArray refs=input_pins; if(refs.GetCount()>256) return false;
		for(const Value& value:refs) {
			if(!value.Is<ValueMap>()) return false; ValueMap pin=value;
			if(!JobExactKeys(pin,{"workspace_id","id","revision"})||MapGetString(pin,"workspace_id")!=ws||
			   !IsString(MapGet(pin,"id"))||!IsString(MapGet(pin,"revision"))||MapGetString(pin,"revision").IsEmpty()||
			   !VisitJobReference(store,ws,MapGetString(pin,"id"),String(),MapGetString(pin,"revision"),visiting,visited,source_ids,nodes,depth)) return false;
		}
	}
	Value relationships=MapGet(root.data,"relationships");
	if(!relationships.IsNull()&&root.kind=="analysis-run") {
		if(!relationships.Is<ValueArray>()) return false;
		ValueArray refs=relationships; if(refs.GetCount()>256) return false;
		for(const Value& value:refs) {
			if(!value.Is<ValueMap>()) return false; ValueMap rel=value;
			if(!JobExactKeys(rel,{"workspace_id","left","left_kind","right","right_kind","type"})||MapGetString(rel,"workspace_id")!=ws||
			   !IsString(MapGet(rel,"left"))||!IsString(MapGet(rel,"left_kind"))||!IsString(MapGet(rel,"right"))||!IsString(MapGet(rel,"right_kind"))||!IsString(MapGet(rel,"type"))) return false;
			for(const char *side:{"left","right"}) if(MapGetString(rel,String(side)+"_kind")=="source"&&
			   !AddJobSourceIdentity(source_ids,MapGetString(rel,side))) return false;
		}
	}
	Value provenance=MapGet(root.data,"source_provenance");
	if(!provenance.IsNull()) {
		if(root.kind!="analysis-run"||!provenance.Is<ValueArray>()) return false;
		ValueArray refs=provenance; if(refs.GetCount()>256) return false;
		for(const Value& value:refs) {
			if(!value.Is<ValueMap>()) return false; ValueMap pin=value;
			String identity,revision;
			if(!JobExactKeys(pin,{"workspace_id","identity","revision"})||MapGetString(pin,"workspace_id")!=ws||
			   !IsString(MapGet(pin,"identity"))||!IsString(MapGet(pin,"revision"))) return false;
			identity=MapGetString(pin,"identity"); revision=MapGetString(pin,"revision");
			if(revision.IsEmpty()) return false;
			if(!AddJobSourceIdentity(source_ids,identity)) return false;
			const DataDocument *source_record=NULL; int matches=0;
			for(const DataDocument& doc:store.GetDocuments()) if(doc.workspace_id==ws&&doc.valid&&doc.validated&&MapGetString(doc.data,"upstream_revision")==revision&&MapGetString(doc.data,"source_identity")==identity) { source_record=&doc; matches++; }
			if(matches!=1||!source_record||!VisitJobSourceRecord(store,ws,*source_record,visiting,visited,source_ids,nodes,depth+1)) return false;
		}
	}
	for(const char *field:{"finding","assessment"}) {
		Value nested=MapGet(root.data,field);
		if(!nested.IsNull()&&!VisitJobNestedAssessment(store,ws,nested,visiting,visited,source_ids,nodes,depth+1)) return false;
	}
	visiting.Remove(visiting.GetCount()-1); visited.Add(record_key); return true;
}

bool JobService::ResolveJobPolicy(ManagerSkylarkApp& app,const DataStore& store,const String& ws,const ValueArray& pins,
                                  const String& provider,const String& requested_revision,ValueArray& contributors,
                                  String& trusted_revision,ValueMap& snapshot) {
	Index<String> source_ids,visiting,visited; int nodes=0;
	for(const Value& v:pins) {
		ValueMap pin=v;
		const DataDocument *doc=ResolveJobReference(store,ws,MapGetString(pin,"id"),MapGetString(pin,"kind"),MapGetString(pin,"revision"));
		if(!doc||!VisitJobSourceRecord(store,ws,*doc,visiting,visited,source_ids,nodes,0)) return false;
	}
	if(source_ids.IsEmpty()||source_ids.GetCount()>32) return false;
	ValueArray decisions; String material=ws+"\n"+provider;
	for(const String& source_id:source_ids) {
		PolicyInput input; input.workspace.workspace_id=ws; input.workspace.object_id=ws; input.provider.workspace_id=ws; input.provider.object_id=provider; input.action=POLICY_ACTION_MODEL;
		PolicyIdentity source; source.workspace_id=ws; source.object_id=source_id; input.sources.Add(source);
		EffectivePolicy p=app.ResolveEffectivePolicy(input);
		if(p.status!="evaluated"||!p.external_model_allowed||!p.source_visible||p.classification=="unknown"||p.classification=="private"||
		   p.provider_config_revision.IsEmpty()||p.provider_config_revision!=requested_revision) return false;
		if(trusted_revision.IsEmpty()) trusted_revision=p.provider_config_revision;
		if(trusted_revision!=p.provider_config_revision) return false;
		contributors.Add(source_id); ValueMap redacted=p.ToRedactedValue(); decisions.Add(redacted); material+="\n"+source_id+"\n"+AsJSON(redacted,true);
	}
	material+="\n"+trusted_revision;
	snapshot.Add("decisions",decisions); snapshot.Add("fingerprint",DataStore::ContentDigest(material));
	snapshot.Add("secret_state",MapGetString((ValueMap)decisions[0],"secret_state"));
	return true;
}

bool JobService::IsProviderConfigCurrent(const ManagerSkylarkApp& app,const String& ws,const String& provider,
                                         const Vector<String>& identities,const String& expected_revision) {
	if(ws.IsEmpty()||ws.GetCount()>128||provider.IsEmpty()||provider.GetCount()>256||expected_revision.IsEmpty()||expected_revision.GetCount()>128||identities.IsEmpty()||identities.GetCount()>32) return false;
	Index<String> unique;
	for(const String& identity:identities) {
		if(identity.IsEmpty()||identity.GetCount()>256) return false;
		if(unique.Find(identity)>=0) continue;
		unique.Add(identity);
		PolicyInput input; input.workspace.workspace_id=ws; input.workspace.object_id=ws; input.provider.workspace_id=ws; input.provider.object_id=provider; input.action=POLICY_ACTION_MODEL;
		PolicyIdentity source; source.workspace_id=ws; source.object_id=identity; input.sources.Add(source);
		EffectivePolicy policy=app.ResolveEffectivePolicy(input);
		if(policy.status!="evaluated"||!policy.source_visible||!policy.external_model_allowed||policy.provider_config_revision!=expected_revision) return false;
	}
	return !unique.IsEmpty();
}

JobService::Result JobService::Submit(ManagerSkylarkApp& app,const DataStore& pinned,const DataWriteAuthorization& auth,const String& ws,
                                      const String& key,const ValueMap& request,const String& timestamp) {
	Result out; out.status="denied";
	if(!JobScope(pinned,auth,ws)||key.IsEmpty()||key.GetCount()>128||!BoundedRequest(request)) return out;
	Value input_value=MapGet(request,"inputs"); if(!input_value.Is<ValueArray>()) { out.status="invalid_request"; return out; }
	ValueArray inputs=input_value; if(inputs.GetCount()>MAX_INPUTS) { out.status="invalid_request"; return out; }
	if(!ValidInputPins(pinned,ws,inputs)) { out.status="stale_input"; return out; }
	ValueArray contributors; ValueMap policy; String trusted_revision;
	if(!ResolveJobPolicy(app,pinned,ws,inputs,MapGetString(request,"provider_id"),MapGetString(request,"model_config_revision"),contributors,trusted_revision,policy)) { out.status="policy_denied"; return out; }
	String digest=DataStore::ContentDigest(AsJSON(request,true)); String id="durable-job-"+DataStore::ContentDigest(ws+"\n"+key).Left(32); out.job_id=id;
	for(const DataDocument& d:pinned.GetDocuments()) if(d.workspace_id==ws&&d.kind=="durable-job"&&JS(d.data,"operation_key")==key) {
		out.status=JS(d.data,"request_digest")==digest?"already_submitted":"idempotency_conflict"; return out;
	}
	ValueMap job=JobBase("durable-job",id,ws); job.Add("state","queued"); job.Add("request_id",digest.Left(32)); job.Add("request",request); job.Add("operation_key",key); job.Add("request_digest",digest);
	job.Add("inputs",inputs); job.Add("contributors",contributors); job.Add("provider_id",MapGetString(request,"provider_id")); job.Add("model_config_revision",trusted_revision); job.Add("policy_snapshot",policy); job.Add("attempt",0); job.Add("attempt_id",String()); job.Add("created_at",timestamp); job.Add("updated_at",timestamp); job.Add("started_at",String()); job.Add("finished_at",String());
	int64 budget=JI(request,"budget_ms"); int64 attempts=JI(request,"max_attempts"), output=JI(request,"max_output_bytes");
	if(budget<1||budget>3600000||attempts<1||attempts>MAX_ATTEMPTS||output<1||output>MAX_OUTPUT_BYTES) { out.status="invalid_budget"; return out; }
	job.Add("budget_ms",budget); job.Add("max_attempts",attempts); job.Add("max_output_bytes",output); job.Add("output_ref",String()); job.Add("output_hash",String()); job.Add("failure_class",String()); job.Add("retryable",false); job.Add("idempotency_safe",false);
	ValueArray evs,runs; ValueMap event=NewEvent(ws,id,"none","queued",0,0,"",key,timestamp,"submitted"); evs.Add(JS(event,"id")); job.Add("event_ids",evs); job.Add("run_ids",runs);
	Vector<DataBatchChange> changes; changes.Add(CreateJobChange(ws,job)); changes.Add(CreateJobChange(ws,event));
	ManagerSkylarkApp::BatchFinalizationResult r=app.FinalizeBatch(auth,JobOperation("submit",ws+"\n"+key+"\n"+digest),changes); out.status=r.status; return out;
}

JobService::Result JobService::RunOne(ManagerSkylarkApp& app,const DataStore& pinned,const DataWriteAuthorization& auth,const String& ws,const String& id,
                                     const String& expected,const String& timestamp,JobExecutor& executor) {
	return RunOneInternal(app,pinned,auth,ws,id,expected,timestamp,executor,NULL,NULL);
}

JobService::Result JobService::RunOneForTests(ManagerSkylarkApp& app,const DataStore& pinned,const DataWriteAuthorization& auth,const String& ws,const String& id,
                                              const String& expected,const String& timestamp,JobExecutor& executor,
                                              void (*before_dispatch_recheck)(void *),void *context) {
	return RunOneInternal(app,pinned,auth,ws,id,expected,timestamp,executor,before_dispatch_recheck,context);
}

static JobService::Result PersistPolicyDeniedRun(ManagerSkylarkApp& app,const DataWriteAuthorization& auth,const String& ws,
                                                 const String& id,const String& attempt_id,const String& timestamp) {
	JobService::Result out; out.job_id=id; out.status="policy_denial_persist_failed";
	CatalogReadHandle latest=app.PinLatestCatalog();
	if(!latest||!latest->IsComplete()||latest->data.GetLoadStatus()!="loaded") return out;
	const DataDocument *running=FindJob(latest->data,ws,id);
	if(!running||JS(running->data,"state")!="running"||JS(running->data,"attempt_id")!=attempt_id) return out;
	ValueMap job=running->data; job.Set("state","failed"); job.Set("failure_class","policy_denied");
	job.Set("retryable",false); job.Set("idempotency_safe",false); job.Set("output_ref",String()); job.Set("output_hash",String());
	job.Set("updated_at",timestamp); job.Set("finished_at",timestamp);
	ValueArray events=MapGet(job,"event_ids");
	ValueMap event=NewEvent(ws,id,"running","failed",events.GetCount(),(int)JI(job,"attempt"),attempt_id,JS(job,"operation_key"),timestamp,"policy_denied");
	events.Add(JS(event,"id")); job.Set("event_ids",events);
	ValueMap run=JobBase("job-run","job-run-"+DataStore::ContentDigest(id+attempt_id+"policy-denied").Left(32),ws);
	run.Add("job_id",id); run.Add("attempt",JI(job,"attempt")); run.Add("attempt_id",attempt_id); run.Add("state","failed");
	run.Add("started_at",JS(job,"started_at")); run.Add("finished_at",timestamp); run.Add("elapsed_ms",0);
	run.Add("output_ref",String()); run.Add("output_hash",String()); run.Add("output_bytes",0); run.Add("inputs",MapGet(job,"inputs"));
	run.Add("failure_class","policy_denied"); run.Add("retryable",false); run.Add("idempotency_safe",false); run.Add("output",String());
	ValueArray runs=MapGet(job,"run_ids"); runs.Add(JS(run,"id")); job.Set("run_ids",runs);
	job.Set("envelope",MapGet(running->data,"envelope"));
	JobService::Result committed=CommitJob(app,auth,JobOperation("policy-denied",id+attempt_id),*running,job,event,&run);
	if(committed.status=="committed"||committed.status=="already_committed") out.status="policy_denied";
	return out;
}

JobService::Result JobService::RunOneInternal(ManagerSkylarkApp& app,const DataStore& pinned,const DataWriteAuthorization& auth,const String& ws,const String& id,
                                              const String& expected,const String& timestamp,JobExecutor& executor,
                                              void (*before_dispatch_recheck)(void *),void *context) {
	Result out; out.status="denied"; if(!JobScope(pinned,auth,ws)) return out;
	const DataDocument *old=FindJob(pinned,ws,id); if(!old) { out.status="not_found"; return out; } out.job_id=id;
	if(old->revision!=expected) { out.status="stale"; return out; } if(JS(old->data,"state")!="queued") { out.status="invalid_transition"; return out; }
	CatalogReadHandle before_run=app.PinLatestCatalog();
	if(!before_run||!before_run->IsComplete()||before_run->data.GetLoadStatus()!="loaded"||before_run->data.GetSourceId()!=pinned.GetSourceId()) { out.status="catalog_unavailable"; return out; }
	ValueArray pins=MapGet(old->data,"inputs"); if(!ValidInputPins(before_run->data,ws,pins)) { out.status="stale_input"; return out; }
	ValueArray contributors; ValueMap fresh_policy; String trusted_revision;
	if(!ResolveJobPolicy(app,before_run->data,ws,pins,JS(old->data,"provider_id"),JS(old->data,"model_config_revision"),contributors,trusted_revision,fresh_policy)) { out.status="policy_denied"; return out; }
	if(AsJSON(contributors,true)!=AsJSON(MapGet(old->data,"contributors"),true)) { out.status="stale_input"; return out; }
	const DataDocument *current_job=FindJob(before_run->data,ws,id);
	if(!current_job||current_job->revision!=expected||JS(current_job->data,"state")!="queued") { out.status="stale"; return out; }
	old=current_job;
	ValueMap job=old->data; int attempt=(int)JI(job,"attempt")+1; if(attempt>JI(job,"max_attempts")) { out.status="attempt_limit"; return out; }
	job.Set("policy_snapshot",fresh_policy);
	String aid="job-attempt-"+DataStore::ContentDigest(id+"\n"+AsString(attempt)).Left(32); job.Set("state","running"); job.Set("attempt",attempt); job.Set("attempt_id",aid); job.Set("started_at",timestamp); job.Set("updated_at",timestamp);
	ValueArray evs=MapGet(job,"event_ids"); ValueMap started=NewEvent(ws,id,"queued","running",evs.GetCount(),attempt,aid,JS(job,"operation_key"),timestamp,"executor_started"); evs.Add(JS(started,"id")); job.Set("event_ids",evs);
	Vector<DataBatchChange> mark; mark.Add(UpdateChange(*old,job)); mark.Add(CreateJobChange(ws,started)); ManagerSkylarkApp::BatchFinalizationResult marked=app.FinalizeBatch(auth,JobOperation("running",id+aid),mark); if(!marked.committed) { out.status=marked.status; return out; }
	if(before_dispatch_recheck) before_dispatch_recheck(context);
	CatalogReadHandle dispatch_catalog=app.PinLatestCatalog(); ValueArray dispatch_sources; ValueMap dispatch_policy; String dispatch_revision;
	if(!dispatch_catalog||!dispatch_catalog->IsComplete()||dispatch_catalog->data.GetSourceId()!=pinned.GetSourceId()||
	   !ValidInputPins(dispatch_catalog->data,ws,pins)||
	   !ResolveJobPolicy(app,dispatch_catalog->data,ws,pins,JS(job,"provider_id"),JS(job,"model_config_revision"),dispatch_sources,dispatch_revision,dispatch_policy)||
	   AsJSON(dispatch_sources,true)!=AsJSON(MapGet(job,"contributors"),true)||dispatch_revision!=JS(job,"model_config_revision")) {
		return PersistPolicyDeniedRun(app,auth,ws,id,aid,timestamp);
	}
	JobExecutionRequest req; req.job=job; req.inputs=MapGet(job,"inputs"); req.runtime_limit_ms=JI(job,"budget_ms");
	req.deadline_at=GetSysTime()+max<int64>(1,(req.runtime_limit_ms+999)/1000); req.input_limit_bytes=MAX_REQUEST_BYTES; req.output_limit_bytes=JI(job,"max_output_bytes");
	JobExecutionResult result=executor.Execute(req);
	// Result and terminal state are one canonical batch. A failed commit is intentionally not retried here.
	job.Set("updated_at",timestamp); job.Set("finished_at",timestamp);
	CatalogReadHandle latest=app.PinLatestCatalog();
	bool pins_current=latest&&latest->IsComplete()&&latest->data.GetLoadStatus()=="loaded"&&latest->data.GetSourceId()==pinned.GetSourceId()&&
	                  ValidInputPins(latest->data,ws,MapGet(job,"inputs"));
	String terminal;
	if(!pins_current) { terminal="uncertain"; result.failure_class="stale_input"; result.retryable=false; result.idempotency_safe=false; }
	else if(result.status=="completed"||result.status=="failed"||result.status=="uncertain") terminal=result.status;
	else { terminal="failed"; result.failure_class="invalid_executor_status"; result.retryable=false; result.idempotency_safe=false; }
	if(pins_current&&(result.elapsed_ms<0||result.elapsed_ms>req.runtime_limit_ms)) { terminal="failed"; result.failure_class="timeout"; result.retryable=true; result.idempotency_safe=false; }
	if(pins_current&&result.output.GetCount()>req.output_limit_bytes) { terminal="failed"; result.failure_class="output_too_large"; result.retryable=false; result.idempotency_safe=false; }
	if(terminal=="uncertain"&&result.failure_class.IsEmpty()) result.failure_class="executor_uncertain";
	if(terminal=="failed"&&result.failure_class.IsEmpty()) result.failure_class="executor_failure";
	String failure=result.failure_class;
	if(failure!="stale_input"&&failure!="invalid_executor_status"&&failure!="timeout"&&failure!="output_too_large"&&failure!="executor_uncertain"&&failure!="executor_failure") failure="executor_failure";
	bool success=terminal=="completed"&&pins_current&&result.elapsed_ms>=0&&result.elapsed_ms<=req.runtime_limit_ms&&result.output.GetCount()<=req.output_limit_bytes;
	if(!success&&terminal=="completed") { terminal="failed"; failure="executor_failure"; result.retryable=false; result.idempotency_safe=false; }
	job.Set("retryable",success||terminal!="failed"?false:result.retryable); job.Set("idempotency_safe",success||terminal!="failed"?false:result.idempotency_safe);
	job.Set("output_ref",success?id+"/attempt/"+AsString(attempt):String()); job.Set("output_hash",success?DataStore::ContentDigest(result.output):String());
	job.Set("failure_class",success?String():failure); job.Set("state",terminal);
	evs=MapGet(job,"event_ids"); ValueMap event=NewEvent(ws,id,"running",terminal,evs.GetCount(),attempt,aid,JS(job,"operation_key"),timestamp,success?"executor_completed":failure); evs.Add(JS(event,"id")); job.Set("event_ids",evs);
	ValueMap run=JobBase("job-run","job-run-"+DataStore::ContentDigest(id+aid).Left(32),ws); run.Add("job_id",id); run.Add("attempt",attempt); run.Add("attempt_id",aid); run.Add("state",terminal); run.Add("started_at",JS(job,"started_at")); run.Add("finished_at",timestamp); run.Add("elapsed_ms",max<int64>(0,result.elapsed_ms));
	run.Add("output_ref",JS(job,"output_ref")); run.Add("output_hash",JS(job,"output_hash")); run.Add("output_bytes",result.output.GetCount()); run.Add("inputs",MapGet(job,"inputs")); run.Add("failure_class",success?String():failure); run.Add("retryable",MapGet(job,"retryable")); run.Add("idempotency_safe",MapGet(job,"idempotency_safe"));
	run.Add("output",success?result.output:String());
	ValueArray runs=MapGet(job,"run_ids"); runs.Add(JS(run,"id")); job.Set("run_ids",runs);
	if(!latest||!latest->IsComplete()) { out.status="catalog_unavailable"; return out; }
	const DataDocument *running=FindJob(latest->data,ws,id); if(!running||JS(running->data,"state")!="running") { out.status="stale"; return out; }
	job.Set("envelope",MapGet(running->data,"envelope"));
	return CommitJob(app,auth,JobOperation("result",id+aid+terminal+JS(run,"output_hash")),*running,job,event,&run);
}

JobService::Result JobService::Cancel(ManagerSkylarkApp& app,const DataStore& pinned,const DataWriteAuthorization& auth,const String& ws,const String& id,const String& expected,const String& time) {
	Result out; out.status="denied"; if(!JobScope(pinned,auth,ws)) return out; const DataDocument *old=FindJob(pinned,ws,id); if(!old){out.status="not_found";return out;} out.job_id=id;
	if(old->revision!=expected){out.status="stale";return out;} String state=JS(old->data,"state"); if(state!="queued"&&state!="running"){out.status="invalid_transition";return out;}
	ValueMap job=old->data; String next=state=="queued"?"cancelled":"uncertain"; job.Set("state",next); job.Set("updated_at",time); job.Set("finished_at",time); job.Set("failure_class",state=="running"?"cancel_ack_missing":"");
	ValueArray evs=MapGet(job,"event_ids"); ValueMap e=NewEvent(ws,id,state,next,evs.GetCount(),(int)JI(job,"attempt"),JS(job,"attempt_id"),JS(job,"operation_key"),time,"cancel_requested"); evs.Add(JS(e,"id")); job.Set("event_ids",evs);
	return CommitJob(app,auth,JobOperation("cancel",id+expected),*old,job,e);
}

JobService::Result JobService::Retry(ManagerSkylarkApp& app,const DataStore& pinned,const DataWriteAuthorization& auth,const String& ws,const String& id,const String& expected,const String& time) {
	Result out; out.status="denied"; if(!JobScope(pinned,auth,ws))return out; const DataDocument *old=FindJob(pinned,ws,id); if(!old){out.status="not_found";return out;} out.job_id=id;
	if(old->revision!=expected){out.status="stale";return out;} Value retryable=MapGet(old->data,"retryable"), safe=MapGet(old->data,"idempotency_safe");
	if(JS(old->data,"state")!="failed"||!retryable.Is<bool>()||!(bool)retryable||!safe.Is<bool>()||!(bool)safe||JI(old->data,"attempt")>=JI(old->data,"max_attempts")){out.status="retry_denied";return out;}
	ValueMap job=old->data; job.Set("state","queued"); job.Set("updated_at",time); job.Set("started_at",String()); job.Set("finished_at",String()); ValueArray evs=MapGet(job,"event_ids");
	ValueMap e=NewEvent(ws,id,"failed","queued",evs.GetCount(),(int)JI(job,"attempt"),JS(job,"attempt_id"),JS(job,"operation_key"),time,"explicit_retry"); evs.Add(JS(e,"id")); job.Set("event_ids",evs);
	return CommitJob(app,auth,JobOperation("retry",id+expected),*old,job,e);
}

int JobService::RecoverRunning(ManagerSkylarkApp& app,const DataStore& pinned,const DataWriteAuthorization& auth,const String& ws,const String& time) {
	if(!JobScope(pinned,auth,ws))return -1; int recovered=0;
	for(const DataDocument& old:pinned.GetDocuments()) if(old.kind=="durable-job"&&old.workspace_id==ws&&JS(old.data,"state")=="running") {
		ValueMap job=old.data; job.Set("state","uncertain"); job.Set("updated_at",time); job.Set("finished_at",time); job.Set("failure_class","interrupted");
		ValueArray evs=MapGet(job,"event_ids"); ValueMap e=NewEvent(ws,old.id,"running","uncertain",evs.GetCount(),(int)JI(job,"attempt"),JS(job,"attempt_id"),JS(job,"operation_key"),time,"recovered_after_restart"); evs.Add(JS(e,"id")); job.Set("event_ids",evs);
		Result r=CommitJob(app,auth,JobOperation("recover",old.id+old.revision),old,job,e); if(r.status=="committed"||r.status=="already_committed") recovered++; else return -1;
	}
	return recovered;
}

bool JobService::RunSyntheticTests() {
	const Vector<SchemaCard>& cards=DataStore::GetDefaultSchemaCards();
	for(const char *kind:{"durable-job","job-event","job-run"}) {
		bool found=false; for(const SchemaCard& c:cards) if(c.kind==kind&&c.schema_version=="2") found=true;
		if(!found) { Cerr()<<"T70 schema check failed: "<<kind<<"\n"; return false; }
	}
	ValueMap small; small.Add("budget_ms",1000); small.Add("max_attempts",2); small.Add("max_output_bytes",4096); small.Add("inputs",ValueArray()); small.Add("provider_id","synthetic-provider"); small.Add("model_config_revision","synthetic-revision");
	ValueMap oversized=small; ValueArray many; for(int i=0;i<=MAX_INPUTS;i++) many.Add(ValueMap()); oversized.Set("inputs",many);
	bool small_ok=BoundedRequest(small), oversized_ok=BoundedRequest(oversized);
	if(!small_ok||oversized_ok) { Cerr()<<"T70 bounds check failed "<<small_ok<<"/"<<oversized_ok<<"\n"; return false; }
	String digest=DataStore::ContentDigest(AsJSON(small,true));
	if(digest.GetCount()!=64||digest!=DataStore::ContentDigest(AsJSON(small,true))) { Cerr()<<"T70 digest check failed\n"; return false; }
	return true;
}

END_UPP_NAMESPACE
