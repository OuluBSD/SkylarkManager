#include "ManagerSkylark.h"
#include "ManagerSkylarkSeed.h"
#include "SystemAuth.h"
#include "AssessmentViews.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include "HostPlatform.h"

NAMESPACE_UPP

ValueMap MakeDemoCandidate(const String& id, const String& source_identity)
{
	ValueMap candidate;
	candidate.Add("id", id);
	candidate.Add("kind", "local-candidate");
	candidate.Add("workspace_id", "demo-workspace");
	candidate.Add("schema_id", "skylark/local-candidate");
	candidate.Add("schema_version", "1");
	candidate.Add("origin_kind", "locally-authored");
	candidate.Add("owner_kind", "local-author");
	candidate.Add("provenance", "synthetic-demo");
	candidate.Add("uncertainty", "synthetic example only");
	candidate.Add("candidate_kind", "candidate");
	if(!source_identity.IsEmpty()) candidate.Add("source_identity", source_identity);
	candidate.Add("relationships", ValueArray());
	ValueMap envelope;
	envelope.Add("schema_id", "skylark/local-candidate");
	envelope.Add("schema_version", "1");
	envelope.Add("workspace_id", "demo-workspace");
	envelope.Add("stable_id", id);
	envelope.Add("record_kind", "local-candidate");
	envelope.Add("revision", "1");
	envelope.Add("visibility", "private");
	envelope.Add("provenance_ref", "synthetic-demo");
	candidate.Add("envelope", envelope);
	return candidate;
}

ValueMap MakeDemoEnvelope(const String& kind, const String& workspace, const String& id,
	                              const String& visibility, const String& provenance, const String& version)
{
	ValueMap envelope;
	envelope.Add("schema_id", "skylark/" + kind);
	envelope.Add("schema_version", version);
	envelope.Add("workspace_id", workspace);
	envelope.Add("stable_id", id);
	envelope.Add("record_kind", kind);
	envelope.Add("revision", "1");
	envelope.Add("visibility", visibility);
	envelope.Add("provenance_ref", provenance);
	return envelope;
}

static ValueMap MakeDemoPolicyIdentity(const String& workspace, const String& object)
{
	ValueMap identity;
	identity.Add("workspace_id", workspace);
	identity.Add("object_id", object);
	return identity;
}

ValueMap MakeDemoPolicyConfig()
{
	ValueMap instance;
	instance.Add("configured", true);
	instance.Add("remote_enabled", false);
	instance.Add("export_enabled", false);
	instance.Add("review_granularity", "record");
	instance.Add("secret_reference", "");
	ValueArray workspaces, sources, providers, ancestry, protected_ancestry;
	workspaces.Add(MakeDemoPolicyIdentity("demo-workspace", "demo-workspace"));
	ValueMap source;
	source.Add("workspace", MakeDemoPolicyIdentity("demo-workspace", "demo-workspace"));
	source.Add("identity", MakeDemoPolicyIdentity("demo-workspace", "demo-source"));
	source.Add("classification", "public"); source.Add("visible", true); sources.Add(source);
	ValueMap provider;
	provider.Add("workspace", MakeDemoPolicyIdentity("demo-workspace", "demo-workspace"));
	provider.Add("identity", MakeDemoPolicyIdentity("demo-workspace", "offline-demo"));
	provider.Add("training_policy", "ineligible"); provider.Add("enabled", false);
	provider.Add("config_revision", ""); provider.Add("secret_reference", ""); providers.Add(provider);
	ValueMap registry;
	registry.Add("version", "trusted-policy-v2"); registry.Add("workspaces", workspaces); registry.Add("sources", sources);
	registry.Add("ancestry", ancestry); registry.Add("providers", providers); registry.Add("protected_ancestry", protected_ancestry);
	ValueMap config; config.Add("instance", instance); config.Add("registry", registry);
	return config;
}

