#include "ManagerSkylark.h"
#include "SystemAuth.h"
#include "HostPlatform.h"

NAMESPACE_UPP

ManagerSkylarkApp* ManagerSkylarkApp::s_instance = NULL;

ManagerSkylarkApp::ManagerSkylarkApp()
	: test_mode(false)
	, custom_port(8080)
{
	s_instance = this;
	root = "";
	port = 8080;
	ip = "0.0.0.0";
	threads = 2;
	prefork = 0;
	use_caching = false;
}

ManagerSkylarkApp& ManagerSkylarkApp::Get()
{
	ASSERT(s_instance);
	return *s_instance;
}

void ManagerSkylarkApp::InitApp()
{
	v2_store.Load();
	local_store.LoadAll();
}

void ManagerSkylarkApp::StopServer()
{
	Quit();
	for(int i = 0; i < threads + 2; i++) {
		HttpRequest req(Format("http://127.0.0.1:%d/quit", port));
		req.Timeout(200);
		req.Execute();
	}
}

bool ManagerSkylarkApp::Initialize(const Vector<String>& args)
{
	CommandLineArguments cla;
	cla.SetDescription("ManagerSkylark - Web application following ../Manager/v2 design");
	cla.AddArg("help", 'h', "Show help options", false);
	cla.AddArg("port", 'p', "HTTP server port", true, "port");
	cla.AddArg("test", 0, "Run automated HTTP and store verification", false);

	if(!cla.Parse(args))
		return false;

	if(cla.IsArg('h') || cla.IsArg("help")) {
		cla.PrintHelp();
		return false;
	}

	test_mode = cla.IsArg("test");

	String p_str = cla.GetArg('p');
	if(p_str.IsEmpty())
		p_str = cla.GetArg("port");

	if(!p_str.IsEmpty()) {
		custom_port = ScanInt(p_str);
		if(custom_port > 0 && custom_port < 65536)
			port = custom_port;
	}

	InitApp();
	return true;
}

String ManagerSkylarkApp::RenderBadge(const String& status)
{
	String s = ToLower(status);
	String bg = "var(--wf-surface-hover, #2d3748)", fg = "var(--wf-fg-normal, #d3dae3)";
	if(s == "done" || s == "released" || s == "resolved") {
		bg = "var(--wf-accent-positive, #00aa7f)"; fg = "#ffffff";
	}
	else if(s == "active" || s == "in-progress") {
		bg = "var(--wf-accent-focus, #00a1c5)"; fg = "#ffffff";
	}
	else if(s == "open" || s == "planned" || s == "accepted" || s == "verified") {
		bg = "var(--wf-accent-hover, #0088a6)"; fg = "#ffffff";
	}
	else if(s == "blocked" || s == "rejected") {
		bg = "var(--wf-accent-negative, #c83771)"; fg = "#ffffff";
	}
	else if(s == "proposed" || s == "draft") {
		bg = "var(--wf-accent-neutral, #6d55aa)"; fg = "#ffffff";
	}

	return Format("<span class=\"wf-badge\" style=\"display:inline-block; padding:3px 8px; border-radius:12px; font-size:12px; font-weight:600; color:%s; background-color:%s;\">%s</span>",
	              fg, bg, status);
}

String ManagerSkylarkApp::RenderProgressBar(double percent)
{
	if(percent < 0) percent = 0;
	if(percent > 100) percent = 100;
	return Format("<div style=\"width:100%%; background-color:var(--wf-bg-alt, #1c202f); border-radius:6px; height:12px; overflow:hidden; border:1px solid var(--wf-border-color, rgba(0,161,197,0.2));\">"
	              "<div style=\"width:%.1f%%; background-color:var(--wf-accent-focus, #00a1c5); height:100%%; border-radius:5px;\"></div>"
	              "</div><span style=\"font-size:11px; color:var(--wf-fg-inactive, #8b919d);\">%.0f%%</span>",
	              percent, percent);
}

