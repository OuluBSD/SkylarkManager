#ifndef _ManagerSkylark_AssessmentView_h_
#define _ManagerSkylark_AssessmentView_h_

#include "ManagerSkylark.h"

NAMESPACE_UPP

class AssessmentViewService {
public:
	static ValueMap Build(const ManagerSkylarkApp& app, const CatalogGeneration& catalog,
	                      const String& workspace, const String& run_id,
	                      const String& filter, const String& sort, const String& direction,
	                      const String& dimension_id, const String& detail_id);
	static String Render(const ValueMap& view, const String& mode);
};

END_UPP_NAMESPACE

#endif
