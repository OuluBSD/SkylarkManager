#include "PrivateHubSite.h"

NAMESPACE_UPP

static PrivateHubWorkspaceBackend *s_backend = NULL;

static PrivateHubWorkspaceBackend& Backend()
{
	ASSERT_(s_backend, "privatehub site not installed");
	return *s_backend;
}

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

static void HtmlPage(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\">"
	       "<title>" + Esc(title) + "</title></head><body>"
	       "<h1>" + Esc(title) + "</h1>" + body + "</body></html>");
}

// Workspace overview: organizations and their repositories on this machine.
static void PrivateHubHome(WebRequest& r)
{
	Vector<String> orgs;
	StringBuffer body;
	if(!Backend().ListOrganizations(orgs)) {
		HtmlPage(r, "PrivateHub", "<p>Workspace root is not readable.</p>");
		return;
	}
	body << "<ul>";
	for(const String& org : orgs) {
		Vector<String> repos;
		Backend().ListRepositories(org, repos);
		body << "<li>" << Esc(org) << "<ul>";
		for(const String& repo : repos)
			body << "<li>" << Esc(repo) << "</li>";
		body << "</ul></li>";
	}
	body << "</ul>";
	HtmlPage(r, "PrivateHub", String(body));
}

void InstallPrivateHubSite(EngineSite& site, PrivateHubWorkspaceBackend& backend)
{
	s_backend = &backend;
	site.Router().Add("", PrivateHubHome);
}

END_UPP_NAMESPACE
