#include "ManagerSkylark.h"
#include "SourceForgePlatform.h"

NAMESPACE_UPP

String CapitalizeWord(const String& s)
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

END_UPP_NAMESPACE