bool SeedSyntheticAssessmentFiles(const String& site_dir, const String& workspace, const String& candidate, const String& evidence_id)
{
	ValueMap rubric;
	rubric.Add("id", "synthetic-rubric"); rubric.Add("workspace_id", workspace); rubric.Add("revision", "r1");
	rubric.Add("score_scale", "integer-0-10"); rubric.Add("total_rule", "weighted-mean");
	ValueArray dimensions, coverage, tiers;
	for(int i = 0; i < 6; i++) {
		String id = Format("dimension-%d", i + 1);
		ValueMap dimension;
		dimension.Add("id", id); dimension.Add("weight", i + 1); dimension.Add("minimum_score", 0);
		dimension.Add("maximum_score", 10); dimension.Add("minimum_evidence_count", 1);
		dimensions.Add(dimension); coverage.Add(id);
	}
	rubric.Add("dimensions", dimensions); rubric.Add("coverage_required", coverage);
	ValueMap low; low.Add("id", "developing"); low.Add("minimum_numerator", 0); low.Add("minimum_denominator", 1); low.Add("maximum_numerator", 5); low.Add("maximum_denominator", 1); tiers.Add(low);
	ValueMap high; high.Add("id", "promising"); high.Add("minimum_numerator", 5); high.Add("minimum_denominator", 1); high.Add("maximum_numerator", 10); high.Add("maximum_denominator", 1); tiers.Add(high);
	rubric.Add("tiers", tiers);
	ValueMap inputs; inputs.Add("workspace_id", workspace);
	ValueArray scores;
	for(int i = 0; i < 6; i++) {
		ValueMap score; score.Add("workspace_id", workspace); score.Add("candidate_id", candidate);
		score.Add("dimension_id", Format("dimension-%d", i + 1)); score.Add("score", "5");
		score.Add("provenance", "computed"); ValueArray refs; refs.Add(evidence_id); score.Add("evidence_ids", refs);
		scores.Add(score);
	}
	inputs.Add("scores", scores); inputs.Add("relationships", ValueArray());
	return SaveFile(AppendFileName(site_dir, "assessment-rubric.json"), AsJSON(rubric, true)) &&
	       SaveFile(AppendFileName(site_dir, "assessment-inputs.json"), AsJSON(inputs, true));
}

