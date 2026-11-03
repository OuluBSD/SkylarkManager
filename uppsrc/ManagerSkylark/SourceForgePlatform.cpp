#include "ManagerSkylark.h"
#include "SourceForgePlatform.h"

NAMESPACE_UPP

static String CapitalizeWord(const String& s)
{
	if(s.IsEmpty()) return s;
	return ToUpper(s.Left(1)) + s.Mid(1);
}

static String GetSourceForgeDataDir(const String& project)
{
	String dir = AppendFileName(GetHomeDirFile(".aria/sites/sourceforge"), project.IsEmpty() ? "topside" : project);
	RealizeDirectory(dir);
	return dir;
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
// SourceForge Data Store Implementation
// ---------------------------------------------------------------------------

Vector<SourceForgeTicket> SourceForgePlatformService::GetTickets(const String& project)
{
	String path = AppendFileName(GetSourceForgeDataDir(project), "tickets_store.json");
	Vector<SourceForgeTicket> tickets;

	if(FileExists(path)) {
		Value v = ParseJSON(LoadFile(path));
		if(v.Is<ValueArray>()) {
			ValueArray va = v;
			for(int i = 0; i < va.GetCount(); i++) {
				SourceForgeTicket t;
				LoadFromJsonValue(t, va[i]);
				tickets.Add(pick(t));
			}
			return tickets;
		}
	}

	// Seed default tickets
	SourceForgeTicket& t1 = tickets.Add();
	t1.num = 1;
	t1.summary = "Establish SourceForge platform parity in ManagerSkylark";
	t1.status = "open";
	t1.priority = "high";
	t1.milestone = "Phase 13";
	t1.author = "sppp";
	t1.created_at = "2026-10-30";
	t1.description = "Implement interactive tickets tracker with lifecycle triage, live Wiki Markdown CMS, community discussion forums, and downloadable tarball releases for Topside.";
	ValueMap c1;
	c1.Set("author", "sppp");
	c1.Set("text", "Initial milestone started. Aria verification and interactive REST API endpoints being connected.");
	c1.Set("created_at", "2026-10-30");
	t1.comments.Add(c1);

	SourceForgeTicket& t2 = tickets.Add();
	t2.num = 2;
	t2.summary = "Connect product design workflow to SourceForge releases";
	t2.status = "open";
	t2.priority = "normal";
	t2.milestone = "Phase 14";
	t2.author = "sppp";
	t2.created_at = "2026-10-30";
	t2.description = "Design a unified workflow linking technical tasks in Manager to public product roadmaps and release downloads.";

	ValueArray va;
	for(int i = 0; i < tickets.GetCount(); i++)
		va.Add(StoreAsJsonValue(tickets[i]));
	SaveFile(path, AsJSON(va, true));

	return tickets;
}

SourceForgeTicket SourceForgePlatformService::GetTicket(const String& project, int num)
{
	Vector<SourceForgeTicket> list = GetTickets(project);
	for(int i = 0; i < list.GetCount(); i++) {
		if(list[i].num == num)
			return list[i];
	}
	SourceForgeTicket empty;
	empty.num = num;
	empty.summary = "Ticket #" + AsString(num) + " Not Found";
	return empty;
}

int SourceForgePlatformService::CreateTicket(const String& project, const String& summary, const String& description, const String& priority, const String& milestone, const String& author)
{
	Vector<SourceForgeTicket> list = GetTickets(project);
	int max_num = 0;
	for(int i = 0; i < list.GetCount(); i++) {
		if(list[i].num > max_num)
			max_num = list[i].num;
	}

	SourceForgeTicket& t = list.Add();
	t.num = max_num + 1;
	t.summary = summary;
	t.description = description;
	t.priority = priority.IsEmpty() ? "normal" : priority;
	t.milestone = milestone.IsEmpty() ? "General" : milestone;
	t.author = author.IsEmpty() ? "sppp" : author;
	t.status = "open";
	t.created_at = Format("%`", GetSysDate());

	String path = AppendFileName(GetSourceForgeDataDir(project), "tickets_store.json");
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	SaveFile(path, AsJSON(va, true));

	return t.num;
}

bool SourceForgePlatformService::AddTicketComment(const String& project, int num, const String& author, const String& comment)
{
	Vector<SourceForgeTicket> list = GetTickets(project);
	bool found = false;
	for(int i = 0; i < list.GetCount(); i++) {
		if(list[i].num == num) {
			ValueMap c;
			c.Set("author", author.IsEmpty() ? "sppp" : author);
			c.Set("text", comment);
			c.Set("created_at", Format("%`", GetSysDate()));
			list[i].comments.Add(c);
			found = true;
			break;
		}
	}
	if(!found) return false;

	String path = AppendFileName(GetSourceForgeDataDir(project), "tickets_store.json");
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	SaveFile(path, AsJSON(va, true));
	return true;
}

bool SourceForgePlatformService::ToggleTicketStatus(const String& project, int num, const String& new_status)
{
	Vector<SourceForgeTicket> list = GetTickets(project);
	bool found = false;
	for(int i = 0; i < list.GetCount(); i++) {
		if(list[i].num == num) {
			if(!new_status.IsEmpty())
				list[i].status = new_status;
			else if(list[i].status == "open" || list[i].status == "in-progress")
				list[i].status = "closed";
			else
				list[i].status = "open";
			found = true;
			break;
		}
	}
	if(!found) return false;

	String path = AppendFileName(GetSourceForgeDataDir(project), "tickets_store.json");
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	SaveFile(path, AsJSON(va, true));
	return true;
}

Vector<SourceForgeWikiPage> SourceForgePlatformService::GetWikiPages(const String& project)
{
	String path = AppendFileName(GetSourceForgeDataDir(project), "wiki_store.json");
	Vector<SourceForgeWikiPage> pages;

	if(FileExists(path)) {
		Value v = ParseJSON(LoadFile(path));
		if(v.Is<ValueArray>()) {
			ValueArray va = v;
			for(int i = 0; i < va.GetCount(); i++) {
				SourceForgeWikiPage p;
				LoadFromJsonValue(p, va[i]);
				pages.Add(pick(p));
			}
			return pages;
		}
	}

	// Seed default wiki pages
	SourceForgeWikiPage& p1 = pages.Add();
	p1.title = "Home";
	p1.slug = "Home";
	p1.author = "sppp";
	p1.updated_at = "2026-10-30";
	p1.content =
		"## Welcome to the Topside Project Wiki\n\n"
		"Topside is an agentic software development and desktop framework powered by **Ultimate++** and autonomous AI agents.\n\n"
		"### Core Pillars\n"
		"- **Self-Hosted Workstations**: Full native control over workstation hardware, filesystem, and storage.\n"
		"- **Mesh-Hosted Nodes**: Distributed cluster fleet with equal-parity files, repositories, and SSH git execution.\n"
		"- **GitHub Hub**: Seamless social repository integration, live gh issues, actions, and projects Kanban.\n"
		"- **SourceForge Hub**: Product marketing, distribution tarballs, tickets, and community governance.\n"
		"- **SweAgent AI**: Autonomous pair programming and benchmark execution.\n\n"
		"### Getting Started\n"
		"Clone the repository:\n"
		"```bash\n"
		"git clone https://git.code.sf.net/p/topside/code topside\n"
		"cd topside\n"
		"./bin/build -m CLANG reference/ManagerSkylark\n"
		"```\n";

	SourceForgeWikiPage& p2 = pages.Add();
	p2.title = "Architecture Overview";
	p2.slug = "Architecture";
	p2.author = "sppp";
	p2.updated_at = "2026-10-30";
	p2.content =
		"## Topside Architecture Overview\n\n"
		"### Subsystem Layout\n"
		"- **uppsrc/WebFramework**: Semantic PageBuilder, templates (MegaMenu, GitHub, SourceForge), density engine, and dual tree dock.\n"
		"- **reference/ManagerSkylark**: High-performance asynchronous web server coordinating local workstation, mesh fleet, and cloud repos.\n"
		"- **uppsrc/Aria**: Headless browser automation and verification driver using Marionette/WebDriver.\n"
		"- **uppsrc/Manager**: Local project, phase, and task lifecycle tracking engine.\n";

	SourceForgeWikiPage& p3 = pages.Add();
	p3.title = "Product Design Workflow";
	p3.slug = "ProductDesign";
	p3.author = "sppp";
	p3.updated_at = "2026-10-30";
	p3.content =
		"## Product Design Workflow\n\n"
		"### From Technical Tasks to Marketable Products\n"
		"1. **Task Execution**: Tasks in Manager develop features incrementally.\n"
		"2. **Release Milestone**: Completed phases are bundled into versioned releases.\n"
		"3. **Distribution Packaging**: Artifacts are packaged into tarballs and checksummed.\n"
		"4. **Public Exposure**: Announcements are published to SourceForge devlog and mailing lists.\n";

	ValueArray va;
	for(int i = 0; i < pages.GetCount(); i++)
		va.Add(StoreAsJsonValue(pages[i]));
	SaveFile(path, AsJSON(va, true));

	return pages;
}

SourceForgeWikiPage SourceForgePlatformService::GetWikiPage(const String& project, const String& slug)
{
	String s = slug.IsEmpty() ? "Home" : slug;
	Vector<SourceForgeWikiPage> pages = GetWikiPages(project);
	for(int i = 0; i < pages.GetCount(); i++) {
		if(ToLower(pages[i].slug) == ToLower(s))
			return pages[i];
	}
	SourceForgeWikiPage p;
	p.title = s;
	p.slug = s;
	p.author = "sppp";
	p.content = "## " + s + "\n\n*This page does not exist yet. Click Edit to create it.*";
	return p;
}

bool SourceForgePlatformService::SaveWikiPage(const String& project, const SourceForgeWikiPage& page)
{
	Vector<SourceForgeWikiPage> pages = GetWikiPages(project);
	bool found = false;
	for(int i = 0; i < pages.GetCount(); i++) {
		if(ToLower(pages[i].slug) == ToLower(page.slug)) {
			pages[i].title = page.title;
			pages[i].content = page.content;
			pages[i].author = page.author.IsEmpty() ? "sppp" : page.author;
			pages[i].updated_at = Format("%`", GetSysDate());
			pages[i].version++;
			found = true;
			break;
		}
	}
	if(!found) {
		SourceForgeWikiPage& np = pages.Add();
		np = page;
		np.updated_at = Format("%`", GetSysDate());
		np.version = 1;
	}

	String path = AppendFileName(GetSourceForgeDataDir(project), "wiki_store.json");
	ValueArray va;
	for(int i = 0; i < pages.GetCount(); i++)
		va.Add(StoreAsJsonValue(pages[i]));
	SaveFile(path, AsJSON(va, true));
	return true;
}

Vector<SourceForgeDiscussionTopic> SourceForgePlatformService::GetDiscussionTopics(const String& project, const String& forum)
{
	String path = AppendFileName(GetSourceForgeDataDir(project), "discussion_store.json");
	Vector<SourceForgeDiscussionTopic> topics;

	if(FileExists(path)) {
		Value v = ParseJSON(LoadFile(path));
		if(v.Is<ValueArray>()) {
			ValueArray va = v;
			for(int i = 0; i < va.GetCount(); i++) {
				SourceForgeDiscussionTopic t;
				LoadFromJsonValue(t, va[i]);
				if(forum.IsEmpty() || ToLower(t.forum) == ToLower(forum))
					topics.Add(pick(t));
			}
			return topics;
		}
	}

	// Seed default discussion topics
	SourceForgeDiscussionTopic& t1 = topics.Add();
	t1.id = 1;
	t1.forum = "general";
	t1.title = "Welcome to Topside Community Discussions";
	t1.author = "sppp";
	t1.created_at = "2026-10-30";
	ValueMap p1;
	p1.Set("author", "sppp");
	p1.Set("text", "Welcome everyone! This is the official community forum for Topside. Feel free to share your agent configurations and tool integrations.");
	p1.Set("created_at", "2026-10-30");
	t1.posts.Add(p1);

	SourceForgeDiscussionTopic& t2 = topics.Add();
	t2.id = 2;
	t2.forum = "development";
	t2.title = "Architecture RFC: Universal Multi-Host Mesh Engine";
	t2.author = "sppp";
	t2.created_at = "2026-10-30";
	ValueMap p2;
	p2.Set("author", "sppp");
	p2.Set("text", "Please review the proposed architecture for low-latency multi-node code execution and synchronization over SSH.");
	p2.Set("created_at", "2026-10-30");
	t2.posts.Add(p2);

	ValueArray va;
	for(int i = 0; i < topics.GetCount(); i++)
		va.Add(StoreAsJsonValue(topics[i]));
	SaveFile(path, AsJSON(va, true));

	if(!forum.IsEmpty()) {
		Vector<SourceForgeDiscussionTopic> filtered;
		for(int i = 0; i < topics.GetCount(); i++) {
			if(ToLower(topics[i].forum) == ToLower(forum))
				filtered.Add(topics[i]);
		}
		return filtered;
	}
	return topics;
}

SourceForgeDiscussionTopic SourceForgePlatformService::GetDiscussionTopic(const String& project, int id)
{
	Vector<SourceForgeDiscussionTopic> list = GetDiscussionTopics(project);
	for(int i = 0; i < list.GetCount(); i++) {
		if(list[i].id == id)
			return list[i];
	}
	SourceForgeDiscussionTopic empty;
	empty.id = id;
	empty.title = "Topic #" + AsString(id) + " Not Found";
	return empty;
}

int SourceForgePlatformService::CreateDiscussionTopic(const String& project, const String& forum, const String& title, const String& first_post, const String& author)
{
	Vector<SourceForgeDiscussionTopic> list = GetDiscussionTopics(project);
	int max_id = 0;
	for(int i = 0; i < list.GetCount(); i++) {
		if(list[i].id > max_id)
			max_id = list[i].id;
	}

	SourceForgeDiscussionTopic& t = list.Add();
	t.id = max_id + 1;
	t.forum = forum.IsEmpty() ? "general" : forum;
	t.title = title;
	t.author = author.IsEmpty() ? "sppp" : author;
	t.created_at = Format("%`", GetSysDate());

	ValueMap p;
	p.Set("author", t.author);
	p.Set("text", first_post);
	p.Set("created_at", t.created_at);
	t.posts.Add(p);

	String path = AppendFileName(GetSourceForgeDataDir(project), "discussion_store.json");
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	SaveFile(path, AsJSON(va, true));

	return t.id;
}

bool SourceForgePlatformService::AddDiscussionPost(const String& project, int topic_id, const String& author, const String& text)
{
	Vector<SourceForgeDiscussionTopic> list = GetDiscussionTopics(project);
	bool found = false;
	for(int i = 0; i < list.GetCount(); i++) {
		if(list[i].id == topic_id) {
			ValueMap p;
			p.Set("author", author.IsEmpty() ? "sppp" : author);
			p.Set("text", text);
			p.Set("created_at", Format("%`", GetSysDate()));
			list[i].posts.Add(p);
			found = true;
			break;
		}
	}
	if(!found) return false;

	String path = AppendFileName(GetSourceForgeDataDir(project), "discussion_store.json");
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	SaveFile(path, AsJSON(va, true));
	return true;
}

Vector<SourceForgeReleaseFile> SourceForgePlatformService::GetFiles(const String& project)
{
	String path = AppendFileName(GetSourceForgeDataDir(project), "files_store.json");
	Vector<SourceForgeReleaseFile> files;

	if(FileExists(path)) {
		Value v = ParseJSON(LoadFile(path));
		if(v.Is<ValueArray>()) {
			ValueArray va = v;
			for(int i = 0; i < va.GetCount(); i++) {
				SourceForgeReleaseFile f;
				LoadFromJsonValue(f, va[i]);
				files.Add(pick(f));
			}
			return files;
		}
	}

	// Seed default release packages
	SourceForgeReleaseFile& f1 = files.Add();
	f1.name = "topside-1.0.0-linux-x86_64.tar.gz";
	f1.version = "1.0.0";
	f1.date = "2026-10-30";
	f1.size_bytes = 14889728;
	f1.size_str = "14.2 MB";
	f1.downloads = 142;
	f1.sha256 = "a93b4f881c20e54d33a948e91c28c894e772b184f09a88c3c2921094184c8a2b";
	f1.url = "/projects/" + project + "/files/" + f1.name;

	SourceForgeReleaseFile& f2 = files.Add();
	f2.name = "topside-1.0.0-windows-x86_64.zip";
	f2.version = "1.0.0";
	f2.date = "2026-10-30";
	f2.size_bytes = 17616076;
	f2.size_str = "16.8 MB";
	f2.downloads = 98;
	f2.sha256 = "7f1e92d90823c4a22b918a381c8411d95e0c29f8a32a184c82901194382c7a10";
	f2.url = "/projects/" + project + "/files/" + f2.name;

	SourceForgeReleaseFile& f3 = files.Add();
	f3.name = "topside-source-master.zip";
	f3.version = "Snapshot";
	f3.date = "2026-10-30";
	f3.size_bytes = 9332326;
	f3.size_str = "8.9 MB";
	f3.downloads = 215;
	f3.sha256 = "18f8e32909418a22b94c3918a829184c9e011948a391c84a82901194382c7a33";
	f3.url = "/projects/" + project + "/files/" + f3.name;

	ValueArray va;
	for(int i = 0; i < files.GetCount(); i++)
		va.Add(StoreAsJsonValue(files[i]));
	SaveFile(path, AsJSON(va, true));

	return files;
}

// ---------------------------------------------------------------------------
// SKYLARK Handlers for SourceForge Parity
// ---------------------------------------------------------------------------

static void RenderSourceForgeView(Http& http, const String& project, const String& subpath)
{
	String p = project.IsEmpty() ? "topside" : project;
	SourceForgeProjectInfo info = SourceForgePlatformService::GetProject(p);

	String path = TrimBoth(subpath);
	String tab = http["tab"];

	Vector<String> parts = Split(path, '/');
	if(parts.GetCount() > 0)
		tab = parts[0];

	if(tab.IsEmpty()) tab = "summary";

	int ticket_num = 0;
	if(tab == "tickets" && parts.GetCount() > 1 && IsDigit(parts[1][0])) {
		ticket_num = ScanInt(parts[1]);
	}

	String wiki_slug = "Home";
	if(tab == "wiki" && parts.GetCount() > 1) {
		wiki_slug = parts[1];
	}

	int topic_id = 0;
	String forum_name = "";
	if(tab == "discussion") {
		if(parts.GetCount() >= 3 && parts[1] == "topic" && IsDigit(parts[2][0])) {
			topic_id = ScanInt(parts[2]);
		}
		else if(parts.GetCount() >= 2) {
			forum_name = parts[1];
		}
	}

	String content;
	String sidebar = SourceForgePlatformService::RenderSidebar(info);

	// 1. Summary View
	if(tab == "summary") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Description</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p style=\"font-size:1.05rem; line-height:1.6;\">" << info.description << "</p>\n"
			<< "    <p>Topside combines high-performance C++ backend architectures with autonomous agent loops, comprehensive browser testing via Aria, and enterprise project tracking via Manager.</p>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Key Features</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <ul style=\"margin:0; padding-left:1.2rem; line-height:1.8;\">\n";
		for(int i = 0; i < info.features.GetCount(); i++) {
			content << "      <li>" << info.features[i] << "</li>\n";
		}
		content
			<< "    </ul>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>User Reviews</span><a href=\"/projects/" << info.name << "/reviews/\" class=\"wf-btn wf-btn-secondary\" style=\"padding:2px 8px; font-size:0.75rem;\">Write a Review</a></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div style=\"display:flex; gap:12px; align-items:flex-start;\">\n"
			<< "      <div style=\"width:36px; height:36px; border-radius:50%; background:#ee6524; color:#fff; display:flex; align-items:center; justify-content:center; font-weight:700;\">S</div>\n"
			<< "      <div>\n"
			<< "        <div style=\"font-weight:600;\">sppp <span style=\"color:#f59f00;\">&#9733;&#9733;&#9733;&#9733;&#9733;</span></div>\n"
			<< "        <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8c929a); margin-bottom:6px;\">Posted today</div>\n"
			<< "        <p style=\"margin:0; font-size:0.9rem;\">Topside provides unmatched autonomy for AI-assisted engineering with U++. Zero latency, native performance, and full platform parity.</p>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 2. Files & Download Packages View
	else if(tab == "files") {
		Vector<SourceForgeReleaseFile> files = SourceForgePlatformService::GetFiles(info.name);
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Files &amp; Download Packages</span><span class=\"wf-badge wf-badge-neutral\">Stable Releases</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>Package File</th><th>Release</th><th>Date</th><th>Size</th><th>Downloads</th><th>SHA-256 Hash</th><th>Action</th></tr></thead>\n"
			<< "      <tbody>\n";
		for(int i = 0; i < files.GetCount(); i++) {
			const SourceForgeReleaseFile& f = files[i];
			content
				<< "        <tr>\n"
				<< "          <td>" << VividIcon::Get("package", 16) << " <strong>" << f.name << "</strong></td>\n"
				<< "          <td><span class=\"wf-badge wf-badge-emerald\">" << f.version << "</span></td>\n"
				<< "          <td>" << f.date << "</td>\n"
				<< "          <td>" << f.size_str << "</td>\n"
				<< "          <td><strong>" << f.downloads << "</strong></td>\n"
				<< "          <td><code style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8c929a);\" title=\"" << f.sha256 << "\">" << f.sha256.Left(12) << "...</code></td>\n"
				<< "          <td><a href=\"" << f.url << "\" class=\"wf-btn wf-btn-primary\" style=\"padding:3px 10px; font-size:0.75rem; background:#ee6524; border-color:#ee6524; display:inline-flex; align-items:center; gap:4px;\">" << VividIcon::Get("download", 13) << " Download</a></td>\n"
				<< "        </tr>\n";
		}
		content << "      </tbody>\n    </table>\n  </div>\n</div>\n";
	}
	// 3. Tickets Tracker View (List or Conversation View)
	else if(tab == "tickets" && ticket_num > 0) {
		SourceForgeTicket t = SourceForgePlatformService::GetTicket(info.name, ticket_num);
		bool is_open = (t.status == "open" || t.status == "in-progress");

		content
			<< "<div style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div style=\"margin-bottom:0.75rem;\">\n"
			<< "    <a href=\"/p/" << info.name << "/tickets/\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.82rem; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("chevron-left", 14) << " Back to Tickets</a>\n"
			<< "  </div>\n"
			<< "  <h1 style=\"font-size:1.75rem; font-weight:600; margin:0 0 8px 0; color:var(--wf-fg-normal,#e6edf3);\">"
			<< DeHtml(t.summary) << " <span style=\"color:var(--wf-fg-inactive,#7d8590); font-weight:400;\">#" << t.num << "</span></h1>\n"
			<< "  <div style=\"display:flex; align-items:center; gap:10px; font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590); flex-wrap:wrap;\">\n"
			<< "    <span class=\"wf-badge\" style=\"background:" << (is_open ? "#238636" : "#8957e5") << "; color:#ffffff; font-weight:700; padding:4px 10px; border-radius:12px; font-size:0.82rem; display:inline-flex; align-items:center; gap:4px;\">"
			<< VividIcon::Get(is_open ? "alert-circle" : "check", 14) << " " << (is_open ? "Open" : "Closed") << "</span>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">Priority: " << CapitalizeWord(t.priority) << "</span>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">Milestone: " << t.milestone << "</span>\n"
			<< "    <span>Created by <strong>" << t.author << "</strong> on " << t.created_at << "</span>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Ticket Description Card
		content
			<< "<div class=\"wf-card\" style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div class=\"wf-card-header\" style=\"justify-content:space-between;\">\n"
			<< "    <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << t.author << "</strong> <span style=\"color:var(--wf-fg-inactive,#8c929a); font-size:0.82rem;\">opened this ticket on " << t.created_at << "</span></div>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">Author</span>\n"
			<< "  </div>\n"
			<< "  <div class=\"wf-card-body\" style=\"font-size:0.92rem; line-height:1.6;\">\n"
			<< (!t.description.IsEmpty() ? DeHtml(t.description) : "<em>No description provided.</em>") << "\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Comments Timeline
		for(int i = 0; i < t.comments.GetCount(); i++) {
			const ValueMap& c = t.comments[i];
			String c_author = c["author"];
			String c_text = c["text"];
			String c_created = c["created_at"];

			content
				<< "<div class=\"wf-card\" style=\"margin-bottom:1.25rem;\">\n"
				<< "  <div class=\"wf-card-header\">\n"
				<< "    <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << c_author << "</strong> <span style=\"color:var(--wf-fg-inactive,#8c929a); font-size:0.82rem;\">commented on " << c_created << "</span></div>\n"
				<< "  </div>\n"
				<< "  <div class=\"wf-card-body\" style=\"font-size:0.92rem; line-height:1.6;\">\n"
				<< DeHtml(c_text) << "\n"
				<< "  </div>\n"
				<< "</div>\n";
		}

		// Comment & Triage Controls
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Add a Comment &amp; Update Status</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <textarea id=\"sf-comment-text\" placeholder=\"Write your comment or update...\" style=\"width:100%; height:90px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:6px; padding:10px; font-family:inherit; margin-bottom:12px;\"></textarea>\n"
			<< "    <div style=\"display:flex; gap:10px; align-items:center;\">\n"
			<< "      <button type=\"button\" id=\"btn-sf-submit-comment\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; padding:6px 14px; font-weight:600;\" onclick=\"SourceForgeUI.submitComment('" << info.name << "', " << t.num << ")\">Add Comment</button>\n"
			<< "      <button type=\"button\" id=\"btn-sf-toggle-ticket\" class=\"wf-btn wf-btn-secondary\" style=\"padding:6px 14px;\" onclick=\"SourceForgeUI.toggleTicket('" << info.name << "', " << t.num << ")\">"
			<< (is_open ? "Close Ticket" : "Reopen Ticket") << "</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(tab == "tickets") {
		Vector<SourceForgeTicket> tickets = SourceForgePlatformService::GetTickets(info.name);

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1.25rem;\">\n"
			<< "  <div style=\"display:flex; align-items:center; gap:8px;\">\n"
			<< "    <span style=\"font-size:1.3rem; font-weight:700;\">Tickets &amp; Bug Tracker</span>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">" << tickets.GetCount() << " Tickets</span>\n"
			<< "  </div>\n"
			<< "  <button type=\"button\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; display:inline-flex; align-items:center; gap:6px; font-weight:600;\" onclick=\"SourceForgeUI.toggleNewTicketCard()\">"
			<< VividIcon::Get("plus", 14) << " Create Ticket</button>\n"
			<< "</div>\n";

		// New Ticket Creation Card (Collapsible)
		content
			<< "<div id=\"sf-new-ticket-card\" class=\"wf-card\" style=\"display:none; margin-bottom:1.5rem; border:1px solid #ee6524;\">\n"
			<< "  <div class=\"wf-card-header\"><span>Create New Ticket</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:12px;\">\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Summary</label>\n"
			<< "      <input type=\"text\" id=\"sf-ticket-summary\" placeholder=\"Brief summary of the issue or feature...\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "    </div>\n"
			<< "    <div style=\"display:grid; grid-template-columns:1fr 1fr; gap:12px;\">\n"
			<< "      <div>\n"
			<< "        <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Priority</label>\n"
			<< "        <select id=\"sf-ticket-priority\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "          <option value=\"low\">Low</option>\n"
			<< "          <option value=\"normal\" selected>Normal</option>\n"
			<< "          <option value=\"high\">High</option>\n"
			<< "          <option value=\"urgent\">Urgent</option>\n"
			<< "        </select>\n"
			<< "      </div>\n"
			<< "      <div>\n"
			<< "        <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Milestone</label>\n"
			<< "        <input type=\"text\" id=\"sf-ticket-milestone\" value=\"Phase 13\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Description</label>\n"
			<< "      <textarea id=\"sf-ticket-desc\" placeholder=\"Detailed description, reproduction steps, or requirements...\" style=\"width:100%; height:90px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:8px 10px; font-family:inherit;\"></textarea>\n"
			<< "    </div>\n"
			<< "    <div style=\"display:flex; gap:10px;\">\n"
			<< "      <button type=\"button\" id=\"btn-sf-create-ticket\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524;\" onclick=\"SourceForgeUI.createTicket('" << info.name << "')\">Submit Ticket</button>\n"
			<< "      <button type=\"button\" class=\"wf-btn wf-btn-secondary\" onclick=\"SourceForgeUI.toggleNewTicketCard()\">Cancel</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Tickets List Table
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>#</th><th>Summary</th><th>Status</th><th>Priority</th><th>Milestone</th><th>Author</th><th>Comments</th></tr></thead>\n"
			<< "      <tbody>\n";
		for(int i = 0; i < tickets.GetCount(); i++) {
			const SourceForgeTicket& t = tickets[i];
			bool is_op = (t.status == "open" || t.status == "in-progress");
			content
				<< "        <tr>\n"
				<< "          <td>#" << t.num << "</td>\n"
				<< "          <td><a href=\"/p/" << info.name << "/tickets/" << t.num << "\" style=\"font-weight:600; color:#ee6524; text-decoration:none;\">" << DeHtml(t.summary) << "</a></td>\n"
				<< "          <td><span class=\"wf-badge\" style=\"background:" << (is_op ? "#238636" : "#8957e5") << "; color:#fff;\">" << CapitalizeWord(t.status) << "</span></td>\n"
				<< "          <td><span class=\"wf-badge wf-badge-" << (t.priority == "high" || t.priority == "urgent" ? "rose" : "amber") << "\">" << CapitalizeWord(t.priority) << "</span></td>\n"
				<< "          <td>" << t.milestone << "</td>\n"
				<< "          <td>" << t.author << "</td>\n"
				<< "          <td>" << t.comments.GetCount() << "</td>\n"
				<< "        </tr>\n";
		}
		content << "      </tbody>\n    </table>\n  </div>\n</div>\n";
	}
	// 4. Wiki CMS View (View or Edit Mode)
	else if(tab == "wiki") {
		SourceForgeWikiPage page = SourceForgePlatformService::GetWikiPage(info.name, wiki_slug);
		Vector<SourceForgeWikiPage> pages = SourceForgePlatformService::GetWikiPages(info.name);
		bool edit_mode = (http["edit"] == "1");

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1.25rem;\">\n"
			<< "  <div style=\"display:flex; align-items:center; gap:8px;\">\n"
			<< "    <span style=\"font-size:1.3rem; font-weight:700;\">Topside Wiki &middot; " << page.title << "</span>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">v" << page.version << "</span>\n"
			<< "  </div>\n"
			<< "  <div style=\"display:flex; gap:8px;\">\n";
		if(!edit_mode) {
			content << "    <a href=\"/p/" << info.name << "/wiki/" << page.slug << "/?edit=1\" id=\"btn-sf-edit-wiki\" class=\"wf-btn wf-btn-secondary\" style=\"display:inline-flex; align-items:center; gap:6px;\">" << VividIcon::Get("edit", 14) << " Edit Page</a>\n";
		} else {
			content << "    <a href=\"/p/" << info.name << "/wiki/" << page.slug << "/\" class=\"wf-btn wf-btn-secondary\">Cancel Editing</a>\n";
		}
		content << "  </div>\n</div>\n";

		// Sub-navigation bar of wiki pages
		content
			<< "<div style=\"display:flex; gap:8px; margin-bottom:1.25rem; flex-wrap:wrap;\">\n";
		for(int i = 0; i < pages.GetCount(); i++) {
			bool is_act_p = (ToLower(pages[i].slug) == ToLower(page.slug));
			content << "  <a href=\"/p/" << info.name << "/wiki/" << pages[i].slug << "/\" class=\"wf-btn " << (is_act_p ? "wf-btn-primary" : "wf-btn-secondary") << "\" style=\"font-size:0.8rem; " << (is_act_p ? "background:#ee6524; border-color:#ee6524;" : "") << "\">" << pages[i].title << "</a>\n";
		}
		content << "</div>\n";

		if(edit_mode) {
			content
				<< "<div class=\"wf-card\">\n"
				<< "  <div class=\"wf-card-header\"><span>Editing Wiki Page: " << page.title << "</span></div>\n"
				<< "  <div class=\"wf-card-body\">\n"
				<< "    <input type=\"text\" id=\"sf-wiki-title\" value=\"" << page.title << "\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px; margin-bottom:12px; font-weight:700;\">\n"
				<< "    <textarea id=\"sf-wiki-content\" style=\"width:100%; height:260px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:6px; padding:10px; font-family:monospace; line-height:1.5; margin-bottom:12px;\">" << DeHtml(page.content) << "</textarea>\n"
				<< "    <div style=\"display:flex; justify-content:space-between; align-items:center;\">\n"
				<< "      <button type=\"button\" id=\"btn-sf-save-wiki\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; padding:6px 14px; font-weight:600;\" onclick=\"SourceForgeUI.saveWikiPage('" << info.name << "', '" << page.slug << "')\">Save Changes</button>\n"
				<< "      <a href=\"/p/" << info.name << "/wiki/" << page.slug << "/\" class=\"wf-btn wf-btn-secondary\">Cancel</a>\n"
				<< "    </div>\n"
				<< "  </div>\n"
				<< "</div>\n";
		} else {
			content
				<< "<div class=\"wf-card markdown-render-box\">\n"
				<< "  <textarea class=\"raw-markdown\" style=\"display:none;\">" << DeHtml(page.content) << "</textarea>\n"
				<< "  <div class=\"wf-card-body markdown-body\" id=\"sf-wiki-render\" style=\"padding:1.5rem; line-height:1.7; font-size:0.95rem;\">" << DeHtml(page.content) << "</div>\n"
				<< "</div>\n";
		}
	}
	// 5. Discussion Forums View (Topic Thread or Forum List)
	else if(tab == "discussion" && topic_id > 0) {
		SourceForgeDiscussionTopic topic = SourceForgePlatformService::GetDiscussionTopic(info.name, topic_id);

		content
			<< "<div style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div style=\"margin-bottom:0.75rem;\">\n"
			<< "    <a href=\"/p/" << info.name << "/discussion/\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.82rem; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("chevron-left", 14) << " Back to Forums</a>\n"
			<< "  </div>\n"
			<< "  <h1 style=\"font-size:1.75rem; font-weight:600; margin:0 0 8px 0; color:var(--wf-fg-normal,#e6edf3);\">"
			<< DeHtml(topic.title) << "</h1>\n"
			<< "  <div style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590);\">\n"
			<< "    Posted in <strong>" << CapitalizeWord(topic.forum) << "</strong> by <strong>" << topic.author << "</strong> on " << topic.created_at << "\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Posts Timeline
		for(int i = 0; i < topic.posts.GetCount(); i++) {
			const ValueMap& p_post = topic.posts[i];
			String p_author = p_post["author"];
			String p_text = p_post["text"];
			String p_created = p_post["created_at"];

			content
				<< "<div class=\"wf-card\" style=\"margin-bottom:1.25rem;\">\n"
				<< "  <div class=\"wf-card-header\">\n"
				<< "    <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << p_author << "</strong> <span style=\"color:var(--wf-fg-inactive,#8c929a); font-size:0.82rem;\">" << p_created << "</span></div>\n"
				<< "  </div>\n"
				<< "  <div class=\"wf-card-body\" style=\"font-size:0.92rem; line-height:1.6;\">\n"
				<< DeHtml(p_text) << "\n"
				<< "  </div>\n"
				<< "</div>\n";
		}

		// Post Reply Form
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Post a Reply</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <textarea id=\"sf-post-text\" placeholder=\"Write your reply to this discussion...\" style=\"width:100%; height:90px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:6px; padding:10px; font-family:inherit; margin-bottom:12px;\"></textarea>\n"
			<< "    <button type=\"button\" id=\"btn-sf-submit-post\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; padding:6px 14px; font-weight:600;\" onclick=\"SourceForgeUI.submitPost('" << info.name << "', " << topic.id << ")\">Post Reply</button>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(tab == "discussion") {
		Vector<SourceForgeDiscussionTopic> topics = SourceForgePlatformService::GetDiscussionTopics(info.name, forum_name);

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1.25rem;\">\n"
			<< "  <div style=\"display:flex; align-items:center; gap:8px;\">\n"
			<< "    <span style=\"font-size:1.3rem; font-weight:700;\">Forums &amp; Discussion</span>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">" << topics.GetCount() << " Topics</span>\n"
			<< "  </div>\n"
			<< "  <button type=\"button\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; display:inline-flex; align-items:center; gap:6px; font-weight:600;\" onclick=\"SourceForgeUI.toggleNewTopicCard()\">"
			<< VividIcon::Get("plus", 14) << " Create Topic</button>\n"
			<< "</div>\n";

		// New Topic Creation Card (Collapsible)
		content
			<< "<div id=\"sf-new-topic-card\" class=\"wf-card\" style=\"display:none; margin-bottom:1.5rem; border:1px solid #ee6524;\">\n"
			<< "  <div class=\"wf-card-header\"><span>Create New Topic</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:12px;\">\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Forum</label>\n"
			<< "      <select id=\"sf-topic-forum\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "        <option value=\"general\">General Discussion</option>\n"
			<< "        <option value=\"development\">Development &amp; Architecture</option>\n"
			<< "      </select>\n"
			<< "    </div>\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Topic Title</label>\n"
			<< "      <input type=\"text\" id=\"sf-topic-title\" placeholder=\"Subject or discussion title...\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "    </div>\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Message</label>\n"
			<< "      <textarea id=\"sf-topic-first-post\" placeholder=\"Write your topic message...\" style=\"width:100%; height:90px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:8px 10px; font-family:inherit;\"></textarea>\n"
			<< "    </div>\n"
			<< "    <div style=\"display:flex; gap:10px;\">\n"
			<< "      <button type=\"button\" id=\"btn-sf-create-topic\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524;\" onclick=\"SourceForgeUI.createTopic('" << info.name << "')\">Create Topic</button>\n"
			<< "      <button type=\"button\" class=\"wf-btn wf-btn-secondary\" onclick=\"SourceForgeUI.toggleNewTopicCard()\">Cancel</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Topics List Table
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>Topic Title</th><th>Forum</th><th>Author</th><th>Posts</th><th>Date</th></tr></thead>\n"
			<< "      <tbody>\n";
		for(int i = 0; i < topics.GetCount(); i++) {
			const SourceForgeDiscussionTopic& top = topics[i];
			content
				<< "        <tr>\n"
				<< "          <td><a href=\"/p/" << info.name << "/discussion/topic/" << top.id << "\" style=\"font-weight:600; color:#ee6524; text-decoration:none;\">" << DeHtml(top.title) << "</a></td>\n"
				<< "          <td><span class=\"wf-badge wf-badge-neutral\">" << CapitalizeWord(top.forum) << "</span></td>\n"
				<< "          <td>" << top.author << "</td>\n"
				<< "          <td><strong>" << top.posts.GetCount() << "</strong></td>\n"
				<< "          <td>" << top.created_at << "</td>\n"
				<< "        </tr>\n";
		}
		content << "      </tbody>\n    </table>\n  </div>\n</div>\n";
	}
	// 6. Reviews View
	else if(tab == "reviews") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Community Reviews &middot; 5.0 out of 5 stars</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div style=\"display:flex; align-items:center; gap:20px; margin-bottom:20px; padding-bottom:16px; border-bottom:1px solid var(--wf-border-color,#393e46);\">\n"
			<< "      <div style=\"font-size:2.8rem; font-weight:700; color:#ee6524;\">5.0</div>\n"
			<< "      <div>\n"
			<< "        <div style=\"color:#f59f00; font-size:1.2rem;\">&#9733;&#9733;&#9733;&#9733;&#9733;</div>\n"
			<< "        <div style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8c929a);\">100% of reviewers recommend this project</div>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "    <div style=\"margin-bottom:20px;\">\n"
			<< "      <h4 style=\"margin-top:0; margin-bottom:10px;\">Write a Review</h4>\n"
			<< "      <div style=\"display:flex; gap:6px; margin-bottom:10px; color:#f59f00; font-size:1.4rem; cursor:pointer;\">&#9733;&#9733;&#9733;&#9733;&#9733;</div>\n"
			<< "      <textarea placeholder=\"Share your experience with Topside...\" style=\"width:100%; height:80px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:8px; font-family:inherit; margin-bottom:8px;\"></textarea>\n"
			<< "      <button class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524;\">Submit Review</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 7. Support View
	else if(tab == "support") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Topside Support &amp; Community Resources</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p>Get assistance with installation, tool integration, and autonomous agents.</p>\n"
			<< "    <div class=\"wf-grid wf-grid-2\" style=\"gap:16px; margin-top:16px;\">\n"
			<< "      <div class=\"wf-card\" style=\"margin-bottom:0;\">\n"
			<< "        <div class=\"wf-card-header\"><span>" << VividIcon::Get("message-square", 16) << " Community Forum</span></div>\n"
			<< "        <div class=\"wf-card-body\" style=\"font-size:0.85rem;\">Ask questions, share workflows, and discuss features with the maintainers.<br><br><a href=\"/p/" << info.name << "/discussion/\" class=\"wf-btn wf-btn-secondary\">Visit Discussion</a></div>\n"
			<< "      </div>\n"
			<< "      <div class=\"wf-card\" style=\"margin-bottom:0;\">\n"
			<< "        <div class=\"wf-card-header\"><span>" << VividIcon::Get("bug", 16) << " Issue Tracker</span></div>\n"
			<< "        <div class=\"wf-card-body\" style=\"font-size:0.85rem;\">Submit bug reports, feature requests, and follow development milestones.<br><br><a href=\"/p/" << info.name << "/tickets/\" class=\"wf-btn wf-btn-secondary\">View Tickets</a></div>\n"
			<< "      </div>\n"
			<< "      <div class=\"wf-card\" style=\"margin-bottom:0;\">\n"
			<< "        <div class=\"wf-card-header\"><span>" << VividIcon::Get("mail", 16) << " Mailing Lists</span></div>\n"
			<< "        <div class=\"wf-card-body\" style=\"font-size:0.85rem;\">Subscribe to <code>topside-discuss</code> for email updates and discussions.<br><br><a href=\"/p/" << info.name << "/mailman/\" class=\"wf-btn wf-btn-secondary\">Mailing Lists</a></div>\n"
			<< "      </div>\n"
			<< "      <div class=\"wf-card\" style=\"margin-bottom:0;\">\n"
			<< "        <div class=\"wf-card-header\"><span>" << VividIcon::Get("robot", 16) << " SweAgent Pair</span></div>\n"
			<< "        <div class=\"wf-card-body\" style=\"font-size:0.85rem;\">Interact directly with the integrated autonomous agent for instant support.<br><br><a href=\"/p/" << info.name << "/agents/\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524;\">Launch SweAgent</a></div>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 8. Blog View
	else if(tab == "blog") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Topside Devlog &middot; Recent Posts</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <article style=\"margin-bottom:24px; padding-bottom:16px; border-bottom:1px solid var(--wf-border-color,#393e46);\">\n"
			<< "      <h3 style=\"margin-top:0; margin-bottom:6px;\"><a href=\"#\" style=\"color:#ee6524; text-decoration:none;\">Topside Architectural Restructuring &amp; Agentic Independence</a></h3>\n"
			<< "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8c929a); margin-bottom:12px;\">By <strong>sppp</strong> &middot; Today &middot; Tags: <em>Architecture, Release</em></div>\n"
			<< "      <p style=\"line-height:1.6;\">Topside will become the flagship autonomous software engineering environment. We are establishing modular parity across SourceForge and GitHub platforms to empower open-source teams.</p>\n"
			<< "    </article>\n"
			<< "    <article>\n"
			<< "      <h3 style=\"margin-top:0; margin-bottom:6px;\"><a href=\"#\" style=\"color:#ee6524; text-decoration:none;\">SourceForge Platform Parity Live with ManagerSkylark</a></h3>\n"
			<< "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8c929a); margin-bottom:12px;\">By <strong>sppp</strong> &middot; Today &middot; Tags: <em>Skylark, Aria</em></div>\n"
			<< "      <p style=\"line-height:1.6;\">Complete SourceForge tool suite parity implemented: Code browser, Tickets tracker, Wiki, Discussion forums, Mailman mailing lists, Subprojects, and SweAgent pair programming integration.</p>\n"
			<< "    </article>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 9. Code Browser View
	else if(tab == "code") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\">\n"
			<< "    <div style=\"display:flex; align-items:center; gap:12px;\">\n"
			<< "      <span>" << VividIcon::Get("git-branch", 16) << " Branch: <strong>" << info.active_branch << "</strong></span>\n"
			<< "      <span class=\"wf-badge wf-badge-neutral\">Commit [eefabd]</span>\n"
			<< "    </div>\n"
			<< "    <a href=\"#\" class=\"wf-btn wf-btn-secondary\" style=\"padding:2px 8px; font-size:0.75rem;\">Clone: " << info.git_repo_url << "</a>\n"
			<< "  </div>\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>File</th><th>Last Commit</th><th>Updated</th></tr></thead>\n"
			<< "      <tbody>\n"
			<< "        <tr>\n"
			<< "          <td>" << VividIcon::Get("file-text", 16) << " <a href=\"#\">README.md</a></td>\n"
			<< "          <td>Initial commit on " << info.active_branch << " branch</td>\n"
			<< "          <td>recent</td>\n"
			<< "        </tr>\n"
			<< "        <tr>\n"
			<< "          <td>" << VividIcon::Get("folder", 16) << " <a href=\"#\">uppsrc</a></td>\n"
			<< "          <td>Core framework packages and runtime</td>\n"
			<< "          <td>today</td>\n"
			<< "        </tr>\n"
			<< "        <tr>\n"
			<< "          <td>" << VividIcon::Get("folder", 16) << " <a href=\"#\">reference</a></td>\n"
			<< "          <td>ManagerSkylark and WebShowcase reference applications</td>\n"
			<< "          <td>today</td>\n"
			<< "        </tr>\n"
			<< "      </tbody>\n"
			<< "    </table>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\">" << VividIcon::Get("book", 16) << " README.md</div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <h2 style=\"margin-top:0;\">Topside</h2>\n"
			<< "    <p>Topside is an agentic software development and desktop platform powered by Ultimate++ and autonomous AI agents.</p>\n"
			<< "    <h3>Quick Start</h3>\n"
			<< "    <pre style=\"background:var(--wf-bg-view,#181a1d); padding:10px; border-radius:4px; font-family:monospace; border:1px solid var(--wf-border-color,#393e46);\">git clone " << info.git_repo_url << " topside\ncd topside\n./bin/build -m CLANG reference/ManagerSkylark</pre>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 10. Mailman Mailing Lists
	else if(tab == "mailman") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Topside Mailing Lists</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p>Subscribe to stay informed with developer discussions and release announcements.</p>\n"
			<< "    <div style=\"margin-top:16px;\">\n"
			<< "      <div style=\"padding:12px; background:var(--wf-bg-view,#181a1d); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; margin-bottom:12px; display:flex; justify-content:space-between; align-items:center;\">\n"
			<< "        <div>\n"
			<< "          <div style=\"font-weight:700;\">topside-discuss</div>\n"
			<< "          <div style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8c929a);\">General discussions for developers and users</div>\n"
			<< "        </div>\n"
			<< "        <a href=\"#\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; font-size:0.75rem;\">Subscribe</a>\n"
			<< "      </div>\n"
			<< "      <div style=\"padding:12px; background:var(--wf-bg-view,#181a1d); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; display:flex; justify-content:space-between; align-items:center;\">\n"
			<< "        <div>\n"
			<< "          <div style=\"font-weight:700;\">topside-announce</div>\n"
			<< "          <div style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8c929a);\">Milestone and major release announcements only</div>\n"
			<< "        </div>\n"
			<< "        <a href=\"#\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.75rem;\">Subscribe</a>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 11. Subproject View
	else if(tab == "subproject") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Linked Subprojects &middot; subproject.topside.p</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p>SourceForge subprojects allow hierarchical structuring of large systems into independent tool ecosystems.</p>\n"
			<< "    <div class=\"wf-card\" style=\"margin-top:16px; margin-bottom:0;\">\n"
			<< "      <div class=\"wf-card-header\">\n"
			<< "        <a href=\"/projects/subproject.topside.p/\" style=\"font-weight:700; color:#ee6524; text-decoration:none;\">\n"
			<< "          " << VividIcon::Get("layers", 16) << " SubProject (subproject.topside.p)\n"
			<< "        </a>\n"
			<< "        <span class=\"wf-badge wf-badge-neutral\">Child Project</span>\n"
			<< "      </div>\n"
			<< "      <div class=\"wf-card-body\" style=\"font-size:0.88rem;\">\n"
			<< "        <p>Brought to you by: <strong>sppp</strong>. Independent downloads, ticket trackers, and git branches configured.</p>\n"
			<< "        <a href=\"/projects/subproject.topside.p/\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; font-size:0.8rem;\">Open Subproject Page</a>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 12. Admin View
	else if(tab == "admin") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Project Administration &middot; Topside</span><span class=\"wf-badge wf-badge-emerald\">Admin Access: sppp</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <h4>Enabled Tools</h4>\n"
			<< "    <div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(200px, 1fr)); gap:12px; margin-bottom:20px;\">\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Summary</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Files</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Reviews</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Support</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Blog</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Code (Git)</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Wiki</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Tickets</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Discussion</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Mailing Lists</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> SubProjects</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> SweAgent Pair</label>\n"
			<< "    </div>\n"
			<< "    <h4>Project Metadata</h4>\n"
			<< "    <div style=\"display:flex; flex-direction:column; gap:10px;\">\n"
			<< "      <div>\n"
			<< "        <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Project Name</label>\n"
			<< "        <input type=\"text\" value=\"" << info.name << "\" readonly style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "      </div>\n"
			<< "      <div>\n"
			<< "        <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Short Description</label>\n"
			<< "        <input type=\"text\" value=\"" << info.description << "\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "      </div>\n"
			<< "      <button class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; align-self:flex-start; margin-top:8px;\">Save Changes</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 13. SweAgent Pair View
	else if(tab == "agents") {
		sidebar = ""; // Expand to full width for pair programming terminal
		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1.25rem;\">\n"
			<< "  <div style=\"display:flex; align-items:center; gap:10px;\">\n"
			<< "    <span style=\"font-size:1.3rem; font-weight:700;\">" << VividIcon::Get("robot", 20) << " SweAgent Pair Programmer</span>\n"
			<< "    <span class=\"wf-badge wf-badge-emerald\">Autonomous Pair</span>\n"
			<< "    <span style=\"color:var(--wf-fg-inactive,#8c929a); font-size:0.85rem;\">&middot; Context: <code>" << info.name << "</code></span>\n"
			<< "  </div>\n"
			<< "  <div style=\"display:flex; gap:8px;\">\n"
			<< "    <a href=\"/repo/SeppoPakonen/" << info.name << "?tab=agents\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("layers", 14) << " Autonomous Runs</a>\n"
			<< "    <a href=\"/chat\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem; display:inline-flex; align-items:center; gap:6px;\" target=\"_blank\">"
			<< VividIcon::Get("external-link", 14) << " Fullscreen Terminal</a>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< WebChatView::RenderSnippet();
	}
	else {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>" << CapitalizeWord(tab) << " &middot; " << info.title << "</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div class=\"wf-alert wf-alert-info\">Displaying " << tab << " tool view for SourceForge project " << info.name << ". All data synchronized with Aria scraper.</div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}

	// Interactive JavaScript helpers for SourceForge UI
	content
		<< "<script>\n"
		<< "var SourceForgeUI = (function() {\n"
		<< "  function toggleNewTicketCard() {\n"
		<< "    let el = document.getElementById('sf-new-ticket-card');\n"
		<< "    if(el) el.style.display = (el.style.display === 'none' ? 'block' : 'none');\n"
		<< "  }\n"
		<< "  function createTicket(proj) {\n"
		<< "    let sum = document.getElementById('sf-ticket-summary').value;\n"
		<< "    let pri = document.getElementById('sf-ticket-priority').value;\n"
		<< "    let mil = document.getElementById('sf-ticket-milestone').value;\n"
		<< "    let desc = document.getElementById('sf-ticket-desc').value;\n"
		<< "    if(!sum) { alert('Summary is required'); return; }\n"
		<< "    fetch('/api/sf/tickets/' + proj + '/create', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({summary: sum, priority: pri, milestone: mil, description: desc, author: 'sppp'})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.href = '/p/' + proj + '/tickets/' + res.num;\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function submitComment(proj, num) {\n"
		<< "    let txt = document.getElementById('sf-comment-text').value;\n"
		<< "    if(!txt) return;\n"
		<< "    fetch('/api/sf/tickets/' + proj + '/comment', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({num: num, comment: txt, author: 'sppp'})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.reload();\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function toggleTicket(proj, num) {\n"
		<< "    fetch('/api/sf/tickets/' + proj + '/toggle', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({num: num})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.reload();\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function saveWikiPage(proj, slug) {\n"
		<< "    let title = document.getElementById('sf-wiki-title').value;\n"
		<< "    let content = document.getElementById('sf-wiki-content').value;\n"
		<< "    fetch('/api/sf/wiki/' + proj + '/save', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({slug: slug, title: title, content: content, author: 'sppp'})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.href = '/p/' + proj + '/wiki/' + slug + '/';\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function toggleNewTopicCard() {\n"
		<< "    let el = document.getElementById('sf-new-topic-card');\n"
		<< "    if(el) el.style.display = (el.style.display === 'none' ? 'block' : 'none');\n"
		<< "  }\n"
		<< "  function createTopic(proj) {\n"
		<< "    let forum = document.getElementById('sf-topic-forum').value;\n"
		<< "    let title = document.getElementById('sf-topic-title').value;\n"
		<< "    let post = document.getElementById('sf-topic-first-post').value;\n"
		<< "    if(!title) { alert('Title is required'); return; }\n"
		<< "    fetch('/api/sf/discussion/' + proj + '/topic/create', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({forum: forum, title: title, first_post: post, author: 'sppp'})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.href = '/p/' + proj + '/discussion/topic/' + res.id;\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function submitPost(proj, topicId) {\n"
		<< "    let txt = document.getElementById('sf-post-text').value;\n"
		<< "    if(!txt) return;\n"
		<< "    fetch('/api/sf/discussion/' + proj + '/post/create', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({topic_id: topicId, text: txt, author: 'sppp'})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.reload();\n"
		<< "    });\n"
		<< "  }\n"
		<< "  return {\n"
		<< "    toggleNewTicketCard: toggleNewTicketCard,\n"
		<< "    createTicket: createTicket,\n"
		<< "    submitComment: submitComment,\n"
		<< "    toggleTicket: toggleTicket,\n"
		<< "    saveWikiPage: saveWikiPage,\n"
		<< "    toggleNewTopicCard: toggleNewTopicCard,\n"
		<< "    createTopic: createTopic,\n"
		<< "    submitPost: submitPost\n"
		<< "  };\n"
		<< "})();\n"
		<< "</script>\n";

	String top_nav = SourceForgePlatformService::RenderTopNav(info);
	String sub_nav = SourceForgePlatformService::RenderProjectHeader(info, tab);
	String breadcrumb = "<a href=\"/projects\">Home</a> &gt; <a href=\"/projects\">Open Source Software</a> &gt; <a href=\"#\">" + info.categories + "</a> &gt; <span>" + info.title + "</span>";

	PageBuilder pb;
	pb.Title(info.title + " download | SourceForge.net")
	  .Brand("SourceForge", "/projects/topside/", "package")
	  .Template("sourceforge")
	  .Theme("sourceforge-dark")
	  .Density(DENSITY_COMFORTABLE)
	  .Header(top_nav)
	  .SubNav(sub_nav)
	  .Breadcrumb(breadcrumb)
	  .Sidebar(sidebar)
	  .Content(content);
	ManagerSkylarkApp::AddMarkdownSupport(pb);
	String html = pb.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

// ---------------------------------------------------------------------------
// SKYLARK Routing Handlers
// ---------------------------------------------------------------------------

SKYLARK(SourceForgeProjectDeep, "p/*/**")
{
	String project = http[0];
	String subpath;
	for(int i = 1; i < http.GetParamCount(); i++) {
		if(i > 1) subpath << "/";
		subpath << http[i];
	}
	RenderSourceForgeView(http, project, subpath);
}

SKYLARK(SourceForgeProjectRoot, "p/*")
{
	String project = http[0];
	RenderSourceForgeView(http, project, "");
}

SKYLARK(SourceForgeProjectsDeep, "projects/*/**")
{
	String project = http[0];
	String subpath;
	for(int i = 1; i < http.GetParamCount(); i++) {
		if(i > 1) subpath << "/";
		subpath << http[i];
	}
	RenderSourceForgeView(http, project, subpath);
}

SKYLARK(SourceForgeProjectsRoot, "projects/*")
{
	String project = http[0];
	RenderSourceForgeView(http, project, "");
}

SKYLARK(SourceForgeDirectDeep, "sf/*/**")
{
	String project = http[0];
	String subpath;
	for(int i = 1; i < http.GetParamCount(); i++) {
		if(i > 1) subpath << "/";
		subpath << http[i];
	}
	RenderSourceForgeView(http, project, subpath);
}

SKYLARK(SourceForgeDirectRoot, "sf/*")
{
	String project = http[0];
	RenderSourceForgeView(http, project, "");
}

// ---------------------------------------------------------------------------
// REST API Handlers for SourceForge Tools
// ---------------------------------------------------------------------------

SKYLARK(ApiSfTicketsList, "api/sf/tickets/*")
{
	String project = http[0];
	Vector<SourceForgeTicket> list = SourceForgePlatformService::GetTickets(project);
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiSfTicketDetail, "api/sf/ticket/*/*")
{
	String project = http[0];
	int num = ScanInt(http[1]);
	SourceForgeTicket t = SourceForgePlatformService::GetTicket(project, num);
	http.ContentType("application/json") << AsJSON(StoreAsJsonValue(t));
}

SKYLARK(ApiSfCreateTicket, "api/sf/tickets/*/create:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	String summary = req["summary"];
	String description = req["description"];
	String priority = req["priority"];
	String milestone = req["milestone"];
	String author = req["author"];

	int num = SourceForgePlatformService::CreateTicket(project, summary, description, priority, milestone, author);
	ValueMap res;
	res.Set("ok", true);
	res.Set("num", num);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfCommentTicket, "api/sf/tickets/*/comment:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	int num = (int)req["num"];
	String comment = req["comment"];
	String author = req["author"];

	bool ok = SourceForgePlatformService::AddTicketComment(project, num, author, comment);
	ValueMap res;
	res.Set("ok", ok);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfToggleTicket, "api/sf/tickets/*/toggle:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	int num = (int)req["num"];
	String new_status = req["status"];

	bool ok = SourceForgePlatformService::ToggleTicketStatus(project, num, new_status);
	ValueMap res;
	res.Set("ok", ok);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfWikiPageGet, "api/sf/wiki/*/*")
{
	String project = http[0];
	String slug = http[1];
	SourceForgeWikiPage p = SourceForgePlatformService::GetWikiPage(project, slug);
	http.ContentType("application/json") << AsJSON(StoreAsJsonValue(p));
}

SKYLARK(ApiSfWikiPageSave, "api/sf/wiki/*/save:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	SourceForgeWikiPage p;
	p.slug = req["slug"];
	p.title = req["title"];
	p.content = req["content"];
	p.author = req["author"];

	bool ok = SourceForgePlatformService::SaveWikiPage(project, p);
	ValueMap res;
	res.Set("ok", ok);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfDiscussionTopics, "api/sf/discussion/*")
{
	String project = http[0];
	String forum = http["forum"];
	Vector<SourceForgeDiscussionTopic> list = SourceForgePlatformService::GetDiscussionTopics(project, forum);
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiSfCreateTopic, "api/sf/discussion/*/topic/create:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	String forum = req["forum"];
	String title = req["title"];
	String first_post = req["first_post"];
	String author = req["author"];

	int id = SourceForgePlatformService::CreateDiscussionTopic(project, forum, title, first_post, author);
	ValueMap res;
	res.Set("ok", true);
	res.Set("id", id);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfAddPost, "api/sf/discussion/*/post/create:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	int topic_id = (int)req["topic_id"];
	String text = req["text"];
	String author = req["author"];

	bool ok = SourceForgePlatformService::AddDiscussionPost(project, topic_id, author, text);
	ValueMap res;
	res.Set("ok", ok);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfFilesList, "api/sf/files/*")
{
	String project = http[0];
	Vector<SourceForgeReleaseFile> list = SourceForgePlatformService::GetFiles(project);
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	http.ContentType("application/json") << AsJSON(va);
}

END_UPP_NAMESPACE
