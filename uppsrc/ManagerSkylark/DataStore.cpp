#include "DataStore.h"
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

static bool HasSymlinkAncestor(const String& path)
{
	String current = NormalizePath(path);
	while(!current.IsEmpty()) {
		FindFile ff(current);
		if(ff && ff.IsSymLink())
			return true;
		String parent = GetFileFolder(current);
		if(parent.IsEmpty() || parent == current)
			break;
		current = parent;
	}
	return false;
}

bool DataStore::IsSafeRoot(const String& path)
{
	String normalized = NormalizePath(path);
	return !normalized.IsEmpty() && IsFullPath(normalized.ToStd().c_str()) &&
	       DirectoryExists(normalized) && !HasSymlinkAncestor(normalized);
}

static bool IsSafeSourceId(const String& id)
{
	if(id.IsEmpty() || id.GetCount() > 64)
		return false;
	for(int i = 0; i < id.GetCount(); i++) {
		char c = id[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		     (c >= '0' && c <= '9') || c == '-' || c == '_'))
			return false;
	}
	return true;
}

static bool GetTypedField(const ValueMap& map, const String& key, Value& value)
{
	int at = map.Find(key);
	if(at < 0)
		return false;
	value = map.GetValue(at);
	return true;
}

struct JsonFidelity {
	bool syntax_ok = true;
	bool duplicate_key = false;
	bool trailing_input = false;
	bool numeric_precision = false;
	bool escaped_key = false;
};

class JsonFidelityScanner {
public:
	JsonFidelityScanner(const String& json) : json(json), pos(0) {}
	JsonFidelity Scan() {
		SkipSpace();
		if(!Value(0)) result.syntax_ok = false;
		SkipSpace();
		if(pos != json.GetCount()) result.trailing_input = true;
		return result;
	}
private:
	const String& json;
	int pos;
	JsonFidelity result;

