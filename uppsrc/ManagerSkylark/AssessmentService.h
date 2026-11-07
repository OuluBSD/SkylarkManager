#ifndef _ManagerSkylark_AssessmentService_h_
#define _ManagerSkylark_AssessmentService_h_

#include "DataStore.h"

NAMESPACE_UPP

// Pure bounded assessment projection. Inputs are caller-supplied ValueMaps; no
// store, filesystem, process, network or model access is performed here.
class AssessmentService {
public:
	static const int MAX_INPUT_ITEMS = 4096;
	static const int MAX_REFRESH_RECORDS = 64;
	static const int MAX_OVERRIDE_HISTORY = 512;
	struct OperationResult : Moveable<OperationResult> {
		String status;
		String operation_id;
		String run_id;
		Vector<String> changed_ids;
	};
	struct OverrideHistory : Moveable<OverrideHistory> {
		String status;
		String computed_value;
		Vector<ValueMap> events;
	};
	static ValueMap Project(const ValueMap& rubric, const ValueMap& inputs);
	static ValueArray BuildRecords(const ValueMap& projection);
	static OperationResult Refresh(const DataStore& pinned_current, const ValueMap& rubric,
	                               const ValueMap& inputs, const DataWriteAuthorization& authorization);
	static OperationResult Override(const DataStore& pinned_current, const ValueMap& rubric,
	                                const ValueMap& current_inputs,
	                                const String& workspace_id, const String& target_id, const String& expected_revision,
	                                const String& dimension_id, const String& replacement,
	                                const String& previous_override_id, const String& reason,
	                                const String& actor, const String& timestamp,
	                                const String& operation_key, const DataWriteAuthorization& authorization);
	static OverrideHistory GetOverrideHistory(const DataStore& pinned_current,
	                                          const ValueMap& rubric, const ValueMap& current_inputs,
	                                          const String& target_id, const String& expected_revision,
	                                          const String& dimension_id, const String& head_id);
	static bool RunSyntheticTests();
};

END_UPP_NAMESPACE

#endif
