#include "PrivateHubSite.h"

NAMESPACE_UPP

// PrivateHub engine site. Every page is login-gated and project-scoped: the
// project is the two path segments "privatehub/<org>/<repo>". The prefix must
// not be a literal that a stock route claims (see WebEngine AGENTS). Writes
// require a session user, and the engine's mutation fence checks the CSRF token.

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

static String Project(WebRequest& r)
{
	return r.PathArg(0) + "/" + r.PathArg(1);
}

// The first `count` path wildcards, for building links to sibling pages.
static Vector<Value> ArgsOf(WebRequest& r, int count)
{
	Vector<Value> args;
	for(int i = 0; i < count; i++)
		args << Value(r.PathArg(i));
	return args;
}

// Returns false (after redirecting to the site login) for anonymous callers.
static bool RequireUser(WebRequest& r)
{
	if(r.HasUser())
		return true;
	r.Redirect(r.Site().Url("session/login"));
	return false;
}

static const char *AUTHOR_ROLE = "member";

static bool CanAuthor(WebRequest& r)
{
	if(!r.HasUser())
		return false;
	for(const String& role : r.User().roles)
		if(role == AUTHOR_ROLE)
			return true;
	return false;
}

static String CsrfField(WebRequest& r)
{
	return "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + Esc(r.CsrfToken()) + "\">";
}

static void Page(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\">"
	       "<title>" + Esc(title) + "</title></head><body>"
	       "<h1>" + Esc(title) + "</h1>" + body + "</body></html>");
}

static void Failed(WebRequest& r, int code, const String& message)
{
	r.Status(code, message);
	Page(r, "PrivateHub", "<p>" + Esc(message) + "</p>");
}

// Section links for the current project.
static String ProjectNav(WebRequest& r)
{
	StringBuffer b;
	b << "<nav><a href=\"" << Esc(r.Site().Url("privatehub/*/*", ArgsOf(r, 2))) << "\">Overview</a> | "
	  << "<a href=\"" << Esc(r.Site().Url("privatehub/*/*/issues", ArgsOf(r, 2))) << "\">Issues</a> | "
	  << "<a href=\"" << Esc(r.Site().Url("privatehub/*/*/wiki", ArgsOf(r, 2))) << "\">Wiki</a> | "
	  << "<a href=\"" << Esc(r.Site().Url("privatehub/*/*/discussions", ArgsOf(r, 2))) << "\">Discussions</a> | "
	  << "<a href=\"" << Esc(r.Site().Url("privatehub/*/*/files", ArgsOf(r, 2))) << "\">Files</a></nav>";
	return String(b);
}

// Workspace overview: organizations and their repositories.
static void PrivateHubHome(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	Vector<String> orgs;
	if(!Backend().ListOrganizations(orgs)) {
		Page(r, "PrivateHub", "<p>Workspace root is not readable.</p>");
		return;
	}
	StringBuffer body;
	body << "<ul>";
	for(const String& org : orgs) {
		Vector<String> repos;
		Backend().ListRepositories(org, repos);
		body << "<li>" << Esc(org) << "<ul>";
		for(const String& repo : repos) {
			Vector<Value> args;
			args << Value(org) << Value(repo);
			body << "<li><a href=\"" << Esc(r.Site().Url("privatehub/*/*", args)) << "\">" << Esc(repo) << "</a></li>";
		}
		body << "</ul></li>";
	}
	body << "</ul>";
	Page(r, "PrivateHub", String(body));
}

static void ProjectHome(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	Page(r, Project(r), ProjectNav(r) + "<p>Issues, wiki pages, discussion topics and release files for this project.</p>");
}

// --- issues ---------------------------------------------------------------

