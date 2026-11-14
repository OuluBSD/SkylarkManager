#include "CompanyStore.h"

#ifdef PLATFORM_POSIX
#include <sys/stat.h>
#endif

NAMESPACE_UPP

String CompanyGenerateId(const char* prefix)
{
	byte buf[16];
	for(int i = 0; i < 16; i++)
		buf[i] = (byte)Random(256);
	return String(prefix) + "_" + ToLower(HexString(buf, 16));
}

static void SortBySeq(Vector<LedgerEntry>& v)
{
	Sort(v, [](const LedgerEntry& a, const LedgerEntry& b) { return a.seq < b.seq; });
}

static void SortBySeq(Vector<AuditEvent>& v)
{
	Sort(v, [](const AuditEvent& a, const AuditEvent& b) { return a.seq < b.seq; });
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

CompanyStore::CompanyStore()
{
}

void CompanyStore::SetRoot(const String& root_dir)
{
	Mutex::Lock __(cols_mutex);
	root = root_dir;
	cols.Clear();
}

bool CompanyStore::Initialize(String& error)
{
	if(root.IsEmpty()) {
		error = "company store root is not set";
		return false;
	}
	RealizeDirectory(root);
	RealizeDirectory(AppendFileName(root, "locks"));
	RealizeDirectory(AppendFileName(root, "data"));

	if(!EnsureSecret(error))
		return false;
	if(!EnsureConfig(error))
		return false;
	return true;
}

// Created under a file lock and renamed into place, so concurrent first starts
// agree on one secret.
bool CompanyStore::EnsureSecret(String& error)
{
	String path = AppendFileName(root, "secret.key");
	JsonFileStore::FileLock lock;
	String lock_error;
	if(!lock.Acquire(AppendFileName(AppendFileName(root, "locks"), "secret.lock"), lock_error)) {
		error = "cannot initialize server secret: " + lock_error;
		return false;
	}
	if(!FileExists(path) && !JsonFileStore::WriteAtomic(path, WebAuthRandomBytes(32) + "\n", error)) {
		error = "cannot initialize server secret: " + error;
		return false;
	}
#ifdef PLATFORM_POSIX
	if(chmod(~path, 0600) != 0) {
		error = "cannot restrict permissions on " + path;
		return false;
	}
#endif
	secret = TrimBoth(LoadFile(path));
	if(secret.GetCount() < 32) {
		error = "server secret is too short; refusing to start (fail-closed)";
		return false;
	}
	return true;
}

// Loads the stored configuration, or writes the defaults on first start.
bool CompanyStore::EnsureConfig(String& error)
{
	if(ReadRecord(COL_CONFIG(), "config", config))
		return true;
	config = CompanyConfig();
	config.schema_version = COMPANY_SCHEMA_VERSION;
	config.repository_allowlist.Clear();
	config.reward_policy.Set("max_hours", 40);
	config.reward_policy.Set("max_amount", 5000);
	config.reward_policy.Set("currency", "CREDIT");
	config.review_policy.Set("operator_quorum", 1);
	config.review_policy.Set("mechanical_required", true);
	config.agent_reviewer.Set("enabled", false);
	config.agent_reviewer.Set("command", "");
	config.updated_at = GetSysTime();
	return SaveConfig(error);
}

// ---------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------

JsonFileStore& CompanyStore::Col(const char* collection) const
{
	Mutex::Lock __(cols_mutex);
	int q = cols.Find(collection);
	if(q < 0) {
		One<JsonFileStore>& col = cols.Add(collection);
		col.Create<JsonFileStore>("company", root, collection);
		return *col;
	}
	return *cols[q];
}

// Fail closed: a collection with a recorded corruption accepts no writes.
bool CompanyStore::Writable(const char* collection, String& error) const
{
	if(IsCollectionCorrupted(collection)) {
		error = String("collection is corrupted, refusing write: ") + collection;
		return false;
	}
	return true;
}

bool CompanyStore::ReadIndex(const char* collection, const String& index_id, String& target) const
{
	String json, rev;
	if(!Col(collection).Get(index_id, json, rev).ok)
		return false;
	ValueMap m = ParseJSON(json);
	target = m["target"];
	return true;
}

String CompanyStore::IndexJson(const String& target)
{
	ValueMap m;
	m.Set("target", target);
	return AsJSON(m, true);
}

bool CompanyStore::IsIndexId(const String& id)
{
	return id.StartsWith("ix-");
}

String CompanyStore::IndexId(const char* kind, const String& value)
{
	return DerivedId(String("ix-") + kind, value);
}

// Stable record id for a value that is not a valid id on its own (a token hash,
// a ledger idempotency key). The id is a SHA-256 of the value, so it is always
// a safe file name.
String CompanyStore::DerivedId(const String& prefix, const String& value)
{
	byte digest[32];
	SHA256(digest, value);
	return prefix + "-" + ToLower(HexString(digest, 32));
}

void CompanyStore::NoteCorruption(const String& collection, const String& error) const
{
	Mutex::Lock __(corruption_mutex);
	corruption_errors.Add(collection + ": " + error);
}

Vector<String> CompanyStore::GetCorruptionErrors() const
{
	Mutex::Lock __(corruption_mutex);
	return clone(corruption_errors);
}

bool CompanyStore::IsCollectionCorrupted(const String& collection) const
{
	Mutex::Lock __(corruption_mutex);
	for(const String& e : corruption_errors)
		if(e.StartsWith(collection + ":"))
			return true;
	return false;
}

// ---------------------------------------------------------------------------
// Users (username index: ix-username-<hash of lowercased name> -> user id)
// ---------------------------------------------------------------------------

Vector<UserRecord> CompanyStore::GetUsers() const
{
	return ListRecords<UserRecord>(COL_USERS());
}

bool CompanyStore::FindUser(const String& id, UserRecord& out) const
{
	return ReadRecord(COL_USERS(), id, out);
}

bool CompanyStore::FindUserByUsername(const String& username, UserRecord& out) const
{
	String user_id;
	if(!ReadIndex(COL_USERS(), IndexId("username", ToLower(username)), user_id))
		return false;
	return FindUser(user_id, out);
}

bool CompanyStore::SaveUser(const UserRecord& user, String& error)
{
	if(!Writable(COL_USERS(), error))
		return false;
	JsonFileStore::Txn txn(Col(COL_USERS()));
	if(!txn.Ok()) {
		error = txn.Error();
		return false;
	}

	String name_index = IndexId("username", ToLower(user.username));
	String owner_json, owner_rev;
	String owner_id;
	bool name_taken = txn.Get(name_index, owner_json, owner_rev).ok;
	if(name_taken) {
		ValueMap owner = ParseJSON(owner_json);
		owner_id = owner["target"];
	}
	if(name_taken && owner_id != user.id) {
		error = "username already exists: " + user.username;
		return false;
	}

	String prev_json, prev_rev, rev;
	bool exists = txn.Get(user.id, prev_json, prev_rev).ok;
	if(exists) {
		UserRecord prev;
		LoadFromJsonValue(prev, ParseJSON(prev_json));
		if(ToLower(prev.username) != ToLower(user.username))
			txn.Delete(IndexId("username", ToLower(prev.username)), String());
	}
	StoreResult w = txn.Put(user.id, AsJSON(StoreAsJsonValue(user), true),
	                        exists ? prev_rev : String(RecordAbsent()), rev);
	if(!w.ok) {
		error = w.error;
		return false;
	}
	if(!name_taken) {
		StoreResult i = txn.Put(name_index, IndexJson(user.id), RecordAbsent(), rev);
		if(!i.ok) {
			error = i.error;
			return false;
		}
	}
	return true;
}

bool CompanyStore::DeleteUser(const String& id, String& error)
{
	if(!Writable(COL_USERS(), error))
		return false;
	JsonFileStore::Txn txn(Col(COL_USERS()));
	if(!txn.Ok()) {
		error = txn.Error();
		return false;
	}
	String json, rev;
	if(!txn.Get(id, json, rev).ok) {
		error = "user not found";
		return false;
	}
	UserRecord user;
	LoadFromJsonValue(user, ParseJSON(json));
	txn.Delete(IndexId("username", ToLower(user.username)), String());
	StoreResult d = txn.Delete(id, rev);
	if(!d.ok)
		error = d.error;
	return d.ok;
}

// ---------------------------------------------------------------------------
// Agent tokens (ix-tokenhash-<hash> -> token id)
// ---------------------------------------------------------------------------

Vector<AgentTokenRecord> CompanyStore::GetTokens() const
{
	return ListRecords<AgentTokenRecord>(COL_TOKENS());
}

bool CompanyStore::FindToken(const String& id, AgentTokenRecord& out) const
{
	return ReadRecord(COL_TOKENS(), id, out);
}

bool CompanyStore::FindTokenByHash(const String& token_hash, AgentTokenRecord& out) const
{
	String token_id;
	if(!ReadIndex(COL_TOKENS(), IndexId("tokenhash", token_hash), token_id))
		return false;
	return FindToken(token_id, out);
}

bool CompanyStore::SaveToken(const AgentTokenRecord& token, String& error)
{
	if(!Writable(COL_TOKENS(), error))
		return false;
	JsonFileStore::Txn txn(Col(COL_TOKENS()));
	if(!txn.Ok()) {
		error = txn.Error();
		return false;
	}
	String prev_json, prev_rev, rev;
	if(txn.Get(token.id, prev_json, prev_rev).ok) {
		AgentTokenRecord prev;
		LoadFromJsonValue(prev, ParseJSON(prev_json));
		if(prev.token_hash != token.token_hash)
			txn.Delete(IndexId("tokenhash", prev.token_hash), String());
	}
	StoreResult w = txn.Put(token.id, AsJSON(StoreAsJsonValue(token), true), String(), rev);
	if(!w.ok) {
		error = w.error;
		return false;
	}
	StoreResult i = txn.Put(IndexId("tokenhash", token.token_hash), IndexJson(token.id), String(), rev);
	if(!i.ok)
		error = i.error;
	return i.ok;
}

// ---------------------------------------------------------------------------
// Sessions (keyed by a derived id of the token hash)
// ---------------------------------------------------------------------------

Vector<SessionRecord> CompanyStore::GetSessions() const
{
	return ListRecords<SessionRecord>(COL_SESSIONS());
}

bool CompanyStore::FindSession(const String& token_hash, SessionRecord& out) const
{
	return ReadRecord(COL_SESSIONS(), DerivedId("ses", token_hash), out);
}

bool CompanyStore::SaveSession(const SessionRecord& session, String& error)
{
	return UpsertRecord(COL_SESSIONS(), DerivedId("ses", session.token_hash), session, error);
}

bool CompanyStore::DeleteSession(const String& token_hash, String& error)
{
	StoreResult d = Col(COL_SESSIONS()).Delete(DerivedId("ses", token_hash), String());
	if(!d.ok)
		error = d.error;
	return d.ok;
}

void CompanyStore::PurgeExpiredSessions()
{
	Time now = GetSysTime();
	for(const SessionRecord& s : GetSessions())
		if(!IsNull(s.expires_at) && s.expires_at < now)
			Col(COL_SESSIONS()).Delete(DerivedId("ses", s.token_hash), String());
}

// ---------------------------------------------------------------------------
// Proposals
// ---------------------------------------------------------------------------

Vector<ProposalRecord> CompanyStore::GetProposals() const
{
	return ListRecords<ProposalRecord>(COL_PROPOSALS());
}

bool CompanyStore::FindProposal(const String& id, ProposalRecord& out) const
{
	return ReadRecord(COL_PROPOSALS(), id, out);
}

bool CompanyStore::CreateProposalOnce(const ProposalRecord& proposal, bool& created, ProposalRecord& existing, String& error)
{
	created = false;
	if(!Writable(COL_PROPOSALS(), error))
		return false;
	JsonFileStore::Txn txn(Col(COL_PROPOSALS()));
	if(!txn.Ok()) {
		error = txn.Error();
		return false;
	}
	String json, rev;
	if(txn.Get(proposal.id, json, rev).ok) {
		LoadFromJsonValue(existing, ParseJSON(json));
		return true;
	}
	StoreResult w = txn.Put(proposal.id, AsJSON(StoreAsJsonValue(proposal), true), RecordAbsent(), rev);
	if(!w.ok) {
		error = w.error;
		return false;
	}
	created = true;
	return true;
}

bool CompanyStore::SaveProposal(const ProposalRecord& proposal, String& error)
{
	return UpsertRecord(COL_PROPOSALS(), proposal.id, proposal, error);
}

int CompanyStore::UpdateProposalWithDedup(const String& proposal_id, const String& event_key,
                                          Function<void(ProposalRecord&, String&)> mutate, ProposalRecord& out, String& error)
{
	return DedupUpdate<ProposalRecord>(COL_PROPOSALS(), proposal_id, event_key, mutate, out, error);
}

// ---------------------------------------------------------------------------
// Client contracts and projects
// ---------------------------------------------------------------------------

Vector<ContractRecord> CompanyStore::GetContracts()
{
	return ListRecords<ContractRecord>(COL_CONTRACTS());
}

bool CompanyStore::FindContract(const String& id, ContractRecord& out)
{
	return ReadRecord(COL_CONTRACTS(), id, out);
}

bool CompanyStore::SaveContract(const ContractRecord& contract, String& error)
{
	return UpsertRecord(COL_CONTRACTS(), contract.id, contract, error);
}

int CompanyStore::UpdateContractWithDedup(const String& id, const String& event_key,
                                          Function<void(ContractRecord&, String&)> mutate, ContractRecord& out, String& error)
{
	return DedupUpdate<ContractRecord>(COL_CONTRACTS(), id, event_key, mutate, out, error);
}

Vector<ProjectRecord> CompanyStore::GetProjects()
{
	return ListRecords<ProjectRecord>(COL_PROJECTS());
}

bool CompanyStore::FindProject(const String& id, ProjectRecord& out)
{
	return ReadRecord(COL_PROJECTS(), id, out);
}

bool CompanyStore::SaveProject(const ProjectRecord& project, String& error)
{
	return UpsertRecord(COL_PROJECTS(), project.id, project, error);
}

int CompanyStore::UpdateProjectWithDedup(const String& id, const String& event_key,
                                         Function<void(ProjectRecord&, String&)> mutate, ProjectRecord& out, String& error)
{
	return DedupUpdate<ProjectRecord>(COL_PROJECTS(), id, event_key, mutate, out, error);
}

// ---------------------------------------------------------------------------
// Work items
// ---------------------------------------------------------------------------

Vector<WorkItemRecord> CompanyStore::GetWorkItems() const
{
	return ListRecords<WorkItemRecord>(COL_WORK());
}

bool CompanyStore::FindWorkItem(const String& id, WorkItemRecord& out) const
{
	return ReadRecord(COL_WORK(), id, out);
}

bool CompanyStore::FindWorkItemByPR(const String& repository, int pr_number, WorkItemRecord& out) const
{
	for(const WorkItemRecord& item : GetWorkItems())
		if(item.repository == repository && item.pr_number == pr_number) {
			out = item;
			return true;
		}
	return false;
}

bool CompanyStore::SaveWorkItem(const WorkItemRecord& item, String& error)
{
	return UpsertRecord(COL_WORK(), item.id, item, error);
}

int CompanyStore::UpdateWorkItemWithDedup(const String& work_item_id, const String& event_key,
                                          Function<void(WorkItemRecord&, String&)> mutate, WorkItemRecord& out, String& error)
{
	return DedupUpdate<WorkItemRecord>(COL_WORK(), work_item_id, event_key, mutate, out, error);
}

bool CompanyStore::MarkWorkItemEventProcessed(const String& work_item_id, const String& key, String& error)
{
	if(key.IsEmpty())
		return true;
	WorkItemRecord ignored;
	int r = DedupUpdate<WorkItemRecord>(COL_WORK(), work_item_id, key,
	                                    [](WorkItemRecord&, String&) {}, ignored, error);
	// 0 = the key was already consumed: marking it again is a success.
	return r >= 0;
}

// ---------------------------------------------------------------------------
// Reviews
// ---------------------------------------------------------------------------

Vector<ReviewRecord> CompanyStore::GetReviews() const
{
	return ListRecords<ReviewRecord>(COL_REVIEWS());
}

bool CompanyStore::FindReview(const String& id, ReviewRecord& out) const
{
	return ReadRecord(COL_REVIEWS(), id, out);
}

Vector<ReviewRecord> CompanyStore::ReviewsForWorkItem(const String& work_item_id) const
{
	Vector<ReviewRecord> out;
	for(const ReviewRecord& r : GetReviews())
		if(r.work_item_id == work_item_id)
			out.Add(r);
	return out;
}

bool CompanyStore::SaveReview(const ReviewRecord& review, String& error)
{
	return UpsertRecord(COL_REVIEWS(), review.id, review, error);
}

// ---------------------------------------------------------------------------
// Ledger (append-only; id derived from the idempotency key, so a duplicate is
// a create-only conflict and balance check and write share one transaction)
// ---------------------------------------------------------------------------

Vector<LedgerEntry> CompanyStore::GetLedger() const
{
	Vector<LedgerEntry> out = ListRecords<LedgerEntry>(COL_LEDGER());
	SortBySeq(out);
	return out;
}

bool CompanyStore::FindLedgerByKey(const String& idempotency_key, LedgerEntry& out) const
{
	return ReadRecord(COL_LEDGER(), DerivedId("lgr", idempotency_key), out);
}

int CompanyStore::LedgerBalance(const String& user_id) const
{
	int balance = 0;
	for(const LedgerEntry& e : GetLedger())
		if(e.user_id == user_id)
			balance += e.amount;
	return balance;
}

bool CompanyStore::AppendLedger(const LedgerEntry& entry, String& error, bool* duplicate)
{
	if(duplicate)
		*duplicate = false;
	if(!Writable(COL_LEDGER(), error))
		return false;
	JsonFileStore::Txn txn(Col(COL_LEDGER()));
	if(!txn.Ok()) {
		error = txn.Error();
		return false;
	}
	String id = DerivedId("lgr", entry.idempotency_key);
	String json, rev;
	if(txn.Get(id, json, rev).ok) {
		// Idempotent duplicate: one completion creates at most one credit.
		if(duplicate)
			*duplicate = true;
		return true;
	}
	if(entry.type != COMPANY_LEDGER_CREDIT && entry.type != COMPANY_LEDGER_DEBIT && entry.type != COMPANY_LEDGER_CORRECTION) {
		error = "unknown ledger entry type: " + entry.type;
		return false;
	}
	if(entry.amount == 0) {
		error = "ledger entry amount must be non-zero";
		return false;
	}
	// The balance projection is recomputed from the entries under the lock.
	Vector<LedgerEntry> all = ListRecords<LedgerEntry>(COL_LEDGER());
	int balance = 0;
	for(const LedgerEntry& e : all)
		if(e.user_id == entry.user_id)
			balance += e.amount;
	if(balance + entry.amount < 0) {
		error = "ledger entry would produce a negative balance";
		return false;
	}
	LedgerEntry stored = entry;
	stored.seq = all.GetCount() + 1;
	StoreResult w = txn.Put(id, AsJSON(StoreAsJsonValue(stored), true), RecordAbsent(), rev);
	if(!w.ok) {
		error = w.error;
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Audit (append-only)
// ---------------------------------------------------------------------------

Vector<AuditEvent> CompanyStore::GetAudit() const
{
	Vector<AuditEvent> out = ListRecords<AuditEvent>(COL_AUDIT());
	SortBySeq(out);
	return out;
}

bool CompanyStore::AppendAudit(const AuditEvent& event, String& error)
{
	if(!Writable(COL_AUDIT(), error))
		return false;
	JsonFileStore::Txn txn(Col(COL_AUDIT()));
	if(!txn.Ok()) {
		error = txn.Error();
		return false;
	}
	AuditEvent stored = event;
	if(stored.event_id.IsEmpty())
		stored.event_id = CompanyGenerateId("evt");
	stored.seq = ListRecords<AuditEvent>(COL_AUDIT()).GetCount() + 1;
	String rev;
	StoreResult w = txn.Put(stored.event_id, AsJSON(StoreAsJsonValue(stored), true), RecordAbsent(), rev);
	if(!w.ok)
		error = w.error;
	return w.ok;
}

bool CompanyStore::RecordAudit(const String& actor_user_id, const String& actor_token_id,
                               const String& actor_type, const Vector<String>& actor_roles,
                               const String& action, const String& target_type, const String& target_id,
                               const String& prior_status, const String& new_status,
                               const String& reason, const String& idempotency_key, String& error)
{
	AuditEvent event;
	event.event_id = CompanyGenerateId("evt");
	event.actor_user_id = actor_user_id;
	event.actor_token_id = actor_token_id;
	event.actor_type = actor_type;
	event.actor_roles = clone(actor_roles);
	event.action = action;
	event.target_type = target_type;
	event.target_id = target_id;
	event.prior_status = prior_status;
	event.new_status = new_status;
	event.reason = reason;
	event.idempotency_key = idempotency_key;
	event.created_at = GetSysTime();
	return AppendAudit(event, error);
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

bool CompanyStore::SaveConfig(String& error)
{
	return UpsertRecord(COL_CONFIG(), "config", config, error);
}

bool CompanyStore::SetConfig(const CompanyConfig& cfg, String& error)
{
	config = cfg;
	return SaveConfig(error);
}

bool CompanyStore::SetRepositoryAllowlist(const Vector<String>& repositories, String& error)
{
	config.repository_allowlist.Clear();
	for(const String& r : repositories)
		config.repository_allowlist.Add(r);
	config.updated_at = GetSysTime();
	return SaveConfig(error);
}

bool CompanyStore::IsRepositoryAllowed(const String& repository) const
{
	if(config.repository_allowlist.IsEmpty())
		return false; // fail closed: an empty allowlist permits nothing
	for(const String& r : config.repository_allowlist)
		if(r == repository)
			return true;
	return false;
}

// ---------------------------------------------------------------------------
// Idempotency
// ---------------------------------------------------------------------------

bool CompanyStore::IsKeyProcessed(const String& key) const
{
	if(key.IsEmpty())
		return false;
	for(const AuditEvent& e : ListRecords<AuditEvent>(COL_AUDIT()))
		if(e.idempotency_key == key)
			return true;
	LedgerEntry ignored;
	if(FindLedgerByKey(key, ignored))
		return true;
	for(const WorkItemRecord& item : GetWorkItems())
		for(const String& k : item.processed_event_keys)
			if(k == key)
				return true;
	return false;
}

// ---------------------------------------------------------------------------
// Platform v2 collections: one record per id, read from disk on every call.
// ---------------------------------------------------------------------------

Vector<PlatformCommandRecord> CompanyStore::GetPlatformCommands()
{
	return ListRecords<PlatformCommandRecord>(COL_COMMANDS());
}

bool CompanyStore::FindPlatformCommand(const String& id, PlatformCommandRecord& out)
{
	return ReadRecord(COL_COMMANDS(), id, out);
}

bool CompanyStore::SavePlatformCommand(const PlatformCommandRecord& command, String& error)
{
	return UpsertRecord(COL_COMMANDS(), command.id, command, error);
}

Vector<PlatformReviewDispatch> CompanyStore::GetReviewDispatches()
{
	return ListRecords<PlatformReviewDispatch>(COL_DISPATCHES());
}

bool CompanyStore::FindReviewDispatch(const String& id, PlatformReviewDispatch& out)
{
	return ReadRecord(COL_DISPATCHES(), id, out);
}

bool CompanyStore::SaveReviewDispatch(const PlatformReviewDispatch& dispatch, String& error)
{
	return UpsertRecord(COL_DISPATCHES(), dispatch.id, dispatch, error);
}

Vector<PlatformReviewAttempt> CompanyStore::GetReviewAttempts()
{
	return ListRecords<PlatformReviewAttempt>(COL_ATTEMPTS());
}

bool CompanyStore::FindReviewAttempt(const String& id, PlatformReviewAttempt& out)
{
	return ReadRecord(COL_ATTEMPTS(), id, out);
}

bool CompanyStore::SaveReviewAttempt(const PlatformReviewAttempt& attempt, String& error)
{
	return UpsertRecord(COL_ATTEMPTS(), attempt.id, attempt, error);
}

Vector<PlatformReviewAttempt> CompanyStore::AttemptsForDispatch(const String& dispatch_id)
{
	Vector<PlatformReviewAttempt> out;
	for(const PlatformReviewAttempt& a : GetReviewAttempts())
		if(a.dispatch_id == dispatch_id)
			out.Add(a);
	return out;
}

Vector<PlatformReviewEvidence> CompanyStore::GetReviewEvidence()
{
	return ListRecords<PlatformReviewEvidence>(COL_EVIDENCE());
}

bool CompanyStore::FindReviewEvidence(const String& id, PlatformReviewEvidence& out)
{
	return ReadRecord(COL_EVIDENCE(), id, out);
}

bool CompanyStore::SaveReviewEvidence(const PlatformReviewEvidence& evidence, String& error)
{
	return UpsertRecord(COL_EVIDENCE(), evidence.id, evidence, error);
}

Vector<PlatformReviewEvidence> CompanyStore::EvidenceForDispatch(const String& dispatch_id)
{
	Vector<PlatformReviewEvidence> out;
	for(const PlatformReviewEvidence& e : GetReviewEvidence())
		if(e.dispatch_id == dispatch_id)
			out.Add(e);
	return out;
}

Vector<PlatformTrialRecord> CompanyStore::GetPlatformTrials()
{
	return ListRecords<PlatformTrialRecord>(COL_TRIALS());
}

bool CompanyStore::FindPlatformTrial(const String& id, PlatformTrialRecord& out)
{
	return ReadRecord(COL_TRIALS(), id, out);
}

bool CompanyStore::SavePlatformTrial(const PlatformTrialRecord& trial, String& error)
{
	return UpsertRecord(COL_TRIALS(), trial.id, trial, error);
}

Vector<PlatformTrialResultRecord> CompanyStore::GetTrialResults()
{
	return ListRecords<PlatformTrialResultRecord>(COL_TRIALRESULTS());
}

bool CompanyStore::FindTrialResult(const String& id, PlatformTrialResultRecord& out)
{
	return ReadRecord(COL_TRIALRESULTS(), id, out);
}

bool CompanyStore::SaveTrialResult(const PlatformTrialResultRecord& result, String& error)
{
	return UpsertRecord(COL_TRIALRESULTS(), result.id, result, error);
}

Vector<PlatformTrialResultRecord> CompanyStore::ResultsForTrial(const String& trial_id)
{
	Vector<PlatformTrialResultRecord> out;
	for(const PlatformTrialResultRecord& r : GetTrialResults())
		if(r.trial_id == trial_id)
			out.Add(r);
	return out;
}

Vector<PlatformArtifactRecord> CompanyStore::GetPlatformArtifacts()
{
	return ListRecords<PlatformArtifactRecord>(COL_ARTIFACTS());
}

bool CompanyStore::FindPlatformArtifact(const String& id, PlatformArtifactRecord& out)
{
	return ReadRecord(COL_ARTIFACTS(), id, out);
}

bool CompanyStore::SavePlatformArtifact(const PlatformArtifactRecord& artifact, String& error)
{
	return UpsertRecord(COL_ARTIFACTS(), artifact.id, artifact, error);
}

Vector<PlatformArtifactRecord> CompanyStore::ArtifactsForOwner(const String& owner_type, const String& owner_id)
{
	Vector<PlatformArtifactRecord> out;
	for(const PlatformArtifactRecord& a : GetPlatformArtifacts())
		if(a.owner_type == owner_type && a.owner_id == owner_id)
			out.Add(a);
	return out;
}

END_UPP_NAMESPACE
