#include "CandidateWorkflow.h"
#include "CandidateLineage.h"
#include "ManagerSkylark.h"

NAMESPACE_UPP

static String CW(const ValueMap& m, const String& k) { return MapGetString(m, k); }
static const char *CW_STAGES[] = { "draft", "source-inspected", "duplicate-reviewed", "spike-verified", "deliverables-ready", "verifier-feasible", "comparison-ready" };
static bool CWStage(const String& s) { for(const char *v : CW_STAGES) if(s == v) return true; return false; }
static String CWStageHeadId(const String& ws, const String& candidate_id, const String& proposal_revision_id)
{
	return "candidate-stage-head-" + DataStore::ContentDigest(ws + "\n" + candidate_id + "\n" + proposal_revision_id).Left(32);
}
static String CWStageEventId(const String& operation, const String& candidate_id, const String& proposal_revision_id, const String& stage)
{
	return "candidate-stage-event-" + DataStore::ContentDigest(operation + "\n" + candidate_id + "\n" + proposal_revision_id + "\n" + stage).Left(32);
}
static bool CWOperation(const String& s)
{
	if(s.GetCount() < 8 || s.GetCount() > 128) return false;
	for(int i = 0; i < s.GetCount(); i++) {
		char c = s[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':')) return false;
	}
	return true;
}
static bool CWTimestamp(const String& value)
{
	if(value.GetCount() != 19 || value[2] != '/' || value[5] != '/' || value[10] != ' ' || value[13] != ':' || value[16] != ':') return false;
	for(int i = 0; i < value.GetCount(); i++) if(i != 2 && i != 5 && i != 10 && i != 13 && i != 16 && (value[i] < '0' || value[i] > '9')) return false;
	int month = ScanInt(value.Mid(0, 2)), day = ScanInt(value.Mid(3, 2)), year = ScanInt(value.Mid(6, 4));
	int hour = ScanInt(value.Mid(11, 2)), minute = ScanInt(value.Mid(14, 2)), second = ScanInt(value.Mid(17, 2));
	if(month < 1 || month > 12 || year < 1 || hour > 23 || minute > 59 || second > 59) return false;
	static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	int month_days = days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
	return day >= 1 && day <= month_days;
}
static int CWRevisionNumber(const String& revision)
{
	if(!revision.StartsWith("r")) return 0;
	int number = ScanInt(revision.Mid(1));
	return number > 0 && Format("r%d", number) == revision ? number : 0;
}
static bool CWText(const ValueMap& m, const String& k, int limit)
{
	Value v = MapGet(m, k);
	if(!IsString(v)) return false;
	String s = TrimBoth((String)v);
	if(s.IsEmpty() || s.GetCount() > limit) return false;
	String lower = ToLower(s);
	if(lower == "n/a" || lower == "unknown") return false;
	for(const char *bad : { "todo", "tbd", "placeholder", "lorem", "enter text", "enter-text", "fill in", "your text" }) if(lower.Find(bad) >= 0) return false;
	return true;
}
static bool CWInteger(const Value& v)
{
	if(v.Is<int>() || v.Is<int64>()) return true;
	if(!v.Is<double>()) return false;
	double d = (double)v;
	return d >= 1 && d <= 100000 && d == floor(d);
}
static bool CWOnly(const ValueMap& m, const char *const *keys, int count)
{
	for(int i = 0; i < m.GetCount(); i++) {
		bool found = false;
		for(int j = 0; j < count; j++) if(m.GetKey(i) == keys[j]) found = true;
		if(!found) return false;
	}
	return true;
}
static ValueMap CWBase(const String& kind, const String& id, const String& ws, const String& provenance = "authored")
{
	ValueMap r, e;
	r.Add("id", id); r.Add("kind", kind); r.Add("workspace_id", ws); r.Add("revision", "1"); r.Add("provenance", provenance);
	e.Add("schema_id", "skylark/" + kind); e.Add("schema_version", "1"); e.Add("workspace_id", ws);
	e.Add("stable_id", id); e.Add("record_kind", kind); e.Add("revision", "1"); e.Add("visibility", "private"); e.Add("provenance_ref", provenance);
	r.Add("envelope", e); return r;
}
static bool CWUsableEvidence(const DataStore& store, const ManagerSkylarkApp& app, const String& ws, const String& id, const String& kind = String())
{
	const DataDocument *d = store.FindDocument(ws + ":" + id);
	if(!d || !d->valid || !d->validated || d->validation_state != "valid" || d->workspace_id != ws || d->kind != "evidence-item" ||
	   MapGetString(d->data, "resolution") != "resolved" || MapGetString(d->data, "visibility") != "visible") return false;
	String source = MapGetString(d->data, "source_identity"), snapshot_id = MapGetString(d->data, "snapshot");
	const DataDocument *snapshot = store.FindDocument(ws + ":" + snapshot_id);
	if(source.IsEmpty() || !app.IsAssessmentSourceVisible(ws, source) || !snapshot || !snapshot->valid || !snapshot->validated ||
	   snapshot->workspace_id != ws || snapshot->kind != "evidence-snapshot" || MapGetString(snapshot->data, "source_identity") != source ||
	   MapGetString(snapshot->data, "resolution") != "resolved" || MapGetString(snapshot->data, "visibility") != "visible" ||
	   MapGet(snapshot->data, "retracted") == true || !app.IsAssessmentSourceVisible(ws, MapGetString(snapshot->data, "source_identity"))) return false;
	if(kind.IsEmpty()) return true;
	ValueMap locator = MapGet(d->data, "locator"), snapshot_locator = MapGet(snapshot->data, "locator");
	return kind == "commit-diff" && MapGetString(snapshot->data, "source_kind") == "commit-diff" &&
	       MapGetString(locator, "type") == "commit-line" && !MapGetString(locator, "id").IsEmpty() &&
	       !MapGetString(locator, "path").IsEmpty() && MapGetString(snapshot_locator, "type") == "commit-line" &&
	       MapGetString(snapshot_locator, "id") == MapGetString(locator, "id") && MapGetString(snapshot_locator, "path") == MapGetString(locator, "path") &&
	       CWInteger(MapGet(locator, "first")) && CWInteger(MapGet(locator, "last")) && (int64)MapGet(locator, "first") > 0 && (int64)MapGet(locator, "last") >= (int64)MapGet(locator, "first") &&
	       CWInteger(MapGet(snapshot_locator, "first")) && CWInteger(MapGet(snapshot_locator, "last")) && (int64)MapGet(snapshot_locator, "first") == (int64)MapGet(locator, "first") && (int64)MapGet(snapshot_locator, "last") == (int64)MapGet(locator, "last");
}
static ValueMap CWResult(const String& status, const String& detail = String())
{
	ValueMap r; r.Add("status", status); if(!detail.IsEmpty()) r.Add("reason", detail); return r;
}

static bool CWHasLink(const ValueArray& links, const String& id)
{
	for(const Value& value : links) if(IsString(value) && (String)value == id) return true;
	return false;
}

static bool CWHasRevisionPin(const ValueArray& pins, const DataDocument& document)
{
	for(const Value& value : pins) if(IsValueMap(value)) {
		ValueMap pin = value;
		if(CW(pin, "workspace_id") == document.workspace_id && CW(pin, "id") == document.id && CW(pin, "revision") == document.revision) return true;
	}
	return false;
}