static void IssuesList(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	Vector<ForgeIssue> issues;
	ForgeResult res = Backend().ListIssues(Project(r), issues);
	if(!res.ok) {
		Failed(r, 400, res.error);
		return;
	}
	StringBuffer b;
	b << ProjectNav(r) << "<table><tr><th>Issue</th><th>State</th><th>Author</th><th>Comments</th></tr>";
	for(const ForgeIssue& i : issues) {
		Vector<Value> args = ArgsOf(r, 2);
		args << Value(i.issue_id);
		b << "<tr><td><a href=\"" << Esc(r.Site().Url("privatehub/*/*/issues/*", args)) << "\">" << Esc(i.title) << "</a></td>"
		  << "<td>" << (i.open ? "open" : "closed") << "</td>"
		  << "<td>" << Esc(i.author) << "</td>"
		  << "<td>" << i.comment_count << "</td></tr>";
	}
	b << "</table>"
	  << "<h2>New issue</h2>"
	  << "<form method=\"post\" action=\"" << Esc(r.Site().Url("privatehub/*/*/issues/create", ArgsOf(r, 2))) << "\">"
	  << "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" << Esc(r.CsrfToken()) << "\">"
	  << "<p><input name=\"title\" required maxlength=\"200\" placeholder=\"Title\"></p>"
	  << "<p><textarea name=\"body\" rows=\"6\" cols=\"80\"></textarea></p>"
	  << "<p><button type=\"submit\">Create issue</button></p></form>";
	Page(r, Project(r) + " issues", String(b));
}

static void IssueCreate(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	String title = r.Param("title");
	if(title.IsEmpty() || title.GetCount() > 200) {
		Failed(r, 400, "issue title is required (max 200 characters)");
		return;
	}
	ForgeIssue draft;
	draft.title = title;
	draft.body = r.Param("body");
	draft.author = r.User().username;
	ForgeIssue out;
	ForgeResult res = Backend().CreateIssue(Project(r), draft, out);
	if(!res.ok) {
		Failed(r, 400, res.error);
		return;
	}
	Vector<Value> args = ArgsOf(r, 2);
	args << Value(out.issue_id);
	r.Redirect(r.Site().Url("privatehub/*/*/issues/*", args));
}

static void IssueDetail(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	ForgeIssue issue;
	if(!Backend().GetIssue(Project(r), r.PathArg(2), issue).ok) {
		Failed(r, 404, "issue not found");
		return;
	}
	Vector<ForgeComment> comments;
	Backend().ListComments(Project(r), r.PathArg(2), comments);
	StringBuffer b;
	b << ProjectNav(r) << "<p>" << (issue.open ? "open" : "closed") << " &middot; " << Esc(issue.author) << "</p>"
	  << "<div style=\"white-space:pre-wrap\">" << Esc(issue.body) << "</div><h2>Comments</h2>";
	for(const ForgeComment& c : comments)
		b << "<div><strong>" << Esc(c.author) << "</strong><div style=\"white-space:pre-wrap\">" << Esc(c.body) << "</div></div>";
	b << "<form method=\"post\" action=\"" << Esc(r.Site().Url("privatehub/*/*/issues/*/comment", ArgsOf(r, 3))) << "\">"
	  << "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" << Esc(r.CsrfToken()) << "\">"
	  << "<p><textarea name=\"body\" rows=\"4\" cols=\"80\" required></textarea></p>"
	  << "<p><button type=\"submit\">Comment</button></p></form>";
	Page(r, issue.title, String(b));
}

static void IssueComment(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	String body = r.Param("body");
	if(body.IsEmpty()) {
		Failed(r, 400, "comment body is required");
		return;
	}
	ForgeComment draft;
	draft.issue_id = r.PathArg(2);
	draft.body = body;
	draft.author = r.User().username;
	ForgeComment out;
	ForgeResult res = Backend().AddComment(Project(r), r.PathArg(2), draft, out);
	if(!res.ok) {
		Failed(r, 400, res.error);
		return;
	}
	r.Redirect(r.Site().Url("privatehub/*/*/issues/*", ArgsOf(r, 3)));
}

// --- wiki -----------------------------------------------------------------

