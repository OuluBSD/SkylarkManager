#include "HostPlatform.h"
#include <sys/stat.h>
#ifdef PLATFORM_POSIX
#include <sys/statvfs.h>
#endif

NAMESPACE_UPP

static String SafeHostPath(const String& path)
{
	if(path.IsEmpty() || path[0] != '/' || path.StartsWith("//"))
		return String();
	String out;
	Vector<String> parts = Split(path, '/');
	for(int i = 0; i < parts.GetCount(); i++) {
		if(parts[i].IsEmpty())
			continue;
		if(!out.IsEmpty()) out << "/";
		out << UrlEncode(parts[i]);
	}
	return out.IsEmpty() ? String() : "/" + out;
}

static String NodeRoute(const String& node_id, const String& suffix)
{
	return SafeHostPath("/node/" + node_id + "/" + suffix);
}

Vector<HostRepoInfo> HostPlatformService::GetLocalRepositories(const String& dev_dir)
{
	(void)dev_dir;
	return Vector<HostRepoInfo>();
}

Vector<DistributedNode> HostPlatformService::GetConfiguredNodes(bool probe_all)
{
	Vector<DistributedNode> nodes;
	String conf_path = AppendFileName(GetHomeDirFile(".config/managerskylark"), "nodes.json");
	if(FileExists(conf_path)) {
		Value v = ParseJSON(LoadFile(conf_path));
		if(v.Is<ValueArray>()) {
			ValueArray va = v;
			for(int i = 0; i < va.GetCount(); i++) {
				DistributedNode node;
				LoadFromJsonValue(node, va[i]);
				nodes.Add(node);
			}
		}
	}
	(void)probe_all;

	return nodes;
}

DistributedNode HostPlatformService::GetNode(const String& node_id)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].id == node_id || nodes[i].hostname == node_id)
			return nodes[i];
	}
	DistributedNode unavailable;
	unavailable.id = node_id;
	unavailable.status = "unconfigured";
	return unavailable;
}

Vector<HostRepoInfo> HostPlatformService::GetRepositoriesForNode(const String& node_id)
{
	DistributedNode node = GetNode(node_id);
	if(node.is_local || node.protocol == "local") {
		return GetLocalRepositories(node.remote_path);
	}
	(void)node_id;
	return Vector<HostRepoInfo>();
}

void HostPlatformService::SaveNodes(const Vector<DistributedNode>& nodes)
{
	String dir = GetHomeDirFile(".config/managerskylark");
	RealizeDirectory(dir);
	String path = AppendFileName(dir, "nodes.json");

	ValueArray va;
	for(int i = 0; i < nodes.GetCount(); i++)
		va.Add(StoreAsJsonValue(nodes[i]));

	SaveFile(path, AsJSON(va, true));
}

bool HostPlatformService::ProbeNode(DistributedNode& node)
{
	node.last_checked = GetSysTime();

	if(node.is_local || node.protocol == "local") {
		node.is_local = true;
		node.latency_ms = 0;
		node.status = "online";

		if(FileExists("/proc/version")) {
			String kv = LoadFile("/proc/version");
			int sp1 = kv.Find("version ");
			if(sp1 >= 0) {
				int sp2 = kv.Find(" ", sp1 + 8);
				if(sp2 > sp1)
					node.os_info = "Linux " + kv.Mid(sp1 + 8, sp2 - sp1 - 8);
			}
		}
		if(node.os_info.IsEmpty()) node.os_info = "Linux x86_64";

		if(FileExists("/proc/uptime")) {
			double up_secs = ScanDouble(LoadFile("/proc/uptime"));
			int hours = (int)(up_secs / 3600);
			int mins = (int)((up_secs - hours * 3600) / 60);
			int days = hours / 24;
			hours = hours % 24;
			if(days > 0)
				node.uptime = Format("%d`d %d`h %d`m", days, hours, mins);
			else
				node.uptime = Format("%d`h %d`m", hours, mins);
		}

		if(FileExists("/proc/loadavg")) {
			String la = LoadFile("/proc/loadavg");
			Vector<String> parts = Split(la, ' ');
			if(parts.GetCount() >= 3)
				node.load_avg = parts[0] + " " + parts[1] + " " + parts[2];
		}

		node.repo_count = GetLocalRepositories().GetCount();
		return true;
	}

	node.status = "disabled";
	return false;
}

bool HostPlatformService::AddOrUpdateNode(const DistributedNode& new_node)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);
	DistributedNode node = new_node;
	if(node.id.IsEmpty())
		node.id = ToLower(node.hostname);

	ProbeNode(node);

	bool found = false;
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].id == node.id || nodes[i].hostname == node.hostname) {
			nodes[i] = node;
			found = true;
			break;
		}
	}
	if(!found)
		nodes.Add(node);

	SaveNodes(nodes);
	return true;
}

bool HostPlatformService::RemoveNode(const String& node_id)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].id == node_id || nodes[i].hostname == node_id) {
			nodes.Remove(i);
			SaveNodes(nodes);
			return true;
		}
	}
	return false;
}

String HostPlatformService::ExecRemoteGit(const String& node_id, const String& repo_name, const String& git_args)
{
	(void)node_id; (void)repo_name; (void)git_args;
	return "disabled: host Git execution has no bounded argv process adapter";
}

String HostPlatformService::TriggerSync(const String& source_node_id, const String& target_node_id, const String& repo_name)
{
	(void)source_node_id; (void)target_node_id; (void)repo_name;
	return "disabled: synchronization requires a bounded argv process adapter";
}

Vector<FleetGitNodeStatus> HostPlatformService::GetFleetGitStatus(const String& repo_name)
{
	(void)repo_name;
	return Vector<FleetGitNodeStatus>();
}

String HostPlatformService::FleetSync(const String& source_node_id, const String& target_node_id, const String& repo_name, const String& mode)
{
	(void)source_node_id; (void)target_node_id; (void)repo_name; (void)mode;
	return "disabled: fleet synchronization and publication have no bounded argv process adapter";
}

FleetBuildResult HostPlatformService::ExecRemoteBuild(const String& node_id, const String& package_name, const String& method, const String& flags)
{
	FleetBuildResult res;
	res.node_id = node_id;
	res.package_name = package_name;
	res.method = method;
	res.flags = flags;
	res.command = "disabled";
	res.output = "disabled: build has no bounded argv process adapter";
	res.exit_code = -1;
	res.success = false;
	return res;
}

