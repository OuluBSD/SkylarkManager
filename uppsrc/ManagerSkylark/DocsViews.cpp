#include "ManagerSkylark.h"
#include <WebDocs/DocsPortal.h>

NAMESPACE_UPP

// The cross-project docs portal (design 020, task 021): a stock-app page over
// a fixed allowlist of local repositories. Host session gated by the app
// filter (test mode opens it); rendering is the markdown embed (escaped text in a textarea, converted client-side) —
// no renderer, no scripts from repository content.

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

static Vector<DocsRepo> DocsRepos()
{
	String spec = ManagerSkylarkApp::Get().DocsReposSpec();
	if(spec.IsEmpty())
		spec = DefaultDocsReposSpec();
	return ParseDocsRepos(spec);
}

SKYLARK(DocsPage, "docs")
{
	Vector<DocsRepo> repos = DocsRepos();
	StringBuffer b;
	if(repos.IsEmpty())
		b << "<p>No documentation repositories are configured (use --docs-repos).</p>";
	else {
		b << "<table border=\"1\"><tr><th>Repository</th><th>Doc files</th></tr>";
		for(const DocsRepo& r : repos) {
			int count = DocFileCount(r.root, 500);
			b << "<tr><td><a href=\"/docs/" << Esc(r.label) << "\">" << Esc(r.label) << "</a></td><td>"
			  << count << "</td></tr>";
		}
		b << "</table>";
	}
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Documentation", "docs", String(b), &http);
}

SKYLARK(DocsRepoPage, "docs/*")
{
	String label = http[0];
	Vector<DocsRepo> repos = DocsRepos();
	StringBuffer b;
	const DocsRepo* repo = NULL;
	for(const DocsRepo& r : repos)
		if(r.label == label) {
			repo = &r;
			break;
		}
	if(!repo) {
		http.Response(404, "Not found");
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Documentation", "docs", "<p>Unknown documentation repository.</p>", &http);
		return;
	}
	b << "<p>Root: <code>" << Esc(repo->root) << "</code></p><ul>";
	Vector<String> files = ListDocFiles(repo->root, 500);
	if(files.IsEmpty())
		b << "<li>No documentation files matched the mask.</li>";
	for(const String& f : files)
		b << "<li><a href=\"/docs/" << Esc(label) << "/" << Esc(f) << "\">" << Esc(f) << "</a></li>";
	b << "</ul>";
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Documentation: " + label, "docs", String(b), &http);
}

SKYLARK(DocsFilePage, "docs/*/**")
{
	String label = http[0];
	StringBuffer rel;
	for(int i = 1; i < http.GetParamCount(); i++) {
		if(i > 1)
			rel << "/";
		rel << http[i];
	}
	Vector<DocsRepo> repos = DocsRepos();
	String abs_path, error;
	StringBuffer b;
	if(!ResolveDocFile(repos, label, String(rel), abs_path, error)) {
		http.Response(404, "Not found");
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Documentation", "docs", "<p>" + Esc(error) + "</p>", &http);
		return;
	}
	String content = LoadFile(abs_path);
	if(AsString(http["raw"]) == "1") {
		http.ContentType("text/plain; charset=utf-8") << content;
		return;
	}
	b << "<p><a href=\"/docs/" << Esc(label) << "\">" << Esc(label) << "</a> / <code>" << Esc(String(rel))
	  << "</code> · <a href=\"/docs/" << Esc(label) << "/" << Esc(String(rel)) << "?raw=1\">raw</a></p>"
	  << "<div class=\"markdown-render-box\">"
	  << "<textarea class=\"raw-markdown\" style=\"display:none;\">" << Esc(content) << "</textarea>"
	  << "<div class=\"markdown-body\"></div></div>";
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Documentation: " + label, "docs", String(b), &http);
}

END_UPP_NAMESPACE
