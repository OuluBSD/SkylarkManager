#include "CompanyWorkflow.h"

NAMESPACE_UPP

CompanyStore* CompanyWorkflow::s_store = NULL;

CompanyWorkflowResult CompanyWorkflow::Fail(const String& error)
{
	CompanyWorkflowResult r;
	r.ok = false;
	r.error = error;
	return r;
}

bool CompanyWorkflow::StoreReady()
{
	return s_store != NULL && s_store->GetCorruptionErrors().IsEmpty();
}

// ---------------------------------------------------------------------------
// Transition tables
// ---------------------------------------------------------------------------

Vector<String> CompanyWorkflow::WorkStates()
{
	Vector<String> s;
	s.Add(COMPANY_STATE_IMPLEMENTATION);
	s.Add(COMPANY_STATE_MECHANICAL);
	s.Add(COMPANY_STATE_OPERATOR);
	s.Add(COMPANY_STATE_COMPLETED);
	s.Add(COMPANY_STATE_COMPENSATED);
	s.Add(COMPANY_STATE_BLOCKED);
	s.Add(COMPANY_STATE_CHANGES);
	s.Add(COMPANY_STATE_REJECTED);
	return s;
}

Vector<String> CompanyWorkflow::ProposalStates()
{
	Vector<String> s;
	s.Add(COMPANY_PROPOSAL_PROPOSED);
	s.Add(COMPANY_PROPOSAL_ACCEPTED);
	s.Add(COMPANY_PROPOSAL_REJECTED);
	s.Add(COMPANY_PROPOSAL_CHANGES);
	return s;
}

static bool PairIs(const String& from, const String& to, const char* f, const char* t)
{
	return from == f && to == t;
}

static Vector<String> CompanyAdminReviewerRoles()
{
	Vector<String> v;
	v.Add(COMPANY_ROLE_ADMIN);
	v.Add(COMPANY_ROLE_REVIEWER);
	return v;
}

bool CompanyWorkflow::IsValidWorkTransition(const String& from, const String& to)
{
	// Forward lifecycle.
	if(PairIs(from, to, COMPANY_STATE_IMPLEMENTATION, COMPANY_STATE_MECHANICAL)) return true;
	if(PairIs(from, to, COMPANY_STATE_MECHANICAL,    COMPANY_STATE_OPERATOR))     return true;
	if(PairIs(from, to, COMPANY_STATE_MECHANICAL,    COMPANY_STATE_IMPLEMENTATION)) return true;
	if(PairIs(from, to, COMPANY_STATE_OPERATOR,      COMPANY_STATE_COMPLETED))    return true;
	if(PairIs(from, to, COMPANY_STATE_OPERATOR,      COMPANY_STATE_CHANGES))      return true;
	if(PairIs(from, to, COMPANY_STATE_OPERATOR,      COMPANY_STATE_REJECTED))     return true;
	if(PairIs(from, to, COMPANY_STATE_CHANGES,       COMPANY_STATE_IMPLEMENTATION)) return true;
	if(PairIs(from, to, COMPANY_STATE_COMPLETED,     COMPANY_STATE_COMPENSATED))  return true;
	// Explicit blocked transitions.
	if(PairIs(from, to, COMPANY_STATE_IMPLEMENTATION, COMPANY_STATE_BLOCKED))     return true;
	if(PairIs(from, to, COMPANY_STATE_MECHANICAL,     COMPANY_STATE_BLOCKED))     return true;
	if(PairIs(from, to, COMPANY_STATE_OPERATOR,       COMPANY_STATE_BLOCKED))     return true;
	if(PairIs(from, to, COMPANY_STATE_CHANGES,        COMPANY_STATE_BLOCKED))     return true;
	if(PairIs(from, to, COMPANY_STATE_BLOCKED,        COMPANY_STATE_IMPLEMENTATION)) return true;
	if(PairIs(from, to, COMPANY_STATE_BLOCKED,        COMPANY_STATE_REJECTED))    return true;
	return false;
}

bool CompanyWorkflow::IsValidProposalTransition(const String& from, const String& to)
{
	if(PairIs(from, to, COMPANY_PROPOSAL_PROPOSED, COMPANY_PROPOSAL_ACCEPTED)) return true;
	if(PairIs(from, to, COMPANY_PROPOSAL_PROPOSED, COMPANY_PROPOSAL_REJECTED)) return true;
	if(PairIs(from, to, COMPANY_PROPOSAL_PROPOSED, COMPANY_PROPOSAL_CHANGES))  return true;
	if(PairIs(from, to, COMPANY_PROPOSAL_CHANGES,  COMPANY_PROPOSAL_PROPOSED)) return true;
	if(PairIs(from, to, COMPANY_PROPOSAL_CHANGES,  COMPANY_PROPOSAL_REJECTED)) return true;
	return false;
}

// ---------------------------------------------------------------------------
// Centralized guards (deny by default)
// ---------------------------------------------------------------------------

bool CompanyWorkflow::CanWriteProposals(const WebAuthIdentity& actor)
{
	return actor.authenticated && actor.HasScope(COMPANY_SCOPE_PROPOSALS_WRITE);
}

bool CompanyWorkflow::IsHumanWithRole(const WebAuthIdentity& actor, const String& role)
{
	return actor.IsHuman() && actor.HasRole(role);
}

bool CompanyWorkflow::IsHumanWithAnyRole(const WebAuthIdentity& actor, const Vector<String>& roles)
{
	if(!actor.IsHuman())
		return false;
	for(int i = 0; i < roles.GetCount(); i++)
		if(actor.HasRole(roles[i]))
			return true;
	return false;
}

bool CompanyWorkflow::IsAdminHuman(const WebAuthIdentity& actor)
{
	return IsHumanWithRole(actor, COMPANY_ROLE_ADMIN);
}

