#ifndef _Company_CompanyWorkflow_h_
#define _Company_CompanyWorkflow_h_

#include <Core/Core.h>
#include <Company/CompanyStore.h>
#include <WebAuth/WebAuthModel.h>

NAMESPACE_UPP

// Canonical lifecycle: proposed -> accepted -> implementation ->
// mechanical_review -> operator_review -> completed -> compensated, with
// explicit rejected / changes_requested / blocked transitions.

// Proposal actions (reviewer decisions).
#define COMPANY_ACTION_ACCEPT       "accept"
#define COMPANY_ACTION_REJECT       "reject"
#define COMPANY_ACTION_CHANGES      "changes"

// Operator review actions.
#define COMPANY_OP_ACTION_APPROVE   "approve"
#define COMPANY_OP_ACTION_REJECT    "reject"
#define COMPANY_OP_ACTION_CHANGES   "changes"

// Audit action names.
#define COMPANY_AUD_PROPOSAL_CREATE     "proposal.create"
#define COMPANY_AUD_PROPOSAL_ACCEPT     "proposal.accept"
#define COMPANY_AUD_PROPOSAL_REJECT     "proposal.reject"
#define COMPANY_AUD_PROPOSAL_CHANGES    "proposal.changes_requested"
#define COMPANY_AUD_PROPOSAL_REVISE     "proposal.revise"
#define COMPANY_AUD_WORK_BIND           "work.bind_pr"
#define COMPANY_AUD_WORK_MECH_REQUEST   "work.request_mechanical"
#define COMPANY_AUD_MECH_PASS           "mechanical.pass"
#define COMPANY_AUD_MECH_FAIL           "mechanical.fail"
#define COMPANY_AUD_WORK_OP_REQUEST     "work.request_operator"
#define COMPANY_AUD_OP_APPROVE          "operator.approve"
#define COMPANY_AUD_OP_REJECT           "operator.reject"
#define COMPANY_AUD_OP_CHANGES          "operator.changes_requested"
#define COMPANY_AUD_WORK_RESUME         "work.resume"
#define COMPANY_AUD_WORK_BLOCK          "work.block"
#define COMPANY_AUD_WORK_UNBLOCK        "work.unblock"
#define COMPANY_AUD_COMPENSATE          "compensation.issue"
#define COMPANY_AUD_LEDGER_CORRECTION   "ledger.correction"
#define COMPANY_AUD_USER_CREATE         "user.create"
#define COMPANY_AUD_USER_ROLES          "user.roles_set"
#define COMPANY_AUD_TOKEN_CREATE        "token.create"
#define COMPANY_AUD_TOKEN_REVOKE        "token.revoke"
#define COMPANY_AUD_CONFIG_UPDATE       "config.update"
#define COMPANY_AUD_REPOS_UPDATE        "repos.update"
#define COMPANY_AUD_CALLBACK_REJECTED   "callback.rejected"
#define COMPANY_AUD_CONTRACT_CREATE     "contract.create"
#define COMPANY_AUD_CONTRACT_NEGOTIATE  "contract.negotiate"
#define COMPANY_AUD_CONTRACT_ACCEPT     "contract.accept"
#define COMPANY_AUD_CONTRACT_CLOSE      "contract.close"
#define COMPANY_AUD_PROJECT_CREATE      "project.create"
#define COMPANY_AUD_PROJECT_ASSIGN      "project.assign_employee"
#define COMPANY_AUD_PROJECT_START       "project.start"
#define COMPANY_AUD_PROJECT_PR_ACCEPT   "project.accept_pr"
#define COMPANY_AUD_PROJECT_COMPLETING  "project.request_completion"
#define COMPANY_AUD_PROJECT_READY       "project.ready"
#define COMPANY_AUD_PROJECT_DELIVER     "project.deliver"
#define COMPANY_AUD_PROPOSAL_PROJECT    "proposal.scope_to_project"
#define COMPANY_AUD_WORK_ADOPT          "work.adopt"

// Outcome of a workflow transition.
struct CompanyWorkflowResult : Moveable<CompanyWorkflowResult> {
	bool   ok = false;
	bool   idempotent_duplicate = false;
	String error;
	String event_id;
	ValueMap payload;   // e.g. raw agent token at creation, proposal/work ids
};

// Agent token helpers (same format and pepper as WebAuthService, without its
// session state). The pepper is the store's server secret.
#define COMPANY_AGENT_TOKEN_PREFIX  "waat_"

static inline String CompanyGenerateAgentToken()
{
	return COMPANY_AGENT_TOKEN_PREFIX + WebAuthRandomBytes(24);
}

static inline String CompanyTokenHash(const String& secret, const String& raw)
{
	return WebAuthPepperedHash(secret, raw);
}

