#include "ManagerSkylark.h"
#include "SourceForgePlatform.h"
#include <SourceForgeForge/SourceForgeForge.h>

NAMESPACE_UPP

String CapitalizeWord(const String& s)
{
	if(s.IsEmpty()) return s;
	return ToUpper(s.Left(1)) + s.Mid(1);
}

ValueMap SourceForgePlatformService::LoadScrapedData(const String& project_slug)
{
	String dir = AppendFileName(GetHomeDirFile(".aria/sites/sourceforge"), project_slug);
	String full_path = AppendFileName(dir, "full_topside_scrape.json");
	if(FileExists(full_path)) {
		Value v = ParseJSON(LoadFile(full_path));
		if(v.Is<ValueMap>())
			return v;
	}
	String sum_path = AppendFileName(dir, "summary.json");
	if(FileExists(sum_path)) {
		Value v = ParseJSON(LoadFile(sum_path));
		if(v.Is<ValueMap>()) {
			ValueMap m;
			m.Add("summary", v);
			return m;
		}
	}
	return ValueMap();
}

SourceForgeProjectInfo SourceForgePlatformService::GetProject(const String& slug)
{
	SourceForgeProjectInfo info;
	String s = ToLower(slug);

	if(s == "subproject" || s == "subproject.topside.p") {
		info.name = "subproject.topside.p";
		info.title = "SubProject";
		info.maintainer = "sppp";
		info.description = "Autonomous subsystem and extension module for the Topside agentic framework.";
		info.license = "BSD-3-Clause";
		info.operating_systems = "POSIX, Linux, Windows";
		info.languages = "C++, Python";
		info.categories = "Software Development, Agent Systems";
		info.downloads_weekly = 0;
		info.last_update = "recent";
		info.is_subproject = true;
		info.parent_project = "topside";
		info.active_branch = "stable";
		info.branches.Add("stable");
		info.branches.Add("research");
		info.features.Add("Modular subproject packaging with independent configuration");
		info.features.Add("Seamless integration with parent Topside platform");
		return info;
	}

	info.name = s.IsEmpty() ? "topside" : s;
	info.title = info.name == "topside" ? "Topside" : CapitalizeWord(info.name);
	info.maintainer = "sppp";
	info.description = "Agentic software development platform and desktop suite for autonomous AI engineers.";
	info.license = "BSD-3-Clause";
	info.operating_systems = "POSIX, Linux, Windows";
	info.languages = "C++, Python";
	info.categories = "Software Development, Frameworks, Build Tools";
	info.git_repo_url = "https://git.code.sf.net/p/" + info.name + "/code";
	info.active_branch = "stable";
	info.branches.Add("stable");
	info.branches.Add("research");
	info.downloads_weekly = 215;
	info.rating_stars = 5;
	info.rating_count = 1;
	info.last_update = "today";

	info.features.Add("Full U++ Skylark web server integration with autonomous SweAgent");
	info.features.Add("Aria headless browser scraper and UI testing suite");
	info.features.Add("Manager project lifecycle, goal tracking, and task pipeline");
	info.features.Add("Platform parity with SourceForge and GitHub templates");
	info.features.Add("Multi-theme KDE Plasma/Vivid and Tangerine styling");

	// Inspect local Dev repository if non-topside
	if(s != "topside") {
		String dev_dir = GitHubPlatformService::GetDevDir();
		String local_path = AppendFileName(dev_dir, slug);
		if(!DirectoryExists(local_path)) {
			FindFile ff(AppendFileName(dev_dir, "*"));
			while(ff) {
				if(ff.IsFolder() && ToLower(ff.GetName()) == s) {
					local_path = ff.GetPath();
					break;
				}
				ff.Next();
			}
		}
		if(DirectoryExists(local_path)) {
			info.name = GetFileName(local_path);
			info.title = info.name;
			String readme = LoadFile(AppendFileName(local_path, "README.md"));
			if(!readme.IsEmpty()) {
				Vector<String> rlines = Split(readme, '\n');
				for(const String& rl : rlines) {
					String t = TrimBoth(rl);
					if(!t.IsEmpty() && !t.StartsWith("#") && !t.StartsWith("==")) {
						info.description = t;
						break;
					}
				}
			}
			if(info.name == "UppBootstrap") {
				info.features.Clear();
				info.features.Add("Zero-dependency C++11 umk bootstrap toolchain");
				info.features.Add("Automatic compiler detection (CLANG, GCC, MSVC, MinGW)");
				info.features.Add("Automatic canonical .bm method file generation");
				info.features.Add(".brc binary resource transpiler for standalone build");
				info.features.Add("Multi-OS CI test matrix (Linux, macOS, Windows)");
				info.license = "BSD-2-Clause";
				info.languages = "C++, Shell, Batch, PowerShell";
			}
		}
	}

	// Try loading scraped data if available
	ValueMap scraped = LoadScrapedData(info.name);
	info.scraped_snapshot = !scraped.IsEmpty();
	if(!scraped.IsEmpty()) {
		if(scraped.Find("code") >= 0) {
			ValueMap code = scraped["code"];
			if(!((String)code["branch"]).IsEmpty())
				info.active_branch = code["branch"];
		}
	}

	return info;
}