	void SkipSpace() {
		while(pos < json.GetCount() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
	}
	bool StringToken(String& raw, bool& escaped) {
		if(pos >= json.GetCount() || json[pos++] != '"') return false;
		int start = pos;
		escaped = false;
		while(pos < json.GetCount()) {
			char c = json[pos++];
			if(c == '"') { raw = json.Mid(start, pos - start - 1); return true; }
			if((byte)c < 0x20) return false;
			if(c == '\\') {
				escaped = true;
				if(pos >= json.GetCount()) return false;
				char e = json[pos++];
				if(e == 'u') {
					if(pos + 4 > json.GetCount()) return false;
					for(int n = 0; n < 4; n++) {
						char h = json[pos++];
						if(!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return false;
					}
				}
				else if(e != '"' && e != '\\' && e != '/' && e != 'b' && e != 'f' && e != 'n' && e != 'r' && e != 't') return false;
			}
		}
		return false;
	}
	bool Number() {
		int start = pos;
		if(pos < json.GetCount() && json[pos] == '-') pos++;
		if(pos >= json.GetCount()) return false;
		if(json[pos] == '0') pos++;
		else if(json[pos] >= '1' && json[pos] <= '9') while(pos < json.GetCount() && json[pos] >= '0' && json[pos] <= '9') pos++;
		else return false;
		bool decimal = false;
		if(pos < json.GetCount() && json[pos] == '.') {
			decimal = true; pos++;
			int digits = pos;
			while(pos < json.GetCount() && json[pos] >= '0' && json[pos] <= '9') pos++;
			if(pos == digits) return false;
		}
		if(pos < json.GetCount() && (json[pos] == 'e' || json[pos] == 'E')) {
			decimal = true; pos++;
			if(pos < json.GetCount() && (json[pos] == '+' || json[pos] == '-')) pos++;
			int digits = pos;
			while(pos < json.GetCount() && json[pos] >= '0' && json[pos] <= '9') pos++;
			if(pos == digits) return false;
		}
		String token = json.Mid(start, pos - start);
	// U++ floating JSON values are not an exact-decimal contract; quarantine all fractions/exponents.
		if(decimal) result.numeric_precision = true;
		if(!decimal) {
			String digits = token.StartsWith("-") ? token.Mid(1) : token;
			if(digits.GetCount() > 16 || (digits.GetCount() == 16 && digits > "9007199254740991"))
				result.numeric_precision = true;
		}
		if(token.Find('e') >= 0 || token.Find('E') >= 0) result.numeric_precision = true;
		return true;
	}
	bool Value(int depth) {
		if(depth > 128) return false;
		SkipSpace();
		if(pos >= json.GetCount()) return false;
		char c = json[pos];
		if(c == '"') { String raw; bool escaped; return StringToken(raw, escaped); }
		if(c == '{') {
			pos++; SkipSpace();
			Index<String> keys;
			if(pos < json.GetCount() && json[pos] == '}') { pos++; return true; }
			for(;;) {
				String key; bool escaped;
				if(!StringToken(key, escaped)) return false;
				// Escaped names need full Unicode normalization to compare; fail closed instead.
				if(escaped) result.escaped_key = true;
				if(keys.Find(key) >= 0) result.duplicate_key = true;
				else keys.Add(key);
				SkipSpace(); if(pos >= json.GetCount() || json[pos++] != ':') return false;
				if(!Value(depth + 1)) return false;
				SkipSpace(); if(pos >= json.GetCount()) return false;
				if(json[pos] == '}') { pos++; return true; }
				if(json[pos++] != ',') return false;
				SkipSpace();
			}
		}
		if(c == '[') {
			pos++; SkipSpace();
			if(pos < json.GetCount() && json[pos] == ']') { pos++; return true; }
			for(;;) {
				if(!Value(depth + 1)) return false;
				SkipSpace(); if(pos >= json.GetCount()) return false;
				if(json[pos] == ']') { pos++; return true; }
				if(json[pos++] != ',') return false;
			}
		}
		if(c == '-' || (c >= '0' && c <= '9')) return Number();
		static const char* literals[] = { "true", "false", "null" };
		for(int i = 0; i < 3; i++) {
			String literal = literals[i];
			if(json.Mid(pos, literal.GetCount()) == literal) { pos += literal.GetCount(); return true; }
		}
		return false;
	}
};

static bool ReadRootRegistration(DataSourceRegistration& registration)
{
	String cfg = GetHomeDirFile(".dataweb-root");
	if(!FileExists(cfg))
		return false;
	return DataStore::ParseRootRegistrationContent(LoadFile(cfg), registration);
}

bool DataStore::ReadConfiguredRegistration(DataSourceRegistration& registration)
{
	return ReadRootRegistration(registration);
}

bool DataStore::RegistrationMatches(const DataSourceRegistration& registration, const String& registered_source_id, const String& normalized_root)
{
	return registration.enabled && registration.write_enabled && !registration.read_only &&
	       IsSafeSourceId(registration.id) && registration.id == registered_source_id &&
	       NormalizePath(registration.root) == normalized_root;
}

bool DataStore::HasCurrentWriteRegistration() const
{
	if(synthetic_write_source)
		return RegistrationMatches(synthetic_registration, source_id, base_dir);
	DataSourceRegistration registration;
	return ReadConfiguredRegistration(registration) && RegistrationMatches(registration, source_id, base_dir);
}

void DataStore::SetSyntheticRegistrationForTests(const DataSourceRegistration& registration) const
{
	synthetic_write_source = true;
	synthetic_registration = registration;
}

String DataStore::ContentDigest(const String& content)
{
	byte digest[32];
	SHA256(digest, content);
	static const char hex[] = "0123456789abcdef";
	String result;
	for(int i = 0; i < 32; i++) {
		result.Cat(hex[digest[i] >> 4]);
		result.Cat(hex[digest[i] & 15]);
	}
	return result;
}

String DataStore::RecordSourceRevision(const String& rel_path, const String& content)
{
	return rel_path + ":" + ContentDigest(content);
}

bool DataStore::ExceedsSourceBudget(int64 file_bytes, int64 bytes_scanned)
{
	return file_bytes <= 0 || file_bytes > MAX_SOURCE_FILE_BYTES ||
	       bytes_scanned > MAX_TOTAL_SOURCE_BYTES - file_bytes;
}

static String SourceFingerprint(const String& rel_path, const String& bytes)
{
	return DataStore::RecordSourceRevision(rel_path, bytes);
}

DataStore::DataStore()
	: source_read_only(true)
	, files_scanned(0)
	, entries_scanned(0)
	, source_bytes_scanned(0)
	, source_errors(0)
	, invalid_count(0)
	, quarantine_count(0)
{
}

DataStore::DataStore(const DataStore& o) { *this = o; }

DataStore& DataStore::operator=(const DataStore& o)
{
	if(this == &o) return *this;
	base_dir = o.base_dir; source_id = o.source_id; source_read_only = o.source_read_only;
	source_write_enabled = o.source_write_enabled; synthetic_write_source = o.synthetic_write_source;
	synthetic_registration = o.synthetic_registration;
	schemas = clone(o.schemas); docs = clone(o.docs); diagram_sources = clone(o.diagram_sources);
	source_diagnostics = clone(o.source_diagnostics); source_fingerprints = clone(o.source_fingerprints); kind_order.Clear();
	for(int i = 0; i < o.kind_order.GetCount(); i++) kind_order.FindAdd(o.kind_order[i]);
	files_scanned = o.files_scanned; entries_scanned = o.entries_scanned; source_bytes_scanned = o.source_bytes_scanned;
	source_errors = o.source_errors; source_incomplete = o.source_incomplete; invalid_count = o.invalid_count; quarantine_count = o.quarantine_count;
	return *this;
}

void DataStore::Clear()
{
	base_dir.Clear();
	source_id.Clear();
	source_read_only = true;
	source_write_enabled = false;
	synthetic_write_source = false;
	synthetic_registration = DataSourceRegistration();
	schemas.Clear();
	docs.Clear();
	diagram_sources.Clear();
	kind_order.Clear();
	files_scanned = 0;
	entries_scanned = 0;
	source_bytes_scanned = 0;
	source_errors = 0;
	source_incomplete = false;
	invalid_count = 0;
	quarantine_count = 0;
	source_diagnostics.Clear();
	source_fingerprints.Clear();
}

String DataStore::ReadRootConfig()
{
	DataSourceRegistration registration;
	return ReadRootRegistration(registration) ? registration.root : String();
}

String DataStore::ParseRootConfigContent(const String& content)
{
	DataSourceRegistration registration;
	return ParseRootRegistrationContent(content, registration) ? registration.root : String();
}

bool DataStore::ParseRootRegistrationContent(const String& content, DataSourceRegistration& registration)
{
	registration = DataSourceRegistration();
	if(content.IsEmpty())
		return false;
	String trimmed = TrimBoth(content);
	if(trimmed.IsEmpty())
		return false;
	String path;
	if(trimmed[0] == '{' || trimmed[0] == '"') {
		Value v;
		try {
			v = ParseJSON(trimmed);
		}
		catch(Exc) {
			return false;
		}
		if(v.IsError())
			return false;
		if(v.Is<ValueMap>()) {
			ValueMap map = v;
			Value sources;
			if(GetTypedField(map, "sources", sources)) {
				if(!sources.Is<ValueArray>() || sources.GetCount() == 0)
					return false;
				Index<String> ids;
				int enabled_count = 0;
				DataSourceRegistration selected;
				ValueArray array = sources;
				for(int i = 0; i < array.GetCount(); i++) {
					if(!array[i].Is<ValueMap>())
						return false;
					ValueMap entry = array[i];
					Value id_value, root_value, enabled_value, read_only_value;
					if(!GetTypedField(entry, "id", id_value) || !IsString(id_value) ||
					   !GetTypedField(entry, "root", root_value) || !IsString(root_value) ||
					   !GetTypedField(entry, "enabled", enabled_value) || !enabled_value.Is<bool>() ||
					   !GetTypedField(entry, "read_only", read_only_value) || !read_only_value.Is<bool>())
						return false;
					String id = (String)id_value;
					if(!IsSafeSourceId(id) || ids.Find(ToLower(id)) >= 0)
						return false;
					ids.Add(ToLower(id));
					String entry_root = TrimBoth((String)root_value);
					Value write_value;
					bool write_enabled = false;
					if(GetTypedField(entry, "write_enabled", write_value)) {
						if(!write_value.Is<bool>()) return false;
						write_enabled = (bool)write_value;
					}
					if(!IsSafeRoot(entry_root) || (!(bool)read_only_value && !write_enabled))
						return false;
					if((bool)enabled_value) {
						selected.id = id;
						selected.root = NormalizePath(entry_root);
						selected.enabled = true;
						selected.read_only = (bool)read_only_value;
						selected.write_enabled = write_enabled;
						enabled_count++;
					}
				}
				if(enabled_count != 1)
					return false;
				registration = selected;
				return true;
			}
			Value root;
			if(!GetTypedField(map, "root", root) || !IsString(root))
				return false;
			path = TrimBoth((String)root);
			registration.id = "configured-root";
		}
		else if(IsString(v)) {
			path = TrimBoth((String)v);
			registration.id = "configured-root";
		}
		else
			return false;
	}
	else {
		// Accept the existing plain-text config form only when it is absolute.
		path = trimmed;
		registration.id = "configured-root";
	}
	if(!IsSafeRoot(path))
		return false;
	registration.root = NormalizePath(path);
	registration.enabled = true;
	registration.read_only = true;
	return true;
}

String DataStore::FindDefaultRoot()
{
	DataSourceRegistration registration;
	return ReadRootRegistration(registration) ? registration.root : String();
}

bool DataStore::Load()
{
	DataSourceRegistration registration;
	if(!ReadConfiguredRegistration(registration)) {
		Clear();
		return false;
	}
	return LoadRoot(registration.root, registration.id, registration.write_enabled && !registration.read_only);
}

bool DataStore::LoadFixtureForTests(const String& fixture_root)
{
	String temp_root = NormalizePath(GetTempDirectory());
	String root = NormalizePath(fixture_root);
	if(!root.StartsWith(temp_root + DIR_SEP) || !IsSafeRoot(root))
		return false;
	return LoadRoot(root, "self-test-fixture");
}

bool DataStore::LoadWriteFixtureForTests(const String& fixture_root, const String& synthetic_source_id)
{
	String temp_root = NormalizePath(GetTempDirectory());
	String root = NormalizePath(fixture_root);
	if(!root.StartsWith(temp_root + DIR_SEP) || !IsSafeRoot(root) || !IsSafeSourceId(synthetic_source_id))
		return false;
	return LoadRoot(root, synthetic_source_id, true, true);
}

bool DataStore::LoadRoot(const String& root_dir, const String& registered_source_id, bool writable, bool test_source)
{
	Clear();
	if(!IsSafeRoot(root_dir))
		return false;
	base_dir = NormalizePath(root_dir);
	source_id = registered_source_id;
	source_read_only = !writable;
	source_write_enabled = writable;
	synthetic_write_source = test_source;
	synthetic_registration = DataSourceRegistration();
	if(test_source) {
		synthetic_registration.id = registered_source_id;
		synthetic_registration.root = base_dir;
		synthetic_registration.enabled = true;
		synthetic_registration.read_only = !writable;
		synthetic_registration.write_enabled = writable;
	}
	// Trusted schemas are available to explicit recovery even when catalog
	// loading stops at a pending journal; this does not expose indexed records.
	const Vector<SchemaCard>& defaults = GetDefaultSchemaCards();
	for(int i = 0; i < defaults.GetCount(); i++)
		schemas.Add(defaults[i]);
	// Until an authorized recovery path resolves the journal, never expose a
	// catalog from a source containing journal state. Detection is conservative.
	String journal_dir = AppendFileName(base_dir, ".mgr-journal");
	FindFile journal_status(journal_dir);
	FindFile journal_entry(AppendFileName(journal_dir, "*"));
	if((journal_status && journal_status.IsSymLink()) || DirectoryExists(journal_dir) || journal_entry) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add("journal:recovery_required");
		return false;
	}

	// Scan top-level entries; each top-level directory is a collection.
	FindFile ff(AppendFileName(base_dir, "*"));
	while(ff) {
		if(entries_scanned >= MAX_FILES) {
			source_incomplete = true;
			source_errors++;
			source_diagnostics.Add("root:entry_limit");
			break;
		}
		entries_scanned++;
		String name = ff.GetName();
		if(name == "." || name == "..") {
			ff.Next();
			continue;
		}
		if(ff.IsSymLink()) {
			ff.Next();
			continue;
		}
		if(ff.IsFolder()) {
			if(!name.StartsWith(".") && !IsSkipDir(name))
				ScanDir(ff.GetPath(), name, name, 1);
		}
		else {
			String ext = ToLower(GetFileExt(name));
			if(ff.IsFile() && ext == ".json" && files_scanned < MAX_FILES)
				IndexFile(ff.GetPath(), name, "root");
			else if(ff.IsFile() && ext == ".puml" && diagram_sources.GetCount() < MAX_FILES)
				IndexDiagramSource(ff.GetPath(), name);
			else if(ff.IsFile() && ((ext == ".json" && files_scanned >= MAX_FILES) || (ext == ".puml" && diagram_sources.GetCount() >= MAX_FILES))) {
				source_incomplete = true;
				source_errors++;
				source_diagnostics.Add(name + ":entry_limit");
			}
		}
		ff.Next();
	}
	// Mark every member of an identity collision; no scan-order winner exists.
	for(int i = 0; i < docs.GetCount(); i++) {
		for(int j = i + 1; j < docs.GetCount(); j++) {
			if(docs[i].qualified_id == docs[j].qualified_id) {
				auto mark = [&](DataDocument& d) {
					d.valid = false;
					if(d.validated) d.validation_state = "invalid";
				bool noted = false;
				for(const String& diagnostic : d.diagnostics) if(diagnostic == "duplicate_identity") noted = true;
				if(!noted) d.diagnostics.Add("duplicate_identity");
					if(d.invalid_reason.IsEmpty()) d.invalid_reason = "duplicate_identity";
				};
				mark(docs[i]); mark(docs[j]);
			}
		}
	}
	for(int i = 0; i < docs.GetCount(); i++) {
		DataDocument& document = docs[i];
		if(!document.validated) continue;
		const SchemaCard* schema = FindSchemaCard(document.kind);
		if(!schema) continue;
		for(const SchemaCard::Field& field : schema->fields) {
			if(field.reference_kind.IsEmpty()) continue;
			Value reference_value;
			if(!GetTypedField(document.data, field.name, reference_value) || !IsString(reference_value)) continue;
			String reference_id = reference_value;
			int matches = 0;
			const DataDocument* target = NULL;
			for(const DataDocument& candidate : docs)
				if(candidate.workspace_id == document.workspace_id && candidate.id == reference_id) {
					matches++;
					target = &candidate;
				}
			String error;
			if(matches == 0) error = "reference_missing:" + field.name;
			else if(matches > 1) error = "reference_ambiguous:" + field.name;
			else if(target->kind != field.reference_kind) error = "reference_wrong_kind:" + field.name;
			else if(target->validation_state != "valid" || !target->valid) error = "reference_target_invalid:" + field.name;
			if(!error.IsEmpty()) {
				document.valid = false;
				document.validation_state = "invalid";
				document.diagnostics.Add(error);
				if(document.invalid_reason.IsEmpty()) document.invalid_reason = error;
			}
		}
	}
	invalid_count = 0;
	quarantine_count = 0;
	for(const DataDocument& d : docs) {
		if(d.validated && !d.valid) invalid_count++;
		if(d.validation_state == "quarantined") quarantine_count++;
	}
	return true;
}

static bool JournalText(const ValueMap& map, const char *key, String& out, int max_bytes)
{
	Value v = MapGet(map, key);
	if(!IsString(v)) return false;
	out = (String)v;
	if(out.IsEmpty() || out.GetCount() > max_bytes) return false;
	for(int i = 0; i < out.GetCount(); i++)
		if((byte)out[i] < 0x21 || (byte)out[i] > 0x7e) return false;
	return true;
}

static bool JournalValueBounded(const Value& value, int depth, int& nodes)
{
	if(depth > 8 || ++nodes > 1024) return false;
	if(value.Is<ValueMap>()) {
		ValueMap map = value;
		if(map.GetCount() > 16) return false;
		for(int i = 0; i < map.GetCount(); i++)
			if(!JournalValueBounded(map.GetValue(i), depth + 1, nodes)) return false;
	}
	else if(value.Is<ValueArray>()) {
		ValueArray array = value;
		if(array.GetCount() > 64) return false;
		for(const Value& item : array)
			if(!JournalValueBounded(item, depth + 1, nodes)) return false;
	}
	return true;
}

static bool JournalHasExactKeys(const ValueMap& map, std::initializer_list<const char *> names)
{
	if(map.GetCount() != (int)names.size()) return false;
	for(const char *name : names)
		if(map.Find(name) < 0) return false;
	return true;
}

static String CreateJournalPath(const String& qualified_id)
{
	if(qualified_id.IsEmpty()) return String();
	return "records/" + DataStore::ContentDigest(qualified_id) + ".json";
}

bool DataStore::ParseJournalManifest(const String& bytes, JournalManifest& manifest)
{
	if(bytes.IsEmpty() || bytes.GetCount() > 65536) return false;
	JsonFidelity fidelity = JsonFidelityScanner(bytes).Scan();
	if(!fidelity.syntax_ok || fidelity.duplicate_key || fidelity.trailing_input || fidelity.numeric_precision || fidelity.escaped_key)
		return false;
	Value root = ParseJSON(bytes);
	if(!root.Is<ValueMap>()) return false;
	int nodes = 0;
	if(!JournalValueBounded(root, 0, nodes)) return false;
	ValueMap map = root;
	Value version = MapGet(map, "version"), records_value = MapGet(map, "records");
	if(!version.Is<double>() || (version != Value(1.0) && version != Value(2.0)) || !records_value.Is<ValueArray>()) return false;
	JournalManifest parsed;
	parsed.version = version == Value(1.0) ? 1 : 2;
	if(!JournalText(map, "operation_id", parsed.operation_id, 64) ||
	   !JournalText(map, "source_id", parsed.source_id, 64) ||
	   !JournalText(map, "workspace_id", parsed.workspace_id, 256) ||
	   !JournalText(map, "state", parsed.state, 16) ||
	   (parsed.state != "prepared" && parsed.state != "committing" && parsed.state != "committed" && parsed.state != "rolled-back")) return false;
	bool root_keys_ok = JournalHasExactKeys(map, { "version", "operation_id", "source_id", "workspace_id", "state", "records", "request_digest" }) ||
	                    (parsed.version == 1 && parsed.state == "prepared" &&
	                     JournalHasExactKeys(map, { "version", "operation_id", "source_id", "workspace_id", "state", "records" }));
	if(!root_keys_ok) return false;
	String workspace_diagnostic;
	if(!MatchIdPattern(parsed.workspace_id, "^[A-Za-z0-9][A-Za-z0-9._/-]*$", workspace_diagnostic)) return false;
	Value digest_value = MapGet(map, "request_digest");
	if(parsed.state == "prepared") {
		if(!digest_value.IsNull() && (!IsString(digest_value) || (String)digest_value != "")) return false;
	}
	else {
		if(!IsString(digest_value)) return false;
		parsed.request_digest = digest_value;
		if(parsed.request_digest.GetCount() != 64) return false;
		for(int i = 0; i < parsed.request_digest.GetCount(); i++)
			if(!((parsed.request_digest[i] >= '0' && parsed.request_digest[i] <= '9') || (parsed.request_digest[i] >= 'a' && parsed.request_digest[i] <= 'f'))) return false;
	}
	for(int i = 0; i < parsed.operation_id.GetCount(); i++) {
		char c = parsed.operation_id[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
	}
	ValueArray records = records_value;
	if(records.GetCount() < 1 || records.GetCount() > 64) return false;
	Index<String> identities, references;
	Index<String> paths;
	for(const Value& item : records) {
		if(!item.Is<ValueMap>()) return false;
		ValueMap rm = item;
		if(parsed.version == 1 ? !JournalHasExactKeys(rm, { "source_id", "record_id", "path", "expected_revision", "new_revision", "backup_ref", "payload_ref", "old_hash", "new_hash", "state" }) :
		   !JournalHasExactKeys(rm, { "source_id", "record_id", "path", "action", "expected_revision", "new_revision", "backup_ref", "payload_ref", "old_hash", "new_hash", "state" })) return false;
		JournalRecord record;
		record.action = parsed.version == 1 ? "update" : MapGetString(rm, "action");
		if(record.action != "update" && record.action != "create") return false;
		Value backup_value = MapGet(rm, "backup_ref"), old_hash_value = MapGet(rm, "old_hash");
		Value expected_revision_value = MapGet(rm, "expected_revision");
		bool expected_revision_ok = record.action == "create" ? (IsString(expected_revision_value) && (String)expected_revision_value == "") :
		                            JournalText(rm, "expected_revision", record.expected_revision, 32);
		if(record.action == "create" && IsString(expected_revision_value)) record.expected_revision = expected_revision_value;
		bool backup_ok = record.action == "create" ? (IsString(backup_value) && (String)backup_value == "") : JournalText(rm, "backup_ref", record.backup_ref, 64);
		if(record.action == "create" && IsString(backup_value)) record.backup_ref = backup_value;
		bool old_hash_ok = record.action == "create" ? (IsString(old_hash_value) && (String)old_hash_value == "") : JournalText(rm, "old_hash", record.old_hash, 64);
		if(record.action == "create" && IsString(old_hash_value)) record.old_hash = old_hash_value;
		if(!JournalText(rm, "source_id", record.source_id, 64) || record.source_id != parsed.source_id ||
		   !JournalText(rm, "record_id", record.record_id, 128) ||
		   !JournalText(rm, "path", record.rel_path, 512) || record.rel_path.StartsWith("/") ||
		   !expected_revision_ok ||
		   !JournalText(rm, "new_revision", record.new_revision, 32) ||
		   !backup_ok ||
		   !JournalText(rm, "payload_ref", record.payload_ref, 64) ||
		   !old_hash_ok ||
		   (record.action == "update" ? record.old_hash.GetCount() != 64 : !record.old_hash.IsEmpty()) ||
		   !JournalText(rm, "new_hash", record.new_hash, 64) || record.new_hash.GetCount() != 64 ||
		   !JournalText(rm, "state", record.state, 16) ||
		   (record.state != "pending" && record.state != "applied" && record.state != "rolled-back") ||
		   identities.Find(record.record_id) >= 0) return false;
		if((record.action == "update" && !record.backup_ref.StartsWith("backup-")) ||
		   (record.action == "create" && !record.backup_ref.IsEmpty()) || !record.payload_ref.StartsWith("payload-")) return false;
		if((!record.backup_ref.IsEmpty() && references.Find(record.backup_ref) >= 0) || references.Find(record.payload_ref) >= 0) return false;
		if(!record.backup_ref.IsEmpty()) references.Add(record.backup_ref);
		references.Add(record.payload_ref);
		for(const String& ref : { record.backup_ref, record.payload_ref }) {
			if(ref.IsEmpty()) continue;
			for(int i = 0; i < ref.GetCount(); i++) {
				char c = ref[i];
				if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
			}
		}
		for(const String& part : Split(record.rel_path, '/'))
			if(part.IsEmpty() || part == "." || part == ".." || part.Find('\\') >= 0) return false;
		if(record.rel_path.StartsWith(".mgr-journal/") || paths.Find(record.rel_path) >= 0) return false;
		paths.Add(record.rel_path);
		identities.Add(record.record_id);
		parsed.records.Add(record);
	}
	manifest = pick(parsed);
	return true;
}

bool DataStore::IsSkipDir(const String& name)
{
	static const char* skip[] = {
		"bin", "out", "build", "node_modules", "__pycache__", ".cache", ".uppsrc_cache",
	};
	for(int i = 0; i < (int)__countof(skip); i++)
		if(name == skip[i])
			return true;
	return false;
}

void DataStore::ScanDir(const String& dir, const String& collection, const String& rel_prefix, int depth)
{
	if(depth > MAX_DEPTH) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add(rel_prefix + ":depth_limit");
		return;
	}

	FindFile ff(AppendFileName(dir, "*"));
	while(ff) {
		if(entries_scanned >= MAX_FILES) {
			source_incomplete = true;
			source_errors++;
			source_diagnostics.Add(rel_prefix + "/" + ff.GetName() + ":entry_limit");
			break;
		}
		entries_scanned++;
		String name = ff.GetName();
		if(name == "." || name == "..") {
			ff.Next();
			continue;
		}
		String rel = rel_prefix.IsEmpty() ? name : rel_prefix + "/" + name;
		if(ff.IsSymLink()) {
			ff.Next();
			continue;
		}
		if(ff.IsFolder()) {
			if(!name.StartsWith(".") && !IsSkipDir(name))
				ScanDir(ff.GetPath(), collection, rel, depth + 1);
		}
		else {
			String ext = ToLower(GetFileExt(name));
			if(ff.IsFile() && ext == ".json" && files_scanned < MAX_FILES)
				IndexFile(ff.GetPath(), rel, collection);
			else if(ff.IsFile() && ext == ".puml" && diagram_sources.GetCount() < MAX_FILES)
				IndexDiagramSource(ff.GetPath(), rel);
			else if(ff.IsFile() && ((ext == ".json" && files_scanned >= MAX_FILES) || (ext == ".puml" && diagram_sources.GetCount() >= MAX_FILES))) {
				source_incomplete = true;
				source_errors++;
				source_diagnostics.Add(rel + ":entry_limit");
			}
		}
		ff.Next();
	}
}

void DataStore::IndexFile(const String& path, const String& rel_path, const String& collection)
{
	files_scanned++;

	int64 len = GetFileLength(path);
	if(ExceedsSourceBudget(len, source_bytes_scanned)) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add(rel_path + ":source_limit");
		return;
	}
	source_bytes_scanned += len;