NavigationModel ManagerSkylarkApp::BuildNavigation(const String& active_nav)
{
	NavigationModel nav;

	// Pillar 1: Self-Hosted / Local Machine
	NavCategory& cat_host = nav.AddCategory("host", "Self-Hosted", "server", "Local workstation, file system, git repositories, and disk storage");
	NavSection& sec_host_local = cat_host.AddSection("Local Workstation (192.168.1.106)");
	sec_host_local.Add("host", "Host Overview", "/host", "server", "Hardware metrics, uptime, and system status").Active(active_nav == "host" || active_nav == "host_overview");
	sec_host_local.Add("host_files", "Home Files", "/host/files", "folder", "Authenticated file explorer for local user home directory").Active(active_nav == "host_files");
	sec_host_local.Add("host_repos", "Local Repos", "/host/repos", "git-branch", "Local Git repositories in ~/Dev/ and build targets").Active(active_nav == "host_repos");
	sec_host_local.Add("host_storage", "Disk Storage", "/host/storage", "database", "Mount points, disk usage, and storage allocations").Active(active_nav == "host_storage");

	NavSection& sec_host_sync = cat_host.AddSection("Cluster Connectivity");
	sec_host_sync.Add("host_fleet", "Fleet Sync & Build", "/host/fleet", "refresh-cw", "Cluster-wide Git synchronization and remote compilation dispatcher").Active(active_nav == "host_fleet");
	sec_host_sync.Add("host_nodes", "Compute Mesh", "/host/nodes", "cpu", "Distributed machine fleet and SSH/rsync mirrors").Active(active_nav == "host_nodes");
	sec_host_sync.Add("host_settings", "Mesh Settings", "/host/settings", "settings", "Network host credentials and fleet management").Active(active_nav == "host_settings");

	// Pillar 2: Mesh-Hosted (Cluster Fleet with Equal Value & Parity)
	NavCategory& cat_mesh = nav.AddCategory("mesh", "Mesh-Hosted", "cpu", "Connected mesh computers with full equal parity: files, repos, storage, and remote terminal");
	NavSection& sec_mesh_105 = cat_mesh.AddSection("MediaCenter FX-8350 (192.168.1.105)");
	sec_mesh_105.Add("node_mediacenter-gentoo_overview", "Node Overview", "/node/mediacenter-gentoo/overview", "server", "Remote Gentoo Linux AMD FX-8350 system metrics").Active(active_nav == "node_mediacenter-gentoo_overview" || active_nav == "node_105_overview" || (active_nav == "node_overview" && active_nav.Find("mediacenter") >= 0));
	sec_mesh_105.Add("node_mediacenter-gentoo_files", "Remote Files", "/node/mediacenter-gentoo/files", "folder", "Remote home directory file explorer").Active(active_nav == "node_mediacenter-gentoo_files" || active_nav == "node_105_files" || (active_nav == "node_files" && active_nav.Find("mediacenter") >= 0));
	sec_mesh_105.Add("node_mediacenter-gentoo_repos", "Remote Repos", "/node/mediacenter-gentoo/repos", "git-branch", "Remote Git repositories in ~/Dev/").Active(active_nav == "node_mediacenter-gentoo_repos" || active_nav == "node_105_repos" || (active_nav == "node_repos" && active_nav.Find("mediacenter") >= 0));
	sec_mesh_105.Add("node_mediacenter-gentoo_storage", "Disk Storage", "/node/mediacenter-gentoo/storage", "database", "Remote filesystem mounts and disk space").Active(active_nav == "node_mediacenter-gentoo_storage" || active_nav == "node_105_storage" || (active_nav == "node_storage" && active_nav.Find("mediacenter") >= 0));

	NavSection& sec_mesh_169 = cat_mesh.AddSection("Gen7 Server FX-6300 (192.168.1.169)");
	sec_mesh_169.Add("node_gen7-lan_overview", "Node Overview", "/node/gen7-lan/overview", "server", "Remote Gentoo Linux AMD FX-6300 system metrics").Active(active_nav == "node_gen7-lan_overview" || active_nav == "node_169_overview" || (active_nav == "node_overview" && active_nav.Find("gen7") >= 0));
	sec_mesh_169.Add("node_gen7-lan_files", "Remote Files", "/node/gen7-lan/files", "folder", "Remote home directory file explorer").Active(active_nav == "node_gen7-lan_files" || active_nav == "node_169_files" || (active_nav == "node_files" && active_nav.Find("gen7") >= 0));
	sec_mesh_169.Add("node_gen7-lan_repos", "Remote Repos", "/node/gen7-lan/repos", "git-branch", "Remote Git repositories in ~/Dev/").Active(active_nav == "node_gen7-lan_repos" || active_nav == "node_169_repos" || (active_nav == "node_repos" && active_nav.Find("gen7") >= 0));
	sec_mesh_169.Add("node_gen7-lan_storage", "Disk Storage", "/node/gen7-lan/storage", "database", "Remote filesystem mounts and disk space").Active(active_nav == "node_gen7-lan_storage" || active_nav == "node_169_storage" || (active_nav == "node_storage" && active_nav.Find("gen7") >= 0));

	NavSection& sec_mesh_manage = cat_mesh.AddSection("Mesh Operations");
	sec_mesh_manage.Add("mesh_fleet", "Fleet Sync & Build", "/host/fleet", "refresh-cw", "1-click cluster Git sync and remote compilation").Active(active_nav == "mesh_fleet" || active_nav == "host_fleet");
	sec_mesh_manage.Add("mesh_nodes", "Mesh Topology", "/host/nodes", "cpu", "Live cluster topology, latency probes, and SSH sync").Active(active_nav == "mesh_nodes");
	sec_mesh_manage.Add("mesh_settings", "Mesh Settings", "/host/settings", "settings", "Add/remove cluster nodes and configure SSH credentials").Active(active_nav == "mesh_settings");

	// Pillar 2: GitHub Hub
	NavCategory& cat_gh = nav.AddCategory("github", "GitHub Hub", "github", "GitHub user profile, private/public repositories, issues, PRs, and actions");
	NavSection& sec_gh_user = cat_gh.AddSection("User & Repositories");
	sec_gh_user.Add("gh_profile", "SeppoPakonen Profile", "/repo/SeppoPakonen", "user", "Authentic GitHub user portfolio and contributions").Active(active_nav == "gh_profile" || active_nav == "user");
	sec_gh_user.Add("gh_repos", "All Repositories", "/repo/SeppoPakonen", "book", "GitHub repositories listing and filters").Active(active_nav == "gh_repos");
	sec_gh_user.Add("gh_priv", "ai-upp-priv Workspace", "/repo/SeppoPakonen/ai-upp", "lock", "Private development workspace and active branches").Active(active_nav == "gh_priv");

	NavSection& sec_gh_collab = cat_gh.AddSection("Development & Collaboration");
	sec_gh_collab.Add("gh_tree", "Code Explorer", "/repo/SeppoPakonen/ai-upp/tree", "code", "Browse repository tree and blob source files").Active(active_nav == "gh_tree");
	sec_gh_collab.Add("gh_issues", "Issues Tracker", "/repo/SeppoPakonen/ai-upp/issues", "bug", "Issue tracker and bug triage").Active(active_nav == "gh_issues");
	sec_gh_collab.Add("gh_pulls", "Pull Requests & Diffs", "/repo/SeppoPakonen/ai-upp/pulls", "git-pull-request", "Code reviews and unified diffs").Active(active_nav == "gh_pulls");
	sec_gh_collab.Add("gh_kanban", "Project Board", "/repo/SeppoPakonen/ai-upp/projects", "layout", "Visual Kanban workflow synced with Manager").Active(active_nav == "gh_kanban");

	// Pillar 3: SourceForge Hub
	NavCategory& cat_sf = nav.AddCategory("sourceforge", "SourceForge Hub", "external-link", "SourceForge projects, downloads, code trees, wiki, and mailing lists");
	NavSection& sec_sf_proj = cat_sf.AddSection("Topside Project");
	sec_sf_proj.Add("sf_summary", "Topside Overview", "/projects/topside/", "home", "Project metadata, maintainers, and releases").Active(active_nav == "sf_summary");
	sec_sf_proj.Add("sf_files", "Release Files", "/projects/topside/files/", "download", "Download mirrors and tarball releases").Active(active_nav == "sf_files");
	sec_sf_proj.Add("sf_code", "Code Repository", "/p/topside/code/", "code", "Source code tree, branch selector, and commit logs").Active(active_nav == "sf_code");

	NavSection& sec_sf_comm = cat_sf.AddSection("Community & Support");
	sec_sf_comm.Add("sf_tickets", "Tickets Tracker", "/p/topside/tickets/", "bug", "Issue tracker and feature requests").Active(active_nav == "sf_tickets");
	sec_sf_comm.Add("sf_discussion", "Discussions", "/p/topside/discussion/", "chat", "Public discussion forums and developer lists").Active(active_nav == "sf_discussion");
	sec_sf_comm.Add("sf_wiki", "Project Wiki", "/p/topside/wiki/", "book", "Documentation, architecture guides, and FAQs").Active(active_nav == "sf_wiki");

	// Pillar 4: Local AI Hub
	NavCategory& cat_ai = nav.AddCategory("sweagent", "Local AI Hub", "robot", "Autonomous pair programming, local issue triage, and multi-tracker orchestration");
	NavSection& sec_ai_ops = cat_ai.AddSection("Autonomous Work & Orchestration");
	sec_ai_ops.Add("chat", "Pair Chat", "/chat", "chat", "Interactive human-agent pair programming terminal").Badge("Live", "emerald").Active(active_nav == "chat");
	sec_ai_ops.Add("sweagent", "SweAgent", "/sweagent", "terminal", "Autonomous SWE-Agent runner and task execution").Active(active_nav == "sweagent");
	sec_ai_ops.Add("projects", "Local Shadow Drafts", "/projects", "layers", "Local low-friction drafts, phases, and task execution").Active(active_nav == "projects");
	sec_ai_ops.Add("goals", "Manager v2 Goals", "/goals", "flag", "High-level milestones and roadmaps").Active(active_nav == "goals");

	NavSection& sec_ai_api = cat_ai.AddSection("API & Diagnostics");
	sec_ai_api.Add("spec", "Framework Spec", "/api/framework/spec", "code", "Semantic JSON tree and API surface for agents").Badge("JSON", "cyan").Active(active_nav == "spec");
	sec_ai_api.Add("health", "Health Status", "/api/health", "server", "Application uptime and health metrics").Badge("HTTP 200", "emerald").Active(active_nav == "health");

	if(active_nav.StartsWith("host"))
		nav.SetActive("host", active_nav);
	else if(active_nav.StartsWith("mesh") || active_nav.StartsWith("node"))
		nav.SetActive("mesh", active_nav);
	else if(active_nav.StartsWith("gh") || active_nav.StartsWith("repo") || active_nav == "user")
		nav.SetActive("github", active_nav);
	else if(active_nav.StartsWith("sf") || active_nav.StartsWith("projects/topside") || active_nav.StartsWith("p/"))
		nav.SetActive("sourceforge", active_nav);
	else if(active_nav == "chat" || active_nav == "sweagent" || active_nav == "projects" || active_nav == "goals" || active_nav == "issues" || active_nav == "releases" || active_nav == "spec" || active_nav == "health")
		nav.SetActive("sweagent", active_nav);

	return nav;
}

