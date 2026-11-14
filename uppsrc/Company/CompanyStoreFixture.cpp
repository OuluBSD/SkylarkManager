#include "CompanyStore.h"

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// Deterministic test fixture
// ---------------------------------------------------------------------------

static Time FixtureTime(int day, int hour, int minute = 0)
{
	return Time(2026, 1, day, hour, minute, 0);
}

void CompanyStore::SeedTestFixture()
{
	if(!GetUsers().IsEmpty())
		return; // already seeded

	String error;
	Time t0 = FixtureTime(15, 10);

	// ---- Users (low iteration count keeps the fixture fast; production
	// bootstrap uses COMPANY_PBKDF2_ITERATIONS) ----
	UserRecord admin;
	admin.id = "usr-0001"; admin.username = "admin"; admin.display_name = "Site Administrator";
	admin.roles.Add(COMPANY_ROLE_CONTRIBUTOR); admin.roles.Add(COMPANY_ROLE_REVIEWER); admin.roles.Add(COMPANY_ROLE_ADMIN);
	admin.hourly_rate = 50;
	admin.password_hash = WebAuthHashPassword("AdminPass123!", "fixturesalt0001", 1000);
	admin.created_at = t0; admin.updated_at = t0;
	SaveUser(admin, error);

	UserRecord alice;
	alice.id = "usr-0002"; alice.username = "alice"; alice.display_name = "Alice Contributor";
	alice.roles.Add(COMPANY_ROLE_CONTRIBUTOR);
	alice.hourly_rate = 40;
	alice.password_hash = WebAuthHashPassword("AlicePass123!", "fixturesalt0002", 1000);
	alice.created_at = t0; alice.updated_at = t0;
	SaveUser(alice, error);

	UserRecord bob;
	bob.id = "usr-0003"; bob.username = "bob"; bob.display_name = "Bob Reviewer";
	bob.roles.Add(COMPANY_ROLE_REVIEWER);
	bob.hourly_rate = 35;
	bob.password_hash = WebAuthHashPassword("BobPass123!", "fixturesalt0003", 1000);
	bob.created_at = t0; bob.updated_at = t0;
	SaveUser(bob, error);

	UserRecord carol;
	carol.id = "usr-0004"; carol.username = "carol"; carol.display_name = "Carol Mixed";
	carol.roles.Add(COMPANY_ROLE_CONTRIBUTOR); carol.roles.Add(COMPANY_ROLE_REVIEWER);
	carol.hourly_rate = 45;
	carol.password_hash = WebAuthHashPassword("CarolPass123!", "fixturesalt0004", 1000);
	carol.created_at = t0; carol.updated_at = t0;
	SaveUser(carol, error);

	UserRecord agentbot;
	agentbot.id = "usr-0005"; agentbot.username = "agentbot"; agentbot.display_name = "Agent Bot";
	agentbot.roles.Add(COMPANY_ROLE_AUTOMATION); agentbot.roles.Add(COMPANY_ROLE_CONTRIBUTOR);
	agentbot.created_at = t0; agentbot.updated_at = t0;
	SaveUser(agentbot, error);

	// ---- Agent tokens (raw tokens are test constants; only hashes persist) ----
	AgentTokenRecord automation;
	automation.id = "tok-0001"; automation.name = "test-automation"; automation.owner_user_id = agentbot.id;
	automation.token_hash = WebAuthPepperedHash(secret, "company-test-agent-token-0001");
	automation.scopes.Add(COMPANY_SCOPE_PROPOSALS_READ); automation.scopes.Add(COMPANY_SCOPE_PROPOSALS_WRITE);
	automation.scopes.Add(COMPANY_SCOPE_WORK_READ); automation.scopes.Add(COMPANY_SCOPE_WORK_WRITE);
	automation.scopes.Add(COMPANY_SCOPE_REVIEWS_READ); automation.scopes.Add(COMPANY_SCOPE_LEDGER_READ);
	automation.created_at = t0; automation.last_used_at = t0;
	SaveToken(automation, error);

	AgentTokenRecord callback;
	callback.id = "tok-0002"; callback.name = "review-callback"; callback.owner_user_id = admin.id;
	callback.token_hash = WebAuthPepperedHash(secret, "company-test-callback-token-0001");
	callback.scopes.Add(COMPANY_SCOPE_CALLBACK_WRITE);
	callback.created_at = t0; callback.last_used_at = t0;
	SaveToken(callback, error);

	AgentTokenRecord expired;
	expired.id = "tok-0003"; expired.name = "expired-token"; expired.owner_user_id = alice.id;
	expired.token_hash = WebAuthPepperedHash(secret, "company-test-expired-token-0001");
	expired.scopes.Add(COMPANY_SCOPE_PROPOSALS_READ);
	expired.created_at = t0; expired.expires_at = FixtureTime(1, 0);
	SaveToken(expired, error);

	AgentTokenRecord revoked;
	revoked.id = "tok-0004"; revoked.name = "revoked-token"; revoked.owner_user_id = alice.id;
	revoked.token_hash = WebAuthPepperedHash(secret, "company-test-revoked-token-0001");
	revoked.scopes.Add(COMPANY_SCOPE_WORK_READ);
	revoked.created_at = t0; revoked.revoked_at = FixtureTime(14, 12);
	SaveToken(revoked, error);

	// ---- Proposals and work items covering every lifecycle state ----
	ProposalRecord p1;
	p1.id = "prp-0001"; p1.author_user_id = alice.id; p1.author_type = COMPANY_ACTOR_HUMAN;
	p1.title = "Add automated end-to-end test suite"; p1.description = "Cover login, proposal flow, and ledger.";
	p1.repository = "OuluBSD/CompanySkylark"; p1.issue_number = 1; p1.estimated_hours = 8;
	p1.status = COMPANY_PROPOSAL_PROPOSED; p1.created_at = t0; p1.updated_at = t0;
	SaveProposal(p1, error);

	ProposalRecord p2;
	p2.id = "prp-0002"; p2.author_user_id = agentbot.id; p2.author_token_id = automation.id; p2.author_type = COMPANY_ACTOR_AGENT;
	p2.title = "Fix documentation links"; p2.description = "Relative links in README.";
	p2.repository = "OuluBSD/CompanySkylark"; p2.issue_number = 2; p2.estimated_hours = 4;
	p2.status = COMPANY_PROPOSAL_ACCEPTED;
	p2.reviewer_decision.Set("action", "accept"); p2.reviewer_decision.Set("actor_user_id", bob.id);
	p2.reviewer_decision.Set("actor_type", COMPANY_ACTOR_HUMAN); p2.reviewer_decision.Set("reason", "Well scoped.");
	p2.reviewer_decision.Set("at", CompanyTimeIso(t0));
	p2.assigned_contributor_user_id = alice.id;
	p2.created_at = t0; p2.updated_at = t0;
	SaveProposal(p2, error);

	ProposalRecord p3;
	p3.id = "prp-0003"; p3.author_user_id = carol.id; p3.author_type = COMPANY_ACTOR_HUMAN;
	p3.title = "Improve error messages"; p3.description = "Clearer store corruption errors.";
	p3.repository = "OuluBSD/CompanySkylark"; p3.issue_number = 3; p3.estimated_hours = 6;
	p3.status = COMPANY_PROPOSAL_ACCEPTED;
	p3.reviewer_decision.Set("action", "accept"); p3.reviewer_decision.Set("actor_user_id", bob.id);
	p3.reviewer_decision.Set("actor_type", COMPANY_ACTOR_HUMAN); p3.reviewer_decision.Set("reason", "Useful.");
	p3.reviewer_decision.Set("at", CompanyTimeIso(t0));
	p3.assigned_contributor_user_id = carol.id;
	p3.created_at = t0; p3.updated_at = t0;
	SaveProposal(p3, error);

	ProposalRecord p4;
	p4.id = "prp-0004"; p4.author_user_id = alice.id; p4.author_type = COMPANY_ACTOR_HUMAN;
	p4.title = "Add density toggle to dashboard"; p4.description = "Compact/comfortable switch.";
	p4.repository = "OuluBSD/CompanySkylark"; p4.issue_number = 4; p4.estimated_hours = 10;
	p4.status = COMPANY_PROPOSAL_PROPOSED; p4.created_at = t0; p4.updated_at = t0;
	SaveProposal(p4, error);

	ProposalRecord p5;
	p5.id = "prp-0005"; p5.author_user_id = carol.id; p5.author_type = COMPANY_ACTOR_HUMAN;
	p5.title = "Refactor JSON store locking"; p5.description = "Split global lock per collection.";
	p5.repository = "OuluBSD/CompanySkylark"; p5.issue_number = 5; p5.estimated_hours = 16;
	p5.status = COMPANY_PROPOSAL_REJECTED;
	p5.reviewer_decision.Set("action", "reject"); p5.reviewer_decision.Set("actor_user_id", bob.id);
	p5.reviewer_decision.Set("actor_type", COMPANY_ACTOR_HUMAN); p5.reviewer_decision.Set("reason", "Out of scope for this release.");
	p5.reviewer_decision.Set("at", CompanyTimeIso(t0));
	p5.created_at = t0; p5.updated_at = t0;
	SaveProposal(p5, error);

	ProposalRecord p6;
	p6.id = "prp-0006"; p6.author_user_id = alice.id; p6.author_type = COMPANY_ACTOR_HUMAN;
	p6.title = "Add operator review evidence view"; p6.description = "Show checks and findings.";
	p6.repository = "OuluBSD/CompanySkylark"; p6.issue_number = 6; p6.estimated_hours = 5;
	p6.status = COMPANY_PROPOSAL_ACCEPTED;
	p6.reviewer_decision.Set("action", "accept"); p6.reviewer_decision.Set("actor_user_id", bob.id);
	p6.reviewer_decision.Set("actor_type", COMPANY_ACTOR_HUMAN); p6.reviewer_decision.Set("reason", "Needed.");
	p6.reviewer_decision.Set("at", CompanyTimeIso(t0));
	p6.assigned_contributor_user_id = alice.id;
	p6.created_at = t0; p6.updated_at = t0;
	SaveProposal(p6, error);

	ProposalRecord p7;
	p7.id = "prp-0007"; p7.author_user_id = carol.id; p7.author_type = COMPANY_ACTOR_HUMAN;
	p7.title = "Add ledger balance projection"; p7.description = "Recompute balances from entries.";
	p7.repository = "OuluBSD/CompanySkylark"; p7.issue_number = 7; p7.estimated_hours = 7;
	p7.status = COMPANY_PROPOSAL_ACCEPTED;
	p7.reviewer_decision.Set("action", "accept"); p7.reviewer_decision.Set("actor_user_id", bob.id);
	p7.reviewer_decision.Set("actor_type", COMPANY_ACTOR_HUMAN); p7.reviewer_decision.Set("reason", "Core feature.");
	p7.reviewer_decision.Set("at", CompanyTimeIso(t0));
	p7.assigned_contributor_user_id = carol.id;
	p7.created_at = t0; p7.updated_at = t0;
	SaveProposal(p7, error);

	ProposalRecord p8;
	p8.id = "prp-0008"; p8.author_user_id = alice.id; p8.author_type = COMPANY_ACTOR_HUMAN;
	p8.title = "Add audit event search"; p8.description = "Filter audit by actor and action.";
	p8.repository = "OuluBSD/CompanySkylark"; p8.issue_number = 8; p8.estimated_hours = 6;
	p8.status = COMPANY_PROPOSAL_ACCEPTED;
	p8.reviewer_decision.Set("action", "accept"); p8.reviewer_decision.Set("actor_user_id", bob.id);
	p8.reviewer_decision.Set("actor_type", COMPANY_ACTOR_HUMAN); p8.reviewer_decision.Set("reason", "Compliance.");
	p8.reviewer_decision.Set("at", CompanyTimeIso(t0));
	p8.assigned_contributor_user_id = alice.id;
	p8.created_at = t0; p8.updated_at = t0;
	SaveProposal(p8, error);

	// ---- Work items ----
	WorkItemRecord w1;
	w1.id = "wrk-0001"; w1.proposal_id = p2.id; w1.contributor_user_id = alice.id; w1.contributor_type = COMPANY_ACTOR_HUMAN;
	w1.repository = p2.repository; w1.required_checks.Add("build"); w1.required_checks.Add("test");
	w1.state = COMPANY_STATE_IMPLEMENTATION; w1.created_at = t0; w1.updated_at = t0;
	SaveWorkItem(w1, error);

	WorkItemRecord w2;
	w2.id = "wrk-0002"; w2.proposal_id = p3.id; w2.contributor_user_id = carol.id; w2.contributor_type = COMPANY_ACTOR_HUMAN;
	w2.repository = p3.repository; w2.pr_number = 7; w2.pr_url = "https://github.com/OuluBSD/CompanySkylark/pull/7";
	w2.branch = "feature/error-messages"; w2.required_checks.Add("build"); w2.required_checks.Add("test");
	w2.state = COMPANY_STATE_MECHANICAL; w2.processed_event_keys.Add("evt-fixture-mech-req-0002");
	w2.created_at = t0; w2.updated_at = t0;
	SaveWorkItem(w2, error);

	WorkItemRecord w3;
	w3.id = "wrk-0003"; w3.proposal_id = p6.id; w3.contributor_user_id = alice.id; w3.contributor_type = COMPANY_ACTOR_HUMAN;
	w3.repository = p6.repository; w3.pr_number = 8; w3.pr_url = "https://github.com/OuluBSD/CompanySkylark/pull/8";
	w3.branch = "feature/evidence-view"; w3.required_checks.Add("build"); w3.required_checks.Add("test");
	w3.state = COMPANY_STATE_OPERATOR; w3.processed_event_keys.Add("evt-fixture-mech-pass-0003");
	w3.created_at = t0; w3.updated_at = t0;
	SaveWorkItem(w3, error);

	WorkItemRecord w4;
	w4.id = "wrk-0004"; w4.proposal_id = p7.id; w4.contributor_user_id = carol.id; w4.contributor_type = COMPANY_ACTOR_HUMAN;
	w4.repository = p7.repository; w4.pr_number = 9; w4.pr_url = "https://github.com/OuluBSD/CompanySkylark/pull/9";
	w4.branch = "feature/ledger-projection"; w4.required_checks.Add("build"); w4.required_checks.Add("test");
	w4.state = COMPANY_STATE_COMPLETED;
	w4.processed_event_keys.Add("evt-fixture-mech-pass-0004"); w4.processed_event_keys.Add("evt-fixture-op-approve-0004");
	w4.created_at = t0; w4.updated_at = t0;
	SaveWorkItem(w4, error);

	WorkItemRecord w5;
	w5.id = "wrk-0005"; w5.proposal_id = p8.id; w5.contributor_user_id = alice.id; w5.contributor_type = COMPANY_ACTOR_HUMAN;
	w5.repository = p8.repository; w5.pr_number = 10; w5.pr_url = "https://github.com/OuluBSD/CompanySkylark/pull/10";
	w5.branch = "feature/audit-search"; w5.required_checks.Add("build"); w5.required_checks.Add("test");
	w5.state = COMPANY_STATE_COMPENSATED;
	w5.processed_event_keys.Add("evt-fixture-mech-pass-0005"); w5.processed_event_keys.Add("evt-fixture-op-approve-0005");
	w5.processed_event_keys.Add("evt-fixture-compensate-0005");
	w5.created_at = t0; w5.updated_at = t0;
	SaveWorkItem(w5, error);

	// ---- Reviews ----
	ReviewRecord r1;
	r1.id = "rev-0001"; r1.work_item_id = w2.id; r1.proposal_id = p3.id; r1.kind = "mechanical";
	r1.actor_type = COMPANY_ACTOR_SYSTEM; r1.status = COMPANY_REVIEW_REQUESTED;
	r1.evidence.Set("requested_by", carol.id); r1.evidence.Set("source", "contributor");
	r1.created_at = t0; r1.updated_at = t0;
	SaveReview(r1, error);

	ReviewRecord r2;
	r2.id = "rev-0002"; r2.work_item_id = w3.id; r2.proposal_id = p6.id; r2.kind = "mechanical";
	r2.actor_type = COMPANY_ACTOR_AUTOMATION; r2.status = COMPANY_REVIEW_PASSED;
	r2.evidence.Set("run_id", "run-fixture-0003"); r2.evidence.Set("comment_url", "https://github.com/OuluBSD/CompanySkylark/pull/8#issuecomment-1");
	r2.evidence.Set("event_id", "evt-fixture-mech-pass-0003"); r2.evidence.Set("source", "github_actions");
	ValueArray checks;
	ValueMap c1; c1.Set("name", "build"); c1.Set("status", "passed"); checks.Add(c1);
	ValueMap c2; c2.Set("name", "test"); c2.Set("status", "passed"); checks.Add(c2);
	r2.evidence.Set("checks", checks);
	ValueMap d1; d1.Set("action", "mechanical_pass"); d1.Set("actor_type", COMPANY_ACTOR_AUTOMATION);
	d1.Set("reason", "All mechanical checks passed."); d1.Set("at", CompanyTimeIso(t0));
	r2.decision_history.Add(d1);
	r2.created_at = t0; r2.updated_at = t0;
	SaveReview(r2, error);

	ReviewRecord r3;
	r3.id = "rev-0003"; r3.work_item_id = w3.id; r3.proposal_id = p6.id; r3.kind = "operator";
	r3.actor_type = COMPANY_ACTOR_SYSTEM; r3.status = COMPANY_REVIEW_REQUESTED;
	r3.evidence.Set("requested_by", alice.id); r3.evidence.Set("source", "contributor");
	r3.created_at = t0; r3.updated_at = t0;
	SaveReview(r3, error);

	ReviewRecord r4;
	r4.id = "rev-0004"; r4.work_item_id = w4.id; r4.proposal_id = p7.id; r4.kind = "mechanical";
	r4.actor_type = COMPANY_ACTOR_AUTOMATION; r4.status = COMPANY_REVIEW_PASSED;
	r4.evidence.Set("run_id", "run-fixture-0004"); r4.evidence.Set("event_id", "evt-fixture-mech-pass-0004");
	r4.evidence.Set("source", "github_actions");
	ValueMap d2; d2.Set("action", "mechanical_pass"); d2.Set("actor_type", COMPANY_ACTOR_AUTOMATION);
	d2.Set("reason", "All mechanical checks passed."); d2.Set("at", CompanyTimeIso(t0));
	r4.decision_history.Add(d2);
	r4.created_at = t0; r4.updated_at = t0;
	SaveReview(r4, error);

	ReviewRecord r5;
	r5.id = "rev-0005"; r5.work_item_id = w4.id; r5.proposal_id = p7.id; r5.kind = "operator";
	r5.actor_user_id = bob.id; r5.actor_type = COMPANY_ACTOR_HUMAN; r5.status = COMPANY_REVIEW_APPROVED;
	ValueMap d3; d3.Set("action", "operator_approve"); d3.Set("actor_user_id", bob.id);
	d3.Set("actor_type", COMPANY_ACTOR_HUMAN); d3.Set("reason", "Verified against issue 7."); d3.Set("at", CompanyTimeIso(t0));
	r5.decision_history.Add(d3);
	r5.created_at = t0; r5.updated_at = t0;
	SaveReview(r5, error);

	ReviewRecord r6;
	r6.id = "rev-0006"; r6.work_item_id = w5.id; r6.proposal_id = p8.id; r6.kind = "mechanical";
	r6.actor_type = COMPANY_ACTOR_AUTOMATION; r6.status = COMPANY_REVIEW_PASSED;
	r6.evidence.Set("run_id", "run-fixture-0005"); r6.evidence.Set("event_id", "evt-fixture-mech-pass-0005");
	r6.evidence.Set("source", "github_actions");
	ValueMap d4; d4.Set("action", "mechanical_pass"); d4.Set("actor_type", COMPANY_ACTOR_AUTOMATION);
	d4.Set("reason", "All mechanical checks passed."); d4.Set("at", CompanyTimeIso(t0));
	r6.decision_history.Add(d4);
	r6.created_at = t0; r6.updated_at = t0;
	SaveReview(r6, error);

	ReviewRecord r7;
	r7.id = "rev-0007"; r7.work_item_id = w5.id; r7.proposal_id = p8.id; r7.kind = "operator";
	r7.actor_user_id = bob.id; r7.actor_type = COMPANY_ACTOR_HUMAN; r7.status = COMPANY_REVIEW_APPROVED;
	ValueMap d5; d5.Set("action", "operator_approve"); d5.Set("actor_user_id", bob.id);
	d5.Set("actor_type", COMPANY_ACTOR_HUMAN); d5.Set("reason", "Verified against issue 8."); d5.Set("at", CompanyTimeIso(t0));
	r7.decision_history.Add(d5);
	r7.created_at = t0; r7.updated_at = t0;
	SaveReview(r7, error);

	// ---- Ledger: one compensation credit for wrk-0005 ----
	LedgerEntry l1;
	l1.entry_id = "lgr-0001"; l1.idempotency_key = CompanyCompensationKey(w5.id);
	l1.type = COMPANY_LEDGER_CREDIT; l1.amount = (int)(p8.estimated_hours * alice.hourly_rate); l1.user_id = alice.id;
	l1.work_item_id = w5.id; l1.reason = "Completion of proposal prp-0008 (PR #10)";
	l1.actor_user_id = bob.id; l1.actor_type = COMPANY_ACTOR_HUMAN; l1.created_at = t0;
	AppendLedger(l1, error);

	// ---- Representative audit trail ----
	RecordAudit(alice.id, "", COMPANY_ACTOR_HUMAN, alice.roles, "proposal.create", "proposal", p1.id,
	            "", COMPANY_PROPOSAL_PROPOSED, "New proposal", "evt-fixture-create-0001", error);
	RecordAudit(bob.id, "", COMPANY_ACTOR_HUMAN, bob.roles, "proposal.accept", "proposal", p2.id,
	            COMPANY_PROPOSAL_PROPOSED, COMPANY_PROPOSAL_ACCEPTED, "Well scoped", "evt-fixture-accept-0002", error);
	RecordAudit(carol.id, "", COMPANY_ACTOR_HUMAN, carol.roles, "work.request_mechanical", "work_item", w2.id,
	            COMPANY_STATE_IMPLEMENTATION, COMPANY_STATE_MECHANICAL, "PR bound, ready", "evt-fixture-mech-req-0002", error);
	RecordAudit("", callback.id, COMPANY_ACTOR_AUTOMATION, Vector<String>(), "mechanical.pass", "review", r2.id,
	            COMPANY_REVIEW_REQUESTED, COMPANY_REVIEW_PASSED, "run-fixture-0003", "evt-fixture-mech-pass-0003", error);
	RecordAudit(alice.id, "", COMPANY_ACTOR_HUMAN, alice.roles, "work.request_operator", "work_item", w3.id,
	            COMPANY_STATE_MECHANICAL, COMPANY_STATE_OPERATOR, "Mechanical passed", "evt-fixture-op-req-0003", error);
	RecordAudit(bob.id, "", COMPANY_ACTOR_HUMAN, bob.roles, "operator.approve", "review", r5.id,
	            COMPANY_REVIEW_REQUESTED, COMPANY_REVIEW_APPROVED, "Verified against issue 7", "evt-fixture-op-approve-0004", error);
	RecordAudit(bob.id, "", COMPANY_ACTOR_HUMAN, bob.roles, "compensation.issue", "ledger", l1.entry_id,
	            COMPANY_STATE_COMPLETED, COMPANY_STATE_COMPENSATED, "Completion of proposal prp-0008", "evt-fixture-compensate-0005", error);

	// ---- Fixture policy: allowlist covers the fixture repository ----
	config.schema_version = COMPANY_SCHEMA_VERSION;
	config.repository_allowlist.Clear();
	config.repository_allowlist.Add("OuluBSD/CompanySkylark");
	config.reward_policy.Set("max_hours", 40);
	config.reward_policy.Set("max_amount", 5000);
	config.reward_policy.Set("currency", "CREDIT");
	config.review_policy.Set("operator_quorum", 1);
	config.review_policy.Set("mechanical_required", true);
	config.agent_reviewer.Set("enabled", false);
	config.agent_reviewer.Set("command", "");
	config.updated_at = GetSysTime();
	SaveConfig(error);

	// ---- Platform v2 fixture records (reference-shaped contracts) ----
	// A trial-service identity token plus one completed command/dispatch/
	// evidence chain for wrk-0005 (PR #10) and one complete baseline trial.
	AgentTokenRecord trial_token;
	trial_token.id = "tok-0005"; trial_token.name = "trial-service"; trial_token.owner_user_id = admin.id;
	trial_token.token_hash = WebAuthPepperedHash(secret, "company-test-trial-token-0001");
	trial_token.scopes.Add(COMPANY_SCOPE_TRIALS_WRITE);
	trial_token.created_at = t0; trial_token.last_used_at = t0;
	SaveToken(trial_token, error);

	PlatformCommandRecord cmd;
	cmd.id = "cmd-0001"; cmd.source = "github_comment"; cmd.repository = "OuluBSD/CompanySkylark";
	cmd.pr_number = 10; cmd.actor_login = "alice"; cmd.command = PLATFORM_CMD_REVIEW;
	cmd.head_sha = "5f4c0a1b2c3d4e5f60718293a4b5c6d7e8f90a1b";
	cmd.status = PLATFORM_CMD_DISPATCHED; cmd.created_by_actor = callback.id;
	cmd.idempotency_key = "fixture-platform-cmd-0001";
	cmd.created_at = t0; cmd.updated_at = t0;
	SavePlatformCommand(cmd, error);

	PlatformReviewDispatch dsp;
	dsp.id = "dsp-0001"; dsp.work_item_id = w5.id; dsp.repository = w5.repository; dsp.pr_number = 10;
	dsp.kind = PLATFORM_REVIEW_MECHANICAL; dsp.head_sha = cmd.head_sha;
	dsp.requested_by = "alice"; dsp.status = PLATFORM_DSP_COMPLETED;
	dsp.idempotency_key = "fixture-platform-dsp-0001";
	dsp.created_at = t0; dsp.updated_at = t0;
	SaveReviewDispatch(dsp, error);

	PlatformReviewAttempt atm;
	atm.id = "atm-0001"; atm.dispatch_id = dsp.id; atm.external_service = "github_actions";
	atm.run_id = "run-fixture-0005"; atm.status = PLATFORM_ATM_COMPLETED;
	atm.created_at = t0; atm.updated_at = t0;
	SaveReviewAttempt(atm, error);

	PlatformReviewEvidence evd;
	evd.id = "evd-0001"; evd.attempt_id = atm.id; evd.dispatch_id = dsp.id; evd.work_item_id = w5.id;
	evd.repository = w5.repository; evd.pr_number = 10; evd.head_sha = cmd.head_sha;
	evd.outcome = PLATFORM_EVD_PASSED; evd.findings = "All mechanical checks passed.";
	evd.run_id = "run-fixture-0005"; evd.external_service = "github_actions";
	evd.tool_version = "mechanical-1"; evd.producer_actor = "token review-callback";
	evd.event_id = "fixture-platform-evd-0001";
	evd.recorded_at = t0;
	SaveReviewEvidence(evd, error);

	PlatformTrialRecord trial;
	trial.id = "trl-0001"; trial.kind = PLATFORM_TRIAL_BASELINE; trial.repository = w5.repository;
	trial.pr_number = 10; trial.head_sha = cmd.head_sha;
	trial.splits.Add("validation"); trial.trials_per_split = 3;
	trial.requested_by = "admin"; trial.external_service = "external_trial_service";
	trial.status = PLATFORM_TRIAL_COMPLETE; trial.expected_results = 3; trial.received_results = 3;
	trial.idempotency_key = "fixture-platform-trial-0001";
	trial.created_at = t0; trial.updated_at = t0;
	SavePlatformTrial(trial, error);

	for(int i = 1; i <= 3; i++) {
		PlatformTrialResultRecord res;
		res.id = "res-000" + AsString(i);
		res.trial_id = trial.id; res.split = "validation"; res.trial_index = i;
		res.outcome = PLATFORM_TRES_RECORDED; res.passed = true; res.reward = 1;
		res.summary = "baseline trial " + AsString(i) + " completed";
		res.head_sha = cmd.head_sha; res.external_job_id = "job-fixture-" + AsString(i);
		res.event_id = "fixture-platform-res-" + AsString(i);
		res.recorded_at = t0;
		SaveTrialResult(res, error);
	}

	PlatformArtifactRecord art;
	art.id = "art-0001"; art.owner_type = "evidence"; art.owner_id = evd.id;
	art.kind = "review_output"; art.uri = "https://example.invalid/artifacts/fixture-run-0005";
	art.content_type = "text/plain"; art.size = 128; art.sha256 = "";
	art.retention = "standard"; art.availability = PLATFORM_ART_AVAILABLE;
	art.produced_by = "token review-callback"; art.event_id = "fixture-platform-art-0001";
	art.recorded_at = t0;
	SavePlatformArtifact(art, error);

	RecordAudit("", callback.id, COMPANY_ACTOR_AUTOMATION, Vector<String>(), "platform.command", "platform_command",
	            cmd.id, "", PLATFORM_CMD_DISPATCHED, "fixture /review ingest", "fixture-platform-cmd-0001", error);
	RecordAudit("", callback.id, COMPANY_ACTOR_AUTOMATION, Vector<String>(), "platform.dispatch", "work_item",
	            w5.id, "", PLATFORM_DSP_COMPLETED, "fixture mechanical dispatch", "fixture-platform-dsp-0001", error);
	RecordAudit("", callback.id, COMPANY_ACTOR_AUTOMATION, Vector<String>(), "platform.evidence", "platform_dispatch",
	            dsp.id, PLATFORM_DSP_DISPATCHED, PLATFORM_DSP_COMPLETED, "fixture evidence", "fixture-platform-evd-0001", error);
	RecordAudit("", trial_token.id, COMPANY_ACTOR_AUTOMATION, Vector<String>(), "platform.trial_complete", "platform_trial",
	            trial.id, PLATFORM_TRIAL_COLLECTING, PLATFORM_TRIAL_COMPLETE, "3/3 baseline results", "fixture-platform-trial-0001", error);
}

END_UPP_NAMESPACE
