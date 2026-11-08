#include "ManagerSkylark.h"
#include "HostPlatform.h"
#ifdef PLATFORM_POSIX
#include <sys/statvfs.h>
#endif

NAMESPACE_UPP

String HostPlatformService::RenderRepos(Http& http, const String& node_id)
{
	DistributedNode node = GetNode(node_id);
	Vector<HostRepoInfo> repos = GetRepositoriesForNode(node.id);

	String b;
	b << RenderSubNav("repos", node.id);

	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>Repositories on " << node.name << " (" << node.hostname << ")</span>\n"
	  << "    <span class=\"badge-v2\">" << repos.GetCount() << " detected in " << node.remote_path << "</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem; margin-bottom:1rem;\">"
	  << "    Git repositories residing on " << node.name << ". Equal parity repository browser: jump into files, check branches, inspect commits, or execute remote SSH git."
	  << "  </p>\n"
	  << "  <table><thead><tr><th>Repository Name</th><th>Git Branch</th><th>Commit Hash &amp; Message</th><th>Packages</th><th>Actions</th></tr></thead><tbody>\n";

	for(int i = 0; i < repos.GetCount(); i++) {
		const HostRepoInfo& r = repos[i];
		String files_url = node.is_local ? ("/host/files/Dev/" + r.name) : ("/node/" + node.id + "/files/Dev/" + r.name);
		b << "<tr id=\"repo-" << r.name << "\">"
		  << "<td><strong><a href=\"" << files_url << "\">" << r.name << "</a></strong></td>"
		  << "<td>"
		  << (r.has_git ? (String("<span style=\"display:inline-flex; align-items:center; gap:4px; font-weight:600; color:var(--wf-accent-focus,#00a1c5);\">") + VividIcon::Get("git-branch", 14) + " " + (r.branch.IsEmpty() ? String("master") : r.branch) + "</span>") : "<span style=\"color:#64748b;\">no git</span>")
		  << "</td>"
		  << "<td><code style=\"color:var(--wf-fg-inactive,#8b919d);\">" << (r.last_commit_hash.IsEmpty() ? "-" : r.last_commit_hash) << "</code> "
		  << "<small style=\"color:var(--wf-fg-normal,#d3dae3);\">" << DeHtml(r.last_commit_msg) << "</small></td>"
		  << "<td>"
		  << (r.upp_packages.IsEmpty() ? "<span style=\"color:#64748b;\">-</span>" : ("<span class=\"badge-v2\" style=\"background:#6d55aa;\">" + AsString(r.upp_packages.GetCount()) + " upp</span>"))
		  << "</td>"
		  << "<td>"
		  << "  <div style=\"display:inline-flex; gap:6px;\">"
		  << "    <a href=\"" << files_url << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Files</a>"
		  << "    <a href=\"/repo/" << (r.owner.IsEmpty() ? "SeppoPakonen" : r.owner) << "/" << r.name << "\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:#24292f; color:#ffffff;\" title=\"GitHub\">GitHub</a>"
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
	b << RenderSubNav("nodes", "local-workstation");

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
	  << "      <button onclick=\"probeAllNodes()\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:#fff;\">" << VividIcon::Get("refresh-cw", 14) << " Probe All Nodes</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Mesh Nodes Grid Cards
	b << "<div style=\"display:grid; grid-template-columns:repeat(auto-fill, minmax(340px, 1fr)); gap:1rem; margin-bottom:1.5rem;\">\n";
	for(int i = 0; i < nodes.GetCount(); i++) {
		const DistributedNode& n = nodes[i];
		bool online = (n.status == "online");
		String status_color = online ? "var(--wf-color-positive,#00aa7f)" : "#ef4444";
		String node_overview_url = n.is_local ? "/host/overview" : ("/node/" + n.id + "/overview");

		b << "<div class=\"card\" id=\"node-card-" << n.id << "\" style=\"border-top:3px solid " << (online ? "var(--wf-accent-focus,#00a1c5)" : "#ef4444") << "; margin-bottom:0;\">\n"
		  << "  <div style=\"display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:0.75rem;\">\n"
		  << "    <div>\n"
		  << "      <div style=\"font-weight:700; font-size:1.05rem; display:flex; align-items:center; gap:6px;\">"
		  << VividIcon::Get(n.is_local ? "monitor" : "server", 18) << " <a href=\"" << node_overview_url << "\" style=\"text-decoration:none;\">" << n.name << "</a></div>\n"
		  << "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\"><code>" << n.hostname << "</code> (" << n.id << ")</div>\n"
		  << "    </div>\n"
		  << "    <div style=\"display:flex; flex-direction:column; align-items:flex-end; gap:4px;\">\n"
		  << "      <span class=\"badge-v2\" style=\"background:" << status_color << "; font-size:0.75rem;\">" << ToUpper(n.status) << "</span>\n"
		  << "      <span class=\"badge-v2\" style=\"background:#1e283b; color:#38bdf8; font-size:0.7rem; border:1px solid rgba(56,189,248,0.2);\">" << n.latency_ms << " ms</span>\n"
		  << "    </div>\n"
		  << "  </div>\n"
		  << "  <div style=\"font-size:0.85rem; line-height:1.6; color:var(--wf-fg-normal,#d3dae3); margin-bottom:0.75rem;\">\n"
		  << "    <div><strong>Protocol:</strong> <span class=\"badge-v2\" style=\"background:#24292f; color:#a78bfa; font-size:0.7rem;\">" << ToUpper(n.protocol) << "</span> "
		  << "      (" << n.ssh_user << ":" << n.ssh_port << ")</div>\n"
		  << "    <div><strong>Path:</strong> <code>" << n.remote_path << "</code></div>\n"
		  << "    <div><strong>Kernel:</strong> " << (n.os_info.IsEmpty() ? "Linux x86_64" : n.os_info) << "</div>\n"
		  << "    <div><strong>Uptime:</strong> " << (n.uptime.IsEmpty() ? "Up" : n.uptime) << "</div>\n"
		  << "    <div><strong>Load Avg:</strong> " << (n.load_avg.IsEmpty() ? "0.00" : n.load_avg) << "</div>\n"
		  << "    <div><strong>Repositories:</strong> " << n.repo_count << " scanned</div>\n"
		  << "  </div>\n"
		  << "  <div style=\"display:flex; gap:6px; border-top:1px solid var(--wf-border-color, rgba(255,255,255,0.08)); padding-top:0.6rem;\">\n"
		  << "    <a href=\"" << node_overview_url << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Open Node</a>\n"
		  << "    <button onclick=\"probeSingleNode('" << n.id << "')\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Probe</button>\n"
		  << "    <button onclick=\"selectRemoteGitNode('" << n.id << "')\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Run Git</button>\n"
		  << "  </div>\n"
		  << "</div>\n";
	}
	b << "</div>\n";

	// Fast Remote Git Box
	b << "<div class=\"card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("git-branch", 18) << " Fast Remote Git Command Runner (Low-Latency SSH)</span>\n"
	  << "    <span class=\"badge-v2\" style=\"background:#38bdf8; color:#0f172a;\">~50ms Direct SSH</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem; margin-bottom:1rem;\">"
	  << "    Executes git operations directly on remote mesh node RAM/CPU over persistent SSH keys. Sub-second response without filesystem mount overhead."
	  << "  </p>\n"
	  << "  <div style=\"display:grid; grid-template-columns: 1fr 1fr 2fr auto; gap:10px; align-items:end; margin-bottom:1rem;\">\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Target Mesh Node</label>\n"
	  << "      <select id=\"git-node-select\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n";

	for(int i = 0; i < nodes.GetCount(); i++) {
		b << "        <option value=\"" << nodes[i].id << "\">" << nodes[i].name << " (" << nodes[i].hostname << ")</option>\n";
	}

	b << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Repository</label>\n"
	  << "      <input type=\"text\" id=\"git-repo-input\" value=\"ai-upp\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Git Command / Arguments</label>\n"
	  << "      <div style=\"display:flex; gap:6px;\">\n"
	  << "        <input type=\"text\" id=\"git-args-input\" value=\"status -s\" class=\"form-control\" style=\"flex:1; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "        <button onclick=\"setGitCmd('status -s')\" class=\"btn\" style=\"padding:4px 8px; font-size:0.75rem; background:#24292f; color:#fff;\">status</button>\n"
	  << "        <button onclick=\"setGitCmd('log -5 --oneline')\" class=\"btn\" style=\"padding:4px 8px; font-size:0.75rem; background:#24292f; color:#fff;\">log</button>\n"
	  << "        <button onclick=\"setGitCmd('branch -a')\" class=\"btn\" style=\"padding:4px 8px; font-size:0.75rem; background:#24292f; color:#fff;\">branch</button>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button id=\"btn-exec-git\" onclick=\"runRemoteGit()\" class=\"btn btn-primary\" style=\"padding:8px 16px; font-weight:700;\">Execute SSH Git</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <pre id=\"git-terminal-output\" style=\"background:#0b0f19; color:#38bdf8; padding:1rem; border-radius:6px; font-family:monospace; font-size:0.85rem; min-height:80px; max-height:360px; overflow-y:auto; border:1px solid #1e293b;\">Ready for remote execution. Select node and press 'Execute SSH Git'.</pre>\n"
	  << "</div>\n";

	// Mesh Synchronization Section
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("refresh-cw", 18) << " Peer-to-Peer Mesh Sync Runner</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem; margin-bottom:1rem;\">"
	  << "    Sync source repository workspace between two nodes across the mesh over SSH rsync."
	  << "  </p>\n"
	  << "  <div style=\"display:grid; grid-template-columns: 1fr 1fr 1fr auto; gap:10px; align-items:end; margin-bottom:1rem;\">\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Source Node</label>\n"
	  << "      <select id=\"sync-src-select\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n";
	for(int i = 0; i < nodes.GetCount(); i++)
		b << "        <option value=\"" << nodes[i].id << "\"" << (nodes[i].is_local ? " selected" : "") << ">" << nodes[i].name << " (" << nodes[i].hostname << ")</option>\n";
	b << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Target Node</label>\n"
	  << "      <select id=\"sync-dst-select\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n";
	for(int i = 0; i < nodes.GetCount(); i++)
		b << "        <option value=\"" << nodes[i].id << "\"" << (!nodes[i].is_local && i == 1 ? " selected" : "") << ">" << nodes[i].name << " (" << nodes[i].hostname << ")</option>\n";
	b << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Repository</label>\n"
	  << "      <input type=\"text\" id=\"sync-repo-input\" value=\"ai-upp\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button onclick=\"runMeshSync()\" class=\"btn btn-primary\" style=\"padding:8px 16px; font-weight:700;\">Trigger Sync</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <pre id=\"sync-terminal-output\" style=\"background:#0b0f19; color:#34d399; padding:1rem; border-radius:6px; font-family:monospace; font-size:0.85rem; min-height:60px; max-height:300px; overflow-y:auto; border:1px solid #1e293b;\">No active sync running.</pre>\n"
	  << "</div>\n";

	// JavaScript controllers
	b << "<script>\n"
	  << "function setGitCmd(cmd) { document.getElementById('git-args-input').value = cmd; }\n"
	  << "function selectRemoteGitNode(id) { document.getElementById('git-node-select').value = id; document.getElementById('git-args-input').focus(); }\n"
	  << "async function runRemoteGit() {\n"
	  << "  const out = document.getElementById('git-terminal-output');\n"
	  << "  const btn = document.getElementById('btn-exec-git');\n"
	  << "  const node = document.getElementById('git-node-select').value;\n"
	  << "  const repo = document.getElementById('git-repo-input').value;\n"
	  << "  const args = document.getElementById('git-args-input').value;\n"
	  << "  out.textContent = 'Running: git -C ' + repo + ' ' + args + ' on ' + node + '...';\n"
	  << "  btn.disabled = true;\n"
	  << "  try {\n"
	  << "    const resp = await fetch('/api/host/remotegit', {\n"
	  << "      method: 'POST',\n"
	  << "      headers: {'Content-Type': 'application/json'},\n"
	  << "      body: JSON.stringify({ node_id: node, repo: repo, args: args })\n"
	  << "    });\n"
	  << "    const data = await resp.json();\n"
	  << "    out.textContent = data.output || '(No output returned)';\n"
	  << "  } catch(e) {\n"
	  << "    out.textContent = 'Error: ' + e;\n"
	  << "  } finally { btn.disabled = false; }\n"
	  << "}\n"
	  << "async function probeSingleNode(id) {\n"
	  << "  try {\n"
	  << "    const resp = await fetch('/api/host/nodes/probe/' + encodeURIComponent(id));\n"
	  << "    const data = await resp.json();\n"
	  << "    alert('Node ' + id + ' probe: status=' + data.status + ', latency=' + data.latency_ms + 'ms, repos=' + data.repo_count);\n"
	  << "    location.reload();\n"
	  << "  } catch(e) { alert('Probe error: ' + e); }\n"
	  << "}\n"
	  << "async function probeAllNodes() {\n"
	  << "  try {\n"
	  << "    await fetch('/api/host/nodes?probe=1');\n"
	  << "    location.reload();\n"
	  << "  } catch(e) { alert('Probe error: ' + e); }\n"
	  << "}\n"
	  << "async function runMeshSync() {\n"
	  << "  const out = document.getElementById('sync-terminal-output');\n"
	  << "  const src = document.getElementById('sync-src-select').value;\n"
	  << "  const dst = document.getElementById('sync-dst-select').value;\n"
	  << "  const repo = document.getElementById('sync-repo-input').value;\n"
	  << "  out.textContent = 'Triggering sync: ' + src + ' -> ' + dst + ' (' + repo + ')...';\n"
	  << "  try {\n"
	  << "    const resp = await fetch('/api/host/sync', {\n"
	  << "      method: 'POST',\n"
	  << "      headers: {'Content-Type': 'application/json'},\n"
	  << "      body: JSON.stringify({ source: src, target: dst, repo: repo })\n"
	  << "    });\n"
	  << "    const data = await resp.json();\n"
	  << "    out.textContent = data.output || 'Sync finished with no output.';\n"
	  << "  } catch(e) { out.textContent = 'Sync error: ' + e; }\n"
	  << "}\n"
	  << "</script>\n";

	return b;
}

String HostPlatformService::RenderFleet(Http& http, const String& node_id)
{
	DistributedNode active_node = GetNode(node_id);
	Vector<FleetGitNodeStatus> fleet = GetFleetGitStatus("ai-upp");
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
	  << "      <button id=\"btn-fleet-sync-all\" onclick=\"runFleetSync('git_pull', 'all')\" class=\"btn btn-primary\" style=\"display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("refresh-cw", 14) << " 1-Click Sync All Fleet</button>\n"
	  << "      <button id=\"btn-fleet-fetch-all\" onclick=\"runFleetSync('git_fetch', 'all')\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:#fff; display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("download", 14) << " SSH Fetch All</button>\n"
	  << "      <button id=\"btn-fleet-refresh-status\" onclick=\"refreshFleetStatus()\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:#38bdf8; display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("activity", 14) << " Refresh Status</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Card 1: Multi-Node Git Status Matrix
	b << "<div class=\"card\" id=\"fleet-git-matrix\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("git-branch", 18) << " Fleet Git Synchronization Matrix (ai-upp)</span>\n"
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

		b << "      <tr id=\"fleet-node-row-" << s.node_id << "\">\n"
		  << "        <td>\n"
		  << "          <div style=\"font-weight:700; color:var(--wf-fg-normal,#fff);\">" << s.name << "</div>\n"
		  << "          <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\"><code>" << s.hostname << "</code> &bull; " << s.latency_ms << " ms</div>\n"
		  << "        </td>\n"
		  << "        <td><code style=\"color:var(--wf-accent-focus,#00a1c5);\">" << s.branch << "</code></td>\n"
		  << "        <td>\n"
		  << "          <div><code style=\"color:#38bdf8;\">" << s.commit_hash << "</code> <span style=\"font-size:0.85rem;\">" << s.commit_msg << "</span></div>\n"
		  << "          <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\">" << s.commit_time << "</div>\n"
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

		if(s.node_id == "local-workstation") {
			b << "          <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">Primary Master</span>\n";
		} else {
			b << "          <button id=\"btn-fleet-sync-" << s.node_id << "\" onclick=\"runFleetSync('git_pull', '" << s.node_id << "')\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:3px 8px;\">Pull</button>\n"
			  << "          <button id=\"btn-fleet-fetch-" << s.node_id << "\" onclick=\"runFleetSync('git_fetch', '" << s.node_id << "')\" class=\"btn\" style=\"font-size:0.75rem; padding:3px 8px; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Fetch</button>\n"
			  << "          <button id=\"btn-fleet-rsync-" << s.node_id << "\" onclick=\"runFleetSync('rsync', '" << s.node_id << "')\" class=\"btn\" style=\"font-size:0.75rem; padding:3px 8px; background:var(--wf-bg-alt,#1c202f); color:#38bdf8;\">Rsync</button>\n";
		}

		b << "        </td>\n"
		  << "      </tr>\n";
	}

	b << "    </tbody>\n"
	  << "  </table>\n"
	  << "  <div style=\"margin-top:1rem;\">\n"
	  << "    <div style=\"font-size:0.85rem; font-weight:700; color:var(--wf-fg-normal,#fff); margin-bottom:6px; display:flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("terminal", 14) << " Fleet Sync Stream Console</div>\n"
	  << "    <pre id=\"fleet-sync-terminal\" style=\"background:#0b0f19; color:#34d399; padding:1rem; border-radius:6px; font-family:monospace; font-size:0.85rem; min-height:80px; max-height:260px; overflow-y:auto; border:1px solid #1e293b;\">Ready for fleet operations. Click '1-Click Sync All Fleet' or individual node actions above.</pre>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Card 2: Remote Compilation & Build Dispatcher
	b << "<div class=\"card\" id=\"fleet-build-card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("cpu", 18) << " Remote Build &amp; Test Dispatcher</span>\n"
	  << "    <span class=\"badge-v2\" style=\"background:#10b981;\">Direct SSH umk / build</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem; margin-bottom:1rem;\">"
	  << "    Dispatch compilation commands (<code>./bin/build &lt;package&gt;</code>) to any remote cluster machine over persistent SSH. View real-time terminal output and build results."
	  << "  </p>\n"
	  << "  <div style=\"display:grid; grid-template-columns: 1.5fr 2fr 1fr 1fr auto auto; gap:10px; align-items:end; margin-bottom:1rem;\">\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Target Cluster Node</label>\n"
	  << "      <select id=\"fleet-build-node\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n";

	for(int i = 0; i < all_nodes.GetCount(); i++) {
		b << "        <option value=\"" << all_nodes[i].id << "\"" << (all_nodes[i].id == active_node.id ? " selected" : "") << ">"
		  << all_nodes[i].name << " (" << all_nodes[i].hostname << ")</option>\n";
	}

	b << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Target Package</label>\n"
	  << "      <input type=\"text\" id=\"fleet-build-pkg\" value=\"reference/ManagerSkylark\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Compiler Method</label>\n"
	  << "      <select id=\"fleet-build-method\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "        <option value=\"CLANG\" selected>CLANG</option>\n"
	  << "        <option value=\"GCC\">GCC</option>\n"
	  << "        <option value=\"MSVS\">MSVS</option>\n"
	  << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Build Flags</label>\n"
	  << "      <input type=\"text\" id=\"fleet-build-flags\" value=\"-r\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button id=\"btn-fleet-build-run\" onclick=\"dispatchFleetBuild(false)\" class=\"btn btn-primary\" style=\"padding:8px 14px; font-weight:700;\">Dispatch Build</button>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button id=\"btn-fleet-build-clean\" onclick=\"dispatchFleetBuild(true)\" class=\"btn\" style=\"padding:8px 14px; font-weight:700; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Clean &amp; Build</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <div id=\"fleet-build-status-bar\" style=\"display:none; margin-bottom:0.75rem; font-size:0.85rem; font-weight:600; padding:6px 12px; border-radius:4px;\"></div>\n"
	  << "  <pre id=\"fleet-build-terminal\" style=\"background:#0b0f19; color:#38bdf8; padding:1rem; border-radius:6px; font-family:monospace; font-size:0.85rem; min-height:100px; max-height:400px; overflow-y:auto; border:1px solid #1e293b;\">Build dispatcher ready. Select target cluster node and click 'Dispatch Build'.</pre>\n"
	  << "</div>\n";

	// Injected JavaScript
	b << "<script>\n"
	  << "async function runFleetSync(mode, target_node) {\n"
	  << "  const term = document.getElementById('fleet-sync-terminal');\n"
	  << "  term.textContent = 'Executing fleet sync (mode: ' + mode + ', target: ' + target_node + ')...';\n"
	  << "  try {\n"
	  << "    const resp = await fetch('/api/host/fleet/sync', {\n"
	  << "      method: 'POST',\n"
	  << "      headers: { 'Content-Type': 'application/json' },\n"
	  << "      body: JSON.stringify({ mode: mode, target_node: target_node, repo: 'ai-upp' })\n"
	  << "    });\n"
	  << "    const data = await resp.json();\n"
	  << "    term.textContent = data.output || 'Fleet sync completed with no output.';\n"
	  << "  } catch(e) { term.textContent = 'Fleet sync failed: ' + e; }\n"
	  << "}\n"
	  << "async function dispatchFleetBuild(is_clean) {\n"
	  << "  const term = document.getElementById('fleet-build-terminal');\n"
	  << "  const sbar = document.getElementById('fleet-build-status-bar');\n"
	  << "  const btnRun = document.getElementById('btn-fleet-build-run');\n"
	  << "  const node = document.getElementById('fleet-build-node').value;\n"
	  << "  const pkg = document.getElementById('fleet-build-pkg').value;\n"
	  << "  const method = document.getElementById('fleet-build-method').value;\n"
	  << "  let flags = document.getElementById('fleet-build-flags').value;\n"
	  << "  if (is_clean && !flags.includes('-c')) flags = '-c ' + flags;\n"
	  << "  term.textContent = 'Dispatching build to node [' + node + ']: ./bin/build ' + pkg + ' -m ' + method + ' ' + flags + '...';\n"
	  << "  sbar.style.display = 'block';\n"
	  << "  sbar.style.background = '#1e293b';\n"
	  << "  sbar.style.color = '#38bdf8';\n"
	  << "  sbar.textContent = 'Building on ' + node + '...';\n"
	  << "  btnRun.disabled = true;\n"
	  << "  try {\n"
	  << "    const resp = await fetch('/api/host/fleet/build', {\n"
	  << "      method: 'POST',\n"
	  << "      headers: { 'Content-Type': 'application/json' },\n"
	  << "      body: JSON.stringify({ node_id: node, package: pkg, method: method, flags: flags })\n"
	  << "    });\n"
	  << "    const data = await resp.json();\n"
	  << "    term.textContent = data.output || '(No build output returned)';\n"
	  << "    if (data.success) {\n"
	  << "      sbar.style.background = '#064e3b';\n"
	  << "      sbar.style.color = '#34d399';\n"
	  << "      sbar.textContent = 'Build SUCCESS on ' + node + ' in ' + data.elapsed_ms + 'ms (exit code 0)';\n"
	  << "    } else {\n"
	  << "      sbar.style.background = '#7f1d1d';\n"
	  << "      sbar.style.color = '#fca5a5';\n"
	  << "      sbar.textContent = 'Build FAILED on ' + node + ' in ' + data.elapsed_ms + 'ms (exit code ' + data.exit_code + ')';\n"
	  << "    }\n"
	  << "  } catch(e) {\n"
	  << "    term.textContent = 'Build dispatch error: ' + e;\n"
	  << "    sbar.style.background = '#7f1d1d';\n"
	  << "    sbar.style.color = '#fca5a5';\n"
	  << "    sbar.textContent = 'Network or dispatch error: ' + e;\n"
	  << "  } finally { btnRun.disabled = false; }\n"
	  << "}\n"
	  << "function refreshFleetStatus() {\n"
	  << "  location.reload();\n"
	  << "}\n"
	  << "</script>\n";

	return b;
}

