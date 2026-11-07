#include "ManagerSkylark.h"
#include "PaperResearch.h"
#include "JobService.h"
#include <cmath>

NAMESPACE_UPP

namespace {

ValueMap PRResult(const String& status, const String& reason = String())
{
	ValueMap out; out.Add("status", status); if(!reason.IsEmpty()) out.Add("reason", reason); return out;
}

bool PRVisible(const DataStore& store, const ManagerSkylarkApp& app, const String& ws,
	             const String& id, const DataDocument **result)
{
	const DataDocument *found = NULL;
	for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id == ws && d.id == id) {
		if(found) return false;
		found = &d;
	}
	if(!found || !found->valid || !found->validated || found->validation_state != "valid") return false;
	if(found->kind == "evidence-item") {
		if(MapGetString(found->data, "visibility") != "visible" || MapGetString(found->data, "resolution") != "resolved" || MapGetString(found->data, "classification") != "public") return false;
		const DataDocument *parent = NULL;
		if(!PRVisible(store, app, ws, MapGetString(found->data, "snapshot"), &parent) || !parent || parent->kind != "evidence-snapshot") return false;
		if(MapGetString(parent->data, "visibility") != "visible" || MapGetString(parent->data, "resolution") != "resolved" || MapGet(parent->data, "retracted") == true || MapGetString(parent->data, "classification") != "public" || !app.IsAssessmentSourceVisible(ws, MapGetString(parent->data, "source_identity"))) return false;
	}
	*result = found; return true;
}

bool CanonicalizePlanValue(const Value& value, Value& canonical, int depth, int& nodes, int64& bytes)
{
	if(depth > 12 || ++nodes > 8192) return false;
	if(IsString(value)) { String s=value; bytes+=s.GetCount(); if(s.GetCount()>8192 || bytes>1536*1024) return false; canonical=s; return true; }
	if(value.Is<ValueMap>()) {
		ValueMap source=value; if(source.GetCount()>64) return false; Vector<String> keys;
		for(int i=0;i<source.GetCount();i++) { if(!IsString(source.GetKey(i))) return false; String key=source.GetKey(i); if(key.IsEmpty()||key.GetCount()>128) return false; bytes+=key.GetCount(); keys.Add(key); }
		if(bytes>1536*1024) return false; Sort(keys); ValueMap out;
		for(const String& key:keys) { Value child, ordered; if(!CanonicalizePlanValue(source.Get(key),ordered,depth+1,nodes,bytes)) return false; child=ordered; out.Add(key,child); }
		canonical=out; return true;
	}
	if(value.Is<ValueArray>()) {
		ValueArray source=value; if(source.GetCount()>128) return false; ValueArray out;
		for(const Value& child:source) { Value ordered; if(!CanonicalizePlanValue(child,ordered,depth+1,nodes,bytes)) return false; out.Add(ordered); }
		canonical=out; return true;
	}
	if(value.Is<int>()||value.Is<int64>()||value.Is<bool>()||value.IsNull()) { bytes+=16; if(bytes>1536*1024) return false; canonical=value; return true; }
	if(value.Is<double>()) { double number=value; if(!std::isfinite(number)) return false; bytes+=24; if(bytes>1536*1024) return false; canonical=value; return true; }
	return false;
}

bool AddResearchIdentity(Vector<String>& identities,const String& identity)
{
	if(identity.IsEmpty()||identity.GetCount()>256) return false;
	for(const String& prior:identities) if(prior==identity) return true;
	if(identities.GetCount()>=32) return false;
	identities.Add(identity); return true;
}

bool AddResearchRecordIdentities(const DataDocument& doc,Vector<String>& identities)
{
	String identity=MapGetString(doc.data,"source_identity");
	if(!identity.IsEmpty()&&!AddResearchIdentity(identities,identity)) return false;
	for(const char *field:{"contributors","source_ids"}) {
		Value value=MapGet(doc.data,field); if(value.IsNull()) continue;
		if(!value.Is<ValueArray>()) return false;
		ValueArray items=value; if(items.GetCount()>256) return false;
		for(const Value& item:items) if(!IsString(item)||!AddResearchIdentity(identities,(String)item)) return false;
	}
	return true;
}

String PlanDigest(const ValueMap& plan)
{
	ValueMap payload=plan; int digest_index=payload.Find("plan_digest"); if(digest_index>=0) payload.Remove(digest_index);
	Value canonical; int nodes=0; int64 bytes=0;
	if(!CanonicalizePlanValue(payload,canonical,0,nodes,bytes)) return String();
	String json=AsJSON(canonical,true); if(json.GetCount()>2*1024*1024) return String();
	return DataStore::ContentDigest(json);
}