static void WikiList(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	Vector<PrivateHubWikiPage> pages;
	ForgeResult res = Backend().ListWikiPages(Project(r), pages);
	if(!res.ok) {
		Failed(r, 400, res.error);
		return;
	}
	StringBuffer b;
	b << ProjectNav(r) << "<ul>";
	for(const PrivateHubWikiPage& p : pages) {
		Vector<Value> args = ArgsOf(r, 2);
		args << Value(p.slug);
		b << "<li><a href=\"" << Esc(r.Site().Url("privatehub/*/*/wiki/*", args)) << "\">" << Esc(p.title) << "</a></li>";
	}
	b << "</ul>";
	if(CanAuthor(r))
		b << "<p><a href=\"" << Esc(r.Site().Url("privatehub/*/*/wiki/new", ArgsOf(r, 2))) << "\">New page</a></p>";
	Page(r, Project(r) + " wiki", String(b));
}

static void WikiPage(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	PrivateHubWikiPage page;
	if(!Backend().GetWikiPage(Project(r), r.PathArg(2), page).ok) {
		Failed(r, 404, "wiki page not found");
		return;
	}
	String edit;
	if(CanAuthor(r))
		edit = "<p><a href=\"" + Esc(r.Site().Url("privatehub/*/*/wiki/*/edit", ArgsOf(r, 3))) + "\">Edit</a></p>";
	Page(r, page.title, ProjectNav(r) + edit + "<pre style=\"white-space:pre-wrap\">" + Esc(page.body) + "</pre>");
}

// --- discussions ----------------------------------------------------------

static void TopicList(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	Vector<PrivateHubDiscussionTopic> topics;
	ForgeResult res = Backend().ListDiscussionTopics(Project(r), topics);
	if(!res.ok) {
		Failed(r, 400, res.error);
		return;
	}
	StringBuffer b;
	b << ProjectNav(r) << "<ul>";
	for(const PrivateHubDiscussionTopic& t : topics) {
		Vector<Value> args = ArgsOf(r, 2);
		args << Value(t.slug);
		b << "<li><a href=\"" << Esc(r.Site().Url("privatehub/*/*/discussions/*", args)) << "\">" << Esc(t.title)
		  << "</a> <small>" << Esc(t.forum) << "</small></li>";
	}
	b << "</ul>";
	if(CanAuthor(r))
		b << "<h2>New topic</h2><form method=\"post\" action=\""
		  << Esc(r.Site().Url("privatehub/*/*/discussions/create", ArgsOf(r, 2))) << "\">" << CsrfField(r)
		  << "<p><input name=\"slug\" required maxlength=\"80\" placeholder=\"slug\"> "
		     "<input name=\"forum\" maxlength=\"80\" placeholder=\"forum (general)\"></p>"
		     "<p><input name=\"title\" required maxlength=\"200\" placeholder=\"Title\"></p>"
		     "<p><button type=\"submit\">Start topic</button></p></form>";
	Page(r, Project(r) + " discussions", String(b));
}

static void TopicDetail(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	PrivateHubDiscussionTopic topic;
	if(!Backend().GetDiscussionTopic(Project(r), r.PathArg(2), topic).ok) {
		Failed(r, 404, "discussion topic not found");
		return;
	}
	Vector<PrivateHubPost> posts;
	Backend().ListDiscussionPosts(Project(r), r.PathArg(2), posts);
	StringBuffer b;
	b << ProjectNav(r);
	for(const PrivateHubPost& p : posts)
		b << "<div><strong>" << Esc(p.author) << "</strong><div style=\"white-space:pre-wrap\">" << Esc(p.body) << "</div></div>";
	b << "<form method=\"post\" action=\"" << Esc(r.Site().Url("privatehub/*/*/discussions/*/post", ArgsOf(r, 3))) << "\">"
	  << "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" << Esc(r.CsrfToken()) << "\">"
	  << "<p><textarea name=\"body\" rows=\"4\" cols=\"80\" required></textarea></p>"
	  << "<p><button type=\"submit\">Post</button></p></form>";
	Page(r, topic.title, String(b));
}