	String json = LoadFile(path);
	source_fingerprints.Add(SourceFingerprint(rel_path, json));
	if(json.GetCount() != len) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add(rel_path + ":source_read_error");
		return;
	}
	JsonFidelity fidelity = JsonFidelityScanner(json).Scan();

	Value v;
	try {
		v = ParseJSON(json);
	}
	catch(Exc) {
		source_errors++;
		bool reported = false;
		if(fidelity.duplicate_key) { source_diagnostics.Add(rel_path + ":duplicate_key"); reported = true; }
		if(fidelity.trailing_input) { source_diagnostics.Add(rel_path + ":trailing_input"); reported = true; }
		if(fidelity.numeric_precision) { source_diagnostics.Add(rel_path + ":numeric_precision_unverified"); reported = true; }
		if(fidelity.escaped_key) { source_diagnostics.Add(rel_path + ":escaped_key_unverified"); reported = true; }
		if(!reported) source_diagnostics.Add(rel_path + ":parse_error");
		return;
	}
	if(v.IsError()) {
		source_errors++;
		bool reported = false;
		if(fidelity.duplicate_key) { source_diagnostics.Add(rel_path + ":duplicate_key"); reported = true; }
		if(fidelity.trailing_input) { source_diagnostics.Add(rel_path + ":trailing_input"); reported = true; }
		if(fidelity.numeric_precision) { source_diagnostics.Add(rel_path + ":numeric_precision_unverified"); reported = true; }
		if(fidelity.escaped_key) { source_diagnostics.Add(rel_path + ":escaped_key_unverified"); reported = true; }
		if(!reported) source_diagnostics.Add(rel_path + ":parse_error");
		return;
	}
	if(!v.Is<ValueMap>()) {
		source_errors++;
		source_diagnostics.Add(rel_path + (v.Is<ValueArray>() ? ":root_array_not_object" : (IsNull(v) ? ":root_null_not_object" : ":root_scalar_not_object")));
		return;
	}

	ValueMap m = v;
	Value kind_value = MapGet(m, "kind");
	if(!IsString(kind_value)) {
		source_errors++;
		source_diagnostics.Add(rel_path + ":kind_not_string");
		return;
	}
	String kind = kind_value;
	if(kind.IsEmpty())
		return;

	DataDocument& d = docs.Add();
	d.source_id = source_id;
	d.read_only = source_read_only;
	d.data = m;
	d.source_revision = RecordSourceRevision(rel_path, json);
	if(fidelity.duplicate_key || fidelity.trailing_input || fidelity.numeric_precision || fidelity.escaped_key || !fidelity.syntax_ok)
		d.raw_json = json;
	if(fidelity.duplicate_key) d.diagnostics.Add("parser_duplicate_key");
	if(fidelity.trailing_input) d.diagnostics.Add("parser_trailing_input");
	if(fidelity.numeric_precision) d.diagnostics.Add("parser_numeric_precision_unverified");
	if(fidelity.escaped_key) d.diagnostics.Add("parser_escaped_key_unverified");
	if(!fidelity.syntax_ok) d.diagnostics.Add("parser_fidelity_syntax_mismatch");
	d.kind = kind;
	d.id = MapGetString(m, "id");
	d.schema_id = MapGetString(m, "schema_id");
	d.schema_version = MapGetString(m, "schema_version");
	d.revision = MapGetString(m, "revision");
	d.visibility = MapGetString(m, "visibility");
	d.provenance_ref = MapGetString(m, "provenance_ref");
	d.title = MapGetString(m, "title");
	d.status = MapGetString(m, "status");
	d.description = MapGetString(m, "description");
	d.collection = collection;
	d.rel_path = rel_path;
	d.path = path;

	Vector<String> parts = Split(rel_path, '/');
	if(parts.GetCount() >= 3)
		d.scope = parts[1];
	if(parts.GetCount() >= 3)
		d.parent = parts[parts.GetCount() - 2];

	kind_order.FindAdd(kind);

	d.qualified_id = source_id + ":" + collection + ":" + (d.scope.IsEmpty() ? "_" : d.scope) + ":" + kind + ":" + d.id;
	ValidateDocument(d, FindSchemaCard(kind));
}

static bool IsJsonString(const Value& v) { return IsString(v); }
static bool IsJsonBoolean(const Value& v) { return v.Is<bool>(); }
static bool IsJsonInteger(const Value& v)
{
	if(v.Is<int>() || v.Is<int64>()) return true;
	if(!v.Is<double>()) return false;
	double number = v;
	return number >= -9223372036854775808.0 && number < 9223372036854775808.0 && (double)(int64)number == number;
}
static bool IsJsonObject(const Value& v) { return v.Is<ValueMap>(); }
static bool IsJsonArray(const Value& v) { return v.Is<ValueArray>(); }
static bool IsJsonNull(const Value& v) { return !v.Is<ValueMap>() && !v.Is<ValueArray>() && IsNull(v); }

static void ValidateObjectFields(const ValueMap& object, const Vector<SchemaCard::Field>& fields,
	                             const String& path, Vector<String>& diagnostics, int depth,
	                             bool allow_additional_properties = true);

static void ValidateFieldValue(const Value& value, const SchemaCard::Field& field, const String& path,
	                           Vector<String>& diagnostics, int depth)
{
	if(depth > 32) { diagnostics.Add("nested_depth_limit:" + path); return; }
	bool type_ok = field.type == "string" ? IsJsonString(value) :
	               field.type == "object" ? IsJsonObject(value) :
	               field.type == "array" ? IsJsonArray(value) :
	               (field.type == "boolean" || field.type == "bool") ? IsJsonBoolean(value) :
	               field.type == "integer" ? IsJsonInteger(value) : false;
	if(!type_ok) { diagnostics.Add("type:" + path); return; }
	if(field.type == "string" && field.max_utf8_bytes >= 0 && AsString(value).GetCount() > field.max_utf8_bytes)
		diagnostics.Add("max_bytes:" + path);
	if(field.type == "integer") {
		int64 number = value.Is<int>() ? (int64)(int)value : value.Is<int64>() ? (int64)value : (int64)(double)value;
		if(number < field.min_integer || number > field.max_integer)
			diagnostics.Add("integer_range:" + path);
	}
	if(field.enum_values.GetCount()) {
		String text = value;
		bool found = false;
		for(const String& allowed : field.enum_values) if(text == allowed) found = true;
		if(!found) diagnostics.Add("enum:" + path);
	}
	if(field.type == "object" && !field.properties.IsEmpty()) {
		ValueMap nested = value;
		ValidateObjectFields(nested, field.properties, path, diagnostics, depth + 1, field.allow_additional_properties);
	}
	if(field.type == "array" && !field.item_type.IsEmpty()) {
		ValueArray items = value;
		if(field.max_items >= 0 && items.GetCount() > field.max_items)
			diagnostics.Add("max_items:" + path);
		for(int i = 0; i < items.GetCount(); i++) {
			String item_path = path + "[" + AsString(i) + "]";
			bool item_type_ok = field.item_type == "object" ? items[i].Is<ValueMap>() :
			                    field.item_type == "array" ? items[i].Is<ValueArray>() :
			                    field.item_type == "string" ? IsString(items[i]) :
							field.item_type == "boolean" ? IsJsonBoolean(items[i]) :
							field.item_type == "integer" ? IsJsonInteger(items[i]) : false;
			if(!item_type_ok) { diagnostics.Add("nested_type:" + path); continue; }
			if(field.item_type == "string" && field.item_max_utf8_bytes >= 0 && AsString(items[i]).GetCount() > field.item_max_utf8_bytes)
				diagnostics.Add("item_max_bytes:" + path);
			if(field.item_type == "integer") {
				int64 number = items[i].Is<int>() ? (int64)(int)items[i] : items[i].Is<int64>() ? (int64)items[i] : (int64)(double)items[i];
				if(number < field.min_integer || number > field.max_integer)
					diagnostics.Add("item_integer_range:" + path);
			}
			if(field.item_type == "object" && !field.item_properties.IsEmpty()) {
				ValueMap item = items[i];
				ValidateObjectFields(item, field.item_properties, item_path, diagnostics, depth + 1);
			}
		}
	}
}

static void ValidateObjectFields(const ValueMap& object, const Vector<SchemaCard::Field>& fields,
	                             const String& path, Vector<String>& diagnostics, int depth,
	                             bool allow_additional_properties)
{
	if(!allow_additional_properties) {
		for(int i = 0; i < object.GetCount(); i++) {
			String key = object.GetKey(i);
			bool known = false;
			for(const SchemaCard::Field& field : fields) if(field.name == key) { known = true; break; }
			if(!known) diagnostics.Add("additional_property:" + path);
		}
	}
	for(const SchemaCard::Field& field : fields) {
		Value value;
		String field_path = path + "." + field.name;
		if(!GetTypedField(object, field.name, value)) {
			if(field.required) diagnostics.Add("required:" + field_path);
			continue;
		}
		ValidateFieldValue(value, field, field_path, diagnostics, depth + 1);
	}
}

void DataStore::ValidateDocument(DataDocument& d, const SchemaCard* schema) const
{
	ValueMap& m = d.data;
	auto diagnostic = [&](const String& text) {
		d.diagnostics.Add(text);
		if(d.invalid_reason.IsEmpty()) d.invalid_reason = text;
	};
	ValueMap envelope;
	Value ev = MapGet(m, "envelope");
	if(!ev.Is<ValueMap>()) {
		d.validated = false; d.validation_state = "quarantined"; diagnostic("envelope_must_be_object"); return;
	}
	envelope = ev;
	String workspace_id = MapGetString(envelope, "workspace_id");
	String stable_id = MapGetString(envelope, "stable_id");
	d.schema_id = MapGetString(envelope, "schema_id");
	d.schema_version = MapGetString(envelope, "schema_version");
	d.revision = MapGetString(envelope, "revision");
	String workspace_pattern_error;
	bool workspace_id_valid = MatchIdPattern(workspace_id, "^[A-Za-z0-9][A-Za-z0-9._/-]*$", workspace_pattern_error);
	d.workspace_id = workspace_id;
	if(IsString(MapGet(envelope, "workspace_id")) && IsString(MapGet(envelope, "stable_id")) &&
	   workspace_id_valid && !stable_id.IsEmpty() && stable_id == d.id)
		d.qualified_id = workspace_id + ":" + stable_id;
	if(!d.diagnostics.IsEmpty()) {
		d.validation_state = "quarantined";
		d.validated = false;
		return;
	}
	if(!schema) {
		d.validation_state = "quarantined";
		diagnostic("unsupported_kind");
		return;
	}
	if(d.schema_id != schema->schema_id || d.schema_version != schema->schema_version) {
		d.validation_state = "quarantined";
		diagnostic(d.schema_id.IsEmpty() || d.schema_version.IsEmpty() ? "legacy_envelope_missing" : "unsupported_schema_version");
		return;
	}
	d.validated = true;
	d.validation_state = "invalid";
	if(!workspace_id_valid) { diagnostic("workspace_id_pattern_mismatch"); return; }
	if(!IsJsonString(MapGet(envelope, "schema_id")) ||
	   !IsJsonString(MapGet(envelope, "workspace_id")) ||
	   !IsJsonString(MapGet(envelope, "stable_id")) || stable_id.IsEmpty() || stable_id != d.id ||
	   !IsJsonString(MapGet(envelope, "record_kind")) || MapGetString(envelope, "record_kind") != d.kind ||
	   !IsJsonString(MapGet(envelope, "revision")) || MapGetString(envelope, "revision").IsEmpty() ||
	   !IsJsonString(MapGet(envelope, "visibility")) || MapGetString(envelope, "visibility").IsEmpty() ||
	   !IsJsonString(MapGet(envelope, "provenance_ref")) || MapGetString(envelope, "provenance_ref").IsEmpty()) {
		diagnostic("invalid_envelope"); return;
	}
	String visibility = MapGetString(envelope, "visibility");
	if(visibility != "public" && visibility != "private" && visibility != "restricted") {
		diagnostic("enum:envelope.visibility"); return;
	}
	for(int i = 0; i < schema->required.GetCount(); i++) {
		const String& name = schema->required[i];
		Value value;
		if(!GetTypedField(m, name, value) || IsJsonNull(value) || value.IsVoid() || (IsString(value) && AsString(value).IsEmpty()))
			diagnostic("required:" + name);
	}
	for(const SchemaCard::Field& field : schema->fields) {
		Value value;
		if(GetTypedField(m, field.name, value))
			ValidateFieldValue(value, field, field.name, d.diagnostics, 0);
	}
	if(d.id.IsEmpty() || !IsString(MapGet(m, "id"))) diagnostic("id_must_be_nonempty_string");
	String pattern_diagnostic;
	if(!MatchIdPattern(d.id, schema->id_pattern, pattern_diagnostic)) diagnostic(pattern_diagnostic);
	if(d.diagnostics.IsEmpty()) { d.valid = true; d.validation_state = "valid"; }
	else { d.valid = false; invalid_count++; }
}

bool DataStore::MatchIdPattern(const String& id, const String& pattern, String& diagnostic)
{
	diagnostic.Clear();
	int pos = 0;
	if(pattern.GetCount() < 8 || pattern[pos++] != '^') { diagnostic = "unsupported_id_pattern"; return false; }
	auto parse_class = [&](bool allowed[128]) -> bool {
		if(pos >= pattern.GetCount() || pattern[pos++] != '[') return false;
		bool any = false;
		while(pos < pattern.GetCount() && pattern[pos] != ']') {
			unsigned char first = pattern[pos++];
			if(first >= 128 || first == '\\') return false;
			if(pos + 1 < pattern.GetCount() && pattern[pos] == '-' && pattern[pos + 1] != ']') {
				pos++;
				unsigned char last = pattern[pos++];
				if(last >= 128 || last < first || last == '\\') return false;
				for(int c = first; c <= last; c++) allowed[c] = true;
			}
			else allowed[first] = true;
			any = true;
		}
		if(pos >= pattern.GetCount() || pattern[pos++] != ']') return false;
		return any;
	};
	bool first[128] = {};
	bool rest[128] = {};
	if(!parse_class(first) || !parse_class(rest) || pos >= pattern.GetCount()) { diagnostic = "unsupported_id_pattern"; return false; }
	char quantifier = pattern[pos++];
	if((quantifier != '*' && quantifier != '+') || pos + 1 != pattern.GetCount() || pattern[pos] != '$') { diagnostic = "unsupported_id_pattern"; return false; }
	if(id.IsEmpty() || !first[(byte)id[0]]) { diagnostic = "id_pattern_mismatch"; return false; }
	if(quantifier == '+' && id.GetCount() < 2) { diagnostic = "id_pattern_mismatch"; return false; }
	for(int i = 1; i < id.GetCount(); i++) {
		byte c = id[i];
		if(c >= 128 || !rest[c]) { diagnostic = "id_pattern_mismatch"; return false; }
	}
	return true;
}

void DataStore::IndexDiagramSource(const String& path, const String& rel_path)
{
	int64 len = GetFileLength(path);
	if(ExceedsSourceBudget(len, source_bytes_scanned)) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add(rel_path + ":source_limit");
		return;
	}
	source_bytes_scanned += len;
	String bytes = LoadFile(path);
	if(bytes.GetCount() != len) {
		source_incomplete = true;
		source_errors++;
		source_diagnostics.Add(rel_path + ":source_read_error");
		return;
	}
	source_fingerprints.Add(SourceFingerprint(rel_path, bytes));
	diagram_sources.Add(rel_path);
}

Vector<String> DataStore::GetKinds() const
{
	Vector<String> res;
	for(int i = 0; i < kind_order.GetCount(); i++)
		res.Add(kind_order[i]);
	Sort(res);
	return res;
}

Vector<String> DataStore::GetCollections() const
{
	Index<String> set;
	for(int i = 0; i < docs.GetCount(); i++)
		set.FindAdd(docs[i].collection);
	Vector<String> res;
	for(int i = 0; i < set.GetCount(); i++)
		res.Add(set[i]);
	Sort(res);
	return res;
}

int DataStore::GetKindCount(const String& kind) const
{
	int n = 0;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].kind == kind)
			n++;
	return n;
}

int DataStore::GetInvalidCount() const
{
	return invalid_count;
}

int DataStore::GetQuarantineCount() const
{
	return quarantine_count;
}

Vector<const DataDocument*> DataStore::GetDocumentsForKind(const String& kind) const
{
	Vector<const DataDocument*> res;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].kind == kind)
			res.Add(&docs[i]);
	return res;
}

Vector<const DataDocument*> DataStore::GetDocumentsForCollection(const String& collection) const
{
	Vector<const DataDocument*> res;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].collection == collection)
			res.Add(&docs[i]);
	return res;
}

Vector<const DataDocument*> DataStore::GetDocumentsForScope(const String& collection, const String& scope) const
{
	Vector<const DataDocument*> res;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].collection == collection && docs[i].scope == scope)
			res.Add(&docs[i]);
	return res;
}

