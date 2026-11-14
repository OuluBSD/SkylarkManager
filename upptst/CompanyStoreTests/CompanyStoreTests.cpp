#include <Company/CompanyStore.h>

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

static UserRecord MakeUser(const String& id, const String& name)
{
	UserRecord u;
	u.id = id;
	u.username = name;
	u.display_name = name;
	u.roles.Add(COMPANY_ROLE_CONTRIBUTOR);
	u.created_at = GetSysTime();
	u.updated_at = u.created_at;
	return u;
}

static void RunStoreChecks(const String& root)
{
	CompanyStore store;
	store.SetRoot(root);
	String error;
	Check(store.Initialize(error), "store initializes on an empty root: " + error);
	Check(store.GetSecret().GetCount() >= 32, "server secret is created");
	Check(store.GetConfig().schema_version == COMPANY_SCHEMA_VERSION, "default config is written");

	// Reopening the same root keeps the secret and the config.
	CompanyStore reopened;
	reopened.SetRoot(root);
	Check(reopened.Initialize(error) && reopened.GetSecret() == store.GetSecret(),
	      "a second start keeps the same secret");

	// Users: create, find, username index, uniqueness, rename.
	Check(store.SaveUser(MakeUser("usr-1", "alice"), error), "user is created");
	UserRecord found;
	Check(store.FindUserByUsername("ALICE", found) && found.id == "usr-1",
	      "username lookup is case-insensitive through the index");
	Check(!store.SaveUser(MakeUser("usr-2", "Alice"), error) && error.Find("already exists") >= 0,
	      "a second user with the same username is refused");
	Check(store.SaveUser(MakeUser("usr-1", "alice2"), error), "a user can be renamed");
	Check(!store.FindUserByUsername("alice", found), "the old username index is removed on rename");
	Check(store.FindUserByUsername("alice2", found) && found.id == "usr-1", "the new username resolves");
	Check(store.SaveUser(MakeUser("usr-2", "alice"), error), "the freed username can be taken");
	Check(store.GetUsers().GetCount() == 2, "listing shows users and skips index records");

	// Tokens: hash lookup through the index, hash change moves the index.
	AgentTokenRecord tok;
	tok.id = "tok-1";
	tok.name = "bot";
	tok.owner_user_id = "usr-1";
	tok.token_hash = "hash-one";
	tok.created_at = GetSysTime();
	Check(store.SaveToken(tok, error), "token is saved");
	AgentTokenRecord got;
	Check(store.FindTokenByHash("hash-one", got) && got.id == "tok-1", "token is found by hash");
	tok.token_hash = "hash-two";
	Check(store.SaveToken(tok, error), "token hash is rotated");
	Check(!store.FindTokenByHash("hash-one", got) && store.FindTokenByHash("hash-two", got),
	      "the rotated hash is found and the old one is not");

	// Sessions: keyed by a derived id of the token hash; expired ones are purged.
	SessionRecord live;
	live.token_hash = "sess-live";
	live.user_id = "usr-1";
	live.expires_at = GetSysTime() + 3600;
	SessionRecord dead;
	dead.token_hash = "sess-dead";
	dead.user_id = "usr-1";
	dead.expires_at = GetSysTime() - 3600;
	Check(store.SaveSession(live, error) && store.SaveSession(dead, error), "sessions are saved");
	store.PurgeExpiredSessions();
	SessionRecord s;
	Check(store.FindSession("sess-live", s) && !store.FindSession("sess-dead", s),
	      "purge removes only expired sessions");

	// Proposals: create once, then dedup updates.
	ProposalRecord p;
	p.id = "prp-1";
	p.title = "t";
	p.created_at = GetSysTime();
	bool created = false;
	ProposalRecord existing;
	Check(store.CreateProposalOnce(p, created, existing, error) && created, "proposal is created once");
	Check(store.CreateProposalOnce(p, created, existing, error) && !created && existing.id == "prp-1",
	      "a second create returns the prior record");

	auto touch = [](ProposalRecord& r, String& e) { r.title = "touched"; };
	ProposalRecord out;
	Check(store.UpdateProposalWithDedup("prp-1", "ev-1", touch, out, error) == 1 && out.title == "touched",
	      "an event key applies its mutation once");
	ProposalRecord out2;
	Check(store.UpdateProposalWithDedup("prp-1", "ev-1", touch, out2, error) == 0,
	      "a repeated event key is a duplicate no-op");
	auto reject = [](ProposalRecord& r, String& e) { e = "not allowed"; };
	Check(store.UpdateProposalWithDedup("prp-1", "ev-2", reject, out2, error) == -1 && error == "not allowed",
	      "a rejected mutation leaves the record unchanged");
	ProposalRecord stored;
	Check(store.FindProposal("prp-1", stored) && stored.processed_event_keys.GetCount() == 1,
	      "only the applied event key is recorded");

	// Ledger: duplicates create no second credit; negative balance is refused.
	LedgerEntry credit;
	credit.entry_id = "lgr-1";
	credit.idempotency_key = "comp-1";
	credit.type = COMPANY_LEDGER_CREDIT;
	credit.amount = 100;
	credit.user_id = "usr-1";
	bool dup = true;
	Check(store.AppendLedger(credit, error, &dup) && !dup, "first ledger credit is appended");
	Check(store.AppendLedger(credit, error, &dup) && dup, "a duplicate idempotency key is a no-op");
	Check(store.LedgerBalance("usr-1") == 100, "the balance counts the credit once");
	LedgerEntry debit = credit;
	debit.entry_id = "lgr-2";
	debit.idempotency_key = "debit-1";
	debit.type = COMPANY_LEDGER_DEBIT;
	debit.amount = -500;
	Check(!store.AppendLedger(debit, error), "a debit below zero is refused");
	Check(store.LedgerBalance("usr-1") == 100, "a refused debit does not change the balance");

	// Audit: append-only with ascending seq.
	Check(store.RecordAudit("usr-1", "", COMPANY_ACTOR_HUMAN, Vector<String>(), "a", "t", "x", "", "", "", "k1", error)
	   && store.RecordAudit("usr-1", "", COMPANY_ACTOR_HUMAN, Vector<String>(), "b", "t", "x", "", "", "", "k2", error),
	      "audit events are appended");
	Vector<AuditEvent> audit = store.GetAudit();
	Check(audit.GetCount() == 2 && audit[0].seq == 1 && audit[1].seq == 2, "audit seq is 1, 2");
	Check(store.IsKeyProcessed("k1") && store.IsKeyProcessed("comp-1") && !store.IsKeyProcessed("nope"),
	      "idempotency keys are found in audit and ledger");

	// Config persists across a reopen.
	String cerr;
	Vector<String> repos;
	repos.Add("OuluBSD/Example");
	Check(store.SetRepositoryAllowlist(repos, cerr) && store.IsRepositoryAllowed("OuluBSD/Example"),
	      "the repository allowlist is saved");
	CompanyStore after;
	after.SetRoot(root);
	after.Initialize(cerr);
	Check(after.IsRepositoryAllowed("OuluBSD/Example"), "the allowlist survives a reopen");

	// A policy change made through SetConfig is persisted, not lost.
	CompanyConfig changed = after.GetConfig();
	changed.reward_policy.Set("max_amount", 1234);
	Check(after.SetConfig(changed, cerr), "a changed configuration is saved");
	CompanyStore last;
	last.SetRoot(root);
	last.Initialize(cerr);
	Check(last.GetConfig().reward_policy.Get("max_amount", 0) == 1234,
	      "a policy change survives a reopen");
}

