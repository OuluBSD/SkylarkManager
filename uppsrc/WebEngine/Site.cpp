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

static thread__ String t_request_host;

void SetRequestHost(const String& host)
{
	t_request_host = host;
}

String RequestHost()
{
	return t_request_host;
}

// The authority ("host[:port]") of an http URL or origin, or "" for anything else.
static String AuthorityOf(const String& url)
{
	String u = TrimBoth(url);
	if(!ToLower(u).StartsWith("http://"))
		return String();
	u = u.Mid(7);
	int e = u.Find('/');
	return e < 0 ? u : u.Left(e);
}

// True for an IP literal or localhost: names an address the requester cannot
// rebind with DNS, so the Host header is trusted only for these.
static bool HostIsLiteralOrLocal(const String& authority)
{
	if(authority.IsEmpty())
		return false;
	if(authority[0] == '[')
		return true; // IPv6 literal
	String host = authority;
	int colon = host.Find(':');
	if(colon >= 0)
		host = host.Left(colon);
	if(CompareNoCase(host, "localhost") == 0)
		return true;
	if(host.IsEmpty())
		return false;
	for(int i = 0; i < host.GetCount(); i++)
		if(!IsDigit(host[i]) && host[i] != '.')
			return false;
	return true; // IPv4 literal
}

// The authority this request is served on. See OriginAllowed.
static String ServedAuthority(const String& declared_origin)
{
	String host = TrimBoth(RequestHost());
	if(HostIsLiteralOrLocal(host))
		return host;
	return AuthorityOf(declared_origin);
}

String EngineSite::Url(const String& pattern, const Vector<Value>& args) const
{
	String url = "http://" + ServedAuthority(external_origin);
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

bool EngineSite::OriginAllowed(const String& origin) const
{
	String served = ServedAuthority(external_origin);
	String asked = AuthorityOf(origin);
	return asked.GetCount() && CompareNoCase(asked, served) == 0;
}

RequestHostScope::RequestHostScope(const String& host)
{
	SetRequestHost(host);
}

RequestHostScope::~RequestHostScope()
{
	SetRequestHost(String());
}

END_UPP_NAMESPACE