bool ManagerSkylarkApp::SeedSyntheticCandidateWorkflow(const String& data_root, const String& workspace_id,
	const String& candidate_id, const String& source_identity)
{
	if(data_root.IsEmpty() || workspace_id.IsEmpty() || candidate_id.IsEmpty() || source_identity.IsEmpty()) return false;
	String provenance = "synthetic-demo";
	String snapshot_id = "demo-evidence-snapshot";
	String evidence_id = "demo-evidence-item";
	String proposal_id = "demo-proposal";
	String revision_id = "demo-proposal-revision";
	String operation_id = "demo-initial-stage";
	String stage_event_id = "candidate-stage-event-" + DataStore::ContentDigest(operation_id + "\n" + candidate_id + "\n" + revision_id + "\ndraft").Left(32);
	String proposal_head_id = "candidate-proposal-head-" + DataStore::ContentDigest(workspace_id + "\n" + proposal_id).Left(32);
	String stage_head_id = "candidate-stage-head-" + DataStore::ContentDigest(workspace_id + "\n" + candidate_id + "\n" + revision_id).Left(32);
	String digest('a', 64);
	ValueMap locator;
	locator.Add("type", "paper-section");
	locator.Add("id", "section-1");
	locator.Add("section", "1");

	ValueMap snapshot;
	snapshot.Add("id", snapshot_id); snapshot.Add("kind", "evidence-snapshot"); snapshot.Add("workspace_id", workspace_id);
	snapshot.Add("source_kind", "paper"); snapshot.Add("source_identity", source_identity); snapshot.Add("upstream_revision", "demo-upstream-1");
	snapshot.Add("content_sha256", digest); snapshot.Add("captured_at", "01/01/2024 00:00:00"); snapshot.Add("source_at", "01/01/2024 00:00:00");
	snapshot.Add("collector", "synthetic-demo"); snapshot.Add("locator", locator); snapshot.Add("resolution", "resolved");
	snapshot.Add("content", "Synthetic evidence content for a local demo workflow."); snapshot.Add("classification", "public");
	snapshot.Add("visibility", "visible"); snapshot.Add("retracted", false); snapshot.Add("predecessor", "none");
	ValueArray contributors; contributors.Add("synthetic-demo"); snapshot.Add("contributors", contributors);
	snapshot.Add("envelope", MakeDemoEnvelope("evidence-snapshot", workspace_id, snapshot_id, "private", provenance, "2"));
	if(!SaveFile(AppendFileName(data_root, "demo-evidence-snapshot.json"), AsJSON(snapshot, true))) return false;

	ValueMap evidence;
	evidence.Add("id", evidence_id); evidence.Add("kind", "evidence-item"); evidence.Add("workspace_id", workspace_id); evidence.Add("snapshot", snapshot_id);
	evidence.Add("source_identity", source_identity); evidence.Add("upstream_revision", "demo-upstream-1"); evidence.Add("content_sha256", digest);
	evidence.Add("locator", locator); evidence.Add("resolution", "resolved"); evidence.Add("excerpt", "Synthetic evidence excerpt.");
	evidence.Add("contributors", contributors); ValueArray source_lines; source_lines.Add(1); evidence.Add("source_lines", source_lines);
	evidence.Add("classification", "public"); evidence.Add("visibility", "visible");
	evidence.Add("envelope", MakeDemoEnvelope("evidence-item", workspace_id, evidence_id, "private", provenance, "2"));
	if(!SaveFile(AppendFileName(data_root, "demo-evidence-item.json"), AsJSON(evidence, true))) return false;

	ValueMap revision;
	revision.Add("id", revision_id); revision.Add("kind", "proposal-revision"); revision.Add("workspace_id", workspace_id);
	revision.Add("schema_id", "skylark/proposal-revision"); revision.Add("schema_version", "1"); revision.Add("origin_kind", "locally-authored");
	revision.Add("owner_kind", "local-author"); revision.Add("provenance", provenance); revision.Add("uncertainty", "unassessed");
	revision.Add("submission_state", "not-submitted"); revision.Add("approval_state", "not-approved"); revision.Add("author_state", "known-local-author");
	revision.Add("intent_state", "unknown"); revision.Add("hypothetical", false); revision.Add("revision", "draft");
	revision.Add("proposal_id", proposal_id); revision.Add("revision_id", "r1"); revision.Add("candidate_id", candidate_id);
	revision.Add("relationships", ValueArray()); revision.Add("envelope", MakeDemoEnvelope("proposal-revision", workspace_id, revision_id, "private", provenance));
	if(!SaveFile(AppendFileName(data_root, "demo-proposal-revision.json"), AsJSON(revision, true))) return false;

	ValueArray evidence_links; evidence_links.Add(evidence_id);
	ValueMap form;
	form.Add("id", "demo-proposal-form"); form.Add("kind", "candidate-proposal"); form.Add("workspace_id", workspace_id); form.Add("revision", "1");
	form.Add("provenance", provenance); form.Add("candidate_id", candidate_id); form.Add("proposal_revision_id", revision_id); form.Add("candidate_kind", "candidate");
	form.Add("source_identity", source_identity); form.Add("title", "Synthetic local proposal"); form.Add("purpose", "Demonstrate a read-only proposal projection.");
	form.Add("quality_contract", "No external submission or approval."); form.Add("inputs", "Synthetic evidence only."); form.Add("outputs", "Read-only status view.");
	form.Add("runtime_resources", "None."); form.Add("assets", "None."); form.Add("verification_plan", "Synthetic tests only.");
	form.Add("evidence_links", evidence_links); form.Add("draft_state", "draft"); form.Add("submission_state", "not-submitted");
	form.Add("approval_state", "not-approved"); form.Add("author_state", "unknown"); form.Add("intent_state", "unknown"); form.Add("hypothetical", false);
	form.Add("envelope", MakeDemoEnvelope("candidate-proposal", workspace_id, "demo-proposal-form", "private", provenance));
	if(!SaveFile(AppendFileName(data_root, "demo-candidate-proposal.json"), AsJSON(form, true))) return false;

	ValueMap proposal_head;
	proposal_head.Add("id", proposal_head_id); proposal_head.Add("kind", "candidate-proposal-head"); proposal_head.Add("workspace_id", workspace_id);
	proposal_head.Add("revision", "1"); proposal_head.Add("provenance", provenance); proposal_head.Add("candidate_id", candidate_id);
	proposal_head.Add("proposal_id", proposal_id); proposal_head.Add("proposal_revision_id", revision_id); proposal_head.Add("revision_id", "r1");
	proposal_head.Add("envelope", MakeDemoEnvelope("candidate-proposal-head", workspace_id, proposal_head_id, "private", provenance));
	if(!SaveFile(AppendFileName(data_root, "demo-candidate-proposal-head.json"), AsJSON(proposal_head, true))) return false;

	ValueMap stage_event;
	stage_event.Add("id", stage_event_id); stage_event.Add("kind", "candidate-stage-event"); stage_event.Add("workspace_id", workspace_id);
	stage_event.Add("revision", "1"); stage_event.Add("provenance", provenance); stage_event.Add("candidate_id", candidate_id);
	stage_event.Add("candidate_revision", "1"); stage_event.Add("proposal_revision_id", revision_id); stage_event.Add("stage", "draft");
	stage_event.Add("previous_event_id", "none"); stage_event.Add("reason", "Initial synthetic draft."); stage_event.Add("evidence_ids", ValueArray());
	stage_event.Add("operation_id", operation_id); stage_event.Add("actor", "demo"); stage_event.Add("authority", "authenticated-admin");
	stage_event.Add("timestamp", "01/01/2024 00:00:00");
	stage_event.Add("envelope", MakeDemoEnvelope("candidate-stage-event", workspace_id, stage_event_id, "private", provenance));
	if(!SaveFile(AppendFileName(data_root, "demo-candidate-stage-event.json"), AsJSON(stage_event, true))) return false;

	ValueMap stage_head;
	stage_head.Add("id", stage_head_id); stage_head.Add("kind", "candidate-stage-head"); stage_head.Add("workspace_id", workspace_id);
	stage_head.Add("revision", "1"); stage_head.Add("provenance", provenance); stage_head.Add("candidate_id", candidate_id);
	stage_head.Add("proposal_revision_id", revision_id); stage_head.Add("stage", "draft"); stage_head.Add("event_id", stage_event_id);
	stage_head.Add("envelope", MakeDemoEnvelope("candidate-stage-head", workspace_id, stage_head_id, "private", provenance));
	return SaveFile(AppendFileName(data_root, "demo-candidate-stage-head.json"), AsJSON(stage_head, true));
}

