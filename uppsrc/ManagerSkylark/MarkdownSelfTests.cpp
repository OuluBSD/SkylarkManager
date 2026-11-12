#include <Core/Core.h>
#include <WebChat/ChatView.h>

NAMESPACE_UPP

// Static checks for the markdown pipeline (task 028): pinned CDN URLs with SRI, and the
// sanitizer call in the shared render script. No network: SRI values are not fetched here.

static bool MarkdownCheck(bool cond, const String& what)
{
	if(cond)
		Cout() << "ok    " << what << "\n";
	else
		Cout() << "FAILED: " << what << "\n";
	return cond;
}

bool RunMarkdownSelfTests()
{
	bool all = true;
	String libs = WebChatView::GetMarkdownLibraryTags();
	String css = WebChatView::GetMarkdownStylesheetTag();
	String script = WebChatView::GetMarkdownScript();

	all &= MarkdownCheck(libs.Find("cdn.jsdelivr.net/npm/marked/lib/marked.umd.js") < 0 && libs.Find("marked@15.0.7/lib/marked.umd.js") >= 0,
	             "markdown: marked is loaded from a pinned version, not the unversioned URL");
	all &= MarkdownCheck(libs.Find("dompurify@3.1.7/dist/purify.min.js") >= 0, "markdown: DOMPurify is loaded from a pinned version");
	all &= MarkdownCheck(libs.Find("highlight.js/11.9.0/highlight.min.js") >= 0 && libs.Find("jsdiff/5.2.0/diff.min.js") >= 0,
	             "markdown: highlight.js and diff keep their pinned versions");
	int sri_count = 0;
	for(const char* p = libs; (p = strstr(p, "integrity=\"sha384-")) != NULL; p++)
		sri_count++;
	all &= MarkdownCheck(sri_count == 4, "markdown: every library script carries an sha384 integrity attribute");
	all &= MarkdownCheck(css.Find("integrity=\"sha384-") >= 0 && css.Find("crossorigin=\"anonymous\"") >= 0,
	             "markdown: the highlight.js stylesheet carries SRI");
	all &= MarkdownCheck(libs.Find("<script src=\"https://cdn.jsdelivr.net/npm/dompurify") >= 0 &&
	             libs.Find("crossorigin=\"anonymous\"") >= 0,
	             "markdown: the scripts use crossorigin for SRI");

	all &= MarkdownCheck(script.Find("DOMPurify.sanitize(html") >= 0,
	             "markdown: the shared renderer sanitizes marked output before innerHTML");
	all &= MarkdownCheck(script.Find("typeof DOMPurify === 'undefined'") >= 0,
	             "markdown: without DOMPurify the renderer falls back to escaped text");
	all &= MarkdownCheck(script.Find("onclick=\"toggleViewerRaw") < 0 && script.Find("data-viewer-action") >= 0,
	             "markdown: viewer buttons use data attributes, not inline handlers");

	return all;
}

END_UPP_NAMESPACE
