#include "HistoricalAssessmentMigration.h"

NAMESPACE_UPP

static const int MIGRATION_MAX_DEPTH = 16;
static const int MIGRATION_MAX_NODES = 20000;
static const int MIGRATION_MAX_KEY_BYTES = 256;
static const int MIGRATION_MAX_STRING_BYTES = 65536;

static bool CheckMigrationValue(const Value& value, int depth, int& nodes, int64& bytes)
{
	if(depth > MIGRATION_MAX_DEPTH || ++nodes > MIGRATION_MAX_NODES) return false;
	if(value.Is<ValueMap>()) {
		ValueMap map = value;
		if(map.GetCount() > 1024) return false;
		for(int i = 0; i < map.GetCount(); i++) {
			Value key_value = map.GetKey(i);
			if(!IsString(key_value)) return false;
			String key = key_value;
			if(key.GetCount() > MIGRATION_MAX_KEY_BYTES) return false;
			bytes += key.GetCount();
			if(bytes > HistoricalAssessmentMigration::MAX_SNAPSHOT_BYTES ||
			   !CheckMigrationValue(map.GetValue(i), depth + 1, nodes, bytes)) return false;
		}
		return true;
	}
	if(value.Is<ValueArray>()) {
		ValueArray array = value;
		if(array.GetCount() > HistoricalAssessmentMigration::MAX_ASSERTIONS * 16) return false;
		for(int i = 0; i < array.GetCount(); i++)
			if(!CheckMigrationValue(array[i], depth + 1, nodes, bytes)) return false;
		return true;
	}
	if(IsNull(value) || value.Is<bool>() || value.Is<int>() || value.Is<int64>()) {
		bytes += 16;
		return bytes <= HistoricalAssessmentMigration::MAX_SNAPSHOT_BYTES;
	}
	if(IsString(value)) {
		String text = value;
		if(text.GetCount() > MIGRATION_MAX_STRING_BYTES) return false;
		bytes += text.GetCount();
		return bytes <= HistoricalAssessmentMigration::MAX_SNAPSHOT_BYTES;
	}
	// In particular, reject floating-point and opaque U++ values before JSON serialization.
	return false;
}

static bool MigrationInputBounded(const Value& input)
{
	int nodes = 0;
	int64 bytes = 0;
	return input.Is<ValueMap>() && CheckMigrationValue(input, 0, nodes, bytes);
}

static String MigrationOperationId(const String& workspace, const String& source, const Value& snapshot)
{
	if(workspace.IsEmpty() || source.IsEmpty() || !MigrationInputBounded(snapshot)) return String();
	String encoded = AsJSON(snapshot, true);
	if(encoded.GetCount() > HistoricalAssessmentMigration::MAX_SNAPSHOT_BYTES) return String();
	return "ham-op-" + DataStore::ContentDigest(workspace + "\n" + source + "\n" + encoded).Left(32);
}

String HistoricalAssessmentMigration::OperationId(const Value& snapshot, const String& workspace,
	                                                const String& source)
{
	return MigrationOperationId(workspace, source, snapshot);
}

static String MigrationId(const String& workspace, const String& source, const ValueMap& snapshot,
	                      const ValueMap& assertion, int ordinal)
{
	String identity = MapGetString(snapshot, "snapshot_id", "unknown") + "\n" +
	                  MapGetString(snapshot, "snapshot_revision", "unknown") + "\n" +
	                  MapGetString(assertion, "source_id", Format("ordinal-%d", ordinal));
	return "ham-" + DataStore::ContentDigest(workspace + "\n" + source + "\n" + identity).Left(32);
}

static ValueMap MakeRecord(const ValueMap& snapshot, const ValueMap& assertion,
	                       const String& workspace, const String& source, int ordinal)
{
	String id = MigrationId(workspace, source, snapshot, assertion, ordinal);
	ValueMap record;
	record.Add("id", id);
	record.Add("kind", "historical-assessment");
	record.Add("migration_identity", id);
	record.Add("snapshot_id", MapGetString(snapshot, "snapshot_id", "unknown"));
	record.Add("snapshot_revision", MapGetString(snapshot, "snapshot_revision", "unknown"));
	record.Add("source_assertion", assertion);
	ValueMap envelope;
	envelope.Add("schema_id", "skylark/historical-assessment");
	envelope.Add("schema_version", "1");
	envelope.Add("workspace_id", workspace);
	envelope.Add("stable_id", id);
	envelope.Add("record_kind", "historical-assessment");
	envelope.Add("revision", "1");
	envelope.Add("visibility", "private");
	envelope.Add("provenance_ref", source);
	record.Add("envelope", envelope);
	return record;
}

