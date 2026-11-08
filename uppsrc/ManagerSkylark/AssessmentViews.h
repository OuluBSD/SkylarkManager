#ifndef _ManagerSkylark_AssessmentViews_h_
#define _ManagerSkylark_AssessmentViews_h_

#include "AssessmentService.h"

NAMESPACE_UPP

class ManagerSkylarkApp;

class AssessmentViewService {
public:
	static ValueMap Build(const ManagerSkylarkApp& app, const String& workspace_id = String());
	static String Render(const ValueMap& view);
	static bool RunSyntheticTests();
};

END_UPP_NAMESPACE

#endif
