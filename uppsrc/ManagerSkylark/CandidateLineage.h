#ifndef _ManagerSkylark_CandidateLineage_h_
#define _ManagerSkylark_CandidateLineage_h_

#include <Core/Core.h>
#include "DataStore.h"

NAMESPACE_UPP

class ManagerSkylarkApp;

class CandidateLineage {
public:
	static const int MAX_RECORDS = 256;
	static const int MAX_RELATIONSHIPS = 1024;
	static const int MAX_TEXT_BYTES = 4096;
	static bool ValidateRecord(const ValueMap& record, String& error);
	static ValueArray BuildRecords(const ValueMap& projection);
	// The caller pins the catalog for the duration of this pure projection.
	static ValueMap Project(const DataStore& pinned_store, const ManagerSkylarkApp& app,
	                        const String& workspace_id);
	static bool RunSyntheticTests();
};

END_UPP_NAMESPACE

#endif
