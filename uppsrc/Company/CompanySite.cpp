#include "CompanySite.h"
#include "CompanyViews.h"

NAMESPACE_UPP

// Company on a composed WebEngine site. Sessions and passwords belong to the
// site's WebAccounts; roles, scopes and every business rule belong to the
// Company store and CompanyWorkflow. The identity helpers below are local to
// this package: the Company package does not depend on CompanyAuth.

static EngineSite*   s_site  = NULL;
static CompanyStore* s_store = NULL;

// The role-to-scope table of CompanySkylark (CompanyAuth RoleScopes).
static Vector<String> CompanyRoleScopes(const Vector<String>& roles)
{
	Vector<String> scopes;
	bool admin = false;
	for(int i = 0; i < roles.GetCount(); i++) {
		const String& r = roles[i];
		if(r == COMPANY_ROLE_ADMIN)
			admin = true;
		else if(r == COMPANY_ROLE_CONTRIBUTOR || r == COMPANY_ROLE_AUTOMATION) {
			scopes.Add(COMPANY_SCOPE_PROPOSALS_READ); scopes.Add(COMPANY_SCOPE_PROPOSALS_WRITE);
			scopes.Add(COMPANY_SCOPE_WORK_READ); scopes.Add(COMPANY_SCOPE_WORK_WRITE);
			scopes.Add(COMPANY_SCOPE_REVIEWS_READ); scopes.Add(COMPANY_SCOPE_LEDGER_READ);
		}
		else if(r == COMPANY_ROLE_REVIEWER) {
			scopes.Add(COMPANY_SCOPE_PROPOSALS_READ);
			scopes.Add(COMPANY_SCOPE_REVIEWS_READ); scopes.Add(COMPANY_SCOPE_REVIEWS_WRITE);
			scopes.Add(COMPANY_SCOPE_WORK_READ); scopes.Add(COMPANY_SCOPE_LEDGER_READ);
		}
	}
	if(admin)
		scopes = CompanyAllScopes();
	Vector<String> out;
	for(int i = 0; i < scopes.GetCount(); i++)
		if(FindIndex(out, scopes[i]) < 0)
			out.Add(scopes[i]);
	return out;
}

// Scopes an identity holds: token scopes for agents, role scopes for humans.
static Vector<String> CompanyEffectiveScopes(const WebAuthIdentity& id)
{
	if(id.IsAgent() || id.IsAutomation())
		return clone(id.scopes);
	return CompanyRoleScopes(id.roles);
}

static WebAuthIdentity CompanyHumanIdentity(const UserRecord& u)
{
	WebAuthIdentity id;
	id.authenticated = true;
	id.actor_type = WEBAUTH_ACTOR_HUMAN;
	id.user_id = u.id;
	id.username = u.username;
	id.display_name = u.display_name;
	id.roles = clone(u.roles);
	id.scopes = CompanyRoleScopes(u.roles);
	return id;
}

// The caller's identity: a Bearer agent token, a Bearer WebAccounts session
// (from api/auth/login), or the engine-resolved browser session. Anything
// else is a guest (authenticated = false).
static WebAuthIdentity CompanyIdentity(WebRequest& r)
{
	WebAuthIdentity guest;
	if(!s_store)
		return guest;
	String auth = TrimBoth(r.Header("authorization"));
	if(ToLower(auth.Left(7)) == "bearer ") {
		String raw = TrimBoth(auth.Mid(7));
		if(raw.IsEmpty())
			return guest;
		String hash = CompanyTokenHash(s_store->GetSecret(), raw);
		AgentTokenRecord tok;
		if(s_store->FindTokenByHash(hash, tok)) {
			if(!IsNull(tok.revoked_at))
				return guest;
			if(!IsNull(tok.expires_at) && tok.expires_at < GetSysTime())
				return guest;
			UserRecord owner;
			bool has_owner = s_store->FindUser(tok.owner_user_id, owner);
			// A token that carries a machine-service scope (review callbacks,
			// trial results) is an automation identity whoever owns it: the
			// platform gates require automation plus that scope.
			bool service_scope = FindIndex(tok.scopes, String(COMPANY_SCOPE_CALLBACK_WRITE)) >= 0 ||
			                     FindIndex(tok.scopes, String(COMPANY_SCOPE_TRIALS_WRITE)) >= 0;
			WebAuthIdentity id;
			id.authenticated = true;
			id.actor_type = (service_scope || (has_owner && owner.HasRole(COMPANY_ROLE_AUTOMATION)))
			                ? WEBAUTH_ACTOR_AUTOMATION : WEBAUTH_ACTOR_AGENT;
			id.token_id = tok.id;
			id.user_id = tok.owner_user_id;
			if(has_owner) {
				id.username = owner.username;
				id.display_name = owner.display_name + " (token " + tok.name + ")";
			}
			else
				id.display_name = "token " + tok.name;
			id.scopes = clone(tok.scopes);
			id.bearer_token_hash = hash;
			return id;
		}
		if(s_site) {
			WebAccountSession ws;
			if(s_site->Accounts().ResolveSession(raw, ws) && ws.user_id.GetCount()) {
				WebAccountUser wu;
				UserRecord u;
				if(s_site->Accounts().GetUser(ws.user_id, wu) && !wu.disabled &&
				   s_store->FindUserByUsername(wu.username, u) && u.enabled)
					return CompanyHumanIdentity(u);
			}
		}
		return guest;
	}
	if(r.HasUser()) {
		UserRecord u;
		if(s_store->FindUserByUsername(r.User().username, u) && u.enabled)
			return CompanyHumanIdentity(u);
	}
	return guest;
}

static void CompanyJson(WebRequest& r, const Value& v, int code = 200)
{
	r.Json(v, code);
}

static void CompanyError(WebRequest& r, int code, const String& message)
{
	ValueMap m;
	m.Set("error", message);
	r.Json(m, code);
}

// Deny by default. 401 when anonymous, 403 when the identity lacks the role or scope.
static bool CompanyRequireAuth(WebRequest& r)
{
	if(CompanyIdentity(r).authenticated)
		return true;
	CompanyError(r, 401, "authentication required");
	return false;
}

static bool CompanyRequireRole(WebRequest& r, const String& role)
{
	WebAuthIdentity id = CompanyIdentity(r);
	if(!id.authenticated) {
		CompanyError(r, 401, "authentication required");
		return false;
	}
	if(!id.HasRole(role)) {
		CompanyError(r, 403, "role required: " + role);
		return false;
	}
	return true;
}

static bool CompanyRequireScope(WebRequest& r, const String& scope)
{
	WebAuthIdentity id = CompanyIdentity(r);
	if(!id.authenticated) {
		CompanyError(r, 401, "authentication required");
		return false;
	}
	if(!id.HasScope(scope)) {
		CompanyError(r, 403, "scope required: " + scope);
		return false;
	}
	return true;
}

// Creates the WebAccounts login for a Company user, with the same password.
// An existing login of the same name gets the new password, so the password
// the caller set is the one that works.
static bool CompanyProvisionLogin(const String& username, const String& password, const String& display_name,
                                  const Vector<String>& roles, String& error)
{
	if(!s_site) {
		error = "site is not installed";
		return false;
	}
	String uid;
	bool existed = false;
	StoreResult sr = s_site->Accounts().CreateUser(username, password, display_name, roles, uid, existed);
	if(sr.ok)
		return true;
	if(!existed) {
		error = sr.error;
		return false;
	}
	WebAccountUser account;
	if(!s_site->Accounts().FindUserByUsername(username, account)) {
		error = "login exists but cannot be found: " + username;
		return false;
	}
	StoreResult pw = s_site->Accounts().SetPassword(account.user_id, password);
	if(!pw.ok)
		error = pw.error;
	return pw.ok;
}

static bool IsJsonObjectBody(WebRequest& r)
{
	try {
		return ParseJSON(r.Body()).Is<ValueMap>();
	}
	catch(Exc&) {
		return false;
	}
}

static ValueMap ParseJsonBody(WebRequest& r)
{
	String content = r.Body();
	if(content.IsEmpty())
		return ValueMap();
	try {
		Value v = ParseJSON(content);
		if(v.Is<ValueMap>())
			return v;
	}
	catch(Exc&) {
	}
	return ValueMap();
}

// Reads an integer query parameter ("?pr_number=7"); 0 = absent/invalid.
static int QueryInt(WebRequest& r, const char* name)
{
	String v = TrimBoth(r.Param(name));
	return v.IsEmpty() ? 0 : ScanInt(v);
}

// ---------------------------------------------------------------------------
// JSON shapes (unchanged from CompanySkylark).
// ---------------------------------------------------------------------------


static ValueMap UserToJson(const UserRecord& u, bool include_hash = false)
{
	ValueMap m;
	m.Set("id", u.id);
	m.Set("username", u.username);
	m.Set("display_name", u.display_name);
	m.Set("enabled", u.enabled);
	ValueArray roles;
	for(int i = 0; i < u.roles.GetCount(); i++)
		roles.Add(u.roles[i]);
	m.Set("roles", roles);
	m.Set("hourly_rate", u.hourly_rate);
	m.Set("created_at", CompanyTimeIso(u.created_at));
	m.Set("updated_at", CompanyTimeIso(u.updated_at));
	if(include_hash)
		m.Set("password_hash", u.password_hash);
	return m;
}

static ValueMap ProposalToJson(CompanyStore& store, const ProposalRecord& p)
{
	ValueMap m;
	m.Set("id", p.id);
	m.Set("author_user_id", p.author_user_id);
	m.Set("author_token_id", p.author_token_id);
	m.Set("author_type", p.author_type);
	m.Set("title", p.title);
	m.Set("description", p.description);
	m.Set("repository", p.repository);
	m.Set("issue_number", p.issue_number);
	m.Set("estimated_hours", p.estimated_hours);
	m.Set("status", p.status);
	m.Set("reviewer_decision", p.reviewer_decision);
	m.Set("assigned_contributor_user_id", p.assigned_contributor_user_id);
	m.Set("created_at", CompanyTimeIso(p.created_at));
	m.Set("updated_at", CompanyTimeIso(p.updated_at));
	// Linked work item ids for convenience.
	ValueArray links;
	Vector<WorkItemRecord> items = store.GetWorkItems();
	for(int i = 0; i < items.GetCount(); i++)
		if(items[i].proposal_id == p.id)
			links.Add(items[i].id);
	m.Set("work_item_ids", links);
	return m;
}

static ValueMap WorkItemToJson(CompanyStore& store, const WorkItemRecord& w)
{
	ValueMap m;
	m.Set("id", w.id);
	m.Set("proposal_id", w.proposal_id);
	m.Set("contributor_user_id", w.contributor_user_id);
	m.Set("contributor_token_id", w.contributor_token_id);
	m.Set("contributor_type", w.contributor_type);
	m.Set("repository", w.repository);
	m.Set("pr_number", w.pr_number);
	m.Set("pr_url", w.pr_url);
	m.Set("branch", w.branch);
	ValueArray checks;
	for(int i = 0; i < w.required_checks.GetCount(); i++)
		checks.Add(w.required_checks[i]);
	m.Set("required_checks", checks);
	m.Set("state", w.state);
	ValueArray reviews;
	Vector<ReviewRecord> rv = store.ReviewsForWorkItem(w.id);
	for(int i = 0; i < rv.GetCount(); i++) {
		ValueMap rm;
		rm.Set("id", rv[i].id);
		rm.Set("kind", rv[i].kind);
		rm.Set("status", rv[i].status);
		rm.Set("evidence", rv[i].evidence);
		reviews.Add(rm);
	}
	m.Set("reviews", reviews);
	m.Set("created_at", CompanyTimeIso(w.created_at));
	m.Set("updated_at", CompanyTimeIso(w.updated_at));
	return m;
}

