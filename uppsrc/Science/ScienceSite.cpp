#include "ScienceSite.h"

NAMESPACE_UPP

static ScienceCorpus *s_corpus = NULL;

static ScienceCorpus& Corpus()
{
	ASSERT_(s_corpus, "science site not installed");
	return *s_corpus;
}

static void HtmlPage(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\">"
	       "<title>" + title + "</title></head><body>"
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

void InstallScienceSite(EngineSite& site, ScienceCorpus& corpus)
{
	s_corpus = &corpus;
	SiteRouter& rt = site.Router();
	rt.Add("", ScienceHome);
	rt.Add("papers", SciencePaperList);
	rt.Add("papers/*", SciencePaperPage);
	rt.Add("api/papers", ScienceApiPapers);
	rt.Add("api/papers/*", ScienceApiPaper);
	rt.Add("api/papers/*/article", ScienceApiArticle);
}

END_UPP_NAMESPACE
