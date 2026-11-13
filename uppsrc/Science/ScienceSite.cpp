#include "ScienceSite.h"

NAMESPACE_UPP

static ScienceCorpus *s_corpus = NULL;
static WebGrants     *s_grants = NULL;

static ScienceCorpus& Corpus()
{
	ASSERT_(s_corpus, "science site not installed");
	return *s_corpus;
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

// Deny by default. Authoring needs either a session user holding the Science
// author role, or an active grant that carries science.author. The grant path
// is bearer-style: whoever presents the grant id acts for its subject, so
// grants must only be handed to callers the consent flow approved.
// Caller binding: a grant authorizes only the authenticated session of its
// subject user. A grant id presented by anyone else, or without a session, is
// refused, so possessing the id alone is never enough.
static bool MayAuthor(WebRequest& r)
{
	if(r.HasUser())
		for(const String& role : r.User().roles)
			if(role == SCIENCE_AUTHOR_ROLE)
				return true;
	String grant_id = r.Header(SCIENCE_GRANT_HEADER);
	if(grant_id.IsEmpty() || !s_grants || !r.HasUser())
		return false;
	WebAccountGrant g;
	if(!s_grants->GetGrant(grant_id, g) || !g.IsActive())
		return false;
	if(g.subject_user_id != r.User().user_id)
		return false;
	return g.HasScopes(Vector<String>() << String(SCIENCE_AUTHOR_SCOPE));
}

// The composed-site palette shared with Atlas (task 038: Science had no stylesheet).
static String ScienceStylesheet()
{
	return R"SCIENCE(<style>
:root { --bg: #f7f7f5; --fg: #1b1b1b; --muted: #5c5c5c; --line: #d0d0cc; --panel: #ffffff; --link: #1864ab; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --bg: #151515; --fg: #ececec; --muted: #a0a0a0; --line: #3a3a3a; --panel: #1f1f1f; --link: #74c0fc; }
}
:root[data-theme="dark"] { --bg: #151515; --fg: #ececec; --muted: #a0a0a0; --line: #3a3a3a; --panel: #1f1f1f; --link: #74c0fc; }
body { background: var(--bg); color: var(--fg); font: 15px/1.45 system-ui, sans-serif; margin: 0; padding: 16px; max-width: 1100px; }
a { color: var(--link); }
h1 { font-size: 1.5rem; margin: 8px 0; }
li { margin: 4px 0; }
</style>)SCIENCE";
}

static void HtmlPage(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
	       "<title>" + title + "</title>" + ScienceStylesheet() + "</head><body>"
	       "<header>" + SessionChromeHtml(r) + "</header>"
	       "<h1>" + title + "</h1>" + body + "</body></html>");
}

static ValueMap JsonOk(bool ok, const String& err = String())
{
	ValueMap m;
	m.Add("ok", ok);
	if(err.GetCount())
		m.Add("error", err);
	return m;
}

static void ScienceHome(WebRequest& r)
{
	Vector<SciencePaper> papers;
	Corpus().ListPapers(papers);
	int with_article = 0;
	for(const SciencePaper& p : papers)
		if(p.has_article)
			with_article++;
	HtmlPage(r, "Science",
		"<p>" + AsString(papers.GetCount()) + " papers, "
		+ AsString(with_article) + " with structured article.</p>"
		+ "<p><a href=\"" + r.Site().Url("papers") + "\">Browse papers</a></p>");
}

static void SciencePaperList(WebRequest& r)
{
	Vector<SciencePaper> papers;
	Corpus().ListPapers(papers);
	StringBuffer body;
	body << "<ul>";
	for(const SciencePaper& p : papers) {
		body << "<li><a href=\"" << r.Site().Url("papers/*", {p.id}) << "\">"
		     << (p.title.IsEmpty() ? p.id : p.title) << "</a>"
		     << (p.has_article ? String(" [article]") : String("")) << "</li>";
	}
	body << "</ul>";
	HtmlPage(r, "Papers", String(body));
}

static String RenderArticleHtml(const ScienceArticleTree& tree)
{
	StringBuffer body;
	for(const ScienceNode& n : tree.nodes) {
		if(n.type == "section")
			body << "<h2>" << n.heading << "</h2>";
		else if(n.type == "paragraph")
			body << "<p>" << n.text << "</p>";
		else if(n.type == "equation")
			body << "<p><code>" << n.latex << "</code></p>";
		else if(n.type == "table") {
			body << "<table border=\"1\"><caption>" << n.caption << "</caption>";
			for(const String& rowstr : n.rows) {
				body << "<tr>";
				for(const String& cell : Split(rowstr, '\t'))
					body << "<td>" << cell << "</td>";
				body << "</tr>";
			}
			body << "</table>";
		}
		else if(n.type == "figure")
			body << "<figure><figcaption>" << n.caption << "</figcaption></figure>";
		else if(n.type == "citation")
			body << "<span class=\"cite\">[" << n.ref_key << "]</span>";
		else if(n.type == "reference")
			body << "<p class=\"ref\">[" << n.ref_key << "] " << n.source_ref << "</p>";
	}
	return String(body);
}

