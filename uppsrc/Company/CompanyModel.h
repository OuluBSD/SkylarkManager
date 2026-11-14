#ifndef _CompanyStore_CompanyModel_h_
#define _CompanyStore_CompanyModel_h_

#include <Core/Core.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// CompanySkylark data model. All records are versioned JSON documents persisted
// under the configured data root (default ./mgr/company/). Schema version is
// stored per collection; unknown future versions are rejected on load.
// ---------------------------------------------------------------------------

#define COMPANY_SCHEMA_VERSION 1

// Explicit roles. Role checks are centralized and deny by default.
#define COMPANY_ROLE_CONTRIBUTOR           "contributor"
#define COMPANY_ROLE_REVIEWER              "reviewer"
#define COMPANY_ROLE_ADMIN                 "admin"
#define COMPANY_ROLE_AUTOMATION            "automation_contributor"
#define COMPANY_ROLE_CLIENT                "client"     // contract counterpart; owns its contracts
#define COMPANY_ROLE_EMPLOYEE              "employee"   // company staff; accepts PRs on the company's behalf

// Actor types.
#define COMPANY_ACTOR_HUMAN                "human"
#define COMPANY_ACTOR_AGENT                "agent"
#define COMPANY_ACTOR_AUTOMATION           "automation"
#define COMPANY_ACTOR_SYSTEM               "system"

// Proposal statuses.
#define COMPANY_PROPOSAL_PROPOSED          "proposed"
#define COMPANY_PROPOSAL_ACCEPTED          "accepted"
#define COMPANY_PROPOSAL_REJECTED          "rejected"
#define COMPANY_PROPOSAL_CHANGES           "changes_requested"

// Work item states (canonical lifecycle).
#define COMPANY_STATE_IMPLEMENTATION       "implementation"
#define COMPANY_STATE_MECHANICAL           "mechanical_review"
#define COMPANY_STATE_OPERATOR             "operator_review"
#define COMPANY_STATE_COMPLETED            "completed"
#define COMPANY_STATE_COMPENSATED          "compensated"
#define COMPANY_STATE_BLOCKED              "blocked"
#define COMPANY_STATE_CHANGES              "changes_requested"
#define COMPANY_STATE_REJECTED             "rejected"

// Review statuses.
#define COMPANY_REVIEW_REQUESTED           "requested"
#define COMPANY_REVIEW_PASSED              "passed"
#define COMPANY_REVIEW_FAILED              "failed"
#define COMPANY_REVIEW_APPROVED            "approved"
#define COMPANY_REVIEW_REJECTED            "rejected"
#define COMPANY_REVIEW_CHANGES             "changes_requested"

// Ledger entry types.
#define COMPANY_LEDGER_CREDIT              "credit"
#define COMPANY_LEDGER_DEBIT               "debit"
#define COMPANY_LEDGER_CORRECTION          "correction"

// Client contract statuses (company <-> client).
#define COMPANY_CONTRACT_PROPOSED          "proposed"
#define COMPANY_CONTRACT_NEGOTIATING       "negotiating"
#define COMPANY_CONTRACT_ACTIVE            "active"
#define COMPANY_CONTRACT_COMPLETED         "completed"
#define COMPANY_CONTRACT_CANCELLED         "cancelled"

// Client project statuses.
#define COMPANY_PROJECT_PLANNING           "planning"
#define COMPANY_PROJECT_ACTIVE             "active"
#define COMPANY_PROJECT_COMPLETING         "completing"
#define COMPANY_PROJECT_READY              "ready"
#define COMPANY_PROJECT_DELIVERED          "delivered"

// Where a project's repository came from.
#define COMPANY_REPO_CREATED               "created"
#define COMPANY_REPO_ADOPTED               "adopted"

// Agent token scopes.
#define COMPANY_SCOPE_PROPOSALS_READ       "proposals:read"
#define COMPANY_SCOPE_PROPOSALS_WRITE      "proposals:write"
#define COMPANY_SCOPE_WORK_READ            "work:read"
#define COMPANY_SCOPE_WORK_WRITE           "work:write"
#define COMPANY_SCOPE_REVIEWS_READ         "reviews:read"
#define COMPANY_SCOPE_REVIEWS_WRITE        "reviews:write"
#define COMPANY_SCOPE_LEDGER_READ          "ledger:read"
#define COMPANY_SCOPE_CALLBACK_WRITE       "callback:write"
#define COMPANY_SCOPE_TRIALS_WRITE         "trials:write"
#define COMPANY_SCOPE_ADMIN                "admin"

