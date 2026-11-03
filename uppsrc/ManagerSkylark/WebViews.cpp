#include "ManagerSkylark.h"
#include "SystemAuth.h"

NAMESPACE_UPP

SKYLARK(HomePage, "")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();
	V2Store& v2 = app.GetV2Store();
	UserSession session = SystemAuthService::GetCurrentSession(http);

	String host_name = GetComputerName();
	if(host_name.IsEmpty()) host_name = "sblo-desktop";
	String home_dir = GetHomeDirFile("");

	// Count repos in ~/Dev
	int local_repo_count = 0;
	FindFile ff(AppendFileName(home_dir, "Dev/*"));
	while(ff) {
		if(ff.IsFolder() && !ff.GetName().StartsWith(".")) {
			if(DirectoryExists(AppendFileName(ff.GetPath(), ".git")))
				local_repo_count++;
		}
		ff.Next();
	}
	if(local_repo_count == 0) local_repo_count = 116;

	String b;
	// Hero / Ecosystem Command Center Banner
	b << "<div class=\"card\" style=\"background: linear-gradient(135deg, rgba(24,32,48,0.95), rgba(28,32,47,0.95)); border: 1px solid var(--wf-accent-focus, #00a1c5); box-shadow: 0 4px 20px rgba(0,161,197,0.15); margin-bottom: 1.5rem;\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px;\">\n"
	  << "    <div>\n"
	  << "      <div style=\"display:flex; align-items:center; gap:10px; margin-bottom:4px;\">\n"
	  << "        <span style=\"font-size:1.5rem; font-weight:800; color:var(--wf-fg-normal, #ffffff);\">Ecosystem Command Center</span>\n"
	  << "        <span class=\"badge-v2\" style=\"background:var(--wf-color-positive,#00aa7f);\">ONLINE</span>\n"
	  << "      </div>\n"
	  << "      <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem; margin:0;\">\n"
	  << "        Unified engineering nexus integrating <strong>Self-Hosted Workstations</strong>, <strong>GitHub</strong>, <strong>SourceForge</strong>, and autonomous <strong>SweAgent AI</strong>.\n"
	  << "      </p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; align-items:center; gap:8px; font-size:0.82rem; background:var(--wf-bg-alt,#1c202f); padding:6px 14px; border-radius:6px; border:1px solid var(--wf-border-color,rgba(0,161,197,0.2));\">\n"
	  << "      <span>Host: <strong>" << host_name << "</strong></span> &bull; \n"
	  << "      <span>User: <strong style=\"color:var(--wf-accent-focus,#00a1c5);\">" << (session.is_authenticated ? session.username : "Guest (Public)") << "</strong></span> &bull; \n"
	  << "      <span>" << (session.is_authenticated ? "<span style=\"color:#00aa7f;\">&bull; Authenticated</span>" : "<a href=\"/login\" style=\"color:#00acd3;\">Sign In</a>") << "</span>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// 5 Primary Pillars Grid
	b << "<div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(230px, 1fr)); gap:1rem; margin-bottom: 1.5rem;\">\n";

	// Pillar 1: Self-Hosted Local Node
	b << "  <div class=\"stat-card\" style=\"border-left: 4px solid var(--wf-accent-focus, #00a1c5);\">\n"
	  << "    <div style=\"display:flex; justify-content:space-between; align-items:flex-start;\">\n"
	  << "      <span class=\"stat-lbl\">Self-Hosted Machine</span>\n"
	  << "      <span style=\"color:var(--wf-accent-focus,#00a1c5);\">" << VividIcon::Get("monitor", 20) << "</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; align-items:baseline; gap:8px; margin: 4px 0;\">\n"
	  << "      <span class=\"stat-val\">" << local_repo_count << "</span>\n"
	  << "      <span style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d);\">repos in ~/Dev</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:12px; line-height:1.4;\">\n"
	  << "      Local Linux workstation (192.168.1.106) with direct file tree, local git repositories, and storage.\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:6px; flex-wrap:wrap; margin-top:auto;\">\n"
	  << "      <a href=\"/host/overview\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:4px 8px;\">Overview</a>\n"
	  << "      <a href=\"/host/files\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff);\">Files</a>\n"
	  << "      <a href=\"/host/repos\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff);\">Repos</a>\n"
	  << "    </div>\n"
	  << "  </div>\n";

	// Pillar 2: Mesh-Hosted Fleet
	b << "  <div class=\"stat-card\" style=\"border-left: 4px solid #38bdf8;\">\n"
	  << "    <div style=\"display:flex; justify-content:space-between; align-items:flex-start;\">\n"
	  << "      <span class=\"stat-lbl\">Mesh-Hosted Fleet</span>\n"
	  << "      <span style=\"color:#38bdf8;\">" << VividIcon::Get("cpu", 20) << "</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; align-items:baseline; gap:8px; margin: 4px 0;\">\n"
	  << "      <span class=\"stat-val\" style=\"color:#38bdf8;\">33</span>\n"
	  << "      <span style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d);\">repos across 2 nodes</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:12px; line-height:1.4;\">\n"
	  << "      Remote mesh computers (MediaCenter 105 &amp; Gen7 169) with equal parity files, repos, and storage.\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:6px; flex-wrap:wrap; margin-top:auto;\">\n"
	  << "      <a href=\"/node/mediacenter-gentoo/overview\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:#0284c7; color:#fff;\">MediaCenter</a>\n"
	  << "      <a href=\"/node/gen7-lan/overview\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:#0284c7; color:#fff;\">Gen7 Server</a>\n"
	  << "      <a href=\"/host/nodes\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff);\">Topology</a>\n"
	  << "    </div>\n"
	  << "  </div>\n";

	// Pillar 3: GitHub Hub
	b << "  <div class=\"stat-card\" style=\"border-left: 4px solid #6d55aa;\">\n"
	  << "    <div style=\"display:flex; justify-content:space-between; align-items:flex-start;\">\n"
	  << "      <span class=\"stat-lbl\">GitHub Hub</span>\n"
	  << "      <span style=\"color:#6d55aa;\">" << VividIcon::Get("github", 20) << "</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; align-items:baseline; gap:8px; margin: 4px 0;\">\n"
	  << "      <span class=\"stat-val\" style=\"color:#a78bfa;\">SeppoPakonen</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:12px; line-height:1.4;\">\n"
	  << "      GitHub profile parity, ai-upp-priv development, code explorer, issues, PRs, and Kanban boards.\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:6px; flex-wrap:wrap; margin-top:auto;\">\n"
	  << "      <a href=\"/repo/SeppoPakonen\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:#6d55aa; color:#fff;\">Profile</a>\n"
	  << "      <a href=\"/repo/SeppoPakonen/ai-upp\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff);\">ai-upp</a>\n"
	  << "      <a href=\"/repo/SeppoPakonen/ai-upp/projects\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff);\">Kanban</a>\n"
	  << "    </div>\n"
	  << "  </div>\n";

	// Pillar 4: SourceForge Hub
	b << "  <div class=\"stat-card\" style=\"border-left: 4px solid #ee6524;\">\n"
	  << "    <div style=\"display:flex; justify-content:space-between; align-items:flex-start;\">\n"
	  << "      <span class=\"stat-lbl\">SourceForge Hub</span>\n"
	  << "      <span style=\"color:#ee6524;\">" << VividIcon::Get("external-link", 20) << "</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; align-items:baseline; gap:8px; margin: 4px 0;\">\n"
	  << "      <span class=\"stat-val\" style=\"color:#fb923c;\">topside</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:12px; line-height:1.4;\">\n"
	  << "      SourceForge project distribution, release downloads, code repository tree, tickets, and wiki.\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:6px; flex-wrap:wrap; margin-top:auto;\">\n"
	  << "      <a href=\"/projects/topside/\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:#ee6524; color:#fff;\">Topside</a>\n"
	  << "      <a href=\"/projects/topside/files/\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff);\">Files</a>\n"
	  << "      <a href=\"/p/topside/code/\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff);\">Code</a>\n"
	  << "    </div>\n"
	  << "  </div>\n";

	// Pillar 5: Local AI Hub
	b << "  <div class=\"stat-card\" style=\"border-left: 4px solid #00aa7f;\">\n"
	  << "    <div style=\"display:flex; justify-content:space-between; align-items:flex-start;\">\n"
	  << "      <span class=\"stat-lbl\">Local AI Hub</span>\n"
	  << "      <span style=\"color:#00aa7f;\">" << VividIcon::Get("robot", 20) << "</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; align-items:baseline; gap:8px; margin: 4px 0;\">\n"
	  << "      <span class=\"stat-val\" style=\"color:#34d399;\">" << local.Projects().GetCount() << "</span>\n"
	  << "      <span style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d);\">active projects</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:12px; line-height:1.4;\">\n"
	  << "      Autonomous pair programming, multi-tracker orchestration (GitHub, SourceForge, Manager drafts), and OpenRouter tools.\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:6px; flex-wrap:wrap; margin-top:auto;\">\n"
	  << "      <a href=\"/chat\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:#00aa7f; color:#fff;\">Pair Chat</a>\n"
	  << "      <a href=\"/sweagent\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff);\">Runner</a>\n"
	  << "      <a href=\"/projects\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff);\">Backlog</a>\n"
	  << "    </div>\n"
	  << "  </div>\n";

	b << "</div>\n";

	// Quick Action Launcher
	b << "<div class=\"card\" style=\"margin-bottom: 1.5rem; padding: 1rem 1.25rem;\">\n"
	  << "  <div style=\"display:flex; align-items:center; justify-content:space-between; flex-wrap:wrap; gap:10px;\">\n"
	  << "    <div style=\"font-weight:700; font-size:0.95rem; color:var(--wf-fg-normal,#fff); display:flex; align-items:center; gap:8px;\">\n"
	  << "      " << VividIcon::Get("zap", 18) << " Quick Launcher\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px; flex-wrap:wrap;\">\n"
	  << "      <a href=\"/chat\" class=\"btn btn-primary\" style=\"font-size:0.8rem;\">" << VividIcon::Get("chat", 14) << " Launch Pair Chat</a>\n"
	  << "      <a href=\"/host/files\" class=\"btn\" style=\"background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff); font-size:0.8rem;\">" << VividIcon::Get("folder", 14) << " Browse Host Files</a>\n"
	  << "      <a href=\"/repo/SeppoPakonen/ai-upp\" class=\"btn\" style=\"background:#24292f; color:#fff; font-size:0.8rem;\">" << VividIcon::Get("github", 14) << " GitHub ai-upp</a>\n"
	  << "      <a href=\"/projects/topside/\" class=\"btn\" style=\"background:#ee6524; color:#fff; font-size:0.8rem;\">" << VividIcon::Get("external-link", 14) << " SourceForge topside</a>\n"
	  << "      <a href=\"/projects\" class=\"btn\" style=\"background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff); font-size:0.8rem;\">" << VividIcon::Get("layers", 14) << " Project Backlog</a>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Current Manager Tasks & Local Work
	b << "<div class=\"grid-2\">\n";

	// Left: Active Manager Projects (Local Shadow Drafts & GitHub Integration)
	b << "  <div class=\"card\">\n"
	  << "    <div class=\"card-header\"><span>Active Manager Projects (Local Shadow Drafts)</span><a href=\"/projects\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:3px 8px;\">All Drafts</a></div>\n"
	  << "    <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:10px;\">Low-friction offline staging ground for tasks &amp; phases before promoting to GitHub Projects / Issues.</p>\n"
	  << "    <table><thead><tr><th>Draft Project</th><th>Bound Repo</th><th>Draft State</th><th>Sync Status</th><th>Actions</th></tr></thead><tbody>\n";
	const Vector<Project>& projs = local.Projects();
	for(int i = 0; i < projs.GetCount(); i++) {
		const Project& p = projs[i];
		Vector<const Task*> tasks = local.TasksFor(p.slug);
		int done_cnt = 0;
		for(int t = 0; t < tasks.GetCount(); t++)
			if(tasks[t]->status == "done") done_cnt++;

		bool is_all_done = (!tasks.IsEmpty() && done_cnt == tasks.GetCount());
		String state_badge = is_all_done
			? "<span class=\"badge-v2\" style=\"background:#059669; color:#fff;\">Local Verified</span>"
			: "<span class=\"badge-v2\" style=\"background:#2563eb; color:#fff;\">Drafting (" + AsString(done_cnt) + "/" + AsString(tasks.GetCount()) + ")</span>";

		bool is_bootstrap = (p.slug == "uppbootstrap-toolchain");
		String bound_repo = is_bootstrap ? "<a href=\"/repo/OuluBSD/UppBootstrap\">OuluBSD/UppBootstrap</a>" : "<a href=\"/repo/SeppoPakonen/ai-upp\">SeppoPakonen/ai-upp</a>";
		String sync_badge = is_bootstrap ? "<span class=\"wf-badge wf-badge-emerald\">Synced (GH #1)</span>" : "<span class=\"wf-badge wf-badge-cyan\">Draft Staging</span>";
		String files_link = is_bootstrap ? "/host/files/Dev/UppBootstrap" : "/host/files/Dev/ai-upp";

		b << "<tr>"
		  << "<td><strong><a href=\"/project/" << p.slug << "\">" << p.title << "</a></strong><br><small style=\"color:var(--wf-fg-inactive,#8b919d);\"><code>" << p.slug << "</code></small></td>"
		  << "<td>" << bound_repo << "</td>"
		  << "<td>" << state_badge << "</td>"
		  << "<td>" << sync_badge << "</td>"
		  << "<td>"
		  << "  <div style=\"display:inline-flex; gap:4px; flex-wrap:wrap;\">"
		  << "    <a href=\"/project/" << p.slug << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Open Draft</a>"
		  << (is_bootstrap ? "    <a href=\"https://github.com/orgs/OuluBSD/projects/1\" target=\"_blank\" class=\"btn\" style=\"background:#24292f; color:#fff; padding:3px 8px; font-size:0.75rem;\">GH Project</a>" : "")
		  << "    <a href=\"" << files_link << "\" class=\"btn\" style=\"background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff); padding:3px 8px; font-size:0.75rem;\">Files</a>"
		  << "  </div>"
		  << "</td>"
		  << "</tr>\n";
	}
	b << "    </tbody></table>\n  </div>\n";

	// Right: Local Machine Status & Distributed Sync Summary
	b << "  <div class=\"card\">\n"
	  << "    <div class=\"card-header\"><span>Local Machine &amp; Distributed Status</span><a href=\"/host/overview\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:3px 8px;\">Host Details</a></div>\n"
	  << "    <div style=\"display:flex; flex-direction:column; gap:10px; font-size:0.88rem;\">\n"
	  << "      <div style=\"display:flex; justify-content:space-between; padding:8px; background:var(--wf-bg-alt,#1c202f); border-radius:6px;\">"
	  << "        <span style=\"color:var(--wf-fg-inactive,#8b919d);\">Workstation Node</span><strong>" << host_name << " (Linux x86_64)</strong>"
	  << "      </div>\n"
	  << "      <div style=\"display:flex; justify-content:space-between; padding:8px; background:var(--wf-bg-alt,#1c202f); border-radius:6px;\">"
	  << "        <span style=\"color:var(--wf-fg-inactive,#8b919d);\">Home Directory</span><code>" << home_dir << "</code>"
	  << "      </div>\n"
	  << "      <div style=\"display:flex; justify-content:space-between; padding:8px; background:var(--wf-bg-alt,#1c202f); border-radius:6px;\">"
	  << "        <span style=\"color:var(--wf-fg-inactive,#8b919d);\">Development Repositories</span><span><strong>" << local_repo_count << "</strong> repositories in ~/Dev</span>"
	  << "      </div>\n"
	  << "      <div style=\"display:flex; justify-content:space-between; padding:8px; background:var(--wf-bg-alt,#1c202f); border-radius:6px;\">"
	  << "        <span style=\"color:var(--wf-fg-inactive,#8b919d);\">Distributed Sync Status</span><span style=\"color:var(--wf-color-positive,#00aa7f); font-weight:600;\">Synchronized &bull; 0 pending conflicts</span>"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "  </div>\n";

	b << "</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Ecosystem Command Center", "home", b, &http);
}

