#include "CompanyWorkflow.h"

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// Client contracts and projects (WP5). Every mutation follows the same rules:
// the role gate runs first (deny by default), the stable idempotency key is
// consumed inside the locked record update so a duplicate never re-executes,
// state and membership checks run inside that same update, and every applied
// transition writes an audit event. A duplicate is a successful no-op.
// ---------------------------------------------------------------------------

// One locked record update's outcome: a rejection fails the call, a duplicate
// is a successful no-op, and only an applied change continues to the audit.
static bool UpdateApplied(CompanyWorkflowResult& r, int res, const String& error)
{
	if(res < 0) {
		r.ok = false;
		r.error = error;
		return false;
	}
	if(res == 0) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return false;
	}
	return true;
}

bool CompanyWorkflow::IsClientHuman(const WebAuthIdentity& actor)
{
	return IsHumanWithRole(actor, COMPANY_ROLE_CLIENT);
}

bool CompanyWorkflow::IsEmployeeHuman(const WebAuthIdentity& actor)
{
	return IsHumanWithRole(actor, COMPANY_ROLE_EMPLOYEE);
}

// "owner/repo#number" with the number a positive integer.
static bool ParsePrRef(const String& pr_ref, String& repo, int& number)
{
	int hash = pr_ref.Find('#');
	if(hash <= 0)
		return false;
	repo = pr_ref.Left(hash);
	String digits = pr_ref.Mid(hash + 1);
	if(digits.IsEmpty())
		return false;
	for(int i = 0; i < digits.GetCount(); i++)
		if(digits[i] < '0' || digits[i] > '9')
			return false;
	number = ScanInt(digits);
	return number > 0 && repo.Find('/') > 0;
}

// ---------------------------------------------------------------------------
// Contracts
// ---------------------------------------------------------------------------

