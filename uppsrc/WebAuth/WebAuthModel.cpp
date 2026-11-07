#include "WebAuth.h"

NAMESPACE_UPP

bool WebAuthIdentity::HasRole(const String& role) const
{
	for(int i = 0; i < roles.GetCount(); i++)
		if(roles[i] == role)
			return true;
	return false;
}

bool WebAuthIdentity::HasAnyRole(const Vector<String>& roles_) const
{
	for(int i = 0; i < roles_.GetCount(); i++)
		if(HasRole(roles_[i]))
			return true;
	return false;
}

bool WebAuthIdentity::HasScope(const String& scope) const
{
	for(int i = 0; i < scopes.GetCount(); i++)
		if(scopes[i] == scope)
			return true;
	return false;
}

END_UPP_NAMESPACE