static bool IsUnknown(const Value& value)
{
	return value.IsVoid() || IsNull(value) || (IsString(value) && ((String)value).IsEmpty());
}

static String ScalarText(const Value& value, bool& valid)
{
	if(IsString(value)) return (String)value;
	if(value.Is<int>() || value.Is<int64>() || value.Is<bool>()) return AsString(value);
	valid = false;
	return String();
}

static bool InSet(const String& value, std::initializer_list<const char *> allowed)
{
	for(const char *candidate : allowed) if(value == candidate) return true;
	return false;
}

static void PreserveLegacy(ValueMap& legacy, const ValueMap& input, const String& key)
{
	if(input.Find(key) >= 0) legacy.Set(key, MapGet(input, key));
}

static ValueArray NormalizeEntries(const Value& input, bool scores, ValueMap& legacy, bool& valid)
{
	ValueArray entries;
	if(input.IsVoid() || IsNull(input)) return entries;
	if(!input.Is<ValueMap>()) { valid = false; return entries; }
	ValueMap map = input;
	if(map.GetCount() > 256) { valid = false; return entries; }
	for(int i = 0; i < map.GetCount(); i++) {
		String category = map.GetKey(i);
		Value raw = map.GetValue(i);
		if(category.GetCount() > 128) { valid = false; return entries; }
		ValueMap entry;
		entry.Add("category", category);
		if(scores) {
			bool scalar = true;
			String text = ScalarText(raw, scalar);
			if(!scalar || text.GetCount() > 512) { valid = false; return entries; }
			entry.Add("score", text);
		}
		else if(IsString(raw) && ((String)raw).GetCount() <= 8192) entry.Add("narrative", (String)raw);
		else {
			legacy.Set("category_narrative_" + category, raw);
			continue;
		}
		entries.Add(entry);
	}
	return entries;
}

