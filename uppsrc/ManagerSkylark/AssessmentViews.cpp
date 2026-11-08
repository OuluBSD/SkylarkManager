#include "AssessmentViews.h"
#include "ManagerSkylark.h"

NAMESPACE_UPP

namespace {

static String AVString(const ValueMap& map, const char *key)
{
	Value value = MapGet(map, key);
	return IsString(value) ? (String)value : String();
}

static ValueArray AVArray(const ValueMap& map, const char *key)
{
	Value value = MapGet(map, key);
	return value.Is<ValueArray>() ? (ValueArray)value : ValueArray();
}

static bool ReadAssessmentMap(const String& path, ValueMap& out)
{
	if(!FileExists(path)) return false;
	int64 size = GetFileLength(path);
	if(size < 0 || size > 2 * 1024 * 1024) return false;
	try {
		Value value = ParseJSON(LoadFile(path));
		if(!value.Is<ValueMap>()) return false;
		out = value;
		return true;
	}
	catch(Exc) { return false; }
}

static String UnqualifyAssessmentId(const String& workspace, const String& id)
{
	String prefix = workspace + ":";
	return id.StartsWith(prefix) ? id.Mid(prefix.GetCount()) : id;
}

static const DataDocument *FindAssessmentRecord(const DataStore& store, const String& workspace,
	const String& id, const String& kind)
{
	const DataDocument *found = NULL;
	int count = 0;
	for(const DataDocument& doc : store.GetDocuments())
		if(doc.workspace_id == workspace && doc.id == id && doc.kind == kind) { found = &doc; count++; }
	return count == 1 ? found : NULL;
}

static bool NormalizeRubricInteger(ValueMap& map, const char *field)
{
	Value value = MapGet(map, field);
	if(value.Is<int>() || value.Is<int64>()) return true;
	if(!value.Is<double>()) return false;
	double number = (double)value;
	if(number < 0 || number > 1000000) return false;
	int64 normalized = (int64)number;
	if((double)normalized != number) return false;
	map.Set(field, normalized);
	return true;
}

static bool NormalizeRubricIntegers(ValueMap& rubric)
{
	ValueArray dimensions = AVArray(rubric, "dimensions");
	for(int i = 0; i < dimensions.GetCount(); i++) {
		if(!dimensions[i].Is<ValueMap>()) return false;
		ValueMap dimension = dimensions[i];
		for(const char *field : { "weight", "minimum_score", "maximum_score", "minimum_evidence_count" })
			if(!NormalizeRubricInteger(dimension, field)) return false;
		dimensions.Set(i, dimension);
	}
	ValueArray tiers = AVArray(rubric, "tiers");
	for(int i = 0; i < tiers.GetCount(); i++) {
		if(!tiers[i].Is<ValueMap>()) return false;
		ValueMap tier = tiers[i];
		for(const char *field : { "minimum_numerator", "minimum_denominator", "maximum_numerator", "maximum_denominator" })
			if(!NormalizeRubricInteger(tier, field)) return false;
		tiers.Set(i, tier);
	}
	rubric.Set("dimensions", dimensions);
	rubric.Set("tiers", tiers);
	return true;
}

static ValueMap SafeAssessmentProjection(const ValueMap& projection)
{
	ValueMap safe;
	for(const char *field : { "status", "workspace_id", "rubric_id", "rubric_revision", "uncertainty",
	                          "unique_source_count", "unique_source_revision_count", "unique_evidence_count" }) {
		Value value = MapGet(projection, field);
		if(!value.IsVoid() && !value.IsNull()) safe.Add(field, value);
	}
	ValueArray candidates;
	for(const Value& value : AVArray(projection, "candidate_assessments")) if(value.Is<ValueMap>()) {
		ValueMap candidate = value, output;
		for(const char *field : { "candidate_id", "provenance", "uncertainty", "tier", "total_numerator", "total_denominator", "total_state" }) {
			Value item = MapGet(candidate, field);
			if(!item.IsVoid() && !item.IsNull()) output.Add(field, item);
		}
		ValueArray dimensions;
		for(const Value& dimension_value : AVArray(candidate, "dimensions")) if(dimension_value.Is<ValueMap>()) {
			ValueMap dimension = dimension_value, safe_dimension;
			for(const char *field : { "dimension_id", "score", "score_state", "evidence_count", "coverage_state" }) {
				Value item = MapGet(dimension, field);
				if(!item.IsVoid() && !item.IsNull()) safe_dimension.Add(field, item);
			}
			dimensions.Add(safe_dimension);
		}
		output.Add("dimensions", dimensions);
		candidates.Add(output);
	}
	safe.Add("candidate_assessments", candidates);
	ValueArray findings;
	for(const Value& finding_value : AVArray(projection, "findings")) if(finding_value.Is<ValueMap>()) {
		ValueMap finding = finding_value, safe_finding;
		for(const char *field : { "code", "candidate_id", "dimension_id" }) {
			Value item = MapGet(finding, field);
			if(!item.IsVoid() && !item.IsNull()) safe_finding.Add(field, item);
		}
		findings.Add(safe_finding);
	}
	safe.Add("findings", findings);
	return safe;
}

}

