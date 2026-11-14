#include "YouTubePlannerSite.h"
#include <WebsiteTemplates/WebsiteTemplates.h>

NAMESPACE_UPP

// YouTubePlanner: a navigable product stub for planning a YouTube video at six levels. It holds
// no plans, makes no YouTube call and offers no editor, so there is nothing to persist or publish.
// The level pages say what belongs at each level and that nothing is saved.

struct PlanningLevel {
	const char *slug;
	const char *name;
	const char *summary;
	const char *placeholder;
};

static const PlanningLevel LEVELS[] = {
	{ "screenwrite", "Screenwrite", "The story idea in prose: premise, beats and intended viewer experience.",
	  "Screenwrite is where the video's idea is written as prose: its premise, its beats and the experience it should give the viewer. This stub has no writing surface yet." },
	{ "screenplay", "Screenplay", "Scene structure: the scenes, their locations and their order.",
	  "Screenplay sets out the scenes, their locations and the order they play in. This stub has no scene editor yet." },
	{ "script", "Script", "Production-ready dialogue and on-screen action, line by line.",
	  "Script holds the dialogue and on-screen action for production, line by line. This stub has no script editor yet." },
	{ "mockup-image", "Mockup Image", "Visual direction: thumbnail and key-frame mockups.",
	  "Mockup Image is the visual direction: thumbnails and key frames. This stub has no image upload or viewer yet." },
	{ "high-level-objective", "High-Level Objective", "What the video is for, stated as a goal for its viewers.",
	  "A High-Level Objective states what the video must achieve for its viewers. This stub has no objective list yet." },
	{ "low-level-objective", "Low-Level Objective", "A checkable step that delivers a high-level objective.",
	  "A Low-Level Objective is a concrete, checkable step that delivers a high-level objective. This stub has no objective list yet." },
};

static const int LEVEL_COUNT = sizeof(LEVELS) / sizeof(LEVELS[0]);

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

int YouTubePlannerLevelCount()
{
	return LEVEL_COUNT;
}

String YouTubePlannerLevelName(int index)
{
	return LEVELS[index].name;
}

String YouTubePlannerLevelSlug(int index)
{
	return LEVELS[index].slug;
}

int YouTubePlannerLevelIndex(const String& slug)
{
	for(int i = 0; i < LEVEL_COUNT; i++)
		if(slug == LEVELS[i].slug)
			return i;
	return -1;
}

// The palette is the composed-site set (Atlas, Science): one look across the portal's webapps.
static String YouTubePlannerStylesheet()
{
	return R"YTP(<style>
:root { --link: var(--accent); }
body { background: var(--bg); color: var(--fg); font: var(--fs)/1.45 system-ui, sans-serif; margin: 0; }
main { padding: calc(var(--pad) * 2); max-width: var(--wf-container-width); margin: 0 auto; }
a { color: var(--link); }
nav.ytp-nav { padding: 10px 16px; border-bottom: 1px solid var(--line); }
nav.ytp-nav a { font-weight: 600; text-decoration: none; }
.ytp-stub { border: 1px solid var(--line); background: var(--panel); border-radius: 8px; padding: 10px 14px; color: var(--muted); }
.ytp-levels ol { list-style: none; padding: 0; margin: 0; display: grid; gap: 10px; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr)); }
.ytp-levels a { display: block; height: 100%; box-sizing: border-box; border: 1px solid var(--line); background: var(--panel); border-radius: 8px; padding: 12px 14px; text-decoration: none; color: var(--fg); }
.ytp-levels a:hover { border-color: var(--link); }
.ytp-levels a:focus-visible { outline: 3px solid var(--link); outline-offset: 2px; }
.ytp-levels a[aria-current="page"] { border-color: var(--link); box-shadow: inset 4px 0 0 var(--link); }
.ytp-num { display: inline-block; min-width: 1.6em; font-weight: 700; color: var(--muted); }
.ytp-name { font-weight: 700; }
.ytp-desc { display: block; color: var(--muted); font-size: 0.9em; margin-top: 4px; }
.ytp-steps { display: flex; flex-wrap: wrap; justify-content: space-between; gap: 10px; margin-top: 18px; }
.ytp-crumb { color: var(--muted); font-size: 0.9em; }
h1 { font-size: 1.6rem; margin: 10px 0; }
@media (max-width: 640px) { main { padding: 12px; } }
</style>)YTP";
}