static bool CWAssessmentCurrent(const DataStore& store, const ManagerSkylarkApp& app, const String& ws, const String& candidate_id, const ValueMap& proposal)
{
	ValueArray linked = MapGet(proposal, "evidence_links");
	Index<String> linked_ids;
	for(const Value& ref : linked) {
		if(!IsString(ref) || !CWUsableEvidence(store, app, ws, (String)ref)) return false;
		const DataDocument *item = store.FindDocument(ws + ":" + (String)ref);
		const DataDocument *snapshot = item ? store.FindDocument(ws + ":" + CW(item->data, "snapshot")) : NULL;
		if(!item || !snapshot) return false;
		linked_ids.Add(item->id); linked_ids.Add(snapshot->id);
	}
	if(linked_ids.IsEmpty()) return false;
	for(const DataDocument& assessment : store.GetDocuments()) if(assessment.valid && assessment.validated && assessment.workspace_id == ws && assessment.kind == "candidate-assessment" && CW(assessment.data, "candidate_id") == candidate_id && CW(assessment.data, "provenance") == "computed") {
		ValueMap value = MapGet(assessment.data, "assessment");
		if(CW(value, "candidate_id") != candidate_id || CW(value, "provenance") != "computed" || CW(value, "uncertainty") != "complete" || CW(value, "total_numerator").IsEmpty() || CW(value, "total_denominator").IsEmpty() || CW(value, "tier").IsEmpty()) continue;
		String run_id = CW(assessment.data, "analysis_run"); const DataDocument *run = store.FindDocument(ws + ":" + run_id);
		if(!run || !run->valid || !run->validated || run->workspace_id != ws || run->kind != "analysis-run" || CW(run->data, "status") != "configured" || CW(run->data, "provenance") != "computed") continue;
		String rubric_id = CW(run->data, "rubric_id"), rubric_revision = CW(run->data, "rubric_revision");
		const DataDocument *rubric = store.FindDocument(ws + ":" + rubric_id);
		if(!rubric || !rubric->valid || !rubric->validated || rubric->workspace_id != ws || rubric->kind != "assessment-rubric" || CW(rubric->data, "revision") != rubric_revision || CW(rubric->data, "provenance") != "authored") continue;
		ValueArray dimensions = MapGet(rubric->data, "dimensions"), assessed_dimensions = MapGet(value, "dimensions"), tiers = MapGet(rubric->data, "tiers");
		if(dimensions.GetCount() != 6 || assessed_dimensions.GetCount() != 6 || tiers.IsEmpty() || (CW(rubric->data, "score_scale") != "integer-0-5" && CW(rubric->data, "score_scale") != "integer-0-10" && CW(rubric->data, "score_scale") != "integer-1-5" && CW(rubric->data, "score_scale") != "integer-1-10") ||
		   (CW(rubric->data, "total_rule") != "weighted-sum" && CW(rubric->data, "total_rule") != "weighted-mean")) continue;
		Index<String> dimension_ids; bool dimensions_valid = true; int64 computed_n = 0, computed_d = 0;
		for(int i = 0; i < 6; i++) {
			ValueMap definition = dimensions[i], result = assessed_dimensions[i]; String dim = CW(definition, "id");
		String score_text = CW(result, "score"); int score = ScanInt(score_text), weight = (int)MapGet(definition, "weight");
		if(dim.IsEmpty() || dimension_ids.Find(dim) >= 0 || CW(result, "dimension_id") != dim || CW(result, "score_state") != "known" || CW(result, "coverage_state") != "covered" || score_text.IsEmpty() || AsString(score) != score_text || weight <= 0 || weight > 100000 || score < (int)MapGet(definition, "minimum_score") || score > (int)MapGet(definition, "maximum_score") || (int)MapGet(result, "evidence_count") < (int)MapGet(definition, "minimum_evidence_count")) { dimensions_valid = false; break; }
			dimension_ids.Add(dim);
			computed_n += (int64)score * weight; computed_d += weight;
		}
		if(!dimensions_valid) continue;
		int64 total_n = ScanInt64(CW(value, "total_numerator")), total_d = ScanInt64(CW(value, "total_denominator")); bool tier_matches = false; int matching_tiers = 0;
		if(total_d <= 0 || total_d > 1000000000LL || total_n < 0 || total_n > 1000000000LL || AsString(total_n) != CW(value, "total_numerator") || AsString(total_d) != CW(value, "total_denominator")) continue;
		if(CW(rubric->data, "total_rule") == "weighted-sum") computed_d = 1;
		if(total_n != computed_n || total_d != computed_d) continue;
		for(const Value& tier_v : tiers) if(IsValueMap(tier_v)) {
			ValueMap tier = tier_v;
			int64 min_n = (int64)MapGet(tier, "minimum_numerator"), min_d = (int64)MapGet(tier, "minimum_denominator"), max_n = (int64)MapGet(tier, "maximum_numerator"), max_d = (int64)MapGet(tier, "maximum_denominator");
			if(min_n < 0 || min_n > 1000000000LL || max_n < 0 || max_n > 1000000000LL || min_d <= 0 || min_d > 1000000000LL || max_d <= 0 || max_d > 1000000000LL) continue;
			if(CW(tier, "id") == CW(value, "tier") && total_n * min_d >= min_n * total_d && total_n * max_d < max_n * total_d) { tier_matches = true; matching_tiers++; }
		}
		if(!tier_matches || matching_tiers != 1) continue;
		bool stale = false;
		for(const DataDocument& marker : store.GetDocuments()) if(marker.workspace_id == ws && marker.kind == "assessment-staleness" && marker.valid && marker.validated && CW(marker.data, "prior_run_id") == run_id) stale = true;
		ValueArray pins = MapGet(run->data, "input_revisions");
		if(pins.IsEmpty()) stale = true;
		for(const String& id : linked_ids) {
			const DataDocument *linked_doc = store.FindDocument(ws + ":" + id);
			if(!linked_doc || !CWHasRevisionPin(pins, *linked_doc)) { stale = true; break; }
		}
		Index<String> seen_pins;
		for(const Value& pin_v : pins) {
			if(!IsValueMap(pin_v)) { stale = true; break; }
			ValueMap pin = pin_v; if(CW(pin, "workspace_id") != ws || CW(pin, "id").IsEmpty() || CW(pin, "revision").IsEmpty()) { stale = true; break; }
			String pin_key = CW(pin, "workspace_id") + ":" + CW(pin, "id"); if(seen_pins.Find(pin_key) >= 0) { stale = true; break; } seen_pins.Add(pin_key);
			bool resolved = false;
			const DataDocument *input = store.FindDocument(ws + ":" + CW(pin, "id"));
			if(input && input->valid && input->validated && input->workspace_id == ws && input->revision == CW(pin, "revision")) {
				if(input->kind == "evidence-item") resolved = CWUsableEvidence(store, app, ws, input->id);
				else if(input->kind == "evidence-snapshot") resolved = CW(input->data, "resolution") == "resolved" && CW(input->data, "visibility") == "visible" && app.IsAssessmentSourceVisible(ws, CW(input->data, "source_identity"));
				else resolved = true;
			}
			if(!resolved) for(const DataDocument& d : store.GetDocuments()) if(d.valid && d.validated && d.workspace_id == ws && d.kind == "evidence-snapshot" && CW(d.data, "source_identity") == CW(pin, "id") && CW(d.data, "upstream_revision") == CW(pin, "revision") && CW(d.data, "resolution") == "resolved" && CW(d.data, "visibility") == "visible" && app.IsAssessmentSourceVisible(ws, CW(d.data, "source_identity"))) resolved = true;
			if(!resolved) { stale = true; break; }
		}
		if(!stale) return true;
	}
	return false;
}

bool CandidateWorkflow::HasCurrentProposalAssessment(const DataStore& store, const ManagerSkylarkApp& app,
	const String& workspace_id, const String& candidate_id, const ValueMap& current_proposal)
{
	return CWAssessmentCurrent(store, app, workspace_id, candidate_id, current_proposal);
}

String CandidateWorkflow::ComparisonAssessmentBlocker(const DataStore& store, const ManagerSkylarkApp& app,
	const String& workspace_id, const String& candidate_id, const ValueMap& current_proposal)
{
	return CWAssessmentCurrent(store, app, workspace_id, candidate_id, current_proposal) ? String() : String("candidate_assessment_missing_stale_or_unpinned_to_current_proposal_evidence");
}

static bool CWHasLinkedCommitDiff(const DataStore& store, const ManagerSkylarkApp& app, const String& ws, const ValueMap& proposal)
{
	for(const Value& ref : ValueArray(MapGet(proposal, "evidence_links"))) if(IsString(ref) && CWUsableEvidence(store, app, ws, (String)ref, "commit-diff")) return true;
	return false;
}

bool CandidateWorkflow::HasLinkedCommitDiff(const DataStore& store, const ManagerSkylarkApp& app,
	const String& workspace_id, const ValueMap& proposal)
{
	return CWHasLinkedCommitDiff(store, app, workspace_id, proposal);
}

ValueMap CandidateWorkflow::CheckStagePrerequisite(const DataStore& store, const ManagerSkylarkApp& app,
	const String& ws, const String& stage, const ValueMap& proposal, const ValueArray& evidence_ids,
	const String& review_outcome, bool projection)
{
	if(!CWStage(stage) || stage == "draft") return CWResult("invalid", "invalid_stage_prerequisite");
	ValueArray links = MapGet(proposal, "evidence_links");
	if(links.GetCount() > 64 || evidence_ids.GetCount() > 64) return CWResult("invalid", "stage_evidence_limit_exceeded");
	for(const Value& ref : links) if(!IsString(ref) || ((String)ref).GetCount() > 256 || !CWUsableEvidence(store, app, ws, (String)ref)) return CWResult("denied");
	if(stage == "spike-verified") return CWResult("blocked", "completed_job_run_artifact_kind_unavailable_until_t19");
	if(stage == "verifier-feasible") return CWResult("blocked", "verification_plan_capability_schema_unavailable_until_t20");
	if(stage == "comparison-ready") {
		if(!projection && !evidence_ids.IsEmpty()) return CWResult("invalid", "comparison_stage_does_not_accept_evidence_ids");
		String blocker = ComparisonAssessmentBlocker(store, app, ws, CW(proposal, "candidate_id"), proposal);
		return blocker.IsEmpty() ? CWResult("ready") : CWResult("blocked", blocker);
	}
	if(projection) {
		if(stage == "deliverables-ready") return CWResult(CWHasLinkedCommitDiff(store, app, ws, proposal) ? "ready" : "blocked", CWHasLinkedCommitDiff(store, app, ws, proposal) ? String() : String("deliverables_require_linked_commit_diff_snapshot_with_valid_locator"));
		for(const Value& ref : links) if(IsString(ref) && CWUsableEvidence(store, app, ws, (String)ref)) return CWResult("ready", stage == "source-inspected" ? "proposal_linked_evidence_available" : "proposal_linked_citations_available");
		return CWResult("blocked", stage == "source-inspected" ? "source_inspection_requires_visible_proposal_evidence" : "duplicate_review_requires_visible_proposal_citations");
	}
	if(evidence_ids.IsEmpty()) return CWResult("denied");
	Index<String> unique;
	bool has_commit_diff = false;
	for(const Value& ref : evidence_ids) {
		if(!IsString(ref) || ((String)ref).IsEmpty() || ((String)ref).GetCount() > 256 || unique.Find((String)ref) >= 0) return CWResult("invalid", "invalid_or_duplicate_evidence_id");
		unique.Add((String)ref);
		if(!CWUsableEvidence(store, app, ws, (String)ref)) return CWResult("denied");
		if(!CWHasLink(links, (String)ref)) return CWResult("blocked", stage == "deliverables-ready" ? "deliverable_evidence_not_linked_to_current_proposal" : "evidence_not_linked_to_current_proposal_revision");
		if(stage == "deliverables-ready" && CWUsableEvidence(store, app, ws, (String)ref, "commit-diff")) has_commit_diff = true;
	}
	if(stage == "duplicate-reviewed" && review_outcome != "no_duplicate" && review_outcome != "duplicate_found" && review_outcome != "uncertain") return CWResult("invalid", "duplicate_review_outcome_required");
	if(stage != "duplicate-reviewed" && !review_outcome.IsEmpty()) return CWResult("invalid", "unexpected_review_outcome");
	if(stage == "deliverables-ready" && !has_commit_diff) return CWResult("blocked", "deliverables_require_linked_commit_diff_snapshot_with_valid_locator");
	return CWResult("ready");
}