ValueMap AssessmentViewService::Build(const ManagerSkylarkApp& app, const String& requested_workspace)
{
	ValueMap result;
	result.Add("status", "unconfigured");
	if(!app.HasExplicitSiteDir() || app.GetDataStore().GetRootDir().IsEmpty()) return result;
	ValueMap rubric, configured;
	if(!ReadAssessmentMap(AppendFileName(app.GetSiteDir(), "assessment-rubric.json"), rubric) ||
	   !ReadAssessmentMap(AppendFileName(app.GetSiteDir(), "assessment-inputs.json"), configured)) {
		return result;
	}
	if(!NormalizeRubricIntegers(rubric)) { result.Set("status", "unavailable"); return result; }
	String workspace = AVString(configured, "workspace_id");
	if(workspace.IsEmpty() || (!requested_workspace.IsEmpty() && requested_workspace != workspace) || AVString(rubric, "workspace_id") != workspace) {
		result.Set("status", "unavailable");
		return result;
	}
	for(const String& key : { String("sources"), String("evidence"), String("input_revisions") })
		if(configured.Find(key) >= 0) { result.Set("status", "unavailable"); return result; }
	ValueArray scores = AVArray(configured, "scores");
	if(scores.IsEmpty() || scores.GetCount() > AssessmentService::MAX_INPUT_ITEMS) { result.Set("status", "unavailable"); return result; }
	ValueMap inputs;
	inputs.Add("workspace_id", workspace);
	ValueArray relationships = AVArray(configured, "relationships");
	Value findings = MapGet(configured, "findings");
	if(!findings.IsVoid() && !findings.IsNull()) inputs.Add("findings", findings);
	Value tool_config = MapGet(configured, "tool_config"), model_config = MapGet(configured, "model_config");
	if(!tool_config.IsVoid() && !tool_config.IsNull()) inputs.Add("tool_config", tool_config);
	if(!model_config.IsVoid() && !model_config.IsNull()) inputs.Add("model_config", model_config);

	ValueArray sources, evidence, pins, normalized_scores;
	Index<String> source_keys, evidence_keys, pin_keys;
	for(const Value& score_value : scores) {
		if(!score_value.Is<ValueMap>()) { result.Set("status", "unavailable"); return result; }
		ValueMap score = score_value;
		if(AVString(score, "workspace_id") != workspace || !MapGet(score, "evidence_ids").Is<ValueArray>()) {
			result.Set("status", "unavailable");
			return result;
		}
		ValueArray normalized_refs;
		for(const Value& ref_value : AVArray(score, "evidence_ids")) {
			if(!IsString(ref_value)) { result.Set("status", "unavailable"); return result; }
			String item_id = UnqualifyAssessmentId(workspace, (String)ref_value);
			const DataDocument *item = FindAssessmentRecord(app.GetDataStore(), workspace, item_id, "evidence-item");
			if(!item || !item->valid || !item->validated || item->validation_state != "valid") { result.Set("status", "unavailable"); return result; }
			const ValueMap& item_data = item->data;
			String source_identity = AVString(item_data, "source_identity");
			String source_revision = AVString(item_data, "upstream_revision");
			String snapshot_id = UnqualifyAssessmentId(workspace, AVString(item_data, "snapshot"));
			if(AVString(item_data, "resolution") != "resolved" || AVString(item_data, "visibility") != "visible" ||
			   AVString(item_data, "classification") != "public" || source_identity.IsEmpty() || source_revision.IsEmpty() ||
			   snapshot_id.IsEmpty() || !app.IsAssessmentSourceVisible(workspace, source_identity)) {
				result.Set("status", "unavailable");
				return result;
			}
			const DataDocument *snapshot = FindAssessmentRecord(app.GetDataStore(), workspace, snapshot_id, "evidence-snapshot");
			if(!snapshot || !snapshot->valid || !snapshot->validated || snapshot->validation_state != "valid") { result.Set("status", "unavailable"); return result; }
			const ValueMap& snapshot_data = snapshot->data;
			if(AVString(snapshot_data, "source_identity") != source_identity || AVString(snapshot_data, "upstream_revision") != source_revision ||
			   AVString(snapshot_data, "resolution") != "resolved" || AVString(snapshot_data, "visibility") != "visible" ||
			   AVString(snapshot_data, "classification") != "public" || (bool)MapGet(snapshot_data, "retracted") ||
			   AVString(snapshot_data, "content_sha256") != AVString(item_data, "content_sha256")) {
				result.Set("status", "unavailable");
				return result;
			}
			ValueMap source;
			source.Add("workspace_id", workspace); source.Add("identity", source_identity); source.Add("revision", source_revision);
			String source_key = source_identity + "\n" + source_revision;
			if(source_keys.Find(source_key) < 0) { source_keys.Add(source_key); sources.Add(source); }
			ValueMap evidence_item;
			evidence_item.Add("workspace_id", workspace); evidence_item.Add("item_id", item_id); evidence_item.Add("snapshot_id", snapshot_id);
			evidence_item.Add("source_identity", source_identity); evidence_item.Add("source_revision", source_revision);
			evidence_item.Add("resolution", "resolved"); evidence_item.Add("visibility", "visible");
			String evidence_key = item_id + "\n" + source_revision;
			if(evidence_keys.Find(evidence_key) < 0) { evidence_keys.Add(evidence_key); evidence.Add(evidence_item); }
			ValueMap source_pin;
			source_pin.Add("workspace_id", workspace); source_pin.Add("id", source_identity); source_pin.Add("revision", source_revision);
			if(pin_keys.Find(source_key) < 0) { pin_keys.Add(source_key); pins.Add(source_pin); }
			String snapshot_key = snapshot_id + "\n" + source_revision;
			if(pin_keys.Find(snapshot_key) < 0) {
				ValueMap snapshot_pin;
				snapshot_pin.Add("workspace_id", workspace); snapshot_pin.Add("id", snapshot_id); snapshot_pin.Add("revision", source_revision);
				pin_keys.Add(snapshot_key); pins.Add(snapshot_pin);
			}
			normalized_refs.Add(item_id);
		}
		score.Set("evidence_ids", normalized_refs);
		normalized_scores.Add(score);
	}
	inputs.Add("scores", normalized_scores); inputs.Add("sources", sources); inputs.Add("evidence", evidence);
	inputs.Add("input_revisions", pins); inputs.Add("relationships", relationships);
	ValueMap projection = AssessmentService::Project(rubric, inputs);
	if(AVString(projection, "status") != "configured") { result.Set("status", "unavailable"); return result; }
	return SafeAssessmentProjection(projection);
}