static ValueMap LedgerToJson(const LedgerEntry& e, bool include_user = true)
{
	ValueMap m;
	m.Set("seq", e.seq);
	m.Set("entry_id", e.entry_id);
	m.Set("type", e.type);
	m.Set("amount", e.amount);
	if(include_user)
		m.Set("user_id", e.user_id);
	m.Set("work_item_id", e.work_item_id);
	m.Set("reason", e.reason);
	m.Set("created_at", CompanyTimeIso(e.created_at));
	return m;
}

static ValueMap AuditToJson(const AuditEvent& e)
{
	ValueMap m;
	m.Set("seq", e.seq);
	m.Set("event_id", e.event_id);
	m.Set("actor_user_id", e.actor_user_id);
	m.Set("actor_token_id", e.actor_token_id);
	m.Set("actor_type", e.actor_type);
	ValueArray roles;
	for(int i = 0; i < e.actor_roles.GetCount(); i++)
		roles.Add(e.actor_roles[i]);
	m.Set("actor_roles", roles);
	m.Set("action", e.action);
	m.Set("target_type", e.target_type);
	m.Set("target_id", e.target_id);
	m.Set("prior_status", e.prior_status);
	m.Set("new_status", e.new_status);
	m.Set("reason", e.reason);
	m.Set("created_at", CompanyTimeIso(e.created_at));
	return m;
}


static String BodyString(const ValueMap& body, const char* key)
{
	return body.Find(key) >= 0 && body[key].Is<String>() ? (String)body[key] : String();
}

static int BodyInt(const ValueMap& body, const char* key, int def = 0)
{
	if(body.Find(key) < 0)
		return def;
	Value v = body[key];
	if(IsNumber(v))
		return (int)v;
	return def;
}

static double BodyDouble(const ValueMap& body, const char* key, double def = 0)
{
	if(body.Find(key) < 0)
		return def;
	Value v = body[key];
	if(IsNumber(v))
		return (double)v;
	return def;
}

static bool BodyHas(const ValueMap& body, const char* key)
{
	return body.Find(key) >= 0 && !IsNull(body[key]);
}


static ValueMap PlatformCommandToJson(const PlatformCommandRecord& c)
{
	ValueMap m;
	m.Set("id", c.id);
	m.Set("source", c.source);
	m.Set("repository", c.repository);
	m.Set("pr_number", c.pr_number);
	m.Set("actor_login", c.actor_login);
	m.Set("command", c.command);
	m.Set("arguments", c.arguments);
	m.Set("head_sha", c.head_sha);
	m.Set("status", c.status);
	m.Set("refusal_reason", c.refusal_reason);
	m.Set("created_at", CompanyTimeIso(c.created_at));
	return m;
}

static ValueMap PlatformDispatchToJson(CompanyStore& store, const PlatformReviewDispatch& d)
{
	ValueMap m;
	m.Set("id", d.id);
	m.Set("work_item_id", d.work_item_id);
	m.Set("repository", d.repository);
	m.Set("pr_number", d.pr_number);
	m.Set("kind", d.kind);
	m.Set("head_sha", d.head_sha);
	m.Set("base_sha", d.base_sha);
	m.Set("status", d.status);
	m.Set("created_at", CompanyTimeIso(d.created_at));
	m.Set("updated_at", CompanyTimeIso(d.updated_at));
	ValueArray attempts;
	Vector<PlatformReviewAttempt> atms = store.AttemptsForDispatch(d.id);
	for(int i = 0; i < atms.GetCount(); i++) {
		ValueMap am;
		am.Set("id", atms[i].id);
		am.Set("external_service", atms[i].external_service);
		am.Set("run_id", atms[i].run_id);
		am.Set("status", atms[i].status);
		am.Set("supersedes", atms[i].supersedes);
		attempts.Add(am);
	}
	m.Set("attempts", attempts);
	ValueArray evs;
	Vector<PlatformReviewEvidence> evidence = store.EvidenceForDispatch(d.id);
	for(int i = 0; i < evidence.GetCount(); i++) {
		ValueMap em;
		em.Set("id", evidence[i].id);
		em.Set("attempt_id", evidence[i].attempt_id);
		em.Set("head_sha", evidence[i].head_sha);
		em.Set("outcome", evidence[i].outcome);
		em.Set("check_url", evidence[i].check_url);
		em.Set("run_id", evidence[i].run_id);
		em.Set("tool_version", evidence[i].tool_version);
		evs.Add(em);
	}
	m.Set("evidence", evs);
	return m;
}

static ValueMap PlatformTrialToJson(CompanyStore& store, const PlatformTrialRecord& t)
{
	ValueMap m;
	m.Set("id", t.id);
	m.Set("kind", t.kind);
	m.Set("work_item_id", t.work_item_id);
	m.Set("repository", t.repository);
	m.Set("pr_number", t.pr_number);
	m.Set("head_sha", t.head_sha);
	ValueArray splits;
	for(int i = 0; i < t.splits.GetCount(); i++)
		splits.Add(t.splits[i]);
	m.Set("splits", splits);
	m.Set("trials_per_split", t.trials_per_split);
	m.Set("status", t.status);
	m.Set("expected_results", t.expected_results);
	m.Set("received_results", t.received_results);
	ValueArray results;
	Vector<PlatformTrialResultRecord> rs = store.ResultsForTrial(t.id);
	for(int i = 0; i < rs.GetCount(); i++) {
		ValueMap rm;
		rm.Set("id", rs[i].id);
		rm.Set("split", rs[i].split);
		rm.Set("trial_index", rs[i].trial_index);
		rm.Set("outcome", rs[i].outcome);
		rm.Set("passed", rs[i].passed);
		rm.Set("reward", rs[i].reward);
		rm.Set("summary", rs[i].summary);
		results.Add(rm);
	}
	m.Set("results", results);
	m.Set("created_at", CompanyTimeIso(t.created_at));
	return m;
}

static bool PlatformListenerAuthorized(const WebAuthIdentity& identity)
{
	return identity.IsAutomation() && identity.HasScope(COMPANY_SCOPE_CALLBACK_WRITE)
	    || identity.IsHuman() && identity.IsAdmin();
}

static bool PlatformTrialAuthorized(const WebAuthIdentity& identity)
{
	return identity.IsAutomation() && identity.HasScope(COMPANY_SCOPE_TRIALS_WRITE)
	    || identity.IsHuman() && identity.IsAdmin();
}

static bool PlatformArtifactAuthorized(const WebAuthIdentity& identity)
{
	return identity.IsAutomation()
	    && (identity.HasScope(COMPANY_SCOPE_CALLBACK_WRITE) || identity.HasScope(COMPANY_SCOPE_TRIALS_WRITE))
	    || identity.IsHuman() && identity.IsAdmin();
}


static ValueMap ContractToJson(const ContractRecord& c)
{
	ValueMap m;
	m.Set("id", c.id);
	m.Set("client_user_id", c.client_user_id);
	m.Set("title", c.title);
	m.Set("brief", c.brief);
	m.Set("amount", c.amount);
	m.Set("currency", c.currency);
	m.Set("payment_terms", c.payment_terms);
	m.Set("practical_details", c.practical_details);
	m.Set("status", c.status);
	m.Set("created_by", c.created_by);
	m.Set("created_at", CompanyTimeIso(c.created_at));
	m.Set("updated_at", CompanyTimeIso(c.updated_at));
	return m;
}

static ValueMap ProjectToJson(const ProjectRecord& p)
{
	ValueMap m;
	m.Set("id", p.id);
	m.Set("contract_id", p.contract_id);
	m.Set("repo_name", p.repo_name);
	m.Set("repo_origin", p.repo_origin);
	m.Set("goals", p.goals);
	m.Set("status", p.status);
	ValueArray employees;
	for(int i = 0; i < p.employee_user_ids.GetCount(); i++)
		employees.Add(p.employee_user_ids[i]);
	m.Set("employee_user_ids", employees);
	ValueArray prs;
	for(int i = 0; i < p.accepted_prs.GetCount(); i++)
		prs.Add(p.accepted_prs[i]);
	m.Set("accepted_prs", prs);
	m.Set("created_at", CompanyTimeIso(p.created_at));
	m.Set("updated_at", CompanyTimeIso(p.updated_at));
	return m;
}

// Whether the identity may read the contract: an admin, or the client that owns it.
static bool CanReadContract(const WebAuthIdentity& id, const ContractRecord& c)
{
	return CompanyWorkflow::IsAdminHuman(id) || (CompanyWorkflow::IsClientHuman(id) && c.client_user_id == id.user_id);
}

// Whether the identity may read the project: an admin, the client of its
// contract, or an employee assigned to it.
static bool CanReadProject(CompanyStore& store, const WebAuthIdentity& id, const ProjectRecord& p)
{
	if(CompanyWorkflow::IsAdminHuman(id) || (CompanyWorkflow::IsEmployeeHuman(id) && p.HasEmployee(id.user_id)))
		return true;
	ContractRecord c;
	return CompanyWorkflow::IsClientHuman(id) && store.FindContract(p.contract_id, c) && c.client_user_id == id.user_id;
}

// Deny messages answer 403; missing records answer 404; other rule failures 400.
static int ClientErrorCode(const String& error)
{
	if(error.Find("not found") >= 0)
		return 404;
	if(error.Find("role") >= 0 || error.Find("only ") >= 0 || error.Find("admin ") >= 0 || error.Find("must be assigned") >= 0)
		return 403;
	return 400;
}

