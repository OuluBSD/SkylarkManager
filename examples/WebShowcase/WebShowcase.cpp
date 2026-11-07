#include "WebShowcase.h"

NAMESPACE_UPP

NavigationModel BuildShowcaseNavigation()
{
	NavigationModel nav;

	// 1. Development Category
	NavCategory& dev = nav.AddCategory("development", "Development", "folder", "Repositories, git actions, and code diffs");
	NavSection& repos = dev.AddSection("Repositories");
	repos.Add("repo-ai-upp", "ai-upp Core", "/showcase?tab=repos", "git-branch", "Ultimate++ C++ agentic engine fork").Badge("Active", "emerald");
	repos.Add("repo-manager", "Manager Skylark", "/showcase?tab=repos", "folder", "Multi-repo management daemon").Badge("C++", "cyan");
	repos.Add("repo-swe", "mini-swe-agent", "/showcase?tab=repos", "folder", "SWE agent official benchmark runtime").Badge("Python", "purple");

	NavSection& git = dev.AddSection("Git Operations");
	git.Add("git-branches", "Branch Manager", "/showcase?tab=branches", "git-branch", "List, inspect, and switch git branches");
	git.Add("git-commits", "Commit Log", "/showcase?tab=commits", "git-commit", "Recent commit timeline & hash audit");
	git.Add("git-diff", "Diff Inspector", "/showcase?tab=diff", "git-diff", "Unified syntax-colored diffs").Badge("Viewer", "cyan");

	// 2. AI & Agent Category
	NavCategory& ai = nav.AddCategory("agent", "AI & Agent", "agent", "Pair programming, REPL CLI, and trajectory benchmarks");
	NavSection& interact = ai.AddSection("Interactive Workspaces");
	interact.Add("agent-chat", "Pair Programming Chat", "/showcase?tab=chat", "chat", "Live token-streamed chat UI").Badge("WebChat", "cyan");
	interact.Add("agent-repl", "SweAgent REPL", "/showcase?tab=repl", "terminal", "Command-line agent environment");

	NavSection& eval = ai.AddSection("Benchmarks & Logs");
	eval.Add("swebench", "SWE-bench Runner", "/showcase?tab=benchmark", "check", "Automated bug resolution benchmark").Badge("Eval", "purple");
	eval.Add("inspector", "Trajectory Inspector", "/showcase?tab=inspector", "search", "Step-by-step audit of agent actions");

	// 3. Framework & Layouts Category
	NavCategory& fw = nav.AddCategory("framework", "Framework & Layouts", "layout", "Templates, KDE Plasma themes, and density controls");
	NavSection& tmpls = fw.AddSection("Templates (Layout & Slots)");
	tmpls.Add("tmpl-mega", "MegaMenu Template", "/", "layout", "Top mega-menu + responsive body container").Badge("Default", "emerald");
	tmpls.Add("tmpl-dash", "Dashboard Template", "/dashboard", "dashboard", "Collapsible side dock + card grid");
	tmpls.Add("tmpl-work", "App Workspace Template", "/workspace", "code", "Dense full-bleed workspace for crowded UIs");

	NavSection& styles = fw.AddSection("Themes & Densities");
	styles.Add("vivid-dark", "KDE Vivid Dark Theme", "#", "palette", "L4ki Vivid Cyan palette with glass blur").Badge("Dark", "cyan");
	styles.Add("vivid-light", "KDE Vivid Light Theme", "#", "palette", "Luminous surface with dark accents").Badge("Light", "neutral");
	styles.Add("vivid-mixed", "KDE Vivid Mixed Theme", "#", "palette", "Twilight dark header + light document surface").Badge("Mixed", "cyan");
	styles.Add("density-ctrl", "Padding Density Engine", "#", "sliders", "Dense / Compact / Comfortable / Spacious");

	// 4. System & Telemetry Category
	NavCategory& sys = nav.AddCategory("system", "System", "settings", "Server metrics, agent JSON APIs, and health checks");
	NavSection& ops = sys.AddSection("Telemetry");
	ops.Add("sys-telemetry", "Visitor Telemetry", "/telemetry", "dashboard", "Cookie-based anonymous visitor analytics").Badge("Cookies", "cyan");
	ops.Add("sys-health", "HTTP Health Check", "/api/health", "settings", "Endpoint status & version report").Badge("HTTP 200", "emerald");
	ops.Add("sys-spec", "Agent Semantic API", "/api/framework/spec", "code", "Machine-readable site tree for SweAgent").Badge("JSON", "purple");
	ops.Add("sys-api-telem", "Telemetry API", "/api/telemetry", "settings", "Anonymous visitor metrics in JSON").Badge("JSON", "purple");

	return nav;
}

// ---------------------------------------------------------------------------
// Anonymous Cookie-based Telemetry Store
// ---------------------------------------------------------------------------

struct TelemetryVisitor {
	String uid;
	Time first_seen;
	Time last_seen;
	int page_views;
	Vector<String> paths;
};

static ArrayMap<String, TelemetryVisitor>& s_visitors()
{
	static ArrayMap<String, TelemetryVisitor> map;
	return map;
}

static int& s_total_page_views()
{
	static int count = 0;
	return count;
}

static String TrackVisitor(Http& http, const String& path)
{
	String uid = http["@wf_uid"];

	if(IsNull(uid) || uid.IsEmpty()) {
		uid = Format("usr_%08x%04x", (int)Random(), (int)(Random() & 0xffff));
		http.SetCookie("wf_uid", uid, GetSysTime() + 365*24*3600, "/");
	}

	String effective_path = path.IsEmpty() ? "/" : path;

	s_total_page_views()++;
	int vq = s_visitors().Find(uid);
	if(vq >= 0) {
		TelemetryVisitor& v = s_visitors()[vq];
		v.last_seen = GetSysTime();
		v.page_views++;
		if(v.paths.GetCount() < 25)
			v.paths.Add(effective_path);
	} else {
		TelemetryVisitor& v = s_visitors().Add(uid);
		v.uid = uid;
		v.first_seen = GetSysTime();
		v.last_seen = v.first_seen;
		v.page_views = 1;
		v.paths.Add(effective_path);
	}
	return uid;
}

// ---------------------------------------------------------------------------
// Skylark Route Handlers
// ---------------------------------------------------------------------------