String HostPlatformService::RenderSubNav(const String& active_tab, const String& node_id)
{
	DistributedNode node = GetNode(node_id);
	Vector<DistributedNode> all_nodes = GetConfiguredNodes(false);

	String h;

	// Machine Switcher Top Bar
	h << "<div style=\"display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:10px; margin-bottom:1rem; padding:8px 14px; background:var(--wf-bg-view,#0d1117); border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
	  << "  <div style=\"display:flex; align-items:center; flex-wrap:wrap; gap:8px; font-size:0.85rem;\">\n"
	  << "    <span style=\"color:var(--wf-fg-inactive,#8b919d); font-weight:700;\">Active Host:</span>\n";

	for(int i = 0; i < all_nodes.GetCount(); i++) {
		const DistributedNode& n = all_nodes[i];
		bool is_cur = (n.id == node.id);
		String target_url;
		if(active_tab == "nodes" || active_tab == "settings")
			target_url = "/host/" + active_tab;
		else if(n.is_local)
			target_url = "/host/" + active_tab;
		else
			target_url = NodeRoute(n.id, active_tab);

		h << "    <a href=\"" << EscapeHtml(SafeHostPath(target_url)) << "\" class=\"btn\" style=\""
		  << (is_cur ? "background:var(--wf-accent-focus,#00a1c5); color:#ffffff; font-weight:700; border-color:var(--wf-accent-focus,#00a1c5);"
		             : "background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#d3dae3);")
		  << " padding:4px 10px; font-size:0.8rem; display:inline-flex; align-items:center; gap:6px;\">"
		  << VividIcon::Get(n.is_local ? "monitor" : "server", 14) << " " << EscapeHtml(n.name)
		  << (n.is_local ? " <small style='opacity:0.75;'>(Local)</small>" : "")
		  << "</a>\n";
	}

	h << "  </div>\n"
	  << "  <div style=\"display:flex; gap:6px;\">\n"
	  << "    <a href=\"/host/nodes\" class=\"btn\" style=\"padding:4px 10px; font-size:0.78rem; background:#1e293b; color:#38bdf8;\">"
	  << VividIcon::Get("cpu", 14) << " Mesh Topology</a>\n"
	  << "    <a href=\"/host/settings\" class=\"btn\" style=\"padding:4px 10px; font-size:0.78rem; background:#1e293b; color:#fff;\">"
	  << VividIcon::Get("settings", 14) << " Settings</a>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Second row: Standard SubNav Tabs for the active machine
	struct Tab { const char* id; const char* title; const char* icon; };
	static Tab tabs[] = {
		{ "overview", "Overview",           "server" },
		{ "files",    "Files (disabled)",   "folder" },
		{ "repos",    "Repositories",       "git-branch" },
		{ "fleet",    "Fleet Sync & Build", "refresh-cw" },
		{ "storage",  "Disk Storage",       "database" },
	};

	h << "<div style=\"display:flex; gap:8px; border-bottom:1px solid var(--wf-border-color, rgba(0,161,197,0.25)); padding-bottom:8px; margin-bottom:1.5rem; overflow-x:auto;\">\n";
	for(int i = 0; i < 5; i++) {
		bool is_act = (active_tab == tabs[i].id);
		String url = node.is_local ? ("/host/" + String(tabs[i].id)) : NodeRoute(node.id, tabs[i].id);
		h << "  <a href=\"" << EscapeHtml(SafeHostPath(url)) << "\" class=\"btn\" style=\""
		  << (is_act ? "background:var(--wf-accent-focus,#00a1c5); color:#ffffff; font-weight:700;"
		             : "background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#d3dae3);")
		  << " padding:6px 14px; font-size:0.85rem; display:inline-flex; align-items:center; gap:6px;\">"
		  << VividIcon::Get(tabs[i].icon, 15) << " " << tabs[i].title << "</a>\n";
	}

	h << "  <a href=\"/host/nodes\" class=\"btn\" style=\""
	  << (active_tab == "nodes" ? "background:var(--wf-accent-focus,#00a1c5); color:#ffffff; font-weight:700;"
	                            : "background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#d3dae3);")
	  << " padding:6px 14px; font-size:0.85rem; display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("cpu", 15) << " Compute Mesh</a>\n";

	h << "  <a href=\"/host/settings\" class=\"btn\" style=\""
	  << (active_tab == "settings" ? "background:var(--wf-accent-focus,#00a1c5); color:#ffffff; font-weight:700;"
	                               : "background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#d3dae3);")
	  << " padding:6px 14px; font-size:0.85rem; display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("settings", 15) << " Mesh Settings</a>\n";

	h << "</div>\n";
	return h;
}

