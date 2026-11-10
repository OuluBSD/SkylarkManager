#ifndef _TaskFoundry_TaskFoundryService_h_
#define _TaskFoundry_TaskFoundryService_h_

#include <Core/Core.h>
#include <WebBroker/WebBroker.h>
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// TaskFoundry: candidates -> proposals -> validation -> submission to Company.
//
// Records live in site-scoped JsonFileStore collections under data_root. Other
// sites are reached only through the broker: Science for paper and article
// evidence, PrivateHub for repository evidence, Company for the draft.
//
// Deny by default: every mutation needs taskfoundry:write, and each broker
// call needs the scope that the target site checks (science.read,
// privatehub:read, proposals:write, proposals:read). A submitted snapshot is
// written once; later edits change the live proposal, never the snapshot.
// ---------------------------------------------------------------------------

#define TASKFOUNDRY_SITE_ID        "taskfoundry"
#define TF_SCOPE_WRITE             "taskfoundry:write"
#define TF_SCOPE_SCIENCE_READ      "science.read"
#define TF_SCOPE_PRIVATEHUB_READ   "privatehub:read"
#define TF_SCOPE_COMPANY_WRITE     "proposals:write"
#define TF_SCOPE_COMPANY_READ      "proposals:read"

#define TF_STATUS_DRAFT      "draft"
#define TF_STATUS_VALIDATED  "validated"
#define TF_STATUS_SUBMITTED  "submitted"

// The authenticated caller and the scopes it holds.
struct TfActor {
	String         user_id;
	String         username;
	Vector<String> scopes;
};

struct TfResult : Moveable<TfResult> {
	bool     ok = false;
	int      status = 500;
	String   error;
	ValueMap payload;
};

class TaskFoundryService {
public:
	// broker must outlive the service. Records are stored under data_root.
	TaskFoundryService(WebBroker& broker, const String& data_root);

	TfResult CreateCandidate(const TfActor& a, const String& title, const String& summary,
	                         const String& paper_id);
	TfResult CreateProposal(const TfActor& a, const String& candidate_id);
	TfResult EditProposal(const TfActor& a, const String& proposal_id, const String& title,
	                      const String& body, const String& repository, double estimated_hours);
	// kind "science": ref is a paper id and node_id names a node of its article.
	// kind "privatehub": ref is an org/repo project; node_id is ignored.
	TfResult AttachEvidence(const TfActor& a, const String& proposal_id, const String& kind,
	                        const String& ref, const String& node_id);
	TfResult Validate(const TfActor& a, const String& proposal_id);
	TfResult Submit(const TfActor& a, const String& proposal_id);
	TfResult Status(const TfActor& a, const String& submission_id);

	// Reads for the board and editor pages. Routes require a session.
	TfResult Board();
	TfResult GetCandidate(const String& candidate_id);
	TfResult GetProposal(const String& proposal_id);

private:
	BrokerResult Ask(const TfActor& a, const String& to_site, const String& op, const ValueMap& payload,
	                 const String& idempotency_key = String());

	JsonFileStore candidates;
	JsonFileStore proposals;
	JsonFileStore submissions;
	WebBroker&    broker;
	Mutex         lock;   // serializes read-modify-write of the records
};

// Canonical content hash of a proposal's submittable fields and evidence.
String TaskFoundryContentHash(const ValueMap& proposal);

// Scopes a signed-in member holds. Interim policy: grants replace this later.
Vector<String> TaskFoundryMemberScopes();

// Synthetic checks for the service, run from the --test suite. scratch_dir is a
// temp directory the checks may create and delete.
bool RunTaskFoundrySelfTests(const String& scratch_dir);

END_UPP_NAMESPACE

#endif