bool ManagerSkylarkApp::SeedSyntheticReviewData(const String& data_root, const String& workspace_id, const String& source_identity)
{
	if(data_root.IsEmpty() || workspace_id.IsEmpty() || source_identity.IsEmpty()) return false;
	String provenance = "synthetic-demo";
	String package_id = "demo-package", revision_id = "demo-review-revision", thread_id = "demo-review-thread";
	String patch = "diff --git a/src/synthetic.cpp b/src/synthetic.cpp\nindex 1111111..2222222 100644\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n@@ -1 +1 @@\n-old\n+new\n";
	String patch_hash = DataStore::ContentDigest(patch);

	ValueMap goal;
	goal.Add("id", "demo-goal"); goal.Add("kind", "goal"); goal.Add("title", "Synthetic Goal");
	goal.Add("envelope", MakeDemoEnvelope("goal", workspace_id, "demo-goal", "public", provenance));
	if(!SaveFile(AppendFileName(data_root, "demo-goal.json"), AsJSON(goal, true))) return false;

	ValueMap topology;
	topology.Add("id", "demo-topology"); topology.Add("kind", "topology"); topology.Add("name", "Synthetic Topology");
	topology.Add("workspace_id", workspace_id); topology.Add("visibility", "public"); topology.Add("classification", "public");
	ValueMap node; node.Add("id", "demo-node"); node.Add("type", "component"); node.Add("name", "Synthetic Component");
	ValueArray nodes; nodes.Add(node); topology.Add("nodes", nodes);
	topology.Add("envelope", MakeDemoEnvelope("topology", workspace_id, "demo-topology", "public", provenance));
	if(!SaveFile(AppendFileName(data_root, "demo-topology.json"), AsJSON(topology, true))) return false;

	ValueMap hidden_check;
	hidden_check.Add("id", "private-check"); hidden_check.Add("kind", "paper"); hidden_check.Add("title", "Private synthetic check");
	hidden_check.Add("source", "synthetic"); hidden_check.Add("status", "draft"); hidden_check.Add("classification", "private");
	hidden_check.Add("envelope", MakeDemoEnvelope("paper", workspace_id, "private-check", "private", provenance));
	if(!SaveFile(AppendFileName(data_root, "demo-private-check.json"), AsJSON(hidden_check, true))) return false;

	ValueMap package;
	package.Add("id", revision_id); package.Add("kind", "review-package-revision"); package.Add("workspace_id", workspace_id);
	package.Add("package_id", package_id); package.Add("goal_id", "demo-goal"); package.Add("scope_id", "demo-topology");
	package.Add("base_revision", "base-r1"); package.Add("base_state", "available"); package.Add("head_revision", "head-r1");
	package.Add("previous_revision", "none"); ValueArray paths; paths.Add("src/synthetic.cpp"); package.Add("selected_paths", paths);
	package.Add("patch_bytes", patch); package.Add("patch_sha256", patch_hash); package.Add("artifact_refs", ValueArray());
	ValueArray check_refs; check_refs.Add("private-check"); check_refs.Add("missing-check"); package.Add("check_refs", check_refs);
	package.Add("artifacts", ValueArray()); package.Add("author", "synthetic-reviewer"); package.Add("author_state", "known-local-author");
	package.Add("capture_state", "captured"); package.Add("envelope", MakeDemoEnvelope("review-package-revision", workspace_id, revision_id, "private", provenance));
	if(!SaveFile(AppendFileName(data_root, "demo-review-package.json"), AsJSON(package, true))) return false;

	ValueMap thread;
	thread.Add("id", thread_id); thread.Add("kind", "review-thread"); thread.Add("workspace_id", workspace_id);
	thread.Add("package_id", package_id); thread.Add("revision_id", revision_id); thread.Add("revision_hash", patch_hash);
	thread.Add("source_identity", source_identity); thread.Add("envelope", MakeDemoEnvelope("review-thread", workspace_id, thread_id, "private", provenance));
	if(!SaveFile(AppendFileName(data_root, "demo-review-thread.json"), AsJSON(thread, true))) return false;

	auto save_comment = [&](const String& id, const String& type, const String& author_kind, const String& author, const String& body_text, const ValueArray& refs) {
		ValueMap comment;
		comment.Add("id", id); comment.Add("kind", "review-comment"); comment.Add("workspace_id", workspace_id);
		comment.Add("package_id", package_id); comment.Add("revision_id", revision_id); comment.Add("revision_hash", patch_hash);
		comment.Add("thread_id", thread_id); comment.Add("record_type", type); comment.Add("author_kind", author_kind);
		comment.Add("author", author); comment.Add("body", body_text); comment.Add("artifact_refs", ValueArray());
		comment.Add("check_refs", refs); comment.Add("source_identity", source_identity);
		comment.Add("envelope", MakeDemoEnvelope("review-comment", workspace_id, id, "private", provenance));
		return SaveFile(AppendFileName(data_root, "demo-review-comment-" + id + ".json"), AsJSON(comment, true));
	};
	ValueArray no_refs, hidden_refs; hidden_refs.Add("private-check"); hidden_refs.Add("missing-check");
	return save_comment("human-comment", "comment", "human", "Private Synthetic Reviewer", "SYNTHETIC HUMAN COMMENT BODY", no_refs) &&
	       save_comment("agent-note", "agent-note", "agent", "Private Synthetic Agent", "SYNTHETIC AGENT NOTE BODY", no_refs) &&
	       save_comment("external-evidence", "external-evidence", "external", "Private External Author", "SYNTHETIC EXTERNAL BODY", hidden_refs);
}

END_UPP_NAMESPACE