inline Vector<String> CompanyAllRoles()
{
	Vector<String> r;
	r.Add(COMPANY_ROLE_CONTRIBUTOR);
	r.Add(COMPANY_ROLE_REVIEWER);
	r.Add(COMPANY_ROLE_ADMIN);
	r.Add(COMPANY_ROLE_AUTOMATION);
	r.Add(COMPANY_ROLE_CLIENT);
	r.Add(COMPANY_ROLE_EMPLOYEE);
	return r;
}

inline Vector<String> CompanyAllScopes()
{
	Vector<String> s;
	s.Add(COMPANY_SCOPE_PROPOSALS_READ);
	s.Add(COMPANY_SCOPE_PROPOSALS_WRITE);
	s.Add(COMPANY_SCOPE_WORK_READ);
	s.Add(COMPANY_SCOPE_WORK_WRITE);
	s.Add(COMPANY_SCOPE_REVIEWS_READ);
	s.Add(COMPANY_SCOPE_REVIEWS_WRITE);
	s.Add(COMPANY_SCOPE_LEDGER_READ);
	s.Add(COMPANY_SCOPE_CALLBACK_WRITE);
	s.Add(COMPANY_SCOPE_TRIALS_WRITE);
	s.Add(COMPANY_SCOPE_ADMIN);
	return s;
}

struct UserRecord : Moveable<UserRecord> {
	String           id;             // usr_<hex>
	String           username;
	String           display_name;
	bool             enabled = true;
	Vector<String>   roles;
	String           password_hash;  // pbkdf2_sha256$iterations$salt$hash (empty = no local password)
	double           hourly_rate = 0; // CR per hour; admin-managed; 0 = unset (blocks compensation)
	Time             created_at;
	Time             updated_at;

	bool HasRole(const String& role) const {
		for(int i = 0; i < roles.GetCount(); i++)
			if(roles[i] == role)
				return true;
		return false;
	}
	// A human-capable role (contributor or reviewer); automation identities
	// lack both and can therefore never be assigned proposal implementation.
	bool HasHumanRole() const { return HasRole(COMPANY_ROLE_CONTRIBUTOR) || HasRole(COMPANY_ROLE_REVIEWER); }

	// Explicit copy support: U++ Vector members are pick-only.
	UserRecord() {}
	UserRecord(const UserRecord& o)
		: id(o.id), username(o.username), display_name(o.display_name)
		, enabled(o.enabled), roles(clone(o.roles)), password_hash(o.password_hash)
		, hourly_rate(o.hourly_rate), created_at(o.created_at), updated_at(o.updated_at) {}
	UserRecord& operator=(const UserRecord& o) {
		if(this != &o) {
			id = o.id;
			username = o.username;
			display_name = o.display_name;
			enabled = o.enabled;
			roles = clone(o.roles);
			password_hash = o.password_hash;
			hourly_rate = o.hourly_rate;
			created_at = o.created_at;
			updated_at = o.updated_at;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("id", id)("username", username)("display_name", display_name)
		  ("enabled", enabled)("roles", roles)("password_hash", password_hash)
		  ("hourly_rate", hourly_rate)
		  ("created_at", created_at)("updated_at", updated_at);
	}
};

struct AgentTokenRecord : Moveable<AgentTokenRecord> {
	String           id;             // tok_<hex>
	String           name;
	String           owner_user_id;
	String           token_hash;     // SHA256(server_secret + raw token); raw token never persisted
	Vector<String>   scopes;
	Time             created_at;
	Time             expires_at;     // Null = no expiry
	Time             revoked_at;     // Null = active
	Time             last_used_at;
	String           last_used_ip;

	// Explicit copy support: U++ Vector members are pick-only.
	AgentTokenRecord() {}
	AgentTokenRecord(const AgentTokenRecord& o)
		: id(o.id), name(o.name), owner_user_id(o.owner_user_id), token_hash(o.token_hash)
		, scopes(clone(o.scopes)), created_at(o.created_at), expires_at(o.expires_at)
		, revoked_at(o.revoked_at), last_used_at(o.last_used_at), last_used_ip(o.last_used_ip) {}
	AgentTokenRecord& operator=(const AgentTokenRecord& o) {
		if(this != &o) {
			id = o.id;
			name = o.name;
			owner_user_id = o.owner_user_id;
			token_hash = o.token_hash;
			scopes = clone(o.scopes);
			created_at = o.created_at;
			expires_at = o.expires_at;
			revoked_at = o.revoked_at;
			last_used_at = o.last_used_at;
			last_used_ip = o.last_used_ip;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("id", id)("name", name)("owner_user_id", owner_user_id)
		  ("token_hash", token_hash)("scopes", scopes)
		  ("created_at", created_at)("expires_at", expires_at)("revoked_at", revoked_at)
		  ("last_used_at", last_used_at)("last_used_ip", last_used_ip);
	}
};

struct SessionRecord : Moveable<SessionRecord> {
	String           token_hash;     // SHA256(server_secret + raw session token)
	String           user_id;
	Time             created_at;
	Time             expires_at;
	String           ip;
	String           user_agent;

