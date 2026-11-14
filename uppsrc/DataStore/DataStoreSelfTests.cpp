#include "DataStoreSelfTests.h"

NAMESPACE_UPP

static const char *SELF_TEST_SOURCE_ID = "synthetic-source";
static const char *SELF_TEST_WORKSPACE_ID = "synthetic-ws";
static const char *SELF_TEST_QUALIFIED_ID = "synthetic-ws:rec-1";

bool DataStoreSelfTests::RunSyntheticTests(const String& scratch_dir)
{
	return TestWriteRecord(scratch_dir)
	    && TestPublishBatch(scratch_dir)
	    && TestPrepareJournalRoundTrip(scratch_dir)
	    && TestCrashRecovery(scratch_dir, "after-replace", DATA_WRITE_FAIL_BATCH_AFTER_REPLACE)
	    && TestCrashRecovery(scratch_dir, "before-replace", DATA_WRITE_FAIL_BATCH_BEFORE_REPLACE);
}

bool DataStoreSelfTests::TestWriteRecord(const String& scratch_dir)
{
	String root = AppendFileName(scratch_dir, "write-record");
	if(!Check(WriteFixture(root, "original"), "write-record fixture")) return false;
	DataStore store;
	if(!Check(LoadStore(store, root), "write-record load")) return false;
	DataWriteAuthorization auth = MakeAuthorization();
	String source_revision = store.FindDocument(SELF_TEST_QUALIFIED_ID)->source_revision;

	DataWriteResult first = store.WriteRecord(auth, SELF_TEST_QUALIFIED_ID, "1", source_revision, "write-key-1", MakeRecord("revised", "1"));
	if(!Check(first.status == "committed" && first.revision == "2", "WriteRecord commits the first write")) return false;
	if(!Check(RecordContent(root) == "revised" && RecordRevision(root) == "2", "WriteRecord persists the new content and revision")) return false;

	DataWriteResult replay = store.WriteRecord(auth, SELF_TEST_QUALIFIED_ID, "1", source_revision, "write-key-1", MakeRecord("revised", "1"));
	if(!Check(replay.status == "already_committed" && replay.revision == "2", "WriteRecord replay with the same key does not re-execute")) return false;
	if(!Check(RecordRevision(root) == "2", "WriteRecord replay leaves the revision unchanged")) return false;

	DataWriteResult stale = store.WriteRecord(auth, SELF_TEST_QUALIFIED_ID, "1", source_revision, "write-key-2", MakeRecord("other", "1"));
	if(!CheckStatus(stale.status, "conflict", "WriteRecord rejects a stale expected revision")) return false;
	if(!Check(RecordContent(root) == "revised", "WriteRecord conflict leaves the content unchanged")) return false;

	DataWriteAuthorization other = MakeAuthorization();
	other.source_id = "other-source";
	DataWriteResult denied = store.WriteRecord(other, SELF_TEST_QUALIFIED_ID, "2", store.FindDocument(SELF_TEST_QUALIFIED_ID)->source_revision, "write-key-3", MakeRecord("denied", "2"));
	return CheckStatus(denied.status, "scope_denied", "WriteRecord denies a source outside the authorization");
}

bool DataStoreSelfTests::TestPublishBatch(const String& scratch_dir)
{
	String root = AppendFileName(scratch_dir, "publish-batch");
	if(!Check(WriteFixture(root, "original"), "publish-batch fixture")) return false;
	DataStore store;
	if(!Check(LoadStore(store, root), "publish-batch load")) return false;
	DataWriteAuthorization auth = MakeAuthorization();

	DataBatchChange change;
	change.qualified_id = SELF_TEST_QUALIFIED_ID;
	change.expected_revision = "1";
	change.expected_source_revision = store.FindDocument(SELF_TEST_QUALIFIED_ID)->source_revision;
	change.proposed = MakeRecord("revised", "1");
	Vector<DataBatchChange> changes;
	changes.Add(change);

	DataBatchResult committed = store.PublishBatch(auth, "publish-op-1", changes);
	if(!Check(committed.status == "committed" && committed.changed_ids.GetCount() == 1 && committed.changed_ids[0] == SELF_TEST_QUALIFIED_ID,
	          "PublishBatch commits one changed record")) return false;
	if(!Check(RecordContent(root) == "revised" && RecordRevision(root) == "2", "PublishBatch persists the new content and revision")) return false;

	// A committed batch keeps its journal until recovery removes it, so a publish
	// before recovery is refused rather than re-executed.
	DataBatchResult blocked = store.PublishBatch(auth, "publish-op-1", changes);
	if(!CheckStatus(blocked.status, "journal_recovery_required", "a committed batch leaves its journal pending until recovery")) return false;
	if(!RecoverOnce(store, auth, "RecoverPendingBatches clears the committed journal")) return false;

	DataBatchResult replay = store.PublishBatch(auth, "publish-op-1", changes);
	if(!CheckStatus(replay.status, "already_committed", "PublishBatch replay with the same operation id does not re-execute")) return false;
	if(!Check(replay.changed_ids.GetCount() == 1 && replay.changed_ids[0] == SELF_TEST_QUALIFIED_ID, "PublishBatch replay returns the prior changed ids")) return false;
	if(!Check(RecordRevision(root) == "2", "PublishBatch replay leaves the revision unchanged")) return false;

	DataBatchChange different = change;
	different.proposed = MakeRecord("different", "1");
	Vector<DataBatchChange> different_changes;
	different_changes.Add(different);
	DataBatchResult conflict = store.PublishBatch(auth, "publish-op-1", different_changes);
	if(!CheckStatus(conflict.status, "idempotency_conflict", "PublishBatch rejects a reused operation id with a different request")) return false;
	return Check(RecordContent(root) == "revised", "PublishBatch idempotency conflict leaves the content unchanged");
}

