#include <Company/CompanySite.h>
#include <Company/CompanyComposed.h>
#include <Company/CompanyWorkflow.h>
#include <Company/PlatformWorkflow.h>
#include <WebEngine/Host.h>
#include <WebEngine/SessionRoutes.h>

// ---------------------------------------------------------------------------
// Company site self-test (port of the still-applicable CompanySkylark
// SelfTests coverage onto the composed Company package).
//
// The run boots one isolated EngineSite ("company") on a temp root and port
// 18231, installs the Company site and session routes, provisions logins with
// SetCompanyUser, then checks:
//   - store and allowlist, and the seeded fixture;
//   - SetCompanyUser (create, update, validation);
//   - broker operations (scopes, refusals, drafts, status, ledger, contracts);
//   - ledger reads: totals, recent rows and caps, subject filter, balances;
//   - workflow transitions (proposal, work item, mechanical, operator review,
//     compensation, ledger correction, role administration);
//   - agent tokens: issue rules, stored hash, scopes, revocation, expiry;
//   - client contracts, client projects, proposal scope and work adoption,
//     project delivery;
//   - platform v2: command listener, review dispatch and head-SHA evidence,
//     artifacts, trials and the status snapshot;
//   - HTTP routes: anonymous refusal, bearer login and tokens, roles, the
//     contract and project routes, the review callback, platform v2, mutations,
//     logout.
// Every path is under GetTempDirectory(); the process removes it on exit.
// ---------------------------------------------------------------------------

NAMESPACE_UPP

static int s_checks = 0;
static int s_failed = 0;

static void Check(bool cond, const String& what)
{
	s_checks++;
	if(cond)
		Cout() << "ok   " << what << "\n";
	else {
		s_failed++;
		Cout() << "FAIL " << what << "\n";
	}
}

static const int TEST_PORT = 18231;
static const char *TEST_REPO = "OuluBSD/CompanySkylark";
static const char *TEST_SHA = "0123456789abcdef0123456789abcdef01234567";

// ---------------------------------------------------------------------------
// Raw HTTP client (Connection: close)
// ---------------------------------------------------------------------------

struct Reply {
	int    status = 0;
	String body;
	String location;
};

static Reply Call(const char *method, const String& path, const String& bearer = String(),
                  const String& json = String())
{
	Reply r;
	TcpSocket s;
	s.Timeout(10000);
	if(!s.Connect("127.0.0.1", TEST_PORT)) {
		Cout() << "connect failed for " << method << " " << path << ": " << s.GetErrorDesc() << "\n";
		return r;
	}
	StringBuffer req;
	req << method << ' ' << path << " HTTP/1.1\r\n";
	req << "Host: 127.0.0.1:" << TEST_PORT << "\r\n";
	req << "Connection: close\r\n";
	if(bearer.GetCount())
		req << "Authorization: Bearer " << bearer << "\r\n";
	if(json.GetCount())
		req << "Content-Type: application/json\r\nContent-Length: " << json.GetCount() << "\r\n";
	req << "\r\n" << json;
	s.Put(req);

	String all;
	String data;
	while((data = s.Get(4096)).GetCount())
		all.Cat(data);
	int q = all.Find("\r\n\r\n");
	String head = q >= 0 ? all.Left(q) : all;
	r.body = q >= 0 ? all.Mid(q + 4) : String();
	Vector<String> lines = Split(head, '\n');
	if(lines.GetCount()) {
		Vector<String> st = Split(lines[0], ' ');
		if(st.GetCount() >= 2)
			r.status = ScanInt(st[1]);
	}
	for(int i = 1; i < lines.GetCount(); i++) {
		String line = TrimBoth(lines[i]);
		int c = line.Find(':');
		if(c >= 0 && ToLower(TrimBoth(line.Left(c))) == "location")
			r.location = TrimBoth(line.Mid(c + 1));
	}
	Cout() << "     trace " << method << " " << path << " -> " << r.status << "\n";
	return r;
}

// The listener accepts before its engine threads serve, so readiness means a
// real answer from /api/health, not a TCP connect.
static bool WaitReady()
{
	for(int i = 0; i < 200; i++) {
		if(Call("GET", "/api/health").status == 200)
			return true;
		Sleep(25);
	}
	return false;
}

// A top-level string field of a JSON reply ("" when the body is not JSON).
static String JsonField(const String& body, const char *key)
{
	try {
		Value v = ParseJSON(body);
		if(v.Is<ValueMap>())
			return (String)((ValueMap)v).Get(key, "");
	}
	catch(Exc&) {
	}
	return String();
}

// A top-level integer field of a JSON reply (-1 when absent or not JSON).
static int JsonInt(const String& body, const char *key)
{
	try {
		Value v = ParseJSON(body);
		if(v.Is<ValueMap>())
			return (int)((ValueMap)v).Get(key, -1);
	}
	catch(Exc&) {
	}
	return -1;
}

static String LoginJson(const char *username, const char *password)
{
	ValueMap m;
	m.Set("username", username);
	m.Set("password", password);
	return AsJSON(m);
}

// The bearer token of a successful password login ("" when the login fails).
static String LoginToken(const char *username, const char *password)
{
	Reply r = Call("POST", "/api/auth/login", String(), LoginJson(username, password));
	return r.status == 200 ? JsonField(r.body, "token") : String();
}

// ---------------------------------------------------------------------------
// Identities (the site derives these from the request; the tests build them
// directly for the headless calls).
// ---------------------------------------------------------------------------

static WebAuthIdentity HumanOf(CompanyStore& store, const String& username)
{
	WebAuthIdentity id;
	UserRecord u;
	if(!store.FindUserByUsername(username, u))
		return id;
	id.authenticated = true;
	id.actor_type = WEBAUTH_ACTOR_HUMAN;
	id.user_id = u.id;
	id.username = u.username;
	id.display_name = u.display_name;
	id.roles = clone(u.roles);
	// The workflow requires a scope and a role; the test grants every scope to
	// humans so that role checks are what decide. Role-derived scopes are
	// exercised over HTTP (the site computes them from the store).
	id.scopes = CompanyAllScopes();
	return id;
}

static WebAuthIdentity AutomationIdentity()
{
	WebAuthIdentity id;
	id.authenticated = true;
	id.actor_type = WEBAUTH_ACTOR_AUTOMATION;
	id.token_id = "tok-test-callback";
	id.scopes.Add(COMPANY_SCOPE_CALLBACK_WRITE);
	return id;
}

static Vector<String> Scopes(const char *a, const char *b = NULL)
{
	Vector<String> s;
	s.Add(a);
	if(b)
		s.Add(b);
	return s;
}

static bool HasScopeOf(const Vector<String>& v, const char *s)
{
	return FindIndex(v, String(s)) >= 0;
}

// ---------------------------------------------------------------------------
// Engine host
// ---------------------------------------------------------------------------

struct CompanyTestHost : WebEngineHost {
	CompanyTestHost()
	{
		port = TEST_PORT;
		threads = 2;
		prefork = 0;
	}
};

static CompanyTestHost *s_host = NULL;

static void HostMain()
{
	s_host->Run();
}

// ---------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------

// Store, allowlist and fixture.
static void RunStoreChecks(CompanyStore& store)
{
	Check(store.IsRepositoryAllowed(TEST_REPO), "store: the composed allowlist permits the test repository");
	Check(!store.IsRepositoryAllowed("evil/repo"), "store: a repository off the allowlist is not permitted");
	store.SeedTestFixture();
	Check(store.GetUsers().GetCount() >= 5, "store: the fixture seeds the users");
	int users = store.GetUsers().GetCount();
	store.SeedTestFixture();
	Check(store.GetUsers().GetCount() == users, "store: a second fixture seed is a no-op");
}

// A malformed id is caller input. Reading it must not mark a collection as
// corrupted (which makes the store refuse every later write). Uses its own
// probe store so a failure cannot cascade into the other sections.
static void RunStoreProbeCheck(const String& root)
{
	CompanyStore probe;
	probe.SetRoot(AppendFileName(root, "probe"));
	String error;
	Check(probe.Initialize(error), "probe: a separate store initializes " + error);
	WorkItemRecord junk;
	bool found = probe.FindWorkItem("", junk);
	Check(!found && probe.GetCorruptionErrors().IsEmpty(),
	      "store: a read with a malformed id does not mark the store corrupted");
}

// SetCompanyUser: validation, create, update and the WebAccounts login.
static void RunProvisionChecks(CompanyStore& store, EngineSite& site)
{
	String error;
	Check(!SetCompanyUser(store, "", "pw", Scopes(COMPANY_ROLE_ADMIN), error) && error.Find("username") >= 0,
	      "provision: an empty username is refused");
	Check(!SetCompanyUser(store, "root", "", Scopes(COMPANY_ROLE_ADMIN), error) && error.Find("password") >= 0,
	      "provision: an empty password is refused");
	Vector<String> none;
	Check(!SetCompanyUser(store, "root", "pw-root-first", none, error) && error.Find("role") >= 0,
	      "provision: at least one role is required");
	Check(!SetCompanyUser(store, "root", "pw-root-first", Scopes("wizard"), error) && error.Find("unknown role") >= 0,
	      "provision: an unknown role is refused");
	UserRecord before;
	Check(!store.FindUserByUsername("root", before), "provision: refused calls create no Company user");
	WebAccountUser login;
	String sid, csrf;
	Check(!site.Accounts().FindUserByUsername("root", login), "provision: refused calls create no login");

	// Create: the Company record (admin role, hashed password) and the login.
	Check(SetCompanyUser(store, "root", "pw-root-first", Scopes(COMPANY_ROLE_ADMIN), error),
	      "provision: the first admin is created " + error);
	UserRecord root;
	Check(store.FindUserByUsername("root", root) && root.HasRole(COMPANY_ROLE_ADMIN),
	      "provision: the Company record carries the admin role");
	Check(root.password_hash.GetCount() > 0 && root.password_hash != "pw-root-first",
	      "provision: the Company record stores a password hash, not the password");
	Check(site.Accounts().FindUserByUsername("root", login), "provision: the WebAccounts login exists");
	int count = store.GetUsers().GetCount();

	// Update: same username, new password, no duplicate record.
	Check(SetCompanyUser(store, "root", "pw-root-second", Scopes(COMPANY_ROLE_ADMIN), error),
	      "provision: an existing user is updated " + error);
	Check(store.GetUsers().GetCount() == count, "provision: an update creates no second Company record");
	UserRecord updated;
	Check(store.FindUserByUsername("root", updated) && updated.id == root.id && updated.password_hash != root.password_hash,
	      "provision: the update keeps the user id and replaces the password hash");
	WebAccountUser ignored;
	Check(site.Accounts().Login("root", "pw-root-second", "127.0.0.1", sid, csrf, ignored, error).ok,
	      "provision: the WebAccounts login accepts the new password");
	Check(!site.Accounts().Login("root", "pw-root-first", "127.0.0.1", sid, csrf, ignored, error).ok,
	      "provision: the WebAccounts login refuses the old password");

	// The fixture admin has a Company record but no login yet: the login is provisioned.
	// Roles are replaced, so the fixture's reviewer and contributor roles are passed again.
	Vector<String> admin_roles = Scopes(COMPANY_ROLE_CONTRIBUTOR, COMPANY_ROLE_REVIEWER);
	admin_roles.Add(COMPANY_ROLE_ADMIN);
	Check(SetCompanyUser(store, "admin", "pw-admin-1", admin_roles, error),
	      "provision: a fixture user without a login gets one " + error);
	Check(site.Accounts().Login("admin", "pw-admin-1", "127.0.0.1", sid, csrf, ignored, error).ok,
	      "provision: the fixture admin signs in with the provisioned password");
	Check(SetCompanyUser(store, "alice", "pw-alice-1", Scopes(COMPANY_ROLE_CONTRIBUTOR), error),
	      "provision: the fixture contributor gets a login " + error);
	Check(SetCompanyUser(store, "bob", "pw-bob-1", Scopes(COMPANY_ROLE_REVIEWER), error),
	      "provision: the fixture reviewer gets a login " + error);
}

