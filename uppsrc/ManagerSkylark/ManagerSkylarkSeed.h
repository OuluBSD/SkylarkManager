#ifndef _ManagerSkylark_ManagerSkylarkSeed_h_
#define _ManagerSkylark_ManagerSkylarkSeed_h_

#include "ManagerSkylark.h"

NAMESPACE_UPP

// Demo and synthetic fixture builders, shared by app setup (InitApp) and the
// self-tests. Internal to the ManagerSkylark package.

ValueMap MakeDemoCandidate(const String& id, const String& source_identity = String());
ValueMap MakeDemoEnvelope(const String& kind, const String& workspace, const String& id,
	                              const String& visibility, const String& provenance, const String& version = "1");
ValueMap MakeDemoPolicyConfig();
bool SeedSyntheticAssessmentFiles(const String& site_dir, const String& workspace, const String& candidate, const String& evidence_id);

END_UPP_NAMESPACE

#endif
