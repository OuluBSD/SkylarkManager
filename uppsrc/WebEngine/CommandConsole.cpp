#include "CommandConsole.h"
#include "Manifest.h"

NAMESPACE_UPP

// The per-site command console (design 018, task 019): a form generator over
// the command manifest. It renders only what Describe already publishes; the
// forms post to the real routes, so every call runs as the signed-in user
// under the site's existing role, CSRF and origin fences.

static String Esc(const String& s)
{
	StringBuffer out;
	for(int i = 0; i < s.GetCount(); i++) {
		char c = s[i];
		switch(c) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(c);
		}
	}
	return String(out);
}

static String HiddenField(WebRequest& r)
{
	return "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + Esc(r.CsrfToken()) + "\">";
}

// A command's api_path carries no wildcard when the form can post straight
// to the route. Wildcard paths stay documentation rows: a plain HTML form
// cannot fill a path segment.
static bool RunnablePath(const String& api_path)
{
	return api_path.Find("*") < 0;
}

static void ConsolePage(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Redirect(r.Site().Url("session/login"));
		return;
	}
	ValueMap desc = TheWebEngine().Manifest().Describe(r.Site().id);
	StringBuffer b;
	b << "<p>Every command runs as your signed-in session, through the site's "
	     "own routes and fences. Commands with a wildcard path or a broker "
	     "transport are listed below — use skylarkctl for those.</p>";
	Value sites = desc.Get("sites");
	if(!sites.Is<ValueArray>()) {
		b << "<p>No commands are registered for this site.</p>";
		r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Command console</title></head>"
		       "<body><h1>Command console</h1>" + String(b) + "</body></html>");
		return;
	}
	for(const Value& sv : (ValueArray)sites) {
		ValueMap site = sv;
		ValueArray cmds;
		Value cv = site.Get("commands");
		if(cv.Is<ValueArray>())
			cmds = cv;
		if(cmds.GetCount() == 0)
			continue;
		b << "<table border=\"1\"><tr><th>Command</th><th>Run</th></tr>";
		for(const Value& v : cmds) {
			ValueMap c = v;
			String name = (String)c.Get("name", "");
			String method = (String)c.Get("method", "");
			String api_path = (String)c.Get("api_path", "");
			String transport = (String)c.Get("transport", "");
			String auth = (String)c.Get("auth", "");
			String response = (String)c.Get("response", "");
			String description = (String)c.Get("description", "");
			Vector<CommandDesc> unused;
			(void)unused;
			b << "<tr><td><b>" << Esc(name) << "</b><br><small>" << Esc(method) << " "
			  << Esc(api_path) << " · " << Esc(transport) << " · " << Esc(response)
			  << " · auth: " << Esc(auth) << "<br>" << Esc(description) << "</small></td><td>";
			if(transport != "http" || !RunnablePath(api_path)) {
				b << "<small>use skylarkctl</small></td></tr>";
				continue;
			}
			// The form posts straight to the route; the route's own fences are
			// the authority.
			ValueArray av;
			Value argv = c.Get("args");
			if(argv.Is<ValueArray>())
				av = argv;
			String fields = HiddenField(r);
			for(const Value& a : av) {
				ValueMap am = a;
				String an = (String)am.Get("name", "");
				String req = (bool)am.Get("required", false) ? " required" : "";
				fields << "<input name=\"" << Esc(an) << "\" placeholder=\"" << Esc(an)
				       << "\"" << req << "><br>";
			}
			String submit = method == "POST" ? "Run" : "Open";
			String m = method == "POST" ? "post" : "get";
			b << "<form method=\"" << m << "\" action=\"" << Esc(r.Site().Url(api_path)) << "\">"
			  << fields << "<input type=\"submit\" value=\"" << submit << "\"></form></td></tr>";
		}
		b << "</table>";
	}
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Command console</title></head>"
	       "<body><nav><a href=\"" + Esc(r.Site().Url("")) + "\">Home</a></nav>"
	       "<h1>Command console</h1>" + String(b) + "</body></html>");
}

void InstallCommandConsole(EngineSite& site)
{
	site.Router().Add("console", ConsolePage);
}

END_UPP_NAMESPACE
