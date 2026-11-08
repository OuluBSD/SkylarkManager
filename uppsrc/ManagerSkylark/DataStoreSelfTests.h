#ifndef _ManagerSkylark_DataStoreSelfTests_h_
#define _ManagerSkylark_DataStoreSelfTests_h_

#include "DataStore.h"

NAMESPACE_UPP

// Headless self-tests for the DataStore write hard paths: WriteRecord,
// PublishBatch, PrepareJournal, RecoverPendingBatches and the journal manifest.
// Every case runs against a fresh writable test source under the scratch
// directory, never the ambient data root. This class is a friend of DataStore
// and DataWriteAuthorization so the tests can build authorizations and reach
// the test-only load path; nothing else should rely on that access.
class DataStoreSelfTests {
public:
	static bool RunSyntheticTests(const String& scratch_dir);

private:
	static bool TestWriteRecord(const String& scratch_dir);
	static bool TestPublishBatch(const String& scratch_dir);
	static bool TestPrepareJournalRoundTrip(const String& scratch_dir);
	static bool TestCrashRecovery(const String& scratch_dir, const String& name, DataWriteTestFailpoint failpoint);

	static DataWriteAuthorization MakeAuthorization();
	static ValueMap MakeRecord(const String& content, const String& revision);
	static String RecordPath(const String& root);
	static bool WriteFixture(const String& root, const String& content);
	static bool LoadStore(DataStore& store, const String& root);
	static String RecordContent(const String& root);
	static String RecordRevision(const String& root);
	static bool Check(bool ok, const char *what);
	static bool CheckStatus(const String& actual, const char *expected, const char *what);
	static bool RecoverOnce(DataStore& store, const DataWriteAuthorization& auth, const char *what);
};

END_UPP_NAMESPACE

#endif