// Broker: scopes, refusals, drafts, status, ledger and contracts.
static void RunBrokerChecks(CompanyComposedModule& module, CompanyStore& store)
{
	Vector<String> none;
	ValueMap draft;
	draft.Set("title", "Broker draft");
	draft.Set("description", "Created through the broker");
	draft.Set("repository", TEST_REPO);
	draft.Set("estimated_hours", 2.0);

	CompanyBrokerResult r = module.Handle(COMPANY_BROKER_CREATE_DRAFT, none, "svc", draft);
	Check(!r.ok && r.status == 403, "broker: a draft without proposals:write is refused");
	Vector<String> writer = Scopes(COMPANY_SCOPE_PROPOSALS_WRITE, COMPANY_SCOPE_PROPOSALS_READ);

	ValueMap off_repo = draft;
	off_repo.Set("repository", "evil/repo");
	r = module.Handle(COMPANY_BROKER_CREATE_DRAFT, writer, "svc", off_repo);
	Check(!r.ok && r.status == 400, "broker: a draft for a repository off the allowlist is refused");

	ValueMap no_hours = draft;
	no_hours.Set("estimated_hours", 0.0);
	r = module.Handle(COMPANY_BROKER_CREATE_DRAFT, writer, "svc", no_hours);
	Check(!r.ok && r.status == 400, "broker: a draft without estimated hours is refused");

	int proposals = store.GetProposals().GetCount();
	r = module.Handle(COMPANY_BROKER_CREATE_DRAFT, writer, "svc", draft);
	String draft_id = (String)r.payload.Get("proposal_id", "");
	Check(r.ok && r.status == 201 && draft_id.StartsWith("prp_"), "broker: a valid draft is created with 201");
	Check(store.GetProposals().GetCount() == proposals + 1, "broker: refused drafts create no records");

	ValueMap status_req;
	status_req.Set("proposal_id", draft_id);
	r = module.Handle(COMPANY_BROKER_STATUS, none, "svc", status_req);
	Check(!r.ok && r.status == 403, "broker: status without proposals:read is refused");
	r = module.Handle(COMPANY_BROKER_STATUS, writer, "svc", status_req);
	Check(r.ok && (String)r.payload.Get("status", "") == COMPANY_PROPOSAL_PROPOSED,
	      "broker: status reports the draft as proposed");
	ValueMap missing;
	missing.Set("proposal_id", "prp_missing");
	r = module.Handle(COMPANY_BROKER_STATUS, writer, "svc", missing);
	Check(!r.ok && r.status == 404, "broker: status of an unknown proposal is 404");

	// Ledger and contracts: admin-only views, ledger:read for the summary.
	r = module.Handle(COMPANY_BROKER_LEDGER_SUMMARY, none, "svc", ValueMap());
	Check(!r.ok && r.status == 403, "broker: ledger summary without ledger:read is refused");
	r = module.Handle(COMPANY_BROKER_LEDGER_SUMMARY, Scopes(COMPANY_SCOPE_LEDGER_READ), "svc", ValueMap());
	Check(r.ok && r.status == 200, "broker: ledger summary with ledger:read succeeds");
	r = module.Handle(COMPANY_BROKER_LEDGER_BALANCES, Scopes(COMPANY_SCOPE_LEDGER_READ), "svc", ValueMap());
	Check(!r.ok && r.status == 403, "broker: ledger balances need the admin scope");
	r = module.Handle(COMPANY_BROKER_LEDGER_BALANCES, Scopes(COMPANY_SCOPE_ADMIN), "svc", ValueMap());
	Check(r.ok && r.status == 200, "broker: ledger balances with the admin scope succeed");
	r = module.Handle(COMPANY_BROKER_CONTRACTS_LIST, writer, "svc", ValueMap());
	Check(!r.ok && r.status == 403, "broker: contracts list needs the admin scope");
	r = module.Handle(COMPANY_BROKER_CONTRACTS_LIST, Scopes(COMPANY_SCOPE_ADMIN), "svc", ValueMap());
	Check(r.ok && r.status == 200, "broker: contracts list with the admin scope succeeds");
	r = module.Handle("no-such-op", writer, "svc", ValueMap());
	Check(!r.ok && r.status == 404, "broker: an unknown operation is 404");
}

// Workflow: proposal -> work item -> review -> compensation, plus refusals.
static void RunWorkflowChecks(CompanyStore& store)
{
	WebAuthIdentity alice = HumanOf(store, "alice");
	WebAuthIdentity bob = HumanOf(store, "bob");
	WebAuthIdentity admin = HumanOf(store, "admin");
	WebAuthIdentity automation = AutomationIdentity();

	CompanyWorkflowResult p = CompanyWorkflow::CreateProposal(alice, "Port the company self-tests",
		"Cover the still-applicable routes and workflows", TEST_REPO, 4, "wf-create-1");
	String pid = (String)p.payload.Get("proposal_id", "");
	Check(p.ok && pid.StartsWith("prp_"), "workflow: a contributor proposes work " + p.error);
	CompanyWorkflowResult dup = CompanyWorkflow::CreateProposal(alice, "Port the company self-tests",
		"Cover the still-applicable routes and workflows", TEST_REPO, 4, "wf-create-1");
	Check(dup.ok && dup.idempotent_duplicate, "workflow: a replayed create key is an idempotent duplicate");

	CompanyWorkflowResult refuse = CompanyWorkflow::DecideProposal(alice, pid, COMPANY_ACTION_ACCEPT, "", "", "wf-acc-alice");
	Check(!refuse.ok && refuse.error.Find("reviewer") >= 0, "workflow: a contributor cannot accept a proposal");
	refuse = CompanyWorkflow::DecideProposal(bob, pid, COMPANY_ACTION_REJECT, "", "", "wf-rej-noreason");
	Check(!refuse.ok && refuse.error.Find("reason") >= 0, "workflow: a rejection without a reason is refused");
	CompanyWorkflowResult acc = CompanyWorkflow::DecideProposal(bob, pid, COMPANY_ACTION_ACCEPT, "", "", "wf-acc-1");
	Check(acc.ok, "workflow: a reviewer accepts the proposal " + acc.error);
	ProposalRecord prop;
	Check(store.FindProposal(pid, prop) && prop.status == COMPANY_PROPOSAL_ACCEPTED,
	      "workflow: the proposal is stored as accepted");

	// The accept creates the work item, assigned to the author.
	String wid;
	Vector<WorkItemRecord> items = store.GetWorkItems();
	for(const WorkItemRecord& w : items)
		if(w.proposal_id == pid)
			wid = w.id;
	WorkItemRecord item;
	Check(!wid.IsEmpty() && store.FindWorkItem(wid, item) && item.state == COMPANY_STATE_IMPLEMENTATION &&
	      item.contributor_user_id == alice.user_id, "workflow: accepting creates an implementation item for the author");

	if(wid.IsEmpty()) {
		Check(false, "workflow: no work item to continue with; the remaining workflow checks are skipped");
		return;
	}
	CompanyWorkflowResult bind = CompanyWorkflow::BindPullRequest(alice, wid, TEST_REPO, 0, "", "feature/x", "wf-bind-0");
	Check(!bind.ok && bind.error.Find("PR number") >= 0, "workflow: a PR binding needs a valid PR number");
	bind = CompanyWorkflow::BindPullRequest(bob, wid, TEST_REPO, 101, "https://example/pr/101", "feature/x", "wf-bind-bob");
	Check(!bind.ok, "workflow: a non-contributor cannot bind a PR");
	bind = CompanyWorkflow::BindPullRequest(alice, wid, TEST_REPO, 101, "https://example/pr/101", "feature/x", "wf-bind-1");
	Check(bind.ok, "workflow: the contributor binds PR #101 " + bind.error);

	CompanyWorkflowResult mech = CompanyWorkflow::RequestMechanicalReview(alice, wid, "wf-mech-1");
	Check(mech.ok && store.FindWorkItem(wid, item) && item.state == COMPANY_STATE_MECHANICAL,
	      "workflow: a mechanical review is requested " + mech.error);
	mech = CompanyWorkflow::RequestMechanicalReview(alice, wid, "wf-mech-1");
	Check(mech.ok && mech.idempotent_duplicate, "workflow: a replayed mechanical request is idempotent");

	ValueMap evidence;
	evidence.Set("run_id", "run-1");
	CompanyWorkflowResult pass = CompanyWorkflow::RecordMechanicalResult(automation, wid, true, evidence, "wf-mech-pass-1");
	Check(pass.ok && store.FindWorkItem(wid, item) && item.state == COMPANY_STATE_OPERATOR,
	      "workflow: a passing callback moves the item to operator review " + pass.error);
	CompanyWorkflowResult pass_dup = CompanyWorkflow::RecordMechanicalResult(automation, wid, true, evidence, "wf-mech-pass-1");
	Check(pass_dup.ok && pass_dup.idempotent_duplicate, "workflow: a replayed callback is idempotent");
	CompanyWorkflowResult agent_pass = CompanyWorkflow::RecordMechanicalResult(alice, wid, true, evidence, "wf-mech-human");
	Check(!agent_pass.ok, "workflow: a human without callback:write cannot record a mechanical result");

	CompanyWorkflowResult op_req = CompanyWorkflow::RequestOperatorReview(alice, wid, "wf-op-req-1");
	Check(op_req.ok, "workflow: the operator review is requested " + op_req.error);
	CompanyWorkflowResult self_approve = CompanyWorkflow::DecideOperatorReview(alice, wid, COMPANY_OP_ACTION_APPROVE, "", "wf-op-self");
	Check(!self_approve.ok, "workflow: a contributor cannot approve their own work");
	CompanyWorkflowResult approve = CompanyWorkflow::DecideOperatorReview(admin, wid, COMPANY_OP_ACTION_APPROVE, "", "wf-op-approve");
	Check(approve.ok && store.FindWorkItem(wid, item) && item.state == COMPANY_STATE_COMPLETED,
	      "workflow: a reviewer approves and the item is completed " + approve.error);

	// Compensation: 4 estimated hours at 40 per hour is one credit of 160.
	// The fixture gives the contributor a rate; clear it to check the refusal first.
	CompanyWorkflowResult cleared = CompanyWorkflow::SetUserHourlyRate(admin, alice.user_id, 0.0, "wf-rate-0");
	Check(cleared.ok, "workflow: an admin clears the contributor's hourly rate " + cleared.error);
	CompanyWorkflowResult no_rate = CompanyWorkflow::IssueCompensation(admin, wid, "early", "wf-comp-norate");
	Check(!no_rate.ok && no_rate.error.Find("hourly rate") >= 0, "workflow: compensation needs an hourly rate");
	CompanyWorkflowResult rate = CompanyWorkflow::SetUserHourlyRate(admin, alice.user_id, 40.0, "wf-rate-1");
	Check(rate.ok, "workflow: an admin sets the contributor's hourly rate " + rate.error);
	int64 balance = store.LedgerBalance(alice.user_id);
	CompanyWorkflowResult self_pay = CompanyWorkflow::IssueCompensation(alice, wid, "self", "wf-comp-self");
	Check(!self_pay.ok, "workflow: a contributor cannot compensate their own work");
	CompanyWorkflowResult pay = CompanyWorkflow::IssueCompensation(admin, wid, "delivered", "wf-comp-1");
	Check(pay.ok && store.FindWorkItem(wid, item) && item.state == COMPANY_STATE_COMPENSATED,
	      "workflow: an admin compensates the completed item " + pay.error);
	Check(store.LedgerBalance(alice.user_id) == balance + 160, "workflow: one credit of 160 is recorded");
	CompanyWorkflowResult pay_dup = CompanyWorkflow::IssueCompensation(admin, wid, "delivered", "wf-comp-1");
	Check(pay_dup.ok && pay_dup.idempotent_duplicate && store.LedgerBalance(alice.user_id) == balance + 160,
	      "workflow: a replayed compensation key creates no second credit");

	CompanyWorkflowResult corr = CompanyWorkflow::LedgerCorrection(bob, alice.user_id, -10, "not allowed", "wf-corr-bob");
	Check(!corr.ok, "workflow: a reviewer cannot post a ledger correction");
	corr = CompanyWorkflow::LedgerCorrection(admin, alice.user_id, -10, "test correction", "wf-corr-1");
	Check(corr.ok && store.LedgerBalance(alice.user_id) == balance + 150,
	      "workflow: an admin correction is signed and applied");

	CompanyWorkflowResult roles = CompanyWorkflow::SetUserRoles(bob, alice.user_id, Scopes(COMPANY_ROLE_REVIEWER), "wf-roles-bob");
	Check(!roles.ok, "workflow: a reviewer cannot change roles");
	roles = CompanyWorkflow::SetUserRoles(admin, HumanOf(store, "bob").user_id, Scopes(COMPANY_ROLE_REVIEWER), "wf-roles-1");
	Check(roles.ok, "workflow: an admin sets a user's roles " + roles.error);
}