bool DataStoreSelfTests::TestPrepareJournalRoundTrip(const String& scratch_dir)
{
	String root = AppendFileName(scratch_dir, "prepare-journal");
	if(!Check(WriteFixture(root, "original"), "prepare-journal fixture")) return false;
	DataStore store;
	if(!Check(LoadStore(store, root), "prepare-journal load")) return false;
	DataWriteAuthorization auth = MakeAuthorization();

	JournalStageRecord stage;
	stage.action = "update";
	stage.qualified_id = SELF_TEST_QUALIFIED_ID;
	stage.rel_path = "evidence/rec-1.json";
	stage.expected_revision = "1";
	stage.new_revision = "2";
	stage.old_payload = AsJSON(MakeRecord("original", "1"), true);
	stage.new_payload = AsJSON(MakeRecord("revised", "2"), true);
	Vector<JournalStageRecord> stages;
	stages.Add(stage);

	JournalStageResult staged = store.PrepareJournal(auth, "prepare-op-1", stages);
	if(!Check(staged.operation_id == "prepare-op-1", "PrepareJournal reports the staged operation")) return false;

	String manifest_path = AppendFileName(AppendFileName(AppendFileName(root, ".mgr-journal"), "prepare-op-1"), "manifest.json");
	JournalManifest manifest;
	if(!Check(DataStore::ParseJournalManifest(LoadFile(manifest_path), manifest), "journal manifest parses back from disk")) return false;
	if(!Check(manifest.operation_id == "prepare-op-1" && manifest.source_id == SELF_TEST_SOURCE_ID &&
	          manifest.workspace_id == SELF_TEST_WORKSPACE_ID && manifest.records.GetCount() == 1 &&
	          manifest.records[0].record_id == "rec-1" && manifest.records[0].new_revision == "2",
	          "journal manifest round-trips the staged operation")) return false;
	if(!Check(RecordContent(root) == "original", "PrepareJournal alone does not change the record")) return false;

	DataBatchChange change;
	change.qualified_id = SELF_TEST_QUALIFIED_ID;
	change.expected_revision = "1";
	change.expected_source_revision = store.FindDocument(SELF_TEST_QUALIFIED_ID)->source_revision;
	change.proposed = MakeRecord("other", "1");
	Vector<DataBatchChange> changes;
	changes.Add(change);
	DataBatchResult blocked = store.PublishBatch(auth, "prepare-op-2", changes);
	if(!CheckStatus(blocked.status, "journal_recovery_required", "PublishBatch is blocked while a journal is pending")) return false;

	JournalRecoveryResult recovered = store.RecoverPendingBatches(auth);
	if(!CheckStatus(recovered.status, "recovered", "RecoverPendingBatches clears a staged journal")) return false;
	if(!Check(!DirectoryExists(AppendFileName(root, ".mgr-journal")), "recovery removes the journal directory")) return false;
	return Check(RecordContent(root) == "original" && RecordRevision(root) == "1", "recovery of a staged-only journal keeps the record unchanged");
}

bool DataStoreSelfTests::TestCrashRecovery(const String& scratch_dir, const String& name, DataWriteTestFailpoint failpoint)
{
	String root = AppendFileName(scratch_dir, "crash-" + name);
	if(!Check(WriteFixture(root, "original"), "crash fixture")) return false;
	DataStore store;
	if(!Check(LoadStore(store, root), "crash load")) return false;
	DataWriteAuthorization auth = MakeAuthorization();
	String operation_id = "crash-op-" + name;

	DataBatchChange change;
	change.qualified_id = SELF_TEST_QUALIFIED_ID;
	change.expected_revision = "1";
	change.expected_source_revision = store.FindDocument(SELF_TEST_QUALIFIED_ID)->source_revision;
	change.proposed = MakeRecord("revised", "1");
	Vector<DataBatchChange> changes;
	changes.Add(change);

	DataBatchResult crashed = store.PublishBatch(auth, operation_id, changes, failpoint);
	if(!CheckStatus(crashed.status, "uncommitted", "simulated crash leaves the batch uncommitted")) return false;

	DataBatchResult blocked = store.PublishBatch(auth, operation_id, changes);
	if(!CheckStatus(blocked.status, "journal_recovery_required", "a second publish does not re-execute a crashed batch")) return false;

	JournalRecoveryResult recovered = store.RecoverPendingBatches(auth);
	if(!CheckStatus(recovered.status, "recovered", "RecoverPendingBatches recovers the crashed batch")) return false;
	if(!Check(RecordContent(root) == "original" && RecordRevision(root) == "1",
	          "recovery leaves no partial write: the uncommitted batch is rolled back")) return false;

	JournalRecoveryResult again = store.RecoverPendingBatches(auth);
	if(!CheckStatus(again.status, "clean", "a second recovery finds no journal")) return false;
	if(!Check(again.recovered_records == 0, "a second recovery recovers nothing")) return false;

	DataBatchResult retry = store.PublishBatch(auth, operation_id, changes);
	if(!CheckStatus(retry.status, "committed", "retry after recovery commits")) return false;
	if(!Check(RecordContent(root) == "revised" && RecordRevision(root) == "2", "retry after recovery applies the change once")) return false;
	if(!RecoverOnce(store, auth, "RecoverPendingBatches clears the retried journal")) return false;

	DataBatchResult replay = store.PublishBatch(auth, operation_id, changes);
	return CheckStatus(replay.status, "already_committed", "replay after recovery does not re-execute") &&
	       Check(RecordRevision(root) == "2", "replay after recovery leaves the revision at 2");
}