String HostPlatformService::RenderOverview(Http& http, const String& node_id)
{
	DistributedNode node = GetNode(node_id);
	if(node.os_info.IsEmpty() || node.status != "online")
		ProbeNode(node);

	UserSession session = SystemAuthService::GetCurrentSession(http);
	Vector<HostRepoInfo> repos = GetRepositoriesForNode(node.id);

	String b;
	b << RenderSubNav("overview", node.id);

	// Status header banner
	b << "<div class=\"card\" style=\"border-left:4px solid " << (node.is_local ? "var(--wf-accent-focus,#00a1c5)" : "#38bdf8") << ";\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px;\">\n"
	  << "    <div>\n"
	  << "      <div style=\"font-size:1.4rem; font-weight:800; color:var(--wf-fg-normal,#fff); display:flex; align-items:center; gap:8px;\">\n"
	  << "        " << VividIcon::Get(node.is_local ? "monitor" : "server", 24) << " Node: <span>" << EscapeHtml(node.name) << "</span> (" << EscapeHtml(node.hostname) << ")\n"
	  << "        <span class=\"badge-v2\" style=\"background:var(--wf-color-positive,#00aa7f);\">" << EscapeHtml(ToUpper(node.status)) << "</span>\n"
	  << "        <span class=\"badge-v2\" style=\"background:#1e283b; color:#38bdf8;\">" << node.latency_ms << " ms</span>\n"
	  << "      </div>\n"
	  << "      <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem; margin-top:4px;\">"
	  << (node.is_local ? "Primary self-hosted workstation with local filesystem, development workspace, and mesh coordination."
	                    : "Remote compute node connected via low-latency SSH with full equal parity file, repository, and storage management.")
	  << "      </p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px;\">\n"
	  << "      <span class=\"wf-badge\">File browsing disabled</span>\n"
	  << "      <a href=\"" << (node.is_local ? "/host/repos" : NodeRoute(node.id, "repos")) << "\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#fff);\">" << VividIcon::Get("git-branch", 14) << " Repositories</a>\n"
	  << "      <a href=\"" << (node.is_local ? "/host/storage" : NodeRoute(node.id, "storage")) << "\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#fff);\">" << VividIcon::Get("database", 14) << " Disk Storage</a>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// 4 Stat Cards
	b << "<div class=\"grid-4\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">Development Repos</span>\n"
	  << "    <span class=\"stat-val\">" << repos.GetCount() << "</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">scanned in " << EscapeHtml(node.remote_path) << "</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">Protocol &amp; Access</span>\n"
	  << "    <span class=\"stat-val\" style=\"font-size:1.35rem; color:#38bdf8;\">" << EscapeHtml(ToUpper(node.protocol)) << "</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">User: " << EscapeHtml(node.ssh_user) << ":" << node.ssh_port << "</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">System Uptime</span>\n"
	  << "    <span class=\"stat-val\" style=\"font-size:1.35rem; color:#34d399;\">" << EscapeHtml(node.uptime.IsEmpty() ? String("Up") : node.uptime) << "</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">Kernel: " << EscapeHtml(node.os_info.IsEmpty() ? String("Linux") : node.os_info) << "</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">SSH Latency &amp; Load</span>\n"
	  << "    <span class=\"stat-val\" style=\"font-size:1.35rem; color:#a78bfa;\">" << node.latency_ms << " ms</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">Load: " << EscapeHtml(node.load_avg.IsEmpty() ? String("0.00") : node.load_avg) << "</span>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Details Grid
	b << "<div class=\"grid-2\">\n";

	// System Specs Table
	b << "  <div class=\"card\">\n"
	  << "    <div class=\"card-header\"><span>System Specifications (" << EscapeHtml(node.name) << ")</span></div>\n"
	  << "    <table><tbody>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d); width:35%;\">Node Identifier</td><td><strong>" << EscapeHtml(node.id) << "</strong></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Hostname / IP</td><td><code>" << EscapeHtml(node.hostname) << "</code></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Operating System</td><td>" << EscapeHtml(node.os_info.IsEmpty() ? String("Linux x86_64") : node.os_info) << "</td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">System Uptime</td><td>" << EscapeHtml(node.uptime) << "</td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Load Average</td><td><code>" << EscapeHtml(node.load_avg) << "</code></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Remote Path</td><td><code>" << EscapeHtml(node.remote_path) << "</code></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">User Account</td><td><code>" << EscapeHtml(node.ssh_user) << "</code> (Port " << node.ssh_port << ")</td></tr>\n"
	  << "    </tbody></table>\n"
	  << "  </div>\n";

	// Repositories Preview
	b << "  <div class=\"card\">\n"
	  << "    <div class=\"card-header\"><span>Repositories on " << EscapeHtml(node.name) << "</span><a href=\"" << (node.is_local ? "/host/repos" : NodeRoute(node.id, "repos")) << "\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:3px 8px;\">All " << repos.GetCount() << "</a></div>\n"
	  << "    <table><thead><tr><th>Repository</th><th>Branch</th><th>Commit</th></tr></thead><tbody>\n";

	int limit = min(repos.GetCount(), 6);
	for(int i = 0; i < limit; i++) {
		const HostRepoInfo& r = repos[i];
		b << "<tr>"
		  << "<td><strong>" << EscapeHtml(r.name) << "</strong></td>"
		  << "<td><code style=\"color:var(--wf-accent-focus,#00a1c5);\">" << EscapeHtml(r.branch.IsEmpty() ? String("-") : r.branch) << "</code></td>"
		  << "<td><code style=\"color:var(--wf-fg-inactive,#8b919d);\">" << EscapeHtml(r.last_commit_hash.IsEmpty() ? String("-") : r.last_commit_hash) << "</code></td>"
		  << "</tr>\n";
	}
	b << "    </tbody></table>\n  </div>\n";

	b << "</div>\n";
	return b;
}

String HostPlatformService::RenderFiles(Http& http, const String& node_id, const String& rel_path)
{
	if(!SystemAuthService::RequireAuth(http))
		return String();
	(void)node_id; (void)rel_path;
	return "<div class=\"card\"><p>Host file browsing disabled until canonical root containment and no-follow access are implemented.</p></div>";
}

String HostPlatformService::RenderRepos(Http& http, const String& node_id)
{
	DistributedNode node = GetNode(node_id);
	Vector<HostRepoInfo> repos = GetRepositoriesForNode(node.id);

	String b;
	b << RenderSubNav("repos", node.id);

	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>Repositories on " << EscapeHtml(node.name) << " (" << EscapeHtml(node.hostname) << ")</span>\n"
	  << "    <span class=\"badge-v2\">" << repos.GetCount() << " detected in " << EscapeHtml(node.remote_path) << "</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem; margin-bottom:1rem;\">"
	  << "    Git repository metadata residing on " << EscapeHtml(node.name) << ". File browsing and remote Git execution are disabled."
	  << "  </p>\n"
	  << "  <table><thead><tr><th>Repository Name</th><th>Git Branch</th><th>Commit Hash &amp; Message</th><th>Packages</th><th>Actions</th></tr></thead><tbody>\n";

	for(int i = 0; i < repos.GetCount(); i++) {
		const HostRepoInfo& r = repos[i];
		b << "<tr id=\"repo-row-" << i << "\">"
		  << "<td><strong>" << EscapeHtml(r.name) << "</strong></td>"
		  << "<td>"
		  << (r.has_git ? (String("<span style=\"display:inline-flex; align-items:center; gap:4px; font-weight:600; color:var(--wf-accent-focus,#00a1c5);\">") + VividIcon::Get("git-branch", 14) + " " + EscapeHtml(r.branch.IsEmpty() ? String("master") : r.branch) + "</span>") : "<span style=\"color:#64748b;\">no git</span>")
		  << "</td>"
		  << "<td><code style=\"color:var(--wf-fg-inactive,#8b919d);\">" << EscapeHtml(r.last_commit_hash.IsEmpty() ? String("-") : r.last_commit_hash) << "</code> "
		  << "<small style=\"color:var(--wf-fg-normal,#d3dae3);\">" << EscapeHtml(r.last_commit_msg) << "</small></td>"
		  << "<td>"
		  << (r.upp_packages.IsEmpty() ? "<span style=\"color:#64748b;\">-</span>" : ("<span class=\"badge-v2\" style=\"background:#6d55aa;\">" + AsString(r.upp_packages.GetCount()) + " upp</span>"))
		  << "</td>"
		  << "<td>"
		  << "  <div style=\"display:inline-flex; gap:6px;\">"
		  << "    <span class=\"wf-badge\">File browsing disabled</span>"
	  << (r.owner.IsEmpty() ? String() : ("    <a href=\"" + SafeHostPath("/repo/" + r.owner + "/" + r.name) + "\" class=\"btn\" title=\"GitHub\">GitHub</a>"))
		  << "  </div>"
		  << "</td>"
		  << "</tr>\n";
	}

	b << "  </tbody></table>\n</div>\n";
	return b;
}