String RunIdentityDigest(const ValueMap& plan)
{
	ValueMap identity;
	for(const char *key : { "workspace_id", "paper_snapshot_id", "paper_revision", "source_pin", "license_pin", "external_version", "extraction_sha256", "source_sha256", "extractor", "extractor_version", "extraction_config_revision", "generator_id", "generator_revision", "rubric_id", "rubric_revision", "provider_id", "provider_config_revision", "evidence_pins" }) identity.Add(key,MapGet(plan,key));
	return DataStore::ContentDigest(AsJSON(identity,true));
}

bool SafeOperationId(const String& operation_id)
{
	if(operation_id.IsEmpty() || operation_id.GetCount()>64) return false;
	for(int i=0;i<operation_id.GetCount();i++) { char c=operation_id[i]; bool alnum=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'); if(i==0?!alnum:! (alnum||c=='-'||c=='_')) return false; }
	return true;
}

String PaperEvidenceSnapshot(const DataStore& store, const DataDocument& paper)
{
	String found;
	for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id==paper.workspace_id && d.kind=="evidence-snapshot" && MapGetString(d.data,"source_identity")==MapGetString(paper.data,"source_identity") && MapGetString(d.data,"upstream_revision")==MapGetString(paper.data,"upstream_revision")) {
		ValueMap env=MapGet(d.data,"envelope");
		if(MapGetString(env,"provenance_ref")!="paper-source:"+paper.id) continue;
		if(!found.IsEmpty()) return String();
		found=d.id;
	}
	return found;
}

ValueMap ResearchDocumentPin(const DataDocument& doc)
{
	ValueMap pin; pin.Add("qualified_id",doc.qualified_id); pin.Add("revision",doc.revision); pin.Add("source_revision",doc.source_revision);
	return pin;
}

bool SameResearchDocumentPin(const DataDocument& doc, const ValueMap& pin)
{
	return doc.qualified_id==MapGetString(pin,"qualified_id") && doc.revision==MapGetString(pin,"revision") && doc.source_revision==MapGetString(pin,"source_revision");
}

ValueMap PaperSourcePin(const DataDocument& paper)
{
	ValueMap pin=ResearchDocumentPin(paper);
	for(const char *key:{"source_identity","source_url","final_url","external_version","upstream_revision","source_sha256","extraction_sha256","extractor","extractor_version","config_revision","rule_id","internal_use_mode","license_id","license_evidence_ref"}) pin.Add(key,MapGet(paper.data,key));
	return pin;
}

ValueMap LicenseEvidencePin(const DataDocument& item)
{
	ValueMap pin=ResearchDocumentPin(item);
	for(const char *key:{"source_identity","upstream_revision","content_sha256","locator"}) pin.Add(key,MapGet(item.data,key));
	return pin;
}

}

