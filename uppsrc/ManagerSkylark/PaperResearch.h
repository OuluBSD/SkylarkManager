#ifndef _ManagerSkylark_PaperResearch_h_
#define _ManagerSkylark_PaperResearch_h_

#include "CandidateWorkflow.h"

NAMESPACE_UPP

struct PaperResearchClaim : Moveable<PaperResearchClaim> {
	String title, purpose, quality_contract, inputs, outputs, runtime_resources, assets, verification_plan;
	String uncertainty;
	Vector<String> evidence_ids, counterevidence_ids;
	bool supported = false;
	PaperResearchClaim() {}
	PaperResearchClaim(const PaperResearchClaim& o) : title(o.title), purpose(o.purpose), quality_contract(o.quality_contract), inputs(o.inputs), outputs(o.outputs), runtime_resources(o.runtime_resources), assets(o.assets), verification_plan(o.verification_plan), uncertainty(o.uncertainty), evidence_ids(clone(o.evidence_ids)), counterevidence_ids(clone(o.counterevidence_ids)), supported(o.supported) {}
	PaperResearchClaim& operator=(const PaperResearchClaim& o) { if(this!=&o) { title=o.title; purpose=o.purpose; quality_contract=o.quality_contract; inputs=o.inputs; outputs=o.outputs; runtime_resources=o.runtime_resources; assets=o.assets; verification_plan=o.verification_plan; uncertainty=o.uncertainty; evidence_ids=clone(o.evidence_ids); counterevidence_ids=clone(o.counterevidence_ids); supported=o.supported; } return *this; }
};

class PaperCandidateGenerator {
public:
	virtual ~PaperCandidateGenerator() {}
	virtual bool Generate(const ValueMap& pinned_input, Vector<PaperResearchClaim>& claims) = 0;
	virtual String GetGeneratorId() const { return "injected-deterministic"; }
	virtual String GetGeneratorRevision() const { return "1"; }
	virtual String GetProviderConfigRevision() const { return String(); }
	virtual String GetRubricId() const { return "paper-claim-support"; }
	virtual String GetRubricRevision() const { return "1"; }
};

class PaperResearch {
public:
	// A null generator is the production default; no analyzer is installed.
	static ValueMap Run(const DataStore& store, const ManagerSkylarkApp& app,
	                    const String& workspace_id, const String& paper_snapshot_id,
	                    const String& paper_revision, const String& extraction_sha256,
	                    const ValueArray& evidence_ids, const String& provider_id,
	                    PaperCandidateGenerator *generator = NULL);
	static ValueMap PersistRun(const ValueMap& plan, const DataStore& store,
	                           const DataWriteAuthorization& authorization,
	                           const String& operation_id);
	static ValueMap Handoff(const DataStore& store, ManagerSkylarkApp& app,
	                        const String& workspace_id, const String& source_id,
	                        const String& run_id, const UserSession& session);
};

END_UPP_NAMESPACE

#endif
