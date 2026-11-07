#ifndef _ManagerSkylark_JobService_h_
#define _ManagerSkylark_JobService_h_

#include "ManagerSkylark.h"

NAMESPACE_UPP

struct JobExecutionRequest : Moveable<JobExecutionRequest> {
	ValueMap job;
	ValueArray inputs;
	Time deadline_at;
	int64 runtime_limit_ms = 0;
	int64 input_limit_bytes = 0;
	int64 output_limit_bytes = 0;
};

struct JobExecutionResult : Moveable<JobExecutionResult> {
	String status = "failed";
	String output;
	String failure_class;
	int64 elapsed_ms = 0;
	bool retryable = false;
	bool idempotency_safe = false;
};

class JobExecutor {
public:
	virtual ~JobExecutor() {}
	// Adapters must enforce deadline_at/runtime_limit_ms and stop output growth at output_limit_bytes.
	virtual JobExecutionResult Execute(const JobExecutionRequest& request) = 0;
};

class JobService {
public:
	static const int MAX_REQUEST_BYTES = 256 * 1024;
	static const int MAX_OUTPUT_BYTES = 256 * 1024;
	static const int MAX_INPUTS = 32;
	static const int MAX_ATTEMPTS = 8;
	struct Result : Moveable<Result> { String status, job_id; };
	static Result Submit(ManagerSkylarkApp& app, const DataStore& pinned, const DataWriteAuthorization& auth,
	                     const String& workspace_id, const String& operation_key, const ValueMap& request, const String& timestamp);
	static Result Cancel(ManagerSkylarkApp& app, const DataStore& pinned, const DataWriteAuthorization& auth,
	                     const String& workspace_id, const String& job_id, const String& expected_revision, const String& timestamp);
	static Result Retry(ManagerSkylarkApp& app, const DataStore& pinned, const DataWriteAuthorization& auth,
	                    const String& workspace_id, const String& job_id, const String& expected_revision, const String& timestamp);
	static Result RunOne(ManagerSkylarkApp& app, const DataStore& pinned, const DataWriteAuthorization& auth,
	                    const String& workspace_id, const String& job_id, const String& expected_revision, const String& timestamp, JobExecutor& executor);
	static Result RunOneForTests(ManagerSkylarkApp& app, const DataStore& pinned, const DataWriteAuthorization& auth,
	                            const String& workspace_id, const String& job_id, const String& expected_revision,
	                            const String& timestamp, JobExecutor& executor, void (*before_dispatch_recheck)(void *), void *context);
	static int RecoverRunning(ManagerSkylarkApp& app, const DataStore& pinned, const DataWriteAuthorization& auth,
	                         const String& workspace_id, const String& timestamp);
	static bool IsProviderConfigCurrent(const ManagerSkylarkApp& app, const String& workspace_id,
	                                   const String& provider_id, const Vector<String>& source_identity_ids,
	                                   const String& expected_config_revision);
	static bool RunSyntheticTests();

private:
	static bool ResolveJobPolicy(ManagerSkylarkApp& app, const DataStore& store, const String& workspace_id,
	                             const ValueArray& pins, const String& provider_id,
	                             const String& requested_config_revision, ValueArray& contributors,
	                             String& trusted_config_revision, ValueMap& snapshot);
	static Result RunOneInternal(ManagerSkylarkApp& app, const DataStore& pinned, const DataWriteAuthorization& auth,
	                            const String& workspace_id, const String& job_id, const String& expected_revision,
	                            const String& timestamp, JobExecutor& executor,
	                            void (*before_dispatch_recheck)(void *), void *context);
};

END_UPP_NAMESPACE
#endif