TreeModel ManagerSkylarkApp::BuildProjectsTree(ManagerStore& store, const String& active_slug)
{
	TreeModel tree;
	const Vector<Project>& projs = store.Projects();

	for(int i = 0; i < projs.GetCount(); i++) {
		const Project& p = projs[i];
		bool p_act = (p.slug == active_slug);

		TreeNode& p_node = tree.Add(p.slug, p.title, "/project/" + p.slug, "folder")
		                       .Icon("folder", "folder-open")
		                       .Expanded(p_act || projs.GetCount() <= 3)
		                       .Selected(p_act)
		                       .Badge(p.status, p.status == "active" ? "emerald" : "cyan")
		                       .Tooltip(p.description);

		Vector<const Phase*> phases = store.PhasesFor(p.slug);
		for(int ph = 0; ph < phases.GetCount(); ph++) {
			const Phase* phase = phases[ph];
			TreeNode& ph_node = p_node.AddChild(p.slug + "-ph-" + AsString(phase->seq),
			                                    phase->title,
			                                    "/project/" + p.slug,
			                                    "layout")
			                          .Expanded(p_act);

			Vector<const Task*> tasks = store.TasksForPhase(p.slug, phase->seq);
			for(int t = 0; t < tasks.GetCount(); t++) {
				const Task* task = tasks[t];
				TreeNode& t_node = ph_node.AddChild(p.slug + "-tsk-" + task->slug,
				                                   task->title,
				                                   "/project/" + p.slug,
				                                   "check");
				t_node.Badge(task->status, task->status == "done" ? "emerald" : "neutral");
			}
		}
	}

	return tree;
}

