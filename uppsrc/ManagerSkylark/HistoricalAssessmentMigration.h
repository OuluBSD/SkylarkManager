#ifndef _ManagerSkylark_HistoricalAssessmentMigration_h_
#define _ManagerSkylark_HistoricalAssessmentMigration_h_

#include "DataStore.h"

NAMESPACE_UPP

struct HistoricalAssessmentMigration {
	static const int MAX_SNAPSHOT_BYTES = 1024 * 1024;
	static const int MAX_ASSERTIONS = 256;

	// Denied previews contain status only; they never return source assertions.
	static ValueMap Preview(const Value& snapshot, const String& workspace_id,
	                        const String& source_id, const DataWriteAuthorization& authorization,
	                        const DataStore& destination);
	static String OperationId(const Value& snapshot, const String& workspace_id, const String& source_id);
	static DataBatchResult Import(const Value& snapshot, const String& workspace_id,
	                              const String& source_id, const String& operation_id,
	                              const DataWriteAuthorization& authorization,
	                              const DataStore& destination);
	static bool RunSyntheticTests(const DataWriteAuthorization& authorization,
	                              const DataWriteAuthorization& denied_authorization);
};

END_UPP_NAMESPACE

#endif