static String LevelCards(int selected)
{
	String h = "<nav class=\"ytp-levels\" aria-label=\"Planning levels\"><ol>";
	for(int i = 0; i < LEVEL_COUNT; i++) {
		h << "<li><a href=\"/level/" << LEVELS[i].slug << "\"";
		if(i == selected)
			h << " aria-current=\"page\"";
		h << "><span class=\"ytp-num\">" << (i + 1) << "</span><span class=\"ytp-name\">" << LEVELS[i].name
		  << "</span><span class=\"ytp-desc\">" << LEVELS[i].summary << "</span></a></li>";
	}
	h << "</ol></nav>";
	return h;
}

String YouTubePlannerOverviewBody()
{
	String h;
	h << "<h1>YouTubePlanner</h1>"
	     "<p>Plan one video from its idea down to the checkable steps that make it. The six planning levels are below; "
	     "open one to see what belongs there.</p>"
	     "<p class=\"ytp-stub\" role=\"note\"><strong>Stub.</strong> Plans are not saved. This site has no YouTube "
	     "connection, no upload or publishing, no comments and no editor.</p>"
	     "<section aria-labelledby=\"ytp-levels-h\"><h2 id=\"ytp-levels-h\">Planning levels</h2>"
	  << LevelCards(-1) <<
	     "</section>"
	     "<section aria-labelledby=\"ytp-rel-h\"><h2 id=\"ytp-rel-h\">How the levels relate</h2>"
	     "<p>Objectives set the purpose: a High-Level Objective says what the video must achieve, and each "
	     "Low-Level Objective is a checkable step toward it. Screenwrite, Screenplay, Script and Mockup Image describe "
	     "the video itself, from its idea to its production text and visuals.</p>"
	     "<p>The levels are shown in one order, but nothing links one level to another in this stub.</p>"
	     "</section>";
	return h;
}

String YouTubePlannerLevelBody(int index)
{
	const PlanningLevel& lv = LEVELS[index];
	String h;
	h << "<p class=\"ytp-crumb\"><a href=\"/\">YouTubePlanner</a> / Level " << (index + 1) << " of " << LEVEL_COUNT << "</p>"
	  << "<h1>" << lv.name << "</h1>"
	  << "<p>" << lv.placeholder << "</p>"
	  << "<p class=\"ytp-stub\" role=\"note\"><strong>Stub.</strong> Nothing is saved on this page.</p>"
	  << "<section aria-labelledby=\"ytp-all-h\"><h2 id=\"ytp-all-h\">All planning levels</h2>" << LevelCards(index) << "</section>";
	h << "<nav class=\"ytp-steps\" aria-label=\"Level steps\">";
	if(index > 0)
		h << "<a href=\"/level/" << LEVELS[index - 1].slug << "\" rel=\"prev\">Previous: " << LEVELS[index - 1].name << "</a>";
	else
		h << "<span></span>";
	if(index + 1 < LEVEL_COUNT)
		h << "<a href=\"/level/" << LEVELS[index + 1].slug << "\" rel=\"next\">Next: " << LEVELS[index + 1].name << "</a>";
	h << "</nav>";
	return h;
}

static void Page(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html lang=\"en\"" + ThemeHtmlAttrs(r) + "><head><meta charset=\"utf-8\">" + ThemeHeadScript() + WebsitePageStyles() + 
	       "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
	       "<title>" + Esc(title) + " - YouTubePlanner</title>" + YouTubePlannerStylesheet() + "</head>"
	       "<body>"
	       "<nav class=\"ytp-nav\"><a href=\"" + Esc(r.Site().Url("")) + "\">YouTubePlanner</a> " + SessionChromeHtml(r) + WebsiteThemeControlsHtml() + "</nav>"
	       "<main>" + body + "</main>" + WebsiteThemeControlsScript() + "</body></html>");
}

static void Overview(WebRequest& r)
{
	Page(r, "Planning overview", YouTubePlannerOverviewBody());
}