String HostPlatformService::RenderSettings(Http& http)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);

	String b;
	b << RenderSubNav("settings", "local-workstation");

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
	  << "        <input type=\"text\" name=\"hostname\" placeholder=\"e.g. 192.168.1.108\" required class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">SSH Username</label>\n"
	  << "        <input type=\"text\" name=\"ssh_user\" value=\"sblo\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">SSH Port</label>\n"
	  << "        <input type=\"number\" name=\"ssh_port\" value=\"22\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Remote Dev Workspace Path</label>\n"
	  << "        <input type=\"text\" name=\"remote_path\" value=\"~/Dev\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
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
		String node_url = n.is_local ? "/host/overview" : ("/node/" + n.id + "/overview");
		b << "<tr>"
		  << "<td><strong><a href=\"" << node_url << "\">" << n.name << "</a></strong><br><small style=\"color:#64748b;\">" << n.id << "</small></td>"
		  << "<td><code>" << n.hostname << "</code></td>"
		  << "<td><code>" << n.ssh_user << ":" << n.ssh_port << "</code></td>"
		  << "<td><code>" << n.remote_path << "</code></td>"
		  << "<td><span class=\"badge-v2\" style=\"background:" << (online ? "var(--wf-color-positive,#00aa7f)" : "#ef4444") << ";\">" << ToUpper(n.status) << "</span></td>"
		  << "<td>" << n.latency_ms << " ms</td>"
		  << "<td>\n"
		  << "  <div style=\"display:inline-flex; gap:6px;\">\n"
		  << "    <a href=\"" << node_url << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Open</a>\n"
		  << "    <a href=\"/api/host/nodes/probe/" << n.id << "\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Probe</a>\n";

		if(!n.is_local) {
			b << "    <form method=\"POST\" action=\"/host/settings/remove\" style=\"margin:0; display:inline;\">\n"
			  << "      <input type=\"hidden\" name=\"id\" value=\"" << n.id << "\">\n"
			  << "      <button type=\"submit\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:#7f1d1d; color:#fca5a5;\" onclick=\"return confirm('Remove node " << n.name << "?');\">Delete</button>\n"
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
	  << "      <div style=\"font-weight:700;\">Reset Default Mesh Fleet</div>\n"
	  << "      <p style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d); margin:4px 0 0 0;\">Restore default verified triple nodes: 192.168.1.106 (Workstation), 192.168.1.105 (MediaCenter), 192.168.1.169 (Gen7).</p>\n"
	  << "    </div>\n"
	  << "    <form method=\"POST\" action=\"/host/settings/reset\" style=\"margin:0;\">\n"
	  << "      <button type=\"submit\" class=\"btn\" style=\"background:#2e1065; color:#c4b5fd; font-weight:600; padding:6px 14px;\">Restore Verified Fleet</button>\n"
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
	  << "  <div class=\"card-header\"><span>Storage Mounts &amp; Disk Usage (" << node.name << ")</span></div>\n"
	  << "  <table><thead><tr><th>Mount Point</th><th>Total Size</th><th>Used</th><th>Free Space</th><th>Usage Bar</th></tr></thead><tbody>\n";

	if(node.is_local || node.hostname == "192.168.1.106") {
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
		String user = node.ssh_user.IsEmpty() ? "sblo" : node.ssh_user;
		int port = node.ssh_port > 0 ? node.ssh_port : 22;
		String cmd = Format("ssh -o BatchMode=yes -o ConnectTimeout=3 -p %d %s@%s \"df -k -P\"",
		                    port, user, node.hostname);
		String out = Sys("sh", {"-c", cmd});
		Vector<String> lines = Split(out, '\n');

		for(int i = 1; i < lines.GetCount(); i++) {
			String line = lines[i];
			Vector<String> parts = Split(line, [](int c)->int { return c == ' ' || c == '\t'; });
			if(parts.GetCount() >= 6) {
				String fs = parts[0];
				double total_kb = ScanDouble(parts[1]);
				double used_kb = ScanDouble(parts[2]);
				double free_kb = ScanDouble(parts[3]);
				String mount = parts[5];

				if(total_kb > 100000) { // filter tiny devfs
					double total_gb = total_kb / (1024.0 * 1024.0);
					double used_gb = used_kb / (1024.0 * 1024.0);
					double free_gb = free_kb / (1024.0 * 1024.0);
					double pct = (total_gb > 0) ? (used_gb / total_gb * 100.0) : 0.0;

					b << "<tr>"
					  << "<td><code>" << mount << "</code> <small style=\"color:#64748b;\">(" << fs << ")</small></td>"
					  << "<td>" << Format("%.1f GB", total_gb) << "</td>"
					  << "<td>" << Format("%.1f GB", used_gb) << "</td>"
					  << "<td><strong style=\"color:#34d399;\">" << Format("%.1f GB", free_gb) << "</strong></td>"
					  << "<td style=\"width:220px;\">" << ManagerSkylarkApp::RenderProgressBar(pct) << "</td>"
					  << "</tr>\n";
				}
			}
		}
	}

	b << "  </tbody></table>\n</div>\n";
	return b;
}

END_UPP_NAMESPACE