String SourceForgePlatformService::RenderTopNav(const SourceForgeProjectInfo& info)
{
	String h;
	h << "<header class=\"wf-sf-topbar\">\n"
	  << "  <div style=\"display:flex; align-items:center; gap:20px;\">\n"
	  << "    <a href=\"/projects/topside/\" style=\"display:flex; align-items:center; gap:8px; font-weight:700; font-size:1.1rem; color:#ee6524;\">\n"
	  << "      <svg width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5\"/></svg>\n"
	  << "      <span>SourceForge</span>\n"
	  << "    </a>\n"
	  << "    <nav style=\"display:flex; gap:16px; font-size:0.86rem;\">\n"
	  << "      <a href=\"/projects\">Open Source</a>\n"
	  << "      <a href=\"/projects/topside/files/\">Business Software</a>\n"
	  << "      <a href=\"/projects/topside/support\">Resources</a>\n"
	  << "      <a href=\"/projects/topside/blog\">Blog</a>\n"
	  << "    </nav>\n"
	  << "  </div>\n"
	  << "  <div style=\"display:flex; align-items:center; gap:14px;\">\n"
	  << "    <div style=\"position:relative;\">\n"
	  << "      <input type=\"search\" placeholder=\"Search open source software...\" style=\"background:#272b30; border:1px solid #393e46; border-radius:4px; padding:4px 10px; font-size:0.82rem; color:#e6e8eb; width:220px;\">\n"
	  << "    </div>\n"
	  << "    <a href=\"/p/" << info.name << "/admin/\" style=\"display:flex; align-items:center; gap:6px; color:#ffffff;\">\n"
	  << "      <span style=\"width:22px; height:22px; border-radius:50%; background:#ee6524; display:inline-flex; align-items:center; justify-content:center; font-size:0.75rem; font-weight:700;\">S</span>\n"
	  << "      <span>" << info.maintainer << "</span>\n"
	  << "    </a>\n"
	  << "    <a href=\"/repo/SeppoPakonen/" << info.name << "\" class=\"wf-btn wf-btn-secondary\" title=\"Switch to GitHub Platform Style\" style=\"display:inline-flex; align-items:center; gap:6px; font-weight:600; font-size:0.78rem; text-decoration:none; padding:3px 10px;\">\n"
	  << "      <svg width=\"14\" height=\"14\" viewBox=\"0 0 16 16\" fill=\"currentColor\" style=\"vertical-align:-2px;\"><path d=\"M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z\"/></svg>\n"
	  << "      <span>GitHub View</span>\n"
	  << "    </a>\n"
	  << "    <div class=\"wf-theme-switcher\" style=\"display:inline-flex; align-items:center; gap:6px;\">\n"
	  << "      <button type=\"button\" class=\"wf-btn wf-btn-secondary\" style=\"padding:2px 8px; font-size:0.75rem;\" onclick=\"WebThemeManager.cycle()\" title=\"Cycle Theme\">\n"
	  << "        <span id=\"wf-theme-label\">Theme</span>\n"
	  << "      </button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</header>\n";
	return h;
}