String AssessmentViewService::Render(const ValueMap& view)
{
	String status = AVString(view, "status");
	String html = "<section class=\"card\"><h1>Assessment</h1>";
	if(status != "configured") return html + "<p role=\"status\">Assessment unavailable or not configured.</p></section>";
	html << "<p>Rubric " << EscapeHtml(AVString(view, "rubric_id")) << " @ " << EscapeHtml(AVString(view, "rubric_revision"))
	     << "; uncertainty: " << EscapeHtml(AVString(view, "uncertainty")) << "</p><ul>";
	for(const Value& candidate_value : AVArray(view, "candidate_assessments")) if(candidate_value.Is<ValueMap>()) {
		ValueMap candidate = candidate_value;
		html << "<li><strong>" << EscapeHtml(AVString(candidate, "candidate_id")) << "</strong>: tier "
		     << EscapeHtml(AVString(candidate, "tier")) << ", score " << EscapeHtml(AVString(candidate, "total_numerator")) << "/"
		     << EscapeHtml(AVString(candidate, "total_denominator")) << ", " << EscapeHtml(AVString(candidate, "uncertainty")) << "<ul>";
		for(const Value& dimension_value : AVArray(candidate, "dimensions")) if(dimension_value.Is<ValueMap>()) {
			ValueMap dimension = dimension_value;
			html << "<li>" << EscapeHtml(AVString(dimension, "dimension_id")) << ": " << EscapeHtml(AVString(dimension, "score"))
			     << " (" << EscapeHtml(AVString(dimension, "score_state")) << ", evidence " << EscapeHtml(AsString(MapGet(dimension, "evidence_count")))
			     << ", " << EscapeHtml(AVString(dimension, "coverage_state")) << ")</li>";
		}
		html << "</ul></li>";
	}
	html << "</ul><p>Computed projection only. No scores were written and no provider was invoked.</p></section>";
	return html;
}

bool AssessmentViewService::RunSyntheticTests()
{
	return AssessmentService::RunSyntheticTests();
}

END_UPP_NAMESPACE