static ValueMap NormalizeAssertion(const ValueMap& input, const String& workspace, const String& source,
	                               const Index<String>& candidate_ids, const DataStore& destination,
	                               ValueArray& findings, int ordinal, bool& valid)
{
	ValueMap out, legacy;
	auto copy_text = [&](const String& key, int max_bytes, const String& fallback, String& state) {
		Value raw = MapGet(input, key);
		if(IsString(raw) && !((String)raw).IsEmpty() && ((String)raw).GetCount() <= max_bytes) {
			out.Add(key, (String)raw); state = "known";
		}
		else {
			out.Add(key, fallback); state = "unknown";
			if(!raw.IsVoid() && !IsNull(raw)) PreserveLegacy(legacy, input, key);
		}
	};
	Value source_id_value = MapGet(input, "source_id");
	String source_id = IsString(source_id_value) ? (String)source_id_value : String();
	if(source_id.IsEmpty() || source_id.GetCount() > 512) {
		if(!source_id_value.IsVoid() && !IsNull(source_id_value)) PreserveLegacy(legacy, input, "source_id");
		source_id = Format("ordinal-%d", ordinal);
	}
	out.Add("source_id", source_id);

	String origin = MapGetString(input, "origin");
	bool origin_known = InSet(origin, { "task-record", "authored-profile", "hypothesis" });
	out.Add("origin", origin_known ? origin : "unknown");
	out.Add("origin_state", origin_known ? "known" : "unknown");
	if(!origin_known && input.Find("origin") >= 0) PreserveLegacy(legacy, input, "origin");

	String kind = MapGetString(input, "candidate_kind");
	bool kind_known = InSet(kind, { "candidate", "proposal", "research-stage" });
	out.Add("candidate_kind", kind_known ? kind : "unknown");
	out.Add("candidate_kind_state", kind_known ? "known" : "unknown");
	if(!kind_known && input.Find("candidate_kind") >= 0) PreserveLegacy(legacy, input, "candidate_kind");

	String lifecycle = MapGetString(input, "lifecycle_state");
	bool lifecycle_known = InSet(lifecycle, { "proposed", "submitted", "approved", "rejected", "completed", "research-stage" });
	out.Add("lifecycle_state", lifecycle_known ? lifecycle : "unknown");
	out.Add("lifecycle_state_state", lifecycle_known ? "known" : "unknown");
	if(!lifecycle_known && input.Find("lifecycle_state") >= 0) PreserveLegacy(legacy, input, "lifecycle_state");

	String state;
	copy_text("timestamp", 128, "unknown", state); out.Add("timestamp_state", state);
	copy_text("author", 512, "unknown", state); out.Add("author_state", state);
	copy_text("rubric", 512, "unknown", state); out.Add("rubric_state", state);
	copy_text("rubric_revision", 512, "unknown", state); out.Add("rubric_revision_state", state);
	copy_text("source_revision", 512, "unknown", state); out.Add("source_revision_state", state);
	copy_text("membership", 128, "unknown", state); out.Add("membership_state", state);
	copy_text("tier", 256, "unknown", state); out.Add("tier_state", state);
	copy_text("rationale", 8192, "unknown", state); out.Add("rationale_state", state);

	Value candidate_value = MapGet(input, "candidate_id");
	String candidate = IsString(candidate_value) ? (String)candidate_value : String();
	bool candidate_known = !candidate.IsEmpty() && candidate.GetCount() <= 512;
	out.Add("candidate_id", candidate_known ? candidate : "unknown");
	String candidate_state = !candidate_known || candidate_ids.IsEmpty() || candidate_ids.Find(candidate) < 0 ? "unresolved" : "resolved";
	out.Add("candidate_link_state", candidate_state);
	if(!candidate_known && input.Find("candidate_id") >= 0) PreserveLegacy(legacy, input, "candidate_id");
	if(candidate_known && !candidate_ids.IsEmpty() && candidate_state == "unresolved") findings.Add("orphan_candidate_link");

	ValueArray original_links, resolved_links;
	bool unsupported_link = false;
	Value links = MapGet(input, "original_links");
	if(links.Is<ValueArray>()) {
		ValueArray raw_links = links;
		if(raw_links.GetCount() > 256) { valid = false; return out; }
		for(int i = 0; i < raw_links.GetCount(); i++) {
			if(!IsString(raw_links[i])) { legacy.Set("original_links", links); unsupported_link = true; continue; }
			String link = raw_links[i];
			original_links.Add(link);
			const DataDocument* target = link.IsEmpty() ? NULL : destination.FindDocument(link);
			if(target && target->workspace_id == workspace && target->source_id == source) resolved_links.Add(link);
			else findings.Add("unresolved_source_link");
		}
	}
	else if(!links.IsVoid() && !IsNull(links)) PreserveLegacy(legacy, input, "original_links");
	out.Add("original_links", original_links);
	out.Add("resolved_links", resolved_links);
	String source_link_state = unsupported_link ? "unresolved" :
	                           original_links.IsEmpty() ? (links.IsVoid() || IsNull(links) ? "unknown" : "unresolved") :
	                           resolved_links.GetCount() == original_links.GetCount() ? "resolved" : "unresolved";
	out.Add("source_link_state", source_link_state);
	out.Add("links_state", source_link_state);

	out.Add("category_scores", NormalizeEntries(MapGet(input, "category_scores"), true, legacy, valid));
	out.Add("category_narratives", NormalizeEntries(MapGet(input, "category_narratives"), false, legacy, valid));
	if(!valid) return out;

	String submission = MapGetString(input, "submission_state");
	if(origin == "hypothesis") {
		if(input.Find("submission_state") >= 0) PreserveLegacy(legacy, input, "submission_state");
		out.Add("submission_state", "not-a-real-submission");
	}
	else out.Add("submission_state", submission == "submission" && kind == "candidate" ? "submission" : "unknown");
	String approval = MapGetString(input, "approval_state");
	if(origin == "hypothesis") {
		if(input.Find("approval_state") >= 0) PreserveLegacy(legacy, input, "approval_state");
		out.Add("approval_state", "not-approved");
	}
	else out.Add("approval_state", origin_known && InSet(approval, { "approved", "not-approved" }) ? approval : "unknown");
	out.Add("score_conflict", false);
	out.Add("narrative_conflict", false);

	static const char *mapped[] = { "source_id", "candidate_id", "candidate_kind", "origin", "lifecycle_state", "timestamp", "author",
		"rubric", "rubric_revision", "source_revision", "membership", "tier", "rationale", "category_scores", "category_narratives",
		"original_links", "submission_state", "approval_state" };
	for(int i = 0; i < input.GetCount(); i++) {
		String key = input.GetKey(i); bool known = false;
		for(const char *name : mapped) if(key == name) known = true;
		if(!known && legacy.Find(key) < 0) legacy.Add(key, input.GetValue(i));
	}
	out.Add("legacy_extra", legacy);
	return out;
}