ValueMap PaperResearch::Run(const DataStore& store, const ManagerSkylarkApp& app,
	const String& ws, const String& paper_id, const String& paper_revision,
	const String& extraction_hash, const ValueArray& evidence_ids, const String& provider_id,
	PaperCandidateGenerator *generator)
{
	if(!generator) return PRResult("analysis_unconfigured", "no_generator_installed");
	if(store.GetLoadStatus() != "loaded" || ws.IsEmpty()) return PRResult("blocked", "complete_pinned_store_required");
	const DataDocument *paper = NULL;
	if(!PRVisible(store, app, ws, paper_id, &paper) || !paper || paper->kind != "paper-source-snapshot" || MapGetString(paper->data, "workspace_id") != ws) return PRResult("blocked", "paper_unavailable_or_hidden");
	String source_identity = MapGetString(paper->data, "source_identity");
	ValueMap paper_envelope=MapGet(paper->data,"envelope");
	if(MapGetString(paper_envelope,"provenance_ref")!="trusted-paper-source-import") return PRResult("blocked","paper_source_not_from_t73_adapter");
	String evidence_snapshot_id=PaperEvidenceSnapshot(store,*paper);
	if(evidence_snapshot_id.IsEmpty()) return PRResult("blocked","paper_evidence_snapshot_mismatch");
	String license = MapGetString(paper->data, "license_id"), rule = MapGetString(paper->data, "rule_id");
	if(MapGetString(paper->data, "upstream_revision") != paper_revision || MapGetString(paper->data, "extraction_sha256") != extraction_hash || extraction_hash.GetCount() != 64 || MapGetString(paper->data, "source_sha256").GetCount() != 64) return PRResult("blocked", "stale_paper_or_extraction");
	if(license.IsEmpty()) return PRResult("blocked", "unknown_license");
	if(rule.IsEmpty() || MapGetString(paper->data, "internal_use_mode") != "internal-only") return PRResult("blocked", "license_or_source_rule_not_allowed");
	if(!app.IsAssessmentSourceVisible(ws, source_identity)) return PRResult("blocked", "paper_source_not_visible_or_unregistered");
	const DataDocument *license_evidence = NULL;
	if(!PRVisible(store, app, ws, MapGetString(paper->data, "license_evidence_ref"), &license_evidence) || !license_evidence || license_evidence->kind != "evidence-item" || license_evidence->workspace_id != ws) return PRResult("blocked", "license_evidence_unavailable");
	const DataDocument *license_snapshot=NULL;
	if(!PRVisible(store,app,ws,MapGetString(license_evidence->data,"snapshot"),&license_snapshot) || !license_snapshot || license_snapshot->kind!="evidence-snapshot" || MapGetString(license_evidence->data,"content_sha256").GetCount()!=64 || MapGet(license_evidence->data,"content_sha256")!=MapGet(license_snapshot->data,"content_sha256") || AsJSON(MapGet(license_evidence->data,"locator"),true)!=AsJSON(MapGet(license_snapshot->data,"locator"),true) || MapGet(license_evidence->data,"locator").IsNull() || MapGetString(license_evidence->data,"source_identity").IsEmpty() || MapGetString(license_evidence->data,"source_identity")!=MapGetString(license_snapshot->data,"source_identity") || MapGetString(license_evidence->data,"upstream_revision")!=MapGetString(license_snapshot->data,"upstream_revision")) return PRResult("blocked","license_evidence_inconsistent");
	if(evidence_ids.IsEmpty() || evidence_ids.GetCount() > 64) return PRResult("blocked", "bounded_evidence_required");
	ValueArray permitted, evidence_pins;
	for(const Value& v : evidence_ids) {
		if(!IsString(v) || ((String)v).IsEmpty() || ((String)v).GetCount() > 256) return PRResult("blocked", "invalid_evidence_id");
		String id = v; for(const Value& prior : permitted) if((String)prior == id) return PRResult("blocked", "duplicate_evidence_id");
		const DataDocument *item = NULL;
		if(!PRVisible(store, app, ws, id, &item) || !item || item->kind != "evidence-item" || MapGetString(item->data,"snapshot") != evidence_snapshot_id || MapGetString(item->data,"source_identity") != source_identity || MapGetString(item->data,"upstream_revision") != paper_revision || MapGetString(item->data,"content_sha256").GetCount() != 64 || MapGet(item->data,"locator").IsNull()) return PRResult("blocked", "evidence_not_pinned_to_exact_paper_extraction");
		permitted.Add(id); ValueMap pin; pin.Add("id",id); pin.Add("revision",item->revision); pin.Add("source_revision",item->source_revision); pin.Add("content_sha256",MapGet(item->data,"content_sha256")); pin.Add("locator",MapGet(item->data,"locator")); evidence_pins.Add(pin);
	}
	PolicyInput policy_input; policy_input.workspace.workspace_id=ws; policy_input.workspace.object_id=ws; policy_input.provider.workspace_id=ws; policy_input.provider.object_id=provider_id;
	PolicyIdentity source; source.workspace_id=ws; source.object_id=source_identity; policy_input.sources.Add(source);
	EffectivePolicy decision=app.ResolveEffectivePolicy(policy_input);
	if(decision.status!="evaluated" || !decision.external_model_allowed || !decision.source_visible) return PRResult("blocked", "external_model_policy_denied");
	String analyzer=generator->GetGeneratorId(), analyzer_revision=generator->GetGeneratorRevision(), provider_config_revision=generator->GetProviderConfigRevision(), rubric_id=generator->GetRubricId(), rubric_revision=generator->GetRubricRevision();
	if(analyzer.IsEmpty()||analyzer.GetCount()>128||analyzer_revision.IsEmpty()||analyzer_revision.GetCount()>128||provider_config_revision.IsEmpty()||provider_config_revision.GetCount()>128||rubric_id.IsEmpty()||rubric_id.GetCount()>128||rubric_revision.IsEmpty()||rubric_revision.GetCount()>128) return PRResult("blocked","invalid_analyzer_configuration");
	ValueMap source_pin=PaperSourcePin(*paper), license_pin=LicenseEvidencePin(*license_evidence);
	ValueMap pinned; pinned.Add("workspace_id",ws); pinned.Add("paper_snapshot_id",paper_id); pinned.Add("paper_revision",paper_revision); pinned.Add("source_pin",source_pin); pinned.Add("license_pin",license_pin); pinned.Add("external_version",MapGet(paper->data,"external_version")); pinned.Add("extraction_sha256",extraction_hash); pinned.Add("source_sha256",MapGet(paper->data,"source_sha256")); pinned.Add("provider_id",provider_id); pinned.Add("provider_config_revision",provider_config_revision); pinned.Add("generator_id",analyzer); pinned.Add("generator_revision",analyzer_revision); pinned.Add("rubric_id",rubric_id); pinned.Add("rubric_revision",rubric_revision); pinned.Add("evidence_ids",permitted);
	Vector<String> source_identities;
	if(!AddResearchRecordIdentities(*paper,source_identities)||!AddResearchRecordIdentities(*license_evidence,source_identities)) return PRResult("blocked","external_model_policy_denied");
	for(const Value& evidence_id:permitted) { const DataDocument *item=NULL; if(!PRVisible(store,app,ws,evidence_id,&item)||!item||!AddResearchRecordIdentities(*item,source_identities)) return PRResult("blocked","external_model_policy_denied"); }
	if(!JobService::IsProviderConfigCurrent(app,ws,provider_id,source_identities,provider_config_revision)) return PRResult("blocked","external_model_policy_denied");
	Vector<PaperResearchClaim> claims; if(!generator->Generate(pinned,claims)) return PRResult("blocked", "analysis_failed");
	if(claims.GetCount()>64) return PRResult("blocked", "claim_limit_exceeded");
	String evidence_hash=DataStore::ContentDigest(AsJSON(evidence_pins,true));
	ValueArray findings,drafts,blockers;
	for(int i=0;i<claims.GetCount();i++) {
		const PaperResearchClaim& c=claims[i]; ValueMap f; f.Add("claim_index",i); f.Add("text",c.title.Left(4096)); f.Add("uncertainty",c.uncertainty.Left(4096));
		ValueArray links, candidate_links, counter, locators, counter_locators; Index<String> unique; bool citation_ok=!c.evidence_ids.IsEmpty() && c.evidence_ids.GetCount()<=64;
		for(const String& id:c.evidence_ids) {
			const DataDocument *ev=NULL; bool selected=false; for(const Value& x:permitted) if((String)x==id) selected=true;
			if(id.GetCount()<=256) links.Add(id);
			bool visible=PRVisible(store,app,ws,id,&ev) && ev && ev->kind=="evidence-item";
			if(visible) { ValueMap loc; loc.Add("id",id); loc.Add("revision",ev->revision); loc.Add("locator",MapGet(ev->data,"locator")); loc.Add("content_sha256",MapGet(ev->data,"content_sha256")); locators.Add(loc); }
			if(!selected || unique.Find(id)>=0 || !visible || MapGetString(ev->data,"snapshot")!=evidence_snapshot_id || MapGetString(ev->data,"upstream_revision")!=paper_revision) { citation_ok=false; continue; }
			unique.Add(id); candidate_links.Add(id);
		}
		bool counter_ok=true;
		for(const String& id:c.counterevidence_ids) {
			const DataDocument *ev=NULL; bool selected=false; for(const Value& x:permitted) if((String)x==id) selected=true;
			if(id.GetCount()<=256) counter.Add(id);
			bool visible=PRVisible(store,app,ws,id,&ev) && ev && ev->kind=="evidence-item";
			if(visible) { ValueMap loc; loc.Add("id",id); loc.Add("revision",ev->revision); loc.Add("locator",MapGet(ev->data,"locator")); loc.Add("content_sha256",MapGet(ev->data,"content_sha256")); counter_locators.Add(loc); }
			if(!selected || !visible || MapGetString(ev->data,"snapshot")!=evidence_snapshot_id || MapGetString(ev->data,"upstream_revision")!=paper_revision) counter_ok=false;
		}
		bool bounded=true; for(const String& text:{c.title,c.purpose,c.quality_contract,c.inputs,c.outputs,c.runtime_resources,c.assets,c.verification_plan,c.uncertainty}) if(text.GetCount()>CandidateWorkflow::MAX_TEXT_BYTES) bounded=false;
		bool supported=c.supported && citation_ok && counter_ok && counter.IsEmpty() && bounded;
		f.Add("assertion",supported?"supported":"unsupported"); f.Add("evidence_ids",links); f.Add("evidence_locators",locators); f.Add("counterevidence_ids",counter); f.Add("counterevidence_locators",counter_locators); f.Add("reason",!citation_ok?"citation_missing_forged_mixed_or_stale":!counter_ok||!counter.IsEmpty()?"counterevidence_unresolved_or_present":!bounded?"claim_field_bound_exceeded":!c.supported?"analyzer_asserted_unsupported":"");
		if(supported) {
			ValueMap form; form.Add("candidate_kind","research-stage"); form.Add("title",c.title); form.Add("purpose",c.purpose); form.Add("quality_contract",c.quality_contract); form.Add("inputs",c.inputs); form.Add("outputs",c.outputs); form.Add("runtime_resources",c.runtime_resources); form.Add("assets",c.assets); form.Add("verification_plan",c.verification_plan); form.Add("evidence_links",candidate_links);
			ValueMap valid=CandidateWorkflow::ValidateProposal(store,form,ws,app);
			if(MapGetString(valid,"status")=="valid") drafts.Add(form); else { f.Set("assertion","unsupported"); f.Set("reason","candidate_form_validation_failed"); }
		}
		findings.Add(f); if(MapGetString(f,"assertion")=="unsupported") blockers.Add(MapGetString(f,"reason"));
	}
	int supported_count=0; for(const Value& finding:findings) if(MapGetString((ValueMap)finding,"assertion")=="supported") supported_count++;
	ValueMap plan; plan.Add("kind","paper-research-run"); plan.Add("workspace_id",ws); plan.Add("revision","1"); plan.Add("status",drafts.IsEmpty()?"no_supported_candidate":"candidate_drafts_ready"); plan.Add("paper_snapshot_id",paper_id); plan.Add("paper_revision",paper_revision); plan.Add("source_pin",source_pin); plan.Add("license_pin",license_pin); plan.Add("external_version",MapGet(paper->data,"external_version")); plan.Add("extraction_sha256",extraction_hash); plan.Add("source_sha256",MapGet(paper->data,"source_sha256")); plan.Add("extractor",MapGet(paper->data,"extractor")); plan.Add("extractor_version",MapGet(paper->data,"extractor_version")); plan.Add("extraction_config_revision",MapGet(paper->data,"config_revision")); plan.Add("generator_id",analyzer); plan.Add("generator_revision",analyzer_revision); plan.Add("rubric_id",rubric_id); plan.Add("rubric_revision",rubric_revision); plan.Add("provider_id",provider_id); plan.Add("provider_config_revision",provider_config_revision); plan.Add("claim_count",claims.GetCount()); plan.Add("supported_claim_count",supported_count); plan.Add("unsupported_claim_count",claims.GetCount()-supported_count); plan.Add("candidate_draft_count",drafts.GetCount()); plan.Add("evidence_pins",evidence_pins); plan.Add("evidence_ids",permitted); plan.Add("candidate_ids",ValueArray()); plan.Add("findings",findings); plan.Add("blockers",blockers);
	ValueMap env; env.Add("extractor",MapGet(paper->data,"extractor")); env.Add("extractor_version",MapGet(paper->data,"extractor_version")); env.Add("extraction_config_revision",MapGet(paper->data,"config_revision")); env.Add("generator_id",analyzer); env.Add("generator_revision",analyzer_revision); env.Add("rubric_id",rubric_id); env.Add("rubric_revision",rubric_revision); env.Add("evidence_hash",evidence_hash); env.Add("license_id",license); env.Add("rule_id",rule); env.Add("internal_use_mode","internal-only"); env.Add("source_pin",source_pin); env.Add("license_pin",license_pin); env.Add("policy_decision",decision.ToRedactedValue()); env.Add("candidate_drafts",drafts); plan.Add("envelope",env);
	String identity_digest=RunIdentityDigest(plan); plan.Add("id","paper-run-"+identity_digest.Left(32));
	String plan_digest=PlanDigest(plan); if(plan_digest.IsEmpty()) return PRResult("blocked","plan_size_or_shape_limit"); plan.Add("plan_digest",plan_digest); return plan;
}