static void SendClientResult(WebRequest& http, const CompanyWorkflowResult& result, const char* id_key)
{
	if(!result.ok) {
		CompanyError(http, result.idempotent_duplicate ? 200 : ClientErrorCode(result.error), result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("applied"));
	m.Set(id_key, result.payload.Get(id_key, ""));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}



// ---------------------------------------------------------------------------
// REST, v2 and browser handlers.
// ---------------------------------------------------------------------------


static ValueMap ManifestArg(const char* name, const char* type, bool required)
{
	ValueMap a;
	a.Set("name", name);
	a.Set("type", type);
	a.Set("required", required);
	a.Set("description", "");
	return a;
}

static ValueMap ManifestCommand(const char* name, const char* method, const char* api_path, const char* auth,
                                const char* description, ValueArray args)
{
	ValueMap c;
	c.Set("name", name);
	c.Set("transport", "http");
	c.Set("method", method);
	c.Set("api_path", api_path);
	c.Set("auth", auth);
	c.Set("response", "json");
	c.Set("description", description);
	c.Set("args", args);
	return c;
}

static ValueMap ManifestSite(const char* id, const char* title, ValueArray commands)
{
	ValueMap s;
	s.Set("id", id);
	s.Set("title", title);
	s.Set("commands", commands);
	return s;
}

static ValueMap CompanyCommandManifest()
{
	ValueArray sites;

	ValueArray company;
	company.Add(ManifestCommand("contracts-list", "GET", "api/contracts", "signed in",
	                            "List the contracts you may see", ValueArray()));
	ValueArray detail;
	detail.Add(ManifestArg("id", "string", true));
	company.Add(ManifestCommand("contract-get", "GET", "api/contracts/*", "signed in", "Read one contract", detail));
	ValueArray negotiate;
	negotiate.Add(ManifestArg("contract_id", "string", true));
	negotiate.Add(ManifestArg("amount", "number", true));
	negotiate.Add(ManifestArg("payment_terms", "text", false));
	negotiate.Add(ManifestArg("idempotency_key", "string", true));
	company.Add(ManifestCommand("contract-negotiate", "POST", "api/contracts/negotiate", "admin",
	                            "Set the amount and payment terms", negotiate));
	ValueArray key_only;
	key_only.Add(ManifestArg("contract_id", "string", true));
	key_only.Add(ManifestArg("idempotency_key", "string", true));
	company.Add(ManifestCommand("contract-accept", "POST", "api/contracts/accept", "admin",
	                            "Accept a contract into the active state", key_only));
	company.Add(ManifestCommand("contract-complete", "POST", "api/contracts/complete", "admin",
	                            "Close an active contract as completed", key_only));
	company.Add(ManifestCommand("contract-cancel", "POST", "api/contracts/cancel", "admin",
	                            "Close an active contract as cancelled", key_only));
	company.Add(ManifestCommand("users-list", "GET", "api/users", "admin",
	                            "List users with their ids and roles", ValueArray()));
	ValueArray proposal_create;
	proposal_create.Add(ManifestArg("title", "string", true));
	proposal_create.Add(ManifestArg("description", "text", true));
	proposal_create.Add(ManifestArg("repository", "string", true));
	proposal_create.Add(ManifestArg("estimated_hours", "number", true));
	proposal_create.Add(ManifestArg("idempotency_key", "string", true));
	company.Add(ManifestCommand("proposal-create", "POST", "api/proposals/create", "contributor",
	                            "Propose a task on an allowlisted repository", proposal_create));
	ValueArray proposal_decide;
	proposal_decide.Add(ManifestArg("proposal_id", "string", true));
	proposal_decide.Add(ManifestArg("action", "string", true));
	proposal_decide.Add(ManifestArg("reason", "text", false));
	proposal_decide.Add(ManifestArg("assign_contributor_user_id", "string", false));
	proposal_decide.Add(ManifestArg("idempotency_key", "string", true));
	company.Add(ManifestCommand("proposal-decide", "POST", "api/proposals/decide", "reviewer",
	                            "Accept, reject or request changes; accept assigns the contributor", proposal_decide));
	company.Add(ManifestCommand("work-list", "GET", "api/work", "signed in",
	                            "List work items", ValueArray()));
	ValueArray work_bind;
	work_bind.Add(ManifestArg("work_item_id", "string", true));
	work_bind.Add(ManifestArg("repository", "string", true));
	work_bind.Add(ManifestArg("pr_number", "number", true));
	work_bind.Add(ManifestArg("pr_url", "string", false));
	work_bind.Add(ManifestArg("branch", "string", false));
	work_bind.Add(ManifestArg("idempotency_key", "string", true));
	company.Add(ManifestCommand("work-bind", "POST", "api/work/bind", "contributor",
	                            "Bind the pull request that implements a work item", work_bind));
	ValueArray user_create;
	user_create.Add(ManifestArg("username", "string", true));
	user_create.Add(ManifestArg("display_name", "string", true));
	user_create.Add(ManifestArg("roles", "string", true));
	user_create.Add(ManifestArg("password", "string", true));
	user_create.Add(ManifestArg("idempotency_key", "string", true));
	company.Add(ManifestCommand("user-create", "POST", "api/users/create", "admin",
	                            "Create a user with comma-separated roles", user_create));
	ValueArray allowlist;
	allowlist.Add(ManifestArg("repositories", "string", true));
	allowlist.Add(ManifestArg("idempotency_key", "string", true));
	company.Add(ManifestCommand("allowlist-set", "POST", "api/config/allowlist", "admin",
	                            "Replace the repository allowlist (comma-separated owner/repo)", allowlist));
	company.Add(ManifestCommand("projects-list", "GET", "api/projects", "signed in",
	                            "List the projects you may see", ValueArray()));
	ValueArray project_create;
	project_create.Add(ManifestArg("contract_id", "string", true));
	project_create.Add(ManifestArg("repo_name", "string", true));
	project_create.Add(ManifestArg("repo_origin", "string", true));
	project_create.Add(ManifestArg("goals", "text", false));
	project_create.Add(ManifestArg("idempotency_key", "string", true));
	company.Add(ManifestCommand("project-create", "POST", "api/projects/create", "admin",
	                            "Create a project under an active contract", project_create));
	ValueArray project_assign;
	project_assign.Add(ManifestArg("project_id", "string", true));
	project_assign.Add(ManifestArg("employee_user_id", "string", true));
	project_assign.Add(ManifestArg("idempotency_key", "string", true));
	company.Add(ManifestCommand("project-assign", "POST", "api/projects/assign", "admin",
	                            "Assign an employee to a project", project_assign));
	ValueArray project_key;
	project_key.Add(ManifestArg("project_id", "string", true));
	project_key.Add(ManifestArg("idempotency_key", "string", true));
	company.Add(ManifestCommand("project-start", "POST", "api/projects/start", "admin",
	                            "Start a planning project", project_key));
	company.Add(ManifestCommand("project-request-completion", "POST", "api/projects/request-completion", "admin",
	                            "Move an active project to completing", project_key));
	company.Add(ManifestCommand("project-ready", "POST", "api/projects/ready", "admin",
	                            "Mark a completing project ready", project_key));
	sites.Add(ManifestSite("company", "Company", company));

	ValueArray client;
	ValueArray client_create;
	client_create.Add(ManifestArg("title", "string", true));
	client_create.Add(ManifestArg("brief", "text", false));
	client_create.Add(ManifestArg("amount", "number", true));
	client_create.Add(ManifestArg("currency", "string", true));
	client_create.Add(ManifestArg("payment_terms", "text", false));
	client_create.Add(ManifestArg("practical_details", "text", false));
	client_create.Add(ManifestArg("idempotency_key", "string", true));
	client.Add(ManifestCommand("contract-create", "POST", "api/contracts/create", "client",
	                           "Propose a contract to the company", client_create));
	client.Add(ManifestCommand("contracts-list", "GET", "api/contracts", "signed in",
	                           "List your contracts", ValueArray()));
	client.Add(ManifestCommand("projects-list", "GET", "api/projects", "signed in",
	                           "List the projects under your contracts", ValueArray()));
	ValueArray deliver;
	deliver.Add(ManifestArg("project_id", "string", true));
	deliver.Add(ManifestArg("idempotency_key", "string", true));
	client.Add(ManifestCommand("project-deliver", "POST", "api/projects/deliver", "client",
	                           "Accept the delivery of a ready project", deliver));
	sites.Add(ManifestSite("client", "Client", client));

	ValueArray employee;
	ValueArray accept_pr;
	accept_pr.Add(ManifestArg("project_id", "string", true));
	accept_pr.Add(ManifestArg("pr_ref", "string", true));
	accept_pr.Add(ManifestArg("idempotency_key", "string", true));
	employee.Add(ManifestCommand("project-accept-pr", "POST", "api/projects/accept-pr", "employee",
	                             "Accept a pull request for an active project you are assigned to", accept_pr));
	ValueArray scope;
	scope.Add(ManifestArg("proposal_id", "string", true));
	scope.Add(ManifestArg("project_id", "string", true));
	scope.Add(ManifestArg("idempotency_key", "string", true));
	employee.Add(ManifestCommand("proposal-scope", "POST", "api/proposals/scope", "employee or admin",
	                             "Scope a task proposal to a project", scope));
	ValueArray adopt;
	adopt.Add(ManifestArg("work_item_id", "string", true));
	adopt.Add(ManifestArg("idempotency_key", "string", true));
	employee.Add(ManifestCommand("work-adopt", "POST", "api/work/adopt", "employee",
	                             "Adopt a contributor's work item of your project", adopt));
	sites.Add(ManifestSite("employee", "Employee", employee));

	ValueMap manifest;
	manifest.Set("schema", "skylark-command-manifest/1");
	manifest.Set("sites", sites);
	return manifest;
}


static void ApiHealth(WebRequest& http)
{
	CompanyStore* store = s_store;
	ValueMap m;
	m.Set("status", "ok");
	m.Set("service", "Company");
	m.Set("time", CompanyTimeIso(GetSysTime()));
	if(store) {
		m.Set("data_root", store->GetRoot());
		Vector<String> corruption = store->GetCorruptionErrors();
		m.Set("corruption", corruption.IsEmpty() ? Value("none") : Value(Join(corruption, "; ")));
	}
	CompanyJson(http, m);
}

static void ApiFrameworkSpec(WebRequest& http)
{
	CompanyJson(http, CompanyViews::BuildFrameworkSpec());
}

static void ApiAuthStatus(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap m;
	m.Set("authenticated", identity.authenticated);
	m.Set("actor_type", identity.actor_type);
	m.Set("user_id", identity.user_id);
	m.Set("token_id", identity.token_id);
	m.Set("display_name", identity.display_name);
	ValueArray roles;
	for(int i = 0; i < identity.roles.GetCount(); i++)
		roles.Add(identity.roles[i]);
	m.Set("roles", roles);
	ValueArray scopes;
	Vector<String> eff = CompanyEffectiveScopes(identity);
	for(int i = 0; i < eff.GetCount(); i++)
		scopes.Add(eff[i]);
	m.Set("scopes", scopes);
	CompanyJson(http, m);
}

static void ApiAuthLogin(WebRequest& http)
{
	if(!http.Body().IsEmpty() && !IsJsonObjectBody(http)) {
		CompanyError(http, 400, "request body must be a JSON object");
		return;
	}
	ValueMap body = ParseJsonBody(http);
	String session_id, csrf, error;
	WebAccountUser user;
	StoreResult lr = s_site->Accounts().Login(BodyString(body, "username"), BodyString(body, "password"),
	                                          http.ClientIp(), session_id, csrf, user, error);
	// One message for every failure, so the response does not reveal which
	// usernames exist.
	if(!lr.ok || session_id.IsEmpty()) {
		CompanyError(http, 401, "invalid credentials");
		return;
	}
	// Fail closed: the account must map to an enabled Company user.
	UserRecord cu;
	if(!s_store || !s_store->FindUserByUsername(user.username, cu) || !cu.enabled) {
		s_site->Accounts().Logout(session_id);
		CompanyError(http, 401, "invalid credentials");
		return;
	}
	ValueMap m;
	m.Set("token", session_id);
	m.Set("token_type", "Bearer");
	m.Set("expires_hours", s_site->session_ttl_hours);
	CompanyJson(http, m);
}

static void ApiAuthLogout(WebRequest& http)
{
	String auth = TrimBoth(http.Header("authorization"));
	if(ToLower(auth.Left(7)) == "bearer ")
		s_site->Accounts().Logout(TrimBoth(auth.Mid(7)));
	http.DropSession();
	ValueMap m;
	m.Set("status", "logged_out");
	CompanyJson(http, m);
}

// ---------------------------------------------------------------------------
// Proposals
// ---------------------------------------------------------------------------

static void ApiProposals(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_PROPOSALS_READ))
		return;
	CompanyStore* store = s_store;
	Vector<ProposalRecord> proposals = store->GetProposals();
	ValueArray arr;
	for(int i = 0; i < proposals.GetCount(); i++)
		arr.Add(ProposalToJson(*store, proposals[i]));
	ValueMap m;
	m.Set("proposals", arr);
	m.Set("count", proposals.GetCount());
	CompanyJson(http, m);
}

