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
	trusted_proxy_peers.Clear();
	accounts.Create(id, root, session_ttl_hours);
	audit.Create(id, root);
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

static String SchemeOf(const String& url)
{
	String u = ToLower(TrimBoth(url));
	if(u.StartsWith("http://")) return "http";
	if(u.StartsWith("https://")) return "https";
	return String();
}

// The authority ("host[:port]") of an HTTP(S) URL or origin, or "" for anything else.
static String AuthorityOf(const String& url)
{
	String u = TrimBoth(url);
	String scheme = SchemeOf(u);
	if(scheme.IsEmpty())
		return String();
	u = u.Mid(scheme.GetCount() + 3);
	int e = u.Find('/');
	return e < 0 ? u : u.Left(e);
}

String EngineSite::Url(const String& pattern, const Vector<Value>& args) const
{
	String root = PublicUrl("/");
	if(root.IsEmpty())
		return String();
	String url = root.Left(root.GetCount() - 1);
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

String EngineSite::PublicUrl(const String& path) const
{
	String scheme = SchemeOf(external_origin);
	String authority = AuthorityOf(external_origin);
	if(scheme.IsEmpty() || authority.IsEmpty() || authority.Find('@') >= 0 ||
	   authority.Find('?') >= 0 || authority.Find('#') >= 0 || authority.Find('\\') >= 0)
		return String();
	for(int i = 0; i < authority.GetCount(); i++) {
		int c = (byte)authority[i];
		if(!(IsAlpha(c) || IsDigit(c) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']'))
			return String();
	}
	return scheme + "://" + authority + (path.StartsWith("/") ? path : "/" + path);
}

bool EngineSite::OriginAllowed(const String& origin) const
{
	if(PublicUrl("/").IsEmpty())
		return false;
	String configured = AuthorityOf(external_origin);
	String asked = AuthorityOf(origin);
	String expected_scheme = SchemeOf(external_origin);
	String asked_scheme = SchemeOf(origin);
	return asked.GetCount() && configured.GetCount() && expected_scheme.GetCount() &&
	       asked_scheme == expected_scheme && CompareNoCase(asked, configured) == 0;
}

END_UPP_NAMESPACE
