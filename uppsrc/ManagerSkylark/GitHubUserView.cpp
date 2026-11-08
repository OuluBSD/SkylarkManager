#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// SKYLARK Views: User Profile & Repository Parity
// ---------------------------------------------------------------------------

void RenderUserProfile(Http& http, const String& username)
{
	String uname = username.IsEmpty() ? "SeppoPakonen" : username;
	Vector<LocalRepoInfo> repos = GitHubPlatformService::GetLiveGitHubRepos(uname);

	String active_tab = http["tab"];
	if(active_tab.IsEmpty()) active_tab = "overview";

	String content;

	// User header / profile subnav
	String sub_nav;
	sub_nav << "<div class=\"wf-gh-repohead\">\n"
	        << "  <div class=\"wf-gh-main-container\" style=\"padding-bottom:0;\">\n"
	        << "    <nav class=\"wf-gh-nav-tabs\">\n"
	        << "      <a href=\"/repo/" << uname << "?tab=overview\" class=\"wf-gh-tab" << (active_tab == "overview" ? " active" : "") << "\">" << VividIcon::Get("book-open", 16) << " Overview</a>\n"
	        << "      <a href=\"/repo/" << uname << "?tab=repositories\" class=\"wf-gh-tab" << (active_tab == "repositories" ? " active" : "") << "\">" << VividIcon::Get("git-repo", 16) << " Repositories <span class=\"wf-gh-counter\">" << repos.GetCount() << "</span></a>\n"
	        << "      <a href=\"/repo/" << uname << "?tab=projects\" class=\"wf-gh-tab" << (active_tab == "projects" ? " active" : "") << "\">" << VividIcon::Get("layout-dashboard", 16) << " Projects</a>\n"
	        << "      <a href=\"/repo/" << uname << "?tab=packages\" class=\"wf-gh-tab" << (active_tab == "packages" ? " active" : "") << "\">" << VividIcon::Get("package", 16) << " Packages</a>\n"
	        << "      <a href=\"/repo/" << uname << "?tab=agents\" class=\"wf-gh-tab" << (active_tab == "agents" ? " active" : "") << "\" style=\"color:var(--wf-color-positive, #00aa7f);\">" << VividIcon::Get("robot", 16) << " SweAgent Workspace</a>\n"
	        << "    </nav>\n"
	        << "  </div>\n"
	        << "</div>\n";

	if(active_tab == "overview") {
		content
			<< "<div style=\"margin-bottom:1.5rem;\">\n"
			<< "  <h3 style=\"font-size:1rem; font-weight:600; margin-bottom:12px;\">Pinned Repositories</h3>\n"
			<< "  <div class=\"wf-grid wf-grid-2\" style=\"gap:16px;\">\n";

		const char* pinned_names[] = { "ai-upp-priv", "Manager", "nes-recompiled", "AR-AirHockey", "PulseVideoGen", "Shaders" };
		for(int i = 0; i < __countof(pinned_names); i++) {
			LocalRepoInfo r = GitHubPlatformService::GetRepo(uname, pinned_names[i]);
			content
				<< "    <div class=\"wf-card\" style=\"margin-bottom:0;\">\n"
				<< "      <div class=\"wf-card-header\">\n"
				<< "        <a href=\"/repo/" << uname << "/" << r.name << "\" style=\"font-weight:600; color:var(--wf-accent-focus,#58a6ff); text-decoration:none;\">"
				<< VividIcon::Get("git-repo", 16) << " " << r.name << "</a>\n"
				<< "        <span class=\"wf-gh-badge\">" << GitHubPlatformService::VisibilityLabel(r.is_private) << "</span>\n"
				<< "      </div>\n"
				<< "      <div class=\"wf-card-body\" style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590);\">\n"
				<< "        <p>" << (!r.description.IsEmpty() ? r.description : "Ultimate++ Agentic repository in ~/Dev/" + r.name) << "</p>\n"
				<< "      </div>\n"
				<< "      <div class=\"wf-card-footer\" style=\"font-size:0.78rem;\">\n"
				<< "        <span><span style=\"display:inline-block; width:10px; height:10px; border-radius:50%; background:#f34b7d; margin-right:4px;\"></span> " << r.language << "</span>\n"
				<< "        <div style=\"display:flex; gap:6px;\">\n"
				<< "          <a href=\"/repo/" << uname << "/" << r.name << "\" class=\"wf-btn wf-btn-secondary\" style=\"padding:2px 8px; font-size:0.75rem;\">GitHub</a>\n"
				<< "          <a href=\"/projects/" << r.name << "/\" class=\"wf-btn\" style=\"padding:2px 8px; font-size:0.75rem; background:#ee6524; color:#fff; border-color:#ee6524;\">SourceForge</a>\n"
				<< "        </div>\n"
				<< "      </div>\n"
				<< "    </div>\n";
		}
		content << "  </div>\n</div>\n";

		// Contribution activity is not read from GitHub; say so instead of drawing a pattern.
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Contribution activity</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div class=\"wf-alert wf-alert-info\">Contribution activity is not loaded by this view.</div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(active_tab == "repositories") {
		content
			<< "<div style=\"margin-bottom:1rem; display:flex; justify-content:space-between; align-items:center;\">\n"
			<< "  <input type=\"search\" placeholder=\"Find a repository...\" style=\"background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 12px; color:var(--wf-fg-normal,#e6edf3); width:320px; font-size:0.88rem;\">\n"
			<< "  <span style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590);\">" << repos.GetCount() << " results</span>\n"
			<< "</div>\n"
			<< "<div class=\"wf-gh-file-box\">\n";
		for(int i = 0; i < repos.GetCount(); i++) {
			const LocalRepoInfo& r = repos[i];
			content
				<< "  <div class=\"wf-gh-file-row\" style=\"justify-content:space-between; padding:14px 16px;\">\n"
				<< "    <div>\n"
				<< "      <div style=\"display:flex; align-items:center; gap:8px;\">\n"
				<< "        <a href=\"/repo/" << uname << "/" << r.name << "\" style=\"font-size:1.1rem; font-weight:600; color:var(--wf-accent-focus,#58a6ff); text-decoration:none;\">" << r.name << "</a>\n"
				<< "        <span class=\"wf-gh-badge\">" << GitHubPlatformService::VisibilityLabel(r.is_private) << "</span>\n";
			if(r.stars > 0)
				content << "        <span style=\"font-size:0.8rem; color:#e3b341;\">★ " << r.stars << "</span>\n";
			if(r.forks > 0)
				content << "        <span style=\"font-size:0.8rem; color:#8b949e;\">⑂ " << r.forks << "</span>\n";
			content
				<< "      </div>\n"
				<< "      <p style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px;\">"
				<< (!r.description.IsEmpty() ? r.description : "~/Dev/" + r.name) << "</p>\n"
				<< "      <div style=\"display:flex; align-items:center; gap:16px; margin-top:8px; font-size:0.78rem; color:var(--wf-fg-muted,#848d97);\">\n"
				<< "        <span><span style=\"display:inline-block; width:10px; height:10px; border-radius:50%; background:#f34b7d; margin-right:4px;\"></span> " << r.language << "</span>\n"
				<< "        <span>Updated: " << r.last_commit_time << "</span>\n"
				<< "      </div>\n"
				<< "    </div>\n"
				<< "    <div style=\"display:flex; gap:8px; align-items:center;\">\n"
				<< "      <a href=\"/repo/" << uname << "/" << r.name << "\" class=\"wf-btn wf-btn-secondary\" style=\"padding:4px 10px; font-size:0.8rem;\">GitHub</a>\n"
				<< "      <a href=\"/projects/" << r.name << "/\" class=\"wf-btn\" style=\"padding:4px 10px; font-size:0.8rem; background:#ee6524; color:#ffffff; border-color:#ee6524; text-decoration:none;\">SourceForge</a>\n"
				<< "    </div>\n"
				<< "  </div>\n";
		}
		content << "</div>\n";
	}
	else if(active_tab == "projects") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>GitHub Projects &amp; Kanban Boards</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div class=\"wf-alert wf-alert-info\">Projects are not loaded by this view. No project boards are shown.</div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(active_tab == "packages") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Ultimate++ Package Ecosystem</span><span class=\"wf-badge wf-badge-cyan\">uppsrc</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p style=\"color:var(--wf-fg-inactive,#7d8590); margin-bottom:1rem;\">Modular U++ assemblies, standard library packages, GUI controls, and network servers.</p>\n"
			<< "    <div class=\"wf-grid wf-grid-3\">\n"
			<< "      <div style=\"padding:10px; background:var(--wf-bg-view,#0d1117); border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\"><strong>Core</strong><br><small style=\"color:#8b919d;\">Foundational runtime, containers, NTL</small></div>\n"
			<< "      <div style=\"padding:10px; background:var(--wf-bg-view,#0d1117); border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\"><strong>Skylark</strong><br><small style=\"color:#8b919d;\">High performance HTTP/REST server</small></div>\n"
			<< "      <div style=\"padding:10px; background:var(--wf-bg-view,#0d1117); border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\"><strong>WebFramework</strong><br><small style=\"color:#8b919d;\">Multi-theme mega-menu &amp; density engine</small></div>\n"
			<< "      <div style=\"padding:10px; background:var(--wf-bg-view,#0d1117); border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\"><strong>SweAgent</strong><br><small style=\"color:#8b919d;\">OpenRouter autonomous coding agent</small></div>\n"
			<< "      <div style=\"padding:10px; background:var(--wf-bg-view,#0d1117); border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\"><strong>WebChat</strong><br><small style=\"color:#8b919d;\">Interactive streaming pair terminal</small></div>\n"
			<< "      <div style=\"padding:10px; background:var(--wf-bg-view,#0d1117); border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\"><strong>Aria</strong><br><small style=\"color:#8b919d;\">Autonomous browser scraping &amp; test harness</small></div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(active_tab == "agents") {
		content
			<< "<h2>SweAgent Pair Programmer Workspace</h2>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>SweAgent Integrated Hub</span><span class=\"wf-badge wf-badge-emerald\">Live</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p style=\"margin-bottom:1rem;\">Launch pair-programming agent sessions directly for any repository in <code>~/Dev/</code>.</p>\n"
			<< "    <a href=\"/chat\" class=\"wf-btn wf-btn-primary\">Open Live WebChat</a>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}

	// Sidebar: Profile Card
	String sidebar;
	sidebar
		<< "<div class=\"wf-gh-sidebar\">\n"
		<< "  <div style=\"display:flex; flex-direction:column; align-items:center; text-align:center;\">\n"
		<< "    <img src=\"https://avatars.githubusercontent.com/u/9528528?v=4\" alt=\"Seppo Pakonen\" style=\"width:140px; height:140px; border-radius:50%; border:2px solid var(--wf-border-color,#30363d); margin-bottom:16px; object-fit:cover;\">\n"
		<< "    <h2 style=\"font-size:1.3rem; margin:0;\">Seppo Pakonen</h2>\n"
		<< "    <span style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.95rem;\">@" << uname << "</span>\n"
		<< "  </div>\n"
		<< "  <p style=\"font-size:0.9rem; line-height:1.4;\">Software Engineer &amp; AI Architect. Creator of Ultimate++, Agentic frameworks and systems.</p>\n"
		<< "  <div style=\"display:flex; gap:12px; font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590); margin-bottom:14px; justify-content:center;\">\n"
		<< "    <span>" << VividIcon::Get("users", 14) << " <strong>12</strong> followers</span>\n"
		<< "    <span>&middot;</span>\n"
		<< "    <span><strong>12</strong> following</span>\n"
		<< "  </div>\n"
		<< "  <div class=\"wf-gh-sidebar-section\">\n"
		<< "    <span class=\"wf-gh-sidebar-title\">Details</span>\n"
		<< "    <div>" << VividIcon::Get("map-pin", 14) << " Oulu, Finland</div>\n"
		<< "    <div>" << VividIcon::Get("folder", 14) << " ~/Dev/ (" << repos.GetCount() << " repos)</div>\n"
		<< "    <div>" << VividIcon::Get("git-repo", 14) << " " << repos.GetCount() << " GitHub Repos</div>\n"
		<< "  </div>\n"
		<< "</div>\n";

	NavigationModel nav = ManagerSkylarkApp::BuildNavigation("gh_profile");
	UserSession sess = SystemAuthService::GetCurrentSession(http);

	PageBuilder pb;
	pb.Title(uname + " (Seppo Pakonen) - GitHub Platform Parity")
	  .Brand("GitHub", "/", "octicon-mark-github")
	  .Navigation(nav)
	  .NavActions(SystemAuthService::RenderUserMenu(sess))
	  .Template("github")
	  .Theme("github-dark")
	  .Density(DENSITY_COMFORTABLE)
	  .SubNav(sub_nav)
	  .Sidebar(sidebar)
	  .Content(content);
	ManagerSkylarkApp::AddMarkdownSupport(pb);
	String html = pb.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

END_UPP_NAMESPACE