static void TopicPost(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	String body = r.Param("body");
	if(body.IsEmpty()) {
		Failed(r, 400, "post body is required");
		return;
	}
	PrivateHubPost post;
	post.post_id = Format(Uuid::Create());
	post.topic = r.PathArg(2);
	post.author = r.User().username;
	post.body = body;
	post.created = AsString(GetSysTime());
	String new_revision;
	ForgeResult res = Backend().PutDiscussionPost(Project(r), post, String(), new_revision);
	if(!res.ok) {
		Failed(r, 400, res.error);
		return;
	}
	r.Redirect(r.Site().Url("privatehub/*/*/discussions/*", ArgsOf(r, 3)));
}

// --- release files --------------------------------------------------------

static void FileList(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	Vector<PrivateHubReleaseFile> files;
	ForgeResult res = Backend().ListReleaseFiles(Project(r), files);
	if(!res.ok) {
		Failed(r, 400, res.error);
		return;
	}
	StringBuffer b;
	b << ProjectNav(r) << "<table><tr><th>File</th><th>Release</th><th>Bytes</th></tr>";
	for(const PrivateHubReleaseFile& f : files) {
		Vector<Value> args = ArgsOf(r, 2);
		args << Value(f.name);
		b << "<tr><td><a href=\"" << Esc(r.Site().Url("privatehub/*/*/files/*", args)) << "\">" << Esc(f.name) << "</a></td>"
		  << "<td>" << Esc(f.release) << "</td><td>" << f.size_bytes << "</td></tr>";
	}
	b << "</table>";
	if(CanAuthor(r))
		b << "<h2>Register release file</h2><form method=\"post\" action=\""
		  << Esc(r.Site().Url("privatehub/*/*/files/register", ArgsOf(r, 2))) << "\">" << CsrfField(r)
		  << "<p><input name=\"name\" required maxlength=\"200\" placeholder=\"file name\"> "
		     "<input name=\"release\" required maxlength=\"80\" placeholder=\"release\"> "
		     "<input name=\"size_bytes\" required inputmode=\"numeric\" placeholder=\"bytes\"></p>"
		     "<p><input name=\"url\" required maxlength=\"2000\" placeholder=\"download URL or path\"></p>"
		     "<p><button type=\"submit\">Register</button></p></form>";
	Page(r, Project(r) + " files", String(b));
}

// The stored url is shown as text only: it is a download URL or a local path,
// and rendering it as a link would expose or redirect to local paths.
static void FileDetail(WebRequest& r)
{
	if(!RequireUser(r))
		return;
	PrivateHubReleaseFile file;
	if(!Backend().GetReleaseFile(Project(r), r.PathArg(2), file).ok) {
		Failed(r, 404, "release file not found");
		return;
	}
	Page(r, file.name, ProjectNav(r) +
	     "<dl><dt>Release</dt><dd>" + Esc(file.release) + "</dd>"
	     "<dt>Bytes</dt><dd>" + AsString(file.size_bytes) + "</dd>"
	     "<dt>Updated</dt><dd>" + Esc(file.updated) + "</dd>"
	     "<dt>Source</dt><dd><code>" + Esc(file.url) + "</code></dd></dl>");
}

// --- authoring ------------------------------------------------------------
// Writes need the PrivateHub member role on top of a login. A create refuses an
// existing slug, so it never overwrites a record. An edit must name the revision
// it was made from, so a save over a newer revision is refused rather than lost.

static bool RequireAuthor(WebRequest& r)
{
	if(!RequireUser(r))
		return false;
	if(!CanAuthor(r)) {
		Failed(r, 403, "your account cannot write to PrivateHub");
		return false;
	}
	return true;
}

static String Today()
{
	Date d = GetSysDate();
	return Format("%04d-%02d-%02d", d.year, d.month, d.day);
}