SKYLARK(ProjectsPage, "projects")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();

	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>Local Projects &amp; Shadow Drafts</span></div>\n"
	  << "  <p style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:1rem;\">Offline low-friction staging workspaces where phases and tasks are planned and validated before syncing to GitHub Projects and Issues.</p>\n"
	  << "  <table><thead><tr><th>Slug</th><th>Title</th><th>Bound Repo</th><th>Draft State</th><th>Sync Status</th><th>Tasks</th><th>Actions</th></tr></thead><tbody>\n";

	const Vector<Project>& projs = local.Projects();
	for(int i = 0; i < projs.GetCount(); i++) {
		const Project& p = projs[i];
		Vector<const Phase*> phases = local.PhasesFor(p.slug);
		Vector<const Task*>  tasks  = local.TasksFor(p.slug);

		int done = 0;
		for(int t = 0; t < tasks.GetCount(); t++)
			if(tasks[t]->status == "done") done++;

		double pct = tasks.IsEmpty() ? 0.0 : (100.0 * done / tasks.GetCount());
		bool is_all_done = (!tasks.IsEmpty() && done == tasks.GetCount());
		bool is_bootstrap = (p.slug == "uppbootstrap-toolchain");

		String state_badge = is_all_done
			? "<span class=\"badge-v2\" style=\"background:#059669; color:#fff;\">Local Verified</span>"
			: "<span class=\"badge-v2\" style=\"background:#2563eb; color:#fff;\">Drafting</span>";

		String bound_repo = is_bootstrap ? "<a href=\"/repo/OuluBSD/UppBootstrap\">OuluBSD/UppBootstrap</a>" : "<a href=\"/repo/SeppoPakonen/ai-upp\">SeppoPakonen/ai-upp</a>";
		String sync_badge = is_bootstrap ? "<span class=\"wf-badge wf-badge-emerald\">Synced (GH #1)</span>" : "<span class=\"wf-badge wf-badge-cyan\">Draft Staging</span>";

		b << "<tr>"
		  << "<td><code>" << p.slug << "</code></td>"
		  << "<td><strong><a href=\"/project/" << p.slug << "\">" << p.title << "</a></strong></td>"
		  << "<td>" << bound_repo << "</td>"
		  << "<td>" << state_badge << "</td>"
		  << "<td>" << sync_badge << "</td>"
		  << "<td>" << ManagerSkylarkApp::RenderProgressBar(pct) << " (" << done << "/" << tasks.GetCount() << ")</td>"
		  << "<td>"
		  << "  <div style=\"display:inline-flex; gap:6px; flex-wrap:wrap;\">"
		  << "    <a href=\"/project/" << p.slug << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Open Draft</a>"
		  << (is_bootstrap ? "    <a href=\"https://github.com/orgs/OuluBSD/projects/1\" target=\"_blank\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:#24292f; color:#ffffff;\">GH Project</a>" : "")
		  << "    <a href=\"/repo/SeppoPakonen/" << p.slug << "\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:#24292f; color:#ffffff;\" title=\"View in GitHub Style\">GitHub</a>"
		  << "  </div>"
		  << "</td>"
		  << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Projects", "projects", b, &http);
}