// Platform v2: the command listener, its refusals and the status snapshot.
static void RunPlatformChecks(CompanyStore& store)
{
	WebAuthIdentity alice = HumanOf(store, "alice");
	WebAuthIdentity listener = AutomationIdentity();

	CompanyWorkflowResult deny = PlatformWorkflow::IngestCommand(alice, "api", TEST_REPO, 101, "alice", "/review", TEST_SHA, "pt-deny");
	Check(!deny.ok, "platform: a human contributor cannot ingest commands");

	CompanyWorkflowResult cmd = PlatformWorkflow::IngestCommand(listener, "api", TEST_REPO, 101, "alice", "/review", TEST_SHA, "pt-review-1");
	Check(cmd.ok && (String)cmd.payload.Get("status", "") == PLATFORM_CMD_RECEIVED,
	      "platform: a /review command is received " + cmd.error);
	Check((String)cmd.payload.Get("command", "") == PLATFORM_CMD_REVIEW, "platform: /review is parsed as review");
	CompanyWorkflowResult replay = PlatformWorkflow::IngestCommand(listener, "api", TEST_REPO, 101, "alice", "/review", TEST_SHA, "pt-review-1");
	Check(replay.ok && replay.idempotent_duplicate, "platform: a replayed command is idempotent");

	CompanyWorkflowResult run_user = PlatformWorkflow::IngestCommand(listener, "api", TEST_REPO, 101, "alice",
		String("/run reference ") + TEST_SHA, TEST_SHA, "pt-run-alice");
	Check(run_user.ok && (String)run_user.payload.Get("status", "") == PLATFORM_CMD_REFUSED,
	      "platform: a run command from a non-admin login is refused");
	CompanyWorkflowResult run_admin = PlatformWorkflow::IngestCommand(listener, "api", TEST_REPO, 101, "admin",
		String("/run reference ") + TEST_SHA, TEST_SHA, "pt-run-admin");
	Check(run_admin.ok && (String)run_admin.payload.Get("status", "") == PLATFORM_CMD_RECEIVED,
	      "platform: a run command from an admin login is received");
	CompanyWorkflowResult noise = PlatformWorkflow::IngestCommand(listener, "api", TEST_REPO, 101, "admin",
		"hello there", TEST_SHA, "pt-noise");
	Check(noise.ok && (String)noise.payload.Get("command", "") == PLATFORM_CMD_UNKNOWN,
	      "platform: an unrecognized command is recorded as unrecognized");
	ValueMap snap = PlatformWorkflow::StatusSnapshot();
	Check(snap.GetCount() > 0, "platform: the status snapshot is produced");
}

// ---------------------------------------------------------------------------
// Ids and counts that one block creates and a later block reads.
// ---------------------------------------------------------------------------

static String s_read_token;       // work:read agent token, issued through the store
static String s_gone_token;       // agent token revoked through the store
static String s_expired_token;    // agent token issued with a past expiry
static String s_contract_id;      // active client contract (client carla)
static String s_project_id;       // active project (employee erin assigned)
static String s_idle_project_id;  // planning project without an employee
static String s_adopted_item;     // work item adopted by erin into the project
static String s_pass_item;        // mechanical review item, callback passes it
static String s_fail_item;        // mechanical review item, callback fails it
static String s_http_item;        // mechanical review item, dispatched over HTTP
static String s_evidence_id;      // platform evidence, owner of the artifact check
static int    s_ledger_before = 0;

static const char *TEST_SHA_A = "5f4c0a1b2c3d4e5f60718293a4b5c6d7e8f90a1b";

// A proposal from alice, accepted by bob with alice assigned. Returns the
// work item id ("" when the chain is refused).
static String AcceptedItem(CompanyStore& store, const String& key)
{
	WebAuthIdentity alice = HumanOf(store, "alice");
	WebAuthIdentity bob = HumanOf(store, "bob");
	CompanyWorkflowResult p = CompanyWorkflow::CreateProposal(alice, "Fixture task " + key,
		"Fixture for the workflow checks", TEST_REPO, 2, key + "-create");
	String pid = (String)p.payload.Get("proposal_id", "");
	CompanyWorkflowResult acc = CompanyWorkflow::DecideProposal(bob, pid, COMPANY_ACTION_ACCEPT, "",
		alice.user_id, key + "-accept");
	return acc.ok ? (String)acc.payload.Get("work_item_id", "") : String();
}

// An accepted item bound to a pull request and waiting in mechanical review.
static String MechanicalItem(CompanyStore& store, int pr_number)
{
	WebAuthIdentity alice = HumanOf(store, "alice");
	String key = Format("mi-%d", pr_number);
	String wid = AcceptedItem(store, key);
	if(wid.IsEmpty())
		return String();
	bool bound = CompanyWorkflow::BindPullRequest(alice, wid, TEST_REPO, pr_number,
		Format("https://github.com/OuluBSD/CompanySkylark/pull/%d", pr_number),
		"feature/fixture", key + "-bind").ok;
	bool mech = CompanyWorkflow::RequestMechanicalReview(alice, wid, key + "-mech").ok;
	return bound && mech ? wid : String();
}

// Ledger reads: totals, recent rows and their caps, the subject filter, balances
// and the admin contract list. The refusals of the same operations are in
// RunBrokerChecks.
static LedgerEntry LedgerRow(const String& key, const char *type, int amount, const String& user, Time at)
{
	LedgerEntry e;
	e.entry_id = CompanyGenerateId("lgr");
	e.idempotency_key = key;
	e.type = type;
	e.amount = amount;
	e.user_id = user;
	e.created_at = at;
	return e;
}

static Time LedgerTimeOf(const Vector<LedgerEntry>& ledger, int seq)
{
	for(const LedgerEntry& e : ledger)
		if(e.seq == seq)
			return e.created_at;
	return Time();
}

static void RunLedgerChecks(CompanyComposedModule& module, CompanyStore& store)
{
	Vector<String> reader = Scopes(COMPANY_SCOPE_LEDGER_READ);
	Vector<String> admin = Scopes(COMPANY_SCOPE_ADMIN);
	Vector<String> admin_reader = Scopes(COMPANY_SCOPE_ADMIN, COMPANY_SCOPE_LEDGER_READ);
	ValueMap empty;
	const Time t0 = GetSysTime();
	String error;

	// Totals are deltas from the ledger as it stands (signed amounts).
	CompanyBrokerResult base = module.Handle(COMPANY_BROKER_LEDGER_SUMMARY, reader, "svc", empty);
	const int base_credit = (int)base.payload.Get("credit_total", 0);
	const int base_debit = (int)base.payload.Get("debit_total", 0);
	const int base_correction = (int)base.payload.Get("correction_total", 0);
	const int base_count = (int)base.payload.Get("entry_count", 0);
	bool appended = store.AppendLedger(LedgerRow("fin-credit-a", COMPANY_LEDGER_CREDIT, 50, "usr-fin-a", t0), error)
	             && store.AppendLedger(LedgerRow("fin-debit-a", COMPANY_LEDGER_DEBIT, -20, "usr-fin-a", t0 + 1), error)
	             && store.AppendLedger(LedgerRow("fin-corr-b", COMPANY_LEDGER_CORRECTION, 5, "usr-fin-b", t0 + 2), error);
	Check(appended, "ledger: a credit, a debit and a correction are appended " + error);

	CompanyBrokerResult summary = module.Handle(COMPANY_BROKER_LEDGER_SUMMARY, reader, "svc", empty);
	Check(summary.ok && (int)summary.payload.Get("credit_total", 0) == base_credit + 50 &&
	      (int)summary.payload.Get("debit_total", 0) == base_debit - 20 &&
	      (int)summary.payload.Get("correction_total", 0) == base_correction + 5 &&
	      (int)summary.payload.Get("entry_count", 0) == base_count + 3,
	      "ledger: the summary totals credits, debits and corrections by type");
	Check(!((String)summary.payload.Get("newest_entry_at", "")).IsEmpty(),
	      "ledger: the summary reports the newest entry time");

	// Recent rows: newest first, at most 50, default 20, user ids by scope.
	for(int i = 0; i < 3; i++)
		store.AppendLedger(LedgerRow(Format("fin-recent-%d", i), COMPANY_LEDGER_CREDIT, 1, "usr-fin-c",
		                             t0 + 100 + 10 * i), error);
	CompanyBrokerResult recent = module.Handle(COMPANY_BROKER_LEDGER_RECENT, reader, "svc", empty);
	ValueArray rows = recent.payload.Get("entries", ValueArray());
	Vector<LedgerEntry> ledger = store.GetLedger();
	int newest_seq = -1;
	Time newest_at;
	for(const LedgerEntry& e : ledger)
		if(newest_seq < 0 || e.created_at > newest_at) {
			newest_seq = e.seq;
			newest_at = e.created_at;
		}
	bool ordered = true;
	for(int i = 1; i < rows.GetCount(); i++)
		if(LedgerTimeOf(ledger, (int)((ValueMap)rows[i]).Get("seq", -1)) >
		   LedgerTimeOf(ledger, (int)((ValueMap)rows[i - 1]).Get("seq", -1)))
			ordered = false;
	Check(recent.ok && rows.GetCount() > 0 && (int)((ValueMap)rows[0]).Get("seq", -1) == newest_seq && ordered,
	      "ledger: recent lists the newest entry first, in non-increasing time order");
	Check(rows.GetCount() > 0 && ((ValueMap)rows[0]).Find("user_id") < 0,
	      "ledger: a ledger:read-only caller gets rows without user_id");

	for(int i = 0; i < 55; i++)
		store.AppendLedger(LedgerRow(Format("fin-cap-%d", i), COMPANY_LEDGER_CREDIT, 1, "usr-fin-cap",
		                             t0 + 200 + i), error);
	ValueMap big;
	big.Set("limit", 500);
	CompanyBrokerResult capped = module.Handle(COMPANY_BROKER_LEDGER_RECENT, reader, "svc", big);
	Check(capped.ok && ((ValueArray)capped.payload.Get("entries", ValueArray())).GetCount() == 50,
	      "ledger: a request above 50 rows is capped at 50");
	CompanyBrokerResult dflt = module.Handle(COMPANY_BROKER_LEDGER_RECENT, reader, "svc", empty);
	Check(((ValueArray)dflt.payload.Get("entries", ValueArray())).GetCount() == 20,
	      "ledger: the default limit is 20 rows");

	// Subject filter: admin scope only; an admin caller also gets user ids.
	ValueMap subject;
	subject.Set("subject_user_id", "usr-fin-c");
	CompanyBrokerResult refused = module.Handle(COMPANY_BROKER_LEDGER_RECENT, reader, "svc", subject);
	Check(!refused.ok && refused.status == 403 && refused.error.Find("subject filter") >= 0,
	      "ledger: a subject filter without the admin scope is refused");
	CompanyBrokerResult filtered = module.Handle(COMPANY_BROKER_LEDGER_RECENT, admin_reader, "svc", subject);
	ValueArray frows = filtered.payload.Get("entries", ValueArray());
	bool subject_only = frows.GetCount() == 3;
	for(int i = 0; i < frows.GetCount(); i++)
		subject_only = subject_only && (String)((ValueMap)frows[i]).Get("user_id", "") == "usr-fin-c";
	Check(filtered.ok && subject_only, "ledger: the admin subject filter returns only that user's rows");
	Check(frows.GetCount() > 0 && ((ValueMap)frows[0]).Find("user_id") >= 0,
	      "ledger: an admin caller gets rows with user_id");

	// Balances: per-user projection of every entry.
	CompanyBrokerResult balances = module.Handle(COMPANY_BROKER_LEDGER_BALANCES, admin, "svc", empty);
	ValueArray brows = balances.payload.Get("balances", ValueArray());
	int bal_a = -1, bal_b = -1;
	for(int i = 0; i < brows.GetCount(); i++) {
		ValueMap b = brows[i];
		if((String)b.Get("user_id", "") == "usr-fin-a") bal_a = (int)b.Get("balance", -1);
		if((String)b.Get("user_id", "") == "usr-fin-b") bal_b = (int)b.Get("balance", -1);
	}
	Check(balances.ok && bal_a == 30 && bal_b == 5,
	      "ledger: balances project credit plus debit per user (usr-fin-a 30, usr-fin-b 5)");

	// Contracts list: the company's view of a stored contract.
	ContractRecord contract;
	contract.id = CompanyGenerateId("ctr");
	contract.client_user_id = "usr-fin-client";
	contract.title = "Financial fixture";
	contract.amount = 1200;
	contract.currency = "EUR";
	contract.status = COMPANY_CONTRACT_ACTIVE;
	contract.created_by = "usr-fin-client";
	contract.created_at = t0;
	contract.updated_at = t0;
	Check(store.SaveContract(contract, error), "ledger: a contract fixture is stored " + error);
	CompanyBrokerResult contracts = module.Handle(COMPANY_BROKER_CONTRACTS_LIST, admin, "svc", empty);
	ValueArray crows = contracts.payload.Get("contracts", ValueArray());
	bool found = false;
	for(int i = 0; i < crows.GetCount(); i++) {
		ValueMap c = crows[i];
		if((String)c.Get("id", "") == contract.id)
			found = (String)c.Get("client_user_id", "") == "usr-fin-client" &&
			        (double)c.Get("amount", 0.0) == 1200.0 &&
			        (String)c.Get("currency", "") == "EUR" &&
			        (String)c.Get("status", "") == COMPANY_CONTRACT_ACTIVE;
	}
	Check(contracts.ok && found, "ledger: the admin contracts list returns the stored contract row");
}