static String WikiForm(WebRequest& r, const String& action, const PrivateHubWikiPage& page, bool editing)
{
	StringBuffer b;
	b << "<form method=\"post\" action=\"" << Esc(action) << "\">" << CsrfField(r);
	if(editing)
		b << "<input type=\"hidden\" name=\"expected_revision\" value=\"" << Esc(page.revision) << "\">"
		  << "<p>Slug: <code>" << Esc(page.slug) << "</code></p>";
	else
		b << "<p><input name=\"slug\" required maxlength=\"80\" placeholder=\"slug\"></p>";
	b << "<p><input name=\"title\" required maxlength=\"200\" placeholder=\"Title\" value=\"" << Esc(page.title) << "\"></p>"
	  << "<p><textarea name=\"body\" rows=\"16\" cols=\"80\">" << Esc(page.body) << "</textarea></p>"
	  << "<p><button type=\"submit\">" << (editing ? "Save page" : "Create page") << "</button></p></form>";
	return String(b);
}

static void WikiNew(WebRequest& r)
{
	if(!RequireAuthor(r))
		return;
	PrivateHubWikiPage blank;
	Page(r, "New wiki page", ProjectNav(r) +
	     WikiForm(r, r.Site().Url("privatehub/*/*/wiki/create", ArgsOf(r, 2)), blank, false));
}

static void WikiCreate(WebRequest& r)
{
	if(!RequireAuthor(r))
		return;
	PrivateHubWikiPage page;
	page.slug = r.Param("slug");
	page.title = r.Param("title");
	page.body = r.Param("body");
	page.updated = Today();
	if(page.title.IsEmpty() || page.title.GetCount() > 200) {
		Failed(r, 400, "wiki page title is required (max 200 characters)");
		return;
	}
	PrivateHubWikiPage existing;
	if(Backend().GetWikiPage(Project(r), page.slug, existing).ok) {
		Failed(r, 409, "a wiki page with this slug already exists");
		return;
	}
	String rev;
	ForgeResult res = Backend().PutWikiPage(Project(r), page, "", rev);
	if(!res.ok) {
		Failed(r, 422, res.error);
		return;
	}
	Vector<Value> args = ArgsOf(r, 2);
	args << Value(page.slug);
	r.Redirect(r.Site().Url("privatehub/*/*/wiki/*", args));
}

static void WikiEdit(WebRequest& r)
{
	if(!RequireAuthor(r))
		return;
	PrivateHubWikiPage page;
	if(!Backend().GetWikiPage(Project(r), r.PathArg(2), page).ok) {
		Failed(r, 404, "wiki page not found");
		return;
	}
	Page(r, "Edit " + page.title, ProjectNav(r) +
	     WikiForm(r, r.Site().Url("privatehub/*/*/wiki/*/save", ArgsOf(r, 3)), page, true));
}

static void WikiSave(WebRequest& r)
{
	if(!RequireAuthor(r))
		return;
	PrivateHubWikiPage page;
	if(!Backend().GetWikiPage(Project(r), r.PathArg(2), page).ok) {
		Failed(r, 404, "wiki page not found");
		return;
	}
	String expected = r.Param("expected_revision");
	if(expected.IsEmpty()) {
		Failed(r, 400, "the edit does not name the revision it was made from");
		return;
	}
	page.title = r.Param("title");
	page.body = r.Param("body");
	page.updated = Today();
	if(page.title.IsEmpty() || page.title.GetCount() > 200) {
		Failed(r, 400, "wiki page title is required (max 200 characters)");
		return;
	}
	String rev;
	ForgeResult res = Backend().PutWikiPage(Project(r), page, expected, rev);
	if(!res.ok) {
		Failed(r, 409, "not saved, the page changed since you opened it: " + res.error);
		return;
	}
	r.Redirect(r.Site().Url("privatehub/*/*/wiki/*", ArgsOf(r, 3)));
}