String SourceForgePlatformService::RenderProjectHeader(const SourceForgeProjectInfo& info, const String& active_tab)
{
	String base_p = "/p/" + info.name;
	String base_proj = "/projects/" + info.name;

	String h;
	h << "<div class=\"wf-sf-masthead\">\n"
	  << "  <div class=\"wf-sf-masthead-hero\">\n"
	  << "    <div class=\"wf-sf-hero-left\">\n"
	  << "      <div class=\"wf-sf-logo-box\">" << info.title.Left(1) << "</div>\n"
	  << "      <div class=\"wf-sf-hero-title\">\n"
	  << "        <h1>" << info.title << "</h1>\n"
	  << "        <div class=\"wf-sf-by\">Brought to you by: <a href=\"#\">" << info.maintainer << "</a></div>\n"
	  << (info.scraped_snapshot ? "        <span class=\"wf-badge wf-badge-neutral\">Scraped snapshot (read-only)</span>\n" : String())
	  << "        <div style=\"display:flex; align-items:center; gap:8px; margin-top:6px; font-size:0.82rem;\">\n"
	  << "          <span style=\"color:#f59f00;\">&#9733;&#9733;&#9733;&#9733;&#9733;</span>\n"
	  << "          <span style=\"color:var(--wf-fg-inactive,#8c929a);\">" << info.rating_stars << ".0 (" << info.rating_count << " rating)</span>\n"
	  << "          <span style=\"color:var(--wf-border-color,#393e46);\">|</span>\n"
	  << "          <a href=\"" << base_proj << "/reviews/\" style=\"color:#ee6524; text-decoration:none;\">Add a Review</a>\n"
	  << "        </div>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "    <div class=\"wf-sf-hero-right\">\n"
	  << "      <a href=\"" << base_proj << "/files/\" class=\"wf-sf-btn-download\">\n"
	  << "        " << VividIcon::Get("download", 20) << "\n"
	  << "        <span>Download Latest Version</span>\n"
	  << "      </a>\n"
	  << "      <div class=\"wf-sf-stats-row\">\n"
	  << "        <span>Downloads: <strong>" << info.downloads_weekly << "</strong> This Week</span> &middot; \n"
	  << "        <span>Last Update: <strong>" << info.last_update << "</strong></span>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <nav class=\"wf-sf-tabs\" aria-label=\"SourceForge Tools\">\n"
	  << "    <a href=\"" << base_proj << "\" class=\"wf-sf-tab" << (active_tab == "summary" ? " active" : "") << "\">" << VividIcon::Get("file-text", 15) << " Summary</a>\n"
	  << "    <a href=\"" << base_proj << "/files/\" class=\"wf-sf-tab" << (active_tab == "files" ? " active" : "") << "\">" << VividIcon::Get("folder", 15) << " Files</a>\n"
	  << "    <a href=\"" << base_proj << "/reviews/\" class=\"wf-sf-tab" << (active_tab == "reviews" ? " active" : "") << "\">" << VividIcon::Get("star", 15) << " Reviews</a>\n"
	  << "    <a href=\"" << base_proj << "/support\" class=\"wf-sf-tab" << (active_tab == "support" ? " active" : "") << "\">" << VividIcon::Get("help-circle", 15) << " Support</a>\n"
	  << "    <a href=\"" << base_p << "/blog/\" class=\"wf-sf-tab" << (active_tab == "blog" ? " active" : "") << "\">" << VividIcon::Get("book-open", 15) << " Blog</a>\n"
	  << "    <a href=\"" << base_p << "/code/ci/stable/tree/\" class=\"wf-sf-tab" << (active_tab == "code" ? " active" : "") << "\">" << VividIcon::Get("code", 15) << " Code</a>\n"
	  << "    <a href=\"" << base_p << "/wiki/Home/\" class=\"wf-sf-tab" << (active_tab == "wiki" ? " active" : "") << "\">" << VividIcon::Get("book", 15) << " Wiki</a>\n"
	  << "    <a href=\"" << base_p << "/tickets/\" class=\"wf-sf-tab" << (active_tab == "tickets" ? " active" : "") << "\">" << VividIcon::Get("bug", 15) << " Tickets</a>\n"
	  << "    <a href=\"" << base_p << "/discussion/\" class=\"wf-sf-tab" << (active_tab == "discussion" ? " active" : "") << "\">" << VividIcon::Get("message-square", 15) << " Discussion</a>\n"
	  << "    <a href=\"" << base_p << "/mailman/\" class=\"wf-sf-tab" << (active_tab == "mailman" ? " active" : "") << "\">" << VividIcon::Get("mail", 15) << " Mailing Lists</a>\n"
	  << "    <a href=\"/projects/subproject.topside.p/\" class=\"wf-sf-tab" << (active_tab == "subproject" ? " active" : "") << "\">" << VividIcon::Get("layers", 15) << " SubProject</a>\n"
	  << "    <a href=\"" << base_p << "/admin/\" class=\"wf-sf-tab" << (active_tab == "admin" ? " active" : "") << "\">" << VividIcon::Get("settings", 15) << " Admin</a>\n"
	  << "    <a href=\"" << base_p << "/agents/\" class=\"wf-sf-tab" << (active_tab == "agents" ? " active" : "") << "\" style=\"color:#069c56;\">" << VividIcon::Get("robot", 15) << " SweAgent Pair</a>\n"
	  << "  </nav>\n"
	  << "</div>\n";
	return h;
}