String HostPlatformService::RenderNodes(Http& http)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);

	String b;
	b << RenderSubNav("nodes", String());

	b << "<div class=\"card\" style=\"border-left:4px solid var(--wf-accent-focus,#00a1c5); margin-bottom:1.5rem;\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px;\">\n"
	  << "    <div>\n"
	  << "      <h2 style=\"margin:0; font-size:1.35rem; display:flex; align-items:center; gap:8px;\">"
	  << VividIcon::Get("cpu", 22) << " Self-Hosted Compute Mesh Topology</h2>\n"
	  << "      <p style=\"margin:4px 0 0 0; color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem;\">"
	  << "        High-performance SSH &amp; rsync mesh linking workstation clusters for rapid multi-machine git coordination and sync."
	  << "      </p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px;\">\n"
	  << "      <a href=\"/host/settings\" class=\"btn btn-primary\">" << VividIcon::Get("settings", 14) << " Add / Manage Nodes</a>\n"
	  << "      <span class=\"wf-badge\">Remote probing disabled</span>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Mesh Nodes Grid Cards
	b << "<div style=\"display:grid; grid-template-columns:repeat(auto-fill, minmax(340px, 1fr)); gap:1rem; margin-bottom:1.5rem;\">\n";
	for(int i = 0; i < nodes.GetCount(); i++) {
		const DistributedNode& n = nodes[i];
		bool online = (n.status == "online");
		String status_color = online ? "var(--wf-color-positive,#00aa7f)" : "#ef4444";
		String node_overview_url = n.is_local ? "/host/overview" : NodeRoute(n.id, "overview");

		b << "<div class=\"card\" id=\"node-card-" << EscapeHtml(n.id) << "\" style=\"border-top:3px solid " << (online ? "var(--wf-accent-focus,#00a1c5)" : "#ef4444") << "; margin-bottom:0;\">\n"
		  << "  <div style=\"display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:0.75rem;\">\n"
		  << "    <div>\n"
		  << "      <div style=\"font-weight:700; font-size:1.05rem; display:flex; align-items:center; gap:6px;\">"
		  << VividIcon::Get(n.is_local ? "monitor" : "server", 18) << " <a href=\"" << EscapeHtml(node_overview_url) << "\" style=\"text-decoration:none;\">" << EscapeHtml(n.name) << "</a></div>\n"
		  << "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\"><code>" << EscapeHtml(n.hostname) << "</code> (" << EscapeHtml(n.id) << ")</div>\n"
		  << "    </div>\n"
		  << "    <div style=\"display:flex; flex-direction:column; align-items:flex-end; gap:4px;\">\n"
		  << "      <span class=\"badge-v2\" style=\"background:" << status_color << "; font-size:0.75rem;\">" << EscapeHtml(ToUpper(n.status)) << "</span>\n"
		  << "      <span class=\"badge-v2\" style=\"background:#1e283b; color:#38bdf8; font-size:0.7rem; border:1px solid rgba(56,189,248,0.2);\">" << n.latency_ms << " ms</span>\n"
		  << "    </div>\n"
		  << "  </div>\n"
		  << "  <div style=\"font-size:0.85rem; line-height:1.6; color:var(--wf-fg-normal,#d3dae3); margin-bottom:0.75rem;\">\n"
		  << "    <div><strong>Protocol:</strong> <span class=\"badge-v2\" style=\"background:#24292f; color:#a78bfa; font-size:0.7rem;\">" << EscapeHtml(ToUpper(n.protocol)) << "</span> "
		  << "      (" << EscapeHtml(n.ssh_user) << ":" << n.ssh_port << ")</div>\n"
		  << "    <div><strong>Path:</strong> <code>" << EscapeHtml(n.remote_path) << "</code></div>\n"
		  << "    <div><strong>Kernel:</strong> " << EscapeHtml(n.os_info.IsEmpty() ? String("Linux x86_64") : n.os_info) << "</div>\n"
		  << "    <div><strong>Uptime:</strong> " << EscapeHtml(n.uptime.IsEmpty() ? String("Up") : n.uptime) << "</div>\n"
		  << "    <div><strong>Load Avg:</strong> " << EscapeHtml(n.load_avg.IsEmpty() ? String("0.00") : n.load_avg) << "</div>\n"
		  << "    <div><strong>Repositories:</strong> " << n.repo_count << " scanned</div>\n"
		  << "  </div>\n"
		  << "  <div style=\"display:flex; gap:6px; border-top:1px solid var(--wf-border-color, rgba(255,255,255,0.08)); padding-top:0.6rem;\">\n"
		  << "    <a href=\"" << EscapeHtml(node_overview_url) << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Open Node</a>\n"
		  << "    <span class=\"wf-badge\">Remote operations disabled</span>\n"
		  << "  </div>\n"
		  << "</div>\n";
	}
	b << "</div>\n";

	// Keep the prior operation controls visibly unavailable; no remote actions are wired.
	b << "<div class=\"card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("git-branch", 18) << " Remote Git</span>\n"
	  << "    <span class=\"wf-badge\">Disabled</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem;\">Host Git execution and mesh synchronization are disabled.</p>\n"
	  << "</div>\n";

	return b;
}