ValueMap CandidateWorkflow::ValidateProposal(const DataStore& store, const ValueMap& in, const String& ws, const ManagerSkylarkApp& app)
{
	static const char *keys[] = { "candidate_kind", "title", "purpose", "quality_contract", "inputs", "outputs", "runtime_resources", "assets", "verification_plan", "evidence_links", "hypothetical", "counterevidence", "uncertainty" };
	ValueMap out; out.Add("status", "invalid");
	if(ws.IsEmpty() || in.GetCount() > (int)__countof(keys) || !CWOnly(in, keys, __countof(keys))) { out.Add("reason", "unknown_or_missing_form_fields"); return out; }
	String ck = CW(in, "candidate_kind");
	if(ck != "candidate" && ck != "proposal" && ck != "research-stage") { out.Add("reason", "invalid_candidate_kind"); return out; }
	for(const char *key : { "title", "purpose", "quality_contract", "inputs", "outputs", "runtime_resources", "assets", "verification_plan" })
		if(!CWText(in, key, MAX_TEXT_BYTES)) { out.Add("reason", String("invalid_required_field:") + key); return out; }
	Value links_v = MapGet(in, "evidence_links");
	if(!links_v.Is<ValueArray>() || ValueArray(links_v).IsEmpty() || ValueArray(links_v).GetCount() > 64) { out.Add("reason", "invalid_or_missing_evidence_links"); return out; }
	ValueArray links = links_v; Index<String> unique_links;
	for(const Value& v : links) {
		if(!IsString(v) || ((String)v).IsEmpty() || ((String)v).GetCount() > 256 || unique_links.Find((String)v) >= 0) { out.Add("reason", "invalid_or_duplicate_evidence_id"); return out; }
		unique_links.Add((String)v);
		if(!CWUsableEvidence(store, app, ws, (String)v)) { out.Add("reason", "blocked_unresolved_or_hidden_evidence"); return out; }
	}
	bool hypothetical = MapGet(in, "hypothetical") == true;
	if(!MapGet(in, "hypothetical").IsVoid() && !MapGet(in, "hypothetical").Is<bool>()) { out.Add("reason", "invalid_hypothesis_flag"); return out; }
	if(hypothetical) {
		Value ce = MapGet(in, "counterevidence");
		if(!ce.Is<ValueArray>() || ValueArray(ce).IsEmpty() || ValueArray(ce).GetCount() > 64 || !CWText(in, "uncertainty", MAX_TEXT_BYTES)) { out.Add("reason", "hypothesis_requires_counterevidence_and_uncertainty"); return out; }
		Index<String> unique_counterevidence;
		for(const Value& v : ValueArray(ce)) {
			if(!IsString(v) || ((String)v).IsEmpty() || ((String)v).GetCount() > 256 || unique_counterevidence.Find((String)v) >= 0) { out.Add("reason", "invalid_or_duplicate_counterevidence_id"); return out; }
			unique_counterevidence.Add((String)v);
			if(!CWUsableEvidence(store, app, ws, (String)v)) { out.Add("reason", "hypothesis_counterevidence_unresolved"); return out; }
		}
	}
	else if(!MapGet(in, "counterevidence").IsVoid() || !MapGet(in, "uncertainty").IsVoid()) { out.Add("reason", "unexpected_hypothesis_fields"); return out; }
	out.Set("status", "valid"); out.Add("candidate_kind", ck); out.Add("evidence_links", links); return out;
}

