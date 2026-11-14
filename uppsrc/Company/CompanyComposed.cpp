#include "CompanyComposed.h"

NAMESPACE_UPP

bool CompanyComposedModule::Open(const String& root, const Vector<String>& repositories, String& error)
{
	store.SetRoot(root);
	if(!store.Initialize(error))
		return false;
	return store.SetRepositoryAllowlist(repositories, error);
}

CompanyBrokerResult CompanyComposedModule::Handle(const String& op, const Vector<String>& scopes,
                                                  const String& caller_id, const ValueMap& payload)
{
	return broker.Handle(op, scopes, caller_id, payload);
}

END_UPP_NAMESPACE
