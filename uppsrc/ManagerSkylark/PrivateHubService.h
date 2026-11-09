#ifndef _reference_ManagerSkylark_PrivateHubService_h_
#define _reference_ManagerSkylark_PrivateHubService_h_

#include <Core/Core.h>
#include <WebBroker/WebBroker.h>
#include <PrivateHubWorkspace/PrivateHubWorkspace.h>

NAMESPACE_UPP

#define PRIVATEHUB_SCOPE_READ   "privatehub:read"
#define PRIVATEHUB_SCOPE_WRITE  "privatehub:write"

// Broker surface over PrivateHubWorkspaceBackend. Operations and payload keys:
//   issue.list     [project]
//   issue.get      [project, issue_id]
//   issue.create   [project, title, body]           write
//   issue.comment  [project, issue_id, body]        write
//   wiki.list      [project]        wiki.get     [project, slug]
//   topic.list     [project]        topic.get    [project, slug]
//   post.list      [project, topic]
//   file.list      [project]        file.get     [project, name]
//
// Deny by default. Unknown ops are 404. Reads need privatehub:read; writes
// need privatehub:write, a caller_id, and an idempotency_key. The author of a
// write is always the caller_id; a payload author is ignored. A successful
// write is recorded under (caller_id, op, idempotency_key), and a repeat with
// the same key returns the recorded result without running again. Failed
// writes are not recorded, so they can be retried.
class PrivateHubService : public IBrokerService {
public:
	// backend must outlive the service. data_root holds the idempotency records.
	PrivateHubService(PrivateHubWorkspaceBackend& backend, const String& data_root);

	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override;

private:
	BrokerResult Execute(const BrokerCallContext& ctx, const BrokerCall& call);
	BrokerResult Recorded(const BrokerCallContext& ctx, const BrokerCall& call, bool& found);
	void Record(const BrokerCallContext& ctx, const BrokerCall& call, const BrokerResult& res);

	PrivateHubWorkspaceBackend& backend;
	String data_root;
	Mutex  lock;   // serializes writes and their idempotency records in this process
};

// Registers a service for site_id on the broker. The broker takes ownership of
// the service. The backend and the broker must outlive it.
void RegisterPrivateHubService(WebBroker& broker, const String& site_id,
                               PrivateHubWorkspaceBackend& backend, const String& data_root);

// Synthetic checks for the service, run from the --test suite. scratch_dir is
// a temp directory the checks may create.
bool RunPrivateHubServiceSelfTests(const String& scratch_dir);

END_UPP_NAMESPACE

#endif