static void ApiProposalCreate(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_PROPOSALS_WRITE))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	String key = BodyString(body, "idempotency_key");
	CompanyWorkflowResult result = CompanyWorkflow::CreateProposal(identity,
	                                                               BodyString(body, "title"),
	                                                               BodyString(body, "description"),
	                                                               BodyString(body, "repository"),
	                                                               BodyDouble(body, "estimated_hours"),
	                                                               key);
	if(!result.ok) {
		CompanyError(http, result.idempotent_duplicate ? 200 : 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("created"));
	m.Set("proposal_id", result.payload.Get("proposal_id", ""));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}

static void ApiProposalDetail(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_PROPOSALS_READ))
		return;
	CompanyStore* store = s_store;
	ProposalRecord p;
	if(!store->FindProposal(http.PathArg(0), p)) {
		CompanyError(http, 404, "proposal not found");
		return;
	}
	CompanyJson(http, ProposalToJson(*store, p));
}

// ---------------------------------------------------------------------------
// Work items
// ---------------------------------------------------------------------------

static void ApiWork(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_WORK_READ))
		return;
	CompanyStore* store = s_store;
	Vector<WorkItemRecord> items = store->GetWorkItems();
	// Optional scoped query: repository and pr_number narrow the result so
	// integrations (e.g. the GitHub workflow) never pull the full list.
	String query_repo = TrimBoth(http.Param("repository"));
	int query_pr = QueryInt(http, "pr_number");
	int matched = 0;
	ValueArray arr;
	for(int i = 0; i < items.GetCount(); i++) {
		if(!query_repo.IsEmpty() && items[i].repository != query_repo)
			continue;
		if(query_pr > 0 && items[i].pr_number != query_pr)
			continue;
		arr.Add(WorkItemToJson(*store, items[i]));
		matched++;
	}
	ValueMap m;
	m.Set("work_items", arr);
	m.Set("count", matched);
	CompanyJson(http, m);
}

static void ApiWorkDetail(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_WORK_READ))
		return;
	CompanyStore* store = s_store;
	WorkItemRecord w;
	if(!store->FindWorkItem(http.PathArg(0), w)) {
		CompanyError(http, 404, "work item not found");
		return;
	}
	CompanyJson(http, WorkItemToJson(*store, w));
}

static void ApiWorkBind(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_WORK_WRITE))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = CompanyWorkflow::BindPullRequest(identity,
	                                                                BodyString(body, "work_item_id"),
	                                                                BodyString(body, "repository"),
	                                                                BodyInt(body, "pr_number"),
	                                                                BodyString(body, "pr_url"),
	                                                                BodyString(body, "branch"),
	                                                                BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("bound"));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m);
}

static void ApiWorkMechanicalRequest(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_WORK_WRITE))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = CompanyWorkflow::RequestMechanicalReview(identity,
	                                                                        BodyString(body, "work_item_id"),
	                                                                        BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("requested"));
	m.Set("review_id", result.payload.Get("review_id", ""));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m);
}

static void ApiWorkOperatorRequest(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_WORK_WRITE))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = CompanyWorkflow::RequestOperatorReview(identity,
	                                                                      BodyString(body, "work_item_id"),
	                                                                      BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("requested"));
	m.Set("review_id", result.payload.Get("review_id", ""));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m);
}

// ---------------------------------------------------------------------------
// Reviews
// ---------------------------------------------------------------------------

static void ApiReviews(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_REVIEWS_READ))
		return;
	CompanyStore* store = s_store;
	Vector<ReviewRecord> reviews = store->GetReviews();
	ValueArray arr;
	for(int i = 0; i < reviews.GetCount(); i++) {
		ValueMap m;
		m.Set("id", reviews[i].id);
		m.Set("work_item_id", reviews[i].work_item_id);
		m.Set("proposal_id", reviews[i].proposal_id);
		m.Set("kind", reviews[i].kind);
		m.Set("status", reviews[i].status);
		m.Set("actor_type", reviews[i].actor_type);
		m.Set("evidence", reviews[i].evidence);
		arr.Add(m);
	}
	ValueMap out;
	out.Set("reviews", arr);
	out.Set("count", reviews.GetCount());
	CompanyJson(http, out);
}

static void ApiReviewDetail(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_REVIEWS_READ))
		return;
	CompanyStore* store = s_store;
	ReviewRecord r;
	if(!store->FindReview(http.PathArg(0), r)) {
		CompanyError(http, 404, "review not found");
		return;
	}
	ValueMap m;
	m.Set("id", r.id);
	m.Set("work_item_id", r.work_item_id);
	m.Set("proposal_id", r.proposal_id);
	m.Set("kind", r.kind);
	m.Set("status", r.status);
	m.Set("actor_user_id", r.actor_user_id);
	m.Set("actor_token_id", r.actor_token_id);
	m.Set("actor_type", r.actor_type);
	m.Set("evidence", r.evidence);
	m.Set("decision_history", r.decision_history);
	CompanyJson(http, m);
}

static void ApiOperatorDecide(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_REVIEWS_WRITE))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = CompanyWorkflow::DecideOperatorReview(identity,
	                                                                     BodyString(body, "work_item_id"),
	                                                                     BodyString(body, "action"),
	                                                                     BodyString(body, "reason"),
	                                                                     BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, 403, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("recorded"));
	m.Set("review_id", result.payload.Get("review_id", ""));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m);
}

// ---------------------------------------------------------------------------
// Ledger
// ---------------------------------------------------------------------------

static void ApiLedger(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_LEDGER_READ))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	CompanyStore* store = s_store;
	Vector<LedgerEntry> entries = store->GetLedger();
	ValueArray arr;
	for(int i = 0; i < entries.GetCount(); i++) {
		// Non-admin identities see only their own entries.
		if(!identity.IsAdmin() && entries[i].user_id != identity.user_id)
			continue;
		arr.Add(LedgerToJson(entries[i], identity.IsAdmin()));
	}
	ValueMap m;
	m.Set("entries", arr);
	CompanyJson(http, m);
}

static void ApiLedgerBalance(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_LEDGER_READ))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	CompanyStore* store = s_store;
	ValueMap m;
	m.Set("balance", store->LedgerBalance(identity.user_id));
	m.Set("user_id", identity.user_id);
	CompanyJson(http, m);
}

// Admin corrections are separate (see ApiLedgerCorrection).

// ---------------------------------------------------------------------------
// Admin API
// ---------------------------------------------------------------------------

static void ApiAudit(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	CompanyStore* store = s_store;
	Vector<AuditEvent> events = store->GetAudit();
	ValueArray arr;
	for(int i = 0; i < events.GetCount(); i++)
		arr.Add(AuditToJson(events[i]));
	ValueMap m;
	m.Set("events", arr);
	m.Set("count", events.GetCount());
	CompanyJson(http, m);
}

static void ApiUsers(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	CompanyStore* store = s_store;
	Vector<UserRecord> users = store->GetUsers();
	ValueArray arr;
	for(int i = 0; i < users.GetCount(); i++)
		arr.Add(UserToJson(users[i]));
	ValueMap m;
	m.Set("users", arr);
	m.Set("count", users.GetCount());
	CompanyJson(http, m);
}

static void ApiUserRoles(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	Vector<String> roles;
	if(BodyHas(body, "roles") && body["roles"].Is<ValueArray>()) {
		ValueArray ra = body["roles"];
		for(int i = 0; i < ra.GetCount(); i++)
			roles.Add((String)ra[i]);
	}
	CompanyWorkflowResult result = CompanyWorkflow::SetUserRoles(identity, BodyString(body, "user_id"),
	                                                             roles, BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("updated"));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m);
}

static void ApiTokens(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	CompanyStore* store = s_store;
	Vector<AgentTokenRecord> tokens = store->GetTokens();
	ValueArray arr;
	for(int i = 0; i < tokens.GetCount(); i++) {
		ValueMap m;
		m.Set("id", tokens[i].id);
		m.Set("name", tokens[i].name);
		m.Set("owner_user_id", tokens[i].owner_user_id);
		ValueArray scopes;
		for(int s = 0; s < tokens[i].scopes.GetCount(); s++)
			scopes.Add(tokens[i].scopes[s]);
		m.Set("scopes", scopes);
		m.Set("created_at", CompanyTimeIso(tokens[i].created_at));
		m.Set("expires_at", IsNull(tokens[i].expires_at) ? Value("") : Value(CompanyTimeIso(tokens[i].expires_at)));
		m.Set("revoked", !IsNull(tokens[i].revoked_at));
		m.Set("last_used_at", CompanyTimeIso(tokens[i].last_used_at));
		arr.Add(m);
	}
	ValueMap out;
	out.Set("tokens", arr);
	out.Set("count", tokens.GetCount());
	CompanyJson(http, out);
}

static void ApiTokenCreate(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	Vector<String> scopes;
	if(BodyHas(body, "scopes") && body["scopes"].Is<ValueArray>()) {
		ValueArray sa = body["scopes"];
		for(int i = 0; i < sa.GetCount(); i++)
			scopes.Add((String)sa[i]);
	}
	else {
		Vector<String> parts = Split(BodyString(body, "scopes"), ',');
		for(int i = 0; i < parts.GetCount(); i++) {
			String s = TrimBoth(parts[i]);
			if(!s.IsEmpty())
				scopes.Add(s);
		}
	}
	String expires = BodyString(body, "expires_at");
	Time expires_at = expires.IsEmpty() ? Time() : ScanTime(expires);

	String raw;
	CompanyWorkflowResult result = CompanyWorkflow::CreateAgentToken(identity, BodyString(body, "name"),
	                                                                 BodyString(body, "owner_user_id"), scopes,
	                                                                 expires_at, BodyString(body, "idempotency_key"),
	                                                                 raw);
	if(!result.ok) {
		CompanyError(http, 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("created"));
	m.Set("token_id", result.payload.Get("token_id", ""));
	// The raw token is returned exactly once.
	m.Set("raw_token", raw);
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}

static void ApiTokenRevoke(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = CompanyWorkflow::RevokeAgentToken(identity, BodyString(body, "token_id"),
	                                                                 BodyString(body, "reason"),
	                                                                 BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("revoked"));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m);
}

static void ApiConfig(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	CompanyStore* store = s_store;
	const CompanyConfig& cfg = store->GetConfig();
	ValueMap m;
	ValueArray repos;
	for(int i = 0; i < cfg.repository_allowlist.GetCount(); i++)
		repos.Add(cfg.repository_allowlist[i]);
	m.Set("repository_allowlist", repos);
	m.Set("reward_policy", cfg.reward_policy);
	m.Set("review_policy", cfg.review_policy);
	m.Set("agent_reviewer", cfg.agent_reviewer);
	m.Set("updated_at", CompanyTimeIso(cfg.updated_at));
	CompanyJson(http, m);
}

static void ApiConfigUpdate(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = CompanyWorkflow::UpdateConfigPolicy(identity,
	                                                                   body.Find("reward_policy") >= 0 && body["reward_policy"].Is<ValueMap>() ? ValueMap(body["reward_policy"]) : ValueMap(),
	                                                                   body.Find("review_policy") >= 0 && body["review_policy"].Is<ValueMap>() ? ValueMap(body["review_policy"]) : ValueMap(),
	                                                                   body.Find("agent_reviewer") >= 0 && body["agent_reviewer"].Is<ValueMap>() ? ValueMap(body["agent_reviewer"]) : ValueMap(),
	                                                                   BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("updated"));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m);
}