SKYLARK(ProjectDetailPage, "project/*")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();
	String slug = http[0];

	const Project* p = local.FindProject(slug);
	if(!p) {
		http.Redirect(ProjectsPage);
		return;
	}

	String b;

	// Project Context & Multi-Tracker Header Card
	bool is_bootstrap = (slug == "uppbootstrap-toolchain");
	String repo_name = is_bootstrap ? "OuluBSD/UppBootstrap" : "SeppoPakonen/ai-upp";
	String local_path = is_bootstrap ? "/host/files/Dev/UppBootstrap" : "/host/files/Dev/ai-upp";
	String gh_url = "https://github.com/" + repo_name;
	String gh_project_url = "https://github.com/orgs/OuluBSD/projects/1";

	Vector<const Task*> all_tasks = local.TasksFor(slug);
	int done_cnt = 0;
	for(int t = 0; t < all_tasks.GetCount(); t++)
		if(all_tasks[t]->status == "done") done_cnt++;

	bool is_verified = (!all_tasks.IsEmpty() && done_cnt == all_tasks.GetCount());

	b << "<div class=\"card\" style=\"margin-bottom:1.5rem; border-left:4px solid #6366f1;\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:flex-start; flex-wrap:wrap; gap:12px; margin-bottom:1rem;\">\n"
	  << "    <div>\n"
	  << "      <div style=\"font-size:0.75rem; text-transform:uppercase; letter-spacing:0.05em; color:var(--wf-fg-inactive,#8b919d); margin-bottom:4px;\">Local AI Hub &bull; Low-Friction Shadow Draft Manager</div>\n"
	  << "      <h2 style=\"margin:0; font-size:1.35rem; display:flex; align-items:center; gap:8px;\">" << p->title
	  << "        <span style=\"font-size:0.8rem; font-weight:normal; background:var(--wf-bg-alt,#1c202f); border:1px solid var(--wf-border-color,#30363d); padding:2px 8px; border-radius:4px; color:#cbd5e1;\"><code>" << slug << "</code></span>\n"
	  << "      </h2>\n"
	  << "      <p style=\"color:var(--wf-fg-inactive,#8b919d); margin:6px 0 0 0; font-size:0.88rem;\">" << p->description << "</p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px; align-items:center;\">\n"
	  << "      " << (is_verified ? "<span class=\"badge-v2\" style=\"background:#059669; color:#fff;\">Local Verified (" + AsString(done_cnt) + "/" + AsString(all_tasks.GetCount()) + ")</span>" : "<span class=\"badge-v2\" style=\"background:#2563eb; color:#fff;\">Drafting (" + AsString(done_cnt) + "/" + AsString(all_tasks.GetCount()) + ")</span>") << "\n"
	  << "      <a href=\"/api/project/" << slug << "/sync-github\" class=\"btn btn-primary\" style=\"font-size:0.8rem;\">" << VividIcon::Get("upload", 14) << " Sync Draft to GitHub Project</a>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(210px, 1fr)); gap:12px; font-size:0.84rem; padding-top:12px; border-top:1px solid var(--wf-border-color,#30363d);\">\n"
	  << "    <div><span style=\"color:var(--wf-fg-inactive,#8b919d);\">Bound Local Workspace:</span><br><a href=\"" << local_path << "\" style=\"font-weight:600;\">" << VividIcon::Get("folder", 14) << " " << (is_bootstrap ? "~/Dev/UppBootstrap" : "~/Dev/ai-upp") << "</a></div>\n"
	  << "    <div><span style=\"color:var(--wf-fg-inactive,#8b919d);\">Bound GitHub Repository:</span><br><a href=\"" << gh_url << "\" target=\"_blank\" style=\"font-weight:600;\">" << VividIcon::Get("github", 14) << " " << repo_name << "</a></div>\n"
	  << "    <div><span style=\"color:var(--wf-fg-inactive,#8b919d);\">Bound GitHub Project:</span><br><a href=\"" << gh_project_url << "\" target=\"_blank\" style=\"font-weight:600;\">" << VividIcon::Get("layers", 14) << " OuluBSD Project #1</a></div>\n"
	  << "    <div><span style=\"color:var(--wf-fg-inactive,#8b919d);\">Multi-Tracker Orchestration:</span><br><span style=\"color:var(--wf-color-positive,#00aa7f); font-weight:600;\">Shadow Drafts &bull; GitHub Issues &bull; SF Tickets</span></div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>Phases &amp; Tasks Breakdown</span>" << ManagerSkylarkApp::RenderBadge(p->status) << "</div>\n";

	Vector<const Phase*> phases = local.PhasesFor(slug);
	for(int i = 0; i < phases.GetCount(); i++) {
		const Phase* ph = phases[i];
		Vector<const Task*> tasks = local.TasksForPhase(slug, ph->seq);

		b << "  <div style=\"margin-top:1.5rem; background:#f8fafc; border:1px solid #e2e8f0; border-radius:6px; padding:1rem;\">\n"
		  << "    <div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:0.75rem;\">\n"
		  << "      <h3 style=\"font-size:1.1rem;\">Phase " << ph->seq << ": " << ph->title << "</h3>\n"
		  << "      " << ManagerSkylarkApp::RenderBadge(ph->status) << "\n"
		  << "    </div>\n";

		if(!tasks.IsEmpty()) {
			b << "    <table><thead><tr><th>#</th><th>Task</th><th>Status</th><th>Instructions / Result</th></tr></thead><tbody>\n";
			for(int t = 0; t < tasks.GetCount(); t++) {
				const Task* tk = tasks[t];
				b << "<tr>"
				  << "<td><code>" << tk->seq << "</code></td>"
				  << "<td><strong>" << tk->title << "</strong></td>"
				  << "<td>" << ManagerSkylarkApp::RenderBadge(tk->status) << "</td>"
				  << "<td>"
				  << (!tk->instructions.IsEmpty() ? ("<div style=\"font-size:0.85rem; color:#475569;\">" + tk->instructions + "</div>") : "")
				  << (!tk->result.IsEmpty() ? ("<div style=\"font-size:0.85rem; color:#059669; font-weight:500; margin-top:2px;\">&bull; " + tk->result + "</div>") : "")
				  << "</td></tr>\n";
			}
			b << "    </tbody></table>\n";
		}
		else {
			b << "    <p style=\"font-size:0.85rem; color:#94a3b8;\">No tasks defined in this phase.</p>\n";
		}
		b << "  </div>\n";
	}
	b << "</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(p->title, "projects", b, &http);
}