SKYLARK(ShowcaseHome, "")
{
	TrackVisitor(http, "/");
	NavigationModel nav = BuildShowcaseNavigation();
	nav.SetActive("framework", "tmpl-mega");

	String content;
	content
		<< "<h1>WebFramework Component Showcase</h1>\n"
		<< "<div class=\"wf-alert wf-alert-info\">"
		<< "  " << VividIcon::Get("logo", 20)
		<< "  <div><strong>WebFramework Showcase:</strong> Demonstrating scalable Mega-Menu navigation, base Tree View Navigator, KDE Plasma Vivid themes, and decoupled templates &amp; densities.</div>"
		<< "</div>\n"

		<< "<h2>Scalable Mega-Menu &amp; Tree View Navigator</h2>\n"
		<< "<div class=\"wf-grid wf-grid-3\">\n"

		// Card 1: Mega-Menu Scalability
		<< "  <div class=\"wf-card\">\n"
		<< "    <div class=\"wf-card-header\">\n"
		<< "      <span>" << VividIcon::Get("layout", 16) << " Scalable Mega-Menu</span>\n"
		<< "      <span class=\"wf-badge wf-badge-cyan\">Multi-Category</span>\n"
		<< "    </div>\n"
		<< "    <div class=\"wf-card-body\">\n"
		<< "      <p style=\"margin-bottom:10px;\">Traditional top tab bars fail when managing multiple repositories, tools, and benchmarks. The Mega-Menu groups content into structured categories that expand seamlessly on hover.</p>\n"
		<< "      <ul style=\"margin-left:1.25rem; font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d);\">\n"
		<< "        <li>Multi-column categories &amp; sections</li>\n"
		<< "        <li>Badges, icons &amp; descriptions</li>\n"
		<< "        <li>Collapses to slim bar when mouse leaves</li>\n"
		<< "      </ul>\n"
		<< "    </div>\n"
		<< "    <div class=\"wf-card-footer\">\n"
		<< "      <span>Hover categories above to view</span>\n"
		<< "      <span class=\"wf-badge wf-badge-emerald\">Active</span>\n"
		<< "    </div>\n"
		<< "  </div>\n"

		// Card 2: Automatic Tree Navigator
		<< "  <div class=\"wf-card\">\n"
		<< "    <div class=\"wf-card-header\">\n"
		<< "      <span>" << VividIcon::Get("folder", 16) << " Base Tree Navigator</span>\n"
		<< "      <span class=\"wf-badge wf-badge-emerald\">Automatic</span>\n"
		<< "    </div>\n"
		<< "    <div class=\"wf-card-body\">\n"
		<< "      <p style=\"margin-bottom:10px;\">Left and right tree view panels are a built-in base feature with automatic content generation. No custom incoherent spaghetti needed:</p>\n"
		<< "      <ul style=\"margin-left:1.25rem; font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d);\">\n"
		<< "        <li>Auto from NavigationModel</li>\n"
		<< "        <li>Auto from Directory / Workspace</li>\n"
		<< "        <li>Auto Table of Contents (Outline)</li>\n"
		<< "        <li>Quick search filter (press <code>/</code>)</li>\n"
		<< "      </ul>\n"
		<< "    </div>\n"
		<< "    <div class=\"wf-card-footer\">\n"
		<< "      <span>Left &amp; Right Docks</span>\n"
		<< "      <span class=\"wf-badge wf-badge-cyan\">Collapsible</span>\n"
		<< "    </div>\n"
		<< "  </div>\n"

		// Card 3: KDE Plasma Vivid Theme & Procedural Sounds
		<< "  <div class=\"wf-card\">\n"
		<< "    <div class=\"wf-card-header\">\n"
		<< "      <span>" << VividIcon::Get("palette", 16) << " Vivid Plasma Theme</span>\n"
		<< "      <span class=\"wf-badge wf-badge-emerald\">KDE Spec</span>\n"
		<< "    </div>\n"
		<< "    <div class=\"wf-card-body\">\n"
		<< "      <p style=\"margin-bottom:10px;\">Accurately styled with <code>L4ki/Vivid-Plasma-Themes</code> RGB palettes, frosted glass blur, and procedural WebAudio sound cues:</p>\n"
		<< "      <div style=\"display:flex; gap:6px; flex-wrap:wrap; margin-bottom:12px;\">\n"
		<< "        <button class=\"wf-btn wf-btn-primary\" onclick=\"WebThemeManager.set('vivid-dark')\">Vivid Dark</button>\n"
		<< "        <button class=\"wf-btn wf-btn-secondary\" onclick=\"WebThemeManager.set('vivid-light')\">Vivid Light</button>\n"
		<< "      </div>\n"
		<< "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">\n"
		<< "        Audio cues test: "
		<< "        <a href=\"javascript:void(0)\" onclick=\"WebAudio.play('click')\">[Click]</a> "
		<< "        <a href=\"javascript:void(0)\" onclick=\"WebAudio.play('switch')\">[Switch]</a> "
		<< "        <a href=\"javascript:void(0)\" onclick=\"WebAudio.play('notify')\">[Notify]</a> "
		<< "        <a href=\"javascript:void(0)\" onclick=\"WebAudio.play('error')\">[Error]</a>\n"
		<< "      </div>\n"
		<< "    </div>\n"
		<< "    <div class=\"wf-card-footer\">\n"
		<< "      <span>Web Audio Synthesizer</span>\n"
		<< "      <span class=\"wf-badge wf-badge-cyan\">0 Assets</span>\n"
		<< "    </div>\n"
		<< "  </div>\n"

		<< "</div>\n" // .wf-grid

		// Card: Density Control
		<< "<div class=\"wf-card\" style=\"margin-top:1.5rem;\">\n"
		<< "  <div class=\"wf-card-header\">\n"
		<< "    <span>" << VividIcon::Get("sliders", 16) << " Dynamic Density &amp; Padding</span>\n"
		<< "    <span class=\"wf-badge wf-badge-purple\">Engine</span>\n"
		<< "  </div>\n"
		<< "  <div class=\"wf-card-body\">\n"
		<< "    <p style=\"margin-bottom:10px;\">Templates define layout slots and padding density. Minimize whitespace when content gets crowded:</p>\n"
		<< "    <div style=\"display:flex; gap:6px; flex-wrap:wrap;\">\n"
		<< "      <button class=\"wf-btn wf-btn-secondary\" onclick=\"WebDensityManager.set('dense')\">Dense (Tight)</button>\n"
		<< "      <button class=\"wf-btn wf-btn-secondary\" onclick=\"WebDensityManager.set('compact')\">Compact</button>\n"
		<< "      <button class=\"wf-btn wf-btn-secondary\" onclick=\"WebDensityManager.set('comfortable')\">Comfortable</button>\n"
		<< "      <button class=\"wf-btn wf-btn-secondary\" onclick=\"WebDensityManager.set('spacious')\">Spacious</button>\n"
		<< "    </div>\n"
		<< "  </div>\n"
		<< "</div>\n"

		// Component Showcase: Tables & Data
		<< "<h2>Framework Component Gallery</h2>\n"
		<< "<div class=\"wf-card\">\n"
		<< "  <div class=\"wf-card-header\">\n"
		<< "    <span>" << VividIcon::Get("dashboard", 16) << " Framework Component Gallery</span>\n"
		<< "    <span class=\"wf-badge wf-badge-cyan\">Standard Widgets</span>\n"
		<< "  </div>\n"
		<< "  <div class=\"wf-card-body\">\n"
		<< "    <table class=\"wf-table\">\n"
		<< "      <thead>\n"
		<< "        <tr><th>Module</th><th>Layout Template</th><th>Tree Dock</th><th>Density</th><th>Theme</th><th>SweAgent Access</th></tr>\n"
		<< "      </thead>\n"
		<< "      <tbody>\n"
		<< "        <tr><td><strong>Repository Hub</strong></td><td>MegaMenuTemplate</td><td>Dual Dock (Nav + TOC)</td><td>Comfortable</td><td>Vivid Cyan Dark</td><td><span class=\"wf-badge wf-badge-emerald\">JSON / DOM</span></td></tr>\n"
		<< "        <tr><td><strong>Developer Dashboard</strong></td><td>DashboardTemplate</td><td>Left Dock</td><td>Compact</td><td>Vivid Dark / Light</td><td><span class=\"wf-badge wf-badge-emerald\">Available</span></td></tr>\n"
		<< "        <tr><td><strong>Pair Programmer Chat</strong></td><td>AppWorkspaceTemplate</td><td>Dual Dock (Files + TOC)</td><td>Dense (Zero-bleed)</td><td>Vivid Dark</td><td><span class=\"wf-badge wf-badge-emerald\">SSE Stream</span></td></tr>\n"
		<< "        <tr><td><strong>Code Diff Inspector</strong></td><td>AppWorkspaceTemplate</td><td>Left Tree</td><td>Dense</td><td>Vivid Dark</td><td><span class=\"wf-badge wf-badge-cyan\">Custom Viewer</span></td></tr>\n"
		<< "      </tbody>\n"
		<< "    </table>\n"
		<< "  </div>\n"
		<< "</div>\n";

	TreeModel nav_tree = TreeGenerators::FromNavigation(nav);

	String html = PageBuilder()
		.Title("WebFramework Showcase - Mega-Menu & Tree View Navigator")
		.Brand("WebFramework", "/", "vivid")
		.Navigation(nav)
		.Template("megamenu")
		.Theme("vivid-dark")
		.Density(DENSITY_COMFORTABLE)
		.LeftTree(nav_tree, "Navigation Tree", "wf-nav-tree")
		.AutoOutline(true)
		.TrustedContent(content)
		.TrustedFooter("&copy; 2026 Ultimate++ Agentic Web Framework. Powered by Skylark &amp; Vivid Plasma Design.")
		.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

SKYLARK(ShowcaseTabs, "showcase")
{
	String tab = http["tab"];
	if(tab.IsEmpty()) tab = "repos";
	TrackVisitor(http, "/showcase?tab=" + tab);

	NavigationModel nav = BuildShowcaseNavigation();
	String title = "WebFramework Showcase";
	String content;

	if(tab == "repos") {
		nav.SetActive("development", "repo-ai-upp");
		title = "Active Repositories - WebFramework";
		content
			<< "<h1>Workspace Repositories</h1>\n"
			<< "<div class=\"wf-alert wf-alert-info\">"
			<< "  " << VividIcon::Get("folder", 18)
			<< "  <div>Three active repositories in this workspace. Multi-repository management powered by Skylark &amp; SweAgent.</div>"
			<< "</div>\n"
			<< "<div class=\"wf-grid wf-grid-3\">\n"
			// Card 1: ai-upp
			<< "  <div class=\"wf-card\">\n"
			<< "    <div class=\"wf-card-header\">\n"
			<< "      <span>" << VividIcon::Get("code", 16) << " ai-upp</span>\n"
			<< "      <span class=\"wf-badge wf-badge-emerald\">Active</span>\n"
			<< "    </div>\n"
			<< "    <div class=\"wf-card-body\">\n"
			<< "      <p style=\"margin-bottom:12px;\">Ultimate++ C++ engine fork with WebFramework, WebChat SSE streaming, and Skylark micro-services.</p>\n"
			<< "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:12px;\">\n"
			<< "        <strong>Branch:</strong> master<br>\n"
			<< "        <strong>Build:</strong> bin/build -m CLANG<br>\n"
			<< "        <strong>Packages:</strong> WebFramework, ManagerSkylark, WebShowcase\n"
			<< "      </div>\n"
			<< "      <div style=\"display:flex; gap:8px;\">\n"
			<< "        <a href=\"/showcase?tab=branches\" class=\"wf-btn wf-btn-primary\">Branches</a>\n"
			<< "        <a href=\"/showcase?tab=diff\" class=\"wf-btn wf-btn-secondary\">Diff</a>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			// Card 2: Manager Skylark
			<< "  <div class=\"wf-card\">\n"
			<< "    <div class=\"wf-card-header\">\n"
			<< "      <span>" << VividIcon::Get("dashboard", 16) << " Manager Skylark</span>\n"
			<< "      <span class=\"wf-badge wf-badge-cyan\">Daemon</span>\n"
			<< "    </div>\n"
			<< "    <div class=\"wf-card-body\">\n"
			<< "      <p style=\"margin-bottom:12px;\">Planner-Worker loop tracking phases, tasks, and agents across ports 8080 and 8081.</p>\n"
			<< "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:12px;\">\n"
			<< "        <strong>Status:</strong> HTTP 200 OK<br>\n"
			<< "        <strong>Phases:</strong> 5 Completed / Active<br>\n"
			<< "        <strong>Active Port:</strong> 8080 (Skylark)\n"
			<< "      </div>\n"
			<< "      <div style=\"display:flex; gap:8px;\">\n"
			<< "        <a href=\"/showcase?tab=commits\" class=\"wf-btn wf-btn-primary\">Commits</a>\n"
			<< "        <a href=\"/workspace\" class=\"wf-btn wf-btn-secondary\">Workspace</a>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			// Card 3: mini-swe-agent
			<< "  <div class=\"wf-card\">\n"
			<< "    <div class=\"wf-card-header\">\n"
			<< "      <span>" << VividIcon::Get("agent", 16) << " mini-swe-agent</span>\n"
			<< "      <span class=\"wf-badge wf-badge-purple\">Python 3.14</span>\n"
			<< "    </div>\n"
			<< "    <div class=\"wf-card-body\">\n"
			<< "      <p style=\"margin-bottom:12px;\">Autonomous SWE agent benchmark harness with OpenRouter integration &amp; directory inspection.</p>\n"
			<< "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:12px;\">\n"
			<< "        <strong>Model:</strong> Nemotron / Llama-3<br>\n"
			<< "        <strong>Self-Tests:</strong> 11/11 Passed<br>\n"
			<< "        <strong>Environment:</strong> venv / SweAgent1\n"
			<< "      </div>\n"
			<< "      <div style=\"display:flex; gap:8px;\">\n"
			<< "        <a href=\"/showcase?tab=benchmark\" class=\"wf-btn wf-btn-primary\">Benchmarks</a>\n"
			<< "        <a href=\"/showcase?tab=repl\" class=\"wf-btn wf-btn-secondary\">Agent REPL</a>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(tab == "branches") {
		nav.SetActive("development", "git-branches");
		title = "Git Branch Manager - WebFramework";
		content
			<< "<h1>Git Branch Manager</h1>\n"
			<< "<div class=\"wf-alert wf-alert-info\">Listing git branches in local workspace repository.</div>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Active Branches</span><span class=\"wf-badge wf-badge-cyan\">git branch -a</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>Branch</th><th>Status</th><th>Last Commit</th><th>Author</th><th>Actions</th></tr></thead>\n"
			<< "      <tbody>\n"
			<< "        <tr><td><strong>master</strong></td><td><span class=\"wf-badge wf-badge-emerald\">Default / Current</span></td><td>Add WebFramework, Density Engine, Vivid Themes</td><td>Agent Pair</td><td><a href=\"/showcase?tab=diff\" class=\"wf-btn wf-btn-secondary\">Inspect</a></td></tr>\n"
			<< "        <tr><td>feat/webframework</td><td><span class=\"wf-badge wf-badge-cyan\">Merged</span></td><td>Decouple templates and density controls</td><td>Agent Pair</td><td><a href=\"/showcase?tab=diff\" class=\"wf-btn wf-btn-secondary\">Inspect</a></td></tr>\n"
			<< "        <tr><td>fix/light-theme</td><td><span class=\"wf-badge wf-badge-purple\">Active</span></td><td>Add Vivid Mixed theme and fix light text colors</td><td>Agent Pair</td><td><a href=\"/showcase?tab=diff\" class=\"wf-btn wf-btn-secondary\">Inspect</a></td></tr>\n"
			<< "      </tbody>\n"
			<< "    </table>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(tab == "commits") {
		nav.SetActive("development", "git-commits");
		title = "Commit Log - WebFramework";
		content
			<< "<h1>Commit Timeline</h1>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Recent Commits</span><span class=\"wf-badge wf-badge-cyan\">git log -n 5</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>Hash</th><th>Subject</th><th>Author</th><th>Date</th><th>Diff</th></tr></thead>\n"
			<< "      <tbody>\n"
			<< "        <tr><td><code>2a2549e</code></td><td>Add WebFramework, Density Engine, Vivid Themes &amp; Dual Tree Navigator</td><td>Agent Pair</td><td>Just now</td><td><a href=\"/showcase?tab=diff\" class=\"wf-btn wf-btn-secondary\">Diff</a></td></tr>\n"
			<< "        <tr><td><code>b37d891</code></td><td>Add tidy-markdown parser and custom diff viewer registry to WebChat</td><td>Agent Pair</td><td>Earlier</td><td><a href=\"/showcase?tab=diff\" class=\"wf-btn wf-btn-secondary\">Diff</a></td></tr>\n"
			<< "        <tr><td><code>e41f029</code></td><td>Implement numbered pagination bar and sliding live window</td><td>Agent Pair</td><td>Earlier</td><td><a href=\"/showcase?tab=diff\" class=\"wf-btn wf-btn-secondary\">Diff</a></td></tr>\n"
			<< "      </tbody>\n"
			<< "    </table>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(tab == "diff") {
		nav.SetActive("development", "git-diff");
		title = "Diff Inspector - WebFramework";
		content
			<< "<h1>Unified Diff Inspector</h1>\n"
			<< "<div class=\"wf-alert wf-alert-info\">Showing syntax-colored unified code diff for WebFramework Theme implementation:</div>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\">\n"
			<< "    <span>" << VividIcon::Get("git-diff", 16) << " diff --git a/uppsrc/WebFramework/Theme.cpp b/uppsrc/WebFramework/Theme.cpp</span>\n"
			<< "    <span class=\"wf-badge wf-badge-cyan\">Vivid Mixed Theme Added</span>\n"
			<< "  </div>\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <div style=\"background:var(--wf-bg-view,#151d2b); color:var(--wf-fg-normal,#d3dae3); padding:1rem; font-family:monospace; font-size:0.86rem; overflow-x:auto; line-height:1.4;\">\n"
			<< "      <div style=\"color:#64748b; padding:1px 0;\">--- a/uppsrc/WebFramework/Theme.cpp</div>\n"
			<< "      <div style=\"color:#64748b; padding:1px 0;\">+++ b/uppsrc/WebFramework/Theme.cpp</div>\n"
			<< "      <div style=\"color:#00a1c5; padding:1px 0;\">@@ -124,6 +124,30 @@ WebTheme ThemeRegistry::CreateVividLight()</div>\n"
			<< "      <div style=\"color:#00aa7f; background:rgba(0,170,127,0.12); padding:1px 4px; border-left:3px solid #00aa7f;\">+WebTheme ThemeRegistry::CreateVividMixed()</div>\n"
			<< "      <div style=\"color:#00aa7f; background:rgba(0,170,127,0.12); padding:1px 4px; border-left:3px solid #00aa7f;\">+{</div>\n"
			<< "      <div style=\"color:#00aa7f; background:rgba(0,170,127,0.12); padding:1px 4px; border-left:3px solid #00aa7f;\">+  WebTheme t(&quot;vivid-mixed&quot;, &quot;Vivid Mixed (KDE Plasma Twilight)&quot;, false);</div>\n"
			<< "      <div style=\"color:#00aa7f; background:rgba(0,170,127,0.12); padding:1px 4px; border-left:3px solid #00aa7f;\">+  t.colors.header_bg = &quot;rgba(24, 32, 48, 0.94)&quot;; // Dark frosted glass header</div>\n"
			<< "      <div style=\"color:#00aa7f; background:rgba(0,170,127,0.12); padding:1px 4px; border-left:3px solid #00aa7f;\">+  t.colors.header_fg = &quot;#f1f5f9&quot;; // Crisp light text</div>\n"
			<< "      <div style=\"color:#00aa7f; background:rgba(0,170,127,0.12); padding:1px 4px; border-left:3px solid #00aa7f;\">+  t.colors.bg_normal = &quot;#f4f6fa&quot;; // Luminous light document background</div>\n"
			<< "      <div style=\"color:#00aa7f; background:rgba(0,170,127,0.12); padding:1px 4px; border-left:3px solid #00aa7f;\">+  return t;</div>\n"
			<< "      <div style=\"color:#00aa7f; background:rgba(0,170,127,0.12); padding:1px 4px; border-left:3px solid #00aa7f;\">+}</div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(tab == "chat") {
		nav.SetActive("agent", "agent-chat");
		title = "Pair Programming Chat - WebFramework";
		content
			<< "<h1>Pair Programming WebChat</h1>\n"
			<< "<div class=\"wf-alert wf-alert-info\">WebChat module with SSE streaming, tidyMarkdown parser, and custom diff viewers is live on port 8080.</div>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>WebChat Live Module</span><span class=\"wf-badge wf-badge-emerald\">SSE Stream</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p style=\"margin-bottom:1rem;\">The pair programming chat interface features server-sent token streaming, numbered pagination bar with sliding live window, message bubble action controls (Copy, Fork, Revert, Retry, Delete), and custom viewers.</p>\n"
			<< "    <a href=\"http://127.0.0.1:8080/chat\" target=\"_blank\" class=\"wf-btn wf-btn-primary\">Open Live Chat on Port 8080</a>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(tab == "repl") {
		nav.SetActive("agent", "agent-repl");
		title = "SweAgent REPL Console - WebFramework";
		content
			<< "<h1>SweAgent REPL Console</h1>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Interactive Console</span><span class=\"wf-badge wf-badge-cyan\">Terminal</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div style=\"background:#0b1120; border:1px solid #334155; border-radius:6px; padding:1rem; font-family:monospace; font-size:0.85rem; color:#38bdf8; min-height:180px;\">\n"
			<< "      [SweAgent REPL] Ready. Connected to OpenRouter Nemotron.<br>\n"
			<< "      [SweAgent REPL] 11 self-tests passed.<br>\n"
			<< "      &gt; ./bin/CLANG/SweAgent1 --session pair-programmer<br>\n"
			<< "      Awaiting prompt...<br>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(tab == "benchmark") {
		nav.SetActive("agent", "swebench");
		title = "SWE-bench Benchmarks - WebFramework";
		content
			<< "<h1>SWE-bench Benchmarks</h1>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Benchmark Results</span><span class=\"wf-badge wf-badge-emerald\">100% Passed</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>Benchmark ID</th><th>Repository</th><th>Duration</th><th>Status</th></tr></thead>\n"
			<< "      <tbody>\n"
			<< "        <tr><td><code>django__django-11099</code></td><td>django/django</td><td>42s</td><td><span class=\"wf-badge wf-badge-emerald\">Resolved</span></td></tr>\n"
			<< "        <tr><td><code>sympy__sympy-13480</code></td><td>sympy/sympy</td><td>31s</td><td><span class=\"wf-badge wf-badge-emerald\">Resolved</span></td></tr>\n"
			<< "        <tr><td><code>scikit-learn__scikit-10949</code></td><td>scikit-learn</td><td>54s</td><td><span class=\"wf-badge wf-badge-emerald\">Resolved</span></td></tr>\n"
			<< "      </tbody>\n"
			<< "    </table>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(tab == "inspector") {
		nav.SetActive("agent", "inspector");
		title = "Trajectory Inspector - WebFramework";
		content
			<< "<h1>Agent Trajectory Inspector</h1>\n"
			<< "<div class=\"wf-alert wf-alert-info\">Inspecting active agent decision loops and tool call transcripts.</div>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Step 1: Code Search</span><span class=\"wf-badge wf-badge-cyan\">Tool Call</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p>Searched repository for DensityConfig and ThemeRegistry definitions.</p>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else {
		nav.SetActive("development", "repo-ai-upp");
		content << "<h1>Showcase Tab: " << tab << "</h1><p>Tab content loaded successfully.</p>";
	}

	TreeModel nav_tree = TreeGenerators::FromNavigation(nav);

	String html = PageBuilder()
		.Title(title)
		.Brand("WebFramework", "/", "vivid")
		.Navigation(nav)
		.Template("megamenu")
		.Theme("vivid-dark")
		.Density(DENSITY_COMFORTABLE)
		.LeftTree(nav_tree, "Navigation Tree", "wf-nav-tree")
		.AutoOutline(true)
		.TrustedContent(content)
		.TrustedFooter("&copy; 2026 Ultimate++ Agentic Web Framework. Powered by Skylark &amp; Vivid Plasma Design.")
		.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

SKYLARK(ShowcaseDashboard, "dashboard")
{
	TrackVisitor(http, "/dashboard");
	NavigationModel nav = BuildShowcaseNavigation();
	nav.SetActive("framework", "tmpl-dash");

	String sidebar;
	sidebar << "<div style=\"font-size:0.75rem; font-weight:700; color:var(--wf-accent-focus,#00a1c5); text-transform:uppercase; margin-bottom:8px;\">Dashboard Dock</div>\n"
	        << "<a href=\"/\" class=\"wf-mega-cat-pill\">" << VividIcon::Get("home", 16) << " Overview</a>\n"
	        << "<a href=\"/dashboard\" class=\"wf-mega-cat-pill active\">" << VividIcon::Get("dashboard", 16) << " Metrics</a>\n"
	        << "<a href=\"/workspace\" class=\"wf-mega-cat-pill\">" << VividIcon::Get("code", 16) << " Workspace</a>\n"
	        << "<a href=\"/telemetry\" class=\"wf-mega-cat-pill\">" << VividIcon::Get("dashboard", 16) << " Telemetry</a>\n"
	        << "<div style=\"margin-top:1.5rem; font-size:0.75rem; font-weight:700; color:var(--wf-accent-focus,#00a1c5); text-transform:uppercase; margin-bottom:8px;\">Actions</div>\n"
	        << "<button class=\"wf-btn wf-btn-primary\" style=\"width:100%;\" onclick=\"WebAudio.play('notify')\">Run Audit</button>\n";

	String content;
	content << "<div class=\"wf-alert wf-alert-success\"><strong>Dashboard Layout Active:</strong> Showing 2-column sidebar dock template with integrated telemetry cards.</div>\n"
	        << "<div class=\"wf-grid wf-grid-3\">\n"
	        << "  <div class=\"wf-card\"><div class=\"wf-card-header\">Repositories</div><div class=\"wf-card-body\"><h2 style=\"color:var(--wf-accent-focus,#00a1c5);\">3 Active</h2><p style=\"font-size:0.8rem;color:var(--wf-fg-inactive,#8b919d);\">ai-upp, Manager, mini-swe-agent</p></div></div>\n"
	        << "  <div class=\"wf-card\"><div class=\"wf-card-header\">AI Sessions</div><div class=\"wf-card-body\"><h2 style=\"color:var(--wf-color-positive,#00aa7f);\">6 Sessions</h2><p style=\"font-size:0.8rem;color:var(--wf-fg-inactive,#8b919d);\">OpenRouter Nemotron</p></div></div>\n"
	        << "  <div class=\"wf-card\"><div class=\"wf-card-header\">Benchmarks</div><div class=\"wf-card-body\"><h2 style=\"color:var(--wf-color-neutral,#6d55aa);\">SWE-bench</h2><p style=\"font-size:0.8rem;color:var(--wf-fg-inactive,#8b919d);\">100% Passed</p></div></div>\n"
	        << "</div>\n";

	String html = PageBuilder()
		.Title("Dashboard Template - WebFramework Showcase")
		.Brand("WebFramework", "/", "vivid")
		.Navigation(nav)
		.Template("dashboard")
		.Theme("vivid-dark")
		.Density(DENSITY_COMPACT)
		.TrustedSidebar(sidebar)
		.TrustedContent(content)
		.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

SKYLARK(ShowcaseWorkspace, "workspace")
{
	TrackVisitor(http, "/workspace");
	NavigationModel nav = BuildShowcaseNavigation();
	nav.SetActive("framework", "tmpl-work");

	// Automatic workspace tree from uppsrc/WebFramework directory
	String fw_dir = "/home/sblo/Dev/ai-upp/uppsrc/WebFramework";
	TreeModel file_tree = TreeGenerators::FromDirectory(fw_dir, 3);

	String content;
	content
		<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1rem;\">\n"
		<< "  <div>\n"
		<< "    <h1 style=\"font-size:1.4rem; font-weight:700;\">WebFramework Workspace Explorer</h1>\n"
		<< "    <div style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d);\">Click any file in the left tree navigator to inspect source code with line numbers.</div>\n"
		<< "  </div>\n"
		<< "  <div style=\"display:flex; gap:8px;\">\n"
		<< "    <a href=\"/showcase?tab=diff\" class=\"wf-btn wf-btn-secondary\">" << VividIcon::Get("git-diff", 14) << " Diff</a>\n"
		<< "    <a href=\"/api/framework/tree\" class=\"wf-btn wf-btn-secondary\" target=\"_blank\">" << VividIcon::Get("code", 14) << " Tree JSON</a>\n"
		<< "  </div>\n"
		<< "</div>\n"

		// Code Viewer Card
		<< "<div class=\"wf-card wf-code-box\" id=\"workspace-code-box\">\n"
		<< "  <div class=\"wf-card-header wf-code-box-header\">\n"
		<< "    <div style=\"display:flex; align-items:center; gap:8px;\">\n"
		<< "      " << VividIcon::Get("file", 16) << "\n"
		<< "      <span id=\"workspace-file-title\" style=\"font-weight:700; color:var(--wf-fg-normal,#ffffff);\">Select a file</span>\n"
		<< "      <span id=\"workspace-file-stats\" class=\"wf-badge wf-badge-cyan\">0 lines</span>\n"
		<< "    </div>\n"
		<< "    <div style=\"display:flex; gap:6px;\">\n"
		<< "      <button class=\"wf-btn wf-btn-secondary\" style=\"padding:3px 8px; font-size:0.75rem;\" onclick=\"navigator.clipboard.writeText(document.getElementById('workspace-file-content').innerText); if(window.WebAudio) WebAudio.play('click');\">Copy</button>\n"
		<< "    </div>\n"
		<< "  </div>\n"
		<< "  <div class=\"wf-code-box-body\" id=\"workspace-file-content\">\n"
		<< "    <div style=\"padding:1.5rem; text-align:center; color:var(--wf-fg-inactive,#8b919d);\">Loading workspace file...</div>\n"
		<< "  </div>\n"
		<< "</div>\n"

		// Dynamic file loader script
		<< "<script>\n"
		<< "function showWorkspaceFileMessage(viewer, text, isError) {\n"
		<< "  let message = document.createElement('div');\n"
		<< "  message.style.padding = '1.5rem';\n"
		<< "  message.style.color = isError ? 'var(--wf-color-negative,#c83771)' : 'var(--wf-fg-inactive,#8b919d)';\n"
		<< "  message.textContent = text;\n"
		<< "  viewer.replaceChildren(message);\n"
		<< "}\n"
		<< "function loadWorkspaceFile(relPath) {\n"
		<< "  let viewer = document.getElementById('workspace-file-content');\n"
		<< "  let titleEl = document.getElementById('workspace-file-title');\n"
		<< "  let statsEl = document.getElementById('workspace-file-stats');\n"
		<< "  if(!viewer) return;\n"
		<< "  showWorkspaceFileMessage(viewer, 'Loading ' + relPath + '...', false);\n"
		<< "  fetch('/api/workspace/file?path=' + encodeURIComponent(relPath))\n"
		<< "    .then(r => r.json())\n"
		<< "    .then(data => {\n"
		<< "      if(data.status === 'ok') {\n"
		<< "        if(titleEl) titleEl.innerText = data.path;\n"
		<< "        if(statsEl) statsEl.innerText = data.lines + ' lines';\n"
		<< "        let lines = data.content.split('\\n');\n"
		<< "        let fragment = document.createDocumentFragment();\n"
		<< "        for(let i = 0; i < lines.length; i++) {\n"
		<< "          let row = document.createElement('div');\n"
		<< "          row.className = 'wf-code-row';\n"
		<< "          let number = document.createElement('span');\n"
		<< "          number.className = 'wf-code-num';\n"
		<< "          number.textContent = String(i + 1);\n"
		<< "          let text = document.createElement('span');\n"
		<< "          text.className = 'wf-code-text';\n"
		<< "          text.textContent = lines[i] || ' ';\n"
		<< "          row.append(number, text);\n"
		<< "          fragment.appendChild(row);\n"
		<< "        }\n"
		<< "        viewer.replaceChildren(fragment);\n"
		<< "        if(window.WebAudio) WebAudio.play('click');\n"
		<< "      } else {\n"
		<< "        showWorkspaceFileMessage(viewer, String(data.message || 'File could not be loaded.'), true);\n"
		<< "      }\n"
		<< "    })\n"
		<< "    .catch(err => {\n"
		<< "      showWorkspaceFileMessage(viewer, 'Error loading file: ' + String(err), true);\n"
		<< "    });\n"
		<< "}\n"
		<< "function checkHash() {\n"
		<< "  let h = window.location.hash;\n"
		<< "  if(h.startsWith('#file/')) {\n"
		<< "    try { loadWorkspaceFile(decodeURIComponent(h.substring(6))); }\n"
		<< "    catch(error) { loadWorkspaceFile(''); }\n"
		<< "  } else {\n"
		<< "    loadWorkspaceFile('Icons.cpp');\n"
		<< "  }\n"
		<< "}\n"
		<< "window.addEventListener('hashchange', checkHash);\n"
		<< "if(document.readyState === 'loading') document.addEventListener('DOMContentLoaded', checkHash);\n"
		<< "else checkHash();\n"
		<< "</script>\n";

	String extra_css =
		"<style>\n"
		"  .wf-code-box { background: var(--wf-bg-view, #151d2b); border: 1px solid var(--wf-border-color, rgba(0,161,197,0.2)); border-radius: var(--wf-radius, 8px); overflow: hidden; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; font-size: 0.85rem; }\n"
		"  .wf-code-box-header { padding: 8px 14px; background: var(--wf-bg-alt, #1c202f); border-bottom: 1px solid var(--wf-border-color, rgba(255,255,255,0.08)); display: flex; justify-content: space-between; align-items: center; }\n"
		"  .wf-code-box-body { overflow-x: auto; max-height: calc(100vh - 200px); background: var(--wf-bg-view, #151d2b); }\n"
		"  .wf-code-row { display: flex; line-height: 1.45; }\n"
		"  .wf-code-row:hover { background: var(--wf-surface-hover, rgba(0,161,197,0.08)); }\n"
		"  .wf-code-num { width: 55px; padding: 1px 12px; text-align: right; color: var(--wf-fg-inactive, #64748b); user-select: none; border-right: 1px solid var(--wf-border-color, rgba(255,255,255,0.06)); background: var(--wf-bg-alt, #1c202f); flex-shrink: 0; font-size: 0.78rem; }\n"
		"  .wf-code-text { padding: 1px 12px; white-space: pre; color: var(--wf-fg-normal, #d3dae3); flex: 1; }\n"
		"</style>\n";

	String html = PageBuilder()
		.Title("App Workspace - Dual Tree Navigator Showcase")
		.Brand("WebFramework", "/", "vivid")
		.Navigation(nav)
		.Template("app-workspace")
		.Theme("vivid-dark")
		.Density(DENSITY_NONE)
		.LeftTree(file_tree, "Workspace Files", "wf-workspace-tree")
		.AutoOutline(true)
		.TrustedExtraHead(extra_css)
		.TrustedContent(content)
		.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

SKYLARK(ApiWorkspaceFile, "api/workspace/file")
{
	String rel_path = http["path"];
	if(rel_path.IsEmpty() || rel_path.Find("..") >= 0 || rel_path.StartsWith("/") || rel_path.StartsWith("\\")) {
		ValueMap err;
		err.Add("status", "error");
		err.Add("message", "Invalid file path");
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}

	String base_dir = "/home/sblo/Dev/ai-upp/uppsrc/WebFramework";
	String full_path = AppendFileName(base_dir, rel_path);

	if(!FileExists(full_path)) {
		ValueMap err;
		err.Add("status", "error");
		err.Add("message", "File not found: " + rel_path);
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}

	String text = LoadFile(full_path);
	Vector<String> lines = Split(text, '\n', false);

	ValueMap res;
	res.Add("status", "ok");
	res.Add("path", rel_path);
	res.Add("filename", GetFileName(rel_path));
	res.Add("lines", lines.GetCount());
	res.Add("content", text);

	http.ContentType("application/json; charset=utf-8") << AsJSON(res, true);
}

SKYLARK(ShowcaseTelemetry, "telemetry")
{
	String cur_uid = TrackVisitor(http, "/telemetry");
	NavigationModel nav = BuildShowcaseNavigation();
	nav.SetActive("system", "sys-telemetry");

	int visitors_count = s_visitors().GetCount();
	int total_views = s_total_page_views();
	double avg_pages = visitors_count > 0 ? (double)total_views / visitors_count : 0.0;

	String content;
	content
		<< "<h1>Anonymous Visitor Telemetry</h1>\n"
		<< "<div class=\"wf-alert wf-alert-info\">"
		<< "  " << VividIcon::Get("dashboard", 18)
		<< "  <div><strong>Cookie-Based Tracking:</strong> Anonymous, persistent cookie-based visitor session tracking (zero third-party trackers, zero external scripts).</div>"
		<< "</div>\n"

		<< "<div class=\"wf-grid wf-grid-3\">\n"
		<< "  <div class=\"wf-card\">\n"
		<< "    <div class=\"wf-card-header\"><span>Unique Visitors</span><span class=\"wf-badge wf-badge-emerald\">Cookies</span></div>\n"
		<< "    <div class=\"wf-card-body\"><h2 style=\"color:var(--wf-accent-focus,#00a1c5);\">" << visitors_count << "</h2><p style=\"font-size:0.8rem;color:var(--wf-fg-inactive,#8b919d);\">Anonymous unique IDs</p></div>\n"
		<< "  </div>\n"
		<< "  <div class=\"wf-card\">\n"
		<< "    <div class=\"wf-card-header\"><span>Total Page Views</span><span class=\"wf-badge wf-badge-cyan\">Requests</span></div>\n"
		<< "    <div class=\"wf-card-body\"><h2 style=\"color:var(--wf-color-positive,#00aa7f);\">" << total_views << "</h2><p style=\"font-size:0.8rem;color:var(--wf-fg-inactive,#8b919d);\">Recorded page requests</p></div>\n"
		<< "  </div>\n"
		<< "  <div class=\"wf-card\">\n"
		<< "    <div class=\"wf-card-header\"><span>Avg Pages / Visitor</span><span class=\"wf-badge wf-badge-purple\">Engagement</span></div>\n"
		<< "    <div class=\"wf-card-body\"><h2 style=\"color:var(--wf-color-neutral,#6d55aa);\">" << Format("%.1f", avg_pages) << "</h2><p style=\"font-size:0.8rem;color:var(--wf-fg-inactive,#8b919d);\">Pages per session</p></div>\n"
		<< "  </div>\n"
		<< "</div>\n"

		<< "<h2 style=\"margin-top:1.5rem; margin-bottom:0.75rem;\">Recent Visitor Sessions</h2>\n"
		<< "<div class=\"wf-card\">\n"
		<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
		<< "    <table class=\"wf-table\">\n"
		<< "      <thead><tr><th>Visitor Cookie ID</th><th>Page Views</th><th>First Seen</th><th>Last Seen</th><th>Recent Paths</th></tr></thead>\n"
		<< "      <tbody>\n";

	for(int i = s_visitors().GetCount() - 1; i >= 0 && i >= s_visitors().GetCount() - 15; i--) {
		const TelemetryVisitor& v = s_visitors()[i];
		bool is_you = (v.uid == cur_uid);
		content << "        <tr>"
		        << "<td><code>" << v.uid << "</code> " << (is_you ? "<span class=\"wf-badge wf-badge-emerald\">You</span>" : "") << "</td>"
		        << "<td>" << v.page_views << "</td>"
		        << "<td>" << Format(v.first_seen) << "</td>"
		        << "<td>" << Format(v.last_seen) << "</td>"
		        << "<td><code>" << Join(v.paths, " &rarr; ") << "</code></td>"
		        << "</tr>\n";
	}

	content
		<< "      </tbody>\n"
		<< "    </table>\n"
		<< "  </div>\n"
		<< "</div>\n";

	TreeModel nav_tree = TreeGenerators::FromNavigation(nav);

	String html = PageBuilder()
		.Title("Visitor Telemetry - WebFramework")
		.Brand("WebFramework", "/", "vivid")
		.Navigation(nav)
		.Template("megamenu")
		.Theme("vivid-dark")
		.Density(DENSITY_COMFORTABLE)
		.LeftTree(nav_tree, "Navigation Tree", "wf-nav-tree")
		.AutoOutline(true)
		.TrustedContent(content)
		.TrustedFooter("&copy; 2026 Ultimate++ Agentic Web Framework. Powered by Skylark &amp; Vivid Plasma Design.")
		.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

SKYLARK(ApiTelemetry, "api/telemetry")
{
	TrackVisitor(http, "/api/telemetry");
	ValueMap root;
	root.Add("status", "ok");
	root.Add("unique_visitors", s_visitors().GetCount());
	root.Add("total_page_views", s_total_page_views());
	ValueArray visitors_arr;
	for(int i = 0; i < s_visitors().GetCount(); i++) {
		const TelemetryVisitor& v = s_visitors()[i];
		ValueMap vm;
		vm.Add("uid", v.uid);
		vm.Add("page_views", v.page_views);
		vm.Add("first_seen", Format(v.first_seen));
		vm.Add("last_seen", Format(v.last_seen));
		ValueArray paths_arr;
		for(int p = 0; p < v.paths.GetCount(); p++)
			paths_arr.Add(v.paths[p]);
		vm.Add("paths", paths_arr);
		visitors_arr.Add(vm);
	}
	root.Add("visitors", visitors_arr);
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiFrameworkTree, "api/framework/tree")
{
	NavigationModel nav = BuildShowcaseNavigation();
	TreeModel tree = TreeGenerators::FromNavigation(nav);

	ValueMap root;
	root.Add("framework", "WebFramework");
	root.Add("type", "navigation_tree");
	root.Add("roots_count", tree.GetRoots().GetCount());
	root.Add("tree", tree.ToValue());

	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiFrameworkSpec, "api/framework/spec")
{
	NavigationModel nav = BuildShowcaseNavigation();

	ValueMap root;
	root.Add("framework", "WebFramework");
	root.Add("version", "1.0.0");
	root.Add("themes", ValueArray({ "vivid-dark", "vivid-light", "vivid-mixed" }));
	root.Add("templates", ValueArray({ "megamenu", "dashboard", "app-workspace" }));
	root.Add("densities", ValueArray({ "dense", "compact", "comfortable", "spacious" }));
	root.Add("navigation", ParseJSON(nav.ToJSON()));

	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiHealthCheck, "api/health")
{
	ValueMap m;
	m.Add("status", "ok");
	m.Add("app", "WebShowcase");
	m.Add("framework", "WebFramework");
	m.Add("vivid_themes", true);
	m.Add("version", "1.0.0");
	http.ContentType("application/json") << AsJSON(m, true);
}

WebShowcaseApp::WebShowcaseApp()
{
	port = 8081;
	root = "";
}

END_UPP_NAMESPACE

using namespace Upp;

CONSOLE_APP_MAIN
{
	StdLogSetup(LOG_COUT | LOG_FILE);
	const Vector<String>& args = CommandLine();

	WebShowcaseApp app;
	for(int i = 0; i < args.GetCount(); i++) {
		if((args[i] == "-p" || args[i] == "--port") && i + 1 < args.GetCount()) {
			int p = ScanInt(args[i + 1]);
			if(p > 0 && p < 65536)
				app.SetPort(p);
		}
	}

	Cout() << "WebShowcase starting on http://127.0.0.1:" << app.GetPort() << "\n";
	app.Run();
}