	void Jsonize(JsonIO& io) {
		io("token_hash", token_hash)("user_id", user_id)
		  ("created_at", created_at)("expires_at", expires_at)
		  ("ip", ip)("user_agent", user_agent);
	}
};

struct ProposalRecord : Moveable<ProposalRecord> {
	String           id;             // prp_<hex>
	String           author_user_id; // Null when author is an agent
	String           author_token_id;// Null when author is human
	String           author_type;    // human | agent
	String           title;
	String           description;
	String           repository;     // "owner/repo"
	int              issue_number = 0; // rarely known at proposal time; attached later when binding
	double           estimated_hours = 0; // workload estimate in hours (compensation = hours * hourly_rate)
	String           status = COMPANY_PROPOSAL_PROPOSED;
	ValueMap         reviewer_decision; // last reviewer decision {action, actor_user_id, actor_type, reason, at}
	String           assigned_contributor_user_id;
	String           assigned_contributor_token_id;
	Vector<String>   processed_event_keys; // consumed idempotency/event keys
	String           submission_hash;      // content hash of a broker-created draft; empty for human proposals
	ValueMap         submission_snapshot;  // content as submitted; written once at creation, never edited
	String           project_id;           // client project this task belongs to; empty when not scoped
	Time             created_at;
	Time             updated_at;

	// Explicit copy support: U++ Vector members are pick-only.
	ProposalRecord() {}
	ProposalRecord(const ProposalRecord& o)
		: id(o.id), author_user_id(o.author_user_id), author_token_id(o.author_token_id)
		, author_type(o.author_type), title(o.title), description(o.description)
		, repository(o.repository), issue_number(o.issue_number)
		, estimated_hours(o.estimated_hours), status(o.status), reviewer_decision(o.reviewer_decision)
		, assigned_contributor_user_id(o.assigned_contributor_user_id)
		, assigned_contributor_token_id(o.assigned_contributor_token_id)
		, processed_event_keys(clone(o.processed_event_keys))
		, submission_hash(o.submission_hash), submission_snapshot(o.submission_snapshot)
		, project_id(o.project_id), created_at(o.created_at), updated_at(o.updated_at) {}
	ProposalRecord& operator=(const ProposalRecord& o) {
		if(this != &o) {
			id = o.id;
			author_user_id = o.author_user_id;
			author_token_id = o.author_token_id;
			author_type = o.author_type;
			title = o.title;
			description = o.description;
			repository = o.repository;
			issue_number = o.issue_number;
			estimated_hours = o.estimated_hours;
			status = o.status;
			reviewer_decision = o.reviewer_decision;
			assigned_contributor_user_id = o.assigned_contributor_user_id;
			assigned_contributor_token_id = o.assigned_contributor_token_id;
			processed_event_keys = clone(o.processed_event_keys);
			submission_hash = o.submission_hash;
			submission_snapshot = o.submission_snapshot;
			project_id = o.project_id;
			created_at = o.created_at;
			updated_at = o.updated_at;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("id", id)("author_user_id", author_user_id)("author_token_id", author_token_id)
		  ("author_type", author_type)("title", title)("description", description)
		  ("repository", repository)("issue_number", issue_number)
		  ("estimated_hours", estimated_hours)("status", status)
		  ("reviewer_decision", reviewer_decision)
		  ("assigned_contributor_user_id", assigned_contributor_user_id)
		  ("assigned_contributor_token_id", assigned_contributor_token_id)
		  ("processed_event_keys", processed_event_keys)
		  ("submission_hash", submission_hash)("submission_snapshot", submission_snapshot)("project_id", project_id)
		  ("created_at", created_at)("updated_at", updated_at);
	}
};

struct WorkItemRecord : Moveable<WorkItemRecord> {
	String           id;             // wrk_<hex>
	String           proposal_id;
	String           contributor_user_id;
	String           contributor_token_id;
	String           contributor_type;
	String           repository;
	int              pr_number = 0;
	String           pr_url;
	String           branch;
	Vector<String>   required_checks;
	String           state = COMPANY_STATE_IMPLEMENTATION;
	Vector<String>   processed_event_keys; // consumed idempotency/event keys
	Time             created_at;
	Time             updated_at;

