#include "ManagerSkylark.h"
#include "CandidateWorkflow.h"

NAMESPACE_UPP

static bool IsPublicDataRecord(const DataDocument& document)
{
	String visibility = ToLower(document.visibility);
	if(!visibility.IsEmpty() && visibility != "public" && visibility != "visible")
		return false;
	if(document.kind == "review-package" || document.kind == "review-package-revision" ||
	   document.kind == "review-thread" || document.kind == "review-comment")
		return false;
	String classification = ToLower(MapGetString(document.data, "classification"));
	if(!classification.IsEmpty() && classification != "public")
		return false;
	Value envelope_value = MapGet(document.data, "envelope");
	if(envelope_value.Is<ValueMap>()) {
		ValueMap envelope = envelope_value;
		String envelope_visibility = ToLower(MapGetString(envelope, "visibility"));
		String envelope_classification = ToLower(MapGetString(envelope, "classification"));
		if((!envelope_visibility.IsEmpty() && envelope_visibility != "public") ||
		   (!envelope_classification.IsEmpty() && envelope_classification != "public"))
			return false;
	}
	return !document.kind.StartsWith("internal-") && !document.kind.StartsWith("private-");
}

SKYLARK(DataIndexPage, "data")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	const DataStore& store = app.GetDataStore();
	String body;
	body << "<section class=\"card\"><h1>Data Catalog</h1>"
	     << "<p>Read-only, schema-validated records from the explicitly configured site data source.</p>"
	     << "<p>Status: <strong>" << EscapeHtml(store.GetRootDir().IsEmpty() ? "not configured" : store.GetLoadStatus()) << "</strong></p>"
	     << "<ul>";
	for(const String& kind : store.GetKinds()) {
		int count = 0;
		for(const DataDocument& document : store.GetDocuments())
			if(document.kind == kind && IsPublicDataRecord(document)) count++;
		if(!count) continue;
		body << "<li><a href=\"/api/data/docs?kind=" << UrlEncode(kind) << "\">" << EscapeHtml(kind) << "</a> (" << count << ")</li>";
	}
	body << "</ul></section>";
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Data Catalog", "data", body, &http);
}

SKYLARK(ApiDataSpec, "api/data/spec")
{
	const DataStore& store = ManagerSkylarkApp::Get().GetDataStore();
	ValueMap result;
	result.Add("status", store.GetRootDir().IsEmpty() ? "unconfigured" : store.GetLoadStatus());
	result.Add("read_only", true);
	result.Add("source_id", store.GetSourceId());
	result.Add("default_policy", ManagerSkylarkApp::Get().ResolveEffectivePolicy(PolicyInput()).ToRedactedValue());
	int visible_invalid = 0;
	for(const DataDocument& document : store.GetDocuments())
		if(IsPublicDataRecord(document) && !document.valid) visible_invalid++;
	result.Add("invalid_records", visible_invalid);
	ValueArray kinds;
	for(const String& kind : store.GetKinds()) {
		ValueMap item;
		item.Add("kind", kind);
		const SchemaCard *schema = store.FindSchemaCard(kind);
		if(schema) {
			item.Add("schema_id", schema->schema_id);
			item.Add("schema_version", schema->schema_version);
		}
		int visible_count = 0;
		for(const DataDocument& document : store.GetDocuments())
			if(document.kind == kind && IsPublicDataRecord(document)) visible_count++;
		if(!visible_count) continue;
		item.Add("visible_count", visible_count);
		kinds.Add(item);
	}
	result.Add("kinds", kinds);
	http.ContentType("application/json; charset=utf-8") << AsJSON(result, true);
}

