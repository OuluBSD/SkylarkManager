#ifndef _ManagerSkylark_EvidenceIngestion_h_
#define _ManagerSkylark_EvidenceIngestion_h_

#include "DataStore.h"
#include "ScopedPolicy.h"

NAMESPACE_UPP

enum EvidenceSourceKind { EVIDENCE_PR_REVIEW, EVIDENCE_COMMIT_DIFF, EVIDENCE_MESSAGE_THREAD, EVIDENCE_PAPER };

struct EvidenceLocator : Moveable<EvidenceLocator> {
	String type;
	String id;
	String path;
	int first = 0;
	int last = 0;
	String section;
	ValueMap ToValue() const;
};

struct EvidencePinnedLocation : Moveable<EvidencePinnedLocation> {
	EvidenceLocator locator;
	String content;
	// Stable local trusted source identity; distinct from external locator IDs.
	String source_identity;
	String upstream_revision;
};

struct EvidenceCapture : Moveable<EvidenceCapture> {
	EvidenceSourceKind kind = EVIDENCE_PR_REVIEW;
	String workspace_id;
	String source_identity;
	String upstream_revision;
	String previous_revision;
	Vector<EvidencePinnedLocation> pinned_locations;
	String source_at;
	String captured_at;
	String collector;
	EvidenceLocator locator;
	Vector<String> policy_source_ids;
	bool retracted = false;
	EvidenceCapture() {}
	EvidenceCapture(const EvidenceCapture& o) : kind(o.kind), workspace_id(o.workspace_id), source_identity(o.source_identity), upstream_revision(o.upstream_revision), previous_revision(o.previous_revision), pinned_locations(clone(o.pinned_locations)), source_at(o.source_at), captured_at(o.captured_at), collector(o.collector), locator(o.locator), policy_source_ids(clone(o.policy_source_ids)), retracted(o.retracted) {}
	EvidenceCapture& operator=(const EvidenceCapture& o) {
		if(this != &o) { kind=o.kind; workspace_id=o.workspace_id; source_identity=o.source_identity; upstream_revision=o.upstream_revision; previous_revision=o.previous_revision; pinned_locations=clone(o.pinned_locations); source_at=o.source_at; captured_at=o.captured_at; collector=o.collector; locator=o.locator; policy_source_ids=clone(o.policy_source_ids); retracted=o.retracted; }
		return *this;
	}
};

// source_identity is the stable local trusted source identity, not an external ID
// such as a commit hash, review ID, message ID, or paper ID.
struct EvidencePrReviewEntry : Moveable<EvidencePrReviewEntry> { String id, revision, content, source_identity, upstream_revision; };
struct EvidenceCommitLineEntry : Moveable<EvidenceCommitLineEntry> { String commit_id, path, content, source_identity, upstream_revision; int first = 0, last = 0; };
struct EvidenceMessageEntry : Moveable<EvidenceMessageEntry> { String message_id, thread_id, content, source_identity, upstream_revision; };
struct EvidencePaperSectionEntry : Moveable<EvidencePaperSectionEntry> { String paper_id, section, content, source_identity, upstream_revision; int page = 0; };
struct EvidencePrReviewValue : Moveable<EvidencePrReviewValue> { EvidenceCapture capture; Vector<EvidencePrReviewEntry> reviews; };
struct EvidenceCommitDiffValue : Moveable<EvidenceCommitDiffValue> { EvidenceCapture capture; Vector<EvidenceCommitLineEntry> lines; };
struct EvidenceMessageThreadValue : Moveable<EvidenceMessageThreadValue> { EvidenceCapture capture; Vector<EvidenceMessageEntry> messages; };
struct EvidencePaperValue : Moveable<EvidencePaperValue> { EvidenceCapture capture; Vector<EvidencePaperSectionEntry> sections; };

struct EvidencePreview : Moveable<EvidencePreview> {
	String status;
	String operation_id;
	String snapshot_id;
	String predecessor_id;
	String content_sha256;
	String resolution;
	String classification;
	String visibility;
	bool duplicate = false;
	ValueMap snapshot;
	ValueMap evidence;
};

class EvidenceIngestion {
public:
	static const int MAX_CONTENT_BYTES = 256 * 1024;
	static const int MAX_LOCATIONS = 1024;
	static EvidencePreview PrepareImport(const DataStore& store, const TrustedPolicyRegistry& policy, const EvidencePrReviewValue& value, const DataWriteAuthorization& authorization);
	static EvidencePreview PrepareImport(const DataStore& store, const TrustedPolicyRegistry& policy, const EvidenceCommitDiffValue& value, const DataWriteAuthorization& authorization);
	static EvidencePreview PrepareImport(const DataStore& store, const TrustedPolicyRegistry& policy, const EvidenceMessageThreadValue& value, const DataWriteAuthorization& authorization);
	static EvidencePreview PrepareImport(const DataStore& store, const TrustedPolicyRegistry& policy, const EvidencePaperValue& value, const DataWriteAuthorization& authorization);
	static DataBatchResult Import(const DataStore& store, const TrustedPolicyRegistry& policy, const EvidencePrReviewValue& value, const DataWriteAuthorization& authorization);
	static DataBatchResult Import(const DataStore& store, const TrustedPolicyRegistry& policy, const EvidenceCommitDiffValue& value, const DataWriteAuthorization& authorization);
	static DataBatchResult Import(const DataStore& store, const TrustedPolicyRegistry& policy, const EvidenceMessageThreadValue& value, const DataWriteAuthorization& authorization);
	static DataBatchResult Import(const DataStore& store, const TrustedPolicyRegistry& policy, const EvidencePaperValue& value, const DataWriteAuthorization& authorization);
	static bool RunSyntheticTests(const DataWriteAuthorization& authorization, const DataWriteAuthorization& wrong_workspace_authorization);
};

END_UPP_NAMESPACE

#endif