ValueMap CandidateWorkflow::Project(const DataStore& store, const ManagerSkylarkApp& app, const String& ws, const String& candidate_id)
{
	if(ws.IsEmpty() || candidate_id.IsEmpty() || store.GetLoadStatus() != "loaded") return CWResult("denied");
	const DataDocument *candidate = store.FindDocument(ws + ":" + candidate_id);
	if(!candidate || !candidate->valid || !candidate->validated || candidate->workspace_id != ws ||
	   (candidate->kind != "local-candidate" && candidate->kind != "local-interpretation")) return CWResult("denied");
	Index<String> initial_proposals;
	for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id == ws && d.kind == "candidate-proposal" && MapGetString(d.data, "candidate_id") == candidate_id) {
		const DataDocument *revision = store.FindDocument(ws + ":" + MapGetString(d.data, "proposal_revision_id"));
		if(revision && revision->valid && revision->validated && revision->kind == "proposal-revision" && initial_proposals.Find(CW(revision->data, "proposal_id")) < 0) initial_proposals.Add(CW(revision->data, "proposal_id"));
	}
	if(initial_proposals.GetCount() != 1) return CWResult("denied");
	String initial_proposal_id = initial_proposals[0];
	const DataDocument *initial_proposal_head = store.FindDocument(ws + ":candidate-proposal-head-" + DataStore::ContentDigest(ws + "\n" + initial_proposal_id).Left(32));
	String current_proposal_revision_id = initial_proposal_head ? CW(initial_proposal_head->data, "proposal_revision_id") : String();
	if(!initial_proposal_head || !initial_proposal_head->valid || !initial_proposal_head->validated || CW(initial_proposal_head->data, "candidate_id") != candidate_id || current_proposal_revision_id.IsEmpty()) return CWResult("denied");
	ValueArray history, proposal_history, current_proposals; String head_stage = "draft", head_event, head_revision = "0"; bool have_head = false; const DataDocument *head_doc = NULL;
	for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id == ws &&
	   (d.kind == "candidate-proposal" || d.kind == "candidate-stage-event" || d.kind == "candidate-stage-head") && MapGetString(d.data, "candidate_id") == candidate_id) {
		if((d.kind == "candidate-stage-event" || d.kind == "candidate-stage-head") && CW(d.data, "proposal_revision_id") != current_proposal_revision_id) continue;
		if(d.kind == "candidate-stage-event") {
			Value refs = MapGet(d.data, "evidence_ids");
			if(!refs.Is<ValueArray>()) return CWResult("denied");
			for(const Value& ref : ValueArray(refs)) if(!IsString(ref) || !CWUsableEvidence(store, app, ws, (String)ref)) return CWResult("denied");
		}
		if(d.kind == "candidate-proposal") {
			String source = MapGetString(d.data, "source_identity");
			if(source.IsEmpty() || !app.IsAssessmentSourceVisible(ws, source)) return CWResult("denied");
			for(const char *field : { "evidence_links", "counterevidence" }) {
				Value refs = MapGet(d.data, field); if(refs.IsVoid()) continue;
				if(!refs.Is<ValueArray>()) return CWResult("denied");
				for(const Value& ref : ValueArray(refs)) if(!IsString(ref) || !CWUsableEvidence(store, app, ws, (String)ref)) return CWResult("denied");
			}
		}
			if(!d.valid || !d.validated) return CWResult("denied");
		if(d.kind == "candidate-stage-event") history.Add(d.data);
		if(d.kind == "candidate-stage-head") { if(have_head) return CWResult("denied"); have_head = true; head_doc = &d; head_stage = CW(d.data, "stage"); head_event = CW(d.data, "event_id"); head_revision = d.revision; }
	}
	ValueMap head_envelope; if(head_doc) head_envelope = MapGet(head_doc->data, "envelope");
	if(!have_head || head_event.IsEmpty() || !head_doc || !CWStage(head_stage) || CW(head_doc->data, "id") != CWStageHeadId(ws, candidate_id, current_proposal_revision_id) || CW(head_doc->data, "proposal_revision_id") != current_proposal_revision_id || CW(head_doc->data, "workspace_id") != ws ||
	   CW(head_envelope, "stable_id") != head_doc->id || CW(head_envelope, "workspace_id") != ws || CW(head_envelope, "revision") != head_doc->revision || CW(head_doc->data, "revision") != head_doc->revision) return CWResult("denied");
	Index<String> event_ids, operation_ids, chain_ids; VectorMap<String, ValueMap> events;
	for(const Value& v : history) { ValueMap e = v; String id = CW(e, "id"), op = CW(e, "operation_id"), stage = CW(e, "stage");
		String expected_id = CWStageEventId(op, candidate_id, current_proposal_revision_id, stage);
		if(id != expected_id || event_ids.Find(id) >= 0 || op.IsEmpty() || operation_ids.Find(op) >= 0 || CW(e, "workspace_id") != ws || CW(e, "candidate_id") != candidate_id || CW(e, "candidate_revision") != candidate->revision || CW(e, "proposal_revision_id") != current_proposal_revision_id || !CWOperation(op) || !CWStage(stage) || !MapGet(e, "evidence_ids").Is<ValueArray>() ||
		   TrimBoth(CW(e, "reason")).IsEmpty() || CW(e, "reason").GetCount() > 1024 || CW(e, "actor").IsEmpty() || CW(e, "actor").GetCount() > 256 || CW(e, "authority") != "authenticated-admin" || !CWTimestamp(CW(e, "timestamp")) ||
		   (stage == "duplicate-reviewed" ? (CW(e, "review_outcome") != "no_duplicate" && CW(e, "review_outcome") != "duplicate_found" && CW(e, "review_outcome") != "uncertain") : !MapGet(e, "review_outcome").IsVoid())) return CWResult("denied");
		event_ids.Add(id); operation_ids.Add(op); events.Add(id, e);
	}
	String cursor = head_event; int stage_index = -1; for(int i = 0; i < (int)__countof(CW_STAGES); i++) if(head_stage == CW_STAGES[i]) stage_index = i;
	while(cursor != "none") {
		int ei = events.Find(cursor); if(ei < 0 || chain_ids.Find(cursor) >= 0 || chain_ids.GetCount() >= (int)__countof(CW_STAGES)) return CWResult("denied");
		ValueMap e = events[ei]; int idx = -1; for(int i = 0; i < (int)__countof(CW_STAGES); i++) if(CW(e, "stage") == CW_STAGES[i]) idx = i;
		if(idx != stage_index || CW(e, "id") != cursor || CW(e, "operation_id").IsEmpty() || !CWOperation(CW(e, "operation_id")) ||
		   (idx == 0 ? CW(e, "previous_event_id") != "none" : CW(e, "previous_event_id").IsEmpty() || CW(e, "previous_event_id") == "none")) return CWResult("denied");
		ValueArray refs = MapGet(e, "evidence_ids"); Index<String> unique_refs;
		for(const Value& ref : refs) { if(!IsString(ref) || !CWUsableEvidence(store, app, ws, (String)ref)) return CWResult("denied"); if(unique_refs.Find((String)ref) >= 0) return CWResult("denied"); unique_refs.Add((String)ref); }
		chain_ids.Add(cursor); cursor = CW(e, "previous_event_id"); stage_index--;
	}
	if(chain_ids.GetCount() != event_ids.GetCount() || chain_ids.IsEmpty() || CW(events.Find(head_event) >= 0 ? events[events.Find(head_event)] : ValueMap(), "stage") != head_stage) return CWResult("denied");
	ValueArray ordered_history; for(int i = chain_ids.GetCount() - 1; i >= 0; i--) ordered_history.Add(events[events.Find(chain_ids[i])]); history = ordered_history;
	Index<String> proposal_ids;
	for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id == ws && d.kind == "candidate-proposal" && MapGetString(d.data, "candidate_id") == candidate_id) {
		if(!d.valid || !d.validated) return CWResult("denied");
		String source = MapGetString(d.data, "source_identity"); if(source.IsEmpty() || !app.IsAssessmentSourceVisible(ws, source)) return CWResult("denied");
		String revision = MapGetString(d.data, "proposal_revision_id");
		const DataDocument *rev = store.FindDocument(ws + ":" + revision);
		if(!rev || !rev->valid || !rev->validated || rev->kind != "proposal-revision" || MapGetString(rev->data, "candidate_id") != candidate_id) return CWResult("denied");
		String proposal = MapGetString(rev->data, "proposal_id");
		if(proposal_ids.Find(proposal) < 0) proposal_ids.Add(proposal);
	}
	for(const String& proposal : proposal_ids) {
		String expected_head_id = "candidate-proposal-head-" + DataStore::ContentDigest(ws + "\n" + proposal).Left(32);
		const DataDocument *proposal_head = store.FindDocument(ws + ":" + expected_head_id);
		ValueMap proposal_head_env; if(proposal_head) proposal_head_env = MapGet(proposal_head->data, "envelope");
		if(!proposal_head || !proposal_head->valid || !proposal_head->validated || MapGetString(proposal_head->data, "id") != expected_head_id || MapGetString(proposal_head->data, "proposal_id") != proposal || MapGetString(proposal_head->data, "candidate_id") != candidate_id ||
		   MapGetString(proposal_head->data, "workspace_id") != ws || CW(proposal_head_env, "stable_id") != expected_head_id || CW(proposal_head_env, "workspace_id") != ws || CW(proposal_head_env, "revision") != proposal_head->revision) return CWResult("denied");
		Index<String> all_revisions;
		VectorMap<String, int> parent_counts;
		int root_count = 0;
		for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id == ws && d.kind == "proposal-revision" && MapGetString(d.data, "proposal_id") == proposal) {
			if(!d.valid || !d.validated || MapGetString(d.data, "candidate_id") != candidate_id || all_revisions.Find(d.id) >= 0) return CWResult("denied");
			all_revisions.Add(d.id);
			String parent = MapGetString(d.data, "parent_revision_id");
			if(parent.IsEmpty()) { root_count++; parent = "<root>"; }
			int pi = parent_counts.Find(parent);
			if(pi < 0) parent_counts.Add(parent, 1); else if(++parent_counts[pi] > 1) return CWResult("denied");
		}
		if(root_count != 1 || all_revisions.IsEmpty()) return CWResult("denied");
		Index<String> chain_ids, parent_ids; String current = MapGetString(proposal_head->data, "proposal_revision_id");
		for(int depth = 0; depth < 128; depth++) {
			if(current.IsEmpty() || chain_ids.Find(current) >= 0) return CWResult("denied");
			chain_ids.Add(current);
			const DataDocument *rev = store.FindDocument(ws + ":" + current);
			if(!rev || !rev->valid || !rev->validated || rev->kind != "proposal-revision" || MapGetString(rev->data, "proposal_id") != proposal || MapGetString(rev->data, "candidate_id") != candidate_id) return CWResult("denied");
			String parent = MapGetString(rev->data, "parent_revision_id");
			if(parent.IsEmpty()) { if(CWRevisionNumber(MapGetString(rev->data, "revision_id")) != 1) return CWResult("denied"); break; }
			if(parent_ids.Find(parent) >= 0) return CWResult("denied");
			const DataDocument *parent_record = store.FindDocument(ws + ":" + parent);
			if(!parent_record || CWRevisionNumber(MapGetString(rev->data, "revision_id")) != CWRevisionNumber(MapGetString(parent_record->data, "revision_id")) + 1) return CWResult("denied");
			parent_ids.Add(parent); current = parent;
			if(depth == 127) return CWResult("denied");
		}
		if(chain_ids.GetCount() == 0 || chain_ids.GetCount() != all_revisions.GetCount() || MapGetString(proposal_head->data, "proposal_revision_id") != chain_ids[0] || chain_ids[0] != current_proposal_revision_id ||
		   MapGetString(proposal_head->data, "revision_id") != MapGetString(store.FindDocument(ws + ":" + chain_ids[0])->data, "revision_id")) return CWResult("denied");
		String current_revision = MapGetString(proposal_head->data, "proposal_revision_id");
		const DataDocument *current_snapshot = NULL;
		for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id == ws && d.kind == "candidate-proposal" &&
		   MapGetString(d.data, "candidate_id") == candidate_id && MapGetString(d.data, "proposal_revision_id") == current_revision) {
			const DataDocument *revision = store.FindDocument(ws + ":" + current_revision);
			if(!revision || MapGetString(revision->data, "proposal_id") != proposal) continue;
			if(current_snapshot) return CWResult("denied");
			current_snapshot = &d;
		}
		if(!current_snapshot || !current_snapshot->valid || !current_snapshot->validated) return CWResult("denied");
		ValueMap current_proposal; current_proposal.Add("proposal_id", proposal); current_proposal.Add("head_revision", proposal_head->revision);
		current_proposal.Add("revision_id", current_revision); current_proposal.Add("snapshot", current_snapshot->data); current_proposals.Add(current_proposal);
		for(int i = chain_ids.GetCount() - 1; i >= 0; i--) proposal_history.Add(chain_ids[i]);
	}
	if(current_proposals.GetCount() != 1) return CWResult("denied");
	ValueMap current_form = MapGet((ValueMap)current_proposals[0], "snapshot");
	ValueArray current_links = MapGet(current_form, "evidence_links");
	for(const Value& item : history) {
		ValueMap event = item; String stage = CW(event, "stage"); ValueArray refs = MapGet(event, "evidence_ids");
		for(const Value& ref : refs) if(!IsString(ref) || !CWHasLink(current_links, (String)ref)) return CWResult("denied");
		if(stage != "draft") { ValueMap check = CheckStagePrerequisite(store, app, ws, stage, current_form, refs, CW(event, "review_outcome"), false); if(CW(check, "status") != "ready") return CWResult("denied"); }
	}
	ValueArray prerequisites;
	int current_stage_index = 0; for(int i = 0; i < (int)__countof(CW_STAGES); i++) if(head_stage == CW_STAGES[i]) current_stage_index = i;
	for(int i = 0; i < 6; i++) {
		ValueMap row; row.Add("stage", CW_STAGES[i + 1]); row.Add("status", i + 1 <= current_stage_index ? "complete" : "blocked");
		String reason;
		if(i + 1 <= current_stage_index) reason = "stage_event_complete";
		if(i + 1 > current_stage_index + 1) {
			reason = String("requires_prior_stage_complete:") + CW_STAGES[i];
			if(i == 2) reason << ";blocked_until_trusted_t19_job_run_schema_and_successful_measurement";
			if(i == 4) reason << ";blocked_until_trusted_t20_verification_capability_schema";
		}
		if(i + 1 == current_stage_index + 1) {
			ValueMap gate = CheckStagePrerequisite(store, app, ws, CW_STAGES[i + 1], current_form, ValueArray(), String(), true);
			row.Set("status", CW(gate, "status")); reason = CW(gate, "reason");
			if(i == 1 && CW(gate, "status") == "ready") reason = "explicit_review_outcome_required_on_transition";
		}
		row.Add("reason", reason); prerequisites.Add(row);
	}
	ValueMap out; out.Add("status", "visible"); out.Add("workspace_id", ws); out.Add("candidate_id", candidate_id);
	out.Add("candidate_revision", candidate->revision); out.Add("proposal_revision_id", current_proposal_revision_id); out.Add("stage", head_stage); out.Add("head_revision", head_revision); out.Add("head_event_id", head_event);
	out.Add("history", history); out.Add("prerequisites", prerequisites); out.Add("hypothetical", false);
	for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id == ws && d.kind == "candidate-proposal" && MapGetString(d.data, "candidate_id") == candidate_id && d.valid && d.validated) {
		if(MapGet(d.data, "hypothetical") == true) out.Set("hypothetical", true);
		ValueArray refs = MapGet(d.data, "evidence_links");
		if(refs.IsEmpty()) return CWResult("denied");
		for(const Value& ref : refs) if(!IsString(ref) || !CWUsableEvidence(store, app, ws, (String)ref)) return CWResult("denied");
		ValueArray counterevidence = MapGet(d.data, "counterevidence");
		for(const Value& ref : counterevidence) if(!IsString(ref) || !CWUsableEvidence(store, app, ws, (String)ref)) return CWResult("denied");
	}
	out.Add("proposal_revision_chain", proposal_history); out.Add("current_proposals", current_proposals);
	return out;
}