String HostPlatformService::RenderFleet(Http& http, const String& node_id)
{
	DistributedNode active_node = GetNode(node_id);
	Vector<FleetGitNodeStatus> fleet = GetFleetGitStatus();
	Vector<DistributedNode> all_nodes = GetConfiguredNodes(false);

	String b;
	b << RenderSubNav("fleet", active_node.id);

	// Fleet Operations Header Card
	b << "<div class=\"card\" style=\"border-left:4px solid var(--wf-accent-focus,#00a1c5); margin-bottom:1.5rem;\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px;\">\n"
	  << "    <div>\n"
	  << "      <h2 style=\"margin:0; font-size:1.35rem; display:flex; align-items:center; gap:8px;\">"
	  << VividIcon::Get("refresh-cw", 22) << " Cluster Fleet Git Synchronization &amp; Remote Compilation</h2>\n"
	  << "      <p style=\"margin:4px 0 0 0; color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem;\">"
	  << "        High-speed multi-node Git synchronization and remote <code>./bin/build</code> dispatcher across workstation and cluster fleet over low-latency SSH."
	  << "      </p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px;\">\n"
	  << "      <button id=\"btn-fleet-sync-all\" disabled class=\"btn btn-primary\" style=\"display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("refresh-cw", 14) << " Fleet sync disabled</button>\n"
	  << "      <button id=\"btn-fleet-fetch-all\" disabled class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:#fff; display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("download", 14) << " Fleet fetch disabled</button>\n"
	  << "      <span class=\"wf-badge\">Read-only status</span>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Card 1: Multi-Node Git Status Matrix
	b << "<div class=\"card\" id=\"fleet-git-matrix\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("git-branch", 18) << " Fleet Git Synchronization Matrix</span>\n"
	  << "    <span class=\"badge-v2\" style=\"background:#38bdf8; color:#0f172a;\">3 Fleet Nodes Online</span>\n"
	  << "  </div>\n"
	  << "  <table>\n"
	  << "    <thead>\n"
	  << "      <tr>\n"
	  << "        <th>Node / Hostname</th>\n"
	  << "        <th>Active Branch</th>\n"
	  << "        <th>Latest Commit</th>\n"
	  << "        <th>Working Tree</th>\n"
	  << "        <th>Fleet Sync State</th>\n"
	  << "        <th style=\"text-align:right;\">Node Actions</th>\n"
	  << "      </tr>\n"
	  << "    </thead>\n"
	  << "    <tbody>\n";

	for(int i = 0; i < fleet.GetCount(); i++) {
		const FleetGitNodeStatus& s = fleet[i];
		String badge_color = "#10b981"; // green
		String badge_text = "SYNCED";

		if(s.sync_state == "behind") {
			badge_color = "#f59e0b"; // amber
			badge_text = "BEHIND";
		} else if(s.sync_state == "ahead") {
			badge_color = "#38bdf8"; // blue
			badge_text = "AHEAD";
		} else if(s.sync_state == "diverged") {
			badge_color = "#ec4899"; // pink
			badge_text = "DIVERGED";
		} else if(s.sync_state == "unreachable") {
			badge_color = "#ef4444"; // red
			badge_text = "OFFLINE";
		}

		b << "      <tr id=\"fleet-node-row-" << EscapeHtml(s.node_id) << "\">\n"
		  << "        <td>\n"
		  << "          <div style=\"font-weight:700; color:var(--wf-fg-normal,#fff);\">" << EscapeHtml(s.name) << "</div>\n"
		  << "          <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\"><code>" << EscapeHtml(s.hostname) << "</code> &bull; " << s.latency_ms << " ms</div>\n"
		  << "        </td>\n"
		  << "        <td><code style=\"color:var(--wf-accent-focus,#00a1c5);\">" << EscapeHtml(s.branch) << "</code></td>\n"
		  << "        <td>\n"
		  << "          <div><code style=\"color:#38bdf8;\">" << EscapeHtml(s.commit_hash) << "</code> <span style=\"font-size:0.85rem;\">" << EscapeHtml(s.commit_msg) << "</span></div>\n"
		  << "          <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\">" << EscapeHtml(s.commit_time) << "</div>\n"
		  << "        </td>\n"
		  << "        <td>\n";

		if(s.is_dirty) {
			b << "          <span class=\"badge-v2\" style=\"background:#f59e0b; color:#000;\">" << s.dirty_count << " modified</span>\n";
		} else {
			b << "          <span class=\"badge-v2\" style=\"background:#10b981;\">Clean</span>\n";
		}

		b << "        </td>\n"
		  << "        <td><span class=\"badge-v2\" style=\"background:" << badge_color << "; font-weight:700;\">" << badge_text << "</span></td>\n"
		  << "        <td style=\"text-align:right;\">\n";

	if(!s.node_id.IsEmpty()) {
			b << "          <span class=\"wf-badge\">Fleet operations disabled</span>\n";
		}

		b << "        </td>\n"
		  << "      </tr>\n";
	}

	b << "    </tbody>\n"
	  << "  </table>\n"
	  << "  <div style=\"margin-top:1rem;\">\n"
	  << "    <div style=\"font-size:0.85rem; font-weight:700; color:var(--wf-fg-normal,#fff); margin-bottom:6px; display:flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("terminal", 14) << " Fleet status</div>\n"
	  << "    <pre id=\"fleet-sync-terminal\" style=\"background:#0b0f19; color:#94a3b8; padding:1rem; border-radius:6px; font-family:monospace; font-size:0.85rem; min-height:80px; max-height:260px; overflow-y:auto; border:1px solid #1e293b;\">Remote fleet operations are disabled. No synchronization was started.</pre>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Card 2: Remote Compilation & Build Dispatcher
	b << "<div class=\"card\" id=\"fleet-build-card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("cpu", 18) << " Remote Build &amp; Test (Disabled)</span>\n"
	  << "    <span class=\"wf-badge\">Unavailable</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem; margin-bottom:1rem;\">"
	  << "    Remote build commands are disabled until a bounded, explicitly authorized process adapter is available. No command can be queued from this page."
	  << "  </p>\n"
	  << "  <div style=\"display:grid; grid-template-columns: 1.5fr 2fr 1fr 1fr auto auto; gap:10px; align-items:end; margin-bottom:1rem;\">\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Target Cluster Node</label>\n"
	  << "      <select id=\"fleet-build-node\" disabled class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n";

	for(int i = 0; i < all_nodes.GetCount(); i++) {
		b << "        <option value=\"" << EscapeHtml(all_nodes[i].id) << "\"" << (all_nodes[i].id == active_node.id ? " selected" : "") << ">"
		  << EscapeHtml(all_nodes[i].name) << " (" << EscapeHtml(all_nodes[i].hostname) << ")</option>\n";
	}

	b << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Target Package</label>\n"
	  << "      <input type=\"text\" id=\"fleet-build-pkg\" disabled class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Compiler Method</label>\n"
	  << "      <select id=\"fleet-build-method\" disabled class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "        <option value=\"CLANG\" selected>CLANG</option>\n"
	  << "        <option value=\"GCC\">GCC</option>\n"
	  << "        <option value=\"MSVS\">MSVS</option>\n"
	  << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Build Flags</label>\n"
	  << "      <input type=\"text\" id=\"fleet-build-flags\" disabled class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button id=\"btn-fleet-build-run\" disabled class=\"btn btn-primary\" style=\"padding:8px 14px; font-weight:700;\">Remote build disabled</button>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button id=\"btn-fleet-build-clean\" disabled class=\"btn\" style=\"padding:8px 14px; font-weight:700; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Remote build disabled</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <div id=\"fleet-build-status-bar\" style=\"display:none; margin-bottom:0.75rem; font-size:0.85rem; font-weight:600; padding:6px 12px; border-radius:4px;\"></div>\n"
	  << "  <pre id=\"fleet-build-terminal\" style=\"background:#0b0f19; color:#94a3b8; padding:1rem; border-radius:6px; font-family:monospace; font-size:0.85rem; min-height:100px; max-height:400px; overflow-y:auto; border:1px solid #1e293b;\">Remote build dispatch is unavailable. No build was started.</pre>\n"
	  << "</div>\n";

	// All remote operation controls and client-side dispatchers are disabled.

	return b;
}

