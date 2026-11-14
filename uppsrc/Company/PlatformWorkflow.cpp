#include "PlatformWorkflow.h"

NAMESPACE_UPP

CompanyStore* PlatformWorkflow::s_store = NULL;

// ---------------------------------------------------------------------------
// Guards
// ---------------------------------------------------------------------------

bool PlatformWorkflow::StoreReady()
{
	return s_store != NULL && s_store->GetCorruptionErrors().IsEmpty();
}

CompanyWorkflowResult PlatformWorkflow::Fail(const String& error)
{
	CompanyWorkflowResult r;
	r.error = error;
	return r;
}

bool PlatformWorkflow::IsListenerActor(const WebAuthIdentity& actor)
{
	if(!actor.authenticated)
		return false;
	if(actor.IsAutomation() && actor.HasScope(COMPANY_SCOPE_CALLBACK_WRITE))
		return true;
	return actor.IsHuman() && actor.IsAdmin();
}

bool PlatformWorkflow::IsTrialServiceActor(const WebAuthIdentity& actor)
{
	if(!actor.authenticated)
		return false;
	if(actor.IsAutomation() && actor.HasScope(COMPANY_SCOPE_TRIALS_WRITE))
		return true;
	return actor.IsHuman() && actor.IsAdmin();
}

bool PlatformWorkflow::CanRecordArtifacts(const WebAuthIdentity& actor)
{
	if(!actor.authenticated)
		return false;
	if(actor.IsAutomation() && (actor.HasScope(COMPANY_SCOPE_CALLBACK_WRITE) || actor.HasScope(COMPANY_SCOPE_TRIALS_WRITE)))
		return true;
	return actor.IsHuman() && actor.IsAdmin();
}

// ---------------------------------------------------------------------------
// Command Listener
// ---------------------------------------------------------------------------

// Parses the first line of a comment into (kind, arguments). Exact-match
// commands only; anything else is unrecognized and never dispatched.
static void ParsePlatformCommand(const String& command_text, String& kind, String& arguments)
{
	kind = PLATFORM_CMD_UNKNOWN;
	arguments.Clear();
	String line = TrimBoth(command_text);
	int nl = line.Find('\n');
	if(nl >= 0)
		line = TrimBoth(line.Mid(0, nl));
	if(line == "/review" || line.StartsWith("/review mechanical") || line.StartsWith("/review agentic")) {
		kind = PLATFORM_CMD_REVIEW;
		arguments = TrimBoth(line.Mid(7));
		return;
	}
	if(line.StartsWith("/run reference")) {
		kind = PLATFORM_CMD_RUN_REFERENCE;
		arguments = TrimBoth(line.Mid(14));
		return;
	}
	if(line.StartsWith("/run baseline")) {
		kind = PLATFORM_CMD_RUN_BASELINE;
		arguments = TrimBoth(line.Mid(13));
		return;
	}
}