ValueMap CandidateWorkflow::CreateProposal(const DataStore& store, const ManagerSkylarkApp& app, const ValueMap& in, const String& ws,
	const String& source, const String& operation, const UserSession& session)
{
	if(!SystemAuthService::AuthorizeWriteForSession(const_cast<ManagerSkylarkApp&>(app), session, source, ws)) return CWResult("denied", "write_authorization_denied");
	if(store.GetLoadStatus() != "loaded" || store.GetSourceId() != source || !CWOperation(operation)) return CWResult("denied", "write_authorization_denied");
	ValueMap valid = ValidateProposal(store, in, ws, app); if(CW(valid, "status") != "valid") return valid;
	String op_key = ws + "\n" + source + "\n" + operation;
	String cid_key = "candidate-" + DataStore::ContentDigest(op_key).Left(32);
	String pid = "proposal-" + DataStore::ContentDigest(op_key).Left(32);
	String kind = CW(in, "candidate_kind"), cid;
	bool hypothetical = MapGet(in, "hypothetical") == true;
	ValueMap a; a.Add("stable_id", cid_key); a.Add("kind", hypothetical ? "local-interpretation" : "local-candidate");
	if(hypothetical) { a.Add("evidence", MapGet(in, "evidence_links")); a.Add("counterevidence", MapGet(in, "counterevidence")); }
	else a.Add("candidate_kind", kind);
	a.Add("provenance", "authenticated-local-draft"); a.Add("uncertainty", hypothetical ? CW(in, "uncertainty") : "unassessed");
	ValueArray assertions; assertions.Add(a); ValueMap lineage_input; lineage_input.Add("workspace_id", ws); lineage_input.Add("assertions", assertions);
	ValueArray first_record = CandidateLineage::BuildRecords(lineage_input); if(first_record.GetCount() != 1) return CWResult("invalid", "candidate_record_build_failed");
	cid = MapGetString((ValueMap)first_record[0], "id");
	ValueMap p; p.Add("stable_id", pid + "-r1"); p.Add("kind", "proposal-revision"); p.Add("proposal_id", pid); p.Add("revision_id", "r1"); p.Add("candidate_id", cid); p.Add("provenance", "authenticated-local-draft"); p.Add("uncertainty", hypothetical ? CW(in, "uncertainty") : "unassessed"); p.Add("hypothetical", hypothetical);
	if(hypothetical) { p.Add("evidence", MapGet(in,"evidence_links")); p.Add("counterevidence", MapGet(in,"counterevidence")); }
	assertions.Add(p); lineage_input.Set("assertions", assertions);
	ValueArray records = CandidateLineage::BuildRecords(lineage_input); if(records.GetCount() != 2) return CWResult("invalid", "lineage_record_build_failed");
	ValueMap form = CWBase("candidate-proposal", "candidate-proposal-" + DataStore::ContentDigest(ws + "\n" + pid + "\nr1").Left(32), ws);
	String evidence_source;
	if(!MapGet(in, "evidence_links").IsNull() && !ValueArray(MapGet(in, "evidence_links")).IsEmpty()) {
		const DataDocument *evidence = store.FindDocument(ws + ":" + (String)ValueArray(MapGet(in, "evidence_links"))[0]);
		if(evidence) evidence_source = MapGetString(evidence->data, "source_identity");
	}
	form.Add("candidate_id", cid); form.Add("proposal_revision_id", MapGetString((ValueMap)records[1], "id")); form.Add("candidate_kind", kind); form.Add("source_identity", evidence_source);
	for(const char *key : { "title", "purpose", "quality_contract", "inputs", "outputs", "runtime_resources", "assets", "verification_plan", "evidence_links", "counterevidence", "uncertainty" }) if(!MapGet(in, key).IsVoid()) form.Add(key, MapGet(in, key));
	form.Add("draft_state", "draft"); form.Set("hypothetical", hypothetical); form.Add("submission_state", hypothetical ? "not-a-real-submission" : "not-submitted"); form.Add("approval_state", "not-approved"); form.Add("author_state", "unknown"); form.Add("intent_state", "unknown");
	ValueMap ph = CWBase("candidate-proposal-head", "candidate-proposal-head-" + DataStore::ContentDigest(ws + "\n" + pid).Left(32), ws);
	ph.Add("candidate_id", cid); ph.Add("proposal_id", pid); ph.Add("proposal_revision_id", MapGetString((ValueMap)records[1], "id")); ph.Add("revision_id", "r1");
	String proposal_revision_id = CW((ValueMap)records[1], "id");
	ValueMap se = CWBase("candidate-stage-event", CWStageEventId(operation, cid, proposal_revision_id, "draft"), ws);
	se.Add("candidate_id", cid); se.Add("candidate_revision", "1"); se.Add("proposal_revision_id", proposal_revision_id); se.Add("stage", "draft"); se.Add("previous_event_id", "none"); se.Add("reason", "Initial local draft"); se.Add("evidence_ids", ValueArray()); se.Add("operation_id", operation);
	se.Add("actor", session.username); se.Add("authority", "authenticated-admin"); se.Add("timestamp", AsString(session.created_at));
	ValueMap sh = CWBase("candidate-stage-head", CWStageHeadId(ws, cid, proposal_revision_id), ws);
	sh.Add("candidate_id", cid); sh.Add("proposal_revision_id", proposal_revision_id); sh.Add("stage", "draft"); sh.Add("event_id", CW(se, "id"));
	Vector<DataBatchChange> changes;
	for(const Value& v : records) { ValueMap r = v; DataBatchChange c; c.action = DATA_BATCH_CREATE; c.qualified_id = ws + ":" + CW(r, "id"); c.proposed = r; changes.Add(c); }
	for(const ValueMap& r : { form, ph, se, sh }) { DataBatchChange c; c.action = DATA_BATCH_CREATE; c.qualified_id = ws + ":" + CW(r, "id"); c.proposed = r; changes.Add(c); }
	ManagerSkylarkApp::BatchFinalizationResult result = SystemAuthService::FinalizeBatchForSession(const_cast<ManagerSkylarkApp&>(app), session, source, ws, operation, changes);
	ValueMap out = CWResult(result.status); out.Add("changed_ids", result.changed_ids.GetCount()); if(result.committed || result.status == "already_committed") { out.Add("candidate_id", cid); out.Add("proposal_id", pid); out.Add("revision_id", MapGetString((ValueMap)records[1], "id")); } return out;
}