TreeModel ManagerSkylarkApp::BuildMeshFleetTree(const String& active_nav, const String& active_node_id)
{
	TreeModel tree;
	Vector<DistributedNode> nodes = HostPlatformService::GetConfiguredNodes(false);

	// Fleet Root Node
	TreeNode& fleet_root = tree.Add("fleet-root", "Compute Mesh Fleet (3 Nodes)", "/host/nodes", "cpu")
	                           .Icon("cpu", "cpu")
	                           .Expanded(true);

	for(int n = 0; n < nodes.GetCount(); n++) {
		const DistributedNode& node = nodes[n];
		bool is_act_node = (active_node_id == node.id || active_nav.Find(node.id) >= 0 ||
		                   (node.is_local && (active_nav == "host" || active_nav.StartsWith("host_"))));

		String base_url = node.is_local ? "/host" : ("/node/" + node.id);
		String node_title = node.name + " (" + node.hostname + ")";

		bool is_overview_act = (is_act_node && active_nav.Find("overview") >= 0);
		bool is_files_act = (is_act_node && active_nav.Find("files") >= 0);
		bool is_repos_act = (is_act_node && active_nav.Find("repos") >= 0);
		bool is_storage_act = (is_act_node && active_nav.Find("storage") >= 0);

		TreeNode& n_node = fleet_root.AddChild("node-" + node.id, node_title, base_url + "/overview", node.is_local ? "monitor" : "server")
		                             .Icon(node.is_local ? "monitor" : "server", node.is_local ? "monitor" : "server")
		                             .Expanded(is_act_node || nodes.GetCount() <= 3)
		                             .Selected(is_overview_act)
		                             .Badge(node.status, node.status == "online" ? "emerald" : "cyan");

		n_node.AddChild("node-" + node.id + "-overview", "Overview", base_url + "/overview", "activity")
		      .Selected(is_overview_act);

		n_node.AddChild("node-" + node.id + "-files", "Home Files", base_url + "/files", "folder")
		      .Selected(is_files_act);

		TreeNode& repos_node = n_node.AddChild("node-" + node.id + "-repos", "Repositories", base_url + "/repos", "git-branch")
		                             .Icon("git-branch", "git-branch")
		                             .Expanded(is_repos_act || is_act_node)
		                             .Selected(is_repos_act);

		Vector<HostRepoInfo> repos = HostPlatformService::GetRepositoriesForNode(node.id);
		repos_node.Badge(AsString(repos.GetCount()), is_repos_act ? "emerald" : "neutral");

		// If this node is active or viewing repos, list the actual repositories!
		if(is_act_node || is_repos_act) {
			for(int r = 0; r < repos.GetCount(); r++) {
				const HostRepoInfo& repo = repos[r];
				String repo_url = is_repos_act ? (base_url + "/repos#repo-" + repo.name)
				                               : (base_url + "/files/Dev/" + repo.name);
				TreeNode& r_child = repos_node.AddChild("repo-" + node.id + "-" + repo.name, repo.name, repo_url, "code");
				if(!repo.branch.IsEmpty()) {
					r_child.Badge(repo.branch, (repo.branch == "master" || repo.branch == "main") ? "neutral" : "emerald");
				}
			}
		}

		n_node.AddChild("node-" + node.id + "-storage", "Disk Storage", base_url + "/storage", "database")
		      .Selected(is_storage_act);
	}

	// Mesh Operations
	TreeNode& ops_node = tree.Add("fleet-ops", "Cluster Operations", "/host/fleet", "settings")
	                         .Icon("settings", "settings")
	                         .Expanded(active_nav == "host_fleet" || active_nav == "host_nodes" || active_nav == "host_settings" || active_nav == "mesh_fleet" || active_nav == "mesh_nodes" || active_nav == "mesh_settings");

	ops_node.AddChild("ops-fleet", "Fleet Sync & Build", "/host/fleet", "refresh-cw")
	        .Selected(active_nav == "host_fleet" || active_nav == "mesh_fleet");
	ops_node.AddChild("ops-topology", "Mesh Topology & Latency", "/host/nodes", "activity")
	        .Selected(active_nav == "host_nodes" || active_nav == "mesh_nodes");
	ops_node.AddChild("ops-settings", "Mesh Settings & Credentials", "/host/settings", "sliders")
	        .Selected(active_nav == "host_settings" || active_nav == "mesh_settings");

	return tree;
}