SKYLARK(ApiDataDocs, "api/data/docs")
{
	const DataStore& store = ManagerSkylarkApp::Get().GetDataStore();
	String kind_filter = http["kind"];
	ValueArray records;
	int limit = ScanInt(AsString(http["limit"]));
	if(limit <= 0 || limit > 100) limit = 100;
	for(const DataDocument& document : store.GetDocuments()) {
		if(!IsPublicDataRecord(document) || (!kind_filter.IsEmpty() && document.kind != kind_filter))
			continue;
		ValueMap record;
		record.Add("id", document.qualified_id);
		record.Add("kind", document.kind);
		record.Add("title", document.title);
		record.Add("status", document.status);
		record.Add("workspace_id", document.workspace_id);
		record.Add("schema_id", document.schema_id);
		record.Add("schema_version", document.schema_version);
		record.Add("revision", document.revision);
		record.Add("validation_state", document.validation_state);
		record.Add("read_only", true);
		records.Add(record);
		if(records.GetCount() >= limit) break;
	}
	ValueMap result;
	result.Add("status", store.GetRootDir().IsEmpty() ? "unconfigured" : store.GetLoadStatus());
	result.Add("count", records.GetCount());
	result.Add("records", records);
	http.ContentType("application/json; charset=utf-8") << AsJSON(result, true);
}

static ValueMap SafeLineageRecord(const ValueMap& source)
{
	ValueMap safe;
	if(MapGetString(source, "kind") == "redacted-lineage-link") {
		safe.Add("kind", "redacted-lineage-link");
		safe.Add("resolution", "denied");
		return safe;
	}
	for(const char *field : { "id", "kind", "workspace_id", "origin_kind", "owner_kind", "provenance",
	                          "uncertainty", "submission_state", "approval_state", "author_state", "intent_state",
	                          "hypothetical", "candidate_kind", "proposal_id", "revision_id", "parent_revision_id",
	                          "candidate_id", "revision", "decision" }) {
		Value value = MapGet(source, field);
		if(!value.IsNull()) safe.Add(field, value);
	}
	Value relationships = MapGet(source, "relationships");
	if(relationships.Is<ValueArray>()) {
		ValueArray safe_links;
		for(const Value& value : (ValueArray)relationships) if(value.Is<ValueMap>()) {
			ValueMap link = value, item;
			for(const char *field : { "type", "target_kind", "resolution" }) {
				Value field_value = MapGet(link, field);
				if(!field_value.IsNull()) item.Add(field, field_value);
			}
			safe_links.Add(item);
		}
		safe.Add("relationships", safe_links);
	}
	return safe;
}

static ValueMap SafeLineageProjection(const ManagerSkylarkApp& app, const String& workspace_id)
{
	ValueMap projection = CandidateLineage::Project(app.GetDataStore(), app, workspace_id);
	ValueMap result;
	String status = MapGetString(projection, "status");
	if(status != "visible") {
		result.Add("status", "unavailable");
		return result;
	}
	result.Add("status", "visible");
	result.Add("workspace_id", workspace_id);
	result.Add("hypothetical", MapGet(projection, "hypothetical"));
	result.Add("hypothesis_state", MapGetString(projection, "hypothesis_state"));
	ValueArray records = MapGet(projection, "records"), safe_records;
	for(const Value& value : records) if(value.Is<ValueMap>()) safe_records.Add(SafeLineageRecord(value));
	result.Add("records", safe_records);
	return result;
}