ValueMap CandidateWorkflow::ReviseProposal(const DataStore& store, const ManagerSkylarkApp& app,
	const String& ws, const String& cid, const String& pid, const String& expected_head_revision,
	const String& expected_revision_id, const ValueMap& in, const String& source, const String& operation,
	const UserSession& session)
{
	if(!SystemAuthService::AuthorizeWriteForSession(const_cast<ManagerSkylarkApp&>(app), session, source, ws)) return CWResult("denied", "write_authorization_denied");
	if(store.GetLoadStatus() != "loaded" || store.GetSourceId() != source || !CWOperation(operation)) return CWResult("denied", "write_authorization_denied");
	ValueMap valid = ValidateProposal(store, in, ws, app); if(CW(valid, "status") != "valid") return valid;
	const DataDocument *candidate = store.FindDocument(ws + ":" + cid);
	const DataDocument *head = store.FindDocument(ws + ":candidate-proposal-head-" + DataStore::ContentDigest(ws + "\n" + pid).Left(32));
	if(!candidate || !candidate->valid || !candidate->validated || !head || !head->valid || !head->validated ||
	   MapGetString(head->data, "candidate_id") != cid || MapGetString(head->data, "proposal_id") != pid ||
	   (candidate->kind != "local-candidate" && candidate->kind != "local-interpretation")) return CWResult("stale", "proposal_head_or_candidate_changed");
	bool hypothetical_candidate = candidate->kind == "local-interpretation";
	if(hypothetical_candidate && (MapGetString(candidate->data, "origin_kind") != "local-hypothesis" ||
	   MapGetString(candidate->data, "owner_kind") != "local-unknown-author" || MapGetString(candidate->data, "submission_state") != "not-a-real-submission" ||
	   MapGetString(candidate->data, "approval_state") != "not-approved" || MapGetString(candidate->data, "author_state") != "unknown" || MapGetString(candidate->data, "intent_state") != "unknown" || MapGet(in, "hypothetical") != true)) return CWResult("invalid", "hypothetical_identity_mismatch");
	if(!hypothetical_candidate && (MapGetString(candidate->data, "origin_kind") != "locally-authored" ||
	   MapGetString(candidate->data, "owner_kind") != "local-author" || MapGet(in, "hypothetical") == true ||
	   MapGetString(candidate->data, "candidate_kind") != CW(in, "candidate_kind"))) return CWResult("invalid", "candidate_identity_mismatch");
	String current_revision_id = MapGetString(head->data, "proposal_revision_id");
	const DataDocument *current_form = NULL;
		for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id == ws && d.kind == "candidate-proposal" &&
		   MapGetString(d.data, "candidate_id") == cid && MapGetString(d.data, "proposal_revision_id") == current_revision_id) {
		if(current_form) return CWResult("invalid", "ambiguous_current_proposal_snapshot");
		current_form = &d;
	}
	if(!current_form || !current_form->valid || !current_form->validated || MapGetString(current_form->data, "proposal_revision_id") != current_revision_id ||
	   MapGetString(current_form->data, "candidate_kind") != CW(in, "candidate_kind") ||
	   MapGetString(current_form->data, "submission_state") != (hypothetical_candidate ? "not-a-real-submission" : "not-submitted") || MapGetString(current_form->data, "approval_state") != "not-approved" ||
	   MapGetString(current_form->data, "author_state") != "unknown" || MapGetString(current_form->data, "intent_state") != "unknown" ||
	   (hypothetical_candidate && MapGet(current_form->data, "hypothetical") != true) ||
		   (!hypothetical_candidate && MapGet(current_form->data, "hypothetical") == true)) return CWResult("invalid", "current_proposal_origin_mismatch");
	const DataDocument *prior = store.FindDocument(ws + ":" + expected_revision_id);
	if(!prior || !prior->valid || !prior->validated || prior->kind != "proposal-revision" || MapGetString(prior->data, "proposal_id") != pid || MapGetString(prior->data, "candidate_id") != cid) return CWResult("stale", "proposal_revision_unavailable");
	if(MapGet(prior->data, "hypothetical") != hypothetical_candidate || MapGetString(prior->data, "origin_kind") != (hypothetical_candidate ? "local-hypothesis" : "locally-authored") ||
	   MapGetString(prior->data, "submission_state") != (hypothetical_candidate ? "not-a-real-submission" : "not-submitted") || MapGetString(prior->data, "approval_state") != "not-approved" ||
	   MapGetString(prior->data, "author_state") != (hypothetical_candidate ? "unknown" : "known-local-author") || MapGetString(prior->data, "intent_state") != "unknown") return CWResult("invalid", "proposal_revision_origin_mismatch");
	int revision_number = CWRevisionNumber(MapGetString(prior->data, "revision_id"));
	if(MapGetString(head->data, "proposal_revision_id") == expected_revision_id && MapGetString(head->data, "revision_id") != MapGetString(prior->data, "revision_id")) return CWResult("invalid", "proposal_head_revision_mismatch");
	if(revision_number < 1 || revision_number >= 9999) return CWResult("invalid", "proposal_revision_limit");
	String revision_id = Format("r%d", revision_number + 1);
	String stable = pid + "-" + revision_id + "-" + DataStore::ContentDigest(operation).Left(12);
	ValueMap assertion; assertion.Add("stable_id", stable); assertion.Add("kind", "proposal-revision"); assertion.Add("proposal_id", pid); assertion.Add("revision_id", revision_id); assertion.Add("candidate_id", cid); assertion.Add("parent_revision_id", expected_revision_id); assertion.Add("provenance", "authenticated-local-draft"); assertion.Add("uncertainty", hypothetical_candidate ? CW(in, "uncertainty") : "unassessed"); assertion.Add("hypothetical", hypothetical_candidate);
	if(hypothetical_candidate) { assertion.Add("evidence", MapGet(in,"evidence_links")); assertion.Add("counterevidence", MapGet(in,"counterevidence")); }
	ValueArray assertions; assertions.Add(assertion); ValueMap spec; spec.Add("workspace_id", ws); spec.Add("assertions", assertions); ValueArray built = CandidateLineage::BuildRecords(spec);
	if(built.GetCount() != 1) return CWResult("invalid", "proposal_revision_build_failed");
	ValueMap revision = built[0];
	ValueMap form = CWBase("candidate-proposal", "candidate-proposal-" + DataStore::ContentDigest(ws + "\n" + pid + "\n" + revision_id).Left(32), ws);
	String evidence_source;
	if(!MapGet(in, "evidence_links").IsNull() && !ValueArray(MapGet(in, "evidence_links")).IsEmpty()) {
		const DataDocument *evidence = store.FindDocument(ws + ":" + (String)ValueArray(MapGet(in, "evidence_links"))[0]);
		if(evidence) evidence_source = MapGetString(evidence->data, "source_identity");
	}
	form.Add("candidate_id", cid); form.Add("proposal_revision_id", CW(revision, "id")); form.Add("candidate_kind", CW(in, "candidate_kind")); form.Add("source_identity", evidence_source);
	for(const char *key : { "title", "purpose", "quality_contract", "inputs", "outputs", "runtime_resources", "assets", "verification_plan", "evidence_links", "counterevidence", "uncertainty" }) if(!MapGet(in, key).IsVoid()) form.Add(key, MapGet(in, key));
	form.Add("draft_state", "draft"); form.Set("hypothetical", hypothetical_candidate); form.Add("submission_state", hypothetical_candidate ? "not-a-real-submission" : "not-submitted"); form.Add("approval_state", "not-approved"); form.Add("author_state", "unknown"); form.Add("intent_state", "unknown");
	String new_revision_id = CW(revision, "id");
	ValueMap draft_event = CWBase("candidate-stage-event", CWStageEventId(operation, cid, new_revision_id, "draft"), ws);
	draft_event.Add("candidate_id", cid); draft_event.Add("candidate_revision", candidate->revision); draft_event.Add("proposal_revision_id", new_revision_id); draft_event.Add("stage", "draft"); draft_event.Add("previous_event_id", "none");
	draft_event.Add("reason", "New proposal revision draft"); draft_event.Add("evidence_ids", ValueArray()); draft_event.Add("operation_id", operation); draft_event.Add("actor", session.username); draft_event.Add("authority", "authenticated-admin"); draft_event.Add("timestamp", AsString(session.created_at));
	ValueMap draft_head = CWBase("candidate-stage-head", CWStageHeadId(ws, cid, new_revision_id), ws);
	draft_head.Add("candidate_id", cid); draft_head.Add("proposal_revision_id", new_revision_id); draft_head.Add("stage", "draft"); draft_head.Add("event_id", CW(draft_event, "id"));
	if(MapGetString(head->data, "proposal_revision_id") == CW(revision, "id")) {
		const DataDocument *saved_form = store.FindDocument(ws + ":" + CW(form, "id"));
		const DataDocument *saved_event = store.FindDocument(ws + ":" + CW(draft_event, "id"));
		const DataDocument *saved_stage_head = store.FindDocument(ws + ":" + CW(draft_head, "id"));
		if(saved_form && saved_form->valid && saved_form->validated && AsJSON(saved_form->data, true) == AsJSON(form, true) && saved_event && saved_event->valid && saved_event->validated &&
		   CW(saved_event->data, "operation_id") == operation && CW(saved_event->data, "proposal_revision_id") == new_revision_id && CW(saved_event->data, "stage") == "draft" &&
		   saved_stage_head && saved_stage_head->valid && saved_stage_head->validated && CW(saved_stage_head->data, "proposal_revision_id") == new_revision_id && CW(saved_stage_head->data, "event_id") == CW(saved_event->data, "id")) {
			ValueMap done = CWResult("already_committed"); done.Add("proposal_id", pid); done.Add("revision_id", CW(revision, "id")); return done;
		}
		return CWResult("idempotency_conflict", "operation_key_reused_with_changed_payload");
	}
	if(head->revision != expected_head_revision || MapGetString(head->data, "proposal_revision_id") != expected_revision_id) return CWResult("stale", "proposal_head_or_candidate_changed");
	ValueMap next_head = head->data; next_head.Set("proposal_revision_id", CW(revision, "id")); next_head.Set("revision_id", revision_id);
	Vector<DataBatchChange> changes;
	for(const ValueMap& r : { revision, form, draft_event, draft_head }) { DataBatchChange c; c.action = DATA_BATCH_CREATE; c.qualified_id = ws + ":" + CW(r, "id"); c.proposed = r; changes.Add(c); }
	DataBatchChange hc; hc.action = DATA_BATCH_UPDATE; hc.qualified_id = head->qualified_id; hc.expected_revision = head->revision; hc.expected_source_revision = head->source_revision; hc.proposed = next_head; changes.Add(hc);
	ManagerSkylarkApp::BatchFinalizationResult result = SystemAuthService::FinalizeBatchForSession(const_cast<ManagerSkylarkApp&>(app), session, source, ws, operation, changes);
	ValueMap out = CWResult(result.status); if(result.committed || result.status == "already_committed") { out.Add("proposal_id", pid); out.Add("revision_id", CW(revision, "id")); } return out;
}