	// Explicit copy support: U++ Vector members are pick-only.
	WorkItemRecord() {}
	WorkItemRecord(const WorkItemRecord& o)
		: id(o.id), proposal_id(o.proposal_id)
		, contributor_user_id(o.contributor_user_id), contributor_token_id(o.contributor_token_id)
		, contributor_type(o.contributor_type), repository(o.repository), pr_number(o.pr_number)
		, pr_url(o.pr_url), branch(o.branch), required_checks(clone(o.required_checks))
		, state(o.state), processed_event_keys(clone(o.processed_event_keys))
		, created_at(o.created_at), updated_at(o.updated_at) {}
	WorkItemRecord& operator=(const WorkItemRecord& o) {
		if(this != &o) {
			id = o.id;
			proposal_id = o.proposal_id;
			contributor_user_id = o.contributor_user_id;
			contributor_token_id = o.contributor_token_id;
			contributor_type = o.contributor_type;
			repository = o.repository;
			pr_number = o.pr_number;
			pr_url = o.pr_url;
			branch = o.branch;
			required_checks = clone(o.required_checks);
			state = o.state;
			processed_event_keys = clone(o.processed_event_keys);
			created_at = o.created_at;
			updated_at = o.updated_at;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("id", id)("proposal_id", proposal_id)
		  ("contributor_user_id", contributor_user_id)("contributor_token_id", contributor_token_id)
		  ("contributor_type", contributor_type)("repository", repository)
		  ("pr_number", pr_number)("pr_url", pr_url)("branch", branch)
		  ("required_checks", required_checks)("state", state)
		  ("processed_event_keys", processed_event_keys)
		  ("created_at", created_at)("updated_at", updated_at);
	}
};

struct ReviewRecord : Moveable<ReviewRecord> {
	String           id;             // rev_<hex>
	String           work_item_id;
	String           proposal_id;
	String           kind;           // mechanical | operator
	String           actor_user_id;
	String           actor_token_id;
	String           actor_type;
	String           status = COMPANY_REVIEW_REQUESTED;
	ValueArray       decision_history; // immutable append-only decisions
	ValueMap         evidence;       // {checks[], comment_url, run_id, findings, event_id, source}
	Time             created_at;
	Time             updated_at;

	void Jsonize(JsonIO& io) {
		io("id", id)("work_item_id", work_item_id)("proposal_id", proposal_id)
		  ("kind", kind)("actor_user_id", actor_user_id)("actor_token_id", actor_token_id)
		  ("actor_type", actor_type)("status", status)
		  ("decision_history", decision_history)("evidence", evidence)
		  ("created_at", created_at)("updated_at", updated_at);
	}
};

struct LedgerEntry : Moveable<LedgerEntry> {
	int              seq = 0;
	String           entry_id;       // lgr_<hex>
	String           idempotency_key;// stable key; duplicates rejected
	String           type;           // credit | debit | correction
	int              amount = 0;
	String           user_id;
	String           work_item_id;
	String           reason;
	String           actor_user_id;
	String           actor_type;
	Time             created_at;

	void Jsonize(JsonIO& io) {
		io("seq", seq)("entry_id", entry_id)("idempotency_key", idempotency_key)
		  ("type", type)("amount", amount)("user_id", user_id)
		  ("work_item_id", work_item_id)("reason", reason)
		  ("actor_user_id", actor_user_id)("actor_type", actor_type)
		  ("created_at", created_at);
	}
};

struct AuditEvent : Moveable<AuditEvent> {
	int              seq = 0;
	String           event_id;       // evt_<hex>
	String           actor_user_id;
	String           actor_token_id;
	String           actor_type;
	Vector<String>   actor_roles;
	String           action;
	String           target_type;
	String           target_id;
	String           prior_status;
	String           new_status;
	String           reason;
	String           idempotency_key;
	Time             created_at;