bool CompanyWorkflow::CanManageWork(const WebAuthIdentity& actor, const WorkItemRecord& item)
{
	if(!actor.authenticated)
		return false;
	if(IsAdminHuman(actor))
		return true;
	bool is_contributor =
		(!item.contributor_user_id.IsEmpty() && item.contributor_user_id == actor.user_id) ||
		(!item.contributor_token_id.IsEmpty() && item.contributor_token_id == actor.token_id);
	return is_contributor;
}

bool CompanyWorkflow::CanRecordMechanical(const WebAuthIdentity& actor)
{
	// Automation callbacks with the callback scope, or a human admin override.
	if(actor.IsAutomation() && actor.HasScope(COMPANY_SCOPE_CALLBACK_WRITE))
		return true;
	return IsAdminHuman(actor);
}

ValueMap CompanyWorkflow::MakeDecision(const String& action, const WebAuthIdentity& actor, const String& reason)
{
	ValueMap d;
	d.Set("action", action);
	d.Set("actor_user_id", actor.user_id);
	d.Set("actor_token_id", actor.token_id);
	d.Set("actor_type", actor.actor_type);
	d.Set("reason", reason);
	d.Set("at", CompanyTimeIso(GetSysTime()));
	return d;
}

bool CompanyWorkflow::FindOpenReview(const String& work_item_id, const String& kind, ReviewRecord& out)
{
	CompanyStore* store = GetStore();
	if(!store)
		return false;
	Vector<ReviewRecord> reviews = store->ReviewsForWorkItem(work_item_id);
	for(int i = 0; i < reviews.GetCount(); i++)
		if(reviews[i].kind == kind && reviews[i].status == COMPANY_REVIEW_REQUESTED) {
			out = reviews[i];
			return true;
		}
	return false;
}

bool CompanyWorkflow::UpsertReview(const ReviewRecord& review, String& error)
{
	return s_store->SaveReview(review, error);
}

bool CompanyWorkflow::Audit(const String& action, const String& target_type, const String& target_id,
                            const String& prior_status, const String& new_status,
                            const WebAuthIdentity& actor, const String& reason,
                            const String& idempotency_key, String* event_id_out)
{
	CompanyStore* store = GetStore();
	if(!store)
		return false;
	String error;
	bool ok = store->RecordAudit(actor.user_id, actor.token_id, actor.actor_type, actor.roles,
	                             action, target_type, target_id, prior_status, new_status,
	                             reason, idempotency_key, error);
	if(!ok) {
		// Fail loudly: a mutation without its audit entry is an integrity failure.
		Cerr() << "AUDIT FAILURE: action=" << action << " target=" << target_id << ": " << error << "\n";
	}
	if(ok && event_id_out)
		*event_id_out = store->GetAudit().IsEmpty() ? String() : store->GetAudit().Top().event_id;
	return ok;
}

// ---------------------------------------------------------------------------
// Proposal lifecycle
// ---------------------------------------------------------------------------

