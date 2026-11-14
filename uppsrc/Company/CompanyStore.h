#ifndef _Company_CompanyStore_h_
#define _Company_CompanyStore_h_

#include <Core/Core.h>
#include <WebStorage/WebStorage.h>
#include <WebAuth/WebAuthCrypto.h>
#include "CompanyModel.h"

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// CompanyStore: the Company persistence layer, on WebStorage.
//
// Each collection is one JsonFileStore with one JSON record per entity.
// Lookups by a non-id key (username, token hash, ledger idempotency key) use
// index records. Writes that must be atomic across records, such as a user
// plus its username index, run in one JsonFileStore::Txn. Ledger and audit are
// append-only: they are written with create-only puts and never deleted.
// There are no in-memory caches; every read comes from disk.
// ---------------------------------------------------------------------------

String CompanyGenerateId(const char* prefix);

class CompanyStore : Moveable<CompanyStore> {
public:
	CompanyStore();

	void   SetRoot(const String& root_dir);
	String GetRoot() const { return root; }

	// Creates directories, loads or creates the server secret and the default
	// configuration. Fails closed on any error.
	bool Initialize(String& error);

	const String& GetSecret() const { return secret; }

	// ---------- Users ----------
	Vector<UserRecord> GetUsers() const;
	bool   FindUser(const String& id, UserRecord& out) const;
	bool   FindUserByUsername(const String& username, UserRecord& out) const;
	bool   SaveUser(const UserRecord& user, String& error);
	bool   DeleteUser(const String& id, String& error);

	// ---------- Agent tokens ----------
	Vector<AgentTokenRecord> GetTokens() const;
	bool   FindToken(const String& id, AgentTokenRecord& out) const;
	bool   FindTokenByHash(const String& token_hash, AgentTokenRecord& out) const;
	bool   SaveToken(const AgentTokenRecord& token, String& error);

	// ---------- Sessions ----------
	Vector<SessionRecord> GetSessions() const;
	bool   FindSession(const String& token_hash, SessionRecord& out) const;
	bool   SaveSession(const SessionRecord& session, String& error);
	bool   DeleteSession(const String& token_hash, String& error);
	void   PurgeExpiredSessions();

	// ---------- Proposals ----------
	Vector<ProposalRecord> GetProposals() const;
	bool   FindProposal(const String& id, ProposalRecord& out) const;
	bool   SaveProposal(const ProposalRecord& proposal, String& error);
	// Writes the proposal unless its id already exists. created is false and
	// existing holds the prior record when the id was already present.
	bool   CreateProposalOnce(const ProposalRecord& proposal, bool& created, ProposalRecord& existing, String& error);
	// Atomic idempotent mutation: `mutate` runs inside the transaction and is
	// applied only when `event_key` was not consumed by this record yet.
	// `mutate` rejects by writing a non-empty reason into its error string.
	// Returns 1 = applied, 0 = duplicate no-op, -1 = error. `out` receives the
	// current record state in every outcome.
	int    UpdateProposalWithDedup(const String& proposal_id, const String& event_key,
	                               Function<void(ProposalRecord&, String&)> mutate, ProposalRecord& out, String& error);

	// ---------- Work items ----------
	Vector<WorkItemRecord> GetWorkItems() const;
	bool   FindWorkItem(const String& id, WorkItemRecord& out) const;
	bool   FindWorkItemByPR(const String& repository, int pr_number, WorkItemRecord& out) const;
	bool   SaveWorkItem(const WorkItemRecord& item, String& error);
	int    UpdateWorkItemWithDedup(const String& work_item_id, const String& event_key,
	                               Function<void(WorkItemRecord&, String&)> mutate, WorkItemRecord& out, String& error);

	// ---------- Reviews ----------
	Vector<ReviewRecord> GetReviews() const;
	bool   FindReview(const String& id, ReviewRecord& out) const;
	Vector<ReviewRecord> ReviewsForWorkItem(const String& work_item_id) const;
	bool   SaveReview(const ReviewRecord& review, String& error);

	// ---------- Ledger (append-only) ----------
	Vector<LedgerEntry> GetLedger() const;
	bool   FindLedgerByKey(const String& idempotency_key, LedgerEntry& out) const;
	int    LedgerBalance(const String& user_id) const;
	// Appends an entry. A duplicate idempotency key is a no-op (*duplicate =
	// true, returns true). Negative resulting balances are rejected.
	bool   AppendLedger(const LedgerEntry& entry, String& error, bool* duplicate = nullptr);

	// ---------- Audit (append-only) ----------
	Vector<AuditEvent> GetAudit() const;
	bool   AppendAudit(const AuditEvent& event, String& error);
	// Builds and appends an audit event with a fresh event id.
	bool   RecordAudit(const String& actor_user_id, const String& actor_token_id,
	                   const String& actor_type, const Vector<String>& actor_roles,
	                   const String& action, const String& target_type, const String& target_id,
	                   const String& prior_status, const String& new_status,
	                   const String& reason, const String& idempotency_key, String& error);