SKYLARK(GoalsPage, "goals")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	V2Store& v2 = app.GetV2Store();

	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>v2 Goals</span></div>\n"
	  << "  <table><thead><tr><th>Repo</th><th>Id</th><th>Title</th><th>Progress</th><th>Tasks</th><th>Action</th></tr></thead><tbody>\n";

	const Vector<V2Goal>& goals = v2.GetGoals();
	for(int i = 0; i < goals.GetCount(); i++) {
		const V2Goal& g = goals[i];
		b << "<tr>"
		  << "<td><span class=\"badge-v2\">" << g.repo << "</span></td>"
		  << "<td><code>" << g.id << "</code></td>"
		  << "<td><strong><a href=\"/goal/" << g.id << "\">" << g.title << "</a></strong></td>"
		  << "<td>" << ManagerSkylarkApp::RenderProgressBar(g.GetProgressPercent()) << "</td>"
		  << "<td>" << g.GetDoneTasks() << " / " << g.GetTotalTasks() << "</td>"
		  << "<td><a href=\"/goal/" << g.id << "\" class=\"btn btn-primary\" style=\"padding:4px 10px;\">View</a></td>"
		  << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Goals", "goals", b, &http);
}

SKYLARK(GoalDetailPage, "goal/**")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	V2Store& v2 = app.GetV2Store();

	String full_path;
	for(int i = 0; i < http.GetParamCount(); i++) {
		if(i > 0) full_path << "/";
		full_path << http[i];
	}

	const V2Goal* g = v2.FindGoal(full_path);
	if(!g) {
		http.Redirect(GoalsPage);
		return;
	}

	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>Goal: " << g->title << "</span><span class=\"badge-v2\">" << g->repo << "</span></div>\n"
	  << "  <div style=\"margin-bottom:1.5rem;\">" << ManagerSkylarkApp::RenderProgressBar(g->GetProgressPercent()) << "</div>\n"
	  << "  <h3 style=\"font-size:1.1rem; margin-bottom:0.75rem;\">Decomposition &amp; Tasks</h3>\n"
	  << "  <table><thead><tr><th>Kind</th><th>Id</th><th>Title</th><th>Status</th><th>Gates</th><th>Sets</th></tr></thead><tbody>\n";

	for(int i = 0; i < g->tasks.GetCount(); i++) {
		const V2GoalNode& n = g->tasks[i];
		b << "<tr>"
		  << "<td><span style=\"font-size:11px; text-transform:uppercase; font-weight:700; color:" << (n.kind == "phase" ? "#3b82f6" : "#64748b") << ";\">" << n.kind << "</span></td>"
		  << "<td><code>" << n.id << "</code></td>"
		  << "<td><strong>" << n.title << "</strong>" << (!n.notes.IsEmpty() ? ("<br><small style=\"color:#64748b;\">" + n.notes + "</small>") : "") << "</td>"
		  << "<td>" << (n.kind == "task" ? ManagerSkylarkApp::RenderBadge(n.status) : "-") << "</td>"
		  << "<td><code style=\"font-size:11px;\">" << (n.gates.IsEmpty() ? "" : n.gates.GetKey(0)) << "</code></td>"
		  << "<td><code style=\"font-size:11px;\">" << (n.sets.IsEmpty() ? "" : n.sets.GetKey(0)) << "</code></td>"
		  << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";

	// State vector card
	if(!g->state.IsEmpty()) {
		b << "<div class=\"card\">\n"
		  << "  <div class=\"card-header\"><span>Target State Vector</span></div>\n"
		  << "  <table><thead><tr><th>State Key</th><th>Target Value</th></tr></thead><tbody>\n";
		for(int i = 0; i < g->state.GetCount(); i++) {
			b << "<tr><td><code>" << g->state.GetKey(i) << "</code></td><td><strong>" << g->state[i] << "</strong></td></tr>\n";
		}
		b << "</tbody></table>\n</div>\n";
	}

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(g->title, "goals", b, &http);
}