Vector<const DataDocument*> DataStore::GetInvalidDocuments() const
{
	Vector<const DataDocument*> res;
	for(int i = 0; i < docs.GetCount(); i++)
		if(!docs[i].valid)
			res.Add(&docs[i]);
	return res;
}

const DataDocument* DataStore::FindDocument(const String& id) const
{
	const DataDocument* found = NULL;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].qualified_id == id) {
			if(found) return NULL;
			found = &docs[i];
		}
	if(found) return found;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].id == id) {
			if(found) return NULL;
			found = &docs[i];
		}
	return found;
}

const DataDocument* DataStore::FindDocumentByRelPath(const String& rel) const
{
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].rel_path == rel)
			return &docs[i];
	return NULL;
}

const SchemaCard* DataStore::FindSchemaCard(const String& kind) const
{
	for(int i = 0; i < schemas.GetCount(); i++)
		if(schemas[i].kind == kind)
			return &schemas[i];
	return NULL;
}

static Mutex s_canonical_write_mutex;

struct CanonicalWriteGuard {
	CanonicalWriteGuard() { s_canonical_write_mutex.Enter(); }
	~CanonicalWriteGuard() { s_canonical_write_mutex.Leave(); }
};

enum AtomicReplaceOutcome { ATOMIC_REPLACE_FAILED, ATOMIC_REPLACE_COMMITTED, ATOMIC_REPLACE_DURABILITY_UNCERTAIN };