static void ApiLedgerCorrection(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	WebAuthIdentity identity = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = CompanyWorkflow::LedgerCorrection(identity,
	                                                                 BodyString(body, "user_id"),
	                                                                 BodyInt(body, "amount"),
	                                                                 BodyString(body, "reason"),
	                                                                 BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("recorded"));
	m.Set("entry_id", result.payload.Get("entry_id", ""));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m);
}

// ---------------------------------------------------------------------------
// GitHub Actions review callback ingestion (untrusted, replayable)
// ---------------------------------------------------------------------------

static void ApiReviewCallback(WebRequest& http)
{
	// 1. Authentication: only callback-scoped tokens (automation) or admins.
	WebAuthIdentity identity = CompanyIdentity(http);
	bool authorized = (identity.IsAutomation() && identity.HasScope(COMPANY_SCOPE_CALLBACK_WRITE)) ||
	                  (identity.IsHuman() && identity.IsAdmin());
	if(!identity.authenticated || !authorized) {
		CompanyError(http, 401, "callback requires callback:write scope");
		return;
	}

	ValueMap body = ParseJsonBody(http);
	String work_item_id = BodyString(body, "work_item_id");
	String repository = BodyString(body, "repository");
	int pr_number = BodyInt(body, "pr_number");
	String event_id = BodyString(body, "event_id");
	bool passed = BodyHas(body, "passed") ? (bool)body["passed"] : false;

	CompanyStore* store = s_store;

	// 2. The work item must exist.
	WorkItemRecord w;
	if(!store->FindWorkItem(work_item_id, w)) {
		CompanyError(http, 404, "work item not found");
		return;
	}

	// 3. Repository allowlist + PR association verification. A GitHub comment
	//    is untrusted: the callback must identify the recorded work item.
	if(!store->IsRepositoryAllowed(repository)) {
		CompanyWorkflow::Audit(COMPANY_AUD_CALLBACK_REJECTED, "work_item", work_item_id, w.state, w.state,
		                       identity, "repository not allowlisted", event_id);
		CompanyError(http, 403, "repository is not allowlisted");
		return;
	}
	if(repository != w.repository) {
		CompanyWorkflow::Audit(COMPANY_AUD_CALLBACK_REJECTED, "work_item", work_item_id, w.state, w.state,
		                       identity, "repository mismatch", event_id);
		CompanyError(http, 403, "repository does not match the work item");
		return;
	}
	if(pr_number != w.pr_number) {
		CompanyWorkflow::Audit(COMPANY_AUD_CALLBACK_REJECTED, "work_item", work_item_id, w.state, w.state,
		                       identity, "pr number mismatch", event_id);
		CompanyError(http, 403, "pr number does not match the work item");
		return;
	}
	if(event_id.IsEmpty()) {
		CompanyError(http, 400, "event_id is required");
		return;
	}

	// 4. Evidence bound and capped.
	ValueMap evidence;
	evidence.Set("run_id", BodyString(body, "run_id"));
	evidence.Set("comment_url", BodyString(body, "comment_url"));
	evidence.Set("findings", BodyString(body, "findings"));
	evidence.Set("event_id", event_id);
	evidence.Set("source", "github_actions");
	if(BodyHas(body, "checks") && body["checks"].Is<ValueArray>()) {
		ValueArray src = body["checks"];
		ValueArray checks;
		for(int i = 0; i < src.GetCount() && i < 32; i++)
			checks.Add(src[i]);
		evidence.Set("checks", checks);
	}

	// 5. Record the mechanical result (state transition + idempotent by event).
	CompanyWorkflowResult result = CompanyWorkflow::RecordMechanicalResult(identity, work_item_id,
	                                                                       passed, evidence, event_id);
	if(!result.ok) {
		CompanyError(http, 409, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("recorded"));
	m.Set("review_id", result.payload.Get("review_id", ""));
	m.Set("work_item_state", passed ? String(COMPANY_STATE_OPERATOR) : String(COMPANY_STATE_IMPLEMENTATION));
	m.Set("event_id", result.event_id);
	// Compensation is NEVER awarded from a GitHub callback.
	CompanyJson(http, m);
}

// ---------------------------------------------------------------------------
// Platform v2 API (reference-shaped contracts): command listener, review
// dispatch/evidence, trials, and artifacts. Mutations are :POST_RAW with
// bearer scopes; reads require admin or the matching read scope.
// ---------------------------------------------------------------------------

static void ApiV2CommandIngest(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !PlatformListenerAuthorized(identity)) {
		CompanyError(http, 401, "command ingest requires callback:write scope (automation) or admin role");
		return;
	}
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = PlatformWorkflow::IngestCommand(identity,
	                                                               BodyString(body, "source"),
	                                                               BodyString(body, "repository"),
	                                                               BodyInt(body, "pr_number"),
	                                                               BodyString(body, "actor_login"),
	                                                               BodyString(body, "command"),
	                                                               BodyString(body, "head_sha"),
	                                                               BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, result.idempotent_duplicate ? 200 : 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("ingested"));
	m.Set("command_id", result.payload.Get("command_id", ""));
	m.Set("command", result.payload.Get("command", ""));
	m.Set("command_status", result.payload.Get("status", ""));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}

static void ApiV2Commands(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	CompanyStore* store = s_store;
	Vector<PlatformCommandRecord> commands = store->GetPlatformCommands();
	ValueArray arr;
	for(int i = 0; i < commands.GetCount(); i++)
		arr.Add(PlatformCommandToJson(commands[i]));
	ValueMap m;
	m.Set("commands", arr);
	m.Set("count", commands.GetCount());
	CompanyJson(http, m);
}

static void ApiV2ReviewDispatch(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !PlatformListenerAuthorized(identity)) {
		CompanyError(http, 401, "review dispatch requires callback:write scope (automation) or admin role");
		return;
	}
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = PlatformWorkflow::DispatchReview(identity,
	                                                                BodyString(body, "work_item_id"),
	                                                                BodyString(body, "kind"),
	                                                                BodyString(body, "head_sha"),
	                                                                BodyString(body, "base_sha"),
	                                                                BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, result.idempotent_duplicate ? 200 : 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("dispatched"));
	m.Set("dispatch_id", result.payload.Get("dispatch_id", ""));
	m.Set("attempt_id", result.payload.Get("attempt_id", ""));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}

static void ApiV2ReviewEvidence(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !PlatformListenerAuthorized(identity)) {
		CompanyError(http, 401, "evidence recording requires callback:write scope (automation) or admin role");
		return;
	}
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = PlatformWorkflow::RecordReviewEvidence(identity,
	                                                                      BodyString(body, "dispatch_id"),
	                                                                      BodyString(body, "head_sha"),
	                                                                      BodyHas(body, "passed") ? (bool)body["passed"] : false,
	                                                                      BodyString(body, "findings"),
	                                                                      BodyString(body, "check_url"),
	                                                                      BodyString(body, "external_service"),
	                                                                      BodyString(body, "run_id"),
	                                                                      BodyString(body, "tool_version"),
	                                                                      BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, result.idempotent_duplicate ? 200 : 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("recorded"));
	m.Set("evidence_id", result.payload.Get("evidence_id", ""));
	m.Set("outcome", result.payload.Get("outcome", ""));
	m.Set("dispatch_status", result.payload.Get("dispatch_status", ""));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}

static void ApiV2Reviews(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_REVIEWS_READ))
		return;
	CompanyStore* store = s_store;
	Vector<PlatformReviewDispatch> dispatches = store->GetReviewDispatches();
	String work_item = TrimBoth(http.Param("work_item_id"));
	ValueArray arr;
	for(int i = 0; i < dispatches.GetCount(); i++) {
		if(!work_item.IsEmpty() && dispatches[i].work_item_id != work_item)
			continue;
		arr.Add(PlatformDispatchToJson(*store, dispatches[i]));
	}
	ValueMap m;
	m.Set("dispatches", arr);
	CompanyJson(http, m);
}

static void ApiV2ReviewDetail(WebRequest& http)
{
	if(!CompanyRequireScope(http, COMPANY_SCOPE_REVIEWS_READ))
		return;
	CompanyStore* store = s_store;
	PlatformReviewDispatch d;
	if(!store->FindReviewDispatch(http.PathArg(0), d)) {
		CompanyError(http, 404, "dispatch not found");
		return;
	}
	CompanyJson(http, PlatformDispatchToJson(*store, d));
}

static void ApiV2TrialDispatch(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !PlatformTrialAuthorized(identity)) {
		CompanyError(http, 401, "trial dispatch requires trials:write scope (automation) or admin role");
		return;
	}
	ValueMap body = ParseJsonBody(http);
	Vector<String> splits;
	if(BodyHas(body, "splits") && body["splits"].Is<ValueArray>()) {
		ValueArray sa = body["splits"];
		for(int i = 0; i < sa.GetCount(); i++)
			splits.Add((String)sa[i]);
	}
	CompanyWorkflowResult result = PlatformWorkflow::DispatchTrial(identity,
	                                                               BodyString(body, "kind"),
	                                                               BodyString(body, "work_item_id"),
	                                                               BodyString(body, "repository"),
	                                                               BodyInt(body, "pr_number"),
	                                                               BodyString(body, "head_sha"),
	                                                               splits,
	                                                               BodyInt(body, "trials_per_split", 1),
	                                                               BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, result.idempotent_duplicate ? 200 : 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("dispatched"));
	m.Set("trial_id", result.payload.Get("trial_id", ""));
	m.Set("expected_results", result.payload.Get("expected_results", 0));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}

static void ApiV2TrialResult(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !PlatformTrialAuthorized(identity)) {
		CompanyError(http, 401, "trial results require trials:write scope (automation) or admin role");
		return;
	}
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = PlatformWorkflow::RecordTrialResult(identity,
	                                                                   BodyString(body, "trial_id"),
	                                                                   BodyString(body, "split"),
	                                                                   BodyInt(body, "trial_index"),
	                                                                   BodyHas(body, "passed") ? (bool)body["passed"] : false,
	                                                                   BodyDouble(body, "reward"),
	                                                                   BodyString(body, "summary"),
	                                                                   BodyString(body, "head_sha"),
	                                                                   BodyString(body, "external_job_id"),
	                                                                   BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, result.idempotent_duplicate ? 200 : 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("recorded"));
	m.Set("result_id", result.payload.Get("result_id", ""));
	m.Set("outcome", result.payload.Get("outcome", ""));
	m.Set("trial_status", result.payload.Get("trial_status", ""));
	m.Set("received_results", result.payload.Get("received_results", 0));
	m.Set("expected_results", result.payload.Get("expected_results", 0));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}

static void ApiV2Trials(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	CompanyStore* store = s_store;
	Vector<PlatformTrialRecord> trials = store->GetPlatformTrials();
	ValueArray arr;
	for(int i = 0; i < trials.GetCount(); i++)
		arr.Add(PlatformTrialToJson(*store, trials[i]));
	ValueMap m;
	m.Set("trials", arr);
	m.Set("count", trials.GetCount());
	CompanyJson(http, m);
}