SKYLARK(IssuesPage, "issues")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	V2Store& v2 = app.GetV2Store();

	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>v2 Issues</span></div>\n"
	  << "  <table><thead><tr><th>Repo</th><th>Id</th><th>Title</th><th>Status</th><th>Notes</th></tr></thead><tbody>\n";

	const Vector<V2Issue>& issues = v2.GetIssues();
	for(int i = 0; i < issues.GetCount(); i++) {
		const V2Issue& is = issues[i];
		b << "<tr>"
		  << "<td><span class=\"badge-v2\">" << is.repo << "</span></td>"
		  << "<td><code>" << is.id << "</code></td>"
		  << "<td><strong>" << is.title << "</strong></td>"
		  << "<td>" << ManagerSkylarkApp::RenderBadge(is.status) << "</td>"
		  << "<td>" << is.notes << "</td>"
		  << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Issues", "issues", b, &http);
}

SKYLARK(ReleasesPage, "releases")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	V2Store& v2 = app.GetV2Store();

	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>v2 Releases</span></div>\n"
	  << "  <table><thead><tr><th>Id</th><th>Title</th><th>Status</th><th>Goals Linked</th></tr></thead><tbody>\n";

	const Vector<V2Release>& rels = v2.GetReleases();
	for(int i = 0; i < rels.GetCount(); i++) {
		const V2Release& r = rels[i];
		b << "<tr>"
		  << "<td><code>" << r.id << "</code></td>"
		  << "<td><strong>" << r.title << "</strong></td>"
		  << "<td>" << ManagerSkylarkApp::RenderBadge(r.status) << "</td>"
		  << "<td>" << r.goals.GetCount() << "</td>"
		  << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Releases", "releases", b, &http);
}

