#include "Site.h"

NAMESPACE_UPP

void EngineSite::Init(const String& site_id, const String& site_title, const String& origin,
                      const String& root, int ttl)
{
	for(int i = 0; i < site_id.GetCount(); i++) {
		int c = site_id[i];
		ASSERT_(IsAlpha(c) || IsDigit(c) || c == '_' || c == '-',
		        "site id must be [A-Za-z0-9_-]: " + site_id);
	}
	ASSERT_(origin.GetCount(), "site needs an external origin");
	id = site_id;
	title = site_title;
	external_origin = origin;
	data_root = root;
	session_ttl_hours = ttl > 0 ? ttl : 12;
	accounts.Create(id, root, session_ttl_hours);
}

SiteAccounts& EngineSite::Accounts()
{
	ASSERT(accounts);
	return *accounts;
}

String EngineSite::CookieName() const
{
	ASSERT(accounts);
	return accounts->CookieName();
}

String EngineSite::Url(const String& pattern, const Vector<Value>& args) const
{
	String url = external_origin;
	int q = pattern.Find(':');
	String p = q >= 0 ? pattern.Mid(0, q) : pattern;
	Vector<String> parts;
	for(const String& s : Split(p, '/'))
		parts.Add(s);
	int ai = 0;
	for(const String& s : parts) {
		url << '/';
		if(s == "*")
			url << (String)args[ai++];
		else if(s == "**")
			while(ai < args.GetCount())
				url << (String)args[ai++];
		else
			url << s;
	}
	return url;
}

bool EngineSite::OriginAllowed(const String& authority) const
{
	return CompareNoCase(TrimBoth(authority), external_origin) == 0;
}

END_UPP_NAMESPACE
