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
	Page(r, page.title, ProjectNav(r) + "<pre style=\"white-space:pre-wrap\">" + Esc(page.body) + "</pre>");
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
	rt.Add("privatehub/*/*/wiki/*", WikiPage);
	rt.Add("privatehub/*/*/discussions", TopicList);
	rt.Add("privatehub/*/*/discussions/*", TopicDetail);
	rt.Add("privatehub/*/*/discussions/*/post:POST", TopicPost);
	rt.Add("privatehub/*/*/files", FileList);
	rt.Add("privatehub/*/*/files/*", FileDetail);
}

END_UPP_NAMESPACE