static void TopicCreate(WebRequest& r)
{
	if(!RequireAuthor(r))
		return;
	PrivateHubDiscussionTopic topic;
	topic.slug = r.Param("slug");
	topic.title = r.Param("title");
	topic.forum = r.Param("forum");
	if(topic.forum.IsEmpty())
		topic.forum = "general";
	topic.author = r.User().username;
	topic.created = Today();
	if(topic.title.IsEmpty() || topic.title.GetCount() > 200) {
		Failed(r, 400, "topic title is required (max 200 characters)");
		return;
	}
	PrivateHubDiscussionTopic existing;
	if(Backend().GetDiscussionTopic(Project(r), topic.slug, existing).ok) {
		Failed(r, 409, "a topic with this slug already exists");
		return;
	}
	String rev;
	ForgeResult res = Backend().PutDiscussionTopic(Project(r), topic, "", rev);
	if(!res.ok) {
		Failed(r, 422, res.error);
		return;
	}
	Vector<Value> args = ArgsOf(r, 2);
	args << Value(topic.slug);
	r.Redirect(r.Site().Url("privatehub/*/*/discussions/*", args));
}

static void FileRegister(WebRequest& r)
{
	if(!RequireAuthor(r))
		return;
	PrivateHubReleaseFile file;
	file.name = r.Param("name");
	file.release = r.Param("release");
	file.url = r.Param("url");
	file.updated = Today();
	String size = r.Param("size_bytes");
	int64 bytes = size.IsEmpty() ? -1 : ScanInt64(size);
	if(bytes < 0 || AsString(bytes) != size) {
		Failed(r, 400, "size_bytes must be a whole number of bytes");
		return;
	}
	file.size_bytes = bytes;
	if(file.name.IsEmpty() || file.url.IsEmpty()) {
		Failed(r, 400, "file name and source are required");
		return;
	}
	PrivateHubReleaseFile existing;
	if(Backend().GetReleaseFile(Project(r), file.name, existing).ok) {
		Failed(r, 409, "a release file with this name is already registered");
		return;
	}
	String rev;
	ForgeResult res = Backend().PutReleaseFile(Project(r), file, "", rev);
	if(!res.ok) {
		Failed(r, 422, res.error);
		return;
	}
	Vector<Value> args = ArgsOf(r, 2);
	args << Value(file.name);
	r.Redirect(r.Site().Url("privatehub/*/*/files/*", args));
}

void InstallPrivateHubSite(EngineSite& site, PrivateHubWorkspaceBackend& backend)
{
	s_backend = &backend;
	SiteRouter& rt = site.Router();
	rt.Add("", PrivateHubHome);
	rt.Add("privatehub/*/*", ProjectHome);
	rt.Add("privatehub/*/*/issues", IssuesList);
	rt.Add("privatehub/*/*/issues/create:POST", IssueCreate);
	rt.Add("privatehub/*/*/issues/*", IssueDetail);
	rt.Add("privatehub/*/*/issues/*/comment:POST", IssueComment);
	rt.Add("privatehub/*/*/wiki", WikiList);
	rt.Add("privatehub/*/*/wiki/new", WikiNew);
	rt.Add("privatehub/*/*/wiki/create:POST", WikiCreate);
	rt.Add("privatehub/*/*/wiki/*", WikiPage);
	rt.Add("privatehub/*/*/wiki/*/edit", WikiEdit);
	rt.Add("privatehub/*/*/wiki/*/save:POST", WikiSave);
	rt.Add("privatehub/*/*/discussions", TopicList);
	rt.Add("privatehub/*/*/discussions/create:POST", TopicCreate);
	rt.Add("privatehub/*/*/discussions/*", TopicDetail);
	rt.Add("privatehub/*/*/discussions/*/post:POST", TopicPost);
	rt.Add("privatehub/*/*/files", FileList);
	rt.Add("privatehub/*/*/files/register:POST", FileRegister);
	rt.Add("privatehub/*/*/files/*", FileDetail);
}

END_UPP_NAMESPACE