String SourceForgePlatformService::RenderSidebar(const SourceForgeProjectInfo& info)
{
	String h;
	h << "<div class=\"wf-sf-sidebar-card\">\n"
	  << "  <div class=\"wf-sf-sidebar-title\">Additional Project Details</div>\n"
	  << "  <div><strong>Registered:</strong> Today</div>\n"
	  << "  <div><strong>License:</strong> <span style=\"color:#ee6524;\">" << info.license << "</span></div>\n"
	  << "  <div><strong>Categories:</strong> " << info.categories << "</div>\n"
	  << "  <div><strong>Operating Systems:</strong> " << info.operating_systems << "</div>\n"
	  << "  <div><strong>Languages:</strong> " << info.languages << "</div>\n"
	  << "  <div><strong>Maintainer:</strong> <a href=\"#\" style=\"color:#ee6524;\">" << info.maintainer << "</a></div>\n"
	  << "  <div style=\"margin-top:8px; padding-top:8px; border-top:1px solid var(--wf-border-color,#393e46);\">\n"
	  << "    <div style=\"font-size:0.78rem; font-weight:600; margin-bottom:4px;\">Git Repository Clone:</div>\n"
	  << "    <input type=\"text\" readonly value=\"git clone " << info.git_repo_url << "\" style=\"width:100%; font-size:0.75rem; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); padding:4px 6px; border-radius:3px;\" onclick=\"this.select();\">\n"
	  << "  </div>\n"
	  << "</div>\n"
	  << "<div class=\"wf-sf-sidebar-card\">\n"
	  << "  <div class=\"wf-sf-sidebar-title\">Project Activity</div>\n"
	  << "  <div style=\"display:flex; align-items:center; gap:8px;\">\n"
	  << "    <span class=\"wf-badge wf-badge-emerald\">Active Daily</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8c929a);\">High development cadence</span>\n"
	  << "  </div>\n"
	  << "  <div style=\"font-size:0.8rem; margin-top:6px;\">Follow updates via RSS or Mailing List.</div>\n"
	  << "</div>\n";
	return h;
}

// ---------------------------------------------------------------------------
// SourceForge data access. Every surface goes through the forge backend; the
// local JSON stores are not read or written here (see the legacy migration).
// Without credentials the backend is read-only, and writes report unavailable.
// ---------------------------------------------------------------------------

static IForgeBackend *backend_override = NULL;
static VectorMap<String, One<IForgeBackend>> default_backends;

void SourceForgePlatformService::SetBackend(IForgeBackend *backend)
{
	backend_override = backend;
}

IForgeBackend& SourceForgePlatformService::Backend(const String& project)
{
	if(backend_override)
		return *backend_override;
	int i = default_backends.Find(project);
	if(i < 0) {
		SfConfig cfg;
		cfg.project = project;
		One<IForgeBackend> b;
		b.Create<SourceForgeForgeBackend>(cfg, String("https://sourceforge.net"));
		i = default_backends.Put(project, pick(b));
	}
	return *default_backends[i];
}

static String ForgeTimeText(Time t)
{
	return IsNull(t) ? String() : Format(t);
}

static SourceForgeTicket TicketFromIssue(const ForgeIssue& issue)
{
	SourceForgeTicket t;
	t.num = ScanInt(issue.issue_id);
	t.summary = issue.title;
	t.description = issue.body;
	t.author = issue.author;
	t.status = issue.open ? "open" : "closed";
	t.priority = "normal";
	t.milestone = "";
	t.created_at = ForgeTimeText(issue.created);
	return t;
}

static ValueMap CommentValue(const ForgeComment& c)
{
	ValueMap m;
	m.Set("author", c.author);
	m.Set("text", c.body);
	m.Set("created_at", ForgeTimeText(c.created));
	return m;
}

static ForgeResult RejectSurface(const char *op, const String& project, ForgeResult *result)
{
	ForgeResult r = IForgeBackend::Unsupported(op, project);
	if(result)
		*result = r;
	return r;
}

Vector<SourceForgeTicket> SourceForgePlatformService::GetTickets(const String& project, ForgeResult *result)
{
	Vector<ForgeIssue> issues;
	ForgeResult r = Backend(project).ListIssues(project, issues);
	if(result)
		*result = r;
	Vector<SourceForgeTicket> tickets;
	if(!r.ok)
		return tickets;
	for(int i = 0; i < issues.GetCount(); i++)
		tickets.Add(TicketFromIssue(issues[i]));
	return tickets;
}