static void LevelPage(WebRequest& r)
{
	int index = YouTubePlannerLevelIndex(r.PathArg(0));
	if(index < 0) {
		r.Status(404, "Not found");
		Page(r, "Not found", "<h1>Not found</h1><p>There is no planning level called \"" + Esc(r.PathArg(0)) + "\".</p>"
		     "<p><a href=\"/\">Back to the planning overview</a></p>");
		return;
	}
	Page(r, LEVELS[index].name, YouTubePlannerLevelBody(index));
}

void InstallYouTubePlannerSite(EngineSite& site)
{
	SiteRouter& rt = site.Router();
	rt.Add("", Overview);
	rt.Add("youtubeplanner", Overview);
	rt.Add("level/*", LevelPage);

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("youtubeplanner-overview", "GET", "youtubeplanner", "anonymous",
	                                        "html", "Planning overview with the six levels (stub, read-only)", {}));
	for(int i = 0; i < LEVEL_COUNT; i++)
		cm.Add(site.id, site.title, HttpCommand(String("youtubeplanner-level-") + LEVELS[i].slug, "GET",
		                                        String("level/") + LEVELS[i].slug, "anonymous", "html",
		                                        String("Planning level: ") + LEVELS[i].name + " (stub, read-only)", {}));
}

// Self-tests use only the pure body renderers and the level table; the routes need a live server.
static bool Check(bool ok, const char *what, Vector<String>& failures)
{
	if(!ok)
		failures.Add(what);
	return ok;
}

bool RunYouTubePlannerSelfTests()
{
	Vector<String> failures;
	Check(LEVEL_COUNT == 6, "six planning levels", failures);
	static const char *names[] = { "Screenwrite", "Screenplay", "Script", "Mockup Image",
	                               "High-Level Objective", "Low-Level Objective" };
	for(int i = 0; i < 6 && i < LEVEL_COUNT; i++)
		Check(YouTubePlannerLevelName(i) == names[i], "level names keep the order given", failures);
	for(int i = 0; i < LEVEL_COUNT; i++) {
		String slug = YouTubePlannerLevelSlug(i);
		Check(YouTubePlannerLevelIndex(slug) == i, "each level slug routes to its own level", failures);
		for(int j = 0; j < i; j++)
			Check(YouTubePlannerLevelSlug(j) != slug, "level slugs are unique", failures);
		Check(slug.Find(' ') < 0 && slug == ToLower(slug), "level slugs are lower-case route segments", failures);
	}
	Check(YouTubePlannerLevelIndex("nope") < 0, "an unknown slug is not a level", failures);

	String overview = YouTubePlannerOverviewBody();
	for(int i = 0; i < LEVEL_COUNT; i++)
		Check(overview.Find(YouTubePlannerLevelName(i)) >= 0, "the overview names every level", failures);
	Check(overview.Find("Plans are not saved") >= 0, "the overview says plans are not saved", failures);
	Check(overview.Find("no YouTube") >= 0, "the overview says there is no YouTube connection", failures);

	for(int i = 0; i < LEVEL_COUNT; i++) {
		String body = YouTubePlannerLevelBody(i);
		Check(body.Find("<h1>" + YouTubePlannerLevelName(i) + "</h1>") >= 0, "a level page has its own heading", failures);
		Check(body.Find("Nothing is saved on this page") >= 0, "a level page says nothing is saved", failures);
		int marked = 0;
		for(int at = body.Find("aria-current=\"page\""); at >= 0; at = body.Find("aria-current=\"page\"", at + 1))
			marked++;
		Check(marked == 1, "a level page marks exactly one selected level", failures);
	}

	// The boundary: no write path. A form, input, textarea or button would be one.
	static const char *write_markers[] = { "<form", "<input", "<textarea", "<button", "<select" };
	for(const char *m : write_markers) {
		Check(overview.Find(m) < 0, "the overview has no write control", failures);
		for(int i = 0; i < LEVEL_COUNT; i++)
			Check(YouTubePlannerLevelBody(i).Find(m) < 0, "a level page has no write control", failures);
	}

	if(failures.GetCount()) {
		for(const String& f : failures)
			Cerr() << "YouTubePlanner self-test failed: " << f << "\n";
		return false;
	}
	return true;
}

END_UPP_NAMESPACE