#ifdef PLATFORM_POSIX
struct LockedSourceRoot {
	int root_fd = -1;
	int lock_fd = -1;
	~LockedSourceRoot() {
		Close();
	}
	void Close() { if(lock_fd >= 0) { flock(lock_fd, LOCK_UN); close(lock_fd); lock_fd = -1; } if(root_fd >= 0) { close(root_fd); root_fd = -1; } }
	bool Open(const String& root) {
		if(!IsFullPath(root.ToStd().c_str())) return false;
		root_fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if(root_fd < 0) return false;
		Vector<String> parts = Split(root.Mid(1), '/');
		for(const String& part : parts) {
			if(part.IsEmpty()) continue;
			if(part == "." || part == "..") return false;
			int next = openat(root_fd, part.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if(next < 0) return false;
			close(root_fd);
			root_fd = next;
		}
		struct stat root_stat;
		if(fstat(root_fd, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode) || root_stat.st_uid != geteuid() ||
		   (root_stat.st_mode & (S_IWGRP | S_IWOTH))) return false;
		lock_fd = openat(root_fd, ".mgr-write.lock", O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
		if(lock_fd < 0) return false;
		struct stat lock_stat;
		if(fstat(lock_fd, &lock_stat) != 0 || !S_ISREG(lock_stat.st_mode) || lock_stat.st_uid != geteuid() || lock_stat.st_nlink != 1 ||
		   fchmod(lock_fd, 0600) != 0 || fstat(lock_fd, &lock_stat) != 0 || (lock_stat.st_mode & 077) != 0)
			return false;
		while(flock(lock_fd, LOCK_EX) != 0) {
			if(errno != EINTR) return false;
		}
		return true;
	}
};

bool DataStore::AcquireSourceLock(const String& root, bool shared, bool create_if_missing, int& lock_fd)
{
	lock_fd = -1;
	if(!IsFullPath(root.ToStd().c_str())) return false;
	int root_fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if(root_fd < 0) return false;
	Vector<String> parts = Split(root.Mid(1), '/');
	for(const String& part : parts) {
		if(part.IsEmpty()) continue;
		if(part == "." || part == "..") { close(root_fd); return false; }
		int next = openat(root_fd, part.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if(next < 0) { close(root_fd); return false; }
		close(root_fd);
		root_fd = next;
	}
	struct stat root_stat;
	if(fstat(root_fd, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode) || root_stat.st_uid != geteuid() ||
	   (root_stat.st_mode & (S_IWGRP | S_IWOTH))) { close(root_fd); return false; }
	int open_flags = (create_if_missing ? O_RDWR | O_CREAT : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
	int fd = openat(root_fd, ".mgr-write.lock", open_flags, 0600);
	int open_error = errno;
	close(root_fd);
	if(fd < 0 && !create_if_missing && open_error == ENOENT) return true;
	if(fd < 0) return false;
	struct stat st;
	if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 || (st.st_mode & 077)) {
		close(fd);
		return false;
	}
	if(create_if_missing && (fchmod(fd, 0600) != 0 || fstat(fd, &st) != 0 || (st.st_mode & 077))) { close(fd); return false; }
	while(flock(fd, shared ? LOCK_SH : LOCK_EX) != 0) {
		if(errno != EINTR) { close(fd); return false; }
	}
	lock_fd = fd;
	return true;
}

void DataStore::ReleaseSourceLock(int& lock_fd)
{
	if(lock_fd >= 0) { flock(lock_fd, LOCK_UN); close(lock_fd); lock_fd = -1; }
}

static bool OpenRecordParent(int root_fd, const String& rel_path, int& parent_fd, String& leaf)
{
	Vector<String> parts = Split(rel_path, '/');
	if(parts.IsEmpty()) return false;
	for(const String& part : parts)
		if(part.IsEmpty() || part == "." || part == ".." || part.Find('\\') >= 0) return false;
	parent_fd = dup(root_fd);
	if(parent_fd < 0) return false;
	for(int i = 0; i + 1 < parts.GetCount(); i++) {
		int next = openat(parent_fd, parts[i].ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if(next < 0) { close(parent_fd); parent_fd = -1; return false; }
		struct stat dir_stat;
		if(fstat(next, &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode) || dir_stat.st_uid != geteuid() ||
		   (dir_stat.st_mode & (S_IWGRP | S_IWOTH))) {
			close(next); close(parent_fd); parent_fd = -1; return false;
		}
		close(parent_fd);
		parent_fd = next;
	}
	leaf = parts.Top();
	return true;
}

static bool EnsureCreateRecordDirectory(int root_fd)
{
	if(mkdirat(root_fd, "records", 0700) != 0 && errno != EEXIST) return false;
	int fd = openat(root_fd, "records", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(fd < 0) return false;
	struct stat st;
	bool ok = fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() &&
	          (st.st_mode & 077) == 0 && fsync(fd) == 0 && fsync(root_fd) == 0;
	if(close(fd) != 0) ok = false;
	return ok;
}

static bool ReadRecordAt(int parent_fd, const String& leaf, String& bytes)
{
	int fd = openat(parent_fd, leaf.ToStd().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
	if(fd < 0) return false;
	struct stat st;
	if(fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)) || st.st_size < 0 ||
	   st.st_size > DataStore::MAX_SOURCE_FILE_BYTES) { close(fd); return false; }
	bytes.Clear();
	char buffer[16384];
	for(;;) {
		ssize_t n = read(fd, buffer, sizeof(buffer));
		if(n < 0 && errno == EINTR) continue;
		if(n < 0) { close(fd); return false; }
		if(n == 0) break;
		bytes.Cat(buffer, (int)n);
		if(bytes.GetCount() > DataStore::MAX_SOURCE_FILE_BYTES) { close(fd); return false; }
	}
	return close(fd) == 0;
}
#endif

#ifndef PLATFORM_POSIX
bool DataStore::AcquireSourceLock(const String&, bool, bool create_if_missing, int& lock_fd) { lock_fd = -1; return !create_if_missing; }
void DataStore::ReleaseSourceLock(int& lock_fd) { lock_fd = -1; }
#endif

static AtomicReplaceOutcome AtomicReplaceRecord(int parent_fd, const String& leaf, const String& bytes,
	                                                DataWriteTestFailpoint failpoint)
{
	if(failpoint == DATA_WRITE_FAIL_TEMP_CREATE || failpoint == DATA_WRITE_FAIL_TEMP_WRITE || failpoint == DATA_WRITE_FAIL_REPLACE)
		return ATOMIC_REPLACE_FAILED;
#ifdef PLATFORM_POSIX
	String temp = ".mgr-write-" + DataStore::ContentDigest(AsString(GetTickCount()) + AsString(Random64())).Mid(0, 24) + ".tmp";
	int fd = openat(parent_fd, temp.ToStd().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if(fd < 0) return ATOMIC_REPLACE_FAILED;
	bool ok = true;
	int64 written = 0;
	while(written < bytes.GetCount()) {
		ssize_t n = write(fd, bytes.Begin() + written, (size_t)(bytes.GetCount() - written));
		if(n < 0 && errno == EINTR) continue;
		if(n <= 0) { ok = false; break; }
		written += n;
	}
	if(ok && fsync(fd) != 0) ok = false;
	if(close(fd) != 0) ok = false;
	if(!ok) { unlinkat(parent_fd, temp.ToStd().c_str(), 0); return ATOMIC_REPLACE_FAILED; }
	if(renameat(parent_fd, temp.ToStd().c_str(), parent_fd, leaf.ToStd().c_str()) != 0) {
		unlinkat(parent_fd, temp.ToStd().c_str(), 0);
		return ATOMIC_REPLACE_FAILED;
	}
	bool durable = fsync(parent_fd) == 0;
	return durable ? ATOMIC_REPLACE_COMMITTED : ATOMIC_REPLACE_DURABILITY_UNCERTAIN;
#else
	(void)parent_fd; (void)leaf; (void)bytes;
	return ATOMIC_REPLACE_FAILED;
#endif
}

static AtomicReplaceOutcome AtomicCreateRecord(int parent_fd, const String& leaf, const String& bytes)
{
#ifdef PLATFORM_POSIX
	String temp = ".mgr-create-" + DataStore::ContentDigest(AsString(GetTickCount()) + AsString(Random64())).Mid(0, 24) + ".tmp";
	int fd = openat(parent_fd, temp.ToStd().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if(fd < 0) return ATOMIC_REPLACE_FAILED;
	int64 written = 0;
	bool ok = true;
	while(written < bytes.GetCount()) {
		ssize_t n = write(fd, bytes.Begin() + written, (size_t)(bytes.GetCount() - written));
		if(n < 0 && errno == EINTR) continue;
		if(n <= 0) { ok = false; break; }
		written += n;
	}
	if(ok && fsync(fd) != 0) ok = false;
	if(close(fd) != 0) ok = false;
	if(!ok || linkat(parent_fd, temp.ToStd().c_str(), parent_fd, leaf.ToStd().c_str(), 0) != 0) {
		unlinkat(parent_fd, temp.ToStd().c_str(), 0);
		return ATOMIC_REPLACE_FAILED;
	}
	if(unlinkat(parent_fd, temp.ToStd().c_str(), 0) != 0 || fsync(parent_fd) != 0) return ATOMIC_REPLACE_DURABILITY_UNCERTAIN;
	return ATOMIC_REPLACE_COMMITTED;
#else
	(void)parent_fd; (void)leaf; (void)bytes;
	return ATOMIC_REPLACE_FAILED;
#endif
}

#ifdef PLATFORM_POSIX
static bool HasConflictingCreateIdentity(int dir_fd, const String& prefix, int depth,
	                                         const String& workspace_id, const String& record_id,
	                                         const String& expected_path, bool& conflict,
	                                         int& entries_scanned, int64& bytes_scanned);
#endif

JournalStageResult DataStore::PrepareJournal(const DataWriteAuthorization& authorization,
	                                            const String& operation_id,
	                                            const Vector<JournalStageRecord>& records,
	                                            DataWriteTestFailpoint test_failpoint)
{
	JournalStageResult result;
	result.status = "invalid_request";
	if(source_read_only || !source_write_enabled || base_dir.IsEmpty() || !authorization.IsAllowed() ||
	   authorization.action_scope != "record:write" || authorization.source_id != source_id ||
	   authorization.workspace_id.IsEmpty()) return result;
	CanonicalWriteGuard write_guard;
#ifndef PLATFORM_POSIX
	result.status = "unsupported_platform";
	return result;
#else
	if(!IsSafeRoot(base_dir) || !IsSafeSourceId(source_id)) { result.status = "source_unavailable"; return result; }
	LockedSourceRoot root;
	if(!root.Open(base_dir)) { result.status = "source_unavailable"; return result; }
	if(!HasCurrentWriteRegistration()) { result.status = "source_capability_changed"; return result; }
	return PrepareJournalLocked(authorization, operation_id, records, root.root_fd, test_failpoint);
#endif
}

JournalStageResult DataStore::PrepareJournalLocked(const DataWriteAuthorization& authorization,
	                                               const String& operation_id,
	                                               const Vector<JournalStageRecord>& records,
	                                               int root_fd,
	                                               DataWriteTestFailpoint test_failpoint) const
{
	JournalStageResult result;
	result.status = "invalid_request";
	if(source_read_only || !source_write_enabled || base_dir.IsEmpty() || !authorization.IsAllowed() ||
	   authorization.action_scope != "record:write" || authorization.source_id != source_id ||
	   authorization.workspace_id.IsEmpty() || records.IsEmpty() || records.GetCount() > 64 ||
	   operation_id.IsEmpty() || operation_id.GetCount() > 64) return result;
	for(int i = 0; i < operation_id.GetCount(); i++) {
		char c = operation_id[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return result;
	}
	for(const JournalStageRecord& record : records)
		if((record.action != "update" && record.action != "create") ||
		   (record.action == "update" && record.old_payload.IsEmpty()) ||
		   (record.action == "create" && !record.old_payload.IsEmpty()) || record.new_payload.IsEmpty() ||
		   record.old_payload.GetCount() > MAX_SOURCE_FILE_BYTES || record.new_payload.GetCount() > MAX_SOURCE_FILE_BYTES)
			return result;
#ifndef PLATFORM_POSIX
	result.status = "unsupported_platform";
	return result;
#else
	if(!IsSafeRoot(base_dir) || !IsSafeSourceId(source_id)) { result.status = "source_unavailable"; return result; }
	if(!HasCurrentWriteRegistration()) { result.status = "source_capability_changed"; return result; }
	const String journal_name = ".mgr-journal";
	struct stat st;
	if(fstatat(root_fd, journal_name.ToStd().c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0) {
		result.status = S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 077) ?
		                "journal_recovery_required" : "journal_cleanup_required";
		return result;
	}
	if(errno != ENOENT) { result.status = "journal_cleanup_required"; return result; }
	bool created_journal_dir = mkdirat(root_fd, journal_name.ToStd().c_str(), 0700) == 0;
	if(!created_journal_dir) {
		if(errno == EEXIST) { result.status = "journal_recovery_required"; return result; }
		result.status = "stage_failed"; return result;
	}
	int journal_fd = openat(root_fd, journal_name.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(journal_fd < 0 || fstat(journal_fd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077) != 0) {
		bool clean = true;
		if(journal_fd >= 0 && close(journal_fd) != 0) clean = false;
		if(unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0) clean = false;
		result.status = clean ? "stage_failed" : "journal_cleanup_required"; return result;
	}
	if(test_failpoint == DATA_WRITE_FAIL_JOURNAL_ROOT_SYNC || fsync(root_fd) != 0) {
		bool clean = close(journal_fd) == 0;
		if(created_journal_dir) {
			if(unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0) clean = false;
		}
		result.status = clean ? "stage_failed" : "journal_cleanup_required";
		return result;
	}
	if(mkdirat(journal_fd, operation_id.ToStd().c_str(), 0700) != 0) {
		int mkdir_error = errno;
		bool clean = close(journal_fd) == 0;
		if(mkdir_error == EEXIST) result.status = "journal_recovery_required";
		else {
			if(created_journal_dir && (unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0)) clean = false;
			result.status = clean ? "stage_failed" : "journal_cleanup_required";
		}
		return result;
	}
	int op_fd = openat(journal_fd, operation_id.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(op_fd < 0 || fstat(op_fd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077) != 0) {
		bool clean = true;
		if(op_fd >= 0 && close(op_fd) != 0) clean = false;
		if(unlinkat(journal_fd, operation_id.ToStd().c_str(), AT_REMOVEDIR) != 0) clean = false;
		if(fsync(journal_fd) != 0 || close(journal_fd) != 0) clean = false;
		if(created_journal_dir && (unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0)) clean = false;
		result.status = clean ? "stage_failed" : "journal_cleanup_required"; return result;
	}
	if(test_failpoint == DATA_WRITE_FAIL_JOURNAL_OPERATION_SYNC || fsync(journal_fd) != 0) {
		bool clean = close(op_fd) == 0;
		if(unlinkat(journal_fd, operation_id.ToStd().c_str(), AT_REMOVEDIR) != 0) clean = false;
		if(fsync(journal_fd) != 0 || close(journal_fd) != 0) clean = false;
		if(created_journal_dir && (unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0)) clean = false;
		result.status = clean ? "stage_failed" : "journal_cleanup_required";
		return result;
	}
	// Durability order: journal root, operation directory entry, payloads and their
	// entries, then this directory sync, followed by atomic prepared-manifest publish
	// (AtomicReplaceRecord syncs the operation directory before success).
	auto cleanup = [&]() -> bool {
		bool clean = true;
		for(int n = 0; n < records.GetCount(); n++) {
			String index = AsString(n);
			String backup = "backup-" + index + ".bin", payload = "payload-" + index + ".bin";
			if(!(test_failpoint == DATA_WRITE_FAIL_JOURNAL_CLEANUP && n == 0) && unlinkat(op_fd, backup.ToStd().c_str(), 0) != 0 && errno != ENOENT) clean = false;
			if(unlinkat(op_fd, payload.ToStd().c_str(), 0) != 0 && errno != ENOENT) clean = false;
		}
		if(unlinkat(op_fd, "manifest.json", 0) != 0 && errno != ENOENT) clean = false;
		if(fsync(op_fd) != 0) clean = false;
		if(close(op_fd) != 0) clean = false;
		if(unlinkat(journal_fd, operation_id.ToStd().c_str(), AT_REMOVEDIR) != 0) clean = false;
		if(fsync(journal_fd) != 0) clean = false;
		if(close(journal_fd) != 0) clean = false;
			if(created_journal_dir && (unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0)) clean = false;
		return clean;
	};
	auto fail_stage = [&](const String& status) {
		result.status = cleanup() ? status : "journal_cleanup_required";
		return result;
	};
	JournalManifest manifest;
	manifest.version = 1; manifest.operation_id = operation_id; manifest.source_id = source_id;
	manifest.workspace_id = authorization.workspace_id; manifest.state = "prepared";
	int64 total = 0;
	Index<String> staged_create_ids, staged_create_paths;
	for(int n = 0; n < records.GetCount(); n++) {
		const JournalStageRecord& input = records[n];
		const DataDocument* doc = FindDocument(input.qualified_id);
		String rel_path = input.rel_path;
		if(input.action == "create") {
			Value created_value = ParseJSON(input.new_payload);
			if(doc || !created_value.Is<ValueMap>() || !input.expected_revision.IsEmpty()) return fail_stage("identity_conflict");
			ValueMap created = created_value;
			String kind = MapGetString(created, "kind"), id = MapGetString(created, "id");
			if(id.IsEmpty() || id.Find('/') >= 0 || id.Find('\\') >= 0 || id.Find("..") >= 0 ||
			   input.qualified_id != authorization.workspace_id + ":" + id || input.new_revision.IsEmpty()) return fail_stage("identity_or_schema_invalid");
			DataDocument created_doc; created_doc.data = created; created_doc.id = id; created_doc.kind = kind;
			ValidateDocument(created_doc, FindSchemaCard(kind));
			if(!created_doc.valid || !created_doc.validated || created_doc.workspace_id != authorization.workspace_id ||
			   created_doc.revision != "1" || input.new_revision != "1")
				return fail_stage("identity_or_schema_invalid");
			rel_path = CreateJournalPath(input.qualified_id);
			if(rel_path.IsEmpty() || staged_create_ids.Find(input.qualified_id) >= 0 || staged_create_paths.Find(rel_path) >= 0)
				return fail_stage("identity_conflict");
			staged_create_ids.Add(input.qualified_id); staged_create_paths.Add(rel_path);
			for(const DataDocument& existing : docs)
				if(existing.id == id && existing.workspace_id == authorization.workspace_id) return fail_stage("identity_conflict");
			bool identity_collision = false;
			int entries_scanned = 0; int64 bytes_scanned = 0;
			if(!HasConflictingCreateIdentity(root_fd, "", 0, authorization.workspace_id, id, rel_path,
			                                 identity_collision, entries_scanned, bytes_scanned))
				return fail_stage("identity_scan_unavailable");
			if(identity_collision) return fail_stage("identity_conflict");
			manifest.version = 2;
		}
		else if(!doc || !doc->valid || !doc->validated || doc->workspace_id != authorization.workspace_id || doc->rel_path != rel_path ||
		        input.expected_revision != doc->revision || input.new_revision.IsEmpty()) return fail_stage("scope_or_revision_denied");
		int parent = -1; String leaf;
		if(!OpenRecordParent(root_fd, rel_path, parent, leaf)) return fail_stage("invalid_path");
		String actual; bool read_ok = ReadRecordAt(parent, leaf, actual);
		if(input.action == "create") {
			struct stat target_stat;
			bool absent = fstatat(parent, leaf.ToStd().c_str(), &target_stat, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
			close(parent);
			if(!absent) return fail_stage("conflict");
		}
		else { close(parent); if(!read_ok || actual != input.old_payload) return fail_stage("conflict"); }
		total += input.old_payload.GetCount() + input.new_payload.GetCount();
		if(total > 64LL * MAX_SOURCE_FILE_BYTES) return fail_stage("size_limit");
		JournalRecord item;
		item.action = input.action;
		item.source_id = source_id; item.record_id = input.action == "create" ? input.qualified_id.Mid(authorization.workspace_id.GetCount() + 1) : doc->id; item.rel_path = rel_path;
		item.expected_revision = input.expected_revision; item.new_revision = input.new_revision;
		item.backup_ref = input.action == "create" ? String() : "backup-" + AsString(n) + ".bin"; item.payload_ref = "payload-" + AsString(n) + ".bin";
		item.old_hash = input.action == "create" ? String() : ContentDigest(input.old_payload); item.new_hash = ContentDigest(input.new_payload); item.state = "pending";
		manifest.records.Add(item);
		if(test_failpoint == DATA_WRITE_FAIL_JOURNAL_PAYLOAD ||
		   (!item.backup_ref.IsEmpty() && AtomicReplaceRecord(op_fd, item.backup_ref, input.old_payload, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) ||
		   AtomicReplaceRecord(op_fd, item.payload_ref, input.new_payload, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED ||
		   test_failpoint == DATA_WRITE_FAIL_JOURNAL_CLEANUP) {
			return fail_stage("stage_failed");
		}
	}
	if(test_failpoint == DATA_WRITE_FAIL_JOURNAL_PRE_MANIFEST_SYNC || fsync(op_fd) != 0)
		return fail_stage("stage_failed");
	ValueArray entries;
	for(const JournalRecord& item : manifest.records) {
		ValueMap row;
		row.Add("source_id", item.source_id); row.Add("record_id", item.record_id); row.Add("path", item.rel_path);
		if(manifest.version >= 2) row.Add("action", item.action);
		row.Add("expected_revision", item.expected_revision); row.Add("new_revision", item.new_revision);
		row.Add("backup_ref", item.backup_ref); row.Add("payload_ref", item.payload_ref);
		row.Add("old_hash", item.old_hash); row.Add("new_hash", item.new_hash); row.Add("state", item.state);
		entries.Add(row);
	}
	ValueMap root_map;
	root_map.Add("version", manifest.version); root_map.Add("operation_id", operation_id); root_map.Add("source_id", source_id);
	root_map.Add("workspace_id", authorization.workspace_id);
	root_map.Add("state", "prepared"); root_map.Add("records", entries);
	root_map.Add("request_digest", "");
	String manifest_bytes = AsJSON(root_map, true);
	JournalManifest verified;
	if(test_failpoint == DATA_WRITE_FAIL_JOURNAL_MANIFEST || manifest_bytes.GetCount() > 65536 || !ParseJournalManifest(manifest_bytes, verified) ||
	   AtomicReplaceRecord(op_fd, "manifest.json", manifest_bytes, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) {
		return fail_stage("stage_failed");
	}
	bool op_close_ok = close(op_fd) == 0;
	bool journal_close_ok = close(journal_fd) == 0;
	result.operation_id = operation_id;
	result.status = test_failpoint == DATA_WRITE_FAIL_JOURNAL_DIRECTORY_CLOSE || !op_close_ok || !journal_close_ok ?
	                "prepared_uncertain" : "prepared";
	return result;
#endif
}

#ifdef PLATFORM_POSIX
static const int MAX_COMMIT_RECEIPTS = 128;
static const int MAX_COMMIT_RECEIPTS_BYTES = 256 * 1024;
struct CommitReceipt : Moveable<CommitReceipt> {
	String operation_id, source_id, workspace_id, digest;
	Vector<String> ids, revisions;
	CommitReceipt() {}
	CommitReceipt(const CommitReceipt& o) : operation_id(o.operation_id), source_id(o.source_id), workspace_id(o.workspace_id), digest(o.digest), ids(clone(o.ids)), revisions(clone(o.revisions)) {}
	CommitReceipt& operator=(const CommitReceipt& o) {
		if(this != &o) { operation_id = o.operation_id; source_id = o.source_id; workspace_id = o.workspace_id; digest = o.digest; ids = clone(o.ids); revisions = clone(o.revisions); }
		return *this;
	}
};
static bool StoreCommitReceipt(int root_fd, const CommitReceipt& receipt, DataWriteTestFailpoint failpoint, bool dry_run = false);
static bool ReadJournalFile(int dir_fd, const String& name, int64 limit, String& bytes)
{
	int fd = openat(dir_fd, name.ToStd().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
	if(fd < 0) return false;
	struct stat st;
	bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
	          st.st_nlink == 1 && !(st.st_mode & 077) && st.st_size >= 0 && st.st_size <= limit;
	bytes.Clear();
	char buffer[8192];
	while(ok) {
		ssize_t n = read(fd, buffer, sizeof(buffer));
		if(n < 0 && errno == EINTR) continue;
		if(n < 0) { ok = false; break; }
		if(!n) break;
		bytes.Cat(buffer, (int)n);
		if(bytes.GetCount() > limit) ok = false;
	}
	if(close(fd) != 0) ok = false;
	return ok;
}

static bool HasConflictingCreateIdentity(int dir_fd, const String& prefix, int depth,
	                                         const String& workspace_id, const String& record_id,
	                                         const String& expected_path, bool& conflict,
	                                         int& entries_scanned, int64& bytes_scanned)
{
	if(depth > DataStore::MAX_DEPTH) return false;
	int scan_fd = openat(dir_fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(scan_fd < 0) return false;
	DIR *dir = fdopendir(scan_fd);
	if(!dir) { close(scan_fd); return false; }
	bool ok = true;
	struct dirent *entry;
	while(ok && (entry = readdir(dir)) != NULL) {
		String name = entry->d_name;
		if(name == "." || name == ".." || name.StartsWith(".mgr-")) continue;
		if(++entries_scanned > DataStore::MAX_FILES) { ok = false; break; }
		String rel = prefix.IsEmpty() ? name : prefix + "/" + name;
		struct stat st;
		if(fstatat(dir_fd, name.ToStd().c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) { ok = false; break; }
		if(S_ISDIR(st.st_mode)) {
			int child = openat(dir_fd, name.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if(child < 0) { ok = false; break; }
			ok = HasConflictingCreateIdentity(child, rel, depth + 1, workspace_id, record_id, expected_path, conflict,
			                                  entries_scanned, bytes_scanned);
			close(child);
		}
		else if(S_ISREG(st.st_mode) && name.EndsWith(".json")) {
			if(st.st_size < 0 || st.st_size > DataStore::MAX_SOURCE_FILE_BYTES ||
			   bytes_scanned > DataStore::MAX_TOTAL_SOURCE_BYTES - st.st_size) { ok = false; break; }
			bytes_scanned += st.st_size;
			String bytes;
			int fd = openat(dir_fd, name.ToStd().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
			if(fd < 0) { ok = false; break; }
			struct stat opened;
			ok = fstat(fd, &opened) == 0 && S_ISREG(opened.st_mode) && opened.st_dev == st.st_dev && opened.st_ino == st.st_ino &&
			     opened.st_size == st.st_size && opened.st_size <= DataStore::MAX_SOURCE_FILE_BYTES;
			char buffer[8192];
			while(ok) {
				ssize_t n = read(fd, buffer, sizeof(buffer));
				if(n < 0 && errno == EINTR) continue;
				if(n < 0) { ok = false; break; }
				if(n == 0) break;
				bytes.Cat(buffer, (int)n);
				if(bytes.GetCount() > DataStore::MAX_SOURCE_FILE_BYTES) ok = false;
			}
			if(close(fd) != 0) ok = false;
			if(!ok) break;
			Value value;
			JsonFidelity fidelity = JsonFidelityScanner(bytes).Scan();
			if(!fidelity.syntax_ok || fidelity.duplicate_key || fidelity.trailing_input || fidelity.numeric_precision || fidelity.escaped_key) { ok = false; break; }
			try { value = ParseJSON(bytes); }
			catch(Exc) { ok = false; break; }
			if(value.IsError()) { ok = false; break; }
			if(value.Is<ValueMap>()) {
				ValueMap record = value, envelope = MapGet(record, "envelope");
				if(MapGetString(record, "id") == record_id && MapGetString(envelope, "workspace_id") == workspace_id && rel != expected_path)
					conflict = true;
			}
		}
	}
	if(closedir(dir) != 0) ok = false;
	return ok;
}

static bool JournalArtifactsMatch(int op_fd, const JournalManifest& manifest)
{
	Index<String> expected;
	expected.Add("manifest.json");
	for(const JournalRecord& record : manifest.records) {
		if(!record.backup_ref.IsEmpty()) expected.Add(record.backup_ref);
		expected.Add(record.payload_ref);
	}
	int scan_fd = openat(op_fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(scan_fd < 0) return false;
	DIR *dir = fdopendir(scan_fd);
	if(!dir) { close(scan_fd); return false; }
	bool ok = true;
	Index<String> seen;
	struct dirent *entry;
	while((entry = readdir(dir)) != NULL) {
		String name = entry->d_name;
		if(name == "." || name == "..") continue;
		struct stat st;
		if(expected.Find(name) < 0 || fstatat(op_fd, name.ToStd().c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 ||
		   !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 || (st.st_mode & 077))
			ok = false;
		else seen.Add(name);
	}
	if(closedir(dir) != 0) ok = false;
	return ok && seen.GetCount() == expected.GetCount();
}

static bool RemoveJournalContents(int op_fd, const JournalManifest& manifest)
{
	if(!JournalArtifactsMatch(op_fd, manifest)) return false;
	for(const JournalRecord& record : manifest.records) {
		if((!record.backup_ref.IsEmpty() && unlinkat(op_fd, record.backup_ref.ToStd().c_str(), 0) != 0) ||
		   unlinkat(op_fd, record.payload_ref.ToStd().c_str(), 0) != 0) return false;
	}
	if(unlinkat(op_fd, "manifest.json", 0) != 0) return false;
	return fsync(op_fd) == 0;
}

static ValueMap JournalManifestMap(const JournalManifest& manifest);

static bool DiscardPreparedJournalLocked(int root_fd, const String& source_id,
	                                     const String& workspace_id, const String& operation_id)
{
	if(operation_id.IsEmpty() || operation_id.GetCount() > 64) return false;
	for(int i = 0; i < operation_id.GetCount(); i++) {
		char c = operation_id[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
	}
	int journal_fd = openat(root_fd, ".mgr-journal", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(journal_fd < 0) return false;
	int op_fd = openat(journal_fd, operation_id.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(op_fd < 0) { close(journal_fd); return false; }
	String bytes; JournalManifest manifest;
	bool valid = ReadJournalFile(op_fd, "manifest.json", 65536, bytes) && DataStore::ParseJournalManifest(bytes, manifest) &&
	             manifest.operation_id == operation_id && manifest.source_id == source_id &&
	             manifest.workspace_id == workspace_id && manifest.state == "prepared";
	bool clean = valid && JournalArtifactsMatch(op_fd, manifest) && RemoveJournalContents(op_fd, manifest);
	if(close(op_fd) != 0) clean = false;
	if(clean && (unlinkat(journal_fd, operation_id.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(journal_fd) != 0)) clean = false;
	if(close(journal_fd) != 0) clean = false;
	if(clean && (unlinkat(root_fd, ".mgr-journal", AT_REMOVEDIR) != 0 || fsync(root_fd) != 0)) clean = false;
	return clean;
}
#endif

JournalRecoveryResult DataStore::RecoverPendingBatches(const DataWriteAuthorization& authorization,
	                                                      DataWriteTestFailpoint test_failpoint) const
{
	JournalRecoveryResult result;
	result.status = "invalid_request";
	if(source_read_only || !source_write_enabled || base_dir.IsEmpty() || !authorization.IsAllowed() ||
	   authorization.action_scope != "record:write" || authorization.source_id != source_id || authorization.workspace_id.IsEmpty())
		return result;
	if(!IsSafeRoot(base_dir) || !IsSafeSourceId(source_id)) { result.status = "source_unavailable"; return result; }
	Mutex::Lock lock(s_canonical_write_mutex);
#ifndef PLATFORM_POSIX
	result.status = "unsupported_platform";
	return result;
#else
	if(!synthetic_write_source) {
		DataSourceRegistration authoritative;
		if(!ReadConfiguredRegistration(authoritative) || !authoritative.enabled || authoritative.read_only || !authoritative.write_enabled ||
		   NormalizePath(authoritative.root) != base_dir || authoritative.id != source_id) {
			result.status = "source_capability_changed"; return result;
		}
	}
	LockedSourceRoot root;
	if(!root.Open(base_dir)) { result.status = "source_unavailable"; return result; }
	struct stat journal_st;
	if(fstatat(root.root_fd, ".mgr-journal", &journal_st, AT_SYMLINK_NOFOLLOW) != 0) {
		result.status = errno == ENOENT ? "clean" : "journal_conflict"; return result;
	}
	if(!S_ISDIR(journal_st.st_mode) || journal_st.st_uid != geteuid() || (journal_st.st_mode & 077)) {
		result.status = "journal_conflict"; return result;
	}
	int journal_fd = openat(root.root_fd, ".mgr-journal", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(journal_fd < 0) { result.status = "journal_conflict"; return result; }
	int scan_fd = dup(journal_fd);
	DIR *journal = scan_fd >= 0 ? fdopendir(scan_fd) : NULL;
	if(!journal) { if(scan_fd >= 0) close(scan_fd); close(journal_fd); result.status = "journal_conflict"; return result; }
	bool failed = false;
	bool scope_denied = false;
	bool orphan_unrecognized = false;
	struct dirent *entry;
	while(!failed && (entry = readdir(journal)) != NULL) {
		String op_id = entry->d_name;
		if(op_id == "." || op_id == "..") continue;
		if(op_id.IsEmpty() || op_id.GetCount() > 64) { failed = true; break; }
		for(int i = 0; i < op_id.GetCount(); i++) {
			char c = op_id[i];
			if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) failed = true;
		}
		struct stat op_st;
		if(failed || fstatat(journal_fd, op_id.ToStd().c_str(), &op_st, AT_SYMLINK_NOFOLLOW) != 0 ||
		   !S_ISDIR(op_st.st_mode) || op_st.st_uid != geteuid() || (op_st.st_mode & 077)) { failed = true; break; }
		int op_fd = openat(journal_fd, op_id.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if(op_fd < 0) { failed = true; break; }
		String manifest_bytes;
		bool has_manifest = fstatat(op_fd, "manifest.json", &op_st, AT_SYMLINK_NOFOLLOW) == 0;
		if(!has_manifest) {
			close(op_fd);
			orphan_unrecognized = true; failed = true; break;
		}
		JournalManifest manifest;
		if(!ReadJournalFile(op_fd, "manifest.json", 65536, manifest_bytes) || !ParseJournalManifest(manifest_bytes, manifest)) {
			close(op_fd); orphan_unrecognized = true; failed = true; break;
		}
		if(manifest.operation_id != op_id || manifest.source_id != source_id) { close(op_fd); failed = true; break; }
		if(manifest.workspace_id != authorization.workspace_id) {
			close(op_fd); scope_denied = true; failed = true; break;
		}
		if(!JournalArtifactsMatch(op_fd, manifest)) { close(op_fd); orphan_unrecognized = true; failed = true; break; }
		Vector<String> old_payloads, new_payloads, current_bytes;
		for(const JournalRecord& rec : manifest.records) {
			String old_bytes, new_bytes;
			if((rec.action == "update" && !ReadJournalFile(op_fd, rec.backup_ref, MAX_SOURCE_FILE_BYTES, old_bytes)) ||
			   !ReadJournalFile(op_fd, rec.payload_ref, MAX_SOURCE_FILE_BYTES, new_bytes) ||
			   (rec.action == "update" && ContentDigest(old_bytes) != rec.old_hash) || ContentDigest(new_bytes) != rec.new_hash) { failed = true; break; }
			Value new_value = ParseJSON(new_bytes);
			if(!new_value.Is<ValueMap>()) { failed = true; break; }
			ValueMap new_map = new_value;
			DataDocument new_doc; new_doc.data = new_map;
			new_doc.id = MapGetString(new_map, "id"); new_doc.kind = MapGetString(new_map, "kind");
			if(new_doc.id != rec.record_id || new_doc.kind.IsEmpty()) { failed = true; break; }
			const SchemaCard *schema = FindSchemaCard(new_doc.kind);
			if(!schema) { failed = true; break; }
			ValidateDocument(new_doc, schema);
			if(!new_doc.valid || !new_doc.validated || new_doc.workspace_id != manifest.workspace_id || new_doc.revision != rec.new_revision ||
			   (rec.action == "create" && rec.new_revision != "1")) {
				failed = true; break;
			}
			if(rec.action == "update") {
				Value old_value = ParseJSON(old_bytes);
				if(!old_value.Is<ValueMap>()) { failed = true; break; }
				ValueMap old_map = old_value; DataDocument old_doc; old_doc.data = old_map;
				old_doc.id = MapGetString(old_map, "id"); old_doc.kind = MapGetString(old_map, "kind");
				ValidateDocument(old_doc, schema);
				if(!old_doc.valid || !old_doc.validated || old_doc.id != rec.record_id || old_doc.kind != new_doc.kind ||
				   old_doc.workspace_id != manifest.workspace_id || old_doc.revision != rec.expected_revision) { failed = true; break; }
			}
			else {
				if(rec.action != "create" || manifest.version != 2 || !old_bytes.IsEmpty() ||
				   rec.rel_path != CreateJournalPath(manifest.workspace_id + ":" + rec.record_id)) { failed = true; break; }
			}
			int parent = -1; String leaf, actual;
			if(!OpenRecordParent(root.root_fd, rec.rel_path, parent, leaf)) { failed = true; break; }
			bool read_ok = ReadRecordAt(parent, leaf, actual);
			struct stat target_stat;
			bool absent = fstatat(parent, leaf.ToStd().c_str(), &target_stat, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
			close(parent);
			if(rec.action == "create" ? (!read_ok && !absent) : !read_ok) { failed = true; break; }
			if(rec.action == "create" && read_ok && ContentDigest(actual) != rec.new_hash) { failed = true; break; }
			if(rec.action == "create") {
				bool collision = false;
				int entries_scanned = 0; int64 bytes_scanned = 0;
				if(!HasConflictingCreateIdentity(root.root_fd, "", 0, manifest.workspace_id, rec.record_id, rec.rel_path,
				                                 collision, entries_scanned, bytes_scanned) || collision) {
					failed = true; break;
				}
			}
			old_payloads.Add(old_bytes); new_payloads.Add(new_bytes); current_bytes.Add(actual);
		}
		if(failed) { close(op_fd); break; }
		if(manifest.state == "rolled-back") {
			for(int i = 0; i < current_bytes.GetCount(); i++)
				if(manifest.records[i].action == "create" ? !current_bytes[i].IsEmpty() : current_bytes[i] != old_payloads[i]) failed = true;
		}
		else if(manifest.state != "prepared") {
			for(int i = 0; i < current_bytes.GetCount(); i++)
				if(manifest.records[i].action == "create" ? (!current_bytes[i].IsEmpty() && current_bytes[i] != new_payloads[i]) :
				   (current_bytes[i] != old_payloads[i] && current_bytes[i] != new_payloads[i])) failed = true;
		}
		if(failed || test_failpoint == DATA_WRITE_FAIL_RECOVERY_BEFORE_RECORD) { close(op_fd); failed = true; break; }
		bool rollback = manifest.state == "committing" || manifest.state == "rolled-back";
		if(manifest.state != "prepared") {
			for(int i = 0; i < manifest.records.GetCount(); i++) {
				if(manifest.records[i].action == "create" && rollback) {
					if(current_bytes[i].IsEmpty()) continue;
					int parent = -1; String leaf;
					if(!OpenRecordParent(root.root_fd, manifest.records[i].rel_path, parent, leaf) ||
					   current_bytes[i] != new_payloads[i] || unlinkat(parent, leaf.ToStd().c_str(), 0) != 0 || fsync(parent) != 0) {
						if(parent >= 0) close(parent); failed = true; break;
					}
					close(parent); result.recovered_records++;
					if(test_failpoint == DATA_WRITE_FAIL_RECOVERY_AFTER_RECORD) { failed = true; break; }
					continue;
				}
				const String& target = rollback ? old_payloads[i] : new_payloads[i];
				if(current_bytes[i] == target) continue;
				int parent = -1; String leaf;
				AtomicReplaceOutcome publish = ATOMIC_REPLACE_FAILED;
				if(OpenRecordParent(root.root_fd, manifest.records[i].rel_path, parent, leaf))
					publish = manifest.records[i].action == "create" ? AtomicCreateRecord(parent, leaf, target) :
					          AtomicReplaceRecord(parent, leaf, target, DATA_WRITE_FAIL_NONE);
				if(parent < 0 || publish != ATOMIC_REPLACE_COMMITTED) {
					if(parent >= 0) close(parent); failed = true; break;
				}
				close(parent); result.recovered_records++;
				if(test_failpoint == DATA_WRITE_FAIL_RECOVERY_AFTER_RECORD) { failed = true; break; }
			}
		}
		if(failed) { close(op_fd); failed = true; break; }
		if(manifest.state == "committing") {
			manifest.state = "rolled-back";
			for(JournalRecord& rec : manifest.records) rec.state = "rolled-back";
			String rolled_back_bytes = AsJSON(JournalManifestMap(manifest), true);
			if(AtomicReplaceRecord(op_fd, "manifest.json", rolled_back_bytes, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) {
				close(op_fd); failed = true; break;
			}
		}
		if(manifest.state == "committed") {
			CommitReceipt receipt;
			receipt.operation_id = manifest.operation_id; receipt.source_id = manifest.source_id;
			receipt.workspace_id = manifest.workspace_id; receipt.digest = manifest.request_digest;
			for(const JournalRecord& rec : manifest.records) {
				receipt.ids.Add(manifest.workspace_id + ":" + rec.record_id);
				receipt.revisions.Add(rec.new_revision);
			}
			if(!StoreCommitReceipt(root.root_fd, receipt, DATA_WRITE_FAIL_NONE)) { close(op_fd); failed = true; break; }
		}
		if(test_failpoint == DATA_WRITE_FAIL_RECOVERY_BEFORE_CLEANUP) { close(op_fd); failed = true; break; }
		bool contents_removed = RemoveJournalContents(op_fd, manifest);
		bool op_closed = close(op_fd) == 0;
		if(!contents_removed || !op_closed) { failed = true; break; }
		if(unlinkat(journal_fd, op_id.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(journal_fd) != 0) {
			failed = true; break;
		}
	}
	if(closedir(journal) != 0) failed = true;
	if(!failed) {
		if(fsync(journal_fd) != 0) failed = true;
		if(!failed) {
			if(unlinkat(root.root_fd, ".mgr-journal", AT_REMOVEDIR) != 0) failed = true;
		}
		if(!failed) {
			if(fsync(root.root_fd) != 0) failed = true;
		}
	}
	if(close(journal_fd) != 0) failed = true;
	result.status = failed ? (orphan_unrecognized ? "journal_orphan_unrecognized" : scope_denied ? "scope_denied" : "recovery_conflict") : "recovered";
	return result;
#endif
}

static ValueMap JournalManifestMap(const JournalManifest& manifest)
{
	ValueArray entries;
	for(const JournalRecord& item : manifest.records) {
		ValueMap row;
		row.Add("source_id", item.source_id); row.Add("record_id", item.record_id); row.Add("path", item.rel_path);
		if(manifest.version >= 2) row.Add("action", item.action);
		row.Add("expected_revision", item.expected_revision); row.Add("new_revision", item.new_revision);
		row.Add("backup_ref", item.backup_ref); row.Add("payload_ref", item.payload_ref);
		row.Add("old_hash", item.old_hash); row.Add("new_hash", item.new_hash); row.Add("state", item.state);
		entries.Add(row);
	}
	ValueMap root;
	root.Add("version", manifest.version); root.Add("operation_id", manifest.operation_id); root.Add("source_id", manifest.source_id);
	root.Add("workspace_id", manifest.workspace_id);
	root.Add("state", manifest.state); root.Add("records", entries);
	root.Add("request_digest", manifest.request_digest);
	return root;
}

#ifdef PLATFORM_POSIX
static bool ReadCommitReceipts(int root_fd, Vector<CommitReceipt>& receipts, bool& exists)
{
	exists = false;
	struct stat st;
	if(fstatat(root_fd, ".mgr-commits", &st, AT_SYMLINK_NOFOLLOW) != 0) return errno == ENOENT;
	if(!S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077)) return false;
	int dir = openat(root_fd, ".mgr-commits", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(dir < 0) return false;
	String bytes;
	bool ok = ReadJournalFile(dir, "receipts.json", MAX_COMMIT_RECEIPTS_BYTES, bytes);
	close(dir);
	if(!ok) return false;
	exists = true;
	JsonFidelity fidelity = JsonFidelityScanner(bytes).Scan();
	if(!fidelity.syntax_ok || fidelity.duplicate_key || fidelity.trailing_input || fidelity.numeric_precision || fidelity.escaped_key) return false;
	Value parsed = ParseJSON(bytes);
	if(!parsed.Is<ValueMap>()) return false;
	ValueMap top = parsed;
	if(MapGet(top, "version") != 1) return false;
	Value rows_value = MapGet(top, "receipts");
	if(!rows_value.Is<ValueArray>()) return false;
	ValueArray rows = rows_value;
	if(rows.GetCount() > MAX_COMMIT_RECEIPTS) return false;
	Index<String> seen;
	for(const Value& row_value : rows) {
		if(!row_value.Is<ValueMap>()) return false;
		ValueMap row = row_value;
		CommitReceipt receipt;
		receipt.operation_id = MapGetString(row, "operation_id"); receipt.source_id = MapGetString(row, "source_id");
		receipt.workspace_id = MapGetString(row, "workspace_id"); receipt.digest = MapGetString(row, "request_digest");
		Value ids_value = MapGet(row, "changed_ids"), revisions_value = MapGet(row, "revisions");
		if(receipt.operation_id.IsEmpty() || receipt.operation_id.GetCount() > 64 || !IsSafeSourceId(receipt.source_id) ||
		   receipt.workspace_id.IsEmpty() || receipt.workspace_id.GetCount() > 256 || receipt.digest.GetCount() != 64 ||
		   !ids_value.Is<ValueArray>() || !revisions_value.Is<ValueArray>() || seen.Find(receipt.operation_id) >= 0) return false;
		for(int i = 0; i < receipt.operation_id.GetCount(); i++) {
			char c = receipt.operation_id[i];
			if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
		}
		for(int i = 0; i < receipt.digest.GetCount(); i++)
			if(!((receipt.digest[i] >= '0' && receipt.digest[i] <= '9') || (receipt.digest[i] >= 'a' && receipt.digest[i] <= 'f'))) return false;
		ValueArray id_array = ids_value, revision_array = revisions_value;
		if(id_array.IsEmpty() || id_array.GetCount() > 64 || id_array.GetCount() != revision_array.GetCount()) return false;
		for(int i = 0; i < id_array.GetCount(); i++) {
			if(!IsString(id_array[i]) || !IsString(revision_array[i]) || ((String)id_array[i]).IsEmpty() || ((String)revision_array[i]).IsEmpty()) return false;
			receipt.ids.Add(id_array[i]); receipt.revisions.Add(revision_array[i]);
		}
		seen.Add(receipt.operation_id); receipts.Add(pick(receipt));
	}
	return true;
}

static bool StoreCommitReceipt(int root_fd, const CommitReceipt& receipt, DataWriteTestFailpoint failpoint, bool dry_run)
{
	Vector<CommitReceipt> receipts; bool exists = false;
	if(!ReadCommitReceipts(root_fd, receipts, exists)) return false;
	for(const CommitReceipt& prior : receipts)
			if(prior.operation_id == receipt.operation_id) {
				if(prior.source_id != receipt.source_id || prior.workspace_id != receipt.workspace_id || prior.digest != receipt.digest ||
				   prior.ids.GetCount() != receipt.ids.GetCount() || prior.revisions.GetCount() != receipt.revisions.GetCount()) return false;
				for(int i = 0; i < prior.ids.GetCount(); i++) if(prior.ids[i] != receipt.ids[i] || prior.revisions[i] != receipt.revisions[i]) return false;
				return true;
			}
	if(receipts.GetCount() >= MAX_COMMIT_RECEIPTS) return false;
	struct stat st;
	int dir = -1;
	if(exists || !dry_run) {
		if(!exists && (mkdirat(root_fd, ".mgr-commits", 0700) != 0 || fsync(root_fd) != 0)) return false;
		dir = openat(root_fd, ".mgr-commits", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if(dir < 0) return false;
		if(fstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077)) { close(dir); return false; }
	}
	receipts.Add(receipt);
	ValueArray rows;
	for(const CommitReceipt& item : receipts) {
		ValueArray ids, revisions;
		for(const String& id : item.ids) ids.Add(id);
		for(const String& rev : item.revisions) revisions.Add(rev);
		ValueMap row; row.Add("operation_id", item.operation_id); row.Add("source_id", item.source_id);
		row.Add("workspace_id", item.workspace_id); row.Add("request_digest", item.digest);
		row.Add("changed_ids", ids); row.Add("revisions", revisions); rows.Add(row);
	}
	ValueMap top; top.Add("version", 1); top.Add("receipts", rows);
	String bytes = AsJSON(top, true);
	bool ok = bytes.GetCount() <= MAX_COMMIT_RECEIPTS_BYTES;
	if(ok && !dry_run)
		ok = failpoint != DATA_WRITE_FAIL_BATCH_COMMIT_MANIFEST &&
		     AtomicReplaceRecord(dir, "receipts.json", bytes, DATA_WRITE_FAIL_NONE) == ATOMIC_REPLACE_COMMITTED && fsync(dir) == 0;
	if(dir >= 0 && close(dir) != 0) ok = false;
	return ok;
}
#endif

DataBatchResult DataStore::PublishBatch(const DataWriteAuthorization& authorization,
	                                    const String& operation_id,
	                                    const Vector<DataBatchChange>& changes,
	                                    DataWriteTestFailpoint test_failpoint) const
{
	DataBatchResult result;
	result.operation_id = operation_id;
	result.status = "invalid_request";
	if(source_read_only || !source_write_enabled || !authorization.IsAllowed() || authorization.source_id != source_id ||
	   changes.IsEmpty() || changes.GetCount() > 64 || operation_id.IsEmpty() || operation_id.GetCount() > 64)
		return result;
	if(source_incomplete || source_errors || authorization.workspace_id.IsEmpty()) { result.status = "source_incomplete"; return result; }
	CanonicalWriteGuard write_guard;
#ifndef PLATFORM_POSIX
	result.status = "unsupported_platform";
	return result;
#else
	if(!IsSafeRoot(base_dir) || !IsSafeSourceId(source_id)) { result.status = "source_unavailable"; return result; }
	LockedSourceRoot root;
	if(!root.Open(base_dir)) { result.status = "source_unavailable"; return result; }
	if(synthetic_write_source && (test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_REVOKE_BEFORE_RECEIPT ||
	   test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_REDIRECT_BEFORE_RECEIPT)) {
		DataSourceRegistration revoked = synthetic_registration;
		if(test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_REVOKE_BEFORE_RECEIPT) revoked.write_enabled = false;
		else revoked.root = base_dir + "-redirected";
		SetSyntheticRegistrationForTests(revoked);
	}
	if(!HasCurrentWriteRegistration()) { result.status = "source_capability_changed"; return result; }
	struct stat pending_stat;
	if(fstatat(root.root_fd, ".mgr-journal", &pending_stat, AT_SYMLINK_NOFOLLOW) == 0) {
		result.status = "journal_recovery_required"; return result;
	}
	if(errno != ENOENT) { result.status = "journal_conflict"; return result; }
	for(const DataBatchChange& change : changes) {
		if(change.action != DATA_BATCH_UPDATE && change.action != DATA_BATCH_CREATE) return result;
		if(change.action == DATA_BATCH_CREATE) {
			if(!change.expected_revision.IsEmpty() || !change.expected_source_revision.IsEmpty()) return result;
		}
		else if(change.expected_revision.IsEmpty() || change.expected_source_revision.IsEmpty()) return result;
	}
	DataStore current;
	if(!current.LoadRoot(base_dir, source_id, source_write_enabled, synthetic_write_source) ||
	   current.source_incomplete || current.source_errors || current.base_dir != base_dir || current.source_id != source_id) {
		result.status = "source_incomplete"; return result;
	}
	if(!HasCurrentWriteRegistration()) { result.status = "source_capability_changed"; return result; }
	Vector<String> request_parts;
	for(const DataBatchChange& change : changes) {
		String part = change.qualified_id + "\n" + change.expected_revision + "\n" + change.expected_source_revision + "\n" + AsJSON(change.proposed, true);
		// Keep the legacy update bytes unchanged; distinguish create requests explicitly.
		if(change.action == DATA_BATCH_CREATE) part = "create\n" + part;
		request_parts.Add(part);
	}
	Sort(request_parts);
	String canonical_request;
	for(const String& part : request_parts) canonical_request << part << "\n";
	String request_digest = ContentDigest(canonical_request);
	for(int i = 0; i < operation_id.GetCount(); i++) {
		char c = operation_id[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return result;
	}
	Vector<CommitReceipt> receipts; bool receipt_file_exists = false;
	if(!ReadCommitReceipts(root.root_fd, receipts, receipt_file_exists)) { result.status = "receipt_store_corrupt"; return result; }
	for(const CommitReceipt& receipt : receipts) if(receipt.operation_id == operation_id) {
		if(receipt.source_id != source_id || receipt.workspace_id != authorization.workspace_id || receipt.digest != request_digest) {
			result.status = "idempotency_conflict"; return result;
		}
		result.changed_ids = clone(receipt.ids); result.revisions = clone(receipt.revisions);
		result.status = "already_committed"; return result;
	}
	if(receipts.GetCount() >= MAX_COMMIT_RECEIPTS) { result.status = "receipt_store_full"; return result; }
	Index<String> ids;
	Vector<JournalStageRecord> stages;
	Vector<ValueMap> candidates;
	struct ReferenceSnapshot : Moveable<ReferenceSnapshot> { String qualified_id, rel_path, revision; };
	Vector<ReferenceSnapshot> reference_snapshots;
	for(const DataBatchChange& change : changes) {
		bool create = change.action == DATA_BATCH_CREATE;
		const DataDocument* doc = current.FindDocument(change.qualified_id);
		if(ids.Find(change.qualified_id) >= 0) { result.status = "identity_conflict"; return result; }
		if(create) {
			if(doc || !change.qualified_id.StartsWith(authorization.workspace_id + ":")) { result.status = "identity_conflict"; return result; }
		}
		else if(!doc || !doc->valid || !doc->validated || doc->source_id != source_id ||
		        doc->workspace_id != authorization.workspace_id || doc->revision != change.expected_revision ||
		        doc->source_revision != change.expected_source_revision) {
			result.status = "prevalidation_failed"; return result;
		}
		ids.Add(change.qualified_id);
		ValueMap candidate = change.proposed;
		Value env_value = MapGet(candidate, "envelope");
		if(!env_value.Is<ValueMap>()) { result.status = "identity_mismatch"; return result; }
		ValueMap env = env_value;
		String id = MapGetString(candidate, "id"), kind = MapGetString(candidate, "kind");
		if(id.IsEmpty() || kind.IsEmpty() ||
		   (create ? (change.qualified_id != authorization.workspace_id + ":" + id ||
		              MapGetString(env, "workspace_id") != authorization.workspace_id ||
		              MapGetString(env, "stable_id") != id || MapGetString(env, "record_kind") != kind ||
		              MapGetString(env, "revision") != "1") :
		             (id != doc->id || kind != doc->kind || MapGetString(env, "workspace_id") != doc->workspace_id ||
		              MapGetString(env, "stable_id") != doc->id || MapGetString(env, "record_kind") != doc->kind ||
		              MapGetString(env, "revision") != doc->revision))) {
			result.status = "identity_mismatch"; return result;
		}
		String next_revision;
		if(create) next_revision = "1";
		else {
			int64 rev = 0;
			for(int i = 0; i < doc->revision.GetCount(); i++) {
				char c = doc->revision[i];
				if(c < '0' || c > '9' || rev > (INT64_MAX - (c - '0')) / 10) { result.status = "invalid_revision"; return result; }
				rev = rev * 10 + c - '0';
			}
			if(rev == INT64_MAX) { result.status = "invalid_revision"; return result; }
			next_revision = AsString(rev + 1);
		}
		env.Set("revision", next_revision);
		candidate.Set("envelope", env);
		DataDocument proposed;
		proposed.data = candidate; proposed.id = id; proposed.kind = kind;
		current.ValidateDocument(proposed, current.FindSchemaCard(kind));
		if(!proposed.valid || !proposed.validated) { result.status = "invalid_schema"; return result; }
		JournalStageRecord stage;
		stage.action = create ? "create" : "update";
		stage.qualified_id = change.qualified_id;
		stage.rel_path = create ? CreateJournalPath(change.qualified_id) : doc->rel_path;
		stage.expected_revision = create ? String() : doc->revision; stage.new_revision = next_revision;
		if(!create) {
			int old_parent = -1; String old_leaf, old_bytes;
			if(!OpenRecordParent(root.root_fd, doc->rel_path, old_parent, old_leaf) || !ReadRecordAt(old_parent, old_leaf, old_bytes)) {
				if(old_parent >= 0) close(old_parent);
				result.status = "conflict"; return result;
			}
			close(old_parent);
			if(RecordSourceRevision(doc->rel_path, old_bytes) != change.expected_source_revision) { result.status = "conflict"; return result; }
			stage.old_payload = old_bytes;
		}
		stage.new_payload = AsJSON(candidate, true);
		stages.Add(stage); candidates.Add(candidate);
	}
	// Resolve each reference against the projected set; retain outside targets for
	// exact lock-time rechecking after journal preparation.
	for(int i = 0; i < stages.GetCount(); i++) {
		const SchemaCard* schema = current.FindSchemaCard(MapGetString(candidates[i], "kind"));
		if(!schema) { result.status = "invalid_schema"; return result; }
		for(const SchemaCard::Field& field : schema->fields) {
			if(field.reference_kind.IsEmpty()) continue;
			Value value;
			if(!GetTypedField(candidates[i], field.name, value) || !IsString(value)) { result.status = "invalid_reference"; return result; }
			int matches = 0; String target_kind; bool target_valid = false;
			for(int n = 0; n < candidates.GetCount(); n++) {
				if(n == i || stages[n].action != "create" || MapGetString(candidates[n], "id") != (String)value) continue;
				if(MapGetString(MapGet(candidates[n], "envelope"), "workspace_id") == authorization.workspace_id) {
					matches++; target_kind = MapGetString(candidates[n], "kind"); target_valid = true;
				}
			}
			for(const DataDocument& existing : current.docs) {
				bool replaced = false;
				for(int n = 0; n < stages.GetCount(); n++) if(stages[n].qualified_id == existing.qualified_id) {
					replaced = true;
					if(existing.workspace_id == authorization.workspace_id && MapGetString(candidates[n], "id") == (String)value) {
						matches++; target_kind = MapGetString(candidates[n], "kind"); target_valid = true;
					}
					break;
				}
				if(!replaced && existing.workspace_id == authorization.workspace_id && existing.id == (String)value) {
					matches++; target_kind = existing.kind;
					target_valid = existing.valid && existing.validated && existing.validation_state == "valid";
				}
			}
			if(matches != 1 || target_kind != field.reference_kind || !target_valid) { result.status = "invalid_reference"; return result; }
			for(const DataDocument& existing : current.docs) {
				bool in_batch = false;
				for(const JournalStageRecord& stage : stages) if(stage.qualified_id == existing.qualified_id) in_batch = true;
				if(!in_batch && existing.workspace_id == authorization.workspace_id && existing.id == (String)value) {
					bool found = false;
					for(const ReferenceSnapshot& snapshot : reference_snapshots) if(snapshot.qualified_id == existing.qualified_id) found = true;
					if(!found) { ReferenceSnapshot snapshot; snapshot.qualified_id = existing.qualified_id; snapshot.rel_path = existing.rel_path; snapshot.revision = existing.source_revision; reference_snapshots.Add(snapshot); }
				}
			}
		}
	}
	CommitReceipt capacity_receipt;
	capacity_receipt.operation_id = operation_id; capacity_receipt.source_id = source_id;
	capacity_receipt.workspace_id = authorization.workspace_id; capacity_receipt.digest = request_digest;
	for(const JournalStageRecord& stage : stages) { capacity_receipt.ids.Add(stage.qualified_id); capacity_receipt.revisions.Add(stage.new_revision); }
	if(!StoreCommitReceipt(root.root_fd, capacity_receipt, DATA_WRITE_FAIL_NONE, true)) {
		result.status = "receipt_store_full"; return result;
	}
	bool has_create = false;
	for(const JournalStageRecord& stage : stages) if(stage.action == "create") has_create = true;
	if(has_create && !EnsureCreateRecordDirectory(root.root_fd)) { result.status = "destination_unavailable"; return result; }
	for(const JournalStageRecord& stage : stages) if(stage.action == "create") {
		bool identity_collision = false;
		int entries_scanned = 0; int64 bytes_scanned = 0;
		String record_id = stage.qualified_id.Mid(authorization.workspace_id.GetCount() + 1);
		if(!HasConflictingCreateIdentity(root.root_fd, "", 0, authorization.workspace_id, record_id, stage.rel_path,
		                                 identity_collision, entries_scanned, bytes_scanned)) {
			result.status = "identity_scan_unavailable"; return result;
		}
		int parent = -1; String leaf;
		if(identity_collision || !OpenRecordParent(root.root_fd, stage.rel_path, parent, leaf)) {
			if(parent >= 0) close(parent);
			result.status = "identity_conflict"; return result;
		}
		struct stat existing_target;
		bool absent = fstatat(parent, leaf.ToStd().c_str(), &existing_target, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
		close(parent);
		if(!absent) { result.status = "identity_conflict"; return result; }
	}
	JournalStageResult prepared = current.PrepareJournalLocked(authorization, operation_id, stages, root.root_fd, test_failpoint);
	if(prepared.status != "prepared") { result.status = prepared.status; return result; }
	if(synthetic_write_source && (test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_CHANGE_REFERENCE ||
	   test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_DELETE_REFERENCE) && !reference_snapshots.IsEmpty()) {
		String path = AppendFileName(base_dir, reference_snapshots[0].rel_path);
		if(test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_DELETE_REFERENCE) FileDelete(path);
		else SaveFile(path, LoadFile(path) + " ");
	}
	if(synthetic_write_source && test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_CHANGE_RECORD && !stages.IsEmpty())
		SaveFile(AppendFileName(base_dir, stages[0].rel_path), stages[0].old_payload + " ");
	int jfd = openat(root.root_fd, ".mgr-journal", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	int ofd = jfd >= 0 ? openat(jfd, operation_id.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
	String manifest_bytes; JournalManifest manifest;
	if(ofd < 0 || !ReadJournalFile(ofd, "manifest.json", 65536, manifest_bytes) || !ParseJournalManifest(manifest_bytes, manifest)) {
		if(ofd >= 0) close(ofd); if(jfd >= 0) close(jfd); result.status = "recovery_required"; return result;
	}
	manifest.request_digest = request_digest;
	auto resolve_conflict = [&]() -> DataBatchResult {
		close(ofd); close(jfd);
		result.status = DiscardPreparedJournalLocked(root.root_fd, source_id, authorization.workspace_id, operation_id) ? "conflict" : "recovery_required";
		return pick(result);
	};
	for(const JournalStageRecord& stage : stages) {
		int parent = -1; String leaf, actual;
		if(stage.action == "create") {
			if(!OpenRecordParent(root.root_fd, stage.rel_path, parent, leaf)) return resolve_conflict();
			struct stat target_stat;
			bool absent = fstatat(parent, leaf.ToStd().c_str(), &target_stat, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
			close(parent);
			if(!absent) return resolve_conflict();
		}
		else if(!OpenRecordParent(root.root_fd, stage.rel_path, parent, leaf) || !ReadRecordAt(parent, leaf, actual) || actual != stage.old_payload) {
			if(parent >= 0) close(parent); return resolve_conflict();
		}
		else close(parent);
	}
	for(const ReferenceSnapshot& snapshot : reference_snapshots) {
		int parent = -1; String leaf, actual;
		if(!OpenRecordParent(root.root_fd, snapshot.rel_path, parent, leaf) || !ReadRecordAt(parent, leaf, actual) ||
		   RecordSourceRevision(snapshot.rel_path, actual) != snapshot.revision) {
			if(parent >= 0) close(parent); return resolve_conflict();
		}
		close(parent);
	}
	if(synthetic_write_source && test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_REGISTRATION_CHANGE) {
		DataSourceRegistration revoked = synthetic_registration;
		revoked.root = base_dir + "-redirected-at-commit";
		SetSyntheticRegistrationForTests(revoked);
	}
	if(!HasCurrentWriteRegistration()) {
		close(ofd); close(jfd); result.status = "source_capability_changed_recovery_required"; return result;
	}
	manifest.state = "committing";
	String bytes = AsJSON(JournalManifestMap(manifest), true);
	if(AtomicReplaceRecord(ofd, "manifest.json", bytes, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) {
		close(ofd); close(jfd); result.status = "uncertain"; return result;
	}
	for(int i = 0; i < stages.GetCount(); i++) {
		if(test_failpoint == DATA_WRITE_FAIL_BATCH_BEFORE_REPLACE && i == 0) { close(ofd); close(jfd); result.status = "uncommitted"; return result; }
		int parent = -1; String leaf;
		if(!OpenRecordParent(root.root_fd, stages[i].rel_path, parent, leaf)) { close(ofd); close(jfd); result.status = "uncertain"; return result; }
		AtomicReplaceOutcome applied = stages[i].action == "create" ? AtomicCreateRecord(parent, leaf, stages[i].new_payload) :
		                              AtomicReplaceRecord(parent, leaf, stages[i].new_payload, DATA_WRITE_FAIL_NONE);
		close(parent);
		if(applied != ATOMIC_REPLACE_COMMITTED) { close(ofd); close(jfd); result.status = "uncertain"; return result; }
		manifest.records[i].state = "applied";
		bytes = AsJSON(JournalManifestMap(manifest), true);
		if(AtomicReplaceRecord(ofd, "manifest.json", bytes, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) {
			close(ofd); close(jfd); result.status = "uncertain"; return result;
		}
		if(test_failpoint == DATA_WRITE_FAIL_BATCH_AFTER_REPLACE && i == 0) { close(ofd); close(jfd); result.status = "uncommitted"; return result; }
	}
	manifest.state = "committed";
	bytes = AsJSON(JournalManifestMap(manifest), true);
	if(test_failpoint == DATA_WRITE_FAIL_BATCH_COMMIT_MANIFEST || AtomicReplaceRecord(ofd, "manifest.json", bytes, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) {
		close(ofd); close(jfd); result.status = "uncertain"; return result;
	}
	for(const JournalStageRecord& stage : stages) { result.changed_ids.Add(stage.qualified_id); result.revisions.Add(stage.new_revision); }
	CommitReceipt receipt;
	receipt.operation_id = operation_id; receipt.source_id = source_id; receipt.workspace_id = authorization.workspace_id; receipt.digest = request_digest;
	receipt.ids = clone(result.changed_ids); receipt.revisions = clone(result.revisions);
	if(!StoreCommitReceipt(root.root_fd, receipt, DATA_WRITE_FAIL_NONE)) {
		close(ofd); close(jfd); result.status = "receipt_persist_failed_recovery_required"; return result;
	}
	close(ofd); close(jfd);
	result.status = "committed";
	return result;
#endif
}

DataWriteResult DataStore::WriteRecord(const DataWriteAuthorization& authorization,
	                                   const String& qualified_id,
	                                   const String& expected_record_revision,
					const String& expected_source_revision,
								   const String& idempotency_key,
								   const ValueMap& proposed,
								   DataWriteTestFailpoint test_failpoint)
{
	DataWriteResult result;
	result.status = "invalid_request";
	if(source_read_only || !source_write_enabled || base_dir.IsEmpty() || source_id.IsEmpty()) { result.status = "read_only"; return result; }
	if(!authorization.IsAllowed() || authorization.principal.GetCount() > 128 ||
	   qualified_id.IsEmpty() || expected_record_revision.IsEmpty() ||
	   expected_source_revision.IsEmpty() || idempotency_key.IsEmpty() || idempotency_key.GetCount() > 128)
		return result;
	for(int i = 0; i < authorization.principal.GetCount(); i++)
		if((byte)authorization.principal[i] < 0x21 || (byte)authorization.principal[i] > 0x7e) return result;
	for(int i = 0; i < idempotency_key.GetCount(); i++)
		if((byte)idempotency_key[i] < 0x21 || (byte)idempotency_key[i] > 0x7e) return result;
	if(!IsSafeRoot(base_dir) || !IsSafeSourceId(source_id)) { result.status = "source_unavailable"; return result; }
	if(authorization.source_id != source_id) { result.status = "scope_denied"; return result; }

	Mutex::Lock lock(s_canonical_write_mutex);
	#ifndef PLATFORM_POSIX
	result.status = "unsupported_platform";
	return result;
	#else
	LockedSourceRoot locked_root;
	if(!locked_root.Open(base_dir)) { result.status = "source_unavailable"; return result; }
	if(!synthetic_write_source) {
		DataSourceRegistration authoritative;
		if(!ReadConfiguredRegistration(authoritative) || !authoritative.enabled || authoritative.read_only ||
		   !authoritative.write_enabled || NormalizePath(authoritative.root) != base_dir || authoritative.id != source_id) {
			result.status = "source_capability_changed";
			return result;
		}
	}
	DataStore current;
	if(!current.LoadRoot(base_dir, source_id)) { result.status = "source_unavailable"; return result; }
	const DataDocument* doc = current.FindDocument(qualified_id);
	if(!doc || !doc->valid || !doc->validated || doc->source_id != source_id) { result.status = "identity_or_schema_invalid"; return result; }
	if(authorization.workspace_id != doc->workspace_id) { result.status = "scope_denied"; return result; }
	int parent_fd = -1;
	String leaf;
	if(!OpenRecordParent(locked_root.root_fd, doc->rel_path, parent_fd, leaf)) { result.status = "identity_or_schema_invalid"; return result; }
	struct ParentHandleGuard { int fd; ~ParentHandleGuard() { if(fd >= 0) close(fd); } } parent_guard = { parent_fd };
	String old_bytes;
	if(!ReadRecordAt(parent_fd, leaf, old_bytes)) { result.status = "identity_or_schema_invalid"; return result; }
	ValueMap candidate = proposed;
	Value envelope_value = MapGet(candidate, "envelope");
	if(!envelope_value.Is<ValueMap>()) { result.status = "invalid_schema"; return result; }
	ValueMap envelope = envelope_value;
	if(MapGetString(candidate, "id") != doc->id || MapGetString(candidate, "kind") != doc->kind ||
	   MapGetString(envelope, "workspace_id") != doc->workspace_id || MapGetString(envelope, "stable_id") != doc->id ||
	   MapGetString(envelope, "record_kind") != doc->kind) { result.status = "identity_mismatch"; return result; }
	ValueMap current_envelope = MapGet(doc->data, "envelope");
	String idem = MapGetString(current_envelope, "last_write_key");
	if(idem == idempotency_key && MapGetString(current_envelope, "last_write_actor") == authorization.principal) {
		if(MapGetString(current_envelope, "last_write_expected_revision") != expected_record_revision ||
		   MapGetString(current_envelope, "last_write_expected_source_revision") != expected_source_revision ||
		   MapGetString(current_envelope, "last_write_request_digest") != ContentDigest(AsJSON(proposed, true))) {
			result.status = "idempotency_conflict";
			return result;
		}
		ValueMap expected_retry = proposed;
		ValueMap retry_envelope = MapGet(expected_retry, "envelope");
		retry_envelope.Set("revision", MapGetString(current_envelope, "revision"));
		retry_envelope.Set("last_write_key", idempotency_key);
		retry_envelope.Set("last_write_actor", authorization.principal);
		retry_envelope.Set("last_write_expected_revision", expected_record_revision);
		retry_envelope.Set("last_write_expected_source_revision", expected_source_revision);
		retry_envelope.Set("last_write_request_digest", ContentDigest(AsJSON(proposed, true)));
		expected_retry.Set("envelope", retry_envelope);
		if(AsJSON(expected_retry, true) != AsJSON(doc->data, true)) {
			result.status = "idempotency_conflict";
			return result;
		}
		result.status = "already_committed";
		result.revision = MapGetString(current_envelope, "revision");
		result.source_revision = RecordSourceRevision(doc->rel_path, old_bytes);
		return result;
	}
	if(old_bytes.IsEmpty() || RecordSourceRevision(doc->rel_path, old_bytes) != expected_source_revision || doc->source_revision != expected_source_revision ||
	   MapGetString(current_envelope, "revision") != expected_record_revision) { result.status = "conflict"; return result; }
	if(MapGetString(envelope, "revision") != expected_record_revision) { result.status = "conflict"; return result; }
	int64 revision_number = 0;
	for(int i = 0; i < expected_record_revision.GetCount(); i++) {
		char c = expected_record_revision[i];
		if(c < '0' || c > '9' || revision_number > (INT64_MAX - (c - '0')) / 10) {
			result.status = "invalid_request";
			return result;
		}
		revision_number = revision_number * 10 + c - '0';
	}
	if(revision_number == INT64_MAX) { result.status = "invalid_request"; return result; }
	String next_revision = AsString(revision_number + 1);
	envelope.Set("revision", next_revision);
	envelope.Set("last_write_key", idempotency_key);
	envelope.Set("last_write_actor", authorization.principal);
	envelope.Set("last_write_expected_revision", expected_record_revision);
	envelope.Set("last_write_expected_source_revision", expected_source_revision);
	envelope.Set("last_write_request_digest", ContentDigest(AsJSON(proposed, true)));
	candidate.Set("envelope", envelope);

	DataDocument proposed_doc;
	proposed_doc.data = candidate;
	proposed_doc.kind = doc->kind;
	proposed_doc.id = doc->id;
	proposed_doc.schema_id = MapGetString(envelope, "schema_id");
	proposed_doc.schema_version = MapGetString(envelope, "schema_version");
	current.ValidateDocument(proposed_doc, current.FindSchemaCard(doc->kind));
	if(!proposed_doc.valid || !proposed_doc.validated) { result.status = "invalid_schema"; return result; }
	const SchemaCard* schema = current.FindSchemaCard(doc->kind);
	if(schema) {
		for(const SchemaCard::Field& field : schema->fields) {
			if(field.reference_kind.IsEmpty()) continue;
			Value reference;
			if(!GetTypedField(candidate, field.name, reference) || !IsString(reference)) {
				result.status = "invalid_reference";
				return result;
			}
			int matches = 0;
			const DataDocument* target = NULL;
			for(const DataDocument& item : current.docs) {
				if(&item == doc) continue;
				if(item.workspace_id == proposed_doc.workspace_id && item.id == (String)reference) {
					matches++;
					target = &item;
				}
			}
			if(matches != 1 || !target || !target->valid || target->validation_state != "valid" || target->kind != field.reference_kind) {
				result.status = "invalid_reference";
				return result;
			}
		}
	}
	String bytes = AsJSON(candidate, true);
	// Recheck bytes and idempotency at the commit boundary after validation/serialization.
	String latest;
	if(!ReadRecordAt(parent_fd, leaf, latest)) { result.status = "conflict"; return result; }
	if(latest != old_bytes || RecordSourceRevision(doc->rel_path, latest) != expected_source_revision) { result.status = "conflict"; return result; }
	AtomicReplaceOutcome replacement = AtomicReplaceRecord(parent_fd, leaf, bytes, test_failpoint);
	if(replacement == ATOMIC_REPLACE_FAILED) {
		result.status = test_failpoint == DATA_WRITE_FAIL_TEMP_CREATE || test_failpoint == DATA_WRITE_FAIL_TEMP_WRITE ? "temp_write_failed" : "replacement_failed";
		return result;
	}
	result.status = replacement == ATOMIC_REPLACE_COMMITTED ? "committed" : "committed_durability_uncertain";
	result.revision = next_revision;
	result.source_revision = RecordSourceRevision(doc->rel_path, bytes);
	return result;
	#endif
}

END_UPP_NAMESPACE