ValueMap CandidateWorkflow::AdvanceStage(const DataStore& store, const ManagerSkylarkApp& app, const String& ws, const String& cid,
	const String& proposal_revision_id, const String& expected_candidate_revision, const String& expected_head_revision, const String& expected_head_event,
	const String& next_stage, const String& reason, const ValueArray& evidence_ids, const String& source, const String& operation,
	const UserSession& session, const String& review_outcome)
{
	if(!SystemAuthService::AuthorizeWriteForSession(const_cast<ManagerSkylarkApp&>(app), session, source, ws)) return CWResult("denied", "write_authorization_denied");
	if(store.GetLoadStatus() != "loaded" || store.GetSourceId() != source || !CWOperation(operation)) return CWResult("denied", "write_authorization_denied");
	if(!CWStage(next_stage) || reason.IsEmpty() || reason.GetCount() > 1024 || TrimBoth(reason).IsEmpty() || evidence_ids.GetCount() > 64) return CWResult("invalid", "invalid_transition_fields");
	Index<String> unique_evidence;
	for(const Value& id : evidence_ids) if(!IsString(id) || ((String)id).IsEmpty() || ((String)id).GetCount() > 256 || unique_evidence.Find((String)id) >= 0) return CWResult("invalid", "invalid_or_duplicate_evidence_id"); else unique_evidence.Add((String)id);
	if(next_stage == "duplicate-reviewed" && review_outcome != "no_duplicate" && review_outcome != "duplicate_found" && review_outcome != "uncertain") return CWResult("invalid", "duplicate_review_outcome_required");
	if(next_stage != "duplicate-reviewed" && !review_outcome.IsEmpty()) return CWResult("invalid", "unexpected_review_outcome");
	if(proposal_revision_id.IsEmpty() || proposal_revision_id.GetCount() > 256) return CWResult("invalid", "proposal_revision_id_required");
	ValueMap view = Project(store, app, ws, cid);
	if(CW(view, "status") != "visible") return CWResult(CW(view, "status"));
	if(CW(view, "proposal_revision_id") != proposal_revision_id) return CWResult("stale", "proposal_revision_head_changed");
	String retry_id = CWStageEventId(operation, cid, proposal_revision_id, next_stage);
	const DataDocument *retry = store.FindDocument(ws + ":" + retry_id);
	if(retry) {
		ValueArray refs; for(const Value& id : evidence_ids) refs.Add(id);
		ValueMap expected = CWBase("candidate-stage-event", retry_id, ws); expected.Add("candidate_id", cid); expected.Add("candidate_revision", expected_candidate_revision); expected.Add("proposal_revision_id", proposal_revision_id); expected.Add("stage", next_stage);
		expected.Add("previous_event_id", expected_head_event.IsEmpty() ? "none" : expected_head_event); expected.Add("reason", TrimBoth(reason)); expected.Add("evidence_ids", refs); expected.Add("operation_id", operation);
		if(!review_outcome.IsEmpty()) expected.Add("review_outcome", review_outcome); expected.Add("actor", session.username); expected.Add("authority", "authenticated-admin"); expected.Add("timestamp", AsString(session.created_at));
		ValueMap saved = retry->data;
		if(retry->valid && retry->validated && AsJSON(saved, true) == AsJSON(expected, true)) {
			ValueMap r = CWResult("already_committed"); r.Add("event_id", retry_id); return r;
		}
		return CWResult("idempotency_conflict", "operation_key_reused_with_changed_payload");
	}
	ValueArray current_proposals = MapGet(view, "current_proposals");
	if(current_proposals.GetCount() != 1) return CWResult("denied");
	ValueMap current_form = MapGet((ValueMap)current_proposals[0], "snapshot");
	ValueArray current_links = MapGet(current_form, "evidence_links");
	const DataDocument *candidate = store.FindDocument(ws + ":" + cid);
	if(!candidate || candidate->revision != expected_candidate_revision || CW(view, "head_revision") != expected_head_revision || CW(view, "head_event_id") != expected_head_event) return CWResult("stale", "candidate_or_stage_head_revision_changed");
	int current = -1, next = -1; for(int i = 0; i < (int)__countof(CW_STAGES); i++) { if(CW(view, "stage") == CW_STAGES[i]) current = i; if(next_stage == CW_STAGES[i]) next = i; }
	if(current < 0 || next != current + 1) return CWResult("blocked", "skipped_or_nonsequential_stage");
	ValueMap gate = CheckStagePrerequisite(store, app, ws, next_stage, current_form, evidence_ids, review_outcome, false);
	if(CW(gate, "status") != "ready") return gate;
	if(store.GetLoadStatus() != "loaded" || source != store.GetSourceId()) return CWResult("denied", "write_authorization_denied");
	String event_id = retry_id;
	ValueMap event = CWBase("candidate-stage-event", event_id, ws); event.Add("candidate_id", cid); event.Add("candidate_revision", expected_candidate_revision); event.Add("proposal_revision_id", proposal_revision_id); event.Add("stage", next_stage);
	event.Add("previous_event_id", expected_head_event.IsEmpty() ? "none" : expected_head_event); event.Add("reason", TrimBoth(reason)); event.Add("evidence_ids", evidence_ids); event.Add("operation_id", operation);
	if(!review_outcome.IsEmpty()) event.Add("review_outcome", review_outcome); event.Add("actor", session.username); event.Add("authority", "authenticated-admin"); event.Add("timestamp", AsString(session.created_at));
	ValueMap head; head.Add("candidate_id", cid); head.Add("stage", next_stage); head.Add("event_id", event_id);
	String head_id = CWStageHeadId(ws, cid, proposal_revision_id);
	const DataDocument *old_head = store.FindDocument(ws + ":" + head_id);
	if(old_head && CW(old_head->data, "proposal_revision_id") != proposal_revision_id) return CWResult("stale", "proposal_revision_stage_head_mismatch");
	if(old_head) head = old_head->data;
	head.Set("stage", next_stage); head.Set("event_id", event_id);
	if(old_head) { int record_revision = ScanInt(CW(head, "revision")); if(record_revision < 1 || record_revision >= 999999) return CWResult("invalid", "stage_head_revision_limit"); head.Set("revision", Format("%d", record_revision + 1)); }
	Vector<DataBatchChange> changes; DataBatchChange ec; ec.action = DATA_BATCH_CREATE; ec.qualified_id = ws + ":" + event_id; ec.proposed = event; changes.Add(ec);
	DataBatchChange hc; hc.qualified_id = ws + ":" + CW(head, "id"); hc.proposed = head;
	if(expected_head_revision == "0") { head = CWBase("candidate-stage-head", head_id, ws); head.Add("candidate_id", cid); head.Add("proposal_revision_id", proposal_revision_id); head.Add("stage", next_stage); head.Add("event_id", event_id); hc.proposed = head; hc.qualified_id = ws + ":" + head_id; hc.action = DATA_BATCH_CREATE; }
	else { if(!old_head || old_head->revision != expected_head_revision || CW(old_head->data, "event_id") != expected_head_event) return CWResult("stale", "stage_head_compare_and_swap_failed"); hc.proposed = head; hc.qualified_id = ws + ":" + head_id; hc.action = DATA_BATCH_UPDATE; hc.expected_revision = expected_head_revision; hc.expected_source_revision = old_head->source_revision; }
	changes.Add(hc); ManagerSkylarkApp::BatchFinalizationResult result = SystemAuthService::FinalizeBatchForSession(const_cast<ManagerSkylarkApp&>(app), session, source, ws, operation, changes);
	if(result.status == "prevalidation_failed" || result.status == "conflict") return CWResult("stale", "stage_head_compare_and_swap_failed");
	return CWResult(result.status);
}