bool DataStoreSelfTests::RecoverOnce(DataStore& store, const DataWriteAuthorization& auth, const char *what)
{
	JournalRecoveryResult recovered = store.RecoverPendingBatches(auth);
	return CheckStatus(recovered.status, "recovered", what);
}

DataWriteAuthorization DataStoreSelfTests::MakeAuthorization()
{
	DataWriteAuthorization auth;
	auth.authenticated = true;
	auth.action_scope = "record:write";
	auth.principal = "synthetic-writer";
	auth.source_id = SELF_TEST_SOURCE_ID;
	auth.workspace_id = SELF_TEST_WORKSPACE_ID;
	return auth;
}

ValueMap DataStoreSelfTests::MakeRecord(const String& content, const String& revision)
{
	ValueMap locator;
	locator.Add("type", "paper-section");
	locator.Add("id", "section-1");
	locator.Add("section", "1");

	ValueMap envelope;
	envelope.Add("schema_id", "skylark/evidence-snapshot");
	envelope.Add("schema_version", "2");
	envelope.Add("workspace_id", SELF_TEST_WORKSPACE_ID);
	envelope.Add("stable_id", "rec-1");
	envelope.Add("record_kind", "evidence-snapshot");
	envelope.Add("revision", revision);
	envelope.Add("visibility", "private");
	envelope.Add("provenance_ref", "synthetic-self-test");

	ValueMap record;
	record.Add("id", "rec-1");
	record.Add("kind", "evidence-snapshot");
	record.Add("workspace_id", SELF_TEST_WORKSPACE_ID);
	record.Add("source_kind", "paper");
	record.Add("source_identity", "synthetic-self-test");
	record.Add("upstream_revision", "synthetic-1");
	record.Add("content_sha256", String('a', 64));
	record.Add("captured_at", "01/01/2024 00:00:00");
	record.Add("source_at", "01/01/2024 00:00:00");
	record.Add("collector", "synthetic-self-test");
	record.Add("locator", locator);
	record.Add("resolution", "resolved");
	record.Add("content", content);
	record.Add("classification", "public");
	record.Add("visibility", "visible");
	record.Add("retracted", false);
	record.Add("predecessor", "none");
	record.Add("contributors", ValueArray());
	record.Add("envelope", envelope);
	return record;
}

String DataStoreSelfTests::RecordPath(const String& root)
{
	return AppendFileName(AppendFileName(root, "evidence"), "rec-1.json");
}

bool DataStoreSelfTests::WriteFixture(const String& root, const String& content)
{
	DeleteFolderDeep(root);
	return RealizeDirectory(AppendFileName(root, "evidence")) && SaveFile(RecordPath(root), AsJSON(MakeRecord(content, "1"), true));
}

bool DataStoreSelfTests::LoadStore(DataStore& store, const String& root)
{
	return store.LoadWriteFixtureForTests(root, SELF_TEST_SOURCE_ID);
}

String DataStoreSelfTests::RecordContent(const String& root)
{
	ValueMap record = ParseJSON(LoadFile(RecordPath(root)));
	return MapGetString(record, "content");
}

String DataStoreSelfTests::RecordRevision(const String& root)
{
	ValueMap record = ParseJSON(LoadFile(RecordPath(root)));
	ValueMap envelope = MapGet(record, "envelope");
	return MapGetString(envelope, "revision");
}

bool DataStoreSelfTests::CheckStatus(const String& actual, const char *expected, const char *what)
{
	if(actual != expected) Cerr() << "FAILED: DataStore self-test: " << what << " (expected " << expected << ", got " << actual << ")\n";
	return actual == expected;
}

bool DataStoreSelfTests::Check(bool ok, const char *what)
{
	if(!ok) Cerr() << "FAILED: DataStore self-test: " << what << "\n";
	return ok;
}

END_UPP_NAMESPACE