	// ---------- Config ----------
	// The configuration is cached in memory; SaveConfig persists the cached copy.
	const CompanyConfig& GetConfig() const { return config; }
	bool   SaveConfig(String& error);
	// Replaces the cached configuration and persists it.
	bool   SetConfig(const CompanyConfig& config, String& error);
	bool   IsRepositoryAllowed(const String& repository) const;
	// Replaces the repository allowlist. An empty list permits no repository.
	bool   SetRepositoryAllowlist(const Vector<String>& repositories, String& error);

	// ---------- Platform v2 ----------
	Vector<PlatformCommandRecord> GetPlatformCommands();
	bool   FindPlatformCommand(const String& id, PlatformCommandRecord& out);
	bool   SavePlatformCommand(const PlatformCommandRecord& command, String& error);

	Vector<PlatformReviewDispatch> GetReviewDispatches();
	bool   FindReviewDispatch(const String& id, PlatformReviewDispatch& out);
	bool   SaveReviewDispatch(const PlatformReviewDispatch& dispatch, String& error);

	Vector<PlatformReviewAttempt> GetReviewAttempts();
	bool   FindReviewAttempt(const String& id, PlatformReviewAttempt& out);
	bool   SaveReviewAttempt(const PlatformReviewAttempt& attempt, String& error);
	Vector<PlatformReviewAttempt> AttemptsForDispatch(const String& dispatch_id);

	Vector<PlatformReviewEvidence> GetReviewEvidence();
	bool   FindReviewEvidence(const String& id, PlatformReviewEvidence& out);
	bool   SaveReviewEvidence(const PlatformReviewEvidence& evidence, String& error);
	Vector<PlatformReviewEvidence> EvidenceForDispatch(const String& dispatch_id);

	Vector<PlatformTrialRecord> GetPlatformTrials();
	bool   FindPlatformTrial(const String& id, PlatformTrialRecord& out);
	bool   SavePlatformTrial(const PlatformTrialRecord& trial, String& error);

	Vector<PlatformTrialResultRecord> GetTrialResults();
	bool   FindTrialResult(const String& id, PlatformTrialResultRecord& out);
	bool   SaveTrialResult(const PlatformTrialResultRecord& result, String& error);
	Vector<PlatformTrialResultRecord> ResultsForTrial(const String& trial_id);

	Vector<PlatformArtifactRecord> GetPlatformArtifacts();
	bool   FindPlatformArtifact(const String& id, PlatformArtifactRecord& out);
	bool   SavePlatformArtifact(const PlatformArtifactRecord& artifact, String& error);
	Vector<PlatformArtifactRecord> ArtifactsForOwner(const String& owner_type, const String& owner_id);

	// ---------- Client contracts and projects ----------
	Vector<ContractRecord> GetContracts();
	bool   FindContract(const String& id, ContractRecord& out);
	bool   SaveContract(const ContractRecord& contract, String& error);
	int    UpdateContractWithDedup(const String& id, const String& event_key,
	                               Function<void(ContractRecord&, String&)> mutate, ContractRecord& out, String& error);

	Vector<ProjectRecord> GetProjects();
	bool   FindProject(const String& id, ProjectRecord& out);
	bool   SaveProject(const ProjectRecord& project, String& error);
	int    UpdateProjectWithDedup(const String& id, const String& event_key,
	                              Function<void(ProjectRecord&, String&)> mutate, ProjectRecord& out, String& error);

	// ---------- Idempotency ----------
	// True when the key was already consumed by an audit event, a ledger entry,
	// or a work item event list.
	bool   IsKeyProcessed(const String& key) const;
	bool   MarkWorkItemEventProcessed(const String& work_item_id, const String& key, String& error);

	// ---------- Integrity ----------
	Vector<String> GetCorruptionErrors() const;
	bool           IsCollectionCorrupted(const String& collection) const;

	// ---------- Deterministic test fixture ----------
	// Seeds users, tokens, proposals, work items, reviews, ledger and audit
	// covering every lifecycle state. Only seeds an empty store; test-only.
	void   SeedTestFixture();

	// Collection names (used by tests and admin inspection).
	static const char* COL_USERS()     { return "users"; }
	static const char* COL_TOKENS()    { return "agent_tokens"; }
	static const char* COL_SESSIONS()  { return "sessions"; }
	static const char* COL_PROPOSALS() { return "proposals"; }
	static const char* COL_WORK()      { return "work_items"; }
	static const char* COL_REVIEWS()   { return "reviews"; }
	static const char* COL_LEDGER()    { return "ledger"; }
	static const char* COL_AUDIT()     { return "audit"; }
	static const char* COL_CONFIG()    { return "config"; }
	static const char* COL_COMMANDS()     { return "platform_commands"; }
	static const char* COL_DISPATCHES()   { return "review_dispatches"; }
	static const char* COL_ATTEMPTS()     { return "review_attempts"; }
	static const char* COL_EVIDENCE()     { return "review_evidence"; }
	static const char* COL_TRIALS()       { return "platform_trials"; }
	static const char* COL_TRIALRESULTS() { return "trial_results"; }
	static const char* COL_ARTIFACTS()    { return "platform_artifacts"; }
	static const char* COL_CONTRACTS()    { return "contracts"; }
	static const char* COL_PROJECTS()     { return "projects"; }

private:
	JsonFileStore& Col(const char* collection) const;
	bool           Writable(const char* collection, String& error) const;
	bool           ReadIndex(const char* collection, const String& index_id, String& target) const;
	static String  IndexJson(const String& target);