TreeModel ManagerSkylarkApp::BuildGoalsTree(V2Store& store, const String& active_id)
{
	TreeModel tree;
	const Vector<V2Goal>& goals = store.GetGoals();

	TreeNode& root = tree.Add("goals-root", "V2 Architecture Goals", "/goals", "flag")
	                     .Icon("flag", "flag")
	                     .Expanded(true);

	for(int g = 0; g < goals.GetCount(); g++) {
		const V2Goal& goal = goals[g];
		bool g_act = (goal.id == active_id);

		TreeNode& g_node = root.AddChild("goal-" + goal.id, goal.title, "/goal/" + goal.id, "flag")
		                       .Icon("flag", "flag")
		                       .Expanded(g_act || goals.GetCount() <= 4)
		                       .Selected(g_act)
		                       .Badge(Format("%.0f%%", goal.GetProgressPercent()), "cyan");

		for(int t = 0; t < goal.tasks.GetCount(); t++) {
			const V2GoalNode& task = goal.tasks[t];
			TreeNode& t_node = g_node.AddChild("task-" + goal.id + "-" + task.id,
			                                   task.title,
			                                   "/goal/" + goal.id + "#task-" + task.id,
			                                   task.kind == "phase" ? "layout" : "check");
			t_node.Badge(task.status, task.status == "done" ? "emerald" : "neutral");
		}
	}

	return tree;
}

