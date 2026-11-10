#ifndef _reference_ManagerSkylark_PrivateHubService_h_
#define _reference_ManagerSkylark_PrivateHubService_h_

#include <Core/Core.h>
#include <WebBroker/WebBroker.h>
#include <PrivateHubWorkspace/PrivateHubWorkspace.h>

NAMESPACE_UPP

#define PRIVATEHUB_SCOPE_READ   "privatehub:read"
#define PRIVATEHUB_SCOPE_WRITE  "privatehub:write"

// A recorded write result is replayed for this long. After that the same key runs
// the write again, so a retry after the window is a new write.
#define PRIVATEHUB_IDEMPOTENCY_TTL (30 * 24 * 3600)

// Holds the exclusive owner lock on a data root for as long as it lives. One process
// may own a data root; a second owner is refused, so the in-process mutex is the only
// serialization the records need.
class DataRootOwner {
public:
	DataRootOwner();
	~DataRootOwner();

	bool Acquire(const String& data_root, String& error);
	bool IsHeld() const { return held; }

private:
	void Release();
#ifdef PLATFORM_WIN32
	void* handle;
#else
	int   fd;
#endif
	bool  held;
};

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
// the same key within the TTL returns the recorded result without running
// again. A record older than the TTL is expired: the key runs the write again.
// Failed writes are not recorded, so they can be retried.
//
// Single owner: the data root is owned by one process (DataRootOwner), so the
// in-process mutex below is the only serialization the records need.
class PrivateHubService : public IBrokerService {
public:
	// backend must outlive the service. data_root holds the idempotency records.
	PrivateHubService(PrivateHubWorkspaceBackend& backend, const String& data_root,
	                  int64 idempotency_ttl_seconds = PRIVATEHUB_IDEMPOTENCY_TTL);

	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override;

	// Takes the owner lock on data_root. Refused when another process holds it.
	bool AcquireDataRoot(String& error);

private:
	BrokerResult Execute(const BrokerCallContext& ctx, const BrokerCall& call);
	// found is true when a live record replays. rev is the stored revision of an
	// expired record, so Record can overwrite it.
	BrokerResult Recorded(const BrokerCallContext& ctx, const BrokerCall& call, bool& found, String& rev);
	void Record(const BrokerCallContext& ctx, const BrokerCall& call, const BrokerResult& res, const String& rev);

	PrivateHubWorkspaceBackend& backend;
	String data_root;
	int64  ttl;
	DataRootOwner owner;
	Mutex  lock;   // serializes writes and their idempotency records in this process
};

// Registers a service for site_id on the broker. The broker takes ownership of
// the service. The backend and the broker must outlive it. Returns false, with
// error set, when another process already owns data_root; nothing is registered.
bool RegisterPrivateHubService(WebBroker& broker, const String& site_id,
                               PrivateHubWorkspaceBackend& backend, const String& data_root,
                               String& error, int64 idempotency_ttl_seconds = PRIVATEHUB_IDEMPOTENCY_TTL);

// Synthetic checks for the service, run from the --test suite. scratch_dir is
// a temp directory the checks may create.
bool RunPrivateHubServiceSelfTests(const String& scratch_dir);

END_UPP_NAMESPACE

#endif