static ValueMap EntriesToMap(const Value& entries_value, bool scores)
{
	ValueMap map;
	if(!entries_value.Is<ValueArray>()) return map;
	ValueArray entries = entries_value;
	for(int i = 0; i < entries.GetCount(); i++) {
		ValueMap entry = entries[i];
		map.Add(MapGetString(entry, "category"), MapGetString(entry, scores ? "score" : "narrative"));
	}
	return map;
}

ValueMap HistoricalAssessmentMigration::Preview(const Value& input, const String& workspace,
	                                               const String& source,
	                                               const DataWriteAuthorization& authorization,
	                                               const DataStore& destination)
{
	ValueMap out;
	if(!authorization.AllowsSource(workspace, source) ||
	   (!destination.GetSourceId().IsEmpty() && destination.GetSourceId() != source)) {
		out.Add("status", "denied");
		return out;
	}
	out.Add("status", "invalid");
	if(!MigrationInputBounded(input)) return out;
	String encoded = AsJSON(input, true);
	if(encoded.GetCount() > MAX_SNAPSHOT_BYTES) return out;
	ValueMap snapshot = input;
	Value assertions_value = MapGet(snapshot, "assertions");
	if(!assertions_value.Is<ValueArray>()) return out;
	ValueArray assertions = assertions_value;
	if(assertions.GetCount() > MAX_ASSERTIONS) return out;
	ValueArray records, findings;
	Index<String> source_ids, candidate_ids;
	Value candidates_value = MapGet(snapshot, "candidates");
	if(candidates_value.Is<ValueArray>()) {
		ValueArray candidates = candidates_value;
		for(int i = 0; i < candidates.GetCount(); i++)
			if(candidates[i].Is<ValueMap>()) candidate_ids.FindAdd(MapGetString((ValueMap)candidates[i], "candidate_id"));
	}
	for(int i = 0; i < assertions.GetCount(); i++) {
		if(!assertions[i].Is<ValueMap>()) return out;
		ValueMap original = assertions[i];
		String sid = MapGetString(original, "source_id");
		if(sid.IsEmpty()) sid = Format("ordinal-%d", i);
		if(source_ids.Find(sid) >= 0) findings.Add("duplicate_source_id");
		else source_ids.Add(sid);
		bool valid = true;
		ValueMap assertion = NormalizeAssertion(original, workspace, source, candidate_ids, destination, findings, i, valid);
		if(!valid) return out;
		ValueMap record = MakeRecord(snapshot, assertion, workspace, source, i);
		records.Add(record);
	}
	// Cross-origin comparisons are additive findings and annotate only assertions involved.
	for(int i = 0; i < records.GetCount(); i++) {
		ValueMap left_record = records[i];
		ValueMap left = MapGet(left_record, "source_assertion");
		String left_origin = MapGetString(left, "origin");
		if(left_origin != "task-record" && left_origin != "authored-profile") continue;
		for(int j = i + 1; j < records.GetCount(); j++) {
			ValueMap right_record = records[j];
			ValueMap right = MapGet(right_record, "source_assertion");
			String right_origin = MapGetString(right, "origin");
			if(right_origin == left_origin || (right_origin != "task-record" && right_origin != "authored-profile") ||
			   MapGetString(left, "candidate_id") != MapGetString(right, "candidate_id") ||
			   MapGetString(left, "rubric") != MapGetString(right, "rubric")) continue;
			bool score_conflict = false, narrative_conflict = false;
			ValueMap a = EntriesToMap(MapGet(left, "category_scores"), true);
			ValueMap b = EntriesToMap(MapGet(right, "category_scores"), true);
			if(a.GetCount() != b.GetCount()) score_conflict = true;
			for(int k = 0; k < a.GetCount(); k++) {
				String key = a.GetKey(k);
				int other = b.Find(key);
				if(other < 0 || AsString(a.GetValue(k)) != AsString(b.GetValue(other))) score_conflict = true;
			}
			ValueMap left_narratives = EntriesToMap(MapGet(left, "category_narratives"), false);
			ValueMap right_narratives = EntriesToMap(MapGet(right, "category_narratives"), false);
			if(AsJSON(left_narratives, true) != AsJSON(right_narratives, true)) narrative_conflict = true;
			for(const String& key : { "membership", "tier", "rationale" })
				if(AsJSON(MapGet(left, key), true) != AsJSON(MapGet(right, key), true)) narrative_conflict = true;
			if(score_conflict || narrative_conflict) {
				if(score_conflict) { left.Set("score_conflict", true); right.Set("score_conflict", true); findings.Add("score_assertion_conflict"); }
				if(narrative_conflict) { left.Set("narrative_conflict", true); right.Set("narrative_conflict", true); findings.Add("narrative_assertion_conflict"); }
				left_record.Set("source_assertion", left); right_record.Set("source_assertion", right);
				records.Set(i, left_record); records.Set(j, right_record);
			}
		}
	}
	// Compare only final, conflict-annotated payloads against stored records.
	for(int i = 0; i < records.GetCount(); i++) {
		ValueMap record = records[i];
		const DataDocument* existing = destination.FindDocument(workspace + ":" + MapGetString(record, "id"));
		if(existing) {
			if(AsJSON(existing->data, true) == AsJSON(record, true)) findings.Add("exact_repeat");
			else findings.Add("destination_collision");
		}
	}
	for(int i = 0; i < findings.GetCount(); i++) {
		String finding = findings[i];
		if(finding == "duplicate_source_id" || finding == "destination_collision") {
			out.Set("status", "conflict");
			out.Add("findings", findings);
			return out;
		}
	}
	out.Set("status", "ready");
	out.Add("records", records);
	out.Add("findings", findings);
	return out;
}