	// Explicit copy support: U++ Vector members are pick-only.
	AuditEvent() {}
	AuditEvent(const AuditEvent& o)
		: seq(o.seq), event_id(o.event_id), actor_user_id(o.actor_user_id)
		, actor_token_id(o.actor_token_id), actor_type(o.actor_type), actor_roles(clone(o.actor_roles))
		, action(o.action), target_type(o.target_type), target_id(o.target_id)
		, prior_status(o.prior_status), new_status(o.new_status), reason(o.reason)
		, idempotency_key(o.idempotency_key), created_at(o.created_at) {}
	AuditEvent& operator=(const AuditEvent& o) {
		if(this != &o) {
			seq = o.seq;
			event_id = o.event_id;
			actor_user_id = o.actor_user_id;
			actor_token_id = o.actor_token_id;
			actor_type = o.actor_type;
			actor_roles = clone(o.actor_roles);
			action = o.action;
			target_type = o.target_type;
			target_id = o.target_id;
			prior_status = o.prior_status;
			new_status = o.new_status;
			reason = o.reason;
			idempotency_key = o.idempotency_key;
			created_at = o.created_at;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("seq", seq)("event_id", event_id)
		  ("actor_user_id", actor_user_id)("actor_token_id", actor_token_id)
		  ("actor_type", actor_type)("actor_roles", actor_roles)
		  ("action", action)("target_type", target_type)("target_id", target_id)
		  ("prior_status", prior_status)("new_status", new_status)
		  ("reason", reason)("idempotency_key", idempotency_key)
		  ("created_at", created_at);
	}
};

struct CompanyConfig : Moveable<CompanyConfig> {
	int              schema_version = COMPANY_SCHEMA_VERSION;
	Vector<String>   repository_allowlist;
	ValueMap         reward_policy;   // {default_amount, max_amount, currency}
	ValueMap         review_policy;   // {operator_quorum, mechanical_required}
	ValueMap         agent_reviewer;  // {enabled, command}
	Time             updated_at;

	// Explicit copy support: U++ Vector members are pick-only.
	CompanyConfig() {}
	CompanyConfig(const CompanyConfig& o)
		: schema_version(o.schema_version), repository_allowlist(clone(o.repository_allowlist))
		, reward_policy(o.reward_policy), review_policy(o.review_policy)
		, agent_reviewer(o.agent_reviewer), updated_at(o.updated_at) {}
	CompanyConfig& operator=(const CompanyConfig& o) {
		if(this != &o) {
			schema_version = o.schema_version;
			repository_allowlist = clone(o.repository_allowlist);
			reward_policy = o.reward_policy;
			review_policy = o.review_policy;
			agent_reviewer = o.agent_reviewer;
			updated_at = o.updated_at;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("schema_version", schema_version)("repository_allowlist", repository_allowlist)
		  ("reward_policy", reward_policy)("review_policy", review_policy)
		  ("agent_reviewer", agent_reviewer)("updated_at", updated_at);
	}
};

// ---------------------------------------------------------------------------
// Platform v2 records (reference-shaped contracts). These model the command
// listener / review dispatch / trial / artifact vocabulary of the reference
// platform topology. Collections carry PLATFORM_SCHEMA_VERSION; records are
// immutable once terminal (dispatches/commands may only move through explicit
// status transitions performed inside locked updates).
// ---------------------------------------------------------------------------

#define PLATFORM_SCHEMA_VERSION 2

// Command kinds.
#define PLATFORM_CMD_REVIEW                "review"
#define PLATFORM_CMD_RUN_REFERENCE         "run_reference"
#define PLATFORM_CMD_RUN_BASELINE          "run_baseline"
#define PLATFORM_CMD_UNKNOWN               "unrecognized"

// Command statuses.
#define PLATFORM_CMD_RECEIVED              "received"
#define PLATFORM_CMD_DISPATCHED            "dispatched"
#define PLATFORM_CMD_REFUSED               "refused"
#define PLATFORM_CMD_SUPERSEDED            "superseded"

// Review dispatch kinds.
#define PLATFORM_REVIEW_MECHANICAL         "mechanical"
#define PLATFORM_REVIEW_AGENTIC            "agentic"

// Review dispatch statuses.
#define PLATFORM_DSP_DISPATCHED            "dispatched"
#define PLATFORM_DSP_IN_PROGRESS           "in_progress"
#define PLATFORM_DSP_COMPLETED             "completed"
#define PLATFORM_DSP_STALE                 "stale"
#define PLATFORM_DSP_FAILED                "failed"
#define PLATFORM_DSP_SUPERSEDED            "superseded"

// Attempt statuses.
#define PLATFORM_ATM_PENDING               "pending"
#define PLATFORM_ATM_RUNNING               "running"
#define PLATFORM_ATM_COMPLETED             "completed"
#define PLATFORM_ATM_STALE                 "stale"
#define PLATFORM_ATM_FAILED                "failed"

// Evidence outcomes.
#define PLATFORM_EVD_PASSED                "passed"
#define PLATFORM_EVD_FAILED                "failed"
#define PLATFORM_EVD_INCOMPLETE            "incomplete"
#define PLATFORM_EVD_STALE                 "stale"

// Trial kinds and statuses.
#define PLATFORM_TRIAL_REFERENCE           "reference"
#define PLATFORM_TRIAL_BASELINE            "baseline"
#define PLATFORM_TRIAL_CANDIDATE           "candidate"
#define PLATFORM_TRIAL_DISPATCHED          "dispatched"
#define PLATFORM_TRIAL_COLLECTING          "collecting"
#define PLATFORM_TRIAL_COMPLETE            "complete"
#define PLATFORM_TRIAL_FAILED              "failed"

// Trial result outcomes.
#define PLATFORM_TRES_RECORDED             "recorded"
#define PLATFORM_TRES_STALE                "stale"

// Artifact availability.
#define PLATFORM_ART_AVAILABLE             "available"
#define PLATFORM_ART_EXPIRED               "expired"
#define PLATFORM_ART_MISSING               "missing"
#define PLATFORM_ART_REPLACED              "replaced"

// Validates a git SHA: 7-40 lowercase/uppercase hex characters.
inline bool PlatformIsValidSha(const String& sha)
{
	if(sha.GetCount() < 7 || sha.GetCount() > 40)
		return false;
	for(int i = 0; i < sha.GetCount(); i++) {
		char c = sha[i];
		if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return false;
	}
	return true;
}

struct PlatformCommandRecord : Moveable<PlatformCommandRecord> {
	String           id;              // cmd_<hex>
	String           source;          // github_comment | api
	String           repository;      // owner/repo (may be empty for API commands)
	int              pr_number = 0;
	String           actor_login;     // SCM login of the command author (informational)
	String           command;         // review | run_reference | run_baseline | unrecognized
	String           arguments;       // raw remainder (sha, args)
	String           head_sha;        // observed head SHA at ingest, when known
	String           status;          // received | refused | unrecognized
	String           refusal_reason;
	String           created_by_actor;// authenticated caller (token id or user id)
	String           idempotency_key;
	Time             created_at;
	Time             updated_at;