CompanyWorkflowResult CompanyWorkflow::CreateProposal(const WebAuthIdentity& actor, const String& title,
                                                      const String& description, const String& repository,
                                                      double estimated_hours,
                                                      const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!CanWriteProposals(actor))
		return Fail("proposals:write scope required");

	CompanyStore* store = GetStore();

	// Fast path: already processed. A replay is answered from the prior
	// result without re-validating the body.
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	if(TrimBoth(title).IsEmpty())
		return Fail("title is required");
	if(!s_store->IsRepositoryAllowed(repository))
		return Fail("repository is not on the allowlist: " + repository);
	if(estimated_hours <= 0)
		return Fail("estimated workload (hours) is required");
	const CompanyConfig& cfg0 = s_store->GetConfig();
	int max_hours = cfg0.reward_policy.Find("max_hours") >= 0 ? (int)cfg0.reward_policy["max_hours"] : 40;
	if(max_hours > 0 && estimated_hours > max_hours)
		return Fail("estimated workload " + CompanyFormatNumber(estimated_hours) + " h exceeds the policy maximum " + AsString(max_hours) + " h");

	ProposalRecord p;
	p.id = CompanyGenerateId("prp");
	p.author_user_id = actor.user_id;
	p.author_token_id = actor.token_id;
	p.author_type = actor.IsAgent() ? COMPANY_ACTOR_AGENT : COMPANY_ACTOR_HUMAN;
	p.title = TrimBoth(title);
	p.description = description;
	p.repository = repository;
	p.estimated_hours = estimated_hours;
	p.status = COMPANY_PROPOSAL_PROPOSED;
	p.created_at = GetSysTime();
	p.updated_at = GetSysTime();

	String error;
	if(!store->SaveProposal(p, error))
		return Fail("cannot save proposal: " + error);

	if(!Audit(COMPANY_AUD_PROPOSAL_CREATE, "proposal", p.id, "", COMPANY_PROPOSAL_PROPOSED,
	          actor, description, idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("proposal_id", p.id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::DecideProposal(const WebAuthIdentity& actor, const String& proposal_id,
                                                      const String& action, const String& reason,
                                                      const String& assign_contributor_user_id,
                                                      const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	// Reviewer decisions are human-only: agents cannot accept proposals,
	// including their own.
	if(!IsHumanWithRole(actor, COMPANY_ROLE_REVIEWER))
		return Fail("human reviewer role required");
	if(action != COMPANY_ACTION_ACCEPT && action != COMPANY_ACTION_REJECT && action != COMPANY_ACTION_CHANGES)
		return Fail("unknown action: " + action);
	if(action != COMPANY_ACTION_ACCEPT && TrimBoth(reason).IsEmpty())
		return Fail("reason is required for reject/changes decisions");

	CompanyStore* store = GetStore();

	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	ProposalRecord current;
	if(!store->FindProposal(proposal_id, current))
		return Fail("proposal not found: " + proposal_id);
	if(current.status != COMPANY_PROPOSAL_PROPOSED && current.status != COMPANY_PROPOSAL_CHANGES)
		return Fail("proposal is not open for decisions (status: " + current.status + ")");

	// Pre-fetch users outside the locked update: store accessors must never be
	// called inside the update callback (nested lock = deadlock).
	Vector<UserRecord> users = store->GetUsers();
	auto FindUserLocal = [&users](const String& id, UserRecord& out) -> bool {
		for(int i = 0; i < users.GetCount(); i++)
			if(users[i].id == id) {
				out = users[i];
				return true;
			}
		return false;
	};

	String new_status;
	if(action == COMPANY_ACTION_ACCEPT)
		new_status = COMPANY_PROPOSAL_ACCEPTED;
	else if(action == COMPANY_ACTION_REJECT)
		new_status = COMPANY_PROPOSAL_REJECTED;
	else
		new_status = COMPANY_PROPOSAL_CHANGES;

	if(!IsValidProposalTransition(current.status, new_status))
		return Fail("invalid proposal transition " + current.status + " -> " + new_status);

	ProposalRecord updated;
	String error;
	int applied = store->UpdateProposalWithDedup(proposal_id, idempotency_key,
		[&](ProposalRecord& p, String& merr) {
			if(p.status != current.status) {
				merr = "proposal changed concurrently (status: " + p.status + ")";
				return;
			}
			p.status = new_status;
			p.reviewer_decision = MakeDecision(action, actor, reason);
			if(action == COMPANY_ACTION_ACCEPT) {
				// Accepting requires a contributor assignment; default to the
				// author when the author is a human contributor.
				String assignee = assign_contributor_user_id;
				if(assignee.IsEmpty())
					assignee = p.author_user_id;
				if(assignee.IsEmpty()) {
					merr = "accepted proposals require an assigned contributor";
					return;
				}
				UserRecord assignee_user;
				if(!FindUserLocal(assignee, assignee_user)) {
					merr = "assigned contributor does not exist: " + assignee;
					return;
				}
				if(!assignee_user.enabled) {
					merr = "assigned contributor is disabled";
					return;
				}
				p.assigned_contributor_user_id = assignee_user.id;
			}
		}, updated, error);

	if(applied < 0)
		return Fail(error);
	if(applied == 0) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	// Accepted proposals become implementation work items.
	if(new_status == COMPANY_PROPOSAL_ACCEPTED) {
		WorkItemRecord w;
		w.id = CompanyGenerateId("wrk");
		w.proposal_id = updated.id;
		w.contributor_user_id = updated.assigned_contributor_user_id;
		w.contributor_token_id = updated.assigned_contributor_token_id;
		w.contributor_type = COMPANY_ACTOR_HUMAN;
		w.repository = updated.repository;
		w.required_checks.Add("build");
		w.required_checks.Add("test");
		w.state = COMPANY_STATE_IMPLEMENTATION;
		w.created_at = GetSysTime();
		w.updated_at = GetSysTime();
		String werr;
		if(!store->SaveWorkItem(w, werr))
			return Fail("proposal accepted but work item creation failed: " + werr);
		r.payload.Set("work_item_id", w.id);
	}

	String audit_action = action == COMPANY_ACTION_ACCEPT ? COMPANY_AUD_PROPOSAL_ACCEPT
	                     : action == COMPANY_ACTION_REJECT ? COMPANY_AUD_PROPOSAL_REJECT
	                     : COMPANY_AUD_PROPOSAL_CHANGES;
	if(!Audit(audit_action, "proposal", updated.id, current.status, new_status,
	          actor, reason, idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("proposal_id", updated.id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::ReviseProposal(const WebAuthIdentity& actor, const String& proposal_id,
                                                      const String& new_description, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!actor.authenticated)
		return Fail("authentication required");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	ProposalRecord current;
	if(!store->FindProposal(proposal_id, current))
		return Fail("proposal not found: " + proposal_id);
	bool is_author = (!current.author_user_id.IsEmpty() && current.author_user_id == actor.user_id) ||
	                 (!current.author_token_id.IsEmpty() && current.author_token_id == actor.token_id);
	if(!is_author && !IsAdminHuman(actor))
		return Fail("only the proposal author (or an admin) can revise it");
	if(current.status != COMPANY_PROPOSAL_CHANGES)
		return Fail("proposal is not in changes_requested state");

	ProposalRecord updated;
	String error;
	int applied = store->UpdateProposalWithDedup(proposal_id, idempotency_key,
		[&](ProposalRecord& p, String& merr) {
			if(!new_description.IsEmpty())
				p.description = new_description;
			p.status = COMPANY_PROPOSAL_PROPOSED;
		}, updated, error);

	if(applied < 0)
		return Fail(error);
	if(applied == 0) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	if(!Audit(COMPANY_AUD_PROPOSAL_REVISE, "proposal", updated.id, COMPANY_PROPOSAL_CHANGES,
	          COMPANY_PROPOSAL_PROPOSED, actor, "Revised after changes requested", idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("proposal_id", updated.id);
	return r;
}

// ---------------------------------------------------------------------------
// Work item lifecycle
// ---------------------------------------------------------------------------

CompanyWorkflowResult CompanyWorkflow::BindPullRequest(const WebAuthIdentity& actor, const String& work_item_id,
                                                       const String& repository, int pr_number,
                                                       const String& pr_url, const String& branch,
                                                       const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!actor.authenticated)
		return Fail("authentication required");
	if(pr_number <= 0)
		return Fail("valid PR number is required");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	WorkItemRecord current;
	if(!store->FindWorkItem(work_item_id, current))
		return Fail("work item not found: " + work_item_id);
	if(!CanManageWork(actor, current))
		return Fail("only the assigned contributor or an admin can bind the pull request");
	if(current.state != COMPANY_STATE_IMPLEMENTATION && current.state != COMPANY_STATE_CHANGES)
		return Fail("cannot bind a PR in state " + current.state);
	if(!repository.IsEmpty() && repository != current.repository)
		return Fail("repository does not match the work item repository");

	WorkItemRecord updated;
	String error;
	int applied = store->UpdateWorkItemWithDedup(work_item_id, idempotency_key,
		[&](WorkItemRecord& w, String& merr) {
			w.pr_number = pr_number;
			w.pr_url = pr_url;
			w.branch = branch;
			if(!repository.IsEmpty())
				w.repository = repository;
		}, updated, error);

	if(applied < 0)
		return Fail(error);
	if(applied == 0) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	if(!Audit(COMPANY_AUD_WORK_BIND, "work_item", updated.id, current.state, updated.state,
	          actor, "PR #" + AsString(pr_number) + " bound", idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("work_item_id", updated.id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::RequestMechanicalReview(const WebAuthIdentity& actor,
                                                               const String& work_item_id,
                                                               const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!actor.authenticated)
		return Fail("authentication required");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	WorkItemRecord current;
	if(!store->FindWorkItem(work_item_id, current))
		return Fail("work item not found: " + work_item_id);
	if(!CanManageWork(actor, current))
		return Fail("only the assigned contributor or an admin can request mechanical review");
	if(current.pr_number <= 0)
		return Fail("a pull request must be bound before requesting mechanical review");
	if(current.state != COMPANY_STATE_IMPLEMENTATION && current.state != COMPANY_STATE_CHANGES)
		return Fail("cannot request mechanical review in state " + current.state);

	String error;
	WorkItemRecord updated;
	int applied = store->UpdateWorkItemWithDedup(work_item_id, idempotency_key,
		[&](WorkItemRecord& w, String& merr) {
			if(w.state != current.state) {
				merr = "work item changed concurrently";
				return;
			}
			w.state = COMPANY_STATE_MECHANICAL;
		}, updated, error);

	if(applied < 0)
		return Fail(error);
	if(applied == 0) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	// Open (or reuse) the mechanical review record.
	ReviewRecord rev;
	rev.work_item_id = updated.id;
	rev.proposal_id = updated.proposal_id;
	rev.kind = "mechanical";
	rev.actor_type = COMPANY_ACTOR_SYSTEM;
	rev.status = COMPANY_REVIEW_REQUESTED;
	rev.evidence.Set("requested_by", actor.user_id);
	rev.evidence.Set("requested_by_token", actor.token_id);
	rev.evidence.Set("source", "contributor");
	rev.created_at = GetSysTime();
	rev.updated_at = GetSysTime();
	ReviewRecord existing;
	if(FindOpenReview(updated.id, "mechanical", existing))
		rev.id = existing.id;
	else
		rev.id = CompanyGenerateId("rev");
	String rerr;
	if(!UpsertReview(rev, rerr))
		return Fail("state advanced but review record failed: " + rerr);

	if(!Audit(COMPANY_AUD_WORK_MECH_REQUEST, "work_item", updated.id, current.state, COMPANY_STATE_MECHANICAL,
	          actor, "Mechanical review requested", idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("review_id", rev.id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::RecordMechanicalResult(const WebAuthIdentity& actor,
                                                              const String& work_item_id, bool passed,
                                                              const ValueMap& evidence, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!CanRecordMechanical(actor))
		return Fail("callback:write scope (automation) or admin role required");
	if(evidence.Find("run_id") < 0 || IsNull(evidence["run_id"]) || ((String)evidence["run_id"]).IsEmpty())
		return Fail("evidence.run_id is required");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	WorkItemRecord current;
	if(!store->FindWorkItem(work_item_id, current))
		return Fail("work item not found: " + work_item_id);
	if(current.state != COMPANY_STATE_MECHANICAL)
		return Fail("work item is not in mechanical_review state (state: " + current.state + ")");

	String new_state = passed ? COMPANY_STATE_OPERATOR : COMPANY_STATE_IMPLEMENTATION;

	String error;
	WorkItemRecord updated;
	int applied = store->UpdateWorkItemWithDedup(work_item_id, idempotency_key,
		[&](WorkItemRecord& w, String& merr) {
			if(w.state != COMPANY_STATE_MECHANICAL) {
				merr = "work item changed concurrently (state: " + w.state + ")";
				return;
			}
			w.state = new_state;
		}, updated, error);

	if(applied < 0)
		return Fail(error);
	if(applied == 0) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	// Close (or create) the mechanical review with the recorded evidence.
	ReviewRecord rev;
	ReviewRecord existing;
	if(FindOpenReview(updated.id, "mechanical", existing))
		rev = existing;
	else {
		rev.id = CompanyGenerateId("rev");
		rev.work_item_id = updated.id;
		rev.proposal_id = updated.proposal_id;
		rev.kind = "mechanical";
		rev.created_at = GetSysTime();
		rev.status = COMPANY_REVIEW_REQUESTED;
	}
	rev.status = passed ? COMPANY_REVIEW_PASSED : COMPANY_REVIEW_FAILED;
	rev.actor_user_id = actor.user_id;
	rev.actor_token_id = actor.token_id;
	rev.actor_type = actor.actor_type;
	rev.evidence = evidence;
	ValueMap d;
	d.Set("action", passed ? "mechanical_pass" : "mechanical_fail");
	d.Set("actor_user_id", actor.user_id);
	d.Set("actor_token_id", actor.token_id);
	d.Set("actor_type", actor.actor_type);
	d.Set("reason", (String)evidence.Get("findings", ""));
	d.Set("at", CompanyTimeIso(GetSysTime()));
	rev.decision_history.Add(d);
	rev.updated_at = GetSysTime();
	String rerr;
	if(!UpsertReview(rev, rerr))
		return Fail("state advanced but review record failed: " + rerr);

	if(!Audit(passed ? COMPANY_AUD_MECH_PASS : COMPANY_AUD_MECH_FAIL, "review", rev.id,
	          COMPANY_REVIEW_REQUESTED, rev.status, actor,
	          (String)evidence.Get("findings", "mechanical result"), idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("review_id", rev.id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::RequestOperatorReview(const WebAuthIdentity& actor,
                                                             const String& work_item_id,
                                                             const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!actor.authenticated)
		return Fail("authentication required");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	WorkItemRecord current;
	if(!store->FindWorkItem(work_item_id, current))
		return Fail("work item not found: " + work_item_id);
	if(!CanManageWork(actor, current))
		return Fail("only the assigned contributor or an admin can request operator review");
	if(current.state != COMPANY_STATE_OPERATOR)
		return Fail("operator review can be requested only after mechanical review passed");

	ReviewRecord rev;
	if(FindOpenReview(current.id, "operator", rev)) {
		// Already requested: idempotent no-op with the caller's key recorded.
		if(!idempotency_key.IsEmpty()) {
			String err;
			store->MarkWorkItemEventProcessed(current.id, idempotency_key, err);
		}
		r.ok = true;
		r.payload.Set("review_id", rev.id);
		return r;
	}

	rev.id = CompanyGenerateId("rev");
	rev.work_item_id = current.id;
	rev.proposal_id = current.proposal_id;
	rev.kind = "operator";
	rev.actor_type = COMPANY_ACTOR_SYSTEM;
	rev.status = COMPANY_REVIEW_REQUESTED;
	rev.evidence.Set("requested_by", actor.user_id);
	rev.evidence.Set("requested_by_token", actor.token_id);
	rev.evidence.Set("source", "contributor");
	rev.created_at = GetSysTime();
	rev.updated_at = GetSysTime();
	String rerr;
	if(!UpsertReview(rev, rerr))
		return Fail("cannot create operator review record: " + rerr);

	if(!idempotency_key.IsEmpty()) {
		String err;
		store->MarkWorkItemEventProcessed(current.id, idempotency_key, err);
	}
	if(!Audit(COMPANY_AUD_WORK_OP_REQUEST, "work_item", current.id, current.state, current.state,
	          actor, "Operator review requested", idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("review_id", rev.id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::DecideOperatorReview(const WebAuthIdentity& actor,
                                                            const String& work_item_id, const String& action,
                                                            const String& reason, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	// Operator Review is a human responsibility: scoped automation tokens can
	// never approve, reject, or request changes.
	if(!IsHumanWithRole(actor, COMPANY_ROLE_REVIEWER))
		return Fail("human reviewer role required for operator review");
	if(action != COMPANY_OP_ACTION_APPROVE && action != COMPANY_OP_ACTION_REJECT && action != COMPANY_OP_ACTION_CHANGES)
		return Fail("unknown operator action: " + action);
	if(action != COMPANY_OP_ACTION_APPROVE && TrimBoth(reason).IsEmpty())
		return Fail("reason is required for reject/changes decisions");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	WorkItemRecord current;
	if(!store->FindWorkItem(work_item_id, current))
		return Fail("work item not found: " + work_item_id);

	// No self-approval: the operator must not be the item's contributor.
	if(!current.contributor_user_id.IsEmpty() && current.contributor_user_id == actor.user_id)
		return Fail("operator review decision cannot be made by the item's contributor");
	if(current.state != COMPANY_STATE_OPERATOR)
		return Fail("work item is not awaiting operator review (state: " + current.state + ")");

	ReviewRecord open;
	if(!FindOpenReview(current.id, "operator", open))
		return Fail("no open operator review for this work item");

	String new_state;
	if(action == COMPANY_OP_ACTION_APPROVE)
		new_state = COMPANY_STATE_COMPLETED;
	else if(action == COMPANY_OP_ACTION_REJECT)
		new_state = COMPANY_STATE_REJECTED;
	else
		new_state = COMPANY_STATE_CHANGES;

	String error;
	WorkItemRecord updated;
	int applied = store->UpdateWorkItemWithDedup(work_item_id, idempotency_key,
		[&](WorkItemRecord& w, String& merr) {
			if(w.state != COMPANY_STATE_OPERATOR) {
				merr = "work item changed concurrently (state: " + w.state + ")";
				return;
			}
			w.state = new_state;
		}, updated, error);

	if(applied < 0)
		return Fail(error);
	if(applied == 0) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	open.status = action == COMPANY_OP_ACTION_APPROVE ? COMPANY_REVIEW_APPROVED
	             : action == COMPANY_OP_ACTION_REJECT ? COMPANY_REVIEW_REJECTED
	             : COMPANY_REVIEW_CHANGES;
	open.actor_user_id = actor.user_id;
	open.actor_type = actor.actor_type;
	ValueMap d = MakeDecision(action, actor, reason);
	open.decision_history.Add(d);
	open.updated_at = GetSysTime();
	String rerr;
	if(!UpsertReview(open, rerr))
		return Fail("state advanced but review record failed: " + rerr);

	String audit_action = action == COMPANY_OP_ACTION_APPROVE ? COMPANY_AUD_OP_APPROVE
	                     : action == COMPANY_OP_ACTION_REJECT ? COMPANY_AUD_OP_REJECT
	                     : COMPANY_AUD_OP_CHANGES;
	if(!Audit(audit_action, "review", open.id, COMPANY_STATE_OPERATOR, new_state,
	          actor, reason, idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("review_id", open.id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::ResumeWork(const WebAuthIdentity& actor, const String& work_item_id,
                                                  const String& reason, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!actor.authenticated)
		return Fail("authentication required");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	WorkItemRecord current;
	if(!store->FindWorkItem(work_item_id, current))
		return Fail("work item not found: " + work_item_id);
	if(!CanManageWork(actor, current))
		return Fail("only the assigned contributor or an admin can resume work");
	if(current.state != COMPANY_STATE_CHANGES)
		return Fail("work item is not in changes_requested state");

	String error;
	WorkItemRecord updated;
	int applied = store->UpdateWorkItemWithDedup(work_item_id, idempotency_key,
		[&](WorkItemRecord& w, String& merr) {
			if(w.state != COMPANY_STATE_CHANGES) {
				merr = "work item changed concurrently";
				return;
			}
			w.state = COMPANY_STATE_IMPLEMENTATION;
		}, updated, error);

	if(applied < 0)
		return Fail(error);
	if(applied == 0) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	if(!Audit(COMPANY_AUD_WORK_RESUME, "work_item", updated.id, COMPANY_STATE_CHANGES,
	          COMPANY_STATE_IMPLEMENTATION, actor, reason, idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	return r;
}

CompanyWorkflowResult CompanyWorkflow::BlockWorkItem(const WebAuthIdentity& actor, const String& work_item_id,
                                                     const String& reason, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsHumanWithAnyRole(actor, CompanyAdminReviewerRoles()))
		return Fail("human admin or reviewer role required to block");
	if(TrimBoth(reason).IsEmpty())
		return Fail("a reason is required to block a work item");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	WorkItemRecord current;
	if(!store->FindWorkItem(work_item_id, current))
		return Fail("work item not found: " + work_item_id);
	if(!IsValidWorkTransition(current.state, COMPANY_STATE_BLOCKED))
		return Fail("cannot block a work item in state " + current.state);

	String error;
	WorkItemRecord updated;
	int applied = store->UpdateWorkItemWithDedup(work_item_id, idempotency_key,
		[&](WorkItemRecord& w, String& merr) {
			if(!IsValidWorkTransition(w.state, COMPANY_STATE_BLOCKED)) {
				merr = "cannot block from state " + w.state;
				return;
			}
			w.state = COMPANY_STATE_BLOCKED;
		}, updated, error);

	if(applied < 0)
		return Fail(error);
	if(applied == 0) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	if(!Audit(COMPANY_AUD_WORK_BLOCK, "work_item", updated.id, current.state, COMPANY_STATE_BLOCKED,
	          actor, reason, idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	return r;
}

CompanyWorkflowResult CompanyWorkflow::UnblockWorkItem(const WebAuthIdentity& actor, const String& work_item_id,
                                                       const String& reason, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("human admin role required to unblock");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	WorkItemRecord current;
	if(!store->FindWorkItem(work_item_id, current))
		return Fail("work item not found: " + work_item_id);
	if(current.state != COMPANY_STATE_BLOCKED)
		return Fail("work item is not blocked");

	String error;
	WorkItemRecord updated;
	int applied = store->UpdateWorkItemWithDedup(work_item_id, idempotency_key,
		[&](WorkItemRecord& w, String& merr) {
			if(w.state != COMPANY_STATE_BLOCKED) {
				merr = "work item changed concurrently";
				return;
			}
			w.state = COMPANY_STATE_IMPLEMENTATION;
		}, updated, error);

	if(applied < 0)
		return Fail(error);
	if(applied == 0) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	if(!Audit(COMPANY_AUD_WORK_UNBLOCK, "work_item", updated.id, COMPANY_STATE_BLOCKED,
	          COMPANY_STATE_IMPLEMENTATION, actor, reason, idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	return r;
}

// ---------------------------------------------------------------------------
// Compensation & ledger
// ---------------------------------------------------------------------------

CompanyWorkflowResult CompanyWorkflow::IssueCompensation(const WebAuthIdentity& actor, const String& work_item_id,
                                                         const String& reason, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	// Compensation is a human admin/reviewer responsibility; scoped agents
	// never issue compensation and nobody compensates their own work.
	if(!IsHumanWithAnyRole(actor, CompanyAdminReviewerRoles()))
		return Fail("human admin or reviewer role required to issue compensation");

	CompanyStore* store = GetStore();

	WorkItemRecord current;
	if(!store->FindWorkItem(work_item_id, current))
		return Fail("work item not found: " + work_item_id);
	if(!current.contributor_user_id.IsEmpty() && current.contributor_user_id == actor.user_id)
		return Fail("compensation cannot be issued by the item's contributor");
	if(current.state == COMPANY_STATE_COMPENSATED) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	if(current.state != COMPANY_STATE_COMPLETED)
		return Fail("only completed work items can be compensated (state: " + current.state + ")");

	ProposalRecord proposal;
	if(!store->FindProposal(current.proposal_id, proposal))
		return Fail("proposal not found: " + current.proposal_id);
	// Compensation input: the estimated workload times the contributor's
	// hourly rate. Fail closed when the contributor has no rate set.
	double estimated_hours = proposal.estimated_hours;
	if(estimated_hours <= 0)
		return Fail("proposal has no estimated workload (hours)");
	UserRecord contributor;
	if(!store->FindUser(current.contributor_user_id, contributor))
		return Fail("contributor not found: " + current.contributor_user_id);
	if(contributor.hourly_rate <= 0)
		return Fail("contributor has no hourly rate set; an admin must set it before compensation");
	const CompanyConfig& cfg = store->GetConfig();
	int max_amount = cfg.reward_policy.Find("max_amount") >= 0 ? (int)cfg.reward_policy["max_amount"] : 5000;
	int amount = (int)(estimated_hours * contributor.hourly_rate + 0.5);
	if(max_amount > 0 && amount > max_amount)
		amount = max_amount;

	// Stable idempotency key: one completion creates at most one credit.
	String key = CompanyCompensationKey(work_item_id);

	LedgerEntry entry;
	entry.entry_id = CompanyGenerateId("lgr");
	entry.idempotency_key = key;
	entry.type = COMPANY_LEDGER_CREDIT;
	entry.amount = amount;
	entry.user_id = current.contributor_user_id;
	entry.work_item_id = current.id;
	entry.reason = TrimBoth(reason).IsEmpty()
	    ? ("Completion of proposal " + current.proposal_id + " ("
	       + CompanyFormatNumber(estimated_hours) + " h x " + CompanyFormatNumber(contributor.hourly_rate) + " CR/h)")
	    : reason;
	entry.actor_user_id = actor.user_id;
	entry.actor_type = actor.actor_type;
	entry.created_at = GetSysTime();

	String lerr;
	bool duplicate = false;
	if(!store->AppendLedger(entry, lerr, &duplicate))
		return Fail("ledger append failed: " + lerr);
	if(duplicate) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	// Move the work item to compensated (state-guarded, retry-safe).
	String error;
	WorkItemRecord updated;
	int applied = store->UpdateWorkItemWithDedup(work_item_id, key + ":state",
		[&](WorkItemRecord& w, String& merr) {
			if(w.state == COMPANY_STATE_COMPENSATED)
				return; // already moved (retry); not an error
			if(w.state != COMPANY_STATE_COMPLETED) {
				merr = "work item is not completed (state: " + w.state + ")";
				return;
			}
			w.state = COMPANY_STATE_COMPENSATED;
		}, updated, error);
	if(applied < 0)
		return Fail("credit recorded but state transition failed: " + error);

	if(!Audit(COMPANY_AUD_COMPENSATE, "ledger", entry.entry_id, COMPANY_STATE_COMPLETED,
	          COMPANY_STATE_COMPENSATED, actor, entry.reason, key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("entry_id", entry.entry_id);
	r.payload.Set("amount", amount);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::LedgerCorrection(const WebAuthIdentity& actor, const String& user_id,
                                                        int amount, const String& reason,
                                                        const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("human admin role required for ledger corrections");
	if(TrimBoth(reason).IsEmpty())
		return Fail("a reason is mandatory for ledger corrections");
	if(amount == 0)
		return Fail("correction amount must be non-zero");
	UserRecord user;
	if(!s_store->FindUser(user_id, user))
		return Fail("user not found: " + user_id);

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	LedgerEntry entry;
	entry.entry_id = CompanyGenerateId("lgr");
	entry.idempotency_key = idempotency_key.IsEmpty() ? CompanyGenerateId("key") : idempotency_key;
	entry.type = COMPANY_LEDGER_CORRECTION;
	entry.amount = amount;
	entry.user_id = user_id;
	entry.reason = reason;
	entry.actor_user_id = actor.user_id;
	entry.actor_type = actor.actor_type;
	entry.created_at = GetSysTime();

	String lerr;
	bool duplicate = false;
	if(!store->AppendLedger(entry, lerr, &duplicate))
		return Fail("ledger append failed: " + lerr);
	if(duplicate) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	if(!Audit(COMPANY_AUD_LEDGER_CORRECTION, "ledger", entry.entry_id, "", "",
	          actor, reason, entry.idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("entry_id", entry.entry_id);
	return r;
}

// ---------------------------------------------------------------------------
// Administration (human admin only)
// ---------------------------------------------------------------------------

CompanyWorkflowResult CompanyWorkflow::SetUserRoles(const WebAuthIdentity& actor, const String& user_id,
                                                    const Vector<String>& roles, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("human admin role required");
	if(roles.IsEmpty())
		return Fail("at least one role is required");

	for(int i = 0; i < roles.GetCount(); i++) {
		bool valid = false;
		Vector<String> all = CompanyAllRoles();
		for(int j = 0; j < all.GetCount(); j++)
			if(all[j] == roles[i])
				valid = true;
		if(!valid)
			return Fail("unknown role: " + roles[i]);
	}

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	UserRecord user;
	if(!store->FindUser(user_id, user))
		return Fail("user not found: " + user_id);

	UserRecord updated = user;
	updated.roles = clone(roles);
	updated.updated_at = GetSysTime();
	String error;
	if(!store->SaveUser(updated, error))
		return Fail("cannot save user: " + error);

	String role_list = Join(roles, ",");
	if(!Audit(COMPANY_AUD_USER_ROLES, "user", user_id, Join(user.roles, ","), role_list,
	          actor, role_list, idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	return r;
}

CompanyWorkflowResult CompanyWorkflow::SetUserEnabled(const WebAuthIdentity& actor, const String& user_id,
                                                      bool enabled, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("human admin role required");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	UserRecord user;
	if(!store->FindUser(user_id, user))
		return Fail("user not found: " + user_id);
	UserRecord updated = user;
	updated.enabled = enabled;
	updated.updated_at = GetSysTime();
	String error;
	if(!store->SaveUser(updated, error))
		return Fail("cannot save user: " + error);
	if(!Audit(enabled ? "user.enable" : "user.disable", "user", user_id, "", "",
	          actor, "", idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	return r;
}

CompanyWorkflowResult CompanyWorkflow::CreateUser(const WebAuthIdentity& actor, const String& username,
                                                  const String& display_name, const Vector<String>& roles,
                                                  const String& password, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("human admin role required");
	if(TrimBoth(username).IsEmpty())
		return Fail("username is required");
	if(roles.IsEmpty())
		return Fail("at least one role is required");

	for(int i = 0; i < roles.GetCount(); i++) {
		bool valid = false;
		Vector<String> all = CompanyAllRoles();
		for(int j = 0; j < all.GetCount(); j++)
			if(all[j] == roles[i])
				valid = true;
		if(!valid)
			return Fail("unknown role: " + roles[i]);
	}

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	UserRecord existing;
	if(store->FindUserByUsername(username, existing))
		return Fail("username already exists: " + username);

	UserRecord user;
	user.id = CompanyGenerateId("usr");
	user.username = TrimBoth(username);
	user.display_name = display_name.IsEmpty() ? user.username : display_name;
	user.enabled = true;
	user.roles = clone(roles);
	user.password_hash = password.IsEmpty() ? String() : WebAuthHashPassword(password);
	user.created_at = GetSysTime();
	user.updated_at = GetSysTime();
	String error;
	if(!store->SaveUser(user, error))
		return Fail("cannot create user: " + error);

	if(!Audit(COMPANY_AUD_USER_CREATE, "user", user.id, "", Join(roles, ","),
	          actor, user.username, idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("user_id", user.id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::CreateAgentToken(const WebAuthIdentity& actor, const String& name,
                                                        const String& owner_user_id, const Vector<String>& scopes,
                                                        const Time& expires_at, const String& idempotency_key,
                                                        String& raw_token_out)
{
	CompanyWorkflowResult r;
	raw_token_out.Clear();
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("human admin role required");
	if(TrimBoth(name).IsEmpty())
		return Fail("token name is required");
	if(scopes.IsEmpty())
		return Fail("at least one scope is required");

	Vector<String> all = CompanyAllScopes();
	for(int i = 0; i < scopes.GetCount(); i++) {
		bool valid = false;
		for(int j = 0; j < all.GetCount(); j++)
			if(all[j] == scopes[i])
				valid = true;
		if(!valid)
			return Fail("unknown scope: " + scopes[i]);
	}

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	if(!owner_user_id.IsEmpty()) {
		UserRecord owner;
		if(!store->FindUser(owner_user_id, owner))
			return Fail("owner user not found: " + owner_user_id);
	}

	// The raw token is returned exactly once; only its peppered hash persists.
	String raw = CompanyGenerateAgentToken();
	AgentTokenRecord token;
	token.id = CompanyGenerateId("tok");
	token.name = TrimBoth(name);
	token.owner_user_id = owner_user_id;
	token.token_hash = CompanyTokenHash(store->GetSecret(), raw);
	token.scopes = clone(scopes);
	token.created_at = GetSysTime();
	token.expires_at = expires_at;
	String error;
	if(!store->SaveToken(token, error))
		return Fail("cannot save token: " + error);

	if(!Audit(COMPANY_AUD_TOKEN_CREATE, "token", token.id, "", Join(scopes, ","),
	          actor, token.name, idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("token_id", token.id);
	r.payload.Set("raw_token", raw);
	raw_token_out = raw;
	return r;
}

CompanyWorkflowResult CompanyWorkflow::RevokeAgentToken(const WebAuthIdentity& actor, const String& token_id,
                                                        const String& reason, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("human admin role required");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	AgentTokenRecord token;
	if(!store->FindToken(token_id, token))
		return Fail("token not found: " + token_id);
	if(!IsNull(token.revoked_at)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	AgentTokenRecord updated = token;
	updated.revoked_at = GetSysTime();
	String error;
	if(!store->SaveToken(updated, error))
		return Fail("cannot revoke token: " + error);

	if(!Audit(COMPANY_AUD_TOKEN_REVOKE, "token", token_id, "active", "revoked",
	          actor, reason, idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	return r;
}

CompanyWorkflowResult CompanyWorkflow::SetUserHourlyRate(const WebAuthIdentity& actor, const String& user_id,
                                                         double hourly_rate, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("human admin role required");
	if(hourly_rate < 0 || hourly_rate > 100000)
		return Fail("hourly rate out of range");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	UserRecord user;
	if(!store->FindUser(user_id, user))
		return Fail("user not found: " + user_id);
	UserRecord updated = user;
	updated.hourly_rate = hourly_rate;
	updated.updated_at = GetSysTime();
	String error;
	if(!store->SaveUser(updated, error))
		return Fail("cannot save user: " + error);
	if(!Audit("user.hourly_rate", "user", user_id, CompanyFormatNumber(user.hourly_rate),
	          CompanyFormatNumber(hourly_rate), actor, "", idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	return r;
}

CompanyWorkflowResult CompanyWorkflow::UpdateConfigPolicy(const WebAuthIdentity& actor, const ValueMap& reward_policy,
                                                          const ValueMap& review_policy, const ValueMap& agent_reviewer,
                                                          const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("human admin role required");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	CompanyConfig cfg = store->GetConfig();
	if(reward_policy.Find("max_hours") >= 0)
		cfg.reward_policy.Set("max_hours", (int)reward_policy["max_hours"]);
	if(reward_policy.Find("max_amount") >= 0)
		cfg.reward_policy.Set("max_amount", (int)reward_policy["max_amount"]);
	if(reward_policy.Find("currency") >= 0)
		cfg.reward_policy.Set("currency", (String)reward_policy["currency"]);
	if(review_policy.Find("operator_quorum") >= 0)
		cfg.review_policy.Set("operator_quorum", (int)review_policy["operator_quorum"]);
	if(review_policy.Find("mechanical_required") >= 0)
		cfg.review_policy.Set("mechanical_required", (bool)review_policy["mechanical_required"]);
	if(agent_reviewer.Find("enabled") >= 0)
		cfg.agent_reviewer.Set("enabled", (bool)agent_reviewer["enabled"]);
	if(agent_reviewer.Find("command") >= 0)
		cfg.agent_reviewer.Set("command", (String)agent_reviewer["command"]);
	cfg.updated_at = GetSysTime();

	String error;
	if(!store->SetConfig(cfg, error))
		return Fail("cannot save config: " + error);
	if(!Audit(COMPANY_AUD_CONFIG_UPDATE, "config", "policy", "", "",
	          actor, "", idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	return r;
}

CompanyWorkflowResult CompanyWorkflow::SetRepositoryAllowlist(const WebAuthIdentity& actor,
                                                              const Vector<String>& repositories,
                                                              const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("human admin role required");

	CompanyStore* store = GetStore();
	if(!idempotency_key.IsEmpty() && store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}

	// The store owns the live config: updating a copy would never reach the allowlist check.
	Vector<String> old = clone(store->GetConfig().repository_allowlist);
	String error;
	if(!store->SetRepositoryAllowlist(repositories, error))
		return Fail("cannot save config: " + error);
	if(!Audit(COMPANY_AUD_REPOS_UPDATE, "config", "repository_allowlist",
	          Join(old, ","), Join(repositories, ","),
	          actor, "", idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	return r;
}

END_UPP_NAMESPACE