ValueMap PaperResearch::PersistRun(const ValueMap& plan, const DataStore& store,
	const DataWriteAuthorization& authorization, const String& operation_id)
{
	String ws=MapGetString(plan,"workspace_id"), id=MapGetString(plan,"id"), supplied_digest=MapGetString(plan,"plan_digest");
	String actual_digest=PlanDigest(plan);
	if(store.GetLoadStatus()!="loaded" || !authorization.AllowsSource(ws,store.GetSourceId())) return PRResult("denied","write_scope_denied");
	if(!SafeOperationId(operation_id)) return PRResult("denied","operation_id_invalid");
	if(MapGetString(plan,"kind")!="paper-research-run" || id.IsEmpty() || actual_digest.IsEmpty() || supplied_digest!=actual_digest) return PRResult("denied","plan_integrity_invalid");
	if(id!="paper-run-"+RunIdentityDigest(plan).Left(32)) return PRResult("denied","run_identity_mismatch");
	ValueMap record=plan; int digest_key=record.Find("plan_digest"); if(digest_key>=0) record.Remove(digest_key); int pins_key=record.Find("evidence_pins"); if(pins_key>=0) record.Remove(pins_key); // Pins are retained in the immutable envelope below.
	ValueMap envelope=MapGet(record,"envelope"); envelope.Add("evidence_pins",MapGet(plan,"evidence_pins")); envelope.Add("plan_digest",MapGetString(plan,"plan_digest")); record.Set("envelope",envelope); record.Add("operation_id",operation_id);
	envelope.Add("schema_id","skylark/paper-research-run"); envelope.Add("schema_version","2"); envelope.Add("workspace_id",ws); envelope.Add("stable_id",id); envelope.Add("record_kind","paper-research-run"); envelope.Add("revision","1"); envelope.Add("visibility","private"); envelope.Add("provenance_ref","computed-paper-research"); record.Set("envelope",envelope);
	const DataDocument *prior=store.FindDocument(ws+":"+id);
	if(prior) return AsJSON(prior->data,true)==AsJSON(record,true)?PRResult("already_committed"):PRResult("conflict","run_id_payload_mismatch");
	Vector<DataBatchChange> batch; DataBatchChange c; c.action=DATA_BATCH_CREATE; c.qualified_id=ws+":"+id; c.proposed=record; batch.Add(c);
	DataBatchResult result=store.PublishBatch(authorization,operation_id,batch);
	if(result.status=="committed") { ValueMap out=PRResult("committed"); out.Add("run_id",id); return out; }
	return PRResult(result.status=="revision_conflict"||result.status=="already_exists"?"conflict":"denied",result.status=="revision_conflict"||result.status=="already_exists"?"run_conflict":"write_not_committed");
}