static void SciencePaperPage(WebRequest& r)
{
	String id = r.PathArg(0);
	SciencePaper paper;
	if(!Corpus().GetPaper(id, paper)) {
		r.Status(404, "paper not found");
		return;
	}
	StringBuffer body;
	body << "<h2>" << (paper.title.IsEmpty() ? paper.id : paper.title) << "</h2>";
	if(!paper.source.IsEmpty())
		body << "<p>Source: <a href=\"" << paper.source << "\">" << paper.source << "</a></p>";
	if(!paper.arxiv_id.IsEmpty())
		body << "<p>arXiv: " << paper.arxiv_id << "</p>";
	body << "<p>Status: " << paper.status << " | schema " << paper.schema_id
	     << " v" << paper.schema_version << " rev " << paper.revision << "</p>";
	body << "<h3>Abstract</h3><p>" << paper.abstract << "</p>";
	ScienceArticleTree tree;
	ForgeResult ar = Corpus().GetArticleTree(id, tree);
	if(ar.ok) {
		body << "<h3>Structured article</h3>" << RenderArticleHtml(tree);
	}
	else {
		body << "<p><i>Full structured text unavailable (" << ar.error << ")."
		     << " Metadata-only record.</i></p>";
	}
	if(MayAuthor(r))
		body << "<p><a href=\"" << r.Site().Url("papers/*/author", {paper.id}) << "\">Author structured article</a></p>";
	HtmlPage(r, "Paper", String(body));
}

static void ScienceApiPapers(WebRequest& r)
{
	Vector<SciencePaper> papers;
	Corpus().ListPapers(papers);
	ValueArray arr;
	for(const SciencePaper& p : papers) {
		ValueMap m;
		m.Add("id", p.id);
		m.Add("title", p.title);
		m.Add("status", p.status);
		m.Add("arxiv_id", p.arxiv_id);
		m.Add("schema_id", p.schema_id);
		m.Add("schema_version", p.schema_version);
		m.Add("revision", p.revision);
		m.Add("visibility", p.visibility);
		m.Add("has_article", p.has_article);
		arr.Add(m);
	}
	ValueMap out;
	out.Add("count", papers.GetCount());
	out.Add("papers", arr);
	r.Json(out);
}

static void ScienceApiPaper(WebRequest& r)
{
	String id = r.PathArg(0);
	SciencePaper paper;
	if(!Corpus().GetPaper(id, paper)) {
		r.Json(JsonOk(false, "paper not found"), 404);
		return;
	}
	ValueMap m;
	m.Add("ok", true);
	m.Add("id", paper.id);
	m.Add("title", paper.title);
	m.Add("source", paper.source);
	m.Add("status", paper.status);
	m.Add("abstract", paper.abstract);
	m.Add("arxiv_id", paper.arxiv_id);
	m.Add("schema_id", paper.schema_id);
	m.Add("schema_version", paper.schema_version);
	m.Add("revision", paper.revision);
	m.Add("visibility", paper.visibility);
	m.Add("has_article", paper.has_article);
	r.Json(m);
}

static void ScienceApiArticle(WebRequest& r)
{
	String id = r.PathArg(0);
	ScienceArticleTree tree;
	ForgeResult ar = Corpus().GetArticleTree(id, tree);
	if(!ar.ok) {
		r.Json(JsonOk(false, ar.error), ar.error.Find("no structured article") >= 0 ? 404 : 500);
		return;
	}
	ValueMap m;
	m.Add("ok", true);
	m.Add("article", StoreAsJsonValue(tree));
	r.Json(m);
}