String HostPlatformService::RenderSettings(Http& http)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);

	String b;
	b << RenderSubNav("settings", String());

	// Add Node Form
	b << "<div class=\"card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("plus-circle", 18) << " Add Computer or Server to Mesh</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem; margin-bottom:1.25rem;\">"
	  << "    Enter connection credentials for any network host accessible via SSH keys. The node will be probed and added to the cluster mesh."
	  << "  </p>\n"
	  << "  <form method=\"POST\" action=\"/host/settings/add\">\n"
	  << "    <div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(220px, 1fr)); gap:12px; margin-bottom:1.25rem;\">\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Node Identifier (Slug)</label>\n"
	  << "        <input type=\"text\" name=\"id\" placeholder=\"e.g. worker-node-2\" required class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Display Name</label>\n"
	  << "        <input type=\"text\" name=\"name\" placeholder=\"e.g. Office Ryzen Workstation\" required class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Hostname / IP Address</label>\n"
	  << "        <input type=\"text\" name=\"hostname\" placeholder=\"host.example\" required class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">SSH Username</label>\n"
	  << "        <input type=\"text\" name=\"ssh_user\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">SSH Port</label>\n"
	  << "        <input type=\"number\" name=\"ssh_port\" value=\"22\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Remote Dev Workspace Path</label>\n"
	  << "        <input type=\"text\" name=\"remote_path\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Protocol</label>\n"
	  << "        <select name=\"protocol\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "          <option value=\"ssh\">SSH (Fast Remote Execution)</option>\n"
	  << "          <option value=\"rsync\">Rsync (Storage Mirroring)</option>\n"
	  << "          <option value=\"local\">Local Workstation</option>\n"
	  << "        </select>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "    <button type=\"submit\" class=\"btn btn-primary\" style=\"padding:8px 20px; font-weight:700;\">"
	  << VividIcon::Get("check", 16) << " Probe &amp; Register Node</button>\n"
	  << "  </form>\n"
	  << "</div>\n";

	// Node Management Table
	b << "<div class=\"card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("list", 18) << " Registered Mesh Cluster Nodes (" << nodes.GetCount() << ")</span>\n"
	  << "  </div>\n"
	  << "  <table><thead><tr><th>Node Name &amp; ID</th><th>Host / IP</th><th>SSH Creds</th><th>Remote Workspace</th><th>Status</th><th>Latency</th><th>Actions</th></tr></thead><tbody>\n";

	for(int i = 0; i < nodes.GetCount(); i++) {
		const DistributedNode& n = nodes[i];
		bool online = (n.status == "online");
		String node_url = n.is_local ? "/host/overview" : NodeRoute(n.id, "overview");
		b << "<tr>"
		  << "<td><strong><a href=\"" << EscapeHtml(node_url) << "\">" << EscapeHtml(n.name) << "</a></strong><br><small style=\"color:#64748b;\">" << EscapeHtml(n.id) << "</small></td>"
		  << "<td><code>" << EscapeHtml(n.hostname) << "</code></td>"
		  << "<td><code>" << EscapeHtml(n.ssh_user) << ":" << n.ssh_port << "</code></td>"
		  << "<td><code>" << EscapeHtml(n.remote_path) << "</code></td>"
		  << "<td><span class=\"badge-v2\" style=\"background:" << (online ? "var(--wf-color-positive,#00aa7f)" : "#ef4444") << ";\">" << EscapeHtml(ToUpper(n.status)) << "</span></td>"
		  << "<td>" << n.latency_ms << " ms</td>"
		  << "<td>\n"
		  << "  <div style=\"display:inline-flex; gap:6px;\">\n"
		  << "    <a href=\"" << EscapeHtml(node_url) << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Open</a>\n";

		if(!n.is_local) {
			b << "    <form method=\"POST\" action=\"/host/settings/remove\" style=\"margin:0; display:inline;\">\n"
			  << "      <input type=\"hidden\" name=\"id\" value=\"" << EscapeHtml(n.id) << "\">\n"
			  << "      <button type=\"submit\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:#7f1d1d; color:#fca5a5;\" onclick=\"return confirm('Remove this node?');\">Delete</button>\n"
			  << "    </form>\n";
		}
		b << "  </div>\n"
		  << "</td>"
		  << "</tr>\n";
	}

	b << "  </tbody></table>\n</div>\n";

	// Restore Defaults Banner
	b << "<div class=\"card\" style=\"border-left:4px solid #a78bfa;\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:center;\">\n"
	  << "    <div>\n"
	  << "      <div style=\"font-weight:700;\">Clear Fleet Configuration</div>\n"
	  << "      <p style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d); margin:4px 0 0 0;\">Remove the registered node configuration and leave the fleet unconfigured.</p>\n"
	  << "    </div>\n"
	  << "    <form method=\"POST\" action=\"/host/settings/reset\" style=\"margin:0;\">\n"
	  << "      <button type=\"submit\" class=\"btn\" style=\"background:#2e1065; color:#c4b5fd; font-weight:600; padding:6px 14px;\">Clear Fleet Configuration</button>\n"
	  << "    </form>\n"
	  << "  </div>\n"
	  << "</div>\n";

	return b;
}