	void Jsonize(JsonIO& io) {
		io("id", id)("source", source)("repository", repository)
		  ("pr_number", pr_number)("actor_login", actor_login)
		  ("command", command)("arguments", arguments)("head_sha", head_sha)
		  ("status", status)("refusal_reason", refusal_reason)
		  ("created_by_actor", created_by_actor)("idempotency_key", idempotency_key)
		  ("created_at", created_at)("updated_at", updated_at);
	}
};

struct PlatformReviewDispatch : Moveable<PlatformReviewDispatch> {
	String           id;              // dsp_<hex>
	String           work_item_id;
	String           repository;
	int              pr_number = 0;
	String           kind;            // mechanical | agentic
	String           head_sha;        // the reviewed commit (immutable after dispatch)
	String           base_sha;
	String           requested_by;    // actor label
	String           status;          // dispatched | in_progress | completed | stale | failed | superseded
	String           idempotency_key;
	Time             created_at;
	Time             updated_at;

	void Jsonize(JsonIO& io) {
		io("id", id)("work_item_id", work_item_id)("repository", repository)
		  ("pr_number", pr_number)("kind", kind)("head_sha", head_sha)("base_sha", base_sha)
		  ("requested_by", requested_by)("status", status)
		  ("idempotency_key", idempotency_key)
		  ("created_at", created_at)("updated_at", updated_at);
	}
};

struct PlatformReviewAttempt : Moveable<PlatformReviewAttempt> {
	String           id;              // atm_<hex>
	String           dispatch_id;
	String           external_service;// github_actions | ...
	String           run_id;          // external run identifier when known
	String           status;          // pending | running | completed | stale | failed
	String           supersedes;      // previous attempt id, when a retry
	Time             created_at;
	Time             updated_at;

	void Jsonize(JsonIO& io) {
		io("id", id)("dispatch_id", dispatch_id)("external_service", external_service)
		  ("run_id", run_id)("status", status)("supersedes", supersedes)
		  ("created_at", created_at)("updated_at", updated_at);
	}
};

struct PlatformReviewEvidence : Moveable<PlatformReviewEvidence> {
	String           id;              // evd_<hex>
	String           attempt_id;
	String           dispatch_id;
	String           work_item_id;
	String           repository;
	int              pr_number = 0;
	String           head_sha;        // SHA the evidence is bound to
	String           outcome;         // passed | failed | incomplete | stale
	String           findings;        // bounded text
	String           check_url;       // run/check URL
	String           run_id;
	String           external_service;
	String           tool_version;
	String           producer_actor;  // authenticated caller label
	String           event_id;        // namespaced idempotency key used for this record
	Time             recorded_at;