// Agent tokens: who may issue and revoke, what is stored, and the lifecycle.
// The bearer behaviour (scopes, expiry, revocation) is checked over HTTP in
// RunAgentTokenHttpChecks.
static void RunAgentTokenChecks(CompanyStore& store)
{
	WebAuthIdentity alice = HumanOf(store, "alice");
	WebAuthIdentity admin = HumanOf(store, "admin");
	WebAuthIdentity automation = AutomationIdentity();
	Vector<String> none;
	String raw;

	CompanyWorkflowResult r = CompanyWorkflow::CreateAgentToken(alice, "alice-token", "",
		Scopes(COMPANY_SCOPE_WORK_READ), Time(), "tok-alice", raw);
	Check(!r.ok && raw.IsEmpty() && r.error.Find("admin role") >= 0,
	      "token: a contributor cannot create an agent token");
	r = CompanyWorkflow::CreateAgentToken(automation, "automation-token", "",
		Scopes(COMPANY_SCOPE_WORK_READ), Time(), "tok-automation", raw);
	Check(!r.ok && raw.IsEmpty(), "token: an automation identity cannot create an agent token");
	r = CompanyWorkflow::CreateAgentToken(admin, "no-scope", "", none, Time(), "tok-noscope", raw);
	Check(!r.ok && r.error.Find("at least one scope") >= 0, "token: a token without scopes is refused");
	r = CompanyWorkflow::CreateAgentToken(admin, "bad-scope", "", Scopes("wizard:cast"), Time(), "tok-badscope", raw);
	Check(!r.ok && r.error.Find("unknown scope") >= 0, "token: an unknown scope is refused");
	r = CompanyWorkflow::CreateAgentToken(admin, "   ", "", Scopes(COMPANY_SCOPE_WORK_READ), Time(), "tok-noname", raw);
	Check(!r.ok && r.error.Find("name") >= 0, "token: a token without a name is refused");
	r = CompanyWorkflow::CreateAgentToken(admin, "owned", "usr-missing", Scopes(COMPANY_SCOPE_WORK_READ), Time(),
		"tok-noowner", raw);
	Check(!r.ok && r.error.Find("owner user not found") >= 0, "token: an unknown owner is refused");

	// Issue: the raw token is returned once; only its peppered hash is stored.
	r = CompanyWorkflow::CreateAgentToken(admin, "reader", "", Scopes(COMPANY_SCOPE_WORK_READ), Time(),
		"tok-reader", raw);
	String reader_id = (String)r.payload.Get("token_id", "");
	Check(r.ok && raw.StartsWith(COMPANY_AGENT_TOKEN_PREFIX), "token: an admin issues a work:read token with the waat_ prefix");
	AgentTokenRecord rec;
	Check(store.FindToken(reader_id, rec) && rec.token_hash == CompanyTokenHash(store.GetSecret(), raw) &&
	      rec.token_hash != raw && IsNull(rec.revoked_at),
	      "token: the store keeps the peppered hash, never the raw token, and the token is active");
	Check(store.FindTokenByHash(CompanyTokenHash(store.GetSecret(), raw), rec) && rec.id == reader_id,
	      "token: the token is found by the hash of its raw value");
	s_read_token = raw;
	String replay_raw;
	CompanyWorkflowResult replay = CompanyWorkflow::CreateAgentToken(admin, "reader", "", Scopes(COMPANY_SCOPE_WORK_READ),
		Time(), "tok-reader", replay_raw);
	Check(replay.ok && replay.idempotent_duplicate && replay_raw.IsEmpty(),
	      "token: a replayed create key issues no second raw token");

	// Expiry is enforced at bearer time (checked over HTTP); here it is stored.
	String expired_raw;
	r = CompanyWorkflow::CreateAgentToken(admin, "expired", "", Scopes(COMPANY_SCOPE_WORK_READ),
		GetSysTime() - 3600, "tok-expired", expired_raw);
	s_expired_token = expired_raw;
	Check(r.ok && !expired_raw.IsEmpty(), "token: a token with a past expiry is recorded");

	// Revocation: admin only, idempotent, and the record keeps its revoked time.
	String gone_raw;
	r = CompanyWorkflow::CreateAgentToken(admin, "gone", "", Scopes(COMPANY_SCOPE_WORK_READ), Time(),
		"tok-gone", gone_raw);
	String gone_id = (String)r.payload.Get("token_id", "");
	s_gone_token = gone_raw;
	r = CompanyWorkflow::RevokeAgentToken(alice, gone_id, "not mine", "tok-rev-alice");
	Check(!r.ok, "token: a contributor cannot revoke a token");
	r = CompanyWorkflow::RevokeAgentToken(admin, "tok-missing", "x", "tok-rev-missing");
	Check(!r.ok && r.error.Find("not found") >= 0, "token: revoking an unknown token is refused");
	r = CompanyWorkflow::RevokeAgentToken(admin, gone_id, "rotated", "tok-rev-1");
	Check(r.ok && !r.idempotent_duplicate && store.FindToken(gone_id, rec) && !IsNull(rec.revoked_at),
	      "token: an admin revokes a token and the record keeps the revoked time");
	r = CompanyWorkflow::RevokeAgentToken(admin, gone_id, "rotated again", "tok-rev-2");
	Check(r.ok && r.idempotent_duplicate, "token: revoking a revoked token is an idempotent no-op");
}

// Client contracts: create, negotiate, accept, close and the role rules. Two
// client accounts are provisioned here; their logins serve the HTTP checks.
static void RunContractChecks(CompanyStore& store)
{
	String error;
	s_ledger_before = store.GetLedger().GetCount();
	Check(SetCompanyUser(store, "carla", "pw-carla-1", Scopes(COMPANY_ROLE_CLIENT), error),
	      "contract: client carla is provisioned " + error);
	Check(SetCompanyUser(store, "dan", "pw-dan-1", Scopes(COMPANY_ROLE_CLIENT), error),
	      "contract: client dan is provisioned " + error);
	WebAuthIdentity carla = HumanOf(store, "carla");
	WebAuthIdentity dan = HumanOf(store, "dan");
	WebAuthIdentity admin = HumanOf(store, "admin");
	WebAuthIdentity alice = HumanOf(store, "alice");
	int audit_before = store.GetAudit().GetCount();

	CompanyWorkflowResult r = CompanyWorkflow::CreateContract(admin, "Admin tries", "x", 100, "EUR", "", "", "ctr-admin");
	Check(!r.ok && r.error.Find("client role") >= 0, "contract: an admin cannot create a contract");
	r = CompanyWorkflow::CreateContract(alice, "Contributor tries", "x", 100, "EUR", "", "", "ctr-alice");
	Check(!r.ok && r.error.Find("client role") >= 0, "contract: a contributor cannot create a contract");
	r = CompanyWorkflow::CreateContract(carla, "Site rebuild", "Rebuild the site", 5000, "EUR", "net 30", "remote",
		"ctr-create");
	String cid = (String)r.payload.Get("contract_id", "");
	ContractRecord c;
	Check(r.ok && cid.StartsWith("ctr_") && store.FindContract(cid, c) && c.status == COMPANY_CONTRACT_PROPOSED &&
	      c.client_user_id == carla.user_id,
	      "contract: a client creates a proposed contract with a ctr_ id");
	r = CompanyWorkflow::CreateContract(carla, "Site rebuild", "Rebuild the site", 5000, "EUR", "net 30", "remote",
		"ctr-create");
	Check(r.ok && r.idempotent_duplicate, "contract: replaying the create key is a duplicate no-op");
	r = CompanyWorkflow::CreateContract(carla, "No key", "x", 1, "EUR", "", "", "");
	Check(!r.ok && r.error.Find("idempotency_key") >= 0, "contract: a create without a key is refused");
	r = CompanyWorkflow::CreateContract(carla, "Zero", "x", 0, "EUR", "", "", "ctr-zero");
	Check(!r.ok && r.error.Find("greater than zero") >= 0, "contract: a zero amount is refused");

	r = CompanyWorkflow::AcceptContract(carla, cid, "ctr-accept-client");
	Check(!r.ok && r.error.Find("admin role") >= 0, "contract: a client cannot accept its own contract");
	r = CompanyWorkflow::CloseContract(admin, cid, true, "ctr-close-early");
	Check(!r.ok && r.error.Find("only an active contract") >= 0, "contract: a proposed contract cannot be closed");
	r = CompanyWorkflow::NegotiateContract(carla, cid, 6000, "net 45", "ctr-neg-client");
	Check(!r.ok && r.error.Find("admin role") >= 0, "contract: a client cannot negotiate its contract");
	r = CompanyWorkflow::NegotiateContract(admin, cid, 0, "net 45", "ctr-neg-zero");
	Check(!r.ok && r.error.Find("greater than zero") >= 0, "contract: a negotiated amount must be positive");
	r = CompanyWorkflow::NegotiateContract(admin, cid, 6200, "net 45", "ctr-neg");
	Check(r.ok && store.FindContract(cid, c) && c.status == COMPANY_CONTRACT_NEGOTIATING && c.amount == 6200 &&
	      c.payment_terms == "net 45",
	      "contract: admin negotiates the amount and payment terms");
	r = CompanyWorkflow::NegotiateContract(admin, cid, 1, "net 1", "ctr-neg");
	Check(r.ok && r.idempotent_duplicate && store.FindContract(cid, c) && c.amount == 6200,
	      "contract: a replayed negotiation changes nothing");
	r = CompanyWorkflow::AcceptContract(admin, cid, "ctr-accept");
	Check(r.ok && store.FindContract(cid, c) && c.status == COMPANY_CONTRACT_ACTIVE,
	      "contract: admin accepts the contract into the active state");
	r = CompanyWorkflow::NegotiateContract(admin, cid, 7000, "net 60", "ctr-neg-late");
	Check(!r.ok && r.error.Find("not negotiable") >= 0, "contract: an active contract cannot be renegotiated");
	r = CompanyWorkflow::CloseContract(dan, cid, true, "ctr-close-dan");
	Check(!r.ok && r.error.Find("admin role") >= 0, "contract: another client cannot close a contract");
	s_contract_id = cid;

	// A second contract is cancelled, the cancel path of the close operation.
	r = CompanyWorkflow::CreateContract(dan, "Second", "Another brief", 800, "EUR", "", "", "ctr-second");
	String cid2 = (String)r.payload.Get("contract_id", "");
	Check(r.ok && !cid2.IsEmpty(), "contract: a second client creates a contract " + r.error);
	Check(CompanyWorkflow::AcceptContract(admin, cid2, "ctr-second-accept").ok, "contract: admin accepts the second contract");
	r = CompanyWorkflow::CloseContract(admin, cid2, false, "ctr-cancel");
	Check(r.ok && store.FindContract(cid2, c) && c.status == COMPANY_CONTRACT_CANCELLED,
	      "contract: admin cancels an active contract");

	Check(store.GetAudit().GetCount() > audit_before, "contract: each applied transition wrote an audit event");
}

// Projects: create under an active contract, employees, start, and accepted
// pull requests. Employees are provisioned here; the project stays active for
// the scope and delivery blocks.
static void RunProjectChecks(CompanyStore& store)
{
	String error;
	Check(SetCompanyUser(store, "erin", "pw-erin-1", Scopes(COMPANY_ROLE_EMPLOYEE), error),
	      "project: employee erin is provisioned " + error);
	Check(SetCompanyUser(store, "frank", "pw-frank-1", Scopes(COMPANY_ROLE_EMPLOYEE), error),
	      "project: employee frank is provisioned " + error);
	WebAuthIdentity admin = HumanOf(store, "admin");
	WebAuthIdentity carla = HumanOf(store, "carla");
	WebAuthIdentity alice = HumanOf(store, "alice");
	WebAuthIdentity erin = HumanOf(store, "erin");
	WebAuthIdentity frank = HumanOf(store, "frank");
	const String cid = s_contract_id;

	CompanyWorkflowResult r = CompanyWorkflow::CreateProject(carla, cid, TEST_REPO, COMPANY_REPO_CREATED, "goals", "prj-client");
	Check(!r.ok && r.error.Find("admin role") >= 0, "project: a client cannot create a project");
	r = CompanyWorkflow::CreateProject(admin, cid, "other/repo", COMPANY_REPO_ADOPTED, "goals", "prj-adopt-off");
	Check(!r.ok && r.error.Find("allowlist") >= 0, "project: an adopted repository must be on the allowlist");
	r = CompanyWorkflow::CreateProject(admin, cid, TEST_REPO, "borrowed", "goals", "prj-origin");
	Check(!r.ok && r.error.Find("created or adopted") >= 0, "project: the repository origin must be created or adopted");
	r = CompanyWorkflow::CreateProject(admin, "ctr_missing", TEST_REPO, COMPANY_REPO_CREATED, "goals", "prj-nocontract");
	Check(!r.ok && r.error.Find("not found") >= 0, "project: a project needs a known contract");
	r = CompanyWorkflow::CreateContract(carla, "Pending", "Not yet accepted", 900, "EUR", "", "", "ctr-pending");
	String pending = (String)r.payload.Get("contract_id", "");
	r = CompanyWorkflow::CreateProject(admin, pending, TEST_REPO, COMPANY_REPO_CREATED, "goals", "prj-pending");
	Check(!r.ok && r.error.Find("active contract") >= 0, "project: a project needs an active contract");

	r = CompanyWorkflow::CreateProject(admin, cid, TEST_REPO, COMPANY_REPO_CREATED, "Ship it", "prj-create");
	s_project_id = (String)r.payload.Get("project_id", "");
	ProjectRecord p;
	Check(r.ok && store.FindProject(s_project_id, p) && p.status == COMPANY_PROJECT_PLANNING && p.contract_id == cid,
	      "project: admin creates a planning project under the active contract");
	r = CompanyWorkflow::CreateProject(admin, cid, TEST_REPO, COMPANY_REPO_CREATED, "Idle", "prj-idle");
	s_idle_project_id = (String)r.payload.Get("project_id", "");
	Check(r.ok, "project: a second planning project is created " + r.error);

	r = CompanyWorkflow::AssignEmployee(admin, s_project_id, alice.user_id, "prj-assign-contrib");
	Check(!r.ok && r.error.Find("no employee role") >= 0, "project: a contributor cannot be assigned as an employee");
	r = CompanyWorkflow::AssignEmployee(admin, s_project_id, erin.user_id, "prj-assign-erin");
	Check(r.ok && store.FindProject(s_project_id, p) && p.HasEmployee(erin.user_id), "project: an employee is assigned");
	r = CompanyWorkflow::StartProject(admin, s_idle_project_id, "prj-start-idle");
	Check(!r.ok && r.error.Find("assign at least one employee") >= 0, "project: a project without an employee cannot start");
	r = CompanyWorkflow::StartProject(admin, s_project_id, "prj-start");
	Check(r.ok && store.FindProject(s_project_id, p) && p.status == COMPANY_PROJECT_ACTIVE, "project: admin starts the project");

	const String pr_ref = String(TEST_REPO) + "#21";
	r = CompanyWorkflow::AcceptProjectPullRequest(alice, s_project_id, pr_ref, "pr-alice");
	Check(!r.ok && r.error.Find("employee role") >= 0, "pr: a contributor cannot accept a pull request");
	r = CompanyWorkflow::AcceptProjectPullRequest(frank, s_project_id, pr_ref, "pr-frank");
	Check(!r.ok && r.error.Find("assigned") >= 0, "pr: an unassigned employee cannot accept it");
	r = CompanyWorkflow::AcceptProjectPullRequest(erin, s_project_id, "other/repo#1", "pr-wrong");
	Check(!r.ok && r.error.Find("project's repository") >= 0, "pr: a pull request from another repository is refused");
	r = CompanyWorkflow::AcceptProjectPullRequest(erin, s_project_id, pr_ref, "pr-erin");
	Check(r.ok && store.FindProject(s_project_id, p) && p.accepted_prs.GetCount() == 1, "pr: the assigned employee accepts the pull request");
	r = CompanyWorkflow::AcceptProjectPullRequest(erin, s_project_id, pr_ref, "pr-erin-again");
	Check(!r.ok && r.error.Find("already accepted") >= 0, "pr: the same pull request is not accepted twice");
}