CompanyWorkflowResult CompanyWorkflow::CreateContract(const WebAuthIdentity& actor, const String& title,
                                                      const String& brief, double amount, const String& currency,
                                                      const String& payment_terms, const String& practical_details,
                                                      const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsClientHuman(actor))
		return Fail("client role required to create a contract");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	CompanyStore* store = GetStore();
	if(store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	if(TrimBoth(title).IsEmpty())
		return Fail("title is required");
	if(amount <= 0)
		return Fail("amount must be greater than zero");
	if(TrimBoth(currency).IsEmpty())
		return Fail("currency is required");

	ContractRecord c;
	c.id = CompanyGenerateId("ctr");
	c.client_user_id = actor.user_id;
	c.title = TrimBoth(title);
	c.brief = brief;
	c.amount = amount;
	c.currency = TrimBoth(currency);
	c.payment_terms = payment_terms;
	c.practical_details = practical_details;
	c.status = COMPANY_CONTRACT_PROPOSED;
	c.created_by = actor.user_id;
	c.created_at = GetSysTime();
	c.updated_at = c.created_at;

	String error;
	if(!store->SaveContract(c, error))
		return Fail("cannot save contract: " + error);
	if(!Audit(COMPANY_AUD_CONTRACT_CREATE, "contract", c.id, "", COMPANY_CONTRACT_PROPOSED, actor, brief,
	          idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("contract_id", c.id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::NegotiateContract(const WebAuthIdentity& actor, const String& contract_id,
                                                         double amount, const String& payment_terms,
                                                         const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("admin role required to negotiate a contract");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	if(amount <= 0)
		return Fail("amount must be greater than zero");

	String prior, error;
	ContractRecord out;
	int res = GetStore()->UpdateContractWithDedup(contract_id, idempotency_key,
		[&](ContractRecord& c, String& e) {
			if(c.status != COMPANY_CONTRACT_PROPOSED && c.status != COMPANY_CONTRACT_NEGOTIATING) {
				e = "contract is " + c.status + ", not negotiable";
				return;
			}
			prior = c.status;
			c.amount = amount;
			c.payment_terms = payment_terms;
			c.status = COMPANY_CONTRACT_NEGOTIATING;
		}, out, error);
	if(!UpdateApplied(r, res, error))
		return r;
	if(!Audit(COMPANY_AUD_CONTRACT_NEGOTIATE, "contract", contract_id, prior, out.status, actor, payment_terms,
	          idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("contract_id", contract_id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::AcceptContract(const WebAuthIdentity& actor, const String& contract_id,
                                                      const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("admin role required to accept a contract");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");

	String prior, error;
	ContractRecord out;
	int res = GetStore()->UpdateContractWithDedup(contract_id, idempotency_key,
		[&](ContractRecord& c, String& e) {
			if(c.status != COMPANY_CONTRACT_PROPOSED && c.status != COMPANY_CONTRACT_NEGOTIATING) {
				e = "contract is " + c.status + ", not open for acceptance";
				return;
			}
			prior = c.status;
			c.status = COMPANY_CONTRACT_ACTIVE;
		}, out, error);
	if(!UpdateApplied(r, res, error))
		return r;
	if(!Audit(COMPANY_AUD_CONTRACT_ACCEPT, "contract", contract_id, prior, out.status, actor, "",
	          idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("contract_id", contract_id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::CloseContract(const WebAuthIdentity& actor, const String& contract_id,
                                                     bool completed, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("admin role required to close a contract");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");

	String prior, error;
	ContractRecord out;
	int res = GetStore()->UpdateContractWithDedup(contract_id, idempotency_key,
		[&](ContractRecord& c, String& e) {
			if(c.status != COMPANY_CONTRACT_ACTIVE) {
				e = "contract is " + c.status + ", only an active contract can be closed";
				return;
			}
			prior = c.status;
			c.status = completed ? String(COMPANY_CONTRACT_COMPLETED) : String(COMPANY_CONTRACT_CANCELLED);
		}, out, error);
	if(!UpdateApplied(r, res, error))
		return r;
	if(!Audit(COMPANY_AUD_CONTRACT_CLOSE, "contract", contract_id, prior, out.status, actor, "",
	          idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("contract_id", contract_id);
	return r;
}

// ---------------------------------------------------------------------------
// Projects
// ---------------------------------------------------------------------------

CompanyWorkflowResult CompanyWorkflow::CreateProject(const WebAuthIdentity& actor, const String& contract_id,
                                                     const String& repo_name, const String& repo_origin,
                                                     const String& goals, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("admin role required to create a project");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	CompanyStore* store = GetStore();
	if(store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	ContractRecord contract;
	if(!store->FindContract(contract_id, contract))
		return Fail("contract not found: " + contract_id);
	if(contract.status != COMPANY_CONTRACT_ACTIVE)
		return Fail("contract is " + contract.status + ", a project needs an active contract");
	if(repo_name.Find('/') <= 0)
		return Fail("repository must be owner/repo");
	if(repo_origin != COMPANY_REPO_CREATED && repo_origin != COMPANY_REPO_ADOPTED)
		return Fail("repository origin must be created or adopted");
	if(repo_origin == COMPANY_REPO_ADOPTED && !store->IsRepositoryAllowed(repo_name))
		return Fail("repository is not on the allowlist: " + repo_name);

	ProjectRecord p;
	p.id = CompanyGenerateId("prj");
	p.contract_id = contract_id;
	p.repo_name = repo_name;
	p.repo_origin = repo_origin;
	p.goals = goals;
	p.status = COMPANY_PROJECT_PLANNING;
	p.created_at = GetSysTime();
	p.updated_at = p.created_at;

	String error;
	if(!store->SaveProject(p, error))
		return Fail("cannot save project: " + error);
	if(!Audit(COMPANY_AUD_PROJECT_CREATE, "project", p.id, "", COMPANY_PROJECT_PLANNING, actor, repo_name,
	          idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("project_id", p.id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::AssignEmployee(const WebAuthIdentity& actor, const String& project_id,
                                                      const String& employee_user_id, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("admin role required to assign an employee");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	UserRecord employee;
	if(!GetStore()->FindUser(employee_user_id, employee))
		return Fail("user not found: " + employee_user_id);
	if(!employee.HasRole(COMPANY_ROLE_EMPLOYEE))
		return Fail("user has no employee role: " + employee_user_id);

	String prior, error;
	ProjectRecord out;
	int res = GetStore()->UpdateProjectWithDedup(project_id, idempotency_key,
		[&](ProjectRecord& p, String& e) {
			if(p.status != COMPANY_PROJECT_PLANNING && p.status != COMPANY_PROJECT_ACTIVE) {
				e = "project is " + p.status + ", employees can no longer be assigned";
				return;
			}
			prior = p.status;
			if(!p.HasEmployee(employee_user_id))
				p.employee_user_ids.Add(employee_user_id);
		}, out, error);
	if(!UpdateApplied(r, res, error))
		return r;
	if(!Audit(COMPANY_AUD_PROJECT_ASSIGN, "project", project_id, prior, out.status, actor, employee_user_id,
	          idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("project_id", project_id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::StartProject(const WebAuthIdentity& actor, const String& project_id,
                                                    const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("admin role required to start a project");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");

	String prior, error;
	ProjectRecord out;
	int res = GetStore()->UpdateProjectWithDedup(project_id, idempotency_key,
		[&](ProjectRecord& p, String& e) {
			if(p.status != COMPANY_PROJECT_PLANNING) {
				e = "project is " + p.status + ", only a planning project can start";
				return;
			}
			if(p.employee_user_ids.IsEmpty()) {
				e = "assign at least one employee before the project starts";
				return;
			}
			prior = p.status;
			p.status = COMPANY_PROJECT_ACTIVE;
		}, out, error);
	if(!UpdateApplied(r, res, error))
		return r;
	if(!Audit(COMPANY_AUD_PROJECT_START, "project", project_id, prior, out.status, actor, "",
	          idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("project_id", project_id);
	return r;
}

CompanyWorkflowResult CompanyWorkflow::AcceptProjectPullRequest(const WebAuthIdentity& actor, const String& project_id,
                                                                const String& pr_ref, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsEmployeeHuman(actor))
		return Fail("employee role required to accept a pull request");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");

	String prior, error;
	ProjectRecord out;
	int res = GetStore()->UpdateProjectWithDedup(project_id, idempotency_key,
		[&](ProjectRecord& p, String& e) {
			if(p.status != COMPANY_PROJECT_ACTIVE) {
				e = "project is " + p.status + ", pull requests are accepted only while it is active";
				return;
			}
			if(!p.HasEmployee(actor.user_id)) {
				e = "only an employee assigned to this project may accept its pull requests";
				return;
			}
			String repo;
			int number = 0;
			if(!ParsePrRef(pr_ref, repo, number) || repo != p.repo_name) {
				e = "pr_ref must be owner/repo#number in this project's repository";
				return;
			}
			for(int i = 0; i < p.accepted_prs.GetCount(); i++)
				if(p.accepted_prs[i] == pr_ref) {
					e = "pull request is already accepted: " + pr_ref;
					return;
				}
			prior = p.status;
			p.accepted_prs.Add(pr_ref);
		}, out, error);
	if(!UpdateApplied(r, res, error))
		return r;
	if(!Audit(COMPANY_AUD_PROJECT_PR_ACCEPT, "project", project_id, prior, out.status, actor, pr_ref,
	          idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("project_id", project_id);
	r.payload.Set("pr_ref", pr_ref);
	return r;
}

// Shared shape for the three admin-driven project steps below: one locked
// status step from `from` to `to`, audited.
static CompanyWorkflowResult ProjectStep(CompanyWorkflowResult r, CompanyStore* store, const WebAuthIdentity& actor,
                                         const String& project_id, const String& idempotency_key,
                                         const char* from, const char* to, const char* action)
{
	String prior, error;
	ProjectRecord out;
	int res = store->UpdateProjectWithDedup(project_id, idempotency_key,
		[&](ProjectRecord& p, String& e) {
			if(p.status != from) {
				e = String("project is ") + p.status + ", expected " + from;
				return;
			}
			prior = p.status;
			p.status = to;
		}, out, error);
	if(!UpdateApplied(r, res, error))
		return r;
	if(!CompanyWorkflow::Audit(action, "project", project_id, prior, out.status, actor, "",
	                            idempotency_key, &r.event_id)) {
		r.ok = false;
		r.error = "audit recording failed";
		return r;
	}
	r.ok = true;
	r.payload.Set("project_id", project_id);
	return r;
}

// Strict completion gate: a project completes only when every work item of its
// proposals is completed. No work items passes; the refusal names the open ones.
CompanyWorkflowResult CompanyWorkflow::RequestProjectCompletion(const WebAuthIdentity& actor, const String& project_id,
                                                                const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("admin role required to request project completion");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	CompanyStore* store = GetStore();
	if(store->IsKeyProcessed(idempotency_key)) {
		r.ok = true;
		r.idempotent_duplicate = true;
		return r;
	}
	String outstanding;
	Vector<WorkItemRecord> items = store->GetWorkItems();
	for(const WorkItemRecord& w : items) {
		ProposalRecord p;
		if(!store->FindProposal(w.proposal_id, p) || p.project_id != project_id)
			continue;
		if(w.state == COMPANY_STATE_COMPLETED)
			continue;
		if(!outstanding.IsEmpty())
			outstanding << ", ";
		outstanding << w.id << " (" << w.state << ")";
	}
	if(!outstanding.IsEmpty())
		return Fail("project has outstanding work items: " + outstanding);
	return ProjectStep(r, GetStore(), actor, project_id, idempotency_key,
	                   COMPANY_PROJECT_ACTIVE, COMPANY_PROJECT_COMPLETING, COMPANY_AUD_PROJECT_COMPLETING);
}

CompanyWorkflowResult CompanyWorkflow::MarkProjectReady(const WebAuthIdentity& actor, const String& project_id,
                                                        const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsAdminHuman(actor))
		return Fail("admin role required to mark a project ready");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	return ProjectStep(r, GetStore(), actor, project_id, idempotency_key,
	                   COMPANY_PROJECT_COMPLETING, COMPANY_PROJECT_READY, COMPANY_AUD_PROJECT_READY);
}

CompanyWorkflowResult CompanyWorkflow::DeliverProject(const WebAuthIdentity& actor, const String& project_id,
                                                      const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsClientHuman(actor))
		return Fail("client role required to accept a delivery");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	CompanyStore* store = GetStore();
	ProjectRecord p;
	if(!store->FindProject(project_id, p))
		return Fail("project not found: " + project_id);
	ContractRecord c;
	if(!store->FindContract(p.contract_id, c) || c.client_user_id != actor.user_id)
		return Fail("only the client of this project's contract may accept its delivery");
	return ProjectStep(r, store, actor, project_id, idempotency_key,
	                   COMPANY_PROJECT_READY, COMPANY_PROJECT_DELIVERED, COMPANY_AUD_PROJECT_DELIVER);
}

CompanyWorkflowResult CompanyWorkflow::ScopeProposalToProject(const WebAuthIdentity& actor, const String& proposal_id,
                                                              const String& project_id, const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	CompanyStore* store = GetStore();
	ProjectRecord p;
	if(!store->FindProject(project_id, p))
		return Fail("project not found: " + project_id);
	bool allowed = IsAdminHuman(actor) || (IsEmployeeHuman(actor) && p.HasEmployee(actor.user_id));
	if(!allowed)
		return Fail("admin role, or an employee assigned to the project, required to scope a proposal");
	if(p.status != COMPANY_PROJECT_PLANNING && p.status != COMPANY_PROJECT_ACTIVE)
		return Fail("project is " + p.status + ", proposals can no longer be scoped to it");

	String error;
	ProposalRecord out;
	int res = store->UpdateProposalWithDedup(proposal_id, idempotency_key,
		[&](ProposalRecord& pr, String& e) {
			if(pr.repository != p.repo_name) {
				e = "proposal repository is not the project repository";
				return;
			}
			if(!pr.project_id.IsEmpty() && pr.project_id != project_id) {
				e = "proposal is already scoped to another project";
				return;
			}
			pr.project_id = project_id;
		}, out, error);
	if(!UpdateApplied(r, res, error))
		return r;
	if(!Audit(COMPANY_AUD_PROPOSAL_PROJECT, "proposal", proposal_id, "", project_id, actor, "",
	          idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("proposal_id", proposal_id);
	r.payload.Set("project_id", project_id);
	return r;
}

// An employee assigned to the project takes over a contributor's work item
// while it is still in implementation or after changes were requested.
// The "near done" completion threshold is not modeled yet; state is the gate.
CompanyWorkflowResult CompanyWorkflow::AdoptWorkItem(const WebAuthIdentity& actor, const String& work_item_id,
                                                     const String& idempotency_key)
{
	CompanyWorkflowResult r;
	if(!StoreReady())
		return Fail("store is not ready");
	if(!IsEmployeeHuman(actor))
		return Fail("employee role required to adopt a work item");
	if(idempotency_key.IsEmpty())
		return Fail("idempotency_key is required");
	CompanyStore* store = GetStore();
	WorkItemRecord item;
	if(!store->FindWorkItem(work_item_id, item))
		return Fail("work item not found: " + work_item_id);
	ProposalRecord proposal;
	if(!store->FindProposal(item.proposal_id, proposal) || proposal.project_id.IsEmpty())
		return Fail("work item is not part of a client project");
	ProjectRecord p;
	if(!store->FindProject(proposal.project_id, p) || !p.HasEmployee(actor.user_id))
		return Fail("only an employee assigned to the project may adopt its work");

	String prior, error;
	WorkItemRecord out;
	int res = store->UpdateWorkItemWithDedup(work_item_id, idempotency_key,
		[&](WorkItemRecord& w, String& e) {
			if(w.state != COMPANY_STATE_IMPLEMENTATION && w.state != COMPANY_STATE_CHANGES) {
				e = "work item is in state " + w.state + ", not adoptable";
				return;
			}
			prior = w.state;
			w.contributor_user_id = actor.user_id;
			w.contributor_token_id = String();
			w.contributor_type = COMPANY_ACTOR_HUMAN;
		}, out, error);
	if(!UpdateApplied(r, res, error))
		return r;
	if(!Audit(COMPANY_AUD_WORK_ADOPT, "work_item", work_item_id, prior, out.state, actor, proposal.project_id,
	          idempotency_key, &r.event_id))
		return Fail("audit recording failed");
	r.ok = true;
	r.payload.Set("work_item_id", work_item_id);
	return r;
}

END_UPP_NAMESPACE
