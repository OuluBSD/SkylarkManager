#include "DataStoreInternal.h"
#include <stdio.h>
#include <errno.h>
#ifdef PLATFORM_POSIX
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>
#endif

NAMESPACE_UPP

const Vector<SchemaCard>& DataStore::GetDefaultSchemaCards()
{
	struct DefaultRegistry {
		Vector<SchemaCard> cards;
		DefaultRegistry() {
			static const char* defs[][2] = {
				{ "paper",              "id,kind,title,source,status" },
				{ "paper-analysis",     "id,kind,paper,repo,version,tasks,findings" },
				{ "task-proposal",      "id,kind,title,status" },
				{ "research-candidate", "id,kind,title,status" },
				{ "topology",           "id,kind,name" },
				{ "goal",               "id,kind" },
				{ "issue",              "id,kind" },
				{ "release",            "id,kind" },
				{ "evidence-snapshot",  "id,kind,workspace_id,source_kind,source_identity,upstream_revision,content_sha256,captured_at,source_at,collector,locator,resolution,content,classification,visibility,retracted,predecessor,contributors" },
				{ "evidence-item",      "id,kind,workspace_id,snapshot,source_identity,upstream_revision,content_sha256,locator,resolution,excerpt,classification,visibility,contributors" },
				{ "paper-source-snapshot", "id,kind,workspace_id,source_identity,source_url,final_url,external_version,upstream_revision,source_sha256,extraction_sha256,captured_at,rule_id,internal_use_mode,license_id,license_evidence_ref,extractor,extractor_version,config_revision,mime_type,transport_installed,request_performed,redirect_chain,peer_address_state,section_count,envelope" },
				{ "paper-research-run", "id,kind,workspace_id,revision,status,paper_snapshot_id,paper_revision,external_version,extraction_sha256,source_sha256,extractor,extractor_version,extraction_config_revision,generator_id,generator_revision,rubric_id,rubric_revision,provider_id,provider_config_revision,claim_count,supported_claim_count,unsupported_claim_count,candidate_draft_count,evidence_ids,candidate_ids,findings,blockers,operation_id,envelope" },
				{ "paper-candidate-handoff", "id,kind,workspace_id,revision,status,run_id,run_revision,paper_snapshot_id,paper_revision,extraction_sha256,source_identity,supported_candidate_ids,duplicates,possible_duplicates,blockers,operation_id,envelope" },
				{ "historical-assessment", "id,kind,migration_identity,snapshot_id,snapshot_revision,source_assertion" },
				{ "assessment-rubric", "id,kind,workspace_id,revision,provenance,dimensions,score_scale,total_rule,coverage_required,tiers" },
				{ "analysis-run", "id,kind,workspace_id,revision,provenance,status,rubric_id,rubric_revision,tool_config,model_config,input_revisions,unique_source_count,unique_source_revision_count,unique_evidence_count,evidence_keys,relationships,overlap_groups,uncertainty,source_provenance" },
				{ "assessment-finding", "id,kind,workspace_id,revision,provenance,analysis_run,finding" },
				{ "category-assessment", "id,kind,workspace_id,revision,provenance,candidate_id,analysis_run,assessment" },
				{ "candidate-assessment", "id,kind,workspace_id,revision,provenance,candidate_id,analysis_run,assessment" },
				{ "assessment-override", "id,kind,workspace_id,revision,provenance,target_id,target_revision,candidate_id,dimension_id,field,computed_value,prior_value,replacement,reason,actor,authority,timestamp,previous_override_id,operation_id" },
				{ "assessment-staleness", "id,kind,workspace_id,revision,provenance,prior_run_id,replacement_run_id,reason_flags" },
				{ "observed-submission", "id,kind,workspace_id,schema_id,schema_version,origin_kind,owner_kind,provenance,uncertainty,submission_state,author_state,intent_state,external_identity,relationships" },
				{ "local-interpretation", "id,kind,workspace_id,schema_id,schema_version,origin_kind,owner_kind,provenance,uncertainty,submission_state,approval_state,author_state,intent_state,hypothetical,relationships" },
				{ "local-candidate", "id,kind,workspace_id,schema_id,schema_version,origin_kind,owner_kind,provenance,uncertainty,candidate_kind,relationships" },
				{ "proposal-revision", "id,kind,workspace_id,schema_id,schema_version,origin_kind,owner_kind,provenance,uncertainty,submission_state,approval_state,author_state,intent_state,hypothetical,revision,proposal_id,revision_id,candidate_id,relationships" },
				{ "selection-decision", "id,kind,workspace_id,schema_id,schema_version,origin_kind,owner_kind,provenance,uncertainty,actor,decision,relationships" },
				{ "candidate-proposal", "id,kind,workspace_id,revision,provenance,candidate_id,proposal_revision_id,candidate_kind,title,purpose,quality_contract,inputs,outputs,runtime_resources,assets,verification_plan,evidence_links,draft_state,submission_state,approval_state,author_state,intent_state,hypothetical,envelope" },
				{ "candidate-stage-event", "id,kind,workspace_id,revision,provenance,candidate_id,candidate_revision,proposal_revision_id,stage,previous_event_id,reason,evidence_ids,operation_id,actor,authority,timestamp,envelope" },
				{ "candidate-stage-head", "id,kind,workspace_id,revision,provenance,candidate_id,proposal_revision_id,stage,event_id,envelope" },
				{ "candidate-proposal-head", "id,kind,workspace_id,revision,provenance,candidate_id,proposal_id,proposal_revision_id,revision_id,envelope" },
				{ "durable-job", "id,kind,workspace_id,revision,provenance,state,request_id,request,operation_key,request_digest,inputs,contributors,provider_id,model_config_revision,policy_snapshot,attempt,created_at,updated_at,budget_ms,max_attempts,max_output_bytes,retryable,idempotency_safe,event_ids,run_ids" },
				{ "job-event", "id,kind,workspace_id,revision,provenance,job_id,sequence,from_state,to_state,operation_key,timestamp,reason" },
				{ "job-run", "id,kind,workspace_id,revision,provenance,job_id,attempt,attempt_id,state,started_at,finished_at,elapsed_ms,output_bytes,inputs,retryable,idempotency_safe" },
				{ "review-package-revision", "id,kind,workspace_id,package_id,goal_id,scope_id,base_revision,base_state,head_revision,previous_revision,selected_paths,patch_bytes,patch_sha256,artifact_refs,check_refs,artifacts,author,author_state,capture_state,envelope" },
				{ "review-thread", "id,kind,workspace_id,package_id,revision_id,revision_hash,source_identity,envelope" },
				{ "review-comment", "id,kind,workspace_id,package_id,revision_id,revision_hash,thread_id,record_type,author_kind,author,body,artifact_refs,check_refs,source_identity,envelope" },
			};
			for(int i = 0; i < (int)__countof(defs); i++) {
				SchemaCard c;
				c.kind = defs[i][0];
				c.schema_id = "skylark/" + c.kind;
				c.schema_version = c.kind == "evidence-snapshot" || c.kind == "evidence-item" || c.kind == "durable-job" || c.kind == "job-event" || c.kind == "job-run" || c.kind == "paper-research-run" || c.kind == "paper-candidate-handoff" ? "2" : "1";
				c.id_pattern = "^[A-Za-z0-9][A-Za-z0-9._/-]*$";
				Vector<String> req = Split(defs[i][1], ',');
				for(int r = 0; r < req.GetCount(); r++)
					c.required.Add(TrimBoth(req[r]));
				c.collection = "builtin";
				auto field = [&](const String& name, const String& type) -> SchemaCard::Field& {
					SchemaCard::Field& f = c.fields.Add(); f.name = name; f.type = type; return f;
				};
				SchemaCard::Field& id_field = field("id", "string");
				if(c.kind == "evidence-snapshot" || c.kind == "evidence-item") id_field.max_utf8_bytes = 256;
				if(c.kind == "assessment-override" || c.kind == "assessment-staleness") id_field.max_utf8_bytes = 256;
				SchemaCard::Field& kind_field=field("kind", "string");if(c.kind=="assessment-override"||c.kind=="assessment-staleness")kind_field.max_utf8_bytes=64;
				if(c.kind == "paper") {
					field("title", "string"); field("source", "string");
					field("abstract", "string");
					SchemaCard::Field& status = field("status", "string"); status.enum_values.Add("draft"); status.enum_values.Add("screened"); status.enum_values.Add("published"); status.enum_values.Add("archived");
				}
				else if(c.kind == "paper-analysis") {
					SchemaCard::Field& paper = field("paper", "string"); paper.reference_kind = "paper";
					field("repo", "string");
					field("version", "string");
					// These collections are arrays of opaque JSON objects; source-byte and item-depth limits bound input.
					SchemaCard::Field& tasks = field("tasks", "array"); tasks.item_type = "object";
					SchemaCard::Field& findings = field("findings", "array"); findings.item_type = "object";
				}
				else if(c.kind == "task-proposal" || c.kind == "research-candidate") {
					field("title", "string"); SchemaCard::Field& status = field("status", "string"); status.enum_values.Add("proposed"); status.enum_values.Add("accepted"); status.enum_values.Add("rejected"); status.enum_values.Add("completed");
				}
				else if(c.kind == "topology") {
					field("name", "string");
					SchemaCard::Field& nodes = field("nodes", "array"); nodes.item_type = "object";
					SchemaCard::Field& id = nodes.item_properties.Add(); id.name = "id"; id.type = "string"; id.required = true;
					SchemaCard::Field& type = nodes.item_properties.Add(); type.name = "type"; type.type = "string"; type.required = true;
					SchemaCard::Field& name = nodes.item_properties.Add(); name.name = "name"; name.type = "string"; name.required = true;
					SchemaCard::Field& description = nodes.item_properties.Add(); description.name = "description"; description.type = "string";
					SchemaCard::Field& metadata = nodes.item_properties.Add(); metadata.name = "metadata"; metadata.type = "object";
					SchemaCard::Field& category = metadata.properties.Add(); category.name = "category"; category.type = "string"; category.required = true; category.enum_values.Add("primary"); category.enum_values.Add("secondary");
					SchemaCard::Field& label = metadata.properties.Add(); label.name = "label"; label.type = "string";
				}
				else if(c.kind == "evidence-snapshot") {
					field("workspace_id", "string").max_utf8_bytes = 128;
					SchemaCard::Field& source_kind = field("source_kind", "string");
					for(const char *v : { "pr-review", "commit-diff", "message-thread", "paper" }) source_kind.enum_values.Add(v);
					field("source_identity", "string").max_utf8_bytes = 256; field("upstream_revision", "string").max_utf8_bytes = 256; field("previous_revision", "string").max_utf8_bytes = 256; field("content_sha256", "string").max_utf8_bytes = 64;
					field("captured_at", "string").max_utf8_bytes = 64; field("source_at", "string").max_utf8_bytes = 64; field("collector", "string").max_utf8_bytes = 128;
					SchemaCard::Field& locator = field("locator", "object");
					locator.allow_additional_properties = false;
					SchemaCard::Field& locator_type = locator.properties.Add(); locator_type.name = "type"; locator_type.type = "string"; locator_type.required = true; locator_type.max_utf8_bytes = 32;
					for(const char *tag : { "pr-review", "commit-line", "message-thread", "paper-section" }) locator_type.enum_values.Add(tag);
					for(const char *name : { "id", "revision", "thread", "section", "path" }) { SchemaCard::Field& f = locator.properties.Add(); f.name = name; f.type = "string"; f.required = String(name) == "id"; f.max_utf8_bytes = String(name) == "path" ? 240 : 128; }
					for(const char *name : { "first", "last", "page" }) { SchemaCard::Field& f = locator.properties.Add(); f.name = name; f.type = "integer"; f.min_integer = 1; f.max_integer = 100000; }
					SchemaCard::Field& resolution = field("resolution", "string");
					for(const char *v : { "resolved", "unresolved", "invalid", "ambiguous", "denied", "retracted" }) resolution.enum_values.Add(v);
					field("content", "string").max_utf8_bytes = 256 * 1024;
					SchemaCard::Field& contributors = field("contributors", "array"); contributors.item_type = "string"; contributors.max_items = 256; contributors.item_max_utf8_bytes = 256;
					SchemaCard::Field& source_lines = field("source_lines", "array"); source_lines.item_type = "integer"; source_lines.max_items = 1024; source_lines.min_integer = 1; source_lines.max_integer = 100000;
					SchemaCard::Field& classification = field("classification", "string");
					for(const char *v : { "public", "private", "protected", "unknown" }) classification.enum_values.Add(v);
					SchemaCard::Field& visibility = field("visibility", "string"); visibility.enum_values.Add("visible"); visibility.enum_values.Add("restricted");
					field("retracted", "bool"); field("predecessor", "string").max_utf8_bytes = 256;
				}
				else if(c.kind == "evidence-item") {
					field("workspace_id", "string").max_utf8_bytes = 128; SchemaCard::Field& snapshot = field("snapshot", "string"); snapshot.reference_kind = "evidence-snapshot"; snapshot.max_utf8_bytes = 256;
					field("source_identity", "string").max_utf8_bytes = 256; field("upstream_revision", "string").max_utf8_bytes = 256; field("content_sha256", "string").max_utf8_bytes = 64;
					SchemaCard::Field& locator = field("locator", "object"); locator.allow_additional_properties = false;
					SchemaCard::Field& locator_type = locator.properties.Add(); locator_type.name = "type"; locator_type.type = "string"; locator_type.required = true; locator_type.max_utf8_bytes = 32;
					for(const char *tag : { "pr-review", "commit-line", "message-thread", "paper-section" }) locator_type.enum_values.Add(tag);
					for(const char *name : { "id", "revision", "thread", "section", "path" }) { SchemaCard::Field& f = locator.properties.Add(); f.name = name; f.type = "string"; f.required = String(name) == "id"; f.max_utf8_bytes = String(name) == "path" ? 240 : 128; }
					for(const char *name : { "first", "last", "page" }) { SchemaCard::Field& f = locator.properties.Add(); f.name = name; f.type = "integer"; f.min_integer = 1; f.max_integer = 100000; }
					SchemaCard::Field& resolution = field("resolution", "string");
					for(const char *v : { "resolved", "unresolved", "invalid", "ambiguous", "denied", "retracted" }) resolution.enum_values.Add(v);
					field("excerpt", "string").max_utf8_bytes = 4 * 1024;
					SchemaCard::Field& contributors = field("contributors", "array"); contributors.item_type = "string"; contributors.max_items = 256; contributors.item_max_utf8_bytes = 256;
					SchemaCard::Field& source_lines = field("source_lines", "array"); source_lines.item_type = "integer"; source_lines.max_items = 1024; source_lines.min_integer = 1; source_lines.max_integer = 100000;
					SchemaCard::Field& classification = field("classification", "string");
					for(const char *v : { "public", "private", "protected", "unknown" }) classification.enum_values.Add(v);
					SchemaCard::Field& visibility = field("visibility", "string"); visibility.enum_values.Add("visible"); visibility.enum_values.Add("restricted");
				}
				else if(c.kind == "paper-source-snapshot") {
					field("workspace_id","string").max_utf8_bytes=128;
					field("source_identity","string").max_utf8_bytes=256;
					field("source_url","string").max_utf8_bytes=2048;
					field("final_url","string").max_utf8_bytes=2048;
					field("external_version","string").max_utf8_bytes=256;
					field("upstream_revision","string").max_utf8_bytes=512;
					field("source_sha256","string").max_utf8_bytes=64;
					field("extraction_sha256","string").max_utf8_bytes=64;
					field("captured_at","string").max_utf8_bytes=64;
					field("rule_id","string").max_utf8_bytes=128;
					field("internal_use_mode","string").max_utf8_bytes=64;
					field("license_id","string").max_utf8_bytes=128;
					SchemaCard::Field& license=field("license_evidence_ref","string"); license.max_utf8_bytes=256; license.reference_kind="evidence-item";
					for(const char *n:{"extractor","extractor_version","config_revision"}) field(n,"string").max_utf8_bytes=128;
					field("mime_type","string").max_utf8_bytes=64;
					field("transport_installed","boolean"); field("request_performed","boolean");
					SchemaCard::Field& redirects=field("redirect_chain","array"); redirects.item_type="string"; redirects.max_items=3; redirects.item_max_utf8_bytes=2048;
					SchemaCard::Field& peer=field("peer_address_state","string"); peer.enum_values.Add("not_observed_transport_disabled"); peer.enum_values.Add("public_dns_pinned_tls_verified");
					field("transport_id","string").max_utf8_bytes=32;
					field("transport_version","string").max_utf8_bytes=64;
					SchemaCard::Field& verification=field("transport_verification_state","string"); verification.enum_values.Add("disabled"); verification.enum_values.Add("tls_peer_and_hostname_verified");
					SchemaCard::Field& count=field("section_count","integer"); count.min_integer=1; count.max_integer=64;
					SchemaCard::Field& env=field("envelope","object"); env.allow_additional_properties=false;
					for(const char *n:{"schema_id","schema_version","workspace_id","stable_id","record_kind","revision","visibility","provenance_ref"}) { SchemaCard::Field& f=env.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
				}
				else if(c.kind == "historical-assessment") {
					field("migration_identity", "string").max_utf8_bytes = 128;
					field("snapshot_id", "string").max_utf8_bytes = 256;
					field("snapshot_revision", "string").max_utf8_bytes = 256;
					SchemaCard::Field& assertion = field("source_assertion", "object");
					assertion.allow_additional_properties = false;
					auto prop = [&](const String& name, const String& type, int max_bytes = -1) -> SchemaCard::Field& {
						SchemaCard::Field& f = assertion.properties.Add(); f.name = name; f.type = type;
						f.required = true; f.max_utf8_bytes = max_bytes; return f;
					};
					for(const char *name : { "source_id", "candidate_id", "timestamp", "author", "rubric", "rubric_revision",
					                         "source_revision", "membership", "tier", "rationale", "origin_state",
					                         "candidate_kind_state", "lifecycle_state_state", "timestamp_state", "author_state",
					                         "rubric_state", "rubric_revision_state", "source_revision_state", "membership_state",
					                         "tier_state", "rationale_state" }) prop(name, "string", String(name) == "rationale" ? 8192 : 512);
					for(const char *name : { "origin_state", "candidate_kind_state", "lifecycle_state_state", "timestamp_state",
					                         "author_state", "rubric_state", "rubric_revision_state", "source_revision_state",
					                         "membership_state", "tier_state", "rationale_state" }) {
						for(SchemaCard::Field& state : assertion.properties)
							if(state.name == name) { state.enum_values.Add("known"); state.enum_values.Add("unknown"); break; }
					}
					SchemaCard::Field& origin = prop("origin", "string", 32);
					for(const char *v : { "task-record", "authored-profile", "hypothesis", "unknown" }) origin.enum_values.Add(v);
					SchemaCard::Field& candidate_kind = prop("candidate_kind", "string", 32);
					for(const char *v : { "candidate", "proposal", "research-stage", "unknown" }) candidate_kind.enum_values.Add(v);
					SchemaCard::Field& lifecycle = prop("lifecycle_state", "string", 64);
					for(const char *v : { "unknown", "proposed", "submitted", "approved", "rejected", "completed", "research-stage" }) lifecycle.enum_values.Add(v);
					for(const char *name : { "candidate_link_state", "source_link_state", "links_state" }) {
						SchemaCard::Field& f = prop(name, "string", 16);
						for(const char *v : { "unknown", "resolved", "unresolved" }) f.enum_values.Add(v);
					}
					SchemaCard::Field& submission = prop("submission_state", "string", 32);
					for(const char *v : { "unknown", "submission", "not-a-real-submission" }) submission.enum_values.Add(v);
					SchemaCard::Field& approval = prop("approval_state", "string", 32);
					for(const char *v : { "unknown", "approved", "not-approved" }) approval.enum_values.Add(v);
					for(const char *name : { "score_conflict", "narrative_conflict" }) prop(name, "bool");
					for(const char *name : { "original_links", "resolved_links" }) {
						SchemaCard::Field& links = prop(name, "array"); links.item_type = "string";
						links.max_items = 256; links.item_max_utf8_bytes = 512;
					}
					for(const char *pair : { "category_scores", "category_narratives" }) {
						SchemaCard::Field& entries = prop(pair, "array"); entries.item_type = "object"; entries.max_items = 256;
						entries.allow_additional_properties = false;
						SchemaCard::Field& category = entries.item_properties.Add(); category.name = "category"; category.type = "string"; category.required = true; category.max_utf8_bytes = 128;
						bool score_entry = String(pair) == "category_scores";
						SchemaCard::Field& text = entries.item_properties.Add(); text.name = score_entry ? "score" : "narrative";
						text.type = "string"; text.required = true; text.max_utf8_bytes = score_entry ? 512 : 8192;
					}
					SchemaCard::Field& legacy = prop("legacy_extra", "object"); legacy.allow_additional_properties = true;
				}
				else if(c.kind == "observed-submission" || c.kind == "local-interpretation" || c.kind == "local-candidate" || c.kind == "proposal-revision" || c.kind == "selection-decision") {
					field("workspace_id", "string").max_utf8_bytes = 128;
					for(const char *n : { "schema_id", "schema_version", "origin_kind", "owner_kind", "provenance", "uncertainty" }) field(n,"string").max_utf8_bytes = 512;
					for(SchemaCard::Field& f:c.fields) if(f.name=="origin_kind") {
						if(c.kind=="observed-submission") f.enum_values.Add("external-observation");
						else if(c.kind=="local-interpretation") f.enum_values.Add("local-hypothesis");
						else if(c.kind=="proposal-revision") { f.enum_values.Add("locally-authored"); f.enum_values.Add("local-hypothesis"); }
						else if(c.kind=="local-candidate") f.enum_values.Add("locally-authored");
						else f.enum_values.Add("human-decision");
					}
					else if(f.name=="owner_kind") {
						if(c.kind=="observed-submission") { f.enum_values.Add("external-unknown"); f.enum_values.Add("external-evidenced"); }
						else if(c.kind=="local-interpretation") f.enum_values.Add("local-unknown-author");
						else if(c.kind=="proposal-revision") { f.enum_values.Add("local-unknown-author"); f.enum_values.Add("local-author"); }
						else if(c.kind=="selection-decision") f.enum_values.Add("human-author");
						else f.enum_values.Add("local-author");
					}
					SchemaCard::Field& rels=field("relationships","array"); rels.item_type="object"; rels.max_items=128; rels.allow_additional_properties=false;
					for(const char *n : { "id", "type", "workspace_id", "target_id", "target_kind", "resolution" }) { SchemaCard::Field& f=rels.item_properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; if(String(n)=="type") for(const char *v:{"observes","interprets","candidate","revision-parent","decision-target","evidence"}) f.enum_values.Add(v); if(String(n)=="target_kind") for(const char *v:{"observed-submission","local-interpretation","local-candidate","proposal-revision","selection-decision","evidence-item","evidence-snapshot"}) f.enum_values.Add(v); if(String(n)=="resolution") for(const char *v:{"resolved","unresolved","ambiguous","denied"}) f.enum_values.Add(v); }
					field("source_identity","string").max_utf8_bytes=256;
					SchemaCard::Field& env=field("envelope","object"); env.allow_additional_properties=false;
					for(const char *n:{"schema_id","schema_version","workspace_id","stable_id","record_kind","revision","visibility","provenance_ref"}) { SchemaCard::Field& f=env.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
					SchemaCard::Field& evidence=field("evidence","array"); evidence.item_type="string"; evidence.max_items=128; evidence.item_max_utf8_bytes=256;
					SchemaCard::Field& counter=field("counterevidence","array"); counter.item_type="string"; counter.max_items=128; counter.item_max_utf8_bytes=256;
					if(c.kind=="observed-submission") { field("external_identity","string").max_utf8_bytes=256; SchemaCard::Field& f=field("submission_state","string"); f.enum_values.Add("observed"); SchemaCard::Field& author=field("author_state","string"); author.max_utf8_bytes=32; author.enum_values.Add("unknown"); author.enum_values.Add("evidenced"); SchemaCard::Field& intent=field("intent_state","string"); intent.max_utf8_bytes=32; intent.enum_values.Add("unknown"); intent.enum_values.Add("evidenced"); }
					if(c.kind=="local-interpretation") { SchemaCard::Field& submission=field("submission_state","string"); submission.enum_values.Add("not-a-real-submission"); SchemaCard::Field& approval=field("approval_state","string"); approval.enum_values.Add("not-approved"); SchemaCard::Field& author=field("author_state","string"); author.enum_values.Add("unknown"); SchemaCard::Field& intent=field("intent_state","string"); intent.enum_values.Add("unknown"); field("hypothetical","boolean"); }
					if(c.kind=="local-candidate") { SchemaCard::Field& f=field("candidate_kind","string"); for(const char *v:{"candidate","proposal","research-stage"}) f.enum_values.Add(v); }
					if(c.kind=="proposal-revision") { SchemaCard::Field& submission=field("submission_state","string"); submission.enum_values.Add("not-submitted"); submission.enum_values.Add("not-a-real-submission"); SchemaCard::Field& approval=field("approval_state","string"); approval.enum_values.Add("not-approved"); SchemaCard::Field& author=field("author_state","string"); author.enum_values.Add("unknown"); author.enum_values.Add("known-local-author"); SchemaCard::Field& intent=field("intent_state","string"); intent.enum_values.Add("unknown"); field("hypothetical","boolean"); SchemaCard::Field& revision=field("revision","string"); revision.enum_values.Add("draft"); for(const char *n : {"proposal_id","revision_id","parent_revision_id","candidate_id"}) field(n,"string").max_utf8_bytes=256; }
					if(c.kind=="selection-decision") { field("actor","string").max_utf8_bytes=256; field("decision","string").max_utf8_bytes=64; }
				}
				else if(c.kind == "assessment-rubric") {
					field("workspace_id", "string"); field("revision", "string");
					SchemaCard::Field& provenance=field("provenance","string"); provenance.enum_values.Add("authored");
					SchemaCard::Field& dimensions=field("dimensions","array"); dimensions.item_type="object"; dimensions.max_items=6;
					dimensions.allow_additional_properties=false;
					for(const char *name : {"id","weight","minimum_score","maximum_score","minimum_evidence_count"}) { SchemaCard::Field& f=dimensions.item_properties.Add(); f.name=name; f.type=String(name)=="id"?"string":"integer"; f.required=true; if(f.type=="integer"){f.min_integer=String(name)=="weight"||String(name)=="minimum_evidence_count"?1:-1000000;f.max_integer=1000000;} }
					SchemaCard::Field& score_scale=field("score_scale","string");for(const char *v:{"integer-0-5","integer-0-10","integer-1-5","integer-1-10"})score_scale.enum_values.Add(v);
					SchemaCard::Field& total_rule=field("total_rule","string");total_rule.enum_values.Add("weighted-sum");total_rule.enum_values.Add("weighted-mean");
					SchemaCard::Field& coverage=field("coverage_required","array"); coverage.item_type="string"; coverage.max_items=64;
					SchemaCard::Field& tiers=field("tiers","array"); tiers.item_type="object"; tiers.max_items=64; tiers.allow_additional_properties=false;
					for(const char *name : {"id","minimum_numerator","minimum_denominator","maximum_numerator","maximum_denominator"}) { SchemaCard::Field& f=tiers.item_properties.Add(); f.name=name; f.type=String(name)=="id"?"string":"integer"; f.required=true; if(f.type=="integer"){f.min_integer=String(name).Find("denominator")>=0?1:0;f.max_integer=1000000000;} }
				}
				else if(c.kind == "analysis-run") {
					field("workspace_id","string"); field("revision","string");
					SchemaCard::Field& provenance=field("provenance","string"); for(const char *v : {"computed","authored","ai","historical","override","unknown"}) provenance.enum_values.Add(v);
					field("status","string"); field("rubric_id","string"); field("rubric_revision","string"); field("tool_config","string"); field("model_config","string");
					auto string_prop=[](SchemaCard::Field& parent,const String& name,bool required=true)->SchemaCard::Field&{SchemaCard::Field& f=parent.item_properties.Add();f.name=name;f.type="string";f.required=required;f.max_utf8_bytes=512;return f;};
					SchemaCard::Field& inputs=field("input_revisions","array");inputs.item_type="object";inputs.max_items=4096;inputs.allow_additional_properties=false;
					for(const char *n:{"workspace_id","id","revision"}) string_prop(inputs,n);
					SchemaCard::Field& rels=field("relationships","array");rels.item_type="object";rels.max_items=4096;rels.allow_additional_properties=false;
					for(const char *n:{"workspace_id","left","left_kind","right","right_kind","type"}) {SchemaCard::Field& f=string_prop(rels,n);if(String(n)=="type")for(const char *v:{"overlap","related","duplicate","depends-on"})f.enum_values.Add(v);if(String(n)=="left_kind"||String(n)=="right_kind")for(const char *v:{"candidate","source"})f.enum_values.Add(v);}
					SchemaCard::Field& groups=field("overlap_groups","array");groups.item_type="object";groups.max_items=4096;groups.allow_additional_properties=false;string_prop(groups,"group_id");
					SchemaCard::Field& members=groups.item_properties.Add();members.name="members";members.type="array";members.required=true;members.item_type="string";members.max_items=4096;members.item_max_utf8_bytes=512;
					SchemaCard::Field& sources=field("source_provenance","array");sources.item_type="object";sources.max_items=4096;sources.allow_additional_properties=false;
					for(const char *n:{"workspace_id","identity","revision"})string_prop(sources,n);
					SchemaCard::Field& evidence_keys=field("evidence_keys","array"); evidence_keys.item_type="string"; evidence_keys.max_items=4096; evidence_keys.item_max_utf8_bytes=512;
					for(const char *n : {"unique_source_count","unique_source_revision_count","unique_evidence_count"}) { SchemaCard::Field& f=field(n,"integer"); f.min_integer=0; f.max_integer=4096; }
					field("uncertainty","string");
				}
				else if(c.kind == "assessment-finding" || c.kind == "category-assessment" || c.kind == "candidate-assessment") {
					field("workspace_id","string"); field("revision","string");
					SchemaCard::Field& provenance=field("provenance","string"); for(const char *v : {"computed","authored","ai","historical","override","unknown"}) provenance.enum_values.Add(v);
					field("analysis_run","string");
					if(c.kind != "assessment-finding") field("candidate_id","string");
					if(c.kind == "assessment-finding") {
						SchemaCard::Field& finding=field("finding","object");finding.allow_additional_properties=false;
						for(const char *n:{"workspace_id","code","candidate_id","dimension_id","evidence_id","provenance"}){SchemaCard::Field& f=finding.properties.Add();f.name=n;f.type="string";f.required=String(n)=="code";f.max_utf8_bytes=512;}
						for(const char *n:{"evidence_ids","supporting_evidence_ids","counterevidence_ids","source_ids"}){SchemaCard::Field& f=finding.properties.Add();f.name=n;f.type="array";f.item_type="string";f.max_items=4096;f.item_max_utf8_bytes=512;}
					}
					else if(c.kind=="category-assessment") {
						SchemaCard::Field& assessment=field("assessment","object");assessment.allow_additional_properties=false;
						for(const char *n:{"workspace_id","candidate_id","dimension_id","score","provenance"}){SchemaCard::Field& f=assessment.properties.Add();f.name=n;f.type="string";f.required=true;f.max_utf8_bytes=512;}
						SchemaCard::Field& refs=assessment.properties.Add();refs.name="evidence_ids";refs.type="array";refs.item_type="string";refs.required=true;refs.max_items=4096;refs.item_max_utf8_bytes=512;
						for(const char *n:{"rationale","crowding_judgment","opportunity_gap"}){SchemaCard::Field& f=assessment.properties.Add();f.name=n;f.type="string";f.max_utf8_bytes=4096;}
					}
					else {
						SchemaCard::Field& assessment=field("assessment","object");assessment.allow_additional_properties=false;
						for(const char *n:{"candidate_id","provenance","uncertainty","total_numerator","total_denominator","tier","total_state"}){SchemaCard::Field& f=assessment.properties.Add();f.name=n;f.type="string";f.required=String(n)=="candidate_id"||String(n)=="provenance"||String(n)=="uncertainty";f.max_utf8_bytes=512;}
						SchemaCard::Field& dimensions=assessment.properties.Add();dimensions.name="dimensions";dimensions.type="array";dimensions.item_type="object";dimensions.required=true;dimensions.max_items=6;dimensions.allow_additional_properties=false;
						for(const char *n:{"dimension_id","score","score_state","coverage_state"}){SchemaCard::Field& f=dimensions.item_properties.Add();f.name=n;f.type="string";f.required=true;f.max_utf8_bytes=128;}
						SchemaCard::Field& evidence_count=dimensions.item_properties.Add();evidence_count.name="evidence_count";evidence_count.type="integer";evidence_count.required=true;evidence_count.min_integer=0;evidence_count.max_integer=4096;
					}
				}
				else if(c.kind == "assessment-override") {
					field("workspace_id","string").max_utf8_bytes=256; field("revision","string").max_utf8_bytes=32;
					SchemaCard::Field& provenance=field("provenance","string");provenance.max_utf8_bytes=16;provenance.enum_values.Add("authored");
					SchemaCard::Field& target=field("target_id","string"); target.reference_kind="candidate-assessment"; target.max_utf8_bytes=256;
					field("target_revision","string").max_utf8_bytes=32; field("candidate_id","string").max_utf8_bytes=256;
					field("dimension_id","string").max_utf8_bytes=128;
					SchemaCard::Field& field_name=field("field","string"); field_name.max_utf8_bytes=16; field_name.enum_values.Add("score");
					field("computed_value","string").max_utf8_bytes=16; field("prior_value","string").max_utf8_bytes=16; field("replacement","string").max_utf8_bytes=16;
					field("reason","string").max_utf8_bytes=4096; field("actor","string").max_utf8_bytes=256;
					SchemaCard::Field& authority=field("authority","string"); authority.max_utf8_bytes=32; authority.enum_values.Add("record:write");
					field("timestamp","string").max_utf8_bytes=20;
					field("previous_override_id","string").max_utf8_bytes=256; field("operation_id","string").max_utf8_bytes=64;
				}
				else if(c.kind == "assessment-staleness") {
					field("workspace_id","string").max_utf8_bytes=256; field("revision","string").max_utf8_bytes=32;
					SchemaCard::Field& provenance=field("provenance","string");provenance.max_utf8_bytes=16;provenance.enum_values.Add("computed");
					SchemaCard::Field& prior=field("prior_run_id","string"); prior.reference_kind="analysis-run"; prior.max_utf8_bytes=256;
					SchemaCard::Field& replacement=field("replacement_run_id","string"); replacement.reference_kind="analysis-run"; replacement.max_utf8_bytes=256;
					SchemaCard::Field& flags=field("reason_flags","object");flags.allow_additional_properties=false;
					for(const char *name:{"source_dependency_set_changed","source_revision_changed","input_revision_changed","rubric_revision_changed","tool_config_changed","model_config_changed"}){
						SchemaCard::Field& f=flags.properties.Add();f.name=name;f.type="boolean";f.required=true;
					}
				}
				else if(c.kind == "paper-research-run") {
					field("workspace_id", "string").max_utf8_bytes=128; field("revision", "string").max_utf8_bytes=32;
					SchemaCard::Field& status=field("status", "string"); status.enum_values.Add("candidate_drafts_ready"); status.enum_values.Add("no_supported_candidate");
					auto research_pin=[&](SchemaCard::Field& pin,bool license) { pin.type="object"; pin.allow_additional_properties=false; for(const char *n:{"qualified_id","revision","source_revision"}) { SchemaCard::Field& f=pin.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; } if(license) { for(const char *n:{"source_identity","upstream_revision","content_sha256"}) { SchemaCard::Field& f=pin.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=2048; } SchemaCard::Field& f=pin.properties.Add(); f.name="locator"; f.type="object"; f.required=true; } else { for(const char *n:{"source_identity","source_url","final_url","external_version","upstream_revision","source_sha256","extraction_sha256","extractor","extractor_version","config_revision","rule_id","internal_use_mode","license_id","license_evidence_ref"}) { SchemaCard::Field& f=pin.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=2048; } } };
					research_pin(field("source_pin","object"),false); research_pin(field("license_pin","object"),true);
					for(const char *n : { "paper_snapshot_id", "paper_revision", "external_version", "extraction_sha256", "source_sha256", "extractor", "extractor_version", "extraction_config_revision", "generator_id", "generator_revision", "rubric_id", "rubric_revision", "provider_id", "provider_config_revision" }) field(n, "string").max_utf8_bytes=256;
					for(const char *n : { "claim_count", "supported_claim_count", "unsupported_claim_count", "candidate_draft_count" }) { SchemaCard::Field& count=field(n,"integer"); count.min_integer=0; count.max_integer=64; }
					field("operation_id", "string").max_utf8_bytes=64;
					for(const char *n : { "evidence_ids", "candidate_ids", "blockers" }) { SchemaCard::Field& a=field(n, "array"); a.item_type="string"; a.max_items=64; a.item_max_utf8_bytes=512; }
					SchemaCard::Field& findings=field("findings", "array"); findings.item_type="object"; findings.max_items=64; findings.allow_additional_properties=false;
					for(const char *n : { "claim_index", "text", "assertion", "evidence_ids", "counterevidence_ids", "uncertainty", "reason" }) {
						SchemaCard::Field& f=findings.item_properties.Add(); f.name=n;
						if(String(n)=="claim_index") { f.type="integer"; f.required=true; f.min_integer=0; f.max_integer=63; }
						else if(String(n)=="evidence_ids" || String(n)=="counterevidence_ids") { f.type="array"; f.item_type="string"; f.max_items=64; f.item_max_utf8_bytes=256; }
						else { f.type="string"; f.max_utf8_bytes=4096; if(String(n)=="text" || String(n)=="assertion" || String(n)=="uncertainty") f.required=true; if(String(n)=="assertion") { f.enum_values.Add("supported"); f.enum_values.Add("unsupported"); } }
					}
					SchemaCard::Field& locators=findings.item_properties.Add(); locators.name="evidence_locators"; locators.type="array"; locators.item_type="object"; locators.max_items=64; locators.allow_additional_properties=false;
					for(const char *n : { "id", "revision", "content_sha256" }) { SchemaCard::Field& f=locators.item_properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
					SchemaCard::Field& locator=locators.item_properties.Add(); locator.name="locator"; locator.type="object"; locator.required=true;
					SchemaCard::Field& counters=findings.item_properties.Add(); counters.name="counterevidence_locators"; counters.type="array"; counters.item_type="object"; counters.max_items=64; counters.allow_additional_properties=false;
					for(const char *n : { "id", "revision", "content_sha256" }) { SchemaCard::Field& f=counters.item_properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
					SchemaCard::Field& counter_locator=counters.item_properties.Add(); counter_locator.name="locator"; counter_locator.type="object"; counter_locator.required=true;
					SchemaCard::Field& envelope=field("envelope", "object"); envelope.allow_additional_properties=false;
					for(const char *n : { "schema_id", "schema_version", "workspace_id", "stable_id", "record_kind", "revision", "visibility", "provenance_ref" }) { SchemaCard::Field& f=envelope.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
					for(const char *n : { "evidence_hash", "license_id", "rule_id", "internal_use_mode" }) { SchemaCard::Field& f=envelope.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
					SchemaCard::Field& source_pin=envelope.properties.Add(); source_pin.name="source_pin"; source_pin.required=true; research_pin(source_pin,false);
					SchemaCard::Field& license_pin=envelope.properties.Add(); license_pin.name="license_pin"; license_pin.required=true; research_pin(license_pin,true);
					{ SchemaCard::Field& f=envelope.properties.Add(); f.name="plan_digest"; f.type="string"; f.required=true; f.max_utf8_bytes=64; }
					SchemaCard::Field& policy=envelope.properties.Add(); policy.name="policy_decision"; policy.type="object"; policy.required=true; policy.allow_additional_properties=false;
					for(const char *n : { "status", "origin", "reason", "classification", "review_granularity", "secret_state" }) { SchemaCard::Field& f=policy.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
					for(const char *n : { "publication_allowed", "source_visible", "external_model_allowed", "review_confirmed" }) { SchemaCard::Field& f=policy.properties.Add(); f.name=n; f.type="boolean"; f.required=true; }
					SchemaCard::Field& drafts=envelope.properties.Add(); drafts.name="candidate_drafts"; drafts.type="array"; drafts.item_type="object"; drafts.max_items=64; drafts.max_utf8_bytes=512*1024;
					drafts.allow_additional_properties=false;
					for(const char *n : { "title", "purpose", "quality_contract", "inputs", "outputs", "runtime_resources", "assets", "verification_plan" }) { SchemaCard::Field& f=drafts.item_properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=4096; }
					SchemaCard::Field& candidate_kind=drafts.item_properties.Add(); candidate_kind.name="candidate_kind"; candidate_kind.type="string"; candidate_kind.required=true; candidate_kind.enum_values.Add("research-stage");
					SchemaCard::Field& links=drafts.item_properties.Add(); links.name="evidence_links"; links.type="array"; links.item_type="string"; links.required=true; links.max_items=64; links.item_max_utf8_bytes=256;
					for(const char *n : { "extractor", "extractor_version", "extraction_config_revision", "generator_id", "generator_revision", "rubric_id", "rubric_revision" }) { SchemaCard::Field& f=envelope.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
					SchemaCard::Field& pins=envelope.properties.Add(); pins.name="evidence_pins"; pins.type="array"; pins.item_type="object"; pins.required=true; pins.max_items=64; pins.allow_additional_properties=false;
					for(const char *n : { "id", "revision", "source_revision", "content_sha256" }) { SchemaCard::Field& f=pins.item_properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
					SchemaCard::Field& pin_locator=pins.item_properties.Add(); pin_locator.name="locator"; pin_locator.type="object"; pin_locator.required=true;
				}
				else if(c.kind == "paper-candidate-handoff") {
					field("workspace_id","string").max_utf8_bytes=128; field("revision","string").max_utf8_bytes=32;
					SchemaCard::Field& status=field("status","string"); for(const char *v:{"complete","partial","no-supported","blocked"}) status.enum_values.Add(v);
					for(const char *n:{"run_id","paper_snapshot_id","paper_revision","extraction_sha256","source_identity"}) { SchemaCard::Field& f=field(n,"string"); f.max_utf8_bytes=256; if(String(n)=="run_id") f.reference_kind="paper-research-run"; }
					field("run_revision","string").max_utf8_bytes=32; field("operation_id","string").max_utf8_bytes=64;
					for(const char *n:{"supported_candidate_ids","blockers"}) { SchemaCard::Field& a=field(n,"array"); a.item_type="string"; a.max_items=64; a.item_max_utf8_bytes=256; }
					for(const char *n:{"duplicates","possible_duplicates"}) { SchemaCard::Field& a=field(n,"array"); a.item_type="object"; a.max_items=64; a.allow_additional_properties=false; for(const char *k:{"draft_index","existing_candidate_id"}) { SchemaCard::Field& f=a.item_properties.Add(); f.name=k; f.type=String(k)=="draft_index"?"integer":"string"; f.required=true; f.max_utf8_bytes=256; if(String(k)=="draft_index") { f.min_integer=0; f.max_integer=63; } } }
					SchemaCard::Field& env=field("envelope","object"); env.allow_additional_properties=false;
					for(const char *n:{"schema_id","schema_version","workspace_id","stable_id","record_kind","revision","visibility","provenance_ref"}) { SchemaCard::Field& f=env.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
					SchemaCard::Field& pins=env.properties.Add(); pins.name="evidence_pins"; pins.type="array"; pins.item_type="object"; pins.required=true; pins.max_items=64; pins.allow_additional_properties=false;
					for(const char *n:{"id","revision","source_revision","content_sha256"}) { SchemaCard::Field& f=pins.item_properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
				}
				else if(c.kind == "durable-job" || c.kind == "job-event" || c.kind == "job-run") {
					field("workspace_id", "string").max_utf8_bytes=128; field("revision", "string").max_utf8_bytes=32;
					SchemaCard::Field& provenance=field("provenance","string"); provenance.enum_values.Add("computed");
					SchemaCard::Field& envelope=field("envelope","object"); envelope.allow_additional_properties=false;
					for(const char *n:{"schema_id","schema_version","workspace_id","stable_id","record_kind","revision","visibility","provenance_ref"}) { SchemaCard::Field& f=envelope.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=256; }
					auto bs=[&](const String& n,int max=256)->SchemaCard::Field& { SchemaCard::Field& f=field(n,"string"); f.max_utf8_bytes=max; return f; };
					auto sa=[&](const String& n,int count,int bytes)->SchemaCard::Field& { SchemaCard::Field& f=field(n,"array"); f.item_type="string"; f.max_items=count; f.item_max_utf8_bytes=bytes; return f; };
					auto input_refs=[&](SchemaCard::Field& a) { a.type="array"; a.item_type="object"; a.max_items=32; a.max_utf8_bytes=256*1024; a.allow_additional_properties=false;
						for(const char *n:{"workspace_id","id","kind","revision","source_id"}) { SchemaCard::Field& f=a.item_properties.Add(); f.name=n; f.type="string"; f.required=true; String name=n; f.max_utf8_bytes=name=="id"||name=="revision"||name=="workspace_id"||name=="source_id"?128:64; }
					};
					if(c.kind=="durable-job") {
						SchemaCard::Field& state=field("state","string"); for(const char *v:{"queued","running","completed","failed","cancelled","uncertain"}) state.enum_values.Add(v);
						for(const char *n:{"request_id","operation_key","request_digest","attempt_id","created_at","updated_at","started_at","finished_at","output_ref","output_hash","failure_class"}) bs(n,String(n)=="request_digest"||String(n)=="output_hash"?64:256);
						bs("provider_id",256); bs("model_config_revision",128);
						SchemaCard::Field& policy=field("policy_snapshot","object"); policy.allow_additional_properties=false;
						SchemaCard::Field& fingerprint=policy.properties.Add(); fingerprint.name="fingerprint"; fingerprint.type="string"; fingerprint.required=true; fingerprint.max_utf8_bytes=64;
						SchemaCard::Field& secret_state=policy.properties.Add(); secret_state.name="secret_state"; secret_state.type="string"; secret_state.required=true; secret_state.max_utf8_bytes=32;
						SchemaCard::Field& decisions=policy.properties.Add(); decisions.name="decisions"; decisions.type="array"; decisions.required=true; decisions.item_type="object"; decisions.max_items=32;
						decisions.allow_additional_properties=false;
						for(const char *n:{"status","origin","reason","classification","review_granularity","secret_state"}) { SchemaCard::Field& f=decisions.item_properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=64; }
						for(const char *n:{"publication_allowed","source_visible","external_model_allowed","review_confirmed"}) { SchemaCard::Field& f=decisions.item_properties.Add(); f.name=n; f.type="boolean"; f.required=true; }
						SchemaCard::Field& contributors=field("contributors","array"); contributors.item_type="string"; contributors.max_items=256; contributors.item_max_utf8_bytes=256;
						for(const char *n:{"attempt","budget_ms","max_attempts","max_output_bytes"}) { SchemaCard::Field& f=field(n,"integer"); f.min_integer=0; f.max_integer=1000000000; }
						for(const char *n:{"retryable","idempotency_safe"}) field(n,"boolean"); sa("event_ids",256,256); sa("run_ids",64,256);
						// Input references have a closed five-string shape at both job and run boundaries.
						SchemaCard::Field& a=field("inputs","array"); input_refs(a);
						SchemaCard::Field& request=field("request","object"); request.allow_additional_properties=false;
						for(const char *n:{"budget_ms","max_attempts","max_output_bytes"}) { SchemaCard::Field& f=request.properties.Add(); f.name=n; f.type="integer"; f.required=true; f.min_integer=1; f.max_integer=1000000000; }
						for(const char *n:{"provider_id","model_config_revision"}) { SchemaCard::Field& f=request.properties.Add(); f.name=n; f.type="string"; f.required=true; f.max_utf8_bytes=String(n)=="provider_id"?256:128; }
						SchemaCard::Field& ri=request.properties.Add(); ri.name="inputs"; ri.required=true; input_refs(ri);
					}
					else if(c.kind=="job-event") {
						for(const char *n:{"job_id","attempt_id","operation_key","timestamp","reason"}) bs(n,512);
						SchemaCard::Field& from=field("from_state","string"); for(const char *v:{"none","queued","running","completed","failed","cancelled","uncertain"}) from.enum_values.Add(v);
						SchemaCard::Field& to=field("to_state","string"); for(const char *v:{"queued","running","completed","failed","cancelled","uncertain"}) to.enum_values.Add(v);
						for(const char *n:{"sequence","attempt"}) { SchemaCard::Field& f=field(n,"integer"); f.min_integer=0; f.max_integer=1000000; }
					}
					else {
						for(const char *n:{"job_id","attempt_id","started_at","finished_at","output_ref","output_hash","failure_class"}) bs(n);
						SchemaCard::Field& state=field("state","string"); for(const char *v:{"completed","failed","uncertain"}) state.enum_values.Add(v);
						for(const char *n:{"attempt","elapsed_ms","output_bytes"}) { SchemaCard::Field& f=field(n,"integer"); f.min_integer=0; f.max_integer=1000000000; }
						field("output","string").max_utf8_bytes=256*1024;
						SchemaCard::Field& run_inputs=field("inputs","array"); input_refs(run_inputs);
						for(const char *n:{"retryable","idempotency_safe"}) field(n,"boolean");
					}
				}
				else if(c.kind == "candidate-proposal") {
					field("workspace_id", "string").max_utf8_bytes = 128;
					for(const char *n : { "revision", "provenance", "candidate_id", "proposal_revision_id", "source_identity", "title", "purpose", "quality_contract", "inputs", "outputs", "runtime_resources", "assets", "verification_plan", "draft_state", "submission_state", "approval_state", "author_state", "intent_state" }) field(n, "string").max_utf8_bytes = 4096;
					SchemaCard::Field& ck = field("candidate_kind", "string"); for(const char *v : { "candidate", "proposal", "research-stage" }) ck.enum_values.Add(v);
					for(const char *n : { "evidence_links", "counterevidence" }) { SchemaCard::Field& a = field(n, "array"); a.item_type = "string"; a.max_items = 64; a.item_max_utf8_bytes = 256; }
					SchemaCard::Field& hypothetical = field("hypothetical", "boolean");
					hypothetical.required = true;
					for(SchemaCard::Field& state : c.fields) {
						if(state.name=="submission_state") for(const char *v : { "not-a-real-submission", "not-submitted" }) state.enum_values.Add(v);
						if(state.name=="approval_state") state.enum_values.Add("not-approved");
						if(state.name=="author_state") for(const char *v : { "unknown", "known-local-author" }) state.enum_values.Add(v);
						if(state.name=="intent_state") state.enum_values.Add("unknown");
					}
					field("uncertainty", "string").max_utf8_bytes = 4096;
					SchemaCard::Field& env = field("envelope", "object"); env.allow_additional_properties = false;
					for(const char *n : { "schema_id", "schema_version", "workspace_id", "stable_id", "record_kind", "revision", "visibility", "provenance_ref" }) { SchemaCard::Field& f = env.properties.Add(); f.name = n; f.type = "string"; f.required = true; f.max_utf8_bytes = 256; }
				}
				else if(c.kind == "candidate-stage-event" || c.kind == "candidate-stage-head" || c.kind == "candidate-proposal-head") {
					field("workspace_id", "string").max_utf8_bytes = 128;
					for(const char *n : { "revision", "provenance", "candidate_id" }) field(n, "string").max_utf8_bytes = 256;
					if(c.kind != "candidate-proposal-head") { SchemaCard::Field& proposal_revision = field("proposal_revision_id", "string"); proposal_revision.max_utf8_bytes = 256; proposal_revision.reference_kind = "proposal-revision"; }
					if(c.kind != "candidate-proposal-head") {
						field("stage", "string").max_utf8_bytes = 256;
						field("event_id", "string").max_utf8_bytes = 256;
					}
					if(c.kind == "candidate-stage-event") {
						field("candidate_revision", "string").max_utf8_bytes = 32; field("previous_event_id", "string").max_utf8_bytes = 256;
						field("reason", "string").max_utf8_bytes = 1024; field("operation_id", "string").max_utf8_bytes = 128;
						for(const char *n : { "actor", "authority", "timestamp" }) field(n, "string").max_utf8_bytes = 256;
						SchemaCard::Field& outcome = field("review_outcome", "string"); for(const char *v : { "no_duplicate", "duplicate_found", "uncertain" }) outcome.enum_values.Add(v);
						SchemaCard::Field& evidence = field("evidence_ids", "array"); evidence.item_type = "string"; evidence.max_items = 64; evidence.item_max_utf8_bytes = 256;
					}
					if(c.kind != "candidate-proposal-head") {
						SchemaCard::Field& stage = c.fields.Add(); stage.name = "stage"; stage.type = "string";
						for(const char *v : { "draft", "source-inspected", "duplicate-reviewed", "spike-verified", "deliverables-ready", "verifier-feasible", "comparison-ready" }) stage.enum_values.Add(v);
					}
					else {
						field("proposal_id", "string").max_utf8_bytes = 256;
						field("proposal_revision_id", "string").max_utf8_bytes = 256;
						field("revision_id", "string").max_utf8_bytes = 32;
					}
					SchemaCard::Field& env = field("envelope", "object"); env.allow_additional_properties = false;
					for(const char *n : { "schema_id", "schema_version", "workspace_id", "stable_id", "record_kind", "revision", "visibility", "provenance_ref" }) { SchemaCard::Field& f = env.properties.Add(); f.name = n; f.type = "string"; f.required = true; f.max_utf8_bytes = 256; }
				}
				else if(c.kind == "review-package-revision") {
					for(const char *n : { "workspace_id", "package_id", "goal_id", "scope_id", "base_revision", "base_state", "head_revision", "previous_revision", "patch_sha256", "author", "author_state", "capture_state" }) field(n, "string").max_utf8_bytes = String(n) == "patch_sha256" ? 64 : 256;
					for(SchemaCard::Field& f : c.fields) if(f.name == "base_state") { f.enum_values.Add("available"); f.enum_values.Add("unavailable"); f.enum_values.Add("unknown"); }
					for(SchemaCard::Field& f : c.fields) if(f.name == "author_state") { f.enum_values.Add("known-local-author"); f.enum_values.Add("unknown"); }
					field("patch_bytes", "string").max_utf8_bytes = 1024 * 1024;
					for(const char *n : { "selected_paths", "artifact_refs", "check_refs" }) { SchemaCard::Field& a = field(n, "array"); a.item_type = "string"; a.max_items = 128; a.item_max_utf8_bytes = 256; }
					SchemaCard::Field& artifacts = field("artifacts", "array"); artifacts.item_type = "object"; artifacts.max_items = 128; artifacts.allow_additional_properties = false;
					for(const char *n : { "path", "content_sha256" }) { SchemaCard::Field& f = artifacts.item_properties.Add(); f.name = n; f.type = "string"; f.required = true; f.max_utf8_bytes = 256; }
					SchemaCard::Field& size = artifacts.item_properties.Add(); size.name = "byte_count"; size.type = "integer"; size.required = true; size.min_integer = 0; size.max_integer = 1LL << 40;
					SchemaCard::Field& hash_only = artifacts.item_properties.Add(); hash_only.name = "hash_only"; hash_only.type = "boolean"; hash_only.required = true;
					SchemaCard::Field& env = field("envelope", "object"); env.allow_additional_properties = false;
					for(const char *n : { "schema_id", "schema_version", "workspace_id", "stable_id", "record_kind", "revision", "visibility", "provenance_ref" }) { SchemaCard::Field& f = env.properties.Add(); f.name = n; f.type = "string"; f.required = true; f.max_utf8_bytes = 256; }
				}
				else if(c.kind == "review-thread" || c.kind == "review-comment") {
					for(const char *n : { "source_identity", "artifact_refs", "check_refs" }) for(int i=c.required.GetCount()-1;i>=0;i--) if(c.required[i]==n) c.required.Remove(i);
					for(const char *n : { "workspace_id", "package_id", "revision_id", "revision_hash" }) field(n, "string").max_utf8_bytes = 256;
					if(c.kind == "review-comment") {
						for(const char *n : { "thread_id", "author", "body" }) field(n, "string").max_utf8_bytes = String(n) == "body" ? 8192 : 256;
						for(const char *n : { "artifact_refs", "check_refs" }) { SchemaCard::Field& refs=field(n,"array"); refs.item_type="string"; refs.max_items=64; refs.item_max_utf8_bytes=256; }
						field("source_identity","string").max_utf8_bytes=256;
					field("created_at","integer");
						SchemaCard::Field& type = field("record_type", "string"); type.enum_values.Add("comment"); type.enum_values.Add("review-request"); type.enum_values.Add("agent-note"); type.enum_values.Add("external-evidence");
						SchemaCard::Field& author_kind = field("author_kind", "string"); author_kind.enum_values.Add("human"); author_kind.enum_values.Add("agent"); author_kind.enum_values.Add("external");
					}
					SchemaCard::Field& env = field("envelope", "object"); env.allow_additional_properties = false;
					for(const char *n : { "schema_id", "schema_version", "workspace_id", "stable_id", "record_kind", "revision", "visibility", "provenance_ref" }) { SchemaCard::Field& f = env.properties.Add(); f.name = n; f.type = "string"; f.required = true; f.max_utf8_bytes = 256; }
				}
				cards.Add(c);
			}
		}
	};
	static DefaultRegistry reg;
	return reg.cards;
}

END_UPP_NAMESPACE