// Proposal scope and work adoption: a task joins a project, and an assigned
// employee takes over the work of a contributor.
static void RunProposalScopeChecks(CompanyStore& store)
{
	WebAuthIdentity alice = HumanOf(store, "alice");
	WebAuthIdentity admin = HumanOf(store, "admin");
	WebAuthIdentity carla = HumanOf(store, "carla");
	WebAuthIdentity erin = HumanOf(store, "erin");
	WebAuthIdentity frank = HumanOf(store, "frank");

	String wid = AcceptedItem(store, "scope-task");
	WorkItemRecord item;
	Check(!wid.IsEmpty() && store.FindWorkItem(wid, item), "scope: a reviewer accepts the task to be scoped");
	const String pid = item.proposal_id;

	CompanyWorkflowResult r = CompanyWorkflow::ScopeProposalToProject(frank, pid, s_project_id, "scope-frank");
	Check(!r.ok && r.error.Find("assigned") >= 0, "scope: an unassigned employee cannot scope a proposal");
	r = CompanyWorkflow::ScopeProposalToProject(alice, pid, s_project_id, "scope-alice");
	Check(!r.ok && r.error.Find("required") >= 0, "scope: a contributor cannot scope a proposal");
	r = CompanyWorkflow::ScopeProposalToProject(erin, pid, s_project_id, "scope-erin");
	ProposalRecord prop;
	Check(r.ok && store.FindProposal(pid, prop) && prop.project_id == s_project_id,
	      "scope: an assigned employee scopes the proposal to the project " + r.error);
	r = CompanyWorkflow::ScopeProposalToProject(admin, pid, s_idle_project_id, "scope-other");
	Check(!r.ok && r.error.Find("already scoped to another project") >= 0,
	      "scope: a proposal scoped to one project cannot move to another");

	r = CompanyWorkflow::AdoptWorkItem(alice, wid, "adopt-alice");
	Check(!r.ok && r.error.Find("employee role") >= 0, "adopt: a contributor cannot adopt work");
	r = CompanyWorkflow::AdoptWorkItem(frank, wid, "adopt-frank");
	Check(!r.ok && r.error.Find("assigned") >= 0, "adopt: an unassigned employee cannot adopt the work");
	String loose = AcceptedItem(store, "adopt-loose");
	r = CompanyWorkflow::AdoptWorkItem(erin, loose, "adopt-loose-erin");
	Check(!r.ok && r.error.Find("not part of a client project") >= 0,
	      "adopt: work outside a client project cannot be adopted");
	r = CompanyWorkflow::AdoptWorkItem(erin, wid, "adopt-erin");
	WorkItemRecord adopted;
	Check(r.ok && store.FindWorkItem(wid, adopted) && adopted.contributor_user_id == erin.user_id &&
	      adopted.state == COMPANY_STATE_IMPLEMENTATION,
	      "adopt: the assigned employee adopts the work item, which stays in implementation " + r.error);
	s_adopted_item = wid;

	// A client may not decide proposals (a reviewer's role).
	CompanyWorkflowResult p2 = CompanyWorkflow::CreateProposal(alice, "Client review target", "x", TEST_REPO, 1, "scope-p2");
	String pid2 = (String)p2.payload.Get("proposal_id", "");
	r = CompanyWorkflow::DecideProposal(carla, pid2, COMPANY_ACTION_ACCEPT, "", "", "scope-client-decide");
	Check(!r.ok, "roles: a client cannot review a proposal");
}

// Delivery: completion needs every work item completed, then the client
// accepts, and the contract closes. The adopted item completes through the
// real review path.
static void RunProjectDeliveryChecks(CompanyStore& store)
{
	WebAuthIdentity admin = HumanOf(store, "admin");
	WebAuthIdentity carla = HumanOf(store, "carla");
	WebAuthIdentity dan = HumanOf(store, "dan");
	WebAuthIdentity erin = HumanOf(store, "erin");
	const String pid = s_project_id;
	const String wid = s_adopted_item;
	ProjectRecord p;
	ContractRecord c;

	CompanyWorkflowResult r = CompanyWorkflow::MarkProjectReady(admin, pid, "dlv-ready-early");
	Check(!r.ok && r.error.Find("expected completing") >= 0, "delivery: a project must complete before it is ready");
	r = CompanyWorkflow::RequestProjectCompletion(admin, pid, "dlv-completing-early");
	Check(!r.ok && r.error.Find("outstanding work items") >= 0, "completion: an open work item blocks completion");

	const String pr_url = "https://github.com/OuluBSD/CompanySkylark/pull/303";
	Check(CompanyWorkflow::BindPullRequest(erin, wid, TEST_REPO, 303, pr_url, "feature/adopted", "dlv-bind").ok,
	      "delivery: the adopting employee binds a pull request");
	Check(CompanyWorkflow::RequestMechanicalReview(erin, wid, "dlv-mech").ok,
	      "delivery: the adopted item enters mechanical review");
	ValueMap ev;
	ev.Set("run_id", "run-dlv-1");
	Check(CompanyWorkflow::RecordMechanicalResult(AutomationIdentity(), wid, true, ev, "dlv-mech-pass").ok,
	      "delivery: the mechanical pass moves the item to operator review");
	Check(CompanyWorkflow::RequestOperatorReview(erin, wid, "dlv-op-req").ok, "delivery: the operator review is requested");
	r = CompanyWorkflow::DecideOperatorReview(admin, wid, COMPANY_OP_ACTION_APPROVE, "", "dlv-op-approve");
	WorkItemRecord item;
	Check(r.ok && store.FindWorkItem(wid, item) && item.state == COMPANY_STATE_COMPLETED,
	      "delivery: the operator approves and the item is completed " + r.error);

	r = CompanyWorkflow::RequestProjectCompletion(admin, pid, "dlv-completing");
	Check(r.ok && store.FindProject(pid, p) && p.status == COMPANY_PROJECT_COMPLETING, "delivery: completion is requested");
	r = CompanyWorkflow::RequestProjectCompletion(admin, pid, "dlv-completing");
	Check(r.ok && r.idempotent_duplicate, "delivery: a replayed completion request is a duplicate no-op");
	r = CompanyWorkflow::MarkProjectReady(carla, pid, "dlv-ready-client");
	Check(!r.ok && r.error.Find("admin role") >= 0, "delivery: a client cannot mark a project ready");
	r = CompanyWorkflow::MarkProjectReady(admin, pid, "dlv-ready");
	Check(r.ok && store.FindProject(pid, p) && p.status == COMPANY_PROJECT_READY, "delivery: admin marks the project ready");
	r = CompanyWorkflow::DeliverProject(dan, pid, "dlv-deliver-dan");
	Check(!r.ok && r.error.Find("only the client") >= 0, "delivery: another client cannot accept the delivery");
	r = CompanyWorkflow::DeliverProject(carla, pid, "dlv-deliver");
	Check(r.ok && store.FindProject(pid, p) && p.status == COMPANY_PROJECT_DELIVERED,
	      "delivery: the contract's client accepts the delivery");
	r = CompanyWorkflow::CloseContract(admin, s_contract_id, true, "dlv-close");
	Check(r.ok && store.FindContract(s_contract_id, c) && c.status == COMPANY_CONTRACT_COMPLETED,
	      "contract: admin closes the delivered contract as completed");

	Check(store.GetLedger().GetCount() == s_ledger_before,
	      "money: contract, project and review writes never touch the contributor ledger");
}

// Platform v2 review workflow: dispatch binds a head SHA and supersedes the
// prior dispatch; evidence is stale, incomplete, or passes the lifecycle.
static void RunPlatformReviewChecks(CompanyStore& store)
{
	WebAuthIdentity alice = HumanOf(store, "alice");
	WebAuthIdentity listener = AutomationIdentity();
	const String wid = MechanicalItem(store, 11);
	Check(!wid.IsEmpty(), "review: a fixture work item waits in mechanical review");
	if(wid.IsEmpty())
		return;
	const String sha_a = TEST_SHA_A;
	const String sha_b = TEST_SHA;

	CompanyWorkflowResult r = PlatformWorkflow::DispatchReview(alice, wid, PLATFORM_REVIEW_MECHANICAL, sha_a, "", "rv-deny");
	Check(!r.ok && r.error.Find("callback:write") >= 0, "review: a human contributor cannot dispatch a review");
	r = PlatformWorkflow::DispatchReview(listener, wid, "security", sha_a, "", "rv-kind");
	Check(!r.ok && r.error.Find("review kind") >= 0, "review: an unknown review kind is refused");
	r = PlatformWorkflow::DispatchReview(listener, wid, PLATFORM_REVIEW_MECHANICAL, "not-a-sha", "", "rv-sha");
	Check(!r.ok && r.error.Find("head_sha") >= 0, "review: a dispatch needs a valid head SHA");

	CompanyWorkflowResult d1 = PlatformWorkflow::DispatchReview(listener, wid, PLATFORM_REVIEW_MECHANICAL, sha_a, "", "rv-dsp-1");
	String dispatch1 = (String)d1.payload.Get("dispatch_id", "");
	PlatformReviewDispatch rd;
	Check(d1.ok && !((String)d1.payload.Get("attempt_id", "")).IsEmpty() &&
	      store.FindReviewDispatch(dispatch1, rd) && rd.status == PLATFORM_DSP_DISPATCHED,
	      "review: a dispatch is created with a pending attempt");
	CompanyWorkflowResult d2 = PlatformWorkflow::DispatchReview(listener, wid, PLATFORM_REVIEW_MECHANICAL, sha_b, "", "rv-dsp-2");
	String dispatch2 = (String)d2.payload.Get("dispatch_id", "");
	PlatformReviewDispatch prior;
	Check(d2.ok && store.FindReviewDispatch(dispatch1, prior) && prior.status == PLATFORM_DSP_SUPERSEDED,
	      "review: a newer dispatch supersedes the prior one");

	r = PlatformWorkflow::RecordReviewEvidence(listener, dispatch1, sha_a, true, "", "", "", "run-x", "mechanical-1", "rv-evd-sup");
	Check(!r.ok && r.error.Find("superseded") >= 0, "review: evidence for a superseded dispatch is refused");

	WorkItemRecord w;
	r = PlatformWorkflow::RecordReviewEvidence(listener, dispatch2, sha_a, true, "", "", "", "run-stale", "mechanical-1", "rv-evd-stale");
	PlatformReviewDispatch sd;
	Check(r.ok && (String)r.payload.Get("outcome", "") == PLATFORM_EVD_STALE && store.FindWorkItem(wid, w) &&
	      w.state == COMPANY_STATE_MECHANICAL,
	      "review: evidence bound to another head SHA is stale and does not advance the item");
	Check(store.FindReviewDispatch(dispatch2, sd) && sd.status == PLATFORM_DSP_STALE, "review: the dispatch is marked stale");

	r = PlatformWorkflow::RecordReviewEvidence(listener, dispatch2, "", true, "", "", "", "run-x", "mechanical-1", "rv-evd-inc");
	Check(r.ok && (String)r.payload.Get("outcome", "") == PLATFORM_EVD_INCOMPLETE && store.FindWorkItem(wid, w) &&
	      w.state == COMPANY_STATE_MECHANICAL,
	      "review: evidence without a head SHA is incomplete and does not advance the item");

	r = PlatformWorkflow::RecordReviewEvidence(listener, dispatch2, sha_b, true, "All mechanical checks passed.",
		"https://example.invalid/run-1", "", "run-1", "mechanical-1", "rv-evd-pass");
	s_evidence_id = (String)r.payload.Get("evidence_id", "");
	Check(r.ok && (String)r.payload.Get("outcome", "") == PLATFORM_EVD_PASSED &&
	      (String)r.payload.Get("work_item_state", "") == COMPANY_STATE_OPERATOR &&
	      store.FindWorkItem(wid, w) && w.state == COMPANY_STATE_OPERATOR,
	      "review: matching passing evidence advances the item to operator review");
	CompanyWorkflowResult replay = PlatformWorkflow::RecordReviewEvidence(listener, dispatch2, sha_b, true, "", "", "",
		"run-1", "mechanical-1", "rv-evd-pass");
	Check(replay.ok && replay.idempotent_duplicate, "review: evidence replay is idempotent");
	r = PlatformWorkflow::DispatchReview(listener, wid, PLATFORM_REVIEW_MECHANICAL, sha_b, "", "rv-late");
	Check(!r.ok && r.error.Find("not reviewable") >= 0, "review: an item past mechanical review cannot be dispatched again");
}