String HostPlatformService::RenderStorage(Http& http, const String& node_id)
{
	DistributedNode node = GetNode(node_id);

	String b;
	b << RenderSubNav("storage", node.id);

	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>Storage Mounts &amp; Disk Usage (" << EscapeHtml(node.name) << ")</span></div>\n"
	  << "  <table><thead><tr><th>Mount Point</th><th>Total Size</th><th>Used</th><th>Free Space</th><th>Usage Bar</th></tr></thead><tbody>\n";

	if(node.is_local || node.protocol == "local") {
#ifdef PLATFORM_POSIX
		const char* check_mounts[] = { "/", "/home", "/yhteinen", "/tmp" };
		for(int i = 0; i < 4; i++) {
			struct statvfs sv;
			if(statvfs(check_mounts[i], &sv) == 0 && sv.f_blocks > 0) {
				double total_gb = (sv.f_blocks * (double)sv.f_frsize) / (1024.0 * 1024 * 1024);
				double free_gb = (sv.f_bavail * (double)sv.f_frsize) / (1024.0 * 1024 * 1024);
				double used_gb = total_gb - free_gb;
				double pct = (total_gb > 0) ? (used_gb / total_gb * 100.0) : 0.0;

				b << "<tr>"
				  << "<td><code>" << check_mounts[i] << "</code></td>"
				  << "<td>" << Format("%.1f GB", total_gb) << "</td>"
				  << "<td>" << Format("%.1f GB", used_gb) << "</td>"
				  << "<td><strong style=\"color:#34d399;\">" << Format("%.1f GB", free_gb) << "</strong></td>"
				  << "<td style=\"width:220px;\">" << ManagerSkylarkApp::RenderProgressBar(pct) << "</td>"
				  << "</tr>\n";
			}
		}
#endif
	} else {
		// Remote SSH df -k -P
		b << "<tr><td colspan=\"5\">Remote storage inspection disabled</td></tr>\n";
	}

	b << "  </tbody></table>\n</div>\n";
	return b;
}

// ============================================================================
// SKYLARK Handlers for /host Namespace (Local Workstation)
// ============================================================================

SKYLARK(HostOverviewPage, "host")
{
	String b = HostPlatformService::RenderOverview(http, String());
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Host Workstation", "host", b, &http);
}

SKYLARK(HostOverviewDeepPage, "host/overview")
{
	String b = HostPlatformService::RenderOverview(http, String());
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Host Workstation", "host", b, &http);
}

SKYLARK(HostFilesPage, "host/files/**")
{
	String rel;
	for(int i = 0; i < http.GetParamCount(); i++) {
		if(i > 0) rel << "/";
		rel << http[i];
	}

	String b = HostPlatformService::RenderFiles(http, String(), rel);
	if(!b.IsEmpty())
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Home Directory Files", "host_files", b, &http);
}

SKYLARK(HostReposPage, "host/repos")
{
	String b = HostPlatformService::RenderRepos(http, String());
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Local Repositories", "host_repos", b, &http);
}

SKYLARK(HostNodesPage, "host/nodes")
{
	String b = HostPlatformService::RenderNodes(http);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Compute Mesh", "host_nodes", b, &http);
}

SKYLARK(HostSettingsPage, "host/settings")
{
	String b = HostPlatformService::RenderSettings(http);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Mesh Settings", "host_settings", b, &http);
}

SKYLARK(HostSettingsAddPost, "host/settings/add:POST_RAW")
{
	String body = http.GetRequestContent();
	VectorMap<String, String> form;
	Vector<String> pairs = Split(body, '&');
	for(const String& p : pairs) {
		int eq = p.Find('=');
		if(eq >= 0)
			form.Add(UrlDecode(p.Left(eq)), UrlDecode(p.Mid(eq + 1)));
	}

	DistributedNode n;
	n.id = form.Get("id", "");
	n.name = form.Get("name", "");
	n.hostname = form.Get("hostname", "");
	n.ssh_user = form.Get("ssh_user", "");
	n.ssh_port = ScanInt(form.Get("ssh_port", "22"));
	n.remote_path = form.Get("remote_path", "");
	n.protocol = form.Get("protocol", "disabled");

	if(!n.hostname.IsEmpty())
		HostPlatformService::AddOrUpdateNode(n);

	http.Redirect("/host/settings");
}

SKYLARK(HostSettingsRemovePost, "host/settings/remove:POST_RAW")
{
	String body = http.GetRequestContent();
	VectorMap<String, String> form;
	Vector<String> pairs = Split(body, '&');
	for(const String& p : pairs) {
		int eq = p.Find('=');
		if(eq >= 0)
			form.Add(UrlDecode(p.Left(eq)), UrlDecode(p.Mid(eq + 1)));
	}

	String id = form.Get("id", "");
	if(!id.IsEmpty())
		HostPlatformService::RemoveNode(id);

	http.Redirect("/host/settings");
}

SKYLARK(HostSettingsResetPost, "host/settings/reset:POST_RAW")
{
	String conf_path = AppendFileName(GetHomeDirFile(".config/managerskylark"), "nodes.json");
	if(FileExists(conf_path))
		FileDelete(conf_path);

	http.Redirect("/host/settings");
}

SKYLARK(HostStoragePage, "host/storage")
{
	String b = HostPlatformService::RenderStorage(http, String());
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Disk Storage", "host_storage", b, &http);
}

// ============================================================================
// SKYLARK Handlers for /node Namespace (Equal Parity Across All Mesh Computers)
// ============================================================================

SKYLARK(NodeOverviewDirect, "node/*")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderOverview(http, node_id);
	DistributedNode node = HostPlatformService::GetNode(node_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Overview", "node_" + node_id + "_overview", b, &http);
}