static ValueMap SafeCandidateWorkflowProjection(const ManagerSkylarkApp& app, const String& workspace_id, const String& candidate_id)
{
	const DataStore& store = app.GetDataStore();
	ValueMap lineage = CandidateLineage::Project(store, app, workspace_id);
	if(MapGetString(lineage, "status") != "visible") { ValueMap unavailable; unavailable.Add("status", "unavailable"); return unavailable; }
	bool candidate_visible = false;
	for(const Value& value : ValueArray(MapGet(lineage, "records"))) if(value.Is<ValueMap>()) {
		ValueMap record = value;
		if(MapGetString(record, "id") == candidate_id && MapGetString(record, "kind") != "redacted-lineage-link")
			candidate_visible = true;
	}
	if(!candidate_visible) { ValueMap unavailable; unavailable.Add("status", "unavailable"); return unavailable; }
	bool has_proposal = false;
	for(const DataDocument& document : store.GetDocuments())
		if(document.workspace_id == workspace_id && document.valid && document.validated && document.kind == "candidate-proposal" &&
		   MapGetString(document.data, "candidate_id") == candidate_id)
			has_proposal = true;
	ValueMap result;
	result.Add("status", "visible");
	result.Add("workspace_id", workspace_id);
	result.Add("candidate_id", candidate_id);
	if(!has_proposal) {
		result.Add("workflow_state", "not_started");
		result.Add("stage", "draft");
		return result;
	}
	ValueMap projection = CandidateWorkflow::Project(store, app, workspace_id, candidate_id);
	if(MapGetString(projection, "status") != "visible") { ValueMap unavailable; unavailable.Add("status", "unavailable"); return unavailable; }
	result.Add("workflow_state", "visible");
	for(const char *field : { "candidate_revision", "proposal_revision_id", "stage", "head_revision", "hypothetical" }) {
		Value value = MapGet(projection, field);
		if(!value.IsNull()) result.Add(field, value);
	}
	ValueArray history, safe_history;
	for(const Value& value : ValueArray(MapGet(projection, "history"))) if(value.Is<ValueMap>()) {
		ValueMap event = value, safe;
		safe.Add("stage", MapGet(event, "stage"));
		safe_history.Add(safe);
	}
	result.Add("history", safe_history);
	ValueArray prerequisites, safe_prerequisites;
	for(const Value& value : ValueArray(MapGet(projection, "prerequisites"))) if(value.Is<ValueMap>()) {
		ValueMap gate = value, safe;
		safe.Add("stage", MapGet(gate, "stage"));
		safe.Add("status", MapGet(gate, "status"));
		safe_prerequisites.Add(safe);
	}
	result.Add("prerequisites", safe_prerequisites);
	ValueArray proposals, safe_proposals;
	for(const Value& value : ValueArray(MapGet(projection, "current_proposals"))) if(value.Is<ValueMap>()) {
		ValueMap proposal = value, safe;
		for(const char *field : { "proposal_id", "revision_id", "head_revision" }) {
			Value v = MapGet(proposal, field);
			if(!v.IsNull()) safe.Add(field, v);
		}
		ValueMap snapshot = MapGet(proposal, "snapshot"), safe_snapshot;
		for(const char *field : { "title", "candidate_kind", "submission_state", "approval_state", "hypothetical" }) {
			Value v = MapGet(snapshot, field);
			if(!v.IsNull()) safe_snapshot.Add(field, v);
		}
		safe.Add("snapshot", safe_snapshot);
		safe_proposals.Add(safe);
	}
	result.Add("current_proposals", safe_proposals);
	return result;
}

SKYLARK(ApiCandidateLineage, "api/candidates/lineage")
{
	String workspace_id = http["workspace"];
	if(workspace_id.IsEmpty() || workspace_id.GetCount() > 128) {
		http.Response(400, "Invalid Workspace");
		http.ContentType("application/json; charset=utf-8") << "{\"status\":\"invalid\"}";
		return;
	}
	ValueMap projection = SafeLineageProjection(ManagerSkylarkApp::Get(), workspace_id);
	if(MapGetString(projection, "status") != "visible") http.Response(404, "Lineage Unavailable");
	http.ContentType("application/json; charset=utf-8") << AsJSON(projection, true);
}

SKYLARK(ApiCandidateWorkflow, "api/candidates/workflow")
{
	String workspace_id = http["workspace"], candidate_id = http["candidate"];
	if(workspace_id.IsEmpty() || candidate_id.IsEmpty() || workspace_id.GetCount() > 128 || candidate_id.GetCount() > 256) {
		http.ContentType("application/json; charset=utf-8") << "{\"status\":\"invalid\"}";
		http.Response(400, "Invalid Candidate");
		return;
	}
	ValueMap projection = SafeCandidateWorkflowProjection(ManagerSkylarkApp::Get(), workspace_id, candidate_id);
	if(MapGetString(projection, "status") != "visible") http.Response(404, "Workflow Unavailable");
	http.ContentType("application/json; charset=utf-8") << AsJSON(projection, true);
}