// Artifacts: metadata for an existing owner only.
static void RunPlatformArtifactChecks(CompanyStore& store)
{
	WebAuthIdentity alice = HumanOf(store, "alice");
	WebAuthIdentity listener = AutomationIdentity();
	CompanyWorkflowResult r = PlatformWorkflow::RecordArtifact(alice, "evidence", s_evidence_id, "review_output",
		"https://example.invalid/artifacts/0", "text/plain", 1, "", "", "art-deny");
	Check(!r.ok && r.error.Find("artifact recording requires") >= 0, "artifact: a human contributor cannot record artifacts");
	r = PlatformWorkflow::RecordArtifact(listener, "bogus", s_evidence_id, "review_output", "https://example.invalid/a",
		"text/plain", 1, "", "", "art-type");
	Check(!r.ok && r.error.Find("owner_type") >= 0, "artifact: an unknown owner type is refused");
	r = PlatformWorkflow::RecordArtifact(listener, "evidence", "evd-missing", "review_output", "https://example.invalid/a",
		"text/plain", 1, "", "", "art-missing");
	Check(!r.ok && r.error.Find("owner not found") >= 0, "artifact: an artifact for a missing owner is refused");
	r = PlatformWorkflow::RecordArtifact(listener, "evidence", s_evidence_id, "review_output", "",
		"text/plain", 1, "", "", "art-uri");
	Check(!r.ok && r.error.Find("uri") >= 0, "artifact: an artifact without a uri is refused");
	r = PlatformWorkflow::RecordArtifact(listener, "evidence", s_evidence_id, "review_output", "https://example.invalid/a",
		"text/plain", -1, "", "", "art-size");
	Check(!r.ok && r.error.Find("non-negative") >= 0, "artifact: a negative size is refused");

	r = PlatformWorkflow::RecordArtifact(listener, "evidence", s_evidence_id, "review_output",
		"https://example.invalid/artifacts/1", "text/plain", 256, "", "standard", "art-1");
	Check(r.ok && store.ArtifactsForOwner("evidence", s_evidence_id).GetCount() == 1,
	      "artifact: the metadata is recorded for the evidence owner");
	r = PlatformWorkflow::RecordArtifact(listener, "evidence", s_evidence_id, "review_output",
		"https://example.invalid/artifacts/1", "text/plain", 256, "", "standard", "art-1");
	Check(r.ok && r.idempotent_duplicate && store.ArtifactsForOwner("evidence", s_evidence_id).GetCount() == 1,
	      "artifact: a replayed artifact key is idempotent");
}

// Trials: a completeness gate over splits x trials_per_split, and stale results
// that never count toward it.
static void RunPlatformTrialChecks(CompanyStore& store)
{
	WebAuthIdentity alice = HumanOf(store, "alice");
	WebAuthIdentity admin = HumanOf(store, "admin");
	WebAuthIdentity listener = AutomationIdentity();
	const String sha_a = TEST_SHA_A;
	const String sha_b = TEST_SHA;
	Vector<String> validation = Scopes("validation");
	Vector<String> test = Scopes("test");

	CompanyWorkflowResult r = PlatformWorkflow::DispatchTrial(alice, PLATFORM_TRIAL_BASELINE, "", TEST_REPO, 11, sha_b,
		validation, 3, "trl-deny");
	Check(!r.ok && r.error.Find("trial dispatch requires") >= 0, "trial: a human contributor cannot dispatch a trial");
	r = PlatformWorkflow::DispatchTrial(listener, PLATFORM_TRIAL_BASELINE, "", TEST_REPO, 11, sha_b, validation, 3, "trl-cb");
	Check(!r.ok && r.error.Find("trials:write") >= 0, "trial: a callback-only listener cannot dispatch a trial");
	r = PlatformWorkflow::DispatchTrial(admin, "custom", "", TEST_REPO, 11, sha_b, validation, 3, "trl-kind");
	Check(!r.ok && r.error.Find("trial kind") >= 0, "trial: an unknown trial kind is refused");
	r = PlatformWorkflow::DispatchTrial(admin, PLATFORM_TRIAL_BASELINE, "", TEST_REPO, 11, sha_b, validation, 0, "trl-zero");
	Check(!r.ok && r.error.Find("trials_per_split") >= 0, "trial: trials_per_split must be at least one");

	r = PlatformWorkflow::DispatchTrial(admin, PLATFORM_TRIAL_BASELINE, "", TEST_REPO, 11, sha_b, validation, 3, "trl-1");
	String trial_id = (String)r.payload.Get("trial_id", "");
	Check(r.ok && (int)r.payload.Get("expected_results", 0) == 3,
	      "trial: a baseline over one split and three runs expects three results");
	r = PlatformWorkflow::RecordTrialResult(admin, trial_id, "test", 1, true, 0, "", sha_b, "job-x", "trl-bad-split");
	Check(!r.ok && r.error.Find("split is not part") >= 0, "trial: a result for an unknown split is refused");
	r = PlatformWorkflow::RecordTrialResult(admin, trial_id, "validation", 4, true, 0, "", sha_b, "job-x", "trl-bad-index");
	Check(!r.ok && r.error.Find("trial_index") >= 0, "trial: a result index beyond trials_per_split is refused");

	PlatformWorkflow::RecordTrialResult(admin, trial_id, "validation", 1, true, 1, "ok", sha_b, "job-1", "trl-res-1");
	PlatformWorkflow::RecordTrialResult(admin, trial_id, "validation", 2, true, 1, "ok", sha_b, "job-2", "trl-res-2");
	PlatformTrialRecord collecting;
	Check(store.FindPlatformTrial(trial_id, collecting) && collecting.status == PLATFORM_TRIAL_COLLECTING &&
	      collecting.received_results == 2,
	      "trial: the trial is collecting at two of three results");
	CompanyWorkflowResult last = PlatformWorkflow::RecordTrialResult(admin, trial_id, "validation", 3, true, 1, "ok",
		sha_b, "job-3", "trl-res-3");
	Check(last.ok && (String)last.payload.Get("trial_status", "") == PLATFORM_TRIAL_COMPLETE,
	      "trial: the third result completes the trial (completeness gate)");
	CompanyWorkflowResult dup = PlatformWorkflow::RecordTrialResult(admin, trial_id, "validation", 3, true, 1, "ok",
		sha_b, "job-3", "trl-res-3b");
	Check(dup.ok && dup.idempotent_duplicate, "trial: a duplicate (split, index) result is idempotent");

	// A result bound to another head SHA is stale and never counts; the slot can
	// later be filled with corrected evidence.
	CompanyWorkflowResult t2 = PlatformWorkflow::DispatchTrial(admin, PLATFORM_TRIAL_REFERENCE, "", TEST_REPO, 0, sha_b,
		test, 1, "trl-2");
	String trial2 = (String)t2.payload.Get("trial_id", "");
	CompanyWorkflowResult stale = PlatformWorkflow::RecordTrialResult(admin, trial2, "test", 1, true, 1, "ok", sha_a,
		"job-9", "trl-stale");
	PlatformTrialRecord t2rec;
	Check(stale.ok && (String)stale.payload.Get("outcome", "") == PLATFORM_TRES_STALE &&
	      store.FindPlatformTrial(trial2, t2rec) && t2rec.received_results == 0 && t2rec.status == PLATFORM_TRIAL_DISPATCHED,
	      "trial: a result bound to another head SHA is stale and does not count");
	CompanyWorkflowResult retry = PlatformWorkflow::RecordTrialResult(admin, trial2, "test", 1, true, 1, "ok", sha_b,
		"job-10", "trl-retry");
	Check(retry.ok && (String)retry.payload.Get("outcome", "") == PLATFORM_TRES_RECORDED &&
	      (String)retry.payload.Get("trial_status", "") == PLATFORM_TRIAL_COMPLETE,
	      "trial: a stale slot can be filled with corrected evidence, which completes the trial");
}

// The status snapshot counts the v2 records written above.
static void RunPlatformSnapshotCheck(CompanyStore& store)
{
	ValueMap snap = PlatformWorkflow::StatusSnapshot();
	Check((int)snap.Get("commands_total", 0) >= 4 && (int)snap.Get("dispatches_total", 0) >= 2 &&
	      (int)snap.Get("evidence_total", 0) >= 3 && (int)snap.Get("trials_total", 0) >= 2 &&
	      (int)snap.Get("artifacts_total", 0) >= 1 && snap.Find("commands_by_status") >= 0,
	      "platform: the status snapshot counts the recorded v2 records");
	(void)store;
}

// Fixed mechanical-review items for the review callback and platform HTTP checks.
static void PrepareHttpWorkItems(CompanyStore& store)
{
	s_pass_item = MechanicalItem(store, 202);
	s_fail_item = MechanicalItem(store, 203);
	s_http_item = MechanicalItem(store, 404);
	Check(!s_pass_item.IsEmpty() && !s_fail_item.IsEmpty() && !s_http_item.IsEmpty(),
	      "setup: the HTTP checks have work items waiting in mechanical review");
}

// HTTP: routes, auth, roles, mutations, logout. Runs on the live engine.
static void RunHttpChecks()
{
	Reply h = Call("GET", "/api/health");
	Check(h.status == 200, "http: /api/health answers 200");
	Reply anon = Call("GET", "/api/proposals");
	Check(anon.status == 401, "http: proposals refuse an anonymous caller");
	Reply nope = Call("GET", "/api/no-such-route");
	Check(nope.status == 404, "http: an unknown API route is 404");

	Reply wrong = Call("POST", "/api/auth/login", String(), LoginJson("root", "pw-root-first"));
	Check(wrong.status == 401, "http: the replaced password no longer signs in");
	Reply login = Call("POST", "/api/auth/login", String(), LoginJson("root", "pw-root-second"));
	Check(login.status == 200, "http: a provisioned admin signs in over api/auth/login");
	String root_token = JsonField(login.body, "token");
	Check(!root_token.IsEmpty(), "http: the login returns a bearer token");

	Reply users = Call("GET", "/api/users", root_token);
	Check(users.status == 200 && users.body.Find("alice") >= 0, "http: an admin lists users");
	Reply platform = Call("GET", "/api/v2/status", root_token);
	Check(platform.status == 200, "http: an admin reads the platform status");

	Reply alice_login = Call("POST", "/api/auth/login", String(), LoginJson("alice", "pw-alice-1"));
	Check(alice_login.status == 200, "http: a contributor signs in");
	String alice_token = JsonField(alice_login.body, "token");
	Reply alice_users = Call("GET", "/api/users", alice_token);
	Check(alice_users.status >= 400 && alice_users.status < 500, "http: a contributor cannot list users");

	ValueMap create;
	create.Set("title", "HTTP proposal");
	create.Set("description", "Created over HTTP");
	create.Set("repository", TEST_REPO);
	create.Set("estimated_hours", 3.0);
	create.Set("idempotency_key", "http-create-1");
	Reply made = Call("POST", "/api/proposals/create", alice_token, AsJSON(create));
	String http_pid = JsonField(made.body, "proposal_id");
	Check((made.status == 201 || made.status == 200) && http_pid.StartsWith("prp_"),
	      "http: a contributor creates a proposal");

	create.Set("repository", "evil/repo");
	create.Set("idempotency_key", "http-create-bad");
	Reply bad = Call("POST", "/api/proposals/create", alice_token, AsJSON(create));
	Check(bad.status == 400, "http: a proposal for a repository off the allowlist is 400");

	Reply got = Call("GET", "/api/proposals/" + http_pid, alice_token);
	Check(got.status == 200 && got.body.Find("HTTP proposal") >= 0, "http: the proposal detail is served");

	ValueMap decide;
	decide.Set("proposal_id", http_pid);
	decide.Set("action", COMPANY_ACTION_ACCEPT);
	decide.Set("reason", "");
	decide.Set("idempotency_key", "http-accept-alice");
	Reply self_decide = Call("POST", "/api/proposals/decide", alice_token, AsJSON(decide));
	Check(self_decide.status >= 400 && self_decide.status < 500, "http: a contributor cannot accept a proposal");

	Reply bob_login = Call("POST", "/api/auth/login", String(), LoginJson("bob", "pw-bob-1"));
	String bob_token = JsonField(bob_login.body, "token");
	decide.Set("idempotency_key", "http-accept-bob");
	Reply accepted = Call("POST", "/api/proposals/decide", bob_token, AsJSON(decide));
	Check(accepted.status == 201, "http: a reviewer accepts the proposal over HTTP (applied)");
	Reply accepted_view = Call("GET", "/api/proposals/" + http_pid, alice_token);
	Check(accepted_view.status == 200 && accepted_view.body.Find("accepted") >= 0,
	      "http: the accepted proposal reads back as accepted");

	Reply logout = Call("POST", "/api/auth/logout", root_token);
	Check(logout.status == 200, "http: logout answers 200");
	Reply after = Call("GET", "/api/proposals", root_token);
	Check(after.status == 401, "http: a logged-out token is refused");

	Reply login_page = Call("GET", "/login");
	Check((login_page.status == 302 || login_page.status == 303) && login_page.location.Find("session/login") >= 0,
	      "http: /login redirects to the engine sign-in page");
	Reply manifest = Call("GET", "/api/cli/manifest");
	Check(manifest.status == 200 && manifest.body.Find("skylark-command-manifest/1") >= 0,
	      "http: the CLI manifest keeps the command manifest schema");
	Reply spec = Call("GET", "/api/framework/spec");
	Check(spec.status == 200, "http: the framework spec is served");
}