static void ApiV2Artifacts(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	CompanyStore* store = s_store;
	Vector<PlatformArtifactRecord> artifacts = store->GetPlatformArtifacts();
	String owner = TrimBoth(http.Param("owner_id"));
	ValueArray arr;
	for(int i = 0; i < artifacts.GetCount(); i++) {
		if(!owner.IsEmpty() && artifacts[i].owner_id != owner)
			continue;
		ValueMap am;
		am.Set("id", artifacts[i].id);
		am.Set("owner_type", artifacts[i].owner_type);
		am.Set("owner_id", artifacts[i].owner_id);
		am.Set("kind", artifacts[i].kind);
		am.Set("uri", artifacts[i].uri);
		am.Set("content_type", artifacts[i].content_type);
		am.Set("size", (int64)artifacts[i].size);
		am.Set("retention", artifacts[i].retention);
		am.Set("availability", artifacts[i].availability);
		arr.Add(am);
	}
	ValueMap m;
	m.Set("artifacts", arr);
	m.Set("count", arr.GetCount());
	CompanyJson(http, m);
}

static void ApiV2ArtifactRecord(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !PlatformArtifactAuthorized(identity)) {
		CompanyError(http, 401, "artifact recording requires callback:write or trials:write scope (automation) or admin role");
		return;
	}
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = PlatformWorkflow::RecordArtifact(identity,
	                                                                BodyString(body, "owner_type"),
	                                                                BodyString(body, "owner_id"),
	                                                                BodyString(body, "kind"),
	                                                                BodyString(body, "uri"),
	                                                                BodyString(body, "content_type"),
	                                                                (int64)BodyDouble(body, "size"),
	                                                                BodyString(body, "sha256"),
	                                                                BodyString(body, "retention"),
	                                                                BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, result.idempotent_duplicate ? 200 : 400, result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("recorded"));
	m.Set("artifact_id", result.payload.Get("artifact_id", ""));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}

static void ApiV2Status(WebRequest& http)
{
	if(!CompanyRequireRole(http, COMPANY_ROLE_ADMIN))
		return;
	ValueMap snap = PlatformWorkflow::StatusSnapshot();
	snap.Set("platform_schema_version", PLATFORM_SCHEMA_VERSION);
	CompanyJson(http, snap);
}

// ---------------------------------------------------------------------------
// Client contracts and projects (WP5). Visibility is deny by default: admins
// see everything, a client sees its own contracts and their projects, an
// employee sees the projects it is assigned to. Mutations go through
// CompanyWorkflow, which holds the role, state, membership and idempotency
// rules; the routes only translate the request and the result.
// ---------------------------------------------------------------------------

static void ApiContracts(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	CompanyStore* store = s_store;
	ValueArray arr;
	Vector<ContractRecord> all = store->GetContracts();
	for(int i = 0; i < all.GetCount(); i++)
		if(CanReadContract(id, all[i]))
			arr.Add(ContractToJson(all[i]));
	ValueMap m;
	m.Set("contracts", arr);
	m.Set("count", arr.GetCount());
	CompanyJson(http, m);
}

static void ApiContractDetail(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ContractRecord c;
	if(!s_store->FindContract(http.PathArg(0), c) || !CanReadContract(id, c)) {
		CompanyError(http, 404, "contract not found");
		return;
	}
	CompanyJson(http, ContractToJson(c));
}

static void ApiContractCreate(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = CompanyWorkflow::CreateContract(id, BodyString(body, "title"),
	                                                               BodyString(body, "brief"),
	                                                               BodyDouble(body, "amount"),
	                                                               BodyString(body, "currency"),
	                                                               BodyString(body, "payment_terms"),
	                                                               BodyString(body, "practical_details"),
	                                                               BodyString(body, "idempotency_key"));
	SendClientResult(http, result, "contract_id");
}

static void ApiContractNegotiate(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::NegotiateContract(id, BodyString(body, "contract_id"),
	                                                          BodyDouble(body, "amount"),
	                                                          BodyString(body, "payment_terms"),
	                                                          BodyString(body, "idempotency_key")),
	                 "contract_id");
}

static void ApiContractAccept(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::AcceptContract(id, BodyString(body, "contract_id"),
	                                                       BodyString(body, "idempotency_key")),
	                 "contract_id");
}

static void ApiContractComplete(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::CloseContract(id, BodyString(body, "contract_id"), true,
	                                                      BodyString(body, "idempotency_key")),
	                 "contract_id");
}

static void ApiContractCancel(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::CloseContract(id, BodyString(body, "contract_id"), false,
	                                                      BodyString(body, "idempotency_key")),
	                 "contract_id");
}

static void ApiProjects(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	CompanyStore* store = s_store;
	ValueArray arr;
	Vector<ProjectRecord> all = store->GetProjects();
	for(int i = 0; i < all.GetCount(); i++)
		if(CanReadProject(*store, id, all[i]))
			arr.Add(ProjectToJson(all[i]));
	ValueMap m;
	m.Set("projects", arr);
	m.Set("count", arr.GetCount());
	CompanyJson(http, m);
}

static void ApiProjectDetail(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	CompanyStore* store = s_store;
	ProjectRecord p;
	if(!store->FindProject(http.PathArg(0), p) || !CanReadProject(*store, id, p)) {
		CompanyError(http, 404, "project not found");
		return;
	}
	CompanyJson(http, ProjectToJson(p));
}

static void ApiProjectCreate(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::CreateProject(id, BodyString(body, "contract_id"),
	                                                      BodyString(body, "repo_name"),
	                                                      BodyString(body, "repo_origin"),
	                                                      BodyString(body, "goals"),
	                                                      BodyString(body, "idempotency_key")),
	                 "project_id");
}

static void ApiProjectAssign(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::AssignEmployee(id, BodyString(body, "project_id"),
	                                                       BodyString(body, "employee_user_id"),
	                                                       BodyString(body, "idempotency_key")),
	                 "project_id");
}

static void ApiProjectStart(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::StartProject(id, BodyString(body, "project_id"),
	                                                     BodyString(body, "idempotency_key")),
	                 "project_id");
}

static void ApiProjectAcceptPr(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::AcceptProjectPullRequest(id, BodyString(body, "project_id"),
	                                                                 BodyString(body, "pr_ref"),
	                                                                 BodyString(body, "idempotency_key")),
	                 "project_id");
}

static void ApiProjectRequestCompletion(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::RequestProjectCompletion(id, BodyString(body, "project_id"),
	                                                                 BodyString(body, "idempotency_key")),
	                 "project_id");
}

static void ApiProjectReady(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::MarkProjectReady(id, BodyString(body, "project_id"),
	                                                         BodyString(body, "idempotency_key")),
	                 "project_id");
}

static void ApiProjectDeliver(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::DeliverProject(id, BodyString(body, "project_id"),
	                                                       BodyString(body, "idempotency_key")),
	                 "project_id");
}

static void ApiProposalScope(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::ScopeProposalToProject(id, BodyString(body, "proposal_id"),
	                                                               BodyString(body, "project_id"),
	                                                               BodyString(body, "idempotency_key")),
	                 "proposal_id");
}

static void ApiWorkAdopt(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::AdoptWorkItem(id, BodyString(body, "work_item_id"),
	                                                      BodyString(body, "idempotency_key")),
	                 "work_item_id");
}

// Admin provisioning for the scenario and operators. A list field takes either
// a JSON array or a comma-separated string, so a manifest argument can pass it.
static Vector<String> BodyStringList(const ValueMap& body, const char* key)
{
	Vector<String> out;
	if(body.Find(key) < 0)
		return out;
	Value v = body[key];
	if(IsString(v)) {
		Vector<String> parts = Split((String)v, ',');
		for(const String& part : parts)
			if(!TrimBoth(part).IsEmpty())
				out.Add(TrimBoth(part));
		return out;
	}
	ValueArray arr = v;
	for(int i = 0; i < arr.GetCount(); i++)
		out.Add((String)arr[i]);
	return out;
}

static void ApiUsersCreate(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	String username = BodyString(body, "username");
	String display_name = BodyString(body, "display_name");
	String password = BodyString(body, "password");
	Vector<String> roles = BodyStringList(body, "roles");
	CompanyWorkflowResult result = CompanyWorkflow::CreateUser(id, username, display_name, roles, password,
	                                                           BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, result.idempotent_duplicate ? 200 : ClientErrorCode(result.error), result.error);
		return;
	}
	String provision_error;
	if(!CompanyProvisionLogin(username, password, display_name, roles, provision_error)) {
		CompanyError(http, 500, "user created but login was not provisioned: " + provision_error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("applied"));
	m.Set("username", username);
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}

static void ApiProposalDecide(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	SendClientResult(http, CompanyWorkflow::DecideProposal(id, BodyString(body, "proposal_id"),
	                                                       BodyString(body, "action"), BodyString(body, "reason"),
	                                                       BodyString(body, "assign_contributor_user_id"),
	                                                       BodyString(body, "idempotency_key")),
	                 "proposal_id");
}

static void ApiConfigAllowlist(WebRequest& http)
{
	if(!CompanyRequireAuth(http))
		return;
	WebAuthIdentity id = CompanyIdentity(http);
	ValueMap body = ParseJsonBody(http);
	CompanyWorkflowResult result = CompanyWorkflow::SetRepositoryAllowlist(id, BodyStringList(body, "repositories"),
	                                                                       BodyString(body, "idempotency_key"));
	if(!result.ok) {
		CompanyError(http, result.idempotent_duplicate ? 200 : ClientErrorCode(result.error), result.error);
		return;
	}
	ValueMap m;
	m.Set("status", result.idempotent_duplicate ? String("duplicate") : String("applied"));
	m.Set("event_id", result.event_id);
	CompanyJson(http, m, result.idempotent_duplicate ? 200 : 201);
}

// ---------------------------------------------------------------------------
// Command manifest (skylark-command-manifest/1, the same shape WebEngine serves),
// so skylarkctl can drive the client, employee and company sections.
// ---------------------------------------------------------------------------


// GET / - welcome (guest) or role-aware dashboard ("Welcome Back, ...").
static void CompanyHome(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	http.Html(CompanyViews::RenderWelcome(http, identity));
}

// GET /login
static void CompanyLogin(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(identity.authenticated) {
		http.Redirect("/dashboard");
		return;
	}
	http.Redirect(http.Site().Url("session/login"));
}

// POST /login/submit - CSRF-protected credential verification.
// POST /logout - CSRF-protected session teardown.
static void CompanyLogout(WebRequest& http)
{
	http.DropSession();
	http.Redirect("/");
}

// GET /dashboard - contributor dashboard (auth required).
static void CompanyDashboard(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderDashboard(http, identity));
}

// GET /proposals
static void CompanyProposals(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderProposals(http, identity));
}

// GET /proposals/new
static void CompanyNewProposal(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !identity.HasScope(COMPANY_SCOPE_PROPOSALS_WRITE)) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderNewProposalForm(http, identity));
}

// POST /proposals/create - CSRF-protected proposal submission.
static void CompanyProposalCreate(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !identity.HasScope(COMPANY_SCOPE_PROPOSALS_WRITE)) {
		http.Redirect("/login");
		return;
	}
	String title = http.Param("title");
	String description = http.Param("description");
	String repository = http.Param("repository");
	double estimated_hours = ScanDouble(http.Param("estimated_hours"));

	CompanyWorkflowResult result = CompanyWorkflow::CreateProposal(identity, title, description, repository,
	                                                               estimated_hours, String());
	if(result.ok)
		http.Redirect("/proposals");
	else {
		String body = CompanyViews::RenderNewProposalForm(http, identity);
		http.Html(CompanyViews::RenderPage(http, "New Proposal", "proposals",
		                              CompanyViews::RenderFormError(result.error) + body, identity));
	}
}