// ---------------------------------------------------------------------------
// CompanyWorkflow: all lifecycle mutations. Deny by default; every transition
// validates actor identity (role + human/agent/automation), current state,
// required evidence, and consumes a stable idempotency/event key inside the
// locked record update so duplicate deliveries never re-execute.
// ---------------------------------------------------------------------------

class CompanyWorkflow {
public:
	static void SetStore(CompanyStore* store) { s_store = store; }
	static CompanyStore* GetStore()           { return s_store; }

	// ---------- Transition validation ----------
	static bool IsValidWorkTransition(const String& from, const String& to);
	static bool IsValidProposalTransition(const String& from, const String& to);
	static Vector<String> WorkStates();
	static Vector<String> ProposalStates();

	// ---------- Proposal lifecycle ----------
	static CompanyWorkflowResult CreateProposal(const WebAuthIdentity& actor, const String& title,
	                                            const String& description, const String& repository,
	                                            double estimated_hours,
	                                            const String& idempotency_key);
	static CompanyWorkflowResult DecideProposal(const WebAuthIdentity& actor, const String& proposal_id,
	                                            const String& action, const String& reason,
	                                            const String& assign_contributor_user_id,
	                                            const String& idempotency_key);
	static CompanyWorkflowResult ReviseProposal(const WebAuthIdentity& actor, const String& proposal_id,
	                                            const String& new_description, const String& idempotency_key);

	// ---------- Work item lifecycle ----------
	static CompanyWorkflowResult BindPullRequest(const WebAuthIdentity& actor, const String& work_item_id,
	                                             const String& repository, int pr_number,
	                                             const String& pr_url, const String& branch,
	                                             const String& idempotency_key);
	static CompanyWorkflowResult RequestMechanicalReview(const WebAuthIdentity& actor,
	                                                     const String& work_item_id,
	                                                     const String& idempotency_key);
	// Callback ingestion (GitHub Actions) and admin manual override.
	static CompanyWorkflowResult RecordMechanicalResult(const WebAuthIdentity& actor,
	                                                    const String& work_item_id, bool passed,
	                                                    const ValueMap& evidence, const String& idempotency_key);
	static CompanyWorkflowResult RequestOperatorReview(const WebAuthIdentity& actor,
	                                                   const String& work_item_id,
	                                                   const String& idempotency_key);
	static CompanyWorkflowResult DecideOperatorReview(const WebAuthIdentity& actor,
	                                                  const String& work_item_id, const String& action,
	                                                  const String& reason, const String& idempotency_key);
	static CompanyWorkflowResult ResumeWork(const WebAuthIdentity& actor, const String& work_item_id,
	                                        const String& reason, const String& idempotency_key);
	static CompanyWorkflowResult BlockWorkItem(const WebAuthIdentity& actor, const String& work_item_id,
	                                           const String& reason, const String& idempotency_key);
	static CompanyWorkflowResult UnblockWorkItem(const WebAuthIdentity& actor, const String& work_item_id,
	                                             const String& reason, const String& idempotency_key);

	// ---------- Compensation & ledger ----------
	// One completed work item creates at most one credit: the ledger dedups on
	// the stable per-work-item key. Human admin/reviewer actors only, never
	// the item's contributor.
	static CompanyWorkflowResult IssueCompensation(const WebAuthIdentity& actor, const String& work_item_id,
	                                               const String& reason, const String& idempotency_key);
	// Admin-only signed correction (may be negative); mandatory reason.
	static CompanyWorkflowResult LedgerCorrection(const WebAuthIdentity& actor, const String& user_id,
	                                              int amount, const String& reason, const String& idempotency_key);
	// Admin-only management of a user's hourly rate (compensation input).
	static CompanyWorkflowResult SetUserHourlyRate(const WebAuthIdentity& actor, const String& user_id,
	                                               double hourly_rate, const String& idempotency_key);

	// ---------- Administration (human admin only) ----------
	static CompanyWorkflowResult SetUserRoles(const WebAuthIdentity& actor, const String& user_id,
	                                          const Vector<String>& roles, const String& idempotency_key);
	static CompanyWorkflowResult SetUserEnabled(const WebAuthIdentity& actor, const String& user_id,
	                                            bool enabled, const String& idempotency_key);
	static CompanyWorkflowResult CreateUser(const WebAuthIdentity& actor, const String& username,
	                                        const String& display_name, const Vector<String>& roles,
	                                        const String& password, const String& idempotency_key);
	static CompanyWorkflowResult CreateAgentToken(const WebAuthIdentity& actor, const String& name,
	                                              const String& owner_user_id, const Vector<String>& scopes,
	                                              const Time& expires_at, const String& idempotency_key,
	                                              String& raw_token_out);
	static CompanyWorkflowResult RevokeAgentToken(const WebAuthIdentity& actor, const String& token_id,
	                                              const String& reason, const String& idempotency_key);
	// ---------- Client contracts (company <-> client money flow) ----------
	// A client creates a contract; company admins negotiate, accept and close it.
	// Contract financials never touch the contributor compensation ledger.
	static CompanyWorkflowResult CreateContract(const WebAuthIdentity& actor, const String& title,
	                                            const String& brief, double amount, const String& currency,
	                                            const String& payment_terms, const String& practical_details,
	                                            const String& idempotency_key);
	static CompanyWorkflowResult NegotiateContract(const WebAuthIdentity& actor, const String& contract_id,
	                                               double amount, const String& payment_terms,
	                                               const String& idempotency_key);
	static CompanyWorkflowResult AcceptContract(const WebAuthIdentity& actor, const String& contract_id,
	                                            const String& idempotency_key);
	static CompanyWorkflowResult CloseContract(const WebAuthIdentity& actor, const String& contract_id,
	                                           bool completed, const String& idempotency_key);

