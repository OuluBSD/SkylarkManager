#ifndef _ManagerSkylark_ReviewDiscussion_h_
#define _ManagerSkylark_ReviewDiscussion_h_

#include "ManagerSkylark.h"

NAMESPACE_UPP

class ReviewDiscussionService {
public:
	static ValueMap AppendHumanRecord(const CatalogGeneration& catalog, ManagerSkylarkApp& app,
	                                  const UserSession& session, const String& workspace_id,
	                                  const String& package_id, const String& revision_id,
	                                  const String& thread_id, const String& body,
	                                  const String& record_type, const String& operation_id);
	static bool IsPrivateKind(const String& kind);
};

class ReviewPackageViewService {
public:
	static ValueMap Build(const ManagerSkylarkApp& app, const CatalogGeneration& catalog,
	                      const String& package_id = String(), const String& revision_id = String());
	static ValueMap BuildThread(const ManagerSkylarkApp& app, const CatalogGeneration& catalog,
	                            const String& thread_id);
	static String Render(const ValueMap& view, bool detail);
};

END_UPP_NAMESPACE

#endif