// POST /proposals/revise - author revises a changes_requested proposal.
static void CompanyProposalRevise(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	String proposal_id = http.Param("proposal_id");
	String description = http.Param("description");
	CompanyWorkflowResult result = CompanyWorkflow::ReviseProposal(identity, proposal_id, description, String());
	if(result.ok)
		http.Redirect("/proposals/detail/" + proposal_id);
	else
		http.Html(CompanyViews::RenderError(http, identity, "Revision Failed", result.error, 400));
}

// GET /proposals/detail/{id}
static void CompanyProposalDetail(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderProposalDetail(http, identity, http.PathArg(0)));
}

// GET /work
static void CompanyWork(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderWorkItems(http, identity));
}

// GET /work/detail/{id}
static void CompanyWorkDetail(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderWorkItemDetail(http, identity, http.PathArg(0)));
}

// POST /work/bind - contributor binds the pull request.
static void CompanyWorkBind(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	String work_item_id = http.Param("work_item_id");
	int pr_number = ScanInt(http.Param("pr_number"));
	String pr_url = http.Param("pr_url");
	String branch = http.Param("branch");
	CompanyWorkflowResult result = CompanyWorkflow::BindPullRequest(identity, work_item_id,
	                                                                String(), pr_number, pr_url, branch, String());
	if(result.ok)
		http.Redirect("/work/detail/" + work_item_id);
	else
		http.Html(CompanyViews::RenderError(http, identity, "Binding Failed", result.error, 400));
}

// POST /work/request-mechanical
static void CompanyWorkRequestMechanical(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	String work_item_id = http.Param("work_item_id");
	CompanyWorkflowResult result = CompanyWorkflow::RequestMechanicalReview(identity, work_item_id, String());
	if(result.ok)
		http.Redirect("/work/detail/" + work_item_id);
	else
		http.Html(CompanyViews::RenderError(http, identity, "Request Failed", result.error, 400));
}

// POST /work/request-operator
static void CompanyWorkRequestOperator(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	String work_item_id = http.Param("work_item_id");
	CompanyWorkflowResult result = CompanyWorkflow::RequestOperatorReview(identity, work_item_id, String());
	if(result.ok)
		http.Redirect("/work/detail/" + work_item_id);
	else
		http.Html(CompanyViews::RenderError(http, identity, "Request Failed", result.error, 400));
}

// POST /work/resume - move changes_requested back to implementation.
static void CompanyWorkResume(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	String work_item_id = http.Param("work_item_id");
	String reason = http.Param("reason");
	CompanyWorkflowResult result = CompanyWorkflow::ResumeWork(identity, work_item_id, reason, String());
	if(result.ok)
		http.Redirect("/work/detail/" + work_item_id);
	else
		http.Html(CompanyViews::RenderError(http, identity, "Resume Failed", result.error, 400));
}

// GET /account - ledger view.
static void CompanyAccount(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderAccount(http, identity));
}

// GET /account/settings - identity facts + password change (modeled on the
// reference site's Profile page).
static void CompanyAccountSettings(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderAccountSettings(http, identity));
}

// POST /account/settings/password - CSRF-protected self-service password change.
static void CompanyAccountSettingsPassword(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	if(!identity.IsHuman()) {
		http.Html(CompanyViews::RenderAccountSettings(http, identity, "only human accounts can change a password"));
		return;
	}
	String current_password = http.Param("current_password");
	String new_password = http.Param("new_password");
	UserRecord user;
	if(!s_store || !s_store->FindUser(identity.user_id, user)) {
		http.Html(CompanyViews::RenderAccountSettings(http, identity, "account not found"));
		return;
	}
	if(current_password.IsEmpty() || new_password.IsEmpty()) {
		http.Html(CompanyViews::RenderAccountSettings(http, identity, "current password is incorrect"));
		return;
	}
	if(new_password.GetCount() < 8) {
		http.Html(CompanyViews::RenderAccountSettings(http, identity, "new password must be at least 8 characters"));
		return;
	}
	// The composed login checks the WebAccounts password: change it there (verifies the current one).
	WebAccountUser login;
	if(!s_site->Accounts().FindUserByUsername(identity.username, login)) {
		http.Html(CompanyViews::RenderAccountSettings(http, identity, "account not found"));
		return;
	}
	StoreResult changed = s_site->Accounts().ChangePassword(login.user_id, current_password, new_password);
	if(!changed.ok) {
		http.Html(CompanyViews::RenderAccountSettings(http, identity, "current password is incorrect"));
		return;
	}
	user.password_hash = WebAuthHashPassword(new_password);
	user.updated_at = GetSysTime();
	String err;
	if(!s_store->SaveUser(user, err)) {
		http.Html(CompanyViews::RenderAccountSettings(http, identity, "cannot save password: " + err));
		return;
	}
	http.Html(CompanyViews::RenderAccountSettings(http, identity, String(), "Password updated."));
}

// GET /review/queue - reviewer queue.
static void CompanyReviewQueue(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !(identity.HasRole(COMPANY_ROLE_REVIEWER) || identity.IsAdmin())) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderReviewQueue(http, identity));
}

// GET /review/detail/{id} - proposal review view.
static void CompanyReviewDetail(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !(identity.HasRole(COMPANY_ROLE_REVIEWER) || identity.IsAdmin())) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderReviewDetail(http, identity, http.PathArg(0)));
}

// POST /review/decide - reviewer proposal decision (human only).
static void CompanyReviewDecide(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	String proposal_id = http.Param("proposal_id");
	String action = http.Param("action");
	String reason = http.Param("reason");
	String assignee = http.Param("assign_contributor");
	CompanyWorkflowResult result = CompanyWorkflow::DecideProposal(identity, proposal_id, action, reason,
	                                                                assignee, String());
	if(result.ok)
		http.Redirect("/review/queue");
	else
		http.Html(CompanyViews::RenderError(http, identity, "Decision Failed", result.error, 400));
}

// GET /review/operator/{id} - operator review view.
static void CompanyOperatorReview(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated || !(identity.HasRole(COMPANY_ROLE_REVIEWER) || identity.IsAdmin())) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderOperatorReview(http, identity, http.PathArg(0)));
}

// POST /review/operator/decide - operator review decision (human reviewer).
static void CompanyOperatorDecide(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	String work_item_id = http.Param("work_item_id");
	String action = http.Param("action");
	String reason = http.Param("reason");
	CompanyWorkflowResult result = CompanyWorkflow::DecideOperatorReview(identity, work_item_id, action, reason, String());
	if(result.ok)
		http.Redirect("/review/queue");
	else
		http.Html(CompanyViews::RenderError(http, identity, "Decision Failed", result.error, 400));
}

// GET /admin/users
static void CompanyAdminUsers(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderAdminUsers(http, identity));
}

// POST /admin/users/roles
static void CompanyAdminUserRoles(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	String user_id = http.Param("user_id");
	Vector<String> roles;
	Vector<String> all = CompanyAllRoles();
	for(int i = 0; i < all.GetCount(); i++) {
		String key = "role_" + all[i];
		if(http.Param(key.Begin()) == "1")
			roles.Add(all[i]);
	}
	CompanyWorkflowResult result = CompanyWorkflow::SetUserRoles(identity, user_id, roles, String());
	if(result.ok)
		http.Redirect("/admin/users");
	else
		http.Html(CompanyViews::RenderError(http, identity, "Role Update Failed", result.error, 400));
}

// POST /admin/users/create
static void CompanyAdminUserCreate(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	String username = http.Param("username");
	String display_name = http.Param("display_name");
	String password = http.Param("password");
	Vector<String> roles;
	Vector<String> parts = Split(http.Param("roles"), ',');
	for(int i = 0; i < parts.GetCount(); i++) {
		String r = TrimBoth(parts[i]);
		if(!r.IsEmpty())
			roles.Add(r);
	}
	CompanyWorkflowResult result = CompanyWorkflow::CreateUser(identity, username, display_name, roles, password, String());
	String provision_error;
	if(result.ok && !CompanyProvisionLogin(username, password, display_name, roles, provision_error))
		result.error = "user created but login was not provisioned: " + provision_error;
	if(result.ok && provision_error.IsEmpty())
		http.Redirect("/admin/users");
	else
		http.Html(CompanyViews::RenderError(http, identity, "User Creation Failed", result.error, 400));
}

// POST /admin/users/rate - manage a user's hourly rate (compensation input).
static void CompanyAdminUserRate(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	String user_id = http.Param("user_id");
	double rate = ScanDouble(http.Param("hourly_rate"));
	CompanyWorkflowResult result = CompanyWorkflow::SetUserHourlyRate(identity, user_id, rate, String());
	if(result.ok)
		http.Redirect("/admin/users");
	else
		http.Html(CompanyViews::RenderError(http, identity, "Rate Update Failed", result.error, 400));
}

// GET /admin/tokens
static void CompanyAdminTokens(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderAdminTokens(http, identity, String()));
}

// POST /admin/tokens/create
static void CompanyAdminTokenCreate(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	String name = http.Param("name");
	String owner = http.Param("owner_user_id");
	Vector<String> scopes;
	Vector<String> parts = Split(http.Param("scopes"), ',');
	for(int i = 0; i < parts.GetCount(); i++) {
		String s = TrimBoth(parts[i]);
		if(!s.IsEmpty())
			scopes.Add(s);
	}
	String expires = http.Param("expires_at");
	Time expires_at = IsNull(expires) ? Time() : (expires.GetLength() >= 16 ? ScanTime(expires) : Time());

	String raw_token;
	CompanyWorkflowResult result = CompanyWorkflow::CreateAgentToken(identity, name, owner, scopes,
	                                                                 expires_at, String(), raw_token);
	if(result.ok)
		http.Html(CompanyViews::RenderAdminTokens(http, identity, raw_token));
	else
		http.Html(CompanyViews::RenderError(http, identity, "Token Creation Failed", result.error, 400));
}

// POST /admin/tokens/revoke
static void CompanyAdminTokenRevoke(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	String token_id = http.Param("token_id");
	CompanyWorkflowResult result = CompanyWorkflow::RevokeAgentToken(identity, token_id, "revoked via admin UI", String());
	if(result.ok)
		http.Redirect("/admin/tokens");
	else
		http.Html(CompanyViews::RenderError(http, identity, "Revocation Failed", result.error, 400));
}

// GET /admin/policy
static void CompanyAdminPolicy(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderAdminPolicy(http, identity));
}

// POST /admin/policy/save
static void CompanyAdminPolicySave(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	ValueMap reward_policy;
	reward_policy.Set("default_amount", ScanInt(http.Param("default_amount")));
	reward_policy.Set("max_amount", ScanInt(http.Param("max_amount")));
	reward_policy.Set("currency", http.Param("currency"));
	ValueMap review_policy;
	review_policy.Set("operator_quorum", ScanInt(http.Param("operator_quorum")));
	CompanyWorkflowResult result = CompanyWorkflow::UpdateConfigPolicy(identity, reward_policy, review_policy,
	                                                                   ValueMap(), String());
	if(result.ok)
		http.Redirect("/admin/policy");
	else
		http.Html(CompanyViews::RenderError(http, identity, "Policy Update Failed", result.error, 400));
}