ValueMap PaperResearch::Handoff(const DataStore& store, ManagerSkylarkApp& app,
	const String& ws, const String& source, const String& run_id, const UserSession& session)
{
	// Authorization deliberately precedes every lookup in the caller-pinned store.
	if(!SystemAuthService::AuthorizeWriteForSession(app,session,source,ws)) return PRResult("denied","write_authorization_denied");
	if(store.GetLoadStatus()!="loaded" || store.GetSourceId()!=source || ws.IsEmpty() || run_id.IsEmpty()) return PRResult("denied","write_scope_denied");
	const DataDocument *run=store.FindDocument(ws+":"+run_id);
	if(!run || !run->valid || !run->validated || run->validation_state!="valid" || run->kind!="paper-research-run" || run->workspace_id!=ws || (MapGetString(run->data,"status")!="candidate_drafts_ready" && MapGetString(run->data,"status")!="no_supported_candidate")) return PRResult("blocked","run_unavailable_or_unsupported");
	const String paper_id=MapGetString(run->data,"paper_snapshot_id"), paper_revision=MapGetString(run->data,"paper_revision");
	const DataDocument *paper=NULL;
	ValueMap saved_env=MapGet(run->data,"envelope"), saved_source_pin=MapGet(run->data,"source_pin"), saved_license_pin=MapGet(run->data,"license_pin");
	if(AsJSON(saved_source_pin,true).IsEmpty() || AsJSON(saved_source_pin,true)!=AsJSON(MapGet(saved_env,"source_pin"),true) || AsJSON(saved_license_pin,true).IsEmpty() || AsJSON(saved_license_pin,true)!=AsJSON(MapGet(saved_env,"license_pin"),true)) return PRResult("blocked","run_source_or_license_stale");
	if(!PRVisible(store,app,ws,paper_id,&paper) || !paper || paper->kind!="paper-source-snapshot" || !SameResearchDocumentPin(*paper,saved_source_pin) || AsJSON(PaperSourcePin(*paper),true)!=AsJSON(saved_source_pin,true) || MapGetString(paper->data,"upstream_revision")!=paper_revision || MapGetString(paper->data,"extraction_sha256")!=MapGetString(run->data,"extraction_sha256") || MapGetString(paper->data,"source_sha256")!=MapGetString(run->data,"source_sha256") || MapGetString(paper->data,"internal_use_mode")!="internal-only") return PRResult("blocked","run_source_or_license_stale");
	String actual_source=MapGetString(paper->data,"source_identity");
	if(actual_source.IsEmpty() || !app.IsAssessmentSourceVisible(ws,actual_source)) return PRResult("blocked","source_not_visible");
	const DataDocument *license_item=NULL;
	if(!PRVisible(store,app,ws,MapGetString(saved_license_pin,"qualified_id").Mid(ws.GetCount()+1),&license_item) || !license_item || license_item->kind!="evidence-item" || !SameResearchDocumentPin(*license_item,saved_license_pin) || AsJSON(LicenseEvidencePin(*license_item),true)!=AsJSON(saved_license_pin,true) || MapGetString(paper->data,"license_evidence_ref")!=license_item->id || MapGetString(license_item->data,"content_sha256").GetCount()!=64 || MapGet(license_item->data,"locator").IsNull()) return PRResult("blocked","run_source_or_license_stale");
	const DataDocument *license_snapshot=NULL;
	if(!PRVisible(store,app,ws,MapGetString(license_item->data,"snapshot"),&license_snapshot) || !license_snapshot || license_snapshot->kind!="evidence-snapshot" || MapGet(license_item->data,"content_sha256")!=MapGet(license_snapshot->data,"content_sha256") || AsJSON(MapGet(license_item->data,"locator"),true)!=AsJSON(MapGet(license_snapshot->data,"locator"),true) || MapGetString(license_item->data,"source_identity")!=MapGetString(license_snapshot->data,"source_identity") || MapGetString(license_item->data,"upstream_revision")!=MapGetString(license_snapshot->data,"upstream_revision")) return PRResult("blocked","run_source_or_license_stale");
	ValueArray pins=MapGet(MapGet(run->data,"envelope"),"evidence_pins");
	if(pins.IsEmpty() || pins.GetCount()>64) return PRResult("blocked","evidence_pins_missing");
	Vector<String> source_identities; if(!AddResearchIdentity(source_identities,actual_source)||!AddResearchRecordIdentities(*paper,source_identities)||!AddResearchRecordIdentities(*license_item,source_identities)) return PRResult("blocked","run_policy_stale");
	for(const Value& pv:pins) {
		if(!pv.Is<ValueMap>()) return PRResult("blocked","evidence_pin_invalid"); ValueMap pin=pv; const DataDocument *ev=NULL;
		if(!PRVisible(store,app,ws,MapGetString(pin,"id"),&ev) || !ev || ev->kind!="evidence-item" || ev->revision!=MapGetString(pin,"revision") || ev->source_revision!=MapGetString(pin,"source_revision") || MapGetString(ev->data,"source_identity")!=actual_source || MapGetString(ev->data,"upstream_revision")!=paper_revision || MapGet(ev->data,"content_sha256")!=MapGet(pin,"content_sha256") || AsJSON(MapGet(ev->data,"locator"),true)!=AsJSON(MapGet(pin,"locator"),true)) return PRResult("blocked","evidence_pin_stale_or_hidden");
		if(!AddResearchRecordIdentities(*ev,source_identities)) return PRResult("blocked","run_policy_stale");
	}
	PolicyInput input; input.workspace.workspace_id=ws; input.workspace.object_id=ws; input.provider.workspace_id=ws; input.provider.object_id=MapGetString(run->data,"provider_id"); input.action=POLICY_ACTION_MODEL;
	PolicyIdentity src; src.workspace_id=ws; src.object_id=actual_source; input.sources.Add(src);
	EffectivePolicy policy=app.ResolveEffectivePolicy(input); ValueMap old_policy=MapGet(MapGet(run->data,"envelope"),"policy_decision");
	if(policy.status!="evaluated" || !policy.source_visible || !policy.external_model_allowed || AsJSON(policy.ToRedactedValue(),true)!=AsJSON(old_policy,true) ||
	   !JobService::IsProviderConfigCurrent(app,ws,MapGetString(run->data,"provider_id"),source_identities,MapGetString(run->data,"provider_config_revision"))) return PRResult("blocked","run_policy_stale");
	String handoff_id="paper-handoff-"+DataStore::ContentDigest(ws+"\n"+run_id+"\n"+MapGetString(run->data,"revision")).Left(32);
	const DataDocument *prior_handoff=store.FindDocument(ws+":"+handoff_id);
	if(prior_handoff) return prior_handoff->valid && prior_handoff->validated && prior_handoff->kind=="paper-candidate-handoff" && MapGetString(prior_handoff->data,"run_id")==run_id ? PRResult("already_committed") : PRResult("blocked","handoff_record_invalid");
	ValueArray drafts=MapGet(MapGet(run->data,"envelope"),"candidate_drafts"); ValueArray supported, duplicates, possibles, blockers;
	if(drafts.GetCount()>64) return PRResult("blocked","draft_limit_exceeded");
	for(int i=0;i<drafts.GetCount();i++) {
		if(!drafts[i].Is<ValueMap>()) { blockers.Add("unsupported_draft"); continue; }
		ValueMap form=drafts[i]; form.Set("candidate_kind","research-stage"); form.Set("evidence_links",MapGet(form,"evidence_links"));
		ValueMap valid=CandidateWorkflow::ValidateProposal(store,form,ws,app);
		if(MapGetString(valid,"status")!="valid") { blockers.Add("candidate_form_invalid"); continue; }
		String fingerprint=DataStore::ContentDigest(AsJSON(form,true)); bool exact=false, near=false; String existing;
		for(const DataDocument& d:store.GetDocuments()) if(d.workspace_id==ws && d.kind=="candidate-proposal" && d.valid && d.validated && MapGetString(d.data,"source_identity")==actual_source) {
			ValueMap prior; for(const char *k:{"candidate_kind","title","purpose","quality_contract","inputs","outputs","runtime_resources","assets","verification_plan","evidence_links","counterevidence","uncertainty"}) if(!MapGet(d.data,k).IsVoid()) prior.Add(k,MapGet(d.data,k));
			String prior_fp=DataStore::ContentDigest(AsJSON(prior,true)); if(prior_fp==fingerprint) { exact=true; existing=MapGetString(d.data,"candidate_id"); break; }
			if(ToLower(MapGetString(d.data,"title"))==ToLower(MapGetString(form,"title"))) { near=true; if(existing.IsEmpty()) existing=MapGetString(d.data,"candidate_id"); }
		}
		if(exact) { ValueMap row; row.Add("draft_index",i); row.Add("existing_candidate_id",existing); duplicates.Add(row); continue; }
		if(near) { ValueMap row; row.Add("draft_index",i); row.Add("existing_candidate_id",existing); possibles.Add(row); continue; }
		String op="paper-handoff-"+DataStore::ContentDigest(ws+"\n"+run_id+"\n"+AsString(i)).Left(32);
		ValueMap created=CandidateWorkflow::CreateProposal(store,app,form,ws,source,op,session);
		if(MapGetString(created,"status")=="committed" || MapGetString(created,"status")=="already_committed" || MapGetString(created,"status")=="committed_notification_pending") supported.Add(MapGetString(created,"candidate_id"));
		else blockers.Add("proposal_creation_blocked");
	}
	String status=drafts.IsEmpty()?"no-supported":blockers.IsEmpty()?"complete":"partial";
	ValueMap record; String id=handoff_id;
	record.Add("id",id); record.Add("kind","paper-candidate-handoff"); record.Add("workspace_id",ws); record.Add("revision","1"); record.Add("status",status); record.Add("run_id",run_id); record.Add("run_revision",MapGetString(run->data,"revision")); record.Add("paper_snapshot_id",paper_id); record.Add("paper_revision",paper_revision); record.Add("extraction_sha256",MapGetString(run->data,"extraction_sha256")); record.Add("source_identity",actual_source); record.Add("supported_candidate_ids",supported); record.Add("duplicates",duplicates); record.Add("possible_duplicates",possibles); record.Add("blockers",blockers); record.Add("operation_id",id);
	ValueMap env; env.Add("schema_id","skylark/paper-candidate-handoff"); env.Add("schema_version","2"); env.Add("workspace_id",ws); env.Add("stable_id",id); env.Add("record_kind","paper-candidate-handoff"); env.Add("revision","1"); env.Add("visibility","private"); env.Add("provenance_ref","validated-paper-handoff"); env.Add("evidence_pins",pins); record.Add("envelope",env);
	const DataDocument *prior=store.FindDocument(ws+":"+id); if(prior) return AsJSON(prior->data,true)==AsJSON(record,true)?PRResult("already_committed"):PRResult("conflict","handoff_payload_mismatch");
	DataBatchChange change; change.action=DATA_BATCH_CREATE; change.qualified_id=ws+":"+id; change.proposed=record; Vector<DataBatchChange> batch; batch.Add(change);
	ManagerSkylarkApp::BatchFinalizationResult result=SystemAuthService::FinalizeBatchForSession(app,session,source,ws,id,batch);
	String final_status=result.committed?status:result.status=="already_committed"?String("already_committed"):result.status=="revision_conflict"?String("conflict"):String("blocked");
	ValueMap out=PRResult(final_status,result.committed?String():String("handoff_not_committed")); if(result.committed) out.Add("handoff_id",id); return out;
}

END_UPP_NAMESPACE