	// Records whose ids start with this prefix are index records, not entities.
	static bool IsIndexId(const String& id);
	static String IndexId(const char* kind, const String& value);
	static String DerivedId(const String& prefix, const String& value);

	// Generic record access. Each helper runs one store call (or one Txn).
	template <class T>
	bool ReadRecord(const char* collection, const String& id, T& out) const;
	template <class T>
	Vector<T> ListRecords(const char* collection) const;
	template <class T>
	bool UpsertRecord(const char* collection, const String& id, const T& record, String& error);
	template <class T>
	int DedupUpdate(const char* collection, const String& id, const String& event_key,
	                Function<void(T&, String&)> mutate, T& out, String& error);

	bool   EnsureSecret(String& error);
	bool   EnsureConfig(String& error);
	void   NoteCorruption(const String& collection, const String& error) const;

	String                  root;
	String                  secret;
	CompanyConfig           config;
	mutable Mutex           cols_mutex;
	mutable VectorMap<String, One<JsonFileStore>> cols;
	mutable Mutex           corruption_mutex;
	mutable Vector<String>  corruption_errors;
};

// ---------------------------------------------------------------------------
// Template bodies.
// ---------------------------------------------------------------------------

template <class T>
bool CompanyStore::ReadRecord(const char* collection, const String& id, T& out) const
{
	// A malformed id is a caller error, not a damaged record: it must not mark
	// the collection corrupted.
	if(!JsonFileStore::ValidId(id))
		return false;
	String json, rev;
	JsonFileStore& col = Col(collection);
	StoreResult r = col.Get(id, json, rev);
	if(!r.ok) {
		if(r.error != "record not found")
			NoteCorruption(collection, r.error);
		return false;
	}
	LoadFromJsonValue(out, ParseJSON(json));
	return true;
}

template <class T>
Vector<T> CompanyStore::ListRecords(const char* collection) const
{
	Vector<T> out;
	Vector<String> ids;
	JsonFileStore& col = Col(collection);
	col.List(ids);
	for(const String& id : ids) {
		if(IsIndexId(id))
			continue;
		T rec;
		if(ReadRecord(collection, id, rec))
			out.Add(pick(rec));
	}
	return out;
}

template <class T>
bool CompanyStore::UpsertRecord(const char* collection, const String& id, const T& record, String& error)
{
	JsonFileStore::Txn txn(Col(collection));
	if(!txn.Ok()) {
		error = txn.Error();
		return false;
	}
	String rev;
	StoreResult r = txn.Put(id, AsJSON(StoreAsJsonValue(record), true), String(), rev);
	if(!r.ok)
		error = r.error;
	return r.ok;
}

// Locked read-modify-write of one record with a consumed event key. Returns
// 1 = applied, 0 = duplicate event key (out holds the record), -1 = rejected
// (error holds the reason; the record is unchanged).
template <class T>
int CompanyStore::DedupUpdate(const char* collection, const String& id, const String& event_key,
                              Function<void(T&, String&)> mutate, T& out, String& error)
{
	JsonFileStore::Txn txn(Col(collection));
	if(!txn.Ok()) {
		error = txn.Error();
		return -1;
	}
	String json, rev;
	StoreResult g = txn.Get(id, json, rev);
	if(!g.ok) {
		error = g.error == "record not found" ? String(collection) + " not found: " + id : g.error;
		return -1;
	}
	T rec;
	LoadFromJsonValue(rec, ParseJSON(json));
	if(!event_key.IsEmpty())
		for(const String& key : rec.processed_event_keys)
			if(key == event_key) {
				out = rec;
				error = "event already processed";
				return 0;
			}
	String merr;
	mutate(rec, merr);
	if(!merr.IsEmpty()) {
		error = merr;
		return -1;
	}
	rec.updated_at = GetSysTime();
	if(!event_key.IsEmpty())
		rec.processed_event_keys.Add(event_key);
	String new_rev;
	StoreResult w = txn.Put(id, AsJSON(StoreAsJsonValue(rec), true), rev, new_rev);
	if(!w.ok) {
		error = w.error;
		return -1;
	}
	out = rec;
	return 1;
}

END_UPP_NAMESPACE

#endif