DataBatchResult HistoricalAssessmentMigration::Import(const Value& snapshot, const String& workspace,
	                                                     const String& source, const String& operation,
	                                                     const DataWriteAuthorization& authorization,
	                                                     const DataStore& destination)
{
	DataBatchResult result;
	if(!authorization.AllowsSource(workspace, source) ||
	   (!destination.GetSourceId().IsEmpty() && destination.GetSourceId() != source)) {
		result.status = "denied";
		return result;
	}
	String expected_operation = MigrationOperationId(workspace, source, snapshot);
	if(expected_operation.IsEmpty()) { result.status = "invalid"; return result; }
	if(operation != expected_operation) { result.status = "operation_id_mismatch"; return result; }
	ValueMap preview = Preview(snapshot, workspace, source, authorization, destination);
	if(MapGetString(preview, "status") != "ready") { result.status = MapGetString(preview, "status"); return result; }
	ValueArray records = MapGet(preview, "records");
	Vector<DataBatchChange> changes;
	for(int i = 0; i < records.GetCount(); i++) {
		ValueMap record = records[i];
		String qualified = workspace + ":" + MapGetString(record, "id");
		const DataDocument* existing = destination.FindDocument(qualified);
		if(existing) {
			if(AsJSON(existing->data, true) != AsJSON(record, true)) { result.status = "conflict"; return result; }
			continue;
		}
		DataBatchChange change;
		change.action = DATA_BATCH_CREATE;
		change.qualified_id = qualified;
		change.proposed = record;
		changes.Add(change);
	}
	if(changes.IsEmpty()) { result.status = "exact_repeat"; return result; }
	result = destination.PublishBatch(authorization, operation, changes);
	if(result.status != "committed" && result.status != "committed_durability_uncertain") {
		result.operation_id.Clear(); result.changed_ids.Clear(); result.revisions.Clear();
	}
	return result;
}

