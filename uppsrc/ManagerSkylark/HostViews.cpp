#include "ManagerSkylark.h"
#include "SystemAuth.h"
#include "HostPlatform.h"
#include "HostExec.h"

NAMESPACE_UPP

struct DirEntry : Moveable<DirEntry> {
	String name;
	int64  length = 0;
	Time   last_write_time;
	bool   is_folder = false;
};

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
			target_url = "/node/" + n.id + "/" + active_tab;

		h << "    <a href=\"" << target_url << "\" class=\"btn\" style=\""
		  << (is_cur ? "background:var(--wf-accent-focus,#00a1c5); color:#ffffff; font-weight:700; border-color:var(--wf-accent-focus,#00a1c5);"
		             : "background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#d3dae3);")
		  << " padding:4px 10px; font-size:0.8rem; display:inline-flex; align-items:center; gap:6px;\">"
		  << VividIcon::Get(n.is_local ? "monitor" : "server", 14) << " " << n.name
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
		{ "files",    "Home Files",         "folder" },
		{ "repos",    "Repositories",       "git-branch" },
		{ "fleet",    "Fleet Sync & Build", "refresh-cw" },
		{ "storage",  "Disk Storage",       "database" },
	};

	h << "<div style=\"display:flex; gap:8px; border-bottom:1px solid var(--wf-border-color, rgba(0,161,197,0.25)); padding-bottom:8px; margin-bottom:1.5rem; overflow-x:auto;\">\n";
	for(int i = 0; i < 5; i++) {
		bool is_act = (active_tab == tabs[i].id);
		String url = node.is_local ? ("/host/" + String(tabs[i].id)) : ("/node/" + node.id + "/" + String(tabs[i].id));
		h << "  <a href=\"" << url << "\" class=\"btn\" style=\""
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
	  << "        " << VividIcon::Get(node.is_local ? "monitor" : "server", 24) << " Node: <span>" << node.name << "</span> (" << node.hostname << ")\n"
	  << "        <span class=\"badge-v2\" style=\"background:var(--wf-color-positive,#00aa7f);\">" << ToUpper(node.status) << "</span>\n"
	  << "        <span class=\"badge-v2\" style=\"background:#1e283b; color:#38bdf8;\">" << node.latency_ms << " ms</span>\n"
	  << "      </div>\n"
	  << "      <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem; margin-top:4px;\">"
	  << (node.is_local ? "Primary self-hosted workstation with local filesystem, development workspace, and mesh coordination."
	                    : "Remote compute node connected via low-latency SSH with full equal parity file, repository, and storage management.")
	  << "      </p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px;\">\n"
	  << "      <a href=\"" << (node.is_local ? "/host/files" : ("/node/" + node.id + "/files")) << "\" class=\"btn btn-primary\">" << VividIcon::Get("folder", 14) << " Explore Files</a>\n"
	  << "      <a href=\"" << (node.is_local ? "/host/repos" : ("/node/" + node.id + "/repos")) << "\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#fff);\">" << VividIcon::Get("git-branch", 14) << " Browse Repos</a>\n"
	  << "      <a href=\"" << (node.is_local ? "/host/storage" : ("/node/" + node.id + "/storage")) << "\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#fff);\">" << VividIcon::Get("database", 14) << " Disk Storage</a>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// 4 Stat Cards
	b << "<div class=\"grid-4\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">Development Repos</span>\n"
	  << "    <span class=\"stat-val\">" << repos.GetCount() << "</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">scanned in " << node.remote_path << "</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">Protocol &amp; Access</span>\n"
	  << "    <span class=\"stat-val\" style=\"font-size:1.35rem; color:#38bdf8;\">" << ToUpper(node.protocol) << "</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">User: " << node.ssh_user << ":" << node.ssh_port << "</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">System Uptime</span>\n"
	  << "    <span class=\"stat-val\" style=\"font-size:1.35rem; color:#34d399;\">" << (node.uptime.IsEmpty() ? "Up" : node.uptime) << "</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">Kernel: " << (node.os_info.IsEmpty() ? "Linux" : node.os_info) << "</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">SSH Latency &amp; Load</span>\n"
	  << "    <span class=\"stat-val\" style=\"font-size:1.35rem; color:#a78bfa;\">" << node.latency_ms << " ms</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">Load: " << (node.load_avg.IsEmpty() ? "0.00" : node.load_avg) << "</span>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Details Grid
	b << "<div class=\"grid-2\">\n";

	// System Specs Table
	b << "  <div class=\"card\">\n"
	  << "    <div class=\"card-header\"><span>System Specifications (" << node.name << ")</span></div>\n"
	  << "    <table><tbody>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d); width:35%;\">Node Identifier</td><td><strong>" << node.id << "</strong></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Hostname / IP</td><td><code>" << node.hostname << "</code></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Operating System</td><td>" << (node.os_info.IsEmpty() ? "Linux x86_64" : node.os_info) << "</td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">System Uptime</td><td>" << node.uptime << "</td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Load Average</td><td><code>" << node.load_avg << "</code></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Remote Path</td><td><code>" << node.remote_path << "</code></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">User Account</td><td><code>" << node.ssh_user << "</code> (Port " << node.ssh_port << ")</td></tr>\n"
	  << "    </tbody></table>\n"
	  << "  </div>\n";

	// Repositories Preview
	b << "  <div class=\"card\">\n"
	  << "    <div class=\"card-header\"><span>Repositories on " << node.name << "</span><a href=\"" << (node.is_local ? "/host/repos" : ("/node/" + node.id + "/repos")) << "\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:3px 8px;\">All " << repos.GetCount() << "</a></div>\n"
	  << "    <table><thead><tr><th>Repository</th><th>Branch</th><th>Commit</th></tr></thead><tbody>\n";

	int limit = min(repos.GetCount(), 6);
	for(int i = 0; i < limit; i++) {
		const HostRepoInfo& r = repos[i];
		String file_link = node.is_local ? ("/host/files/Dev/" + r.name) : ("/node/" + node.id + "/files/Dev/" + r.name);
		b << "<tr>"
		  << "<td><strong><a href=\"" << file_link << "\">" << r.name << "</a></strong></td>"
		  << "<td><code style=\"color:var(--wf-accent-focus,#00a1c5);\">" << (r.branch.IsEmpty() ? "-" : r.branch) << "</code></td>"
		  << "<td><code style=\"color:var(--wf-fg-inactive,#8b919d);\">" << (r.last_commit_hash.IsEmpty() ? "-" : r.last_commit_hash) << "</code></td>"
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

	DistributedNode node = GetNode(node_id);

	String clean_rel = rel_path;
	while(clean_rel.StartsWith("/")) clean_rel = clean_rel.Mid(1);
	while(clean_rel.EndsWith("/")) clean_rel = clean_rel.Left(clean_rel.GetLength() - 1);

	// Prevent path traversal
	Vector<String> parts = Split(clean_rel, '/');
	Vector<String> safe_parts;
	for(int i = 0; i < parts.GetCount(); i++) {
		String p = parts[i];
		if(p == "." || p.IsEmpty()) continue;
		if(p == "..") {
			if(!safe_parts.IsEmpty()) safe_parts.Drop();
			continue;
		}
		safe_parts.Add(p);
	}

	String safe_rel = Join(safe_parts, "/");
	String node_base_url = node.is_local ? "/host/files" : ("/node/" + node.id + "/files");

	String b;
	b << RenderSubNav("files", node.id);

	// Breadcrumb header
	b << "<div class=\"card\" style=\"margin-bottom:1rem; padding:0.85rem 1.25rem;\">\n"
	  << "  <div style=\"display:flex; align-items:center; gap:8px; font-size:0.95rem; font-weight:600; flex-wrap:wrap;\">\n"
	  << "    <a href=\"" << node_base_url << "\" style=\"display:inline-flex; align-items:center; gap:4px;\">"
	  << VividIcon::Get("home", 16) << " ~ (" << node.name << ")</a>\n";

	String accum;
	for(int i = 0; i < safe_parts.GetCount(); i++) {
		if(!accum.IsEmpty()) accum << "/";
		accum << safe_parts[i];
		b << "    <span style=\"color:var(--wf-fg-inactive,#8b919d);\">/</span>\n";
		if(i == safe_parts.GetCount() - 1)
			b << "    <span style=\"color:var(--wf-accent-focus,#00a1c5);\">" << safe_parts[i] << "</span>\n";
		else
			b << "    <a href=\"" << node_base_url << "/" << accum << "\">" << safe_parts[i] << "</a>\n";
	}
	b << "  </div>\n"
	  << "</div>\n";

	if(node.is_local || node.hostname == "192.168.1.106") {
		// Local filesystem handling
		String home_dir = GetHomeDirFile("");
		String full_path = safe_rel.IsEmpty() ? home_dir : AppendFileName(home_dir, safe_rel);

		if(DirectoryExists(full_path)) {
			b << "<div class=\"card\">\n"
			  << "  <div class=\"card-header\"><span>Contents: <code>~/" << safe_rel << "</code></span></div>\n"
			  << "  <table><thead><tr><th>Name</th><th>Size</th><th>Permissions</th><th>Last Modified</th></tr></thead><tbody>\n";

			if(!safe_parts.IsEmpty()) {
				String parent_rel;
				for(int i = 0; i < safe_parts.GetCount() - 1; i++) {
					if(i > 0) parent_rel << "/";
					parent_rel << safe_parts[i];
				}
				b << "<tr><td colspan=\"4\"><a href=\"" << node_base_url << "/" << parent_rel << "\" style=\"display:inline-flex; align-items:center; gap:6px; font-weight:700;\">"
				  << VividIcon::Get("chevron-left", 14) << " .. (Parent Directory)</a></td></tr>\n";
			}

			Vector<DirEntry> folders, files;
			FindFile ff(AppendFileName(full_path, "*"));
			while(ff) {
				if(ff.GetName() != "." && ff.GetName() != "..") {
					DirEntry e;
					e.name = ff.GetName();
					e.length = ff.GetLength();
					e.last_write_time = ff.GetLastWriteTime();
					e.is_folder = ff.IsFolder();
					if(e.is_folder) folders.Add(e);
					else files.Add(e);
				}
				ff.Next();
			}
			Sort(folders, [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
			Sort(files, [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });

			for(int i = 0; i < folders.GetCount(); i++) {
				const DirEntry& f = folders[i];
				String child_rel = safe_rel.IsEmpty() ? f.name : (safe_rel + "/" + f.name);
				b << "<tr><td><a href=\"" << node_base_url << "/" << child_rel << "\" style=\"display:inline-flex; align-items:center; gap:8px; font-weight:600;\">"
				  << VividIcon::Get("folder", 16) << " " << f.name << "</a></td>"
				  << "<td style=\"color:var(--wf-fg-inactive,#8b919d);\">[dir]</td>"
				  << "<td><code>drwxr-xr-x</code></td><td>" << Format(f.last_write_time) << "</td></tr>\n";
			}
			for(int i = 0; i < files.GetCount(); i++) {
				const DirEntry& f = files[i];
				String child_rel = safe_rel.IsEmpty() ? f.name : (safe_rel + "/" + f.name);
				int64 sz = f.length;
				String sz_str = sz > 1048576 ? Format("%.1f MB", sz / 1048576.0) : (sz > 1024 ? Format("%.1f KB", sz / 1024.0) : AsString(sz) + " B");
				b << "<tr><td><a href=\"" << node_base_url << "/" << child_rel << "\" style=\"display:inline-flex; align-items:center; gap:8px;\">"
				  << VividIcon::Get("file", 16) << " " << f.name << "</a></td>"
				  << "<td>" << sz_str << "</td><td><code>-rw-r--r--</code></td><td>" << Format(f.last_write_time) << "</td></tr>\n";
			}
			b << "  </tbody></table>\n</div>\n";
		} else if(FileExists(full_path)) {
			int64 sz = GetFileLength(full_path);
			String sz_str = sz > 1048576 ? Format("%.1f MB", sz / 1048576.0) : (sz > 1024 ? Format("%.1f KB", sz / 1024.0) : AsString(sz) + " B");
			String content = LoadFile(full_path);
			bool is_markdown = full_path.EndsWith(".md") || full_path.EndsWith(".markdown") || full_path.EndsWith(".MD");

			b << "<div class=\"card\">\n"
			  << "  <div class=\"card-header\">\n"
			  << "    <span>" << VividIcon::Get("file-text", 18) << " <strong>" << GetFileName(full_path) << "</strong> (" << sz_str << ")</span>\n"
			  << "    <div style=\"display:flex; gap:8px; align-items:center;\">\n";

			if(is_markdown) {
				b << "      <div class=\"wf-view-toggle\">\n"
				  << "        <button type=\"button\" id=\"btn-view-preview\" class=\"active\" onclick=\"toggleFileView('preview')\">Preview</button>\n"
				  << "        <button type=\"button\" id=\"btn-view-code\" onclick=\"toggleFileView('code')\">Code</button>\n"
				  << "      </div>\n";
			}

			b << "      <a href=\"" << node_base_url << "/" << (safe_parts.GetCount() > 1 ? safe_rel.Left(safe_rel.ReverseFind('/')) : "") << "\" class=\"btn\" style=\"background:var(--wf-surface,#1e283b); color:#fff; font-size:0.75rem;\">Back</a>\n"
			  << "    </div>\n"
			  << "  </div>\n";

			if(is_markdown) {
				b << "  <div id=\"wf-blob-preview-box\" class=\"markdown-render-box\" style=\"padding:1.5rem; background:var(--wf-bg-view,#151d2b); border-radius:6px;\">\n"
				  << "    <textarea id=\"wf-blob-raw-md\" class=\"raw-markdown\" style=\"display:none;\">" << DeHtml(content) << "</textarea>\n"
				  << "    <div id=\"wf-blob-preview\" class=\"markdown-body\"></div>\n"
				  << "  </div>\n";
			}

			Vector<String> lines = Split(content, '\n', false);
			b << "  <div id=\"wf-blob-code-box\" style=\"" << (is_markdown ? "display:none; " : "") << "background:var(--wf-bg-view,#151d2b); padding:1rem; border-radius:6px; overflow-x:auto; font-family:monospace; font-size:0.85rem; line-height:1.5;\">\n"
			  << "    <table style=\"border-collapse:collapse; width:100%;\">\n";
			for(int i = 0; i < lines.GetCount(); i++) {
				b << "<tr><td style=\"width:45px; text-align:right; padding-right:12px; color:var(--wf-fg-inactive,#8b919d); user-select:none; border:none;\">" << (i + 1) << "</td>"
				  << "<td style=\"white-space:pre-wrap; border:none; color:var(--wf-fg-normal,#d3dae3);\">" << DeHtml(lines[i]) << "</td></tr>\n";
			}
			b << "    </table>\n  </div>\n</div>\n";
		} else {
			b << "<div class=\"card\"><p style=\"color:#ef4444;\">Path not found: " << DeHtml(safe_rel) << "</p></div>\n";
		}
	} else {
		// Remote SSH File & Directory Handling
		String user = node.ssh_user.IsEmpty() ? "sblo" : node.ssh_user;
		int port = node.ssh_port > 0 ? node.ssh_port : 22;
		String target_path = safe_rel.IsEmpty() ? "~" : ("~/" + safe_rel);

		String out;
		if(HostExec::SafeUser(user) && HostExec::SafeHost(node.hostname) && HostExec::SafeRemotePath(target_path)) {
			String script = "if [ -d " + target_path + " ]; then echo IS_DIR; find " + target_path + "/ -mindepth 1 -maxdepth 1 -printf '%y|%s|%T@|%f\\n'; "
			                "elif [ -f " + target_path + " ]; then echo IS_FILE; cat " + target_path + "; else echo NOT_FOUND; fi";
			Vector<String> ssh_args = HostExec::SshArgs(port, user, node.hostname, 3);
			ssh_args.Add(script);
			out = HostExec::Run("ssh", ssh_args, false);
		}
		Vector<String> lines = Split(out, '\n', false);

		if(lines.GetCount() > 0 && lines[0] == "IS_DIR") {
			b << "<div class=\"card\">\n"
			  << "  <div class=\"card-header\"><span>Contents on " << node.name << ": <code>~/" << safe_rel << "</code></span></div>\n"
			  << "  <table><thead><tr><th>Name</th><th>Size</th><th>Permissions</th><th>Type</th></tr></thead><tbody>\n";

			if(!safe_parts.IsEmpty()) {
				String parent_rel;
				for(int i = 0; i < safe_parts.GetCount() - 1; i++) {
					if(i > 0) parent_rel << "/";
					parent_rel << safe_parts[i];
				}
				b << "<tr><td colspan=\"4\"><a href=\"" << node_base_url << "/" << parent_rel << "\" style=\"display:inline-flex; align-items:center; gap:6px; font-weight:700;\">"
				  << VividIcon::Get("chevron-left", 14) << " .. (Parent Directory)</a></td></tr>\n";
			}

			Vector<DirEntry> folders, files;
			for(int i = 1; i < lines.GetCount(); i++) {
				Vector<String> p = Split(lines[i], '|');
				if(p.GetCount() >= 4 && p[3] != "." && p[3] != "..") {
					DirEntry e;
					e.name = p[3];
					e.length = ScanInt64(p[1]);
					e.is_folder = (p[0] == "d" || (p[0] == "l" && p[3].Find(".") < 0));
					if(e.is_folder) folders.Add(e);
					else files.Add(e);
				}
			}
			Sort(folders, [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
			Sort(files, [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });

			for(int i = 0; i < folders.GetCount(); i++) {
				const DirEntry& f = folders[i];
				String child_rel = safe_rel.IsEmpty() ? f.name : (safe_rel + "/" + f.name);
				b << "<tr><td><a href=\"" << node_base_url << "/" << child_rel << "\" style=\"display:inline-flex; align-items:center; gap:8px; font-weight:600;\">"
				  << VividIcon::Get("folder", 16) << " " << f.name << "</a></td>"
				  << "<td style=\"color:var(--wf-fg-inactive,#8b919d);\">[dir]</td>"
				  << "<td><code>drwxr-xr-x</code></td><td>Folder</td></tr>\n";
			}
			for(int i = 0; i < files.GetCount(); i++) {
				const DirEntry& f = files[i];
				String child_rel = safe_rel.IsEmpty() ? f.name : (safe_rel + "/" + f.name);
				int64 sz = f.length;
				String sz_str = sz > 1048576 ? Format("%.1f MB", sz / 1048576.0) : (sz > 1024 ? Format("%.1f KB", sz / 1024.0) : AsString(sz) + " B");
				b << "<tr><td><a href=\"" << node_base_url << "/" << child_rel << "\" style=\"display:inline-flex; align-items:center; gap:8px;\">"
				  << VividIcon::Get("file", 16) << " " << f.name << "</a></td>"
				  << "<td>" << sz_str << "</td><td><code>-rw-r--r--</code></td><td>File</td></tr>\n";
			}
			b << "  </tbody></table>\n</div>\n";
		} else if(lines.GetCount() > 0 && lines[0] == "IS_FILE") {
			bool is_markdown = safe_rel.EndsWith(".md") || safe_rel.EndsWith(".markdown") || safe_rel.EndsWith(".MD");
			String remote_file_content;
			for(int i = 1; i < lines.GetCount(); i++) {
				if(i > 1) remote_file_content << "\n";
				remote_file_content << lines[i];
			}

			b << "<div class=\"card\">\n"
			  << "  <div class=\"card-header\">\n"
			  << "    <span>" << VividIcon::Get("file-text", 18) << " <strong>" << GetFileName(safe_rel) << "</strong> on " << node.name << "</span>\n"
			  << "    <div style=\"display:flex; gap:8px; align-items:center;\">\n";

			if(is_markdown) {
				b << "      <div class=\"wf-view-toggle\">\n"
				  << "        <button type=\"button\" id=\"btn-view-preview\" class=\"active\" onclick=\"toggleFileView('preview')\">Preview</button>\n"
				  << "        <button type=\"button\" id=\"btn-view-code\" onclick=\"toggleFileView('code')\">Code</button>\n"
				  << "      </div>\n";
			}

			b << "      <a href=\"" << node_base_url << "/" << (safe_parts.GetCount() > 1 ? safe_rel.Left(safe_rel.ReverseFind('/')) : "") << "\" class=\"btn\" style=\"background:var(--wf-surface,#1e283b); color:#fff; font-size:0.75rem;\">Back</a>\n"
			  << "    </div>\n"
			  << "  </div>\n";

			if(is_markdown) {
				b << "  <div id=\"wf-blob-preview-box\" class=\"markdown-render-box\" style=\"padding:1.5rem; background:var(--wf-bg-view,#151d2b); border-radius:6px;\">\n"
				  << "    <textarea id=\"wf-blob-raw-md\" class=\"raw-markdown\" style=\"display:none;\">" << DeHtml(remote_file_content) << "</textarea>\n"
				  << "    <div id=\"wf-blob-preview\" class=\"markdown-body\"></div>\n"
				  << "  </div>\n";
			}

			b << "  <div id=\"wf-blob-code-box\" style=\"" << (is_markdown ? "display:none; " : "") << "background:var(--wf-bg-view,#151d2b); padding:1rem; border-radius:6px; overflow-x:auto; font-family:monospace; font-size:0.85rem; line-height:1.5;\">\n"
			  << "    <table style=\"border-collapse:collapse; width:100%;\">\n";
			for(int i = 1; i < lines.GetCount(); i++) {
				b << "<tr><td style=\"width:45px; text-align:right; padding-right:12px; color:var(--wf-fg-inactive,#8b919d); user-select:none; border:none;\">" << i << "</td>"
				  << "<td style=\"white-space:pre-wrap; border:none; color:var(--wf-fg-normal,#d3dae3);\">" << DeHtml(lines[i]) << "</td></tr>\n";
			}
			b << "    </table>\n  </div>\n</div>\n";
		} else {
			b << "<div class=\"card\"><p style=\"color:#ef4444;\">Path not found on remote node: " << DeHtml(safe_rel) << "</p></div>\n";
		}
	}

	return b;
}

END_UPP_NAMESPACE