static void SciencePaperAuthorPage(WebRequest& r)
{
	String id = r.PathArg(0);
	SciencePaper paper;
	if(!Corpus().GetPaper(id, paper)) {
		r.Status(404, "paper not found");
		return;
	}
	if(!MayAuthor(r)) {
		r.Status(403, "authoring needs the science-author role or a science.author grant");
		return;
	}
	ScienceArticleTree tree;
	String json = "{\"title\":\"\",\"nodes\":[{\"id\":\"s1\",\"type\":\"section\",\"heading\":\"Introduction\"}]}";
	if(Corpus().GetArticleTree(id, tree).ok)
		json = StoreAsJson(tree);
	String url = r.Site().Url("api/papers/*/article", {paper.id});
	StringBuffer body;
	body << "<p>Paper revision <input id=\"rev\" value=\"" << Esc(paper.revision) << "\" readonly></p>"
	     << "<input type=\"hidden\" id=\"csrf\" value=\"" << Esc(r.CsrfToken()) << "\">"
	     << "<textarea id=\"tree\" rows=\"30\" cols=\"100\">" << Esc(json) << "</textarea>"
	     << "<p><button type=\"button\" onclick=\"saveArticle()\">Save article</button> <span id=\"result\"></span></p>"
	     << "<script>\n"
	     << "function saveArticle() {\n"
	     << "  var article;\n"
	     << "  try { article = JSON.parse(document.getElementById('tree').value); }\n"
	     << "  catch(e) { document.getElementById('result').innerText = 'Invalid JSON: ' + e; return; }\n"
	     << "  fetch('" << url << "', {method: 'POST', credentials: 'same-origin',\n"
	     << "    headers: {'Content-Type': 'application/json', 'x-wf-csrf': document.getElementById('csrf').value},\n"
	     << "    body: JSON.stringify({expected_paper_revision: document.getElementById('rev').value, article: article})})\n"
	     << "  .then(function(res) { return res.text().then(function(t) {\n"
	     << "    document.getElementById('result').innerText = res.status + ' ' + t; }); });\n"
	     << "}\n"
	     << "</script>";
	HtmlPage(r, "Author " + (paper.title.IsEmpty() ? paper.id : paper.title), String(body));
}

// POST api/papers/<id>/article with {"expected_paper_revision": "...", "article": {tree}}.
static void ScienceApiArticleWrite(WebRequest& r)
{
	String id = r.PathArg(0);
	if(!MayAuthor(r)) {
		r.Json(JsonOk(false, "authoring needs the science-author role or a science.author grant"), 403);
		return;
	}
	Value body = ParseJSON(r.Body());
	if(!body.Is<ValueMap>()) {
		r.Json(JsonOk(false, "body must be a JSON object"), 400);
		return;
	}
	ValueMap m = body;
	String expected = m["expected_paper_revision"];
	if(expected.IsEmpty()) {
		r.Json(JsonOk(false, "expected_paper_revision is required"), 400);
		return;
	}
	ScienceArticleTree tree;
	if(!LoadFromJson(tree, AsJSON(m["article"]))) {
		r.Json(JsonOk(false, "article is not a valid article tree"), 400);
		return;
	}
	String author = r.HasUser() ? r.User().username : String("grant:") + r.Header(SCIENCE_GRANT_HEADER);
	ForgeResult res = Corpus().SaveArticleTree(id, tree, expected, author);
	if(!res.ok) {
		r.Json(JsonOk(false, res.error), ScienceErrorStatus(res.error));
		return;
	}
	ValueMap out;
	out.Add("ok", true);
	out.Add("paper_id", id);
	r.Json(out);
}

void InstallScienceSite(EngineSite& site, ScienceCorpus& corpus, WebGrants *grants)
{
	s_corpus = &corpus;
	s_grants = grants;
	SiteRouter& rt = site.Router();
	rt.Add("", ScienceHome);
	rt.Add("papers", SciencePaperList);
	rt.Add("papers/*", SciencePaperPage);
	rt.Add("api/papers", ScienceApiPapers);
	rt.Add("api/papers/*", ScienceApiPaper);
	rt.Add("api/papers/*/article", ScienceApiArticle);
	rt.Add("api/papers/*/article:POST", ScienceApiArticleWrite);
	rt.Add("papers/*/author", SciencePaperAuthorPage);

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("papers-list", "GET", "api/papers", "public",
	                                        "json", "List papers in the corpus", {}));
	cm.Add(site.id, site.title, HttpCommand("paper-get", "GET", "api/papers/*", "public",
	                                        "json", "Read one paper's metadata", {"id:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("article-get", "GET", "api/papers/*/article", "public",
	                                        "json", "Read one paper's article tree", {"id:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("article-write", "POST", "api/papers/*/article",
	                                        "science-author role or science.author grant", "json",
	                                        "Replace the article tree when the expected revision matches",
	                                        {"id:string:required", "expected_paper_revision:string:required",
	                                         "article:text:required"}));
}

END_UPP_NAMESPACE