// Agent tokens over HTTP: the fixture tokens, scopes, roles, issue, revocation
// and expiry. Human sessions come from the WebAccounts login (bearer).
static void RunAgentTokenHttpChecks(CompanyStore& store)
{
	const String fixture_agent = "company-test-agent-token-0001";
	ValueMap proposal;
	proposal.Set("title", "Agent proposal over HTTP");
	proposal.Set("description", "Created with the fixture agent token");
	proposal.Set("repository", TEST_REPO);
	proposal.Set("estimated_hours", 2.0);
	proposal.Set("idempotency_key", "http-agent-1");
	Reply made = Call("POST", "/api/proposals/create", fixture_agent, AsJSON(proposal));
	Check(made.status == 201 && JsonField(made.body, "proposal_id").StartsWith("prp_"),
	      "agent http: a proposals:write token creates a proposal (201)");
	Reply again = Call("POST", "/api/proposals/create", fixture_agent, AsJSON(proposal));
	Check(again.status == 200 && again.body.Find("duplicate") >= 0, "agent http: a replayed create key answers as a duplicate");
	Check(Call("GET", "/api/work", fixture_agent).status == 200, "agent http: a work:read token reads /api/work");
	Reply balance = Call("GET", "/api/ledger/balance", fixture_agent);
	Check(balance.status == 200, "agent http: a ledger:read token reads a balance");
	Check(Call("GET", "/api/users", fixture_agent).status == 403,
	      "agent http: a token without the admin role cannot list users (403)");
	ValueMap decide;
	decide.Set("work_item_id", s_http_item);
	decide.Set("action", COMPANY_OP_ACTION_APPROVE);
	decide.Set("reason", "self");
	Check(Call("POST", "/api/reviews/operator/decide", fixture_agent, AsJSON(decide)).status == 403,
	      "agent http: an agent token cannot make operator decisions (no reviews:write)");
	Check(Call("POST", "/api/webhook/review-callback", fixture_agent, "{}").status == 401,
	      "agent http: an agent token cannot post review callbacks (401)");
	Check(Call("GET", "/api/v2/status", fixture_agent).status == 403,
	      "agent http: an agent token cannot read the admin platform status (403)");

	// Fixture tokens that must fail closed.
	Check(Call("GET", "/api/proposals", "company-test-expired-token-0001").status == 401,
	      "agent http: the fixture expired token fails closed (401)");
	Check(Call("GET", "/api/work", "company-test-revoked-token-0001").status == 401,
	      "agent http: the fixture revoked token fails closed (401)");
	Check(Call("GET", "/api/work", s_expired_token).status == 401,
	      "agent http: a token issued with a past expiry fails closed (401)");
	Check(Call("GET", "/api/work", s_gone_token).status == 401,
	      "agent http: a token revoked through the store fails closed (401)");
	Check(Call("GET", "/api/work", "waat_not-a-real-token").status == 401,
	      "agent http: an unknown bearer token is refused (401)");

	// Issued token: its own scopes only.
	Check(Call("GET", "/api/work", s_read_token).status == 200, "agent http: a freshly issued work:read token authenticates");
	Check(Call("GET", "/api/proposals", s_read_token).status == 403,
	      "agent http: a token lacks the scopes it was not granted (403)");
	Check(Call("GET", "/api/ledger/balance", s_read_token).status == 403,
	      "agent http: a work:read token cannot read the ledger balance (403)");

	// An admin session issues, revokes and sees the token state.
	String admin_token = LoginToken("root", "pw-root-second");
	Check(!admin_token.IsEmpty(), "agent http: the admin signs in and receives a bearer session");
	ValueArray scopes;
	scopes.Add(String(COMPANY_SCOPE_WORK_READ));
	ValueMap create;
	create.Set("name", "http-reader");
	create.Set("scopes", scopes);
	create.Set("idempotency_key", "http-tok-1");
	Reply issued = Call("POST", "/api/tokens/create", admin_token, AsJSON(create));
	String http_raw = JsonField(issued.body, "raw_token");
	String http_id = JsonField(issued.body, "token_id");
	Check(issued.status == 201 && http_raw.StartsWith(COMPANY_AGENT_TOKEN_PREFIX),
	      "agent http: an admin issues a token and receives the raw value (201)");
	Reply replay = Call("POST", "/api/tokens/create", admin_token, AsJSON(create));
	Check(replay.status == 200 && JsonField(replay.body, "raw_token").IsEmpty(),
	      "agent http: a replayed issue key returns no second raw token");
	Check(Call("GET", "/api/work", http_raw).status == 200, "agent http: the issued token reads /api/work");
	Check(Call("GET", "/api/ledger", http_raw).status == 403, "agent http: an issued token without ledger:read is refused (403)");

	ValueMap revoke;
	revoke.Set("token_id", http_id);
	revoke.Set("reason", "rotated");
	revoke.Set("idempotency_key", "http-rev-1");
	Reply revoked = Call("POST", "/api/tokens/revoke", admin_token, AsJSON(revoke));
	Check(revoked.status == 200 && revoked.body.Find("revoked") >= 0, "agent http: an admin revokes the issued token (200)");
	Check(Call("GET", "/api/work", http_raw).status == 401, "agent http: a revoked token no longer authenticates (401)");
	revoke.Set("idempotency_key", "http-rev-2");
	Reply again_revoked = Call("POST", "/api/tokens/revoke", admin_token, AsJSON(revoke));
	Check(again_revoked.status == 200 && again_revoked.body.Find("duplicate") >= 0,
	      "agent http: revoking an already revoked token is a duplicate no-op");

	// Human sessions: role scopes, no token powers.
	String alice_token = LoginToken("alice", "pw-alice-1");
	Reply alice_balance = Call("GET", "/api/ledger/balance", alice_token);
	Check(alice_balance.status == 200 && JsonField(alice_balance.body, "user_id") == HumanOf(store, "alice").user_id,
	      "agent http: a contributor session reads its own ledger balance");
	Check(Call("GET", "/api/users", alice_token).status == 403, "agent http: a contributor session cannot list users (403)");
	ValueMap alice_create;
	alice_create.Set("name", "alice-token");
	alice_create.Set("scopes", scopes);
	alice_create.Set("idempotency_key", "http-alice-tok");
	Check(Call("POST", "/api/tokens/create", alice_token, AsJSON(alice_create)).status == 403,
	      "agent http: a contributor session cannot create agent tokens (403)");
}

// Client contracts and projects over HTTP: visibility by role and membership,
// creation rules, and the manifest entries for the new routes.
static void RunContractHttpChecks()
{
	String carla = LoginToken("carla", "pw-carla-1");
	String dan = LoginToken("dan", "pw-dan-1");
	String erin = LoginToken("erin", "pw-erin-1");
	String frank = LoginToken("frank", "pw-frank-1");
	String admin = LoginToken("root", "pw-root-second");
	Check(!carla.IsEmpty() && !dan.IsEmpty() && !erin.IsEmpty() && !frank.IsEmpty(),
	      "contract http: the client and employee accounts sign in");

	Check(Call("GET", "/api/contracts").status == 401, "contract http: the contract list refuses an anonymous caller (401)");
	ValueMap body;
	body.Set("title", "HTTP contract");
	body.Set("brief", "Created over HTTP");
	body.Set("amount", 1500.0);
	body.Set("currency", "EUR");
	body.Set("payment_terms", "net 14");
	body.Set("practical_details", "remote");
	body.Set("idempotency_key", "http-ctr-1");
	Reply created = Call("POST", "/api/contracts/create", carla, AsJSON(body));
	String http_cid = JsonField(created.body, "contract_id");
	Check(created.status == 201 && http_cid.StartsWith("ctr_"), "contract http: a client creates a contract (201)");
	Check(Call("POST", "/api/contracts/create", admin, AsJSON(body)).status == 403,
	      "contract http: an admin session cannot create a client contract (403)");
	body.Set("idempotency_key", "");
	Check(Call("POST", "/api/contracts/create", carla, AsJSON(body)).status == 400,
	      "contract http: a create without an idempotency key is refused (400)");

	Reply mine = Call("GET", "/api/contracts", carla);
	Check(mine.status == 200 && mine.body.Find(http_cid) >= 0, "contract http: the client lists its own contract");
	Reply theirs = Call("GET", "/api/contracts", dan);
	Check(theirs.status == 200 && theirs.body.Find(http_cid) < 0, "contract http: another client does not see the contract");
	Check(Call("GET", "/api/contracts/" + http_cid, dan).status == 404,
	      "contract http: another client gets 404 for the contract detail");
	Check(Call("GET", "/api/contracts/" + http_cid, carla).status == 200, "contract http: the owner reads the contract detail");

	ValueMap close;
	close.Set("contract_id", s_contract_id);
	close.Set("idempotency_key", "http-close-dan");
	Check(Call("POST", "/api/contracts/complete", dan, AsJSON(close)).status == 403,
	      "contract http: a client cannot complete a contract (403)");

	Check(Call("GET", "/api/projects").status == 401, "contract http: the project list refuses an anonymous caller (401)");
	Check(Call("GET", "/api/projects", carla).body.Find(s_project_id) >= 0,
	      "contract http: the client sees the project of its contract");
	Check(Call("GET", "/api/projects", erin).body.Find(s_project_id) >= 0,
	      "contract http: the assigned employee sees the project");
	Check(Call("GET", "/api/projects/" + s_project_id, frank).status == 404,
	      "contract http: an unassigned employee gets 404 for the project");
	ValueMap project;
	project.Set("contract_id", s_contract_id);
	project.Set("repo_name", TEST_REPO);
	project.Set("repo_origin", COMPANY_REPO_CREATED);
	project.Set("idempotency_key", "http-prj-client");
	Check(Call("POST", "/api/projects/create", carla, AsJSON(project)).status == 403,
	      "contract http: a client cannot create a project (403)");

	Reply manifest = Call("GET", "/api/cli/manifest");
	Check(manifest.status == 200 && manifest.body.Find("contract-create") >= 0 &&
	      manifest.body.Find("project-accept-pr") >= 0 && manifest.body.Find("work-adopt") >= 0,
	      "contract http: the manifest lists the contract, project and adoption routes");
}

// The review callback over HTTP: authentication, repository and PR binding,
// idempotent replay, and the mechanical-review state rule.
static String CallbackJson(const String& work_item_id, const char *repo, int pr, const char *event, bool passed,
                           const char *run_id)
{
	ValueMap m;
	m.Set("work_item_id", work_item_id);
	m.Set("repository", repo);
	m.Set("pr_number", pr);
	m.Set("event_id", event);
	m.Set("passed", passed);
	m.Set("run_id", run_id);
	m.Set("findings", "fixture findings");
	return AsJSON(m);
}

