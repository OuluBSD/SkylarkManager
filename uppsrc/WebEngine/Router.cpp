#include "Router.h"

NAMESPACE_UPP

void SiteRouter::Add(const char *method, const char *pattern, Callback1<WebRequest&> handler)
{
	String m = method && *method ? String(method) : String("GET");
	String p = pattern;
	int q = p.Find(':');
	if(q >= 0) { // allow "pattern:POST" convenience
		m = p.Mid(q + 1);
		p = p.Mid(0, q);
	}
	if(m == "POST")
		m = "POST";
	for(const SiteRoute& r : routes)
		ASSERT_(!(r.pattern == p && r.method == m),
		        Format("duplicate site route %s %s", ~m, ~p));
	SiteRoute& r = routes.Add();
	r.method = m;
	r.pattern = p;
	r.handler = handler;
}

void SiteRouter::Add(const char *method, const char *pattern, void (*fn)(WebRequest&))
{
	ASSERT_(fn, "site route needs a handler");
	Add(method, pattern, Callback1<WebRequest&>(fn, 0));
}

static void SplitPattern(const String& pattern, Vector<String>& parts)
{
	for(const String& s : Split(pattern, '/'))
		parts.Add(s);
}

bool SiteRouter::Match(const String& method, const Vector<String>& segments,
                       Vector<String>& args, SiteRoute& out) const
{
	String m = method;
	if(m == "HEAD" || m == "OPTIONS") {
		// fall through to exact match first; caller-side HEAD/OPTIONS routes
		// win, otherwise GET handlers serve HEAD.
	}
	bool want_fallback = (m == "HEAD" || m == "OPTIONS");
	int best_score = -1;
	bool found = false;
	for(const SiteRoute& r : routes) {
		bool method_ok = (r.method == m) || (want_fallback && r.method == "GET");
		if(!method_ok)
			continue;
		Vector<String> pp;
		SplitPattern(r.pattern, pp);
		bool varargs = pp.GetCount() && pp.Top() == "**";
		int fixed = varargs ? pp.GetCount() - 1 : pp.GetCount();
		if(!varargs && pp.GetCount() != segments.GetCount())
			continue;
		if(varargs && segments.GetCount() < fixed)
			continue;
		int literals = 0, wildcards = 0;
		Vector<String> rargs;
		bool ok = true;
		for(int i = 0; i < fixed; i++) {
			const String& pat = pp[i];
			if(pat == "*") {
				wildcards++;
				rargs.Add(segments[i]);
			}
			else if(pat == segments[i])
				literals++;
			else {
				ok = false;
				break;
			}
		}
		if(!ok)
			continue;
		if(varargs)
			for(int i = fixed; i < segments.GetCount(); i++)
				rargs.Add(segments[i]);
		int score = literals * 1000 + segments.GetCount() * 10 - wildcards;
		if(score > best_score) {
			best_score = score;
			out = r;
			args <<= rargs;
			found = true;
		}
	}
	return found;
}

END_UPP_NAMESPACE