bool HistoricalAssessmentMigration::RunSyntheticTests(const DataWriteAuthorization& authorization,
	                                                     const DataWriteAuthorization& denied)
{
	auto fail = [](const String& condition, const String& detail = String()) {
		Cerr() << "FAILED: T15 pure synthetic assertion: " << condition;
		if(!detail.IsEmpty()) Cerr() << " (" << detail << ")";
		Cerr() << "\n";
		return false;
	};
	const String workspace = "synthetic-t15-workspace";
	const String source = "synthetic-t15-source";
	ValueMap snapshot;
	snapshot.Add("snapshot_id", "synthetic-snapshot");
	snapshot.Add("snapshot_revision", "synthetic-revision");
	ValueMap unrelated_candidate; unrelated_candidate.Add("candidate_id", "another-synthetic-candidate");
	ValueArray candidates; candidates.Add(unrelated_candidate); snapshot.Add("candidates", candidates);
	ValueArray assertions;
	auto assertion = [](const String& id, const String& origin, const String& score) {
		ValueMap a; a.Add("source_id", id); a.Add("candidate_id", "synthetic-candidate");
		a.Add("candidate_kind", "proposal"); a.Add("origin", origin); a.Add("rubric", "synthetic-rubric");
		ValueMap scores; scores.Add("synthetic-category", score); a.Add("category_scores", scores);
		a.Add("membership", "shortlisted"); a.Add("tier", "synthetic-tier"); a.Add("rationale", "synthetic rationale");
		ValueArray links; links.Add("unresolved-source"); a.Add("original_links", links);
		return a;
	};
	assertions.Add(assertion("task-assertion", "task-record", "1"));
	assertions.Add(assertion("profile-assertion", "authored-profile", "2"));
	assertions.Add(assertion("hypothesis-assertion", "hypothesis", "3"));
	assertions.Add(assertion("task-assertion", "task-record", "4"));
	snapshot.Add("assertions", assertions);
	DataStore empty;
	ValueMap denied_preview = Preview(snapshot, workspace, source, denied, empty);
	if(MapGetString(denied_preview, "status") != "denied" || denied_preview.Find("records") >= 0)
		return fail("mismatched authorization is redacted", AsJSON(denied_preview, true));
	ValueMap conflict = Preview(snapshot, workspace, source, authorization, empty);
	if(MapGetString(conflict, "status") != "conflict")
		return fail("duplicate source IDs block preview", MapGetString(conflict, "status"));
	ValueMap unique_snapshot = snapshot;
	ValueArray unique_assertions; unique_assertions.Add(assertions[0]); unique_assertions.Add(assertions[1]); unique_assertions.Add(assertions[2]);
	unique_snapshot.Set("assertions", unique_assertions);
	ValueMap preview = Preview(unique_snapshot, workspace, source, authorization, empty);
	if(MapGetString(preview, "status") != "ready" || !MapGet(preview, "records").Is<ValueArray>())
		return fail("unique assertion set maps", MapGetString(preview, "status"));
	ValueMap fractional_snapshot = unique_snapshot;
	ValueArray fractional_assertions; ValueMap fractional_assertion = assertions[0];
	ValueMap fractional_scores; fractional_scores.Add("synthetic-category", 1.25);
	fractional_assertion.Set("category_scores", fractional_scores); fractional_assertions.Add(fractional_assertion);
	fractional_snapshot.Set("assertions", fractional_assertions);
	ValueMap fractional_preview = Preview(fractional_snapshot, workspace, source, authorization, empty);
	if(!OperationId(fractional_snapshot, workspace, source).IsEmpty() ||
	   MapGetString(fractional_preview, "status") != "invalid")
		return fail("fractional score fails closed", MapGetString(fractional_preview, "status"));
	ValueMap oversized_snapshot = unique_snapshot; ValueArray oversized_assertions; ValueMap oversized_assertion = assertions[0];
	oversized_assertion.Set("large_synthetic_text", String('x', MAX_SNAPSHOT_BYTES + 1));
	oversized_assertions.Add(oversized_assertion); oversized_snapshot.Set("assertions", oversized_assertions);
	if(!OperationId(oversized_snapshot, workspace, source).IsEmpty())
		return fail("oversized tree is rejected before operation hashing");
	ValueArray records = MapGet(preview, "records");
	if(records.GetCount() != 3) return fail("three independent records retained", AsString(records.GetCount()));
	ValueMap first = MapGet((ValueMap)records[0], "source_assertion");
	ValueMap second = MapGet((ValueMap)records[1], "source_assertion");
	ValueMap hypothesis = MapGet((ValueMap)records[2], "source_assertion");
	if(MapGetString(first, "timestamp") != "unknown" || MapGetString(first, "timestamp_state") != "unknown") return fail("missing timestamp remains explicitly unknown");
	if(MapGetString(first, "author") != "unknown" || MapGetString(first, "author_state") != "unknown") return fail("missing author remains explicitly unknown");
	if(MapGetString(first, "source_revision") != "unknown" || MapGetString(first, "source_revision_state") != "unknown") return fail("missing source revision remains explicitly unknown");
	if(MapGetString(first, "membership") != "shortlisted") return fail("shortlist membership preserved");
	if(!(bool)MapGet(first, "score_conflict") || !(bool)MapGet(second, "score_conflict"))
		return fail("task/profile score conflict marks both assertions");
	if(MapGetString(hypothesis, "submission_state") != "not-a-real-submission")
		return fail("hypothesis is not labeled as a real submission", MapGetString(hypothesis, "submission_state"));
	if(MapGetString(hypothesis, "approval_state") != "not-approved") return fail("hypothesis cannot be approved");

	ValueMap disjoint_task = assertion("disjoint-task", "task-record", "1");
	ValueMap disjoint_profile = assertion("disjoint-profile", "authored-profile", "2");
	ValueMap task_map; task_map.Add("alpha", "1"); task_map.Add("beta", "2");
	ValueMap profile_map; profile_map.Add("gamma", "3"); profile_map.Add("delta", "4");
	disjoint_task.Set("category_scores", task_map); disjoint_profile.Set("category_scores", profile_map);
	ValueMap task_narratives; task_narratives.Add("alpha", "task narrative");
	ValueMap profile_narratives; profile_narratives.Add("alpha", "profile narrative");
	disjoint_task.Set("category_narratives", task_narratives); disjoint_profile.Set("category_narratives", profile_narratives);
	disjoint_profile.Set("membership", "not-shortlisted"); disjoint_profile.Set("tier", "other-tier");
	disjoint_profile.Set("rationale", "different rationale");
	ValueArray comparison_assertions; comparison_assertions.Add(disjoint_task); comparison_assertions.Add(disjoint_profile);
	ValueMap comparison_snapshot = unique_snapshot; comparison_snapshot.Set("assertions", comparison_assertions);
	ValueMap comparison = Preview(comparison_snapshot, workspace, source, authorization, empty);
	if(MapGetString(comparison, "status") != "ready") return fail("disjoint category comparison maps", MapGetString(comparison, "status"));
	ValueArray comparison_records = MapGet(comparison, "records");
	ValueMap compared_task = MapGet((ValueMap)comparison_records[0], "source_assertion");
	ValueMap compared_profile = MapGet((ValueMap)comparison_records[1], "source_assertion");
	if(!(bool)MapGet(compared_task, "score_conflict") || !(bool)MapGet(compared_profile, "score_conflict"))
		return fail("disjoint score category keys conflict despite equal map cardinality");
	if(!(bool)MapGet(compared_task, "narrative_conflict") || !(bool)MapGet(compared_profile, "narrative_conflict"))
		return fail("narrative/membership/tier/rationale conflicts mark both assertions");
	for(const String& field : { "category_narratives", "membership", "tier", "rationale" }) {
		ValueMap left = assertion("field-left", "task-record", "same");
		ValueMap right = assertion("field-right", "authored-profile", "same");
		if(field == "category_narratives") {
			ValueMap left_map, right_map; left_map.Add("category", "left"); right_map.Add("category", "right");
			left.Set(field, left_map); right.Set(field, right_map);
		}
		else right.Set(field, "different");
		ValueArray pair; pair.Add(left); pair.Add(right);
		ValueMap pair_snapshot = unique_snapshot; pair_snapshot.Set("assertions", pair);
		ValueMap pair_preview = Preview(pair_snapshot, workspace, source, authorization, empty);
		ValueArray pair_records = MapGet(pair_preview, "records");
		if(MapGetString(pair_preview, "status") != "ready" || pair_records.GetCount() != 2 ||
		   !(bool)MapGet(MapGet((ValueMap)pair_records[0], "source_assertion"), "narrative_conflict") ||
		   !(bool)MapGet(MapGet((ValueMap)pair_records[1], "source_assertion"), "narrative_conflict"))
			return fail("individual narrative/membership/tier/rationale conflict detected", field);
	}

	ValueMap unsupported = assertion("unsupported-origin", "unknown-origin", "1");
	unsupported.Set("candidate_kind", "unknown-kind"); unsupported.Set("lifecycle_state", "untrusted-lifecycle");
	unsupported.Set("timestamp", 123); unsupported.Set("author", true); unsupported.Set("membership", ValueArray());
	unsupported.Set("tier", 7); unsupported.Set("rationale", false);
	unsupported.Remove(unsupported.Find("original_links"));
	ValueMap contradicted = assertion("contradicted-hypothesis", "hypothesis", "1");
	contradicted.Set("submission_state", "submission"); contradicted.Set("approval_state", "approved"); contradicted.Set("approved", true);
	ValueArray provenance_assertions; provenance_assertions.Add(unsupported); provenance_assertions.Add(contradicted);
	ValueMap provenance_snapshot = unique_snapshot; provenance_snapshot.Set("assertions", provenance_assertions);
	ValueMap provenance_preview = Preview(provenance_snapshot, workspace, source, authorization, empty);
	if(MapGetString(provenance_preview, "status") != "ready") return fail("unsupported provenance is retained in safe unknown state", MapGetString(provenance_preview, "status"));
	ValueArray provenance_records = MapGet(provenance_preview, "records");
	ValueMap normalized_unknown = MapGet((ValueMap)provenance_records[0], "source_assertion");
	ValueMap normalized_hypothesis = MapGet((ValueMap)provenance_records[1], "source_assertion");
	ValueMap unknown_extra = MapGet(normalized_unknown, "legacy_extra");
	ValueMap hypothesis_extra = MapGet(normalized_hypothesis, "legacy_extra");
	if(MapGetString(normalized_unknown, "origin") != "unknown" || MapGetString(normalized_unknown, "candidate_kind") != "unknown" ||
	   MapGetString(normalized_unknown, "origin_state") != "unknown" || MapGetString(normalized_unknown, "candidate_kind_state") != "unknown" ||
	   MapGetString(normalized_unknown, "lifecycle_state") != "unknown" || MapGetString(normalized_unknown, "lifecycle_state_state") != "unknown" ||
	   MapGetString(normalized_unknown, "timestamp") != "unknown" ||
	   MapGetString(normalized_unknown, "author") != "unknown" || MapGetString(normalized_unknown, "membership") != "unknown" ||
	   MapGetString(normalized_unknown, "rubric_revision") != "unknown" || MapGetString(normalized_unknown, "rubric_revision_state") != "unknown" ||
	   MapGetString(normalized_unknown, "candidate_link_state") != "unresolved" || MapGetString(normalized_unknown, "source_link_state") != "unknown" ||
	   MapGetString(normalized_unknown, "tier") != "unknown" || MapGetString(normalized_unknown, "rationale") != "unknown" ||
	   MapGetString(unknown_extra, "origin") != "unknown-origin" || MapGetString(unknown_extra, "candidate_kind") != "unknown-kind")
		return fail("unsupported provenance normalized and raw inputs retained", AsJSON(normalized_unknown, true));
	if(MapGetString(normalized_hypothesis, "submission_state") != "not-a-real-submission" ||
	   MapGetString(normalized_hypothesis, "approval_state") != "not-approved" ||
	   MapGetString(hypothesis_extra, "submission_state") != "submission" || MapGetString(hypothesis_extra, "approval_state") != "approved")
		return fail("hypothesis contradictory submission/approval input is overridden and retained");
	return true;
}

END_UPP_NAMESPACE