SKYLARK(CandidateWorkflowPage, "candidate-workflow")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	String workspace_id = http["workspace"], candidate_id = http["candidate"];
	if(app.IsDemoSite()) {
		if(workspace_id.IsEmpty()) workspace_id = "demo-workspace";
		if(candidate_id.IsEmpty()) candidate_id = "demo-candidate";
	}
	String body = "<section class=\"card\"><h1>Candidate Workflow</h1>"
	              "<p>Read-only candidate proposal and stage projection. Create, revise, submit, approve, and advance actions are disabled.</p>"
	              "<form method=\"GET\" action=\"/candidate-workflow\"><label>Workspace <input name=\"workspace\" value=\"" + EscapeHtml(workspace_id) + "\"></label>"
	              "<label>Candidate <input name=\"candidate\" value=\"" + EscapeHtml(candidate_id) + "\"></label><button type=\"submit\">View</button></form>";
	if(!workspace_id.IsEmpty() && !candidate_id.IsEmpty()) {
		ValueMap projection = SafeCandidateWorkflowProjection(app, workspace_id, candidate_id);
		body << CandidateWorkflow::Render(projection);
	}
	body << "</section>";
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Candidate Workflow", "candidates", body, &http);
}

SKYLARK(ApiResearchPapers, "api/research/papers")
{
	const DataStore& store = ManagerSkylarkApp::Get().GetDataStore();
	ValueArray papers;
	for(const DataDocument& document : store.GetDocuments()) {
		if(!IsPublicDataRecord(document) || !document.valid || !document.validated ||
		   (document.kind != "paper" && document.kind != "paper-analysis"))
			continue;
		ValueMap paper;
		paper.Add("id", document.qualified_id);
		paper.Add("kind", document.kind);
		paper.Add("title", document.title);
		paper.Add("status", document.status);
		paper.Add("workspace_id", document.workspace_id);
		papers.Add(paper);
		if(papers.GetCount() >= 100) break;
	}
	ValueMap result;
	result.Add("status", store.GetRootDir().IsEmpty() ? "unconfigured" : store.GetLoadStatus());
	result.Add("read_only", true);
	result.Add("papers", papers);
	http.ContentType("application/json; charset=utf-8") << AsJSON(result, true);
}

SKYLARK(ResearchIndexPage, "research")
{
	const DataStore& store = ManagerSkylarkApp::Get().GetDataStore();
	String body = "<section class=\"card\"><h1>Research Library</h1><p>Read-only metadata from the explicitly configured site data source. Paper fetching and analysis providers are not installed.</p><ul>";
	for(const DataDocument& document : store.GetDocuments())
		if(IsPublicDataRecord(document) && document.valid && document.validated && (document.kind == "paper" || document.kind == "paper-analysis"))
			body << "<li><strong>" << EscapeHtml(document.title) << "</strong> <span>" << EscapeHtml(document.kind) << "</span> " << EscapeHtml(document.status) << "</li>";
	body << "</ul></section>";
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Research Library", "research", body, &http);
}

SKYLARK(CandidateIndexPage, "candidates")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	String workspace_id = http["workspace"];
	if(workspace_id.IsEmpty() && app.IsDemoSite()) workspace_id = "demo-workspace";
	String body = "<section class=\"card\"><h1>Candidate Lineage</h1><p>Read-only, workspace-scoped candidate provenance. Proposal creation, stage changes, and submission are not exposed.</p>"
	              "<form method=\"GET\" action=\"/candidates\"><label>Workspace <input name=\"workspace\" value=\"" + EscapeHtml(workspace_id) + "\"></label><button type=\"submit\">View</button></form>";
	if(!workspace_id.IsEmpty()) {
		ValueMap projection = SafeLineageProjection(app, workspace_id);
		if(MapGetString(projection, "status") == "visible") {
			body << "<ul>";
			for(const Value& value : (ValueArray)MapGet(projection, "records")) if(value.Is<ValueMap>()) {
				ValueMap record = value;
				body << "<li>" << EscapeHtml(MapGetString(record, "kind"));
				if(MapGetString(record, "kind") == "redacted-lineage-link") body << " - related record unavailable";
				else {
					String id = MapGetString(record, "id");
					body << " " << EscapeHtml(id) << ": " << EscapeHtml(MapGetString(record, "uncertainty"))
					     << " <a href=\"/candidate-workflow?workspace=" << UrlEncode(workspace_id) << "&amp;candidate=" << UrlEncode(id) << "\">Workflow</a>";
				}
				body << "</li>";
			}
			body << "</ul>";
		}
		else body << "<p role=\"status\">Workspace lineage unavailable.</p>";
	}
	body << "</section>";
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Candidate Lineage", "candidates", body, &http);
}

END_UPP_NAMESPACE