String CandidateWorkflow::Render(const ValueMap& p)
{
	String html = "<section class=\"cw-panel\"><h1>Proposal Workspace</h1><p>Local draft only. Nothing here is submitted, approved, accepted, or implementation-authorized.</p>";
	html << "<p>Workflow status: <strong>" << EscapeHtml(CW(p, "status")) << "</strong></p>";
	html << "<form id=\"cw-create\" aria-label=\"Create a local proposal draft\"><input type=\"hidden\" name=\"operation_id\" value=\"proposal-" << DataStore::ContentDigest(AsString(GetSysTime()) + AsString(GetTickCount())).Left(32) << "\"><label>Workspace <input name=\"workspace_id\" required maxlength=\"128\"></label>";
	html << "<label>Kind <select name=\"candidate_kind\"><option>candidate</option><option>proposal</option><option>research-stage</option></select></label>";
	for(const char *field : { "title", "purpose", "quality_contract", "inputs", "outputs", "runtime_resources", "assets", "verification_plan", "evidence_links" })
		html << "<label>" << EscapeHtml(field) << " <textarea name=\"" << EscapeHtml(field) << "\" required maxlength=\"4096\"></textarea></label>";
	html << "<label><input type=\"checkbox\" name=\"hypothetical\" value=\"true\"> Hypothetical local interpretation only (not submitted or approved)</label>";
	html << "<fieldset id=\"cw-hypothetical-fields\" class=\"cw-hypothetical\" hidden><legend>Hypothetical evidence and uncertainty</legend>"
	        "<label>Counterevidence IDs <textarea name=\"counterevidence\" maxlength=\"4096\"></textarea></label>"
	        "<label>Uncertainty <textarea name=\"uncertainty\" maxlength=\"4096\"></textarea></label></fieldset>";
	html << "<button type=\"submit\">Save local draft</button></form><p id=\"cw-result\" role=\"status\" aria-live=\"polite\"></p>";
	html << "<script>(function(){var f=document.getElementById('cw-create'),hyp=f.elements.hypothetical,fields=document.getElementById('cw-hypothetical-fields'),ce=fields.querySelector('[name=counterevidence]'),unc=fields.querySelector('[name=uncertainty]');function syncHypothesis(){fields.hidden=!hyp.checked;ce.required=hyp.checked;unc.required=hyp.checked}hyp.addEventListener('change',syncHypothesis);syncHypothesis();f.addEventListener('submit',function(e){e.preventDefault();var d=new FormData(f),form={};['candidate_kind','title','purpose','quality_contract','inputs','outputs','runtime_resources','assets','verification_plan'].forEach(function(k){form[k]=d.get(k)});form.evidence_links=d.get('evidence_links').split(',').map(function(v){return v.trim()}).filter(Boolean);if(hyp.checked){form.hypothetical=true;form.counterevidence=d.get('counterevidence').split(',').map(function(v){return v.trim()}).filter(Boolean);form.uncertainty=d.get('uncertainty')}fetch('/api/candidates/proposal',{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json'},body:JSON.stringify({workspace_id:d.get('workspace_id'),operation_id:d.get('operation_id'),form:form})}).then(function(r){return r.json()}).then(function(v){document.getElementById('cw-result').textContent=v.status+(v.reason?': '+v.reason:'')}).catch(function(){document.getElementById('cw-result').textContent='Request failed; no external action was attempted.'})})})()</script>";
	ValueArray current_proposals = MapGet(p, "current_proposals");
	int revise_form_number = 0;
	for(const Value& item : current_proposals) if(IsValueMap(item)) {
		ValueMap current = item, snapshot = MapGet(current, "snapshot");
		String proposal_id = CW(current, "proposal_id"), revision_id = CW(current, "revision_id");
		String operation_id = "revise-" + DataStore::ContentDigest(proposal_id + "\n" + revision_id).Left(32);
		String form_id = "cw-revise-" + AsString(++revise_form_number);
		html << "<form class=\"cw-revise\" id=\"" << form_id << "\" aria-label=\"Revise current local proposal\">"
		     << "<h2>Revise Current Proposal</h2><p>This creates a new immutable draft revision; it does not submit or approve it.</p>"
		     << "<input type=\"hidden\" name=\"workspace_id\" value=\"" << EscapeHtml(CW(p, "workspace_id")) << "\">"
		     << "<input type=\"hidden\" name=\"candidate_id\" value=\"" << EscapeHtml(CW(p, "candidate_id")) << "\">"
		     << "<input type=\"hidden\" name=\"proposal_id\" value=\"" << EscapeHtml(proposal_id) << "\">"
		     << "<input type=\"hidden\" name=\"expected_head_revision\" value=\"" << EscapeHtml(CW(current, "head_revision")) << "\">"
		     << "<input type=\"hidden\" name=\"expected_revision_id\" value=\"" << EscapeHtml(revision_id) << "\">"
		     << "<input type=\"hidden\" name=\"operation_id\" value=\"" << EscapeHtml(operation_id) << "\">";
		if(MapGet(snapshot, "hypothetical") == true) html << "<input type=\"hidden\" name=\"hypothetical\" value=\"true\"><p>Hypothetical local interpretation only; not submitted or approved.</p>";
		html << "<label>Kind <select name=\"candidate_kind\">";
		for(const char *kind : { "candidate", "proposal", "research-stage" }) html << "<option value=\"" << kind << "\"" << (CW(snapshot, "candidate_kind") == kind ? " selected" : "") << ">" << kind << "</option>";
		html << "</select></label>";
		for(const char *field : { "title", "purpose", "quality_contract", "inputs", "outputs", "runtime_resources", "assets", "verification_plan" })
			html << "<label>" << EscapeHtml(field) << " <textarea name=\"" << EscapeHtml(field) << "\" required maxlength=\"4096\">" << EscapeHtml(CW(snapshot, field)) << "</textarea></label>";
		String evidence_ids; for(const Value& ref : ValueArray(MapGet(snapshot, "evidence_links"))) { if(!evidence_ids.IsEmpty()) evidence_ids << ", "; if(IsString(ref)) evidence_ids << EscapeHtml((String)ref); }
		html << "<label>evidence_links <textarea name=\"evidence_links\" required maxlength=\"4096\">" << evidence_ids << "</textarea></label>";
		if(MapGet(snapshot, "hypothetical") == true) {
			String counterevidence; for(const Value& ref : ValueArray(MapGet(snapshot, "counterevidence"))) { if(!counterevidence.IsEmpty()) counterevidence << ", "; if(IsString(ref)) counterevidence << EscapeHtml((String)ref); }
			html << "<fieldset class=\"cw-hypothetical\"><legend>Hypothetical evidence and uncertainty</legend>"
			     << "<label>Counterevidence IDs <textarea name=\"counterevidence\" required maxlength=\"4096\">" << counterevidence << "</textarea></label>"
			     << "<label>Uncertainty <textarea name=\"uncertainty\" required maxlength=\"4096\">" << EscapeHtml(CW(snapshot, "uncertainty")) << "</textarea></label></fieldset>";
		}
		html << "<button type=\"submit\">Save new draft revision</button></form>";
	}
	if(revise_form_number) html << "<script>(function(){document.querySelectorAll('.cw-revise').forEach(function(f){f.addEventListener('submit',function(e){e.preventDefault();var d=new FormData(f),form={};['candidate_kind','title','purpose','quality_contract','inputs','outputs','runtime_resources','assets','verification_plan'].forEach(function(k){form[k]=d.get(k)});form.evidence_links=d.get('evidence_links').split(',').map(function(v){return v.trim()}).filter(Boolean);if(d.get('hypothetical')==='true'){form.hypothetical=true;form.counterevidence=d.get('counterevidence').split(',').map(function(v){return v.trim()}).filter(Boolean);form.uncertainty=d.get('uncertainty')}var body={workspace_id:d.get('workspace_id'),candidate_id:d.get('candidate_id'),proposal_id:d.get('proposal_id'),expected_head_revision:d.get('expected_head_revision'),expected_revision_id:d.get('expected_revision_id'),operation_id:d.get('operation_id'),form:form};fetch('/api/candidates/proposal/revise',{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(function(r){return r.json()}).then(function(v){document.getElementById('cw-result').textContent=v.status+(v.reason?': '+v.reason:'')}).catch(function(){document.getElementById('cw-result').textContent='Request failed; no external action was attempted.'})})})})()</script>";
	ValueArray gates = MapGet(p, "prerequisites");
	for(const Value& v : gates) { ValueMap g = v; html << "<p><strong>" << EscapeHtml(CW(g, "stage")) << ": " << EscapeHtml(CW(g, "status")) << "</strong> " << EscapeHtml(CW(g, "reason")) << "</p>"; }
	html << "<p>Proposal revision: " << EscapeHtml(CW(p, "proposal_revision_id")) << "; current stage: " << EscapeHtml(CW(p, "stage")) << "; head revision: " << EscapeHtml(CW(p, "head_revision")) << "</p>";
	int projected_stage = 0; for(int i=0;i<(int)__countof(CW_STAGES);i++) if(CW(p,"stage")==CW_STAGES[i]) projected_stage=i;
	if(CW(p, "status") == "visible" && !CW(p, "candidate_id").IsEmpty() && CW(p, "stage") != "comparison-ready") {
		String next_stage = CW_STAGES[projected_stage + 1];
		String gate_status = "blocked"; for(const Value& v : gates) if(IsValueMap(v) && CW((ValueMap)v,"stage")==next_stage) gate_status=CW((ValueMap)v,"status");
		html << "<form id=\"cw-advance\" data-workspace=\"" << EscapeHtml(CW(p, "workspace_id")) << "\" data-candidate=\"" << EscapeHtml(CW(p, "candidate_id")) << "\" data-candidate-revision=\"" << EscapeHtml(CW(p, "candidate_revision")) << "\" data-head-revision=\"" << EscapeHtml(CW(p, "head_revision")) << "\" data-head-event=\"" << EscapeHtml(CW(p, "head_event_id")) << "\"><input type=\"hidden\" name=\"proposal_revision_id\" value=\"" << EscapeHtml(CW(p, "proposal_revision_id")) << "\"><label>Next stage <input name=\"next_stage\" readonly value=\"" << EscapeHtml(next_stage) << "\"></label><p>Readiness: " << EscapeHtml(gate_status) << "</p>";
		html << "<label>Reason <input name=\"reason\" required maxlength=\"1024\"></label><label>Evidence IDs <input name=\"evidence_ids\" maxlength=\"4096\"></label>";
		if(next_stage == "duplicate-reviewed") html << "<label>Review outcome <select name=\"review_outcome\" required><option value=\"\">Select outcome</option><option>no_duplicate</option><option>duplicate_found</option><option>uncertain</option></select></label>";
		html << "<input type=\"hidden\" name=\"operation_id\" value=\"stage-" << DataStore::ContentDigest(CW(p, "candidate_id") + CW(p, "head_revision") + AsString(GetTickCount())).Left(32) << "\"><button type=\"submit\">Request next stage</button></form>";
		html << "<script>(function(){var f=document.getElementById('cw-advance');f.addEventListener('submit',function(e){e.preventDefault();var d=new FormData(f);fetch('/api/candidates/stage',{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json'},body:JSON.stringify({workspace_id:f.dataset.workspace,candidate_id:f.dataset.candidate,proposal_revision_id:d.get('proposal_revision_id'),expected_candidate_revision:f.dataset.candidateRevision,expected_head_revision:f.dataset.headRevision,expected_head_event_id:f.dataset.headEvent,next_stage:d.get('next_stage'),reason:d.get('reason'),evidence_ids:d.get('evidence_ids').split(',').map(function(v){return v.trim()}).filter(Boolean),review_outcome:d.get('review_outcome')||'',operation_id:d.get('operation_id')})}).then(function(r){return r.json()}).then(function(v){document.getElementById('cw-result').textContent=v.status+(v.reason?': '+v.reason:'')}).catch(function(){document.getElementById('cw-result').textContent='Request failed.'})})})()</script>";
	}
	html << "</section>"; return html;
}

bool CandidateWorkflow::RunSyntheticTests()
{
	ValueMap form; form.Add("candidate_kind", "proposal");
	for(const char *k : { "title", "purpose", "quality_contract", "inputs", "outputs", "runtime_resources", "assets", "verification_plan" }) form.Add(k, String("Synthetic ") + k);
	ValueArray refs; refs.Add("synthetic-visible-evidence"); form.Add("evidence_links", refs);
	if(!CWText(form, "title", MAX_TEXT_BYTES) || !IsString(MapGet(form, "title"))) return false;
	ValueMap bad = form; bad.Set("title", "TODO"); if(CWText(bad, "title", MAX_TEXT_BYTES)) return false;
	bad = form; bad.Add("unexpected", true); static const char *keys[] = { "candidate_kind", "title", "purpose", "quality_contract", "inputs", "outputs", "runtime_resources", "assets", "verification_plan", "evidence_links", "hypothetical", "counterevidence", "uncertainty" };
	if(CWOnly(bad, keys, __countof(keys))) return false;
	bad = form; bad.Set("title", 9); if(CWText(bad, "title", MAX_TEXT_BYTES)) return false;
	bad = form; bad.Set("title", String('x', MAX_TEXT_BYTES + 1)); if(CWText(bad, "title", MAX_TEXT_BYTES)) return false;
	Cout() << "ok    T65 form type/placeholder/unknown-field guards and explicit blocked-stage service paths\n";
	if(!CWStage("draft") || !CWStage("comparison-ready") || CWStage("accepted") || CWStage("submitted")) return false;
	Index<String> outcomes; outcomes.Add("no_duplicate"); outcomes.Add("duplicate_found"); outcomes.Add("uncertain"); if(outcomes.GetCount() != 3 || outcomes.Find("accepted") >= 0) return false;
	if(CWOperation("short") || CWOperation(String(129, 'x'))) return false;
	ValueArray duplicate_refs; duplicate_refs.Add("evidence-a"); duplicate_refs.Add("evidence-a"); Index<String> unique_refs;
	for(const Value& v : duplicate_refs) { if(unique_refs.Find((String)v) >= 0) break; unique_refs.Add((String)v); }
	if(unique_refs.GetCount() != 1) return false;
	Cout() << "ok    T67 stage allowlist, operation bounds, structured review outcomes and duplicate evidence rejection contract\n";
	return true;
}

END_UPP_NAMESPACE