String ManagerSkylarkApp::RenderHtmlPage(const String& title, const String& active_nav, const String& body_content, const Http* http)
{
	NavigationModel nav = BuildNavigation(active_nav);

	PageBuilder pb;
	pb.Title(title + " - Manager Skylark")
	  .Brand("Manager Skylark", "/", "cube")
	  .Navigation(nav)
	  .Template("megamenu")
	  .Theme("vivid-dark")
	  .Density(active_nav == "chat" ? DENSITY_NONE : DENSITY_COMFORTABLE)
	  .Content(body_content);

	// User auth menu in navbar
	if(http) {
		UserSession sess = SystemAuthService::GetCurrentSession(*http);
		pb.NavActions(SystemAuthService::RenderUserMenu(sess));
	} else {
		UserSession guest;
		pb.NavActions(SystemAuthService::RenderUserMenu(guest));
	}

	// Contextual Left Tree configuration
	if(active_nav != "home" && active_nav != "chat" && active_nav != "login") {
		TreeModel tree;
		String tree_title = "Navigation";
		String tree_id = "wf-tree";

		if(active_nav.StartsWith("node") || active_nav.StartsWith("host") || active_nav.StartsWith("mesh")) {
			String active_node_id = "";
			if(active_nav.StartsWith("node_")) {
				Vector<String> parts = Split(active_nav, '_');
				if(parts.GetCount() >= 2)
					active_node_id = parts[1];
			} else if(active_nav.StartsWith("host")) {
				active_node_id = "local-workstation";
			}
			tree = BuildMeshFleetTree(active_nav, active_node_id);
			tree_title = "Compute Fleet & Repositories";
			tree_id = "mesh-tree";
		}
		else if(active_nav == "projects" || active_nav.StartsWith("project")) {
			tree = BuildProjectsTree(Get().GetLocalStore(), "");
			tree_title = "Manager Task Queue";
			tree_id = "mgr-tree";
		}
		else if(active_nav == "goals" || active_nav.StartsWith("goal")) {
			tree = BuildGoalsTree(Get().GetV2Store(), "");
			tree_title = "Milestones & Goals";
			tree_id = "goals-tree";
		}
		else {
			tree = TreeGenerators::FromNavigation(nav);
			tree_title = "Workspace Navigation";
			tree_id = "nav-tree";
		}

		if(!tree.IsEmpty()) {
			pb.LeftTree(tree, tree_title, tree_id);
			pb.AutoOutline(true); // Automatically builds right-side TOC outline
		}
	}

	pb.Footer("Manager Skylark &bull; Built on Ultimate++ Skylark &amp; Core &bull; OpenRouter &amp; SweAgent Autonomous Engineering");

	// Add manager custom component styling integrated with Vivid Plasma palette variables
	String custom_css;
	custom_css << "<style>\n"
	           << "  main.wf-page-body { max-width: var(--wf-container-width, 1500px); margin: 0 auto; }\n"
	           << "  .card { background: var(--wf-bg-view, #151d2b); border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); border-radius: var(--wf-radius, 8px); padding: var(--wf-pad-card, 1.25rem); margin-bottom: var(--wf-gap-grid, 1.25rem); box-shadow: var(--wf-shadow); }\n"
	           << "  .card-header { font-size: 1.15rem; font-weight: 700; margin-bottom: 0.85rem; color: var(--wf-fg-normal, #d3dae3); border-bottom: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); padding-bottom: 0.5rem; display: flex; justify-content: space-between; align-items: center; }\n"
	           << "  .card-header a { font-size: 0.85rem; }\n"
	           << "  .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: var(--wf-gap-grid, 1.25rem); }\n"
	           << "  .grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: var(--wf-gap-grid, 1.25rem); }\n"
	           << "  .grid-4 { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: var(--wf-gap-grid, 1.25rem); }\n"
	           << "  .stat-card { background: var(--wf-bg-view, #151d2b); border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); border-radius: var(--wf-radius, 8px); padding: var(--wf-pad-card, 1.25rem); display: flex; flex-direction: column; gap: 4px; box-shadow: var(--wf-shadow); }\n"
	           << "  .stat-val { font-size: 2rem; font-weight: 800; color: var(--wf-accent-focus, #00a1c5); }\n"
	           << "  .stat-lbl { font-size: 0.8rem; color: var(--wf-fg-inactive, #8b919d); text-transform: uppercase; letter-spacing: 0.05em; font-weight: 600; }\n"
	           << "  table { width: 100%; border-collapse: collapse; margin-top: 0.5rem; }\n"
	           << "  th { text-align: left; padding: var(--wf-cell-pad, 10px 12px); background-color: var(--wf-bg-alt, #1c202f); color: var(--wf-fg-inactive, #8b919d); font-weight: 600; font-size: 0.82rem; border-bottom: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); text-transform: uppercase; letter-spacing: 0.04em; }\n"
	           << "  td { padding: var(--wf-cell-pad, 10px 12px); border-bottom: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); font-size: 0.9rem; color: var(--wf-fg-normal, #d3dae3); }\n"
	           << "  tr:hover td { background-color: var(--wf-surface-hover, rgba(0, 161, 197, 0.08)); }\n"
	           << "  .badge-v2 { font-size: 0.75rem; background-color: var(--wf-accent-focus, #00a1c5); color: #ffffff; padding: 2px 7px; border-radius: 4px; font-weight: 600; }\n"
	           << "  .btn { display: inline-block; padding: var(--wf-btn-pad, 6px 14px); border-radius: var(--wf-radius, 8px); font-weight: 600; text-decoration: none; border: none; cursor: pointer; font-size: 0.88rem; transition: all 0.15s; }\n"
	           << "  .btn-primary { background-color: var(--wf-accent-focus, #00a1c5); color: #ffffff; }\n"
	           << "  .btn-primary:hover { background-color: var(--wf-accent-hover, #0088a6); }\n"
	           << "  .code-box { background-color: var(--wf-bg-alt, #1c202f); color: var(--wf-accent-focus, #00a1c5); padding: 1rem; border-radius: var(--wf-radius, 8px); font-family: monospace; font-size: 0.85rem; overflow-x: auto; border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); }\n"
	           << "</style>\n";

	pb.ExtraHead(custom_css);
	AddMarkdownSupport(pb);

	return pb.Build();
}

void ManagerSkylarkApp::AddMarkdownSupport(PageBuilder& pb)
{
	String markdown_head;
	markdown_head << "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/atom-one-dark.min.css\">\n"
	              << "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js\"></script>\n"
	              << "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/jsdiff/5.2.0/diff.min.js\"></script>\n"
	              << "<script src=\"https://cdn.jsdelivr.net/npm/marked/lib/marked.umd.js\"></script>\n"
	              << "<style>\n"
	              << WebChatView::GetMarkdownStyles() << "\n"
	              << "</style>\n";

	pb.ExtraHead(markdown_head);
	pb.ExtraScripts(WebChatView::GetMarkdownScript());
}

END_UPP_NAMESPACE