SKYLARK(NodeOverviewPage, "node/*/overview")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderOverview(http, node_id);
	DistributedNode node = HostPlatformService::GetNode(node_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Overview", "node_" + node_id + "_overview", b, &http);
}

SKYLARK(NodeFilesRootPage, "node/*/files")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderFiles(http, node_id, "");
	DistributedNode node = HostPlatformService::GetNode(node_id);
	if(!b.IsEmpty())
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Files", "node_" + node_id + "_files", b, &http);
}

SKYLARK(NodeFilesDeepPage, "node/*/files/**")
{
	String node_id = http[0];
	String rel;
	for(int i = 1; i < http.GetParamCount(); i++) {
		if(i > 1) rel << "/";
		rel << http[i];
	}

	String b = HostPlatformService::RenderFiles(http, node_id, rel);
	DistributedNode node = HostPlatformService::GetNode(node_id);
	if(!b.IsEmpty())
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Files", "node_" + node_id + "_files", b, &http);
}

SKYLARK(NodeReposPage, "node/*/repos")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderRepos(http, node_id);
	DistributedNode node = HostPlatformService::GetNode(node_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Repositories", "node_" + node_id + "_repos", b, &http);
}

SKYLARK(NodeStoragePage, "node/*/storage")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderStorage(http, node_id);
	DistributedNode node = HostPlatformService::GetNode(node_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Disk Storage", "node_" + node_id + "_storage", b, &http);
}

SKYLARK(ApiHostStatus, "api/host/status")
{
	ValueMap res;
	res.Set("hostname", GetComputerName());
	res.Set("cpu_cores", CPU_Cores());
	res.Set("repo_inventory_available", false);
	res.Set("nodes_count", HostPlatformService::GetConfiguredNodes().GetCount());
	http.ContentType("application/json") << AsJSON(res);
}

static void HostOperationDisabled(Http& http)
{
	ValueMap res;
	res.Add("status", "disabled");
	res.Add("available", false);
	res.Add("error", "generic_operation_disabled");
	http.ContentType("application/json");
	http.Response(503, "Service Unavailable");
	http << AsJSON(res, true);
}

SKYLARK(ApiHostRepos, "api/host/repos")
{
	HostOperationDisabled(http);
}

SKYLARK(ApiNodeRepos, "api/node/*/repos")
{
	HostOperationDisabled(http);
}

SKYLARK(ApiNodeStatus, "api/node/*/status")
{
	String node_id = http[0];
	DistributedNode node = HostPlatformService::GetNode(node_id);
	http.ContentType("application/json") << AsJSON(StoreAsJsonValue(node));
}

SKYLARK(ApiHostNodesList, "api/host/nodes")
{
	Vector<DistributedNode> nodes = HostPlatformService::GetConfiguredNodes(false);
	ValueArray va;
	for(int i = 0; i < nodes.GetCount(); i++)
		va.Add(StoreAsJsonValue(nodes[i]));
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiHostNodesAdd, "api/host/nodes/add:POST_RAW")
{
	String body = http.GetRequestContent();
	DistributedNode node;
	Value v = ParseJSON(body);
	if(v.Is<ValueMap>()) {
		LoadFromJsonValue(node, v);
	} else {
		// Form encoded fallback
		Vector<String> pairs = Split(body, '&');
		for(const String& p : pairs) {
			int eq = p.Find('=');
			if(eq >= 0) {
				String k = UrlDecode(p.Left(eq));
				String val = UrlDecode(p.Mid(eq + 1));
				if(k == "id") node.id = val;
				else if(k == "name") node.name = val;
				else if(k == "hostname") node.hostname = val;
				else if(k == "ssh_user") node.ssh_user = val;
				else if(k == "ssh_port") node.ssh_port = ScanInt(val);
				else if(k == "remote_path") node.remote_path = val;
				else if(k == "protocol") node.protocol = val;
			}
		}
	}

	if(node.hostname.IsEmpty()) {
		ValueMap err;
		err.Set("status", "error");
		err.Set("message", "Missing hostname / IP");
		http.ContentType("application/json") << AsJSON(err);
		return;
	}

	HostPlatformService::AddOrUpdateNode(node);
	ValueMap res;
	res.Set("status", "ok");
	res.Set("node_id", node.id);
	res.Set("latency_ms", node.latency_ms);
	res.Set("node_status", node.status);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiHostNodesRemove, "api/host/nodes/remove:POST_RAW")
{
	String body = http.GetRequestContent();
	String node_id;
	Value v = ParseJSON(body);
	if(v.Is<ValueMap>()) {
		node_id = v["id"];
		if(node_id.IsEmpty()) node_id = v["node_id"];
	} else {
		Vector<String> pairs = Split(body, '&');
		for(const String& p : pairs) {
			int eq = p.Find('=');
			if(eq >= 0 && UrlDecode(p.Left(eq)) == "id")
				node_id = UrlDecode(p.Mid(eq + 1));
		}
	}

	bool ok = HostPlatformService::RemoveNode(node_id);
	ValueMap res;
	res.Set("status", ok ? "ok" : "not_found");
	res.Set("node_id", node_id);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiHostNodesProbe, "api/host/nodes/probe/*")
{
	HostOperationDisabled(http);
}

SKYLARK(ApiHostRemoteGit, "api/host/remotegit:POST_RAW")
{
	HostOperationDisabled(http);
}

SKYLARK(ApiHostSync, "api/host/sync:POST_RAW")
{
	HostOperationDisabled(http);
}

SKYLARK(HostFleetPage, "host/fleet")
{
	String b = HostPlatformService::RenderFleet(http, String());
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Fleet Sync & Build", "host_fleet", b, &http);
}

SKYLARK(NodeFleetPage, "node/*/fleet")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderFleet(http, node_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Fleet Sync & Build - " + node_id, "node_" + node_id + "_fleet", b, &http);
}

SKYLARK(ApiHostFleetStatus, "api/host/fleet/status")
{
	HostOperationDisabled(http);
}

SKYLARK(ApiHostFleetSync, "api/host/fleet/sync:POST_RAW")
{
	HostOperationDisabled(http);
}

SKYLARK(ApiHostFleetBuild, "api/host/fleet/build:POST_RAW")
{
	HostOperationDisabled(http);
}

END_UPP_NAMESPACE