static void RunFixtureChecks(const String& root)
{
	CompanyStore store;
	store.SetRoot(root);
	String error;
	Check(store.Initialize(error), "fixture root initializes: " + error);
	store.SeedTestFixture();
	Check(store.GetUsers().GetCount() > 0, "fixture seeds users");
	Check(store.GetLedger().GetCount() > 0 && store.GetAudit().GetCount() > 0,
	      "fixture seeds ledger and audit");
	int users_before = store.GetUsers().GetCount();
	int audit_before = store.GetAudit().GetCount();
	store.SeedTestFixture();
	Check(store.GetUsers().GetCount() == users_before && store.GetAudit().GetCount() == audit_before,
	      "a second seed is a no-op");
}

END_UPP_NAMESPACE

CONSOLE_APP_MAIN
{
	using namespace Upp;

	String root = AppendFileName(GetTempDirectory(), "company-store-test-" + CompanyGenerateId("t"));
	RealizeDirectory(root);
	RunStoreChecks(AppendFileName(root, "store"));
	RunFixtureChecks(AppendFileName(root, "fixture"));
	DeleteFolderDeep(root);
	Cout() << "\nCompanyStoreTests: " << (s_checks - s_failed) << "/" << s_checks << " checks passed\n";
	SetExitCode(s_failed ? 1 : 0);
}