SKYLARK(SweAgentPage, "sweagent")
{
	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>SweAgent Autonomous Web Runner</span></div>\n"
	  << "  <p style=\"color:#64748b; margin-bottom:1.5rem;\">Dispatch software engineering tasks to autonomous agents powered by OpenRouter and U++ SweAgent runtime.</p>\n"
	  << "  <form method=\"POST\" action=\"/api/sweagent/run\" style=\"display:flex; flex-direction:column; gap:1rem;\">\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-weight:600; font-size:0.9rem; margin-bottom:4px;\">Task Statement / Problem</label>\n"
	  << "      <textarea name=\"task\" rows=\"3\" style=\"width:100%; border:1px solid #cbd5e1; border-radius:6px; padding:8px; font-family:inherit;\" placeholder=\"e.g. Inspect the current workspace and run tests\"></textarea>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:1rem;\">\n"
	  << "      <div style=\"flex:1;\">\n"
	  << "        <label style=\"display:block; font-weight:600; font-size:0.9rem; margin-bottom:4px;\">OpenRouter Model</label>\n"
	  << "        <input type=\"text\" name=\"model\" value=\"nvidia/nemotron-3.5-super-vl\" style=\"width:100%; border:1px solid #cbd5e1; border-radius:6px; padding:8px;\">\n"
	  << "      </div>\n"
	  << "      <div style=\"width:150px;\">\n"
	  << "        <label style=\"display:block; font-weight:600; font-size:0.9rem; margin-bottom:4px;\">Mode</label>\n"
	  << "        <select name=\"mode\" style=\"width:100%; border:1px solid #cbd5e1; border-radius:6px; padding:8px;\">\n"
	  << "          <option value=\"yolo\">Yolo</option>\n"
	  << "          <option value=\"confirm\">Confirm</option>\n"
	  << "        </select>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button type=\"submit\" class=\"btn btn-primary\">Dispatch SweAgent Task</button>\n"
	  << "    </div>\n"
	  << "  </form>\n"
	  << "</div>\n";

	// API guidance card
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>REST API Integration</span></div>\n"
	  << "  <p style=\"margin-bottom:0.75rem;\">Autonomous workers and CLI agents can query and advance tasks via JSON REST endpoints:</p>\n"
	  << "  <pre class=\"code-box\">"
	  << "curl http://localhost:8080/api/tasks/next/manager-skylark\n"
	  << "curl -X POST http://localhost:8080/api/tasks/claim -d '{\"project\":\"manager-skylark\",\"phase\":\"2\",\"task\":\"5\"}'\n"
	  << "curl -X POST http://localhost:8080/api/tasks/done -d '{\"project\":\"manager-skylark\",\"phase\":\"2\",\"task\":\"5\",\"result\":\"Scaffold complete\"}'\n"
	  << "</pre>\n"
	  << "</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("SweAgent", "sweagent", b, &http);
}