SourceForgeTicket SourceForgePlatformService::GetTicket(const String& project, int num, ForgeResult *result)
{
	SourceForgeTicket t;
	t.num = num;
	ForgeIssue issue;
	ForgeResult r = Backend(project).GetIssue(project, AsString(num), issue);
	if(result)
		*result = r;
	if(!r.ok)
		return t;
	t = TicketFromIssue(issue);

	Vector<ForgeComment> comments;
	ForgeResult cr = Backend(project).ListComments(project, AsString(num), comments);
	if(!cr.ok) {
		if(result)
			*result = cr;
		return t;
	}
	for(int i = 0; i < comments.GetCount(); i++)
		t.comments.Add(CommentValue(comments[i]));
	return t;
}

int SourceForgePlatformService::CreateTicket(const String& project, const String& summary, const String& description, const String& priority, const String& milestone, const String& author, ForgeResult *result)
{
	ForgeIssue draft;
	draft.title = summary;
	draft.body = description;
	draft.author = author;
	ForgeIssue made;
	ForgeResult r = Backend(project).CreateIssue(project, draft, made);
	if(result)
		*result = r;
	return r.ok ? ScanInt(made.issue_id) : -1;
}

bool SourceForgePlatformService::AddTicketComment(const String& project, int num, const String& author, const String& comment, ForgeResult *result)
{
	ForgeComment draft;
	draft.author = author;
	draft.body = comment;
	ForgeComment saved;
	ForgeResult r = Backend(project).AddComment(project, AsString(num), draft, saved);
	if(result)
		*result = r;
	return r.ok;
}

bool SourceForgePlatformService::ToggleTicketStatus(const String& project, int num, const String& new_status, ForgeResult *result)
{
	bool open = true;
	if(new_status.IsEmpty()) {
		ForgeIssue current;
		ForgeResult g = Backend(project).GetIssue(project, AsString(num), current);
		if(!g.ok) {
			if(result)
				*result = g;
			return false;
		}
		open = !current.open;
	}
	else if(new_status == "closed")
		open = false;
	else if(new_status == "open" || new_status == "in-progress")
		open = true;
	else {
		ForgeResult r = IForgeBackend::Failed("set-issue-state", project, "unknown ticket status '" + new_status + "'");
		if(result)
			*result = r;
		return false;
	}
	ForgeIssue out;
	ForgeResult r = Backend(project).SetIssueState(project, AsString(num), open, out);
	if(result)
		*result = r;
	return r.ok;
}

Vector<SourceForgeWikiPage> SourceForgePlatformService::GetWikiPages(const String& project, ForgeResult *result)
{
	RejectSurface("wiki-pages", project, result);
	return Vector<SourceForgeWikiPage>();
}

SourceForgeWikiPage SourceForgePlatformService::GetWikiPage(const String& project, const String& slug, ForgeResult *result)
{
	RejectSurface("wiki-page", project, result);
	return SourceForgeWikiPage();
}

bool SourceForgePlatformService::SaveWikiPage(const String& project, const SourceForgeWikiPage& page, ForgeResult *result)
{
	RejectSurface("wiki-save", project, result);
	return false;
}

Vector<SourceForgeDiscussionTopic> SourceForgePlatformService::GetDiscussionTopics(const String& project, const String& forum, ForgeResult *result)
{
	RejectSurface("discussion-topics", project, result);
	return Vector<SourceForgeDiscussionTopic>();
}

SourceForgeDiscussionTopic SourceForgePlatformService::GetDiscussionTopic(const String& project, int id, ForgeResult *result)
{
	RejectSurface("discussion-topic", project, result);
	return SourceForgeDiscussionTopic();
}

int SourceForgePlatformService::CreateDiscussionTopic(const String& project, const String& forum, const String& title, const String& first_post, const String& author, ForgeResult *result)
{
	RejectSurface("discussion-create", project, result);
	return -1;
}

bool SourceForgePlatformService::AddDiscussionPost(const String& project, int topic_id, const String& author, const String& text, ForgeResult *result)
{
	RejectSurface("discussion-post", project, result);
	return false;
}

Vector<SourceForgeReleaseFile> SourceForgePlatformService::GetFiles(const String& project, ForgeResult *result)
{
	RejectSurface("files", project, result);
	return Vector<SourceForgeReleaseFile>();
}

END_UPP_NAMESPACE