	void Jsonize(JsonIO& io) {
		io("id", id)("attempt_id", attempt_id)("dispatch_id", dispatch_id)
		  ("work_item_id", work_item_id)("repository", repository)("pr_number", pr_number)
		  ("head_sha", head_sha)("outcome", outcome)("findings", findings)
		  ("check_url", check_url)("run_id", run_id)("external_service", external_service)
		  ("tool_version", tool_version)("producer_actor", producer_actor)
		  ("event_id", event_id)("recorded_at", recorded_at);
	}
};

struct PlatformTrialRecord : Moveable<PlatformTrialRecord> {
	String           id;              // trl_<hex>
	String           kind;            // reference | baseline | candidate
	String           work_item_id;    // optional association
	String           repository;
	int              pr_number = 0;
	String           head_sha;
	Vector<String>   splits;
	int              trials_per_split = 0;
	String           requested_by;
	String           external_service;
	String           status;          // dispatched | collecting | complete | failed
	int              expected_results = 0;
	int              received_results = 0;
	String           idempotency_key;
	Time             created_at;
	Time             updated_at;

	// Explicit copy support: U++ Vector members are pick-only.
	PlatformTrialRecord() {}
	PlatformTrialRecord(const PlatformTrialRecord& o)
		: id(o.id), kind(o.kind), work_item_id(o.work_item_id), repository(o.repository)
		, pr_number(o.pr_number), head_sha(o.head_sha), splits(clone(o.splits))
		, trials_per_split(o.trials_per_split), requested_by(o.requested_by)
		, external_service(o.external_service), status(o.status)
		, expected_results(o.expected_results), received_results(o.received_results)
		, idempotency_key(o.idempotency_key), created_at(o.created_at), updated_at(o.updated_at) {}
	PlatformTrialRecord& operator=(const PlatformTrialRecord& o) {
		if(this != &o) {
			id = o.id; kind = o.kind; work_item_id = o.work_item_id; repository = o.repository;
			pr_number = o.pr_number; head_sha = o.head_sha; splits = clone(o.splits);
			trials_per_split = o.trials_per_split; requested_by = o.requested_by;
			external_service = o.external_service; status = o.status;
			expected_results = o.expected_results; received_results = o.received_results;
			idempotency_key = o.idempotency_key; created_at = o.created_at; updated_at = o.updated_at;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("id", id)("kind", kind)("work_item_id", work_item_id)
		  ("repository", repository)("pr_number", pr_number)("head_sha", head_sha)
		  ("splits", splits)("trials_per_split", trials_per_split)
		  ("requested_by", requested_by)("external_service", external_service)
		  ("status", status)("expected_results", expected_results)
		  ("received_results", received_results)("idempotency_key", idempotency_key)
		  ("created_at", created_at)("updated_at", updated_at);
	}
};

struct PlatformTrialResultRecord : Moveable<PlatformTrialResultRecord> {
	String           id;              // res_<hex>
	String           trial_id;
	String           split;
	int              trial_index = 0; // 1-based within the split
	String           outcome;         // recorded | stale
	bool             passed = false;
	double           reward = 0;      // optional numeric reward for the trial
	String           summary;
	String           head_sha;
	String           external_job_id;
	String           event_id;
	Time             recorded_at;

	void Jsonize(JsonIO& io) {
		io("id", id)("trial_id", trial_id)("split", split)("trial_index", trial_index)
		  ("outcome", outcome)("passed", passed)("reward", reward)("summary", summary)
		  ("head_sha", head_sha)("external_job_id", external_job_id)
		  ("event_id", event_id)("recorded_at", recorded_at);
	}
};

struct PlatformArtifactRecord : Moveable<PlatformArtifactRecord> {
	String           id;              // art_<hex>
	String           owner_type;      // evidence | trial | dispatch | work_item
	String           owner_id;
	String           kind;            // review_output | trial_archive | capture | ...
	String           uri;             // external URI (never content)
	String           content_type;
	int64            size = 0;
	String           sha256;          // when known
	String           retention;       // policy label
	String           expires_at;      // ISO when known
	String           availability;    // available | expired | missing | replaced
	String           superseded_by;   // replacing artifact id, when replaced
	String           produced_by;     // authenticated caller label
	String           event_id;
	Time             recorded_at;

	void Jsonize(JsonIO& io) {
		io("id", id)("owner_type", owner_type)("owner_id", owner_id)("kind", kind)
		  ("uri", uri)("content_type", content_type)("size", size)("sha256", sha256)
		  ("retention", retention)("expires_at", expires_at)("availability", availability)
		  ("superseded_by", superseded_by)("produced_by", produced_by)
		  ("event_id", event_id)("recorded_at", recorded_at);
	}
};

// A client contract (company <-> client). Its financial terms are a separate
// money flow from the contributor compensation ledger; nothing here writes to
// the ledger.
struct ContractRecord : Moveable<ContractRecord> {
	String           id;                 // ctr_<hex>
	String           client_user_id;     // the client account that owns the contract
	String           title;
	String           brief;
	double           amount = 0;         // agreed amount, company <-> client
	String           currency;
	String           payment_terms;
	String           practical_details;
	String           status;             // COMPANY_CONTRACT_*
	String           created_by;         // user id of the creator
	Vector<String>   processed_event_keys;
	Time             created_at;
	Time             updated_at;