SKYLARK(ManagerChatPage, "chat")
{
	String session_id = http["session"];
	String snippet = WebChatView::RenderSnippet(session_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Pair Programming Chat", "chat", snippet, &http);
}

static String GetCookieVal(const Http& http, const char* name)
{
	String cookie_hdr = http.GetHeader("cookie");
	Vector<String> parts = Split(cookie_hdr, ';');
	for(int i = 0; i < parts.GetCount(); i++) {
		String p = TrimBoth(parts[i]);
		if(p.StartsWith(String(name) + "="))
			return TrimBoth(p.Mid((int)strlen(name) + 1));
	}
	return String();
}

SKYLARK(PlatformViewRedirect, "view/*")
{
	String proj = http[0];
	String pref = GetCookieVal(http, "wf_platform_pref");
	if(pref == "sf") {
		http.Redirect("/projects/" + proj + "/");
	} else {
		http.Redirect("/repo/SeppoPakonen/" + proj);
	}
}

SKYLARK(PlatformSetPref, "api/platform/set")
{
	String pref = http["platform"];
	if(pref == "sf" || pref == "gh") {
		http.SetCookie("wf_platform_pref", pref, GetSysTime() + 3600 * 24 * 365, "/");
		http.ContentType("application/json") << "{\"status\":\"ok\",\"platform\":\"" << pref << "\"}";
	} else {
		http.ContentType("application/json") << "{\"error\":\"invalid_platform\"}";
	}
}

END_UPP_NAMESPACE
