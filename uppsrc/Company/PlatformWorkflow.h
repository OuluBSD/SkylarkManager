#ifndef _Company_PlatformWorkflow_h_
#define _Company_PlatformWorkflow_h_

#include <Core/Core.h>
#include "CompanyStore.h"
#include <WebAuth/WebAuthModel.h>
#include "CompanyWorkflow.h"

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// PlatformWorkflow: the reference-shaped control plane (Platform v2).
//
// Implements the reference-platform vocabulary on top of CompanySkylark:
//   - Command Listener: parse + authorize SCM commands into immutable records.
//   - Review Workflow: dispatch/attempt records bound to a head SHA, with
//     head-SHA-bound evidence (stale/incomplete never advance the lifecycle).
//   - Trials: external trial-service dispatch with a completeness gate.
//   - Artifacts: external-artifact metadata with retention/availability.
//
// Security invariants:
//   - Deny by default: every mutation validates the caller identity.
//   - Run commands are admin-gated: the commenting SCM login must map to a
//     local user holding the admin role.
//   - Evidence without a head SHA is `incomplete`; evidence bound to a
//     different SHA than the dispatch is `stale`; neither advances the
//     lifecycle. Compensation is never awarded from platform records.
//   - Every mutation records an audit event under a namespaced idempotency
//     key so replays short-circuit without re-executing.
// ---------------------------------------------------------------------------

// Audit actions (namespaced from the legacy workflow actions).
#define PLATFORM_AUD_COMMAND          "platform.command"
#define PLATFORM_AUD_DISPATCH         "platform.dispatch"
#define PLATFORM_AUD_EVIDENCE         "platform.evidence"
#define PLATFORM_AUD_TRIAL_DISPATCH   "platform.trial_dispatch"
#define PLATFORM_AUD_TRIAL_RESULT     "platform.trial_result"
#define PLATFORM_AUD_TRIAL_COMPLETE   "platform.trial_complete"
#define PLATFORM_AUD_TRIAL_STALE      "platform.trial_stale"
#define PLATFORM_AUD_ARTIFACT         "platform.artifact"

// Namespaced idempotency keys: platform events never collide with legacy
// workflow keys, and the raw key stays reserved for the lifecycle transition
// performed through CompanyWorkflow.
inline String PlatformEventKey(const char* domain, const String& key)
{
	if(key.IsEmpty())
		return String();
	return String("platform.") + domain + ":" + key;
}

class PlatformWorkflow {
public:
	static void SetStore(CompanyStore* store) { s_store = store; }
	static CompanyStore* GetStore()           { return s_store; }

	// ---------- Caller scopes (centralized, deny by default) ----------
	// The SCM command listener and review executor authenticate with a
	// callback:write automation token (or an admin session). The external
	// trial service authenticates with a trials:write token (or admin).
	static bool IsListenerActor(const WebAuthIdentity& actor);
	static bool IsTrialServiceActor(const WebAuthIdentity& actor);
	static bool CanRecordArtifacts(const WebAuthIdentity& actor);

	// ---------- Command Listener ----------
	// Parses `command_text` (first line, exact match): `/review`,
	// `/run reference <sha>`, `/run baseline <args>`; anything else is
	// recorded as unrecognized. Run commands require `actor_login` to map to
	// a local user with the admin role. The command record preserves the
	// decision (received / refused / unrecognized) and never dispatches by
	// itself; dispatch is a separate, explicitly authorized call.
	static CompanyWorkflowResult IngestCommand(const WebAuthIdentity& actor, const String& source,
	                                           const String& repository, int pr_number,
	                                           const String& actor_login, const String& command_text,
	                                           const String& head_sha, const String& idempotency_key);

	// ---------- Review Workflow ----------
	// Creates a dispatch bound to `head_sha` (required, SHA-validated) plus a
	// pending first attempt. A new active dispatch supersedes prior
	// dispatched/in_progress dispatches of the same work item.
	static CompanyWorkflowResult DispatchReview(const WebAuthIdentity& actor, const String& work_item_id,
	                                            const String& kind, const String& head_sha,
	                                            const String& base_sha, const String& idempotency_key);

	// Records head-SHA-bound review evidence and creates the attempt for it
	// (each evidence delivery is one attempt; retries create new attempts).
	// Outcomes: passed/failed with matching SHA -> lifecycle advances through
	// CompanyWorkflow::RecordMechanicalResult; empty SHA -> incomplete;
	// different SHA -> stale (dispatch marked stale, lifecycle untouched).
	static CompanyWorkflowResult RecordReviewEvidence(const WebAuthIdentity& actor, const String& dispatch_id,
	                                                  const String& head_sha, bool passed, const String& findings,
	                                                  const String& check_url, const String& external_service,
	                                                  const String& run_id, const String& tool_version,
	                                                  const String& idempotency_key);

	// ---------- Trials (external service contract) ----------
	// expected_results = splits.GetCount() * trials_per_split.
	static CompanyWorkflowResult DispatchTrial(const WebAuthIdentity& actor, const String& kind,
	                                           const String& work_item_id, const String& repository,
	                                           int pr_number, const String& head_sha,
	                                           const Vector<String>& splits, int trials_per_split,
	                                           const String& idempotency_key);
	// One result per (split, trial_index); duplicates are idempotent no-ops.
	// Completeness gate: the trial turns complete when received == expected.
	// A result SHA differing from the trial SHA is recorded as stale and
	// never counts toward completeness.
	static CompanyWorkflowResult RecordTrialResult(const WebAuthIdentity& actor, const String& trial_id,
	                                               const String& split, int trial_index, bool passed,
	                                               double reward, const String& summary, const String& head_sha,
	                                               const String& external_job_id, const String& idempotency_key);

	// ---------- Artifacts (metadata only; content stays external) ----------
	static CompanyWorkflowResult RecordArtifact(const WebAuthIdentity& actor, const String& owner_type,
	                                            const String& owner_id, const String& kind, const String& uri,
	                                            const String& content_type, int64 size, const String& sha256,
	                                            const String& retention, const String& idempotency_key);

	// ---------- Bookkeeping snapshot ----------
	// Counts by status/outcome for the admin Platform page and /api/v2/status.
	static ValueMap StatusSnapshot();

private:
	static CompanyStore* s_store;
	static CompanyWorkflowResult Fail(const String& error);
	static bool StoreReady();
};

END_UPP_NAMESPACE

#endif