static void RunReviewCallbackHttpChecks(CompanyStore& store)
{
	const String callback_token = "company-test-callback-token-0001";
	const String agent_token = "company-test-agent-token-0001";
	const char *callback = "/api/webhook/review-callback";

	Check(Call("POST", callback, String(), CallbackJson(s_pass_item, TEST_REPO, 202, "cb-anon", true, "run-1")).status == 401,
	      "callback: an anonymous caller cannot post a review result (401)");
	Check(Call("POST", callback, agent_token, CallbackJson(s_pass_item, TEST_REPO, 202, "cb-agent", true, "run-1")).status == 401,
	      "callback: a plain agent token cannot post a review result (401)");
	Check(Call("POST", callback, callback_token, CallbackJson(s_pass_item, "evil/repo", 202, "cb-evil", true, "run-1")).status == 403,
	      "callback: a repository off the allowlist is refused (403)");
	Check(Call("POST", callback, callback_token, CallbackJson(s_pass_item, TEST_REPO, 999, "cb-wrong-pr", true, "run-1")).status == 403,
	      "callback: a PR that is not the work item's PR is refused (403)");
	bool rejection_audited = false;
	for(const AuditEvent& e : store.GetAudit())
		if(e.action == COMPANY_AUD_CALLBACK_REJECTED)
			rejection_audited = true;
	Check(rejection_audited, "callback: a rejected callback is written to the audit trail");

	ValueMap no_event;
	no_event.Set("work_item_id", s_pass_item);
	no_event.Set("repository", TEST_REPO);
	no_event.Set("pr_number", 202);
	no_event.Set("passed", true);
	no_event.Set("run_id", "run-1");
	Check(Call("POST", callback, callback_token, AsJSON(no_event)).status == 400,
	      "callback: a callback without an event id is refused (400)");
	Check(Call("POST", callback, callback_token, CallbackJson("wrk_missing", TEST_REPO, 202, "cb-missing", true, "run-1")).status == 404,
	      "callback: an unknown work item is 404");

	Reply pass = Call("POST", callback, callback_token, CallbackJson(s_pass_item, TEST_REPO, 202, "cb-pass-1", true, "run-1"));
	Check(pass.status == 200 && pass.body.Find("recorded") >= 0 && pass.body.Find(COMPANY_STATE_OPERATOR) >= 0,
	      "callback: a passing result is recorded and moves the item to operator review (200)");
	Reply dup = Call("POST", callback, callback_token, CallbackJson(s_pass_item, TEST_REPO, 202, "cb-pass-1", true, "run-1"));
	Check(dup.status == 200 && dup.body.Find("duplicate") >= 0, "callback: a duplicate event is idempotent");
	Reply late = Call("POST", callback, callback_token, CallbackJson(s_pass_item, TEST_REPO, 202, "cb-pass-2", true, "run-1"));
	Check(late.status == 409 && late.body.Find("mechanical_review") >= 0,
	      "callback: a result for an item past mechanical review is refused (409)");

	Reply fail = Call("POST", callback, callback_token, CallbackJson(s_fail_item, TEST_REPO, 203, "cb-fail-1", false, "run-2"));
	Check(fail.status == 200 && fail.body.Find(COMPANY_STATE_IMPLEMENTATION) >= 0,
	      "callback: a failing result returns the item to implementation (200)");

	WorkItemRecord w;
	Check(store.FindWorkItem(s_pass_item, w) && w.state == COMPANY_STATE_OPERATOR,
	      "callback: the stored passing item is in operator review");
	Check(store.FindWorkItem(s_fail_item, w) && w.state == COMPANY_STATE_IMPLEMENTATION,
	      "callback: the stored failing item is back in implementation");

	String admin_token = LoginToken("root", "pw-root-second");
	Reply reviews = Call("GET", "/api/reviews", admin_token);
	Check(reviews.status == 200 && reviews.body.Find(s_pass_item) >= 0, "callback: the review record is listed for the item");
}

// Platform v2 over HTTP: command ingest and refusals, dispatch and evidence on
// a fresh item, artifacts, trials, and the admin-only reads.
static void RunPlatformHttpChecks(CompanyStore& store)
{
	const String callback_token = "company-test-callback-token-0001";
	const String trial_token = "company-test-trial-token-0001";
	const String agent_token = "company-test-agent-token-0001";
	const String sha = TEST_SHA;
	String admin_token = LoginToken("root", "pw-root-second");
	String alice_token = LoginToken("alice", "pw-alice-1");

	Check(Call("POST", "/api/v2/commands/ingest", String(), "{}").status == 401,
	      "platform http: an anonymous command ingest is refused (401)");
	Check(Call("POST", "/api/v2/commands/ingest", agent_token, "{}").status == 401,
	      "platform http: a plain agent token cannot ingest commands (401)");

	ValueMap cmd;
	cmd.Set("source", "github_comment");
	cmd.Set("repository", TEST_REPO);
	cmd.Set("pr_number", 11);
	cmd.Set("actor_login", "alice");
	cmd.Set("command", "/review");
	cmd.Set("head_sha", sha);
	cmd.Set("idempotency_key", "http-pt-1");
	Reply ingested = Call("POST", "/api/v2/commands/ingest", callback_token, AsJSON(cmd));
	Check(ingested.status == 201 && ingested.body.Find("ingested") >= 0 && ingested.body.Find("review") >= 0,
	      "platform http: a callback token ingests a /review command (201)");
	Check(Call("POST", "/api/v2/commands/ingest", callback_token, AsJSON(cmd)).status == 200,
	      "platform http: a replayed command is a duplicate (200)");
	cmd.Set("command", String("/run reference ") + sha);
	cmd.Set("idempotency_key", "http-pt-2");
	Reply refused = Call("POST", "/api/v2/commands/ingest", callback_token, AsJSON(cmd));
	Check(refused.status == 201 && refused.body.Find("refused") >= 0,
	      "platform http: a run command from a non-admin login is refused (201, status refused)");

	// Dispatch and evidence over HTTP on a mechanical item.
	ValueMap dsp;
	dsp.Set("work_item_id", s_http_item);
	dsp.Set("kind", PLATFORM_REVIEW_MECHANICAL);
	dsp.Set("head_sha", sha);
	dsp.Set("base_sha", "");
	dsp.Set("idempotency_key", "http-dsp-1");
	Reply dispatched = Call("POST", "/api/v2/reviews/dispatch", callback_token, AsJSON(dsp));
	String dispatch_id = JsonField(dispatched.body, "dispatch_id");
	Check(dispatched.status == 201 && dispatch_id.StartsWith("dsp_"), "platform http: a review dispatch is created (201)");
	Reply listed = Call("GET", "/api/v2/reviews?work_item_id=" + s_http_item, admin_token);
	Check(listed.status == 200 && listed.body.Find(dispatch_id) >= 0, "platform http: the dispatch is listed for its work item");

	ValueMap ev;
	ev.Set("dispatch_id", dispatch_id);
	ev.Set("head_sha", TEST_SHA_A);
	ev.Set("passed", true);
	ev.Set("findings", "wrong head");
	ev.Set("run_id", "run-http-stale");
	ev.Set("idempotency_key", "http-evd-stale");
	Reply stale = Call("POST", "/api/v2/reviews/evidence", callback_token, AsJSON(ev));
	Check(stale.status == 201 && stale.body.Find("stale") >= 0, "platform http: evidence with a wrong head SHA is stale (201)");
	ev.Set("head_sha", sha);
	ev.Set("run_id", "run-http-1");
	ev.Set("idempotency_key", "http-evd-pass");
	Reply passed = Call("POST", "/api/v2/reviews/evidence", callback_token, AsJSON(ev));
	String evidence_id = JsonField(passed.body, "evidence_id");
	Check(passed.status == 201 && passed.body.Find("passed") >= 0 && evidence_id.StartsWith("evd_"),
	      "platform http: evidence bound to the head SHA passes (201)");
	WorkItemRecord w;
	Check(store.FindWorkItem(s_http_item, w) && w.state == COMPANY_STATE_OPERATOR,
	      "platform http: the passing evidence moves the work item to operator review");

	// Artifacts: callback or trial scope (automation) or admin; the agent is refused.
	ValueMap art;
	art.Set("owner_type", "evidence");
	art.Set("owner_id", evidence_id);
	art.Set("kind", "review_output");
	art.Set("uri", "https://example.invalid/artifacts/http-1");
	art.Set("content_type", "text/plain");
	art.Set("size", 128);
	art.Set("sha256", "");
	art.Set("retention", "standard");
	art.Set("idempotency_key", "http-art-1");
	Check(Call("POST", "/api/v2/artifacts/record", agent_token, AsJSON(art)).status == 401,
	      "platform http: a plain agent token cannot record an artifact (401)");
	Reply recorded = Call("POST", "/api/v2/artifacts/record", callback_token, AsJSON(art));
	String artifact_id = JsonField(recorded.body, "artifact_id");
	Check(recorded.status == 201 && artifact_id.StartsWith("art_"), "platform http: a callback token records an artifact (201)");
	Reply artifacts = Call("GET", "/api/v2/artifacts?owner_id=" + evidence_id, admin_token);
	Check(artifacts.status == 200 && artifacts.body.Find(artifact_id) >= 0,
	      "platform http: the artifact is listed for its owner");

	// Trials: trials:write (automation) or admin. A callback token is not a trial service.
	ValueMap trial;
	trial.Set("kind", PLATFORM_TRIAL_REFERENCE);
	trial.Set("work_item_id", "");
	trial.Set("repository", TEST_REPO);
	trial.Set("pr_number", 0);
	trial.Set("head_sha", sha);
	ValueArray splits;
	splits.Add(String("test"));
	trial.Set("splits", splits);
	trial.Set("trials_per_split", 1);
	trial.Set("idempotency_key", "http-pt-trial-1");
	Check(Call("POST", "/api/v2/trials/dispatch", callback_token, AsJSON(trial)).status == 401,
	      "platform http: a callback token cannot dispatch trials (401)");
	Reply dispatched_trial = Call("POST", "/api/v2/trials/dispatch", trial_token, AsJSON(trial));
	Check(dispatched_trial.status == 201 && JsonInt(dispatched_trial.body, "expected_results") == 1,
	      "platform http: a trials:write token dispatches a trial with one expected result (201)");

	// Admin-only reads.
	Reply status = Call("GET", "/api/v2/status", admin_token);
	Check(status.status == 200 && status.body.Find("commands_total") >= 0, "platform http: an admin reads the platform status");
	Check(Call("GET", "/api/v2/status", alice_token).status == 403, "platform http: a contributor cannot read the platform status (403)");
	Check(Call("GET", "/api/v2/commands", alice_token).status == 403, "platform http: a contributor cannot list commands (403)");
	Check(Call("GET", "/api/v2/trials", admin_token).status == 200, "platform http: an admin lists the trials");
}

// ---------------------------------------------------------------------------

static void RunCompanySiteChecks(const String& root)
{
	CompanyComposedModule module;
	String error;
	Vector<String> repos;
	repos.Add(TEST_REPO);
	Check(module.Open(AppendFileName(root, "company"), repos, error), "open: the composed Company module opens " + error);
	CompanyStore& store = module.Store();
	RunStoreChecks(store);
	RunStoreProbeCheck(root);

	String site_root = AppendFileName(root, "site");
	RealizeDirectory(site_root);
	EngineSite& site = TheWebEngine().AddSite("company", "Company",
		Format("http://127.0.0.1:%d", TEST_PORT), site_root);
	InstallCompanySite(site, store);
	InstallSessionRoutes(site);

	RunProvisionChecks(store, site);
	RunBrokerChecks(module, store);
	RunLedgerChecks(module, store);
	RunWorkflowChecks(store);
	RunPlatformChecks(store);
	RunAgentTokenChecks(store);
	RunContractChecks(store);
	RunProjectChecks(store);
	RunProposalScopeChecks(store);
	RunProjectDeliveryChecks(store);
	RunPlatformReviewChecks(store);
	RunPlatformArtifactChecks(store);
	RunPlatformTrialChecks(store);
	RunPlatformSnapshotCheck(store);
	PrepareHttpWorkItems(store);

	CompanyTestHost host;
	s_host = &host;
	// The stock listener (TEST_PORT) serves the company site; BindSite would
	// need a port other than the stock one, so only the default binding is used.
	host.BindDefaultSite("company");
	Thread host_thread;
	host_thread.Run(HostMain);
	Check(WaitReady(), "http: the company site answers on the stock listener");
	RunHttpChecks();
	RunAgentTokenHttpChecks(store);
	RunContractHttpChecks();
	RunReviewCallbackHttpChecks(store);
	RunPlatformHttpChecks(store);
	host.StopEngines();
	host.Quit();
	host_thread.Wait();
}

END_UPP_NAMESPACE

CONSOLE_APP_MAIN
{
	using namespace Upp;
	setvbuf(stdout, NULL, _IONBF, 0);
	Cout() << "CompanySiteTests: Company site self-test\n";
	String root = AppendFileName(GetTempDirectory(), "company-site-test-" + CompanyGenerateId("t"));
	RealizeDirectory(root);
	RunCompanySiteChecks(root);
	DeleteFolderDeep(root);
	Cout() << "\nCompanySiteTests: " << (s_checks - s_failed) << "/" << s_checks << " checks passed\n";
	_Exit(s_failed ? 1 : 0);
}
