#ifndef _ManagerSkylark_CandidateWorkflow_h_
#define _ManagerSkylark_CandidateWorkflow_h_

#include "DataStore.h"
#include "SystemAuth.h"

NAMESPACE_UPP

class CandidateWorkflow {
public:
	static const int MAX_TEXT_BYTES = 4096;
	static ValueMap ValidateProposal(const DataStore& store, const ValueMap& input,
	                                 const String& workspace_id, const class ManagerSkylarkApp& app);
	static ValueMap Project(const DataStore& store, const class ManagerSkylarkApp& app,
	                       const String& workspace_id, const String& candidate_id);
	static ValueMap CreateProposal(const DataStore& store, const class ManagerSkylarkApp& app,
	                               const ValueMap& input, const String& workspace_id,
	                               const String& source_id, const String& operation_id,
	                               const UserSession& session);
	static ValueMap ReviseProposal(const DataStore& store, const class ManagerSkylarkApp& app,
	                               const String& workspace_id, const String& candidate_id,
	                               const String& proposal_id, const String& expected_head_revision,
	                               const String& expected_revision_id, const ValueMap& input,
	                               const String& source_id, const String& operation_id,
	                               const UserSession& session);
	static ValueMap AdvanceStage(const DataStore& store, const class ManagerSkylarkApp& app,
	                             const String& workspace_id, const String& candidate_id,
	                             const String& proposal_revision_id,
	                             const String& expected_candidate_revision,
	                             const String& expected_head_revision, const String& expected_head_event,
	                             const String& next_stage, const String& reason,
	                             const ValueArray& evidence_ids, const String& source_id,
	                             const String& operation_id, const UserSession& session,
	                             const String& review_outcome = String());
	static String Render(const ValueMap& projection);
	static bool HasCurrentProposalAssessment(const DataStore& store, const class ManagerSkylarkApp& app,
	                                        const String& workspace_id, const String& candidate_id,
	                                        const ValueMap& current_proposal);
	static String ComparisonAssessmentBlocker(const DataStore& store, const class ManagerSkylarkApp& app,
	                                        const String& workspace_id, const String& candidate_id,
	                                        const ValueMap& current_proposal);
	static bool HasLinkedCommitDiff(const DataStore& store, const class ManagerSkylarkApp& app,
	                                const String& workspace_id, const ValueMap& proposal);
	static ValueMap CheckStagePrerequisite(const DataStore& store, const class ManagerSkylarkApp& app,
	                                       const String& workspace_id, const String& stage,
	                                       const ValueMap& current_proposal, const ValueArray& evidence_ids,
	                                       const String& review_outcome, bool projection = false);
	static bool RunSyntheticTests();
};

END_UPP_NAMESPACE

#endif