CompanyWorkflowResult PlatformWorkflow::IngestCommand(const WebAuthIdentity& actor, const String& source,
                                                      const String& repository, int pr_number,
                                                      const String& actor_login, const String& command_text,
                                                      const String& head_sha, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsListenerActor(actor))
		return Fail("command ingest requires callback:write scope (automation) or admin role");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");

	CompanyStore* store = GetStore();
	String event_key = PlatformEventKey("command", idempotency_key);
	if(store->IsKeyProcessed(event_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	String kind, arguments;
	ParsePlatformCommand(command_text, kind, arguments);

	String status = PLATFORM_CMD_RECEIVED;
	String refusal_reason;
	if(!repository.IsEmpty() && !store->IsRepositoryAllowed(repository)) {
		status = PLATFORM_CMD_REFUSED;
		refusal_reason = "repository is not allowlisted: " + repository;
	}
	else if(kind == PLATFORM_CMD_RUN_REFERENCE || kind == PLATFORM_CMD_RUN_BASELINE) {
		// Run commands are admin-gated: the SCM login must map to a local
		// admin user. Unknown logins are refused (fail closed).
		UserRecord user;
		if(actor_login.IsEmpty()) {
			status = PLATFORM_CMD_REFUSED;
			refusal_reason = "run commands require the commenting login (actor_login)";
		}
		else if(!store->FindUserByUsername(actor_login, user)) {
			status = PLATFORM_CMD_REFUSED;
			refusal_reason = "commenting login is not a known user: " + actor_login;
		}
		else if(!user.HasRole(COMPANY_ROLE_ADMIN)) {
			status = PLATFORM_CMD_REFUSED;
			refusal_reason = "run commands are admin-gated; '" + actor_login + "' is not an admin";
		}
	}

	PlatformCommandRecord cmd;
	cmd.id = CompanyGenerateId("cmd");
	cmd.source = source.IsEmpty() ? "api" : source;
	cmd.repository = repository;
	cmd.pr_number = pr_number;
	cmd.actor_login = actor_login;
	cmd.command = kind;
	cmd.arguments = arguments;
	if(!head_sha.IsEmpty() && PlatformIsValidSha(head_sha))
		cmd.head_sha = head_sha;
	cmd.status = kind == PLATFORM_CMD_UNKNOWN ? PLATFORM_CMD_UNKNOWN : status;
	cmd.refusal_reason = refusal_reason;
	cmd.created_by_actor = actor.token_id.IsEmpty() ? actor.user_id : ("token " + actor.token_id);
	cmd.idempotency_key = event_key;
	cmd.created_at = GetSysTime();
	cmd.updated_at = GetSysTime();

	String error;
	if(!store->SavePlatformCommand(cmd, error))
		return Fail("cannot store command: " + error);

	if(!CompanyWorkflow::Audit(PLATFORM_AUD_COMMAND, "platform_command", cmd.id, "", cmd.status, actor,
	                           refusal_reason.IsEmpty() ? kind : refusal_reason, event_key, &r.event_id))
		return Fail("audit recording failed");

	r.ok = true;
	r.payload.Set("command_id", cmd.id);
	r.payload.Set("command", kind);
	r.payload.Set("status", cmd.status);
	if(!refusal_reason.IsEmpty())
		r.payload.Set("refusal_reason", refusal_reason);
	return r;
}

// ---------------------------------------------------------------------------
// Review dispatch
// ---------------------------------------------------------------------------

CompanyWorkflowResult PlatformWorkflow::DispatchReview(const WebAuthIdentity& actor, const String& work_item_id,
                                                       const String& kind, const String& head_sha,
                                                       const String& base_sha, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsListenerActor(actor))
		return Fail("review dispatch requires callback:write scope (automation) or admin role");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	if(kind != PLATFORM_REVIEW_MECHANICAL && kind != PLATFORM_REVIEW_AGENTIC)
		return Fail("review kind must be mechanical or agentic");
	if(!PlatformIsValidSha(head_sha))
		return Fail("head_sha is required (7-40 hex characters)");
	if(!base_sha.IsEmpty() && !PlatformIsValidSha(base_sha))
		return Fail("base_sha must be empty or 7-40 hex characters");

	CompanyStore* store = GetStore();
	String event_key = PlatformEventKey("dispatch", idempotency_key);
	if(store->IsKeyProcessed(event_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	WorkItemRecord item;
	if(!store->FindWorkItem(work_item_id, item))
		return Fail("work item not found: " + work_item_id);
	if(!store->IsRepositoryAllowed(item.repository))
		return Fail("repository is not allowlisted: " + item.repository);
	if(item.state != COMPANY_STATE_IMPLEMENTATION && item.state != COMPANY_STATE_CHANGES
	   && item.state != COMPANY_STATE_MECHANICAL)
		return Fail("work item is not reviewable in state: " + item.state);
	if(item.pr_number == 0)
		return Fail("work item has no pull request bound");

	// Supersede prior active dispatches of this work item: only the newest
	// dispatch may accept evidence.
	Vector<PlatformReviewDispatch> dispatches = store->GetReviewDispatches();
	for(int i = 0; i < dispatches.GetCount(); i++) {
		if(dispatches[i].work_item_id == work_item_id
		   && (dispatches[i].status == PLATFORM_DSP_DISPATCHED || dispatches[i].status == PLATFORM_DSP_IN_PROGRESS)) {
			PlatformReviewDispatch prior = dispatches[i];
			prior.status = PLATFORM_DSP_SUPERSEDED;
			prior.updated_at = GetSysTime();
			String serr;
			store->SaveReviewDispatch(prior, serr);
			CompanyWorkflow::Audit(PLATFORM_AUD_DISPATCH, "platform_dispatch", prior.id,
			                       dispatches[i].status, PLATFORM_DSP_SUPERSEDED, actor,
			                       "superseded by a newer dispatch", "", NULL);
		}
	}

	PlatformReviewDispatch dsp;
	dsp.id = CompanyGenerateId("dsp");
	dsp.work_item_id = item.id;
	dsp.repository = item.repository;
	dsp.pr_number = item.pr_number;
	dsp.kind = kind;
	dsp.head_sha = head_sha;
	dsp.base_sha = base_sha;
	dsp.requested_by = actor.token_id.IsEmpty() ? actor.user_id : ("token " + actor.token_id);
	dsp.status = PLATFORM_DSP_DISPATCHED;
	dsp.idempotency_key = event_key;
	dsp.created_at = GetSysTime();
	dsp.updated_at = GetSysTime();
	String error;
	if(!store->SaveReviewDispatch(dsp, error))
		return Fail("cannot store dispatch: " + error);

	PlatformReviewAttempt atm;
	atm.id = CompanyGenerateId("atm");
	atm.dispatch_id = dsp.id;
	atm.external_service = "github_actions";
	atm.status = PLATFORM_ATM_PENDING;
	atm.created_at = GetSysTime();
	atm.updated_at = GetSysTime();
	if(!store->SaveReviewAttempt(atm, error))
		return Fail("cannot store attempt: " + error);

	if(!CompanyWorkflow::Audit(PLATFORM_AUD_DISPATCH, "platform_dispatch", dsp.id, "", dsp.status, actor,
	                           kind + " review dispatch (head " + head_sha + ")", event_key, &r.event_id))
		return Fail("audit recording failed");

	r.ok = true;
	r.payload.Set("dispatch_id", dsp.id);
	r.payload.Set("attempt_id", atm.id);
	r.payload.Set("work_item_id", item.id);
	r.payload.Set("head_sha", head_sha);
	return r;
}

// ---------------------------------------------------------------------------
// Review evidence (head-SHA-bound)
// ---------------------------------------------------------------------------

CompanyWorkflowResult PlatformWorkflow::RecordReviewEvidence(const WebAuthIdentity& actor, const String& dispatch_id,
                                                             const String& head_sha, bool passed, const String& findings,
                                                             const String& check_url, const String& external_service,
                                                             const String& run_id, const String& tool_version,
                                                             const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsListenerActor(actor))
		return Fail("evidence recording requires callback:write scope (automation) or admin role");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");

	CompanyStore* store = GetStore();
	String event_key = PlatformEventKey("evidence", idempotency_key);
	if(store->IsKeyProcessed(event_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	PlatformReviewDispatch dsp;
	if(!store->FindReviewDispatch(dispatch_id, dsp))
		return Fail("dispatch not found: " + dispatch_id);
	if(dsp.status == PLATFORM_DSP_SUPERSEDED)
		return Fail("dispatch is superseded; evidence must target the active dispatch");

	WorkItemRecord item;
	if(!store->FindWorkItem(dsp.work_item_id, item))
		return Fail("work item not found: " + dsp.work_item_id);

	// Outcome determination (fail closed):
	//   - missing/invalid SHA or missing run id -> incomplete;
	//   - SHA mismatch against the dispatch -> stale (no lifecycle advance);
	//   - otherwise passed/failed and the lifecycle may advance.
	String outcome;
	if(head_sha.IsEmpty() || !PlatformIsValidSha(head_sha) || dsp.head_sha.IsEmpty() || run_id.IsEmpty())
		outcome = PLATFORM_EVD_INCOMPLETE;
	else if(ToLower(head_sha) != ToLower(dsp.head_sha))
		outcome = PLATFORM_EVD_STALE;
	else
		outcome = passed ? PLATFORM_EVD_PASSED : PLATFORM_EVD_FAILED;

	PlatformReviewAttempt atm;
	atm.id = CompanyGenerateId("atm");
	atm.dispatch_id = dsp.id;
	atm.external_service = external_service.IsEmpty() ? "github_actions" : external_service;
	atm.run_id = run_id;
	atm.status = outcome == PLATFORM_EVD_STALE ? PLATFORM_ATM_STALE
	           : outcome == PLATFORM_EVD_INCOMPLETE ? PLATFORM_ATM_FAILED
	           : PLATFORM_ATM_COMPLETED;
	atm.created_at = GetSysTime();
	atm.updated_at = GetSysTime();
	String error;
	if(!store->SaveReviewAttempt(atm, error))
		return Fail("cannot store attempt: " + error);

	PlatformReviewEvidence evd;
	evd.id = CompanyGenerateId("evd");
	evd.attempt_id = atm.id;
	evd.dispatch_id = dsp.id;
	evd.work_item_id = dsp.work_item_id;
	evd.repository = dsp.repository;
	evd.pr_number = dsp.pr_number;
	evd.head_sha = head_sha;
	evd.outcome = outcome;
	evd.findings = findings;
	evd.check_url = check_url;
	evd.run_id = run_id;
	evd.external_service = external_service.IsEmpty() ? "github_actions" : external_service;
	evd.tool_version = tool_version;
	evd.producer_actor = actor.token_id.IsEmpty() ? actor.user_id : ("token " + actor.token_id);
	evd.event_id = event_key;
	evd.recorded_at = GetSysTime();
	if(!store->SaveReviewEvidence(evd, error))
		return Fail("cannot store evidence: " + error);

	// Dispatch status follows the outcome; stale dispatches never accept
	// further evidence.
	String dispatch_status = dsp.status;
	if(outcome == PLATFORM_EVD_STALE)
		dispatch_status = PLATFORM_DSP_STALE;
	else if(outcome == PLATFORM_EVD_PASSED || outcome == PLATFORM_EVD_FAILED)
		dispatch_status = PLATFORM_DSP_COMPLETED;
	else
		dispatch_status = PLATFORM_DSP_IN_PROGRESS;
	if(dispatch_status != dsp.status) {
		PlatformReviewDispatch updated = dsp;
		updated.status = dispatch_status;
		updated.updated_at = GetSysTime();
		String serr;
		store->SaveReviewDispatch(updated, serr);
	}

	// Lifecycle transition only for complete, non-stale evidence while the
	// work item is in mechanical_review. The raw idempotency key stays
	// reserved for the legacy transition so duplicate deliveries never
	// re-execute it.
	String transition_error;
	if((outcome == PLATFORM_EVD_PASSED || outcome == PLATFORM_EVD_FAILED) && item.state == COMPANY_STATE_MECHANICAL) {
		ValueMap legacy_evidence;
		legacy_evidence.Set("run_id", run_id);
		legacy_evidence.Set("comment_url", check_url);
		legacy_evidence.Set("findings", findings);
		legacy_evidence.Set("event_id", event_key);
		legacy_evidence.Set("source", evd.external_service);
		legacy_evidence.Set("head_sha", head_sha);
		legacy_evidence.Set("dispatch_id", dsp.id);
		legacy_evidence.Set("evidence_id", evd.id);
		CompanyWorkflowResult transition = CompanyWorkflow::RecordMechanicalResult(actor, item.id, passed,
		                                                                          legacy_evidence, idempotency_key);
		if(!transition.ok)
			transition_error = transition.error;
	}

	if(!CompanyWorkflow::Audit(PLATFORM_AUD_EVIDENCE, "platform_dispatch", dsp.id,
	                           dsp.status, dispatch_status, actor,
	                           outcome + (transition_error.IsEmpty() ? "" : " (transition: " + transition_error + ")"),
	                           event_key, &r.event_id))
		return Fail("audit recording failed");

	r.ok = true;
	r.payload.Set("evidence_id", evd.id);
	r.payload.Set("attempt_id", atm.id);
	r.payload.Set("outcome", outcome);
	r.payload.Set("dispatch_status", dispatch_status);
	r.payload.Set("work_item_state", item.state == COMPANY_STATE_MECHANICAL
	                                 && (outcome == PLATFORM_EVD_PASSED) ? Value(COMPANY_STATE_OPERATOR)
	                                 : Value(item.state));
	if(!transition_error.IsEmpty())
		r.payload.Set("transition_error", transition_error);
	return r;
}

// ---------------------------------------------------------------------------
// Trials (external service contract)
// ---------------------------------------------------------------------------

CompanyWorkflowResult PlatformWorkflow::DispatchTrial(const WebAuthIdentity& actor, const String& kind,
                                                      const String& work_item_id, const String& repository,
                                                      int pr_number, const String& head_sha,
                                                      const Vector<String>& splits, int trials_per_split,
                                                      const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsTrialServiceActor(actor))
		return Fail("trial dispatch requires trials:write scope (automation) or admin role");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	if(kind != PLATFORM_TRIAL_REFERENCE && kind != PLATFORM_TRIAL_BASELINE && kind != PLATFORM_TRIAL_CANDIDATE)
		return Fail("trial kind must be reference, baseline, or candidate");
	if(!PlatformIsValidSha(head_sha))
		return Fail("head_sha is required (7-40 hex characters)");
	if(splits.GetCount() == 0 || splits.GetCount() > 16)
		return Fail("1-16 splits are required");
	if(trials_per_split < 1 || trials_per_split > 16)
		return Fail("trials_per_split must be between 1 and 16");

	CompanyStore* store = GetStore();
	String event_key = PlatformEventKey("trial", idempotency_key);
	if(store->IsKeyProcessed(event_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	if(!repository.IsEmpty() && !store->IsRepositoryAllowed(repository))
		return Fail("repository is not allowlisted: " + repository);
	if(!work_item_id.IsEmpty()) {
		WorkItemRecord item;
		if(!store->FindWorkItem(work_item_id, item))
			return Fail("work item not found: " + work_item_id);
	}

	PlatformTrialRecord trial;
	trial.id = CompanyGenerateId("trl");
	trial.kind = kind;
	trial.work_item_id = work_item_id;
	trial.repository = repository;
	trial.pr_number = pr_number;
	trial.head_sha = head_sha;
	trial.splits = clone(splits);
	trial.trials_per_split = trials_per_split;
	trial.requested_by = actor.token_id.IsEmpty() ? actor.user_id : ("token " + actor.token_id);
	trial.external_service = "external_trial_service";
	trial.status = PLATFORM_TRIAL_DISPATCHED;
	trial.expected_results = splits.GetCount() * trials_per_split;
	trial.received_results = 0;
	trial.idempotency_key = event_key;
	trial.created_at = GetSysTime();
	trial.updated_at = GetSysTime();
	String error;
	if(!store->SavePlatformTrial(trial, error))
		return Fail("cannot store trial: " + error);

	if(!CompanyWorkflow::Audit(PLATFORM_AUD_TRIAL_DISPATCH, "platform_trial", trial.id, "", trial.status, actor,
	                           kind + " trial dispatch (" + AsString(trial.expected_results) + " expected results)",
	                           event_key, &r.event_id))
		return Fail("audit recording failed");

	r.ok = true;
	r.payload.Set("trial_id", trial.id);
	r.payload.Set("expected_results", trial.expected_results);
	return r;
}

CompanyWorkflowResult PlatformWorkflow::RecordTrialResult(const WebAuthIdentity& actor, const String& trial_id,
                                                          const String& split, int trial_index, bool passed,
                                                          double reward, const String& summary, const String& head_sha,
                                                          const String& external_job_id, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsTrialServiceActor(actor))
		return Fail("trial results require trials:write scope (automation) or admin role");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");

	CompanyStore* store = GetStore();
	String event_key = PlatformEventKey("result", idempotency_key);
	if(store->IsKeyProcessed(event_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	PlatformTrialRecord trial;
	if(!store->FindPlatformTrial(trial_id, trial))
		return Fail("trial not found: " + trial_id);

	// A recorded (split, index) result is an idempotent no-op regardless of
	// the trial status; a stale slot may be retried with corrected evidence.
	Vector<PlatformTrialResultRecord> existing = store->ResultsForTrial(trial_id);
	for(int i = 0; i < existing.GetCount(); i++)
		if(existing[i].split == split && existing[i].trial_index == trial_index
		   && existing[i].outcome == PLATFORM_TRES_RECORDED) {
			r.ok = true;
			r.idempotent_duplicate = true;
			r.payload.Set("result_id", existing[i].id);
			return r;
		}

	if(trial.status != PLATFORM_TRIAL_DISPATCHED && trial.status != PLATFORM_TRIAL_COLLECTING)
		return Fail("trial is not accepting results (status: " + trial.status + ")");

	bool split_known = false;
	for(int i = 0; i < trial.splits.GetCount(); i++)
		if(trial.splits[i] == split)
			split_known = true;
	if(!split_known)
		return Fail("split is not part of this trial: " + split);
	if(trial_index < 1 || trial_index > trial.trials_per_split)
		return Fail("trial_index must be between 1 and " + AsString(trial.trials_per_split));

	// Fail closed: a result is only counted when bound to the trial's SHA.
	String outcome = PLATFORM_TRES_RECORDED;
	if(head_sha.IsEmpty() || !PlatformIsValidSha(head_sha) || ToLower(head_sha) != ToLower(trial.head_sha))
		outcome = PLATFORM_TRES_STALE;

	PlatformTrialResultRecord res;
	res.id = CompanyGenerateId("res");
	res.trial_id = trial.id;
	res.split = split;
	res.trial_index = trial_index;
	res.outcome = outcome;
	res.passed = passed;
	res.reward = reward;
	res.summary = summary;
	res.head_sha = head_sha;
	res.external_job_id = external_job_id;
	res.event_id = event_key;
	res.recorded_at = GetSysTime();
	String error;
	if(!store->SaveTrialResult(res, error))
		return Fail("cannot store trial result: " + error);

	String trial_status = trial.status;
	if(outcome == PLATFORM_TRES_RECORDED) {
		int recorded = 0;
		Vector<PlatformTrialResultRecord> results = store->ResultsForTrial(trial_id);
		for(int i = 0; i < results.GetCount(); i++)
			if(results[i].outcome == PLATFORM_TRES_RECORDED)
				recorded++;
		trial.received_results = recorded;
		if(recorded >= trial.expected_results) {
			trial_status = PLATFORM_TRIAL_COMPLETE;
			trial.status = PLATFORM_TRIAL_COMPLETE;
		}
		else if(trial.status == PLATFORM_TRIAL_DISPATCHED)
			trial.status = PLATFORM_TRIAL_COLLECTING;
		trial.updated_at = GetSysTime();
		if(!store->SavePlatformTrial(trial, error))
			return Fail("cannot update trial: " + error);
	}

	if(outcome == PLATFORM_TRES_STALE) {
		if(!CompanyWorkflow::Audit(PLATFORM_AUD_TRIAL_STALE, "platform_trial", trial.id, trial.status, trial.status,
		                           actor, "result " + split + "#" + AsString(trial_index) + " is not bound to head "
		                           + trial.head_sha, event_key, &r.event_id))
			return Fail("audit recording failed");
	}
	else if(trial_status == PLATFORM_TRIAL_COMPLETE) {
		if(!CompanyWorkflow::Audit(PLATFORM_AUD_TRIAL_COMPLETE, "platform_trial", trial.id, PLATFORM_TRIAL_COLLECTING,
		                           PLATFORM_TRIAL_COMPLETE, actor,
		                           AsString(trial.received_results) + "/" + AsString(trial.expected_results)
		                           + " results recorded", event_key, &r.event_id))
			return Fail("audit recording failed");
	}
	else {
		if(!CompanyWorkflow::Audit(PLATFORM_AUD_TRIAL_RESULT, "platform_trial", trial.id, trial.status, trial_status,
		                           actor, split + "#" + AsString(trial_index) + (passed ? " passed" : " failed"),
		                           event_key, &r.event_id))
			return Fail("audit recording failed");
	}

	r.ok = true;
	r.payload.Set("result_id", res.id);
	r.payload.Set("outcome", outcome);
	r.payload.Set("trial_status", trial.status);
	r.payload.Set("received_results", trial.received_results);
	r.payload.Set("expected_results", trial.expected_results);
	return r;
}

// ---------------------------------------------------------------------------
// Artifacts (metadata only)
// ---------------------------------------------------------------------------

CompanyWorkflowResult PlatformWorkflow::RecordArtifact(const WebAuthIdentity& actor, const String& owner_type,
                                                       const String& owner_id, const String& kind, const String& uri,
                                                       const String& content_type, int64 size, const String& sha256,
                                                       const String& retention, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!CanRecordArtifacts(actor))
		return Fail("artifact recording requires callback:write or trials:write scope (automation) or admin role");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	if(owner_type != "evidence" && owner_type != "trial" && owner_type != "dispatch" && owner_type != "work_item")
		return Fail("owner_type must be evidence, trial, dispatch, or work_item");
	if(kind.IsEmpty())
		return Fail("artifact kind is required");
	if(uri.IsEmpty())
		return Fail("artifact uri is required");
	if(size < 0)
		return Fail("artifact size must be non-negative");

	CompanyStore* store = GetStore();
	String event_key = PlatformEventKey("artifact", idempotency_key);
	if(store->IsKeyProcessed(event_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	// The owner must exist: artifacts are pointers, never orphans.
	if(owner_type == "evidence") {
		PlatformReviewEvidence evd;
		if(!store->FindReviewEvidence(owner_id, evd))
			return Fail("evidence owner not found: " + owner_id);
	}
	else if(owner_type == "trial") {
		PlatformTrialRecord trial;
		if(!store->FindPlatformTrial(owner_id, trial))
			return Fail("trial owner not found: " + owner_id);
	}
	else if(owner_type == "dispatch") {
		PlatformReviewDispatch dsp;
		if(!store->FindReviewDispatch(owner_id, dsp))
			return Fail("dispatch owner not found: " + owner_id);
	}
	else {
		WorkItemRecord item;
		if(!store->FindWorkItem(owner_id, item))
			return Fail("work item owner not found: " + owner_id);
	}

	PlatformArtifactRecord art;
	art.id = CompanyGenerateId("art");
	art.owner_type = owner_type;
	art.owner_id = owner_id;
	art.kind = kind;
	art.uri = uri;
	art.content_type = content_type;
	art.size = size;
	art.sha256 = sha256;
	art.retention = retention.IsEmpty() ? "standard" : retention;
	art.availability = PLATFORM_ART_AVAILABLE;
	art.produced_by = actor.token_id.IsEmpty() ? actor.user_id : ("token " + actor.token_id);
	art.event_id = event_key;
	art.recorded_at = GetSysTime();
	String error;
	if(!store->SavePlatformArtifact(art, error))
		return Fail("cannot store artifact: " + error);

	if(!CompanyWorkflow::Audit(PLATFORM_AUD_ARTIFACT, "platform_artifact", art.id, "", art.availability, actor,
	                           kind + " artifact for " + owner_type + " " + owner_id, event_key, &r.event_id))
		return Fail("audit recording failed");

	r.ok = true;
	r.payload.Set("artifact_id", art.id);
	return r;
}

// ---------------------------------------------------------------------------
// Bookkeeping snapshot
// ---------------------------------------------------------------------------

static void CountByStatus(const String& status, ValueMap& out)
{
	if(out.Find(status) < 0)
		out.Set(status, 0);
	out.Set(status, (int)out[status] + 1);
}

ValueMap PlatformWorkflow::StatusSnapshot()
{
	ValueMap snap;
	CompanyStore* store = GetStore();
	if(!store)
		return snap;

	Vector<PlatformCommandRecord> commands = store->GetPlatformCommands();
	ValueMap by_status;
	for(int i = 0; i < commands.GetCount(); i++)
		CountByStatus(commands[i].status, by_status);
	snap.Set("commands_total", commands.GetCount());
	snap.Set("commands_by_status", by_status);

	Vector<PlatformReviewDispatch> dispatches = store->GetReviewDispatches();
	by_status.Clear();
	for(int i = 0; i < dispatches.GetCount(); i++)
		CountByStatus(dispatches[i].status, by_status);
	snap.Set("dispatches_total", dispatches.GetCount());
	snap.Set("dispatches_by_status", by_status);

	Vector<PlatformReviewEvidence> evidence = store->GetReviewEvidence();
	by_status.Clear();
	for(int i = 0; i < evidence.GetCount(); i++)
		CountByStatus(evidence[i].outcome, by_status);
	snap.Set("evidence_total", evidence.GetCount());
	snap.Set("evidence_by_outcome", by_status);

	Vector<PlatformTrialRecord> trials = store->GetPlatformTrials();
	by_status.Clear();
	for(int i = 0; i < trials.GetCount(); i++)
		CountByStatus(trials[i].status, by_status);
	snap.Set("trials_total", trials.GetCount());
	snap.Set("trials_by_status", by_status);

	Vector<PlatformArtifactRecord> artifacts = store->GetPlatformArtifacts();
	snap.Set("artifacts_total", artifacts.GetCount());

	return snap;
}

END_UPP_NAMESPACE