	// ---------- Client projects (repository, employees, delivery) ----------
	static CompanyWorkflowResult CreateProject(const WebAuthIdentity& actor, const String& contract_id,
	                                           const String& repo_name, const String& repo_origin,
	                                           const String& goals, const String& idempotency_key);
	static CompanyWorkflowResult AssignEmployee(const WebAuthIdentity& actor, const String& project_id,
	                                            const String& employee_user_id, const String& idempotency_key);
	static CompanyWorkflowResult StartProject(const WebAuthIdentity& actor, const String& project_id,
	                                          const String& idempotency_key);
	// An assigned employee accepts a pull request on the company's behalf; pr_ref is "owner/repo#number".
	static CompanyWorkflowResult AcceptProjectPullRequest(const WebAuthIdentity& actor, const String& project_id,
	                                                      const String& pr_ref, const String& idempotency_key);
	static CompanyWorkflowResult RequestProjectCompletion(const WebAuthIdentity& actor, const String& project_id,
	                                                      const String& idempotency_key);
	static CompanyWorkflowResult MarkProjectReady(const WebAuthIdentity& actor, const String& project_id,
	                                              const String& idempotency_key);
	// The client of the project's contract accepts the delivery.
	static CompanyWorkflowResult DeliverProject(const WebAuthIdentity& actor, const String& project_id,
	                                            const String& idempotency_key);
	// Scopes a task proposal to a project (same repository), or an employee adopts a
	// contributor's work item of that project.
	static CompanyWorkflowResult ScopeProposalToProject(const WebAuthIdentity& actor, const String& proposal_id,
	                                                    const String& project_id, const String& idempotency_key);
	static CompanyWorkflowResult AdoptWorkItem(const WebAuthIdentity& actor, const String& work_item_id,
	                                           const String& idempotency_key);

	static CompanyWorkflowResult UpdateConfigPolicy(const WebAuthIdentity& actor, const ValueMap& reward_policy,
	                                                const ValueMap& review_policy, const ValueMap& agent_reviewer,
	                                                const String& idempotency_key);
	static CompanyWorkflowResult SetRepositoryAllowlist(const WebAuthIdentity& actor,
	                                                    const Vector<String>& repositories,
	                                                    const String& idempotency_key);

public:
	// Role guards (deny by default; a human actor holding the role).
	static bool IsAdminHuman(const WebAuthIdentity& actor);
	static bool IsClientHuman(const WebAuthIdentity& actor);
	static bool IsEmployeeHuman(const WebAuthIdentity& actor);
	// Audit recording is shared with the API layer (callback rejection trail).
	static bool Audit(const String& action, const String& target_type, const String& target_id,
	                  const String& prior_status, const String& new_status,
	                  const WebAuthIdentity& actor, const String& reason,
	                  const String& idempotency_key, String* event_id_out = nullptr);

private:
	static CompanyStore* s_store;

	static CompanyWorkflowResult Fail(const String& error);
	static bool StoreReady();

	// Centralized guards. Deny by default.
	static bool CanWriteProposals(const WebAuthIdentity& actor);
	static bool IsHumanWithRole(const WebAuthIdentity& actor, const String& role);
	static bool IsHumanWithAnyRole(const WebAuthIdentity& actor, const Vector<String>& roles);
	static bool CanManageWork(const WebAuthIdentity& actor, const WorkItemRecord& item); // contributor or admin human
	static bool CanRecordMechanical(const WebAuthIdentity& actor);

	static ValueMap MakeDecision(const String& action, const WebAuthIdentity& actor, const String& reason);

	// Review record helpers (one open review per kind per work item).
	static bool FindOpenReview(const String& work_item_id, const String& kind, ReviewRecord& out);
	static bool UpsertReview(const ReviewRecord& review, String& error);
};

END_UPP_NAMESPACE

#endif