	ContractRecord() {}
	ContractRecord(const ContractRecord& o)
		: id(o.id), client_user_id(o.client_user_id), title(o.title), brief(o.brief), amount(o.amount)
		, currency(o.currency), payment_terms(o.payment_terms), practical_details(o.practical_details)
		, status(o.status), created_by(o.created_by), processed_event_keys(clone(o.processed_event_keys))
		, created_at(o.created_at), updated_at(o.updated_at) {}
	ContractRecord& operator=(const ContractRecord& o) {
		if(this != &o) {
			id = o.id; client_user_id = o.client_user_id; title = o.title; brief = o.brief; amount = o.amount;
			currency = o.currency; payment_terms = o.payment_terms; practical_details = o.practical_details;
			status = o.status; created_by = o.created_by; processed_event_keys = clone(o.processed_event_keys);
			created_at = o.created_at; updated_at = o.updated_at;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("id", id)("client_user_id", client_user_id)("title", title)("brief", brief)("amount", amount)
		  ("currency", currency)("payment_terms", payment_terms)("practical_details", practical_details)
		  ("status", status)("created_by", created_by)("processed_event_keys", processed_event_keys)
		  ("created_at", created_at)("updated_at", updated_at);
	}
};

// A project under an active contract: one repository, the employees assigned
// to it, and the pull requests an employee accepted on the company's behalf.
struct ProjectRecord : Moveable<ProjectRecord> {
	String           id;                 // prj_<hex>
	String           contract_id;
	String           repo_name;          // owner/repo on the forge
	String           repo_origin;        // COMPANY_REPO_CREATED | COMPANY_REPO_ADOPTED
	String           goals;
	String           status;             // COMPANY_PROJECT_*
	Vector<String>   employee_user_ids;
	Vector<String>   accepted_prs;       // owner/repo#number accepted by an assigned employee
	Vector<String>   processed_event_keys;
	Time             created_at;
	Time             updated_at;

	bool HasEmployee(const String& user_id) const {
		for(int i = 0; i < employee_user_ids.GetCount(); i++)
			if(employee_user_ids[i] == user_id)
				return true;
		return false;
	}

	ProjectRecord() {}
	ProjectRecord(const ProjectRecord& o)
		: id(o.id), contract_id(o.contract_id), repo_name(o.repo_name), repo_origin(o.repo_origin), goals(o.goals)
		, status(o.status), employee_user_ids(clone(o.employee_user_ids)), accepted_prs(clone(o.accepted_prs))
		, processed_event_keys(clone(o.processed_event_keys)), created_at(o.created_at), updated_at(o.updated_at) {}
	ProjectRecord& operator=(const ProjectRecord& o) {
		if(this != &o) {
			id = o.id; contract_id = o.contract_id; repo_name = o.repo_name; repo_origin = o.repo_origin;
			goals = o.goals; status = o.status; employee_user_ids = clone(o.employee_user_ids);
			accepted_prs = clone(o.accepted_prs); processed_event_keys = clone(o.processed_event_keys);
			created_at = o.created_at; updated_at = o.updated_at;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("id", id)("contract_id", contract_id)("repo_name", repo_name)("repo_origin", repo_origin)
		  ("goals", goals)("status", status)("employee_user_ids", employee_user_ids)
		  ("accepted_prs", accepted_prs)("processed_event_keys", processed_event_keys)
		  ("created_at", created_at)("updated_at", updated_at);
	}
};

// Stable idempotency key for compensation of a work item: exactly one credit
// per completion.
inline String CompanyCompensationKey(const String& work_item_id)
{
	return "compensation:" + work_item_id;
}

// Renders a double compactly for UI/API text: integers without decimals,
// otherwise one decimal. (U++ Format has no printf-style %g; keep bare
// letters out of format strings entirely.)
inline String CompanyFormatNumber(double v)
{
	if(v == (double)(int)v)
		return AsString((int)v);
	return FormatDoubleFix(v, 1);
}

inline String CompanyTimeIso(const Time& t)
{
	if(IsNull(t))
		return "";
	// NB: the ISO 'T' separator must never pass through Format() itself —
	// U++ Format treats bare letters as value placeholders.
	return Format("%04d-%02d-%02d", t.year, t.month, t.day) + "T"
	     + Format("%02d:%02d:%02d", t.hour, t.minute, t.second);
}

END_UPP_NAMESPACE

#endif