// GET /admin/audit (+ optional ?action= filter)
static void CompanyAdminAudit(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderAdminAudit(http, identity, http.Param("action")));
}
// GET /admin/platform - reference-shaped control-plane overview (admin only).

static void CompanyAdminPlatform(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderAdminPlatform(http, identity));
}

// GET /admin/repos

static void CompanyAdminRepos(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	http.Html(CompanyViews::RenderAdminRepos(http, identity));
}

// POST /admin/repos/save
static void CompanyAdminReposSave(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.IsAdmin()) {
		http.Redirect("/login");
		return;
	}
	Vector<String> repos;
	Vector<String> parts = Split(http.Param("repositories"), '\n');
	for(int i = 0; i < parts.GetCount(); i++) {
		String r = TrimBoth(parts[i]);
		if(!r.IsEmpty())
			repos.Add(r);
	}
	CompanyWorkflowResult result = CompanyWorkflow::SetRepositoryAllowlist(identity, repos, String());
	if(result.ok)
		http.Redirect("/admin/repos");
	else
		http.Html(CompanyViews::RenderError(http, identity, "Allowlist Update Failed", result.error, 400));
}

// POST /admin/compensate - issue compensation for a completed work item.
static void CompanyAdminCompensate(WebRequest& http)
{
	WebAuthIdentity identity = CompanyIdentity(http);
	if(!identity.authenticated) {
		http.Redirect("/login");
		return;
	}
	String work_item_id = http.Param("work_item_id");
	String reason = http.Param("reason");
	CompanyWorkflowResult result = CompanyWorkflow::IssueCompensation(identity, work_item_id, reason, String());
	if(result.ok)
		http.Redirect("/review/operator/" + work_item_id);
	else
		http.Html(CompanyViews::RenderError(http, identity, "Compensation Failed", result.error, 400));
}


static void ApiCliManifest(WebRequest& http)
{
	CompanyJson(http, CompanyCommandManifest());
}



// Installs the Company pages, REST API and v2 API on a host-created site. The
// host registers the session routes (InstallSessionRoutes) and the Company
// store must outlive the site. Each mutation is a POST on its own literal path.
void InstallCompanySite(EngineSite& site, CompanyStore& store)
{
	s_site = &site;
	s_store = &store;
	CompanyWorkflow::SetStore(&store);
	PlatformWorkflow::SetStore(&store);
	CompanyViews::SetStore(&store);
	SiteRouter& rt = site.Router();
	rt.Add("GET", "api/health", ApiHealth);
	rt.Add("GET", "api/framework/spec", ApiFrameworkSpec);
	rt.Add("GET", "api/auth/status", ApiAuthStatus);
	rt.Add("POST", "api/auth/login", ApiAuthLogin);
	rt.Add("POST", "api/auth/logout", ApiAuthLogout);
	rt.Add("GET", "api/proposals", ApiProposals);
	rt.Add("POST", "api/proposals/create", ApiProposalCreate);
	rt.Add("GET", "api/proposals/*", ApiProposalDetail);
	rt.Add("GET", "api/work", ApiWork);
	rt.Add("GET", "api/work/*", ApiWorkDetail);
	rt.Add("POST", "api/work/bind", ApiWorkBind);
	rt.Add("POST", "api/work/mechanical/request", ApiWorkMechanicalRequest);
	rt.Add("POST", "api/work/operator/request", ApiWorkOperatorRequest);
	rt.Add("GET", "api/reviews", ApiReviews);
	rt.Add("GET", "api/reviews/*", ApiReviewDetail);
	rt.Add("POST", "api/reviews/operator/decide", ApiOperatorDecide);
	rt.Add("GET", "api/ledger", ApiLedger);
	rt.Add("GET", "api/ledger/balance", ApiLedgerBalance);
	rt.Add("GET", "api/audit", ApiAudit);
	rt.Add("GET", "api/users", ApiUsers);
	rt.Add("POST", "api/users/roles", ApiUserRoles);
	rt.Add("GET", "api/tokens", ApiTokens);
	rt.Add("POST", "api/tokens/create", ApiTokenCreate);
	rt.Add("POST", "api/tokens/revoke", ApiTokenRevoke);
	rt.Add("GET", "api/config", ApiConfig);
	rt.Add("POST", "api/config/update", ApiConfigUpdate);
	rt.Add("POST", "api/ledger/correction", ApiLedgerCorrection);
	rt.Add("POST", "api/webhook/review-callback", ApiReviewCallback);
	rt.Add("POST", "api/v2/commands/ingest", ApiV2CommandIngest);
	rt.Add("GET", "api/v2/commands", ApiV2Commands);
	rt.Add("POST", "api/v2/reviews/dispatch", ApiV2ReviewDispatch);
	rt.Add("POST", "api/v2/reviews/evidence", ApiV2ReviewEvidence);
	rt.Add("GET", "api/v2/reviews", ApiV2Reviews);
	rt.Add("GET", "api/v2/reviews/*", ApiV2ReviewDetail);
	rt.Add("POST", "api/v2/trials/dispatch", ApiV2TrialDispatch);
	rt.Add("POST", "api/v2/trials/results", ApiV2TrialResult);
	rt.Add("GET", "api/v2/trials", ApiV2Trials);
	rt.Add("GET", "api/v2/artifacts", ApiV2Artifacts);
	rt.Add("POST", "api/v2/artifacts/record", ApiV2ArtifactRecord);
	rt.Add("GET", "api/v2/status", ApiV2Status);
	rt.Add("GET", "api/contracts", ApiContracts);
	rt.Add("GET", "api/contracts/*", ApiContractDetail);
	rt.Add("POST", "api/contracts/create", ApiContractCreate);
	rt.Add("POST", "api/contracts/negotiate", ApiContractNegotiate);
	rt.Add("POST", "api/contracts/accept", ApiContractAccept);
	rt.Add("POST", "api/contracts/complete", ApiContractComplete);
	rt.Add("POST", "api/contracts/cancel", ApiContractCancel);
	rt.Add("GET", "api/projects", ApiProjects);
	rt.Add("GET", "api/projects/*", ApiProjectDetail);
	rt.Add("POST", "api/projects/create", ApiProjectCreate);
	rt.Add("POST", "api/projects/assign", ApiProjectAssign);
	rt.Add("POST", "api/projects/start", ApiProjectStart);
	rt.Add("POST", "api/projects/accept-pr", ApiProjectAcceptPr);
	rt.Add("POST", "api/projects/request-completion", ApiProjectRequestCompletion);
	rt.Add("POST", "api/projects/ready", ApiProjectReady);
	rt.Add("POST", "api/projects/deliver", ApiProjectDeliver);
	rt.Add("POST", "api/proposals/scope", ApiProposalScope);
	rt.Add("POST", "api/work/adopt", ApiWorkAdopt);
	rt.Add("POST", "api/users/create", ApiUsersCreate);
	rt.Add("POST", "api/proposals/decide", ApiProposalDecide);
	rt.Add("POST", "api/config/allowlist", ApiConfigAllowlist);
	rt.Add("GET", "api/cli/manifest", ApiCliManifest);
	rt.Add("GET", "", CompanyHome);
	rt.Add("GET", "login", CompanyLogin);
	rt.Add("POST", "logout", CompanyLogout);
	rt.Add("GET", "dashboard", CompanyDashboard);
	rt.Add("GET", "proposals", CompanyProposals);
	rt.Add("GET", "proposals/new", CompanyNewProposal);
	rt.Add("POST", "proposals/create", CompanyProposalCreate);
	rt.Add("POST", "proposals/revise", CompanyProposalRevise);
	rt.Add("GET", "proposals/detail/*", CompanyProposalDetail);
	rt.Add("GET", "work", CompanyWork);
	rt.Add("GET", "work/detail/*", CompanyWorkDetail);
	rt.Add("POST", "work/bind", CompanyWorkBind);
	rt.Add("POST", "work/request-mechanical", CompanyWorkRequestMechanical);
	rt.Add("POST", "work/request-operator", CompanyWorkRequestOperator);
	rt.Add("POST", "work/resume", CompanyWorkResume);
	rt.Add("GET", "account", CompanyAccount);
	rt.Add("GET", "account/settings", CompanyAccountSettings);
	rt.Add("POST", "account/settings/password", CompanyAccountSettingsPassword);
	rt.Add("GET", "review/queue", CompanyReviewQueue);
	rt.Add("GET", "review/detail/*", CompanyReviewDetail);
	rt.Add("POST", "review/decide", CompanyReviewDecide);
	rt.Add("GET", "review/operator/*", CompanyOperatorReview);
	rt.Add("POST", "review/operator/decide", CompanyOperatorDecide);
	rt.Add("GET", "admin/users", CompanyAdminUsers);
	rt.Add("POST", "admin/users/roles", CompanyAdminUserRoles);
	rt.Add("POST", "admin/users/create", CompanyAdminUserCreate);
	rt.Add("POST", "admin/users/rate", CompanyAdminUserRate);
	rt.Add("GET", "admin/tokens", CompanyAdminTokens);
	rt.Add("POST", "admin/tokens/create", CompanyAdminTokenCreate);
	rt.Add("POST", "admin/tokens/revoke", CompanyAdminTokenRevoke);
	rt.Add("GET", "admin/policy", CompanyAdminPolicy);
	rt.Add("POST", "admin/policy/save", CompanyAdminPolicySave);
	rt.Add("GET", "admin/audit", CompanyAdminAudit);
	rt.Add("GET", "admin/platform", CompanyAdminPlatform);
	rt.Add("GET", "admin/repos", CompanyAdminRepos);
	rt.Add("POST", "admin/repos/save", CompanyAdminReposSave);
	rt.Add("POST", "admin/compensate", CompanyAdminCompensate);
}

bool SetCompanyUser(CompanyStore& store, const String& username, const String& password,
                    const Vector<String>& roles, String& error)
{
	String name = TrimBoth(username);
	if(name.IsEmpty()) {
		error = "username is required";
		return false;
	}
	if(password.IsEmpty()) {
		error = "password is required";
		return false;
	}
	if(roles.IsEmpty()) {
		error = "at least one role is required";
		return false;
	}
	if(!s_site || s_store != &store) {
		error = "company site is not installed over this store";
		return false;
	}
	Vector<String> all = CompanyAllRoles();
	for(const String& role : roles)
		if(FindIndex(all, role) < 0) {
			error = "unknown role: " + role;
			return false;
		}

	UserRecord user;
	if(!store.FindUserByUsername(name, user)) {
		user.id = CompanyGenerateId("usr");
		user.username = name;
		user.display_name = name;
		user.enabled = true;
		user.created_at = GetSysTime();
	}
	user.roles = clone(roles);
	user.password_hash = WebAuthHashPassword(password);
	user.updated_at = GetSysTime();
	if(!store.SaveUser(user, error))
		return false;

	// The login lives in WebAccounts. CompanyProvisionLogin creates it; it does
	// not reset an existing one, so an existing login gets SetPassword instead.
	WebAccountUser login;
	if(s_site->Accounts().FindUserByUsername(name, login)) {
		StoreResult r = s_site->Accounts().SetPassword(login.user_id, password);
		if(!r.ok) {
			error = r.error;
			return false;
		}
		return true;
	}
	return CompanyProvisionLogin(name, password, user.display_name, roles, error);
}

END_UPP_NAMESPACE
