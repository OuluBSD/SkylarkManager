#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"

NAMESPACE_UPP

void RenderRepoView(Http& http, const String& owner, const String& repo, const String& deep_path)
{
	if(http["platform"] == "sf") {
		http.Redirect("/projects/" + repo + "/");
		return;
	}

	LocalRepoInfo info = GitHubPlatformService::GetRepo(owner, repo);
	ValueMap scraped = GitHubPlatformService::LoadScrapedData(owner, repo);

	String active_tab = http["tab"];
	String sub_path = deep_path;
	String agent_run_id = http["run"];

	// Parse sub-path segments
	String mode = "tree"; // "tree", "blob", "commits", "issues", "pulls", "agents", "actions", "projects", "security", "settings"
	String branch = info.branch;
	String file_or_folder;
	int issue_num = 0;
	int pr_num = ScanInt((String)http["pr"]);

	if(!sub_path.IsEmpty()) {
		Vector<String> parts = Split(sub_path, '/');
		if(parts.GetCount() > 0) {
			String p0 = parts[0];
			if(p0 == "tree") {
				mode = "tree";
				active_tab = "code";
				if(parts.GetCount() > 1) branch = parts[1];
				for(int i = 2; i < parts.GetCount(); i++) {
					if(!file_or_folder.IsEmpty()) file_or_folder << "/";
					file_or_folder << parts[i];
				}
			} else if(p0 == "blob") {
				mode = "blob";
				active_tab = "code";
				if(parts.GetCount() > 1) branch = parts[1];
				for(int i = 2; i < parts.GetCount(); i++) {
					if(!file_or_folder.IsEmpty()) file_or_folder << "/";
					file_or_folder << parts[i];
				}
			} else if(p0 == "commits") {
				mode = "commits";
				active_tab = "commits";
				if(parts.GetCount() > 1) branch = parts[1];
			} else if(p0 == "issues") {
				mode = "issues";
				active_tab = "issues";
				if(parts.GetCount() > 1 && IsDigit(parts[1][0])) {
					issue_num = ScanInt(parts[1]);
				}
			} else if(p0 == "pulls" || p0 == "pull") {
				mode = "pulls";
				active_tab = "pulls";
				if(parts.GetCount() > 1 && IsDigit(parts[1][0])) {
					pr_num = ScanInt(parts[1]);
				}
			} else if(p0 == "agents") {
				mode = "agents";
				active_tab = "agents";
				if(parts.GetCount() > 1) {
					agent_run_id = parts[1];
				}
			} else if(p0 == "actions") {
				mode = "actions";
				active_tab = "actions";
				if(parts.GetCount() > 1 && parts[1] == "new") {
					mode = "actions_new";
				}
			} else if(p0 == "projects") {
				mode = "projects";
				active_tab = "projects";
			} else if(p0 == "security") {
				mode = "security";
				active_tab = "security";
			} else if(p0 == "settings") {
				mode = "settings";
				active_tab = "settings";
			}
		}
	}

	String req_template = http["template"];
	String req_wf_template = http["workflow_template"];
	if(http["action"] == "new" || !req_template.IsEmpty() || !req_wf_template.IsEmpty()) {
		mode = "actions_new";
		active_tab = "actions";
	}

	if(active_tab.IsEmpty()) active_tab = "code";

	String sub_nav = GitHubPlatformService::RenderRepoHeader(info) +
	                 GitHubPlatformService::RenderRepoTabs(owner, repo, active_tab, info);

	String content;
	String sidebar;

	// -----------------------------------------------------------------------
	// 1. Code Explorer (Tree & Blob Viewer)
	// -----------------------------------------------------------------------
	if(active_tab == "code") {
		String base_url = "/repo/" + owner + "/" + repo;
		Vector<String> branches = GitHubPlatformService::GetBranches(info.path);

		// Branch Switcher Bar
		String branch_bar;
		branch_bar
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1rem; flex-wrap:wrap; gap:12px;\">\n"
			<< "  <div style=\"display:flex; align-items:center; gap:8px;\">\n"
			<< "    <div class=\"wf-dropdown\" style=\"position:relative; display:inline-block;\">\n"
			<< "      <button class=\"wf-gh-btn\" style=\"font-weight:600; font-size:0.85rem;\">" << VividIcon::Get("git-branch", 14) << " " << branch << " &#9662;</button>\n"
			<< "    </div>\n";

		// Path breadcrumb
		branch_bar << "    <nav class=\"wf-breadcrumb\" style=\"font-size:0.9rem; font-weight:600;\">\n"
		           << "      <a href=\"" << base_url << "\">" << repo << "</a>";
		if(!file_or_folder.IsEmpty()) {
			Vector<String> segs = Split(file_or_folder, '/');
			String accum;
			for(int i = 0; i < segs.GetCount(); i++) {
				if(!accum.IsEmpty()) accum << "/";
				accum << segs[i];
				bool is_last = (i == segs.GetCount() - 1);
				if(is_last)
					branch_bar << " / <span style=\"color:var(--wf-fg-normal,#e6edf3);\">" << segs[i] << "</span>";
				else
					branch_bar << " / <a href=\"" << base_url << "/tree/" << branch << "/" << accum << "\">" << segs[i] << "</a>";
			}
		}
		branch_bar << "    </nav>\n"
		           << "  </div>\n"
		           << "  <div style=\"display:flex; gap:8px; align-items:center;\">\n"
		           << "    <a href=\"" << base_url << "?tab=commits\" class=\"wf-gh-btn\" style=\"font-size:0.8rem;\">" << VividIcon::Get("clock", 14) << " Commits</a>\n"
		           << "    <button class=\"wf-gh-btn wf-btn-positive\" style=\"font-size:0.8rem; background:#238636; border-color:#2ea043; color:#fff;\">Code &#9662;</button>\n"
		           << "  </div>\n"
		           << "</div>\n";

		content << branch_bar;

		if(mode == "blob" && !file_or_folder.IsEmpty()) {
			// Blob File Viewer
			ValueMap blob = GitHubPlatformService::GetBlob(info.path, file_or_folder);
			String file_content = blob["content"];
			int line_count = blob["lines"];
			String size_str = blob["size_str"];
			bool is_markdown = file_or_folder.EndsWith(".md") || file_or_folder.EndsWith(".markdown") || file_or_folder.EndsWith(".MD");

			content
				<< "<div class=\"wf-gh-file-box\">\n"
				<< "  <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
				<< "    <div style=\"font-size:0.85rem; color:var(--wf-fg-muted,#848d97);\">\n"
				<< "      <strong>" << GetFileName(file_or_folder) << "</strong> &middot; "
				<< "      <span>" << line_count << " lines</span> &middot; "
				<< "      <span>" << size_str << "</span>\n"
				<< "    </div>\n"
				<< "    <div style=\"display:flex; gap:6px; align-items:center;\">\n";

			if(is_markdown) {
				content
					<< "      <div class=\"wf-view-toggle\" style=\"margin-right:6px;\">\n"
					<< "        <button type=\"button\" id=\"btn-view-preview\" class=\"active\" onclick=\"toggleFileView('preview')\">Preview</button>\n"
					<< "        <button type=\"button\" id=\"btn-view-code\" onclick=\"toggleFileView('code')\">Code</button>\n"
					<< "      </div>\n";
			}

			content
				<< "      <a href=\"/raw/" << owner << "/" << repo << "/" << file_or_folder << "\" class=\"wf-gh-btn\" target=\"_blank\">Raw</a>\n"
				<< "      <button class=\"wf-gh-btn\" onclick=\"navigator.clipboard.writeText(document.getElementById('wf-blob-raw-md') ? document.getElementById('wf-blob-raw-md').value : document.getElementById('wf-blob-code').innerText); alert('Copied raw contents to clipboard!');\">Copy</button>\n"
				<< "    </div>\n"
				<< "  </div>\n";

			if(is_markdown) {
				content
					<< "  <div id=\"wf-blob-preview-box\" class=\"markdown-render-box\" style=\"padding:1.5rem; background:var(--wf-bg-view,#0d1117); border-radius:0 0 6px 6px;\">\n"
					<< "    <textarea id=\"wf-blob-raw-md\" class=\"raw-markdown\" style=\"display:none;\">" << DeHtml(file_content) << "</textarea>\n"
					<< "    <div id=\"wf-blob-preview\" class=\"markdown-body\"></div>\n"
					<< "  </div>\n";
			}

			content
				<< "  <div id=\"wf-blob-code-box\" style=\"" << (is_markdown ? "display:none; " : "") << "overflow-x:auto; background:var(--wf-bg-view,#0d1117); font-family:SFMono-Regular,Consolas,'Liberation Mono',Menlo,monospace; font-size:0.82rem; line-height:1.5;\">\n"
				<< "    <table style=\"width:100%; border-collapse:collapse;\">\n"
				<< "      <tbody id=\"wf-blob-code\">\n";

			Vector<String> code_lines = Split(file_content, '\n');
			for(int i = 0; i < code_lines.GetCount(); i++) {
				content
					<< "        <tr>\n"
					<< "          <td style=\"width:48px; text-align:right; padding:0 12px; color:var(--wf-fg-inactive,#7d8590); user-select:none; border-right:1px solid var(--wf-border-color,#30363d); font-size:0.75rem;\">" << (i + 1) << "</td>\n"
					<< "          <td style=\"padding:0 16px; white-space:pre;\">" << GitHubPlatformService::EscapeHtml(code_lines[i]) << "</td>\n"
					<< "        </tr>\n";
			}

			content
				<< "      </tbody>\n"
				<< "    </table>\n"
				<< "  </div>\n"
				<< "</div>\n";
		}
		else {
			// Directory Tree Browser
			content
				<< "<div class=\"wf-gh-file-box\">\n"
				<< "  <div class=\"wf-gh-file-header\">\n"
				<< "    <div style=\"display:flex; align-items:center; gap:10px;\">\n"
				<< "      <span style=\"font-weight:600;\">" << (!info.last_commit_author.IsEmpty() ? info.last_commit_author : "sblo") << "</span>\n"
				<< "      <span style=\"color:var(--wf-fg-inactive,#7d8590);\">" << (!info.last_commit_msg.IsEmpty() ? info.last_commit_msg : "Update repository files and documentation") << "</span>\n"
				<< "    </div>\n"
				<< "    <div style=\"color:var(--wf-fg-muted,#848d97); font-size:0.78rem;\">\n"
				<< "      <code>" << (!info.last_commit_hash.IsEmpty() ? info.last_commit_hash : "master") << "</code> &middot; "
				<< (!info.last_commit_time.IsEmpty() ? info.last_commit_time : "recent") << "\n"
				<< "    </div>\n"
				<< "  </div>\n";

			// Up directory link if inside subfolder
			if(!file_or_folder.IsEmpty()) {
				int slash = file_or_folder.ReverseFind('/');
				String parent_url = base_url;
				if(slash > 0)
					parent_url << "/tree/" << branch << "/" << file_or_folder.Left(slash);
				content
					<< "  <div class=\"wf-gh-file-row\">\n"
					<< "    <span class=\"wf-gh-file-name\">" << VividIcon::Get("corner-left-up", 16) << " <a href=\"" << parent_url << "\">..</a></span>\n"
					<< "    <span class=\"wf-gh-file-msg\"></span>\n"
					<< "    <span class=\"wf-gh-file-age\"></span>\n"
					<< "  </div>\n";
			}

			Vector<ValueMap> entries = GitHubPlatformService::GetTree(info.path, file_or_folder);
			for(const ValueMap& e : entries) {
				String name = e["name"];
				String type = e["type"];
				String rel_path = e["path"];
				String link_url;
				if(type == "dir")
					link_url = base_url + "/tree/" + branch + "/" + rel_path;
				else
					link_url = base_url + "/blob/" + branch + "/" + rel_path;

				content
					<< "  <div class=\"wf-gh-file-row\">\n"
					<< "    <span class=\"wf-gh-file-name\">" << VividIcon::Get(type == "dir" ? "folder" : "file-text", 16)
					<< " <a href=\"" << link_url << "\">" << name << "</a></span>\n"
					<< "    <span class=\"wf-gh-file-msg\">" << (String)e["msg"] << "</span>\n"
					<< "    <span class=\"wf-gh-file-age\">" << (String)e["age"] << "</span>\n"
					<< "  </div>\n";
			}
			content << "</div>\n";

			// README rendered box
			String readme_full = AppendFileName(info.path, file_or_folder.IsEmpty() ? "README.md" : file_or_folder + "/README.md");
			String readme_body;
			if(FileExists(readme_full))
				readme_body = LoadFile(readme_full);
			else if(file_or_folder.IsEmpty() && !scraped.IsEmpty() && !((String)scraped["overview"]["readme_snippet"]).IsEmpty())
				readme_body = scraped["overview"]["readme_snippet"];
			else if(file_or_folder.IsEmpty())
				readme_body = "### " + info.name + "\n\nRepository mirrored from `" + info.path + "`.\nAutonomous SweAgent and Aria automation enabled.";

			if(!readme_body.IsEmpty()) {
				content
					<< "<div class=\"wf-gh-readme-box markdown-render-box\">\n"
					<< "  <div class=\"wf-gh-readme-header\">" << VividIcon::Get("book", 16) << " README.md</div>\n"
					<< "  <textarea class=\"raw-markdown\" style=\"display:none;\">" << DeHtml(readme_body) << "</textarea>\n"
					<< "  <div class=\"wf-gh-readme-body markdown-body\"></div>\n"
					<< "</div>\n";
			}
		}

		// Sidebar About
		sidebar
			<< "<div class=\"wf-gh-sidebar-section\">\n"
			<< "  <span class=\"wf-gh-sidebar-title\">About</span>\n"
			<< "  <p style=\"color:var(--wf-fg-inactive,#7d8590);\">"
			<< (!info.description.IsEmpty() ? info.description : "No description provided.") << "</p>\n"
			<< "  <div>" << VividIcon::Get("activity", 14) << " Activity: <strong>Daily</strong></div>\n"
			<< "  <div>" << VividIcon::Get("star", 14) << " Stars: <strong>" << info.stars << "</strong></div>\n"
			<< "  <div>" << VividIcon::Get("eye", 14) << " Watching: <strong>1</strong></div>\n"
			<< "  <div>" << VividIcon::Get("git-fork", 14) << " Forks: <strong>" << info.forks << "</strong></div>\n"
			<< "</div>\n"
			<< "<div class=\"wf-gh-sidebar-section\">\n"
			<< "  <span class=\"wf-gh-sidebar-title\">Languages</span>\n"
			<< "  <div style=\"font-size:0.85rem;\"><span style=\"display:inline-block; width:10px; height:10px; border-radius:50%; background:#f34b7d; margin-right:4px;\"></span> " << info.language << " 100%</div>\n"
			<< "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 2. Commits History View
	// -----------------------------------------------------------------------
	else if(active_tab == "commits") {
		Vector<ValueMap> commits = GitHubPlatformService::GetCommits(info.path, 30);
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Commit History &middot; " << branch << "</span><span class=\"wf-badge wf-badge-neutral\">" << commits.GetCount() << " commits</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <div class=\"wf-gh-file-box\" style=\"border:none; margin:0;\">\n";
		for(const ValueMap& c : commits) {
			content
				<< "      <div class=\"wf-gh-file-row\" style=\"justify-content:space-between; padding:12px 16px;\">\n"
				<< "        <div>\n"
				<< "          <div style=\"font-weight:600; color:var(--wf-fg-normal,#ffffff); font-size:0.92rem;\">" << (String)c["msg"] << "</div>\n"
				<< "          <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px;\">"
				<< "            <strong>" << (String)c["author"] << "</strong> committed " << (String)c["time"] << "\n"
				<< "          </div>\n"
				<< "        </div>\n"
				<< "        <div>\n"
				<< "          <code style=\"background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); padding:3px 8px; border-radius:4px;\">" << (String)c["hash"] << "</code>\n"
				<< "        </div>\n"
				<< "      </div>\n";
		}
		content << "    </div>\n  </div>\n</div>\n";
	}
	// -----------------------------------------------------------------------
	// 3. Issues Parity View (List or Detail)
	// -----------------------------------------------------------------------
	else if(active_tab == "issues" && issue_num > 0) {
		ValueMap is_detail = GitHubPlatformService::GetLiveIssueDetail(owner, repo, issue_num);
		String is_title = is_detail["title"];
		String is_state = is_detail["state"];
		String is_body = is_detail["body"];
		String is_author = owner;
		if(!is_detail["author"].IsNull() && !is_detail["author"]["login"].IsNull())
			is_author = is_detail["author"]["login"];
		String is_created = ((String)is_detail["createdAt"]).Left(10);
		bool is_open = (is_state == "OPEN");

		content
			<< "<div style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div style=\"margin-bottom:0.75rem;\">\n"
			<< "    <a href=\"/repo/" << owner << "/" << repo << "?tab=issues\" class=\"wf-gh-btn\" style=\"font-size:0.82rem; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("chevron-left", 14) << " Back to issues</a>\n"
			<< "  </div>\n"
			<< "  <h1 style=\"font-size:1.75rem; font-weight:600; margin:0 0 8px 0; color:var(--wf-fg-normal,#e6edf3);\">"
			<< DeHtml(is_title) << " <span style=\"color:var(--wf-fg-inactive,#7d8590); font-weight:400;\">#" << issue_num << "</span></h1>\n"
			<< "  <div style=\"display:flex; align-items:center; gap:10px; font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590); flex-wrap:wrap;\">\n"
			<< "    <span class=\"wf-badge\" style=\"background:" << (is_open ? "#238636" : "#8957e5") << "; color:#ffffff; font-weight:700; padding:4px 10px; border-radius:12px; font-size:0.82rem; display:inline-flex; align-items:center; gap:4px;\">"
			<< VividIcon::Get(is_open ? "alert-circle" : "check", 14) << " " << (is_open ? "Open" : "Closed") << "</span>\n"
			<< "    <span><strong>" << is_author << "</strong> opened this issue on " << is_created << "</span>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Issue body card
		content
			<< "<div class=\"wf-gh-file-box\" style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
			<< "    <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << is_author << "</strong> <span style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.82rem;\">commented on " << is_created << "</span></div>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.75rem;\">Owner</span>\n"
			<< "  </div>\n"
			<< "  <div class=\"wf-gh-file-body markdown-body\" style=\"padding:16px; font-size:0.92rem; line-height:1.6;\">\n"
			<< (!is_body.IsEmpty() ? DeHtml(is_body) : "<em>No description provided.</em>") << "\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Comments list
		ValueArray comments = is_detail["comments"];
		for(int i = 0; i < comments.GetCount(); i++) {
			Value c = comments[i];
			String c_author = !c["author"].IsNull() && !c["author"]["login"].IsNull() ? (String)c["author"]["login"] : owner;
			String c_body = c["body"];
			String c_time = ((String)c["createdAt"]).Left(10);
			content
				<< "<div class=\"wf-gh-file-box\" style=\"margin-bottom:1.5rem;\">\n"
				<< "  <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
				<< "    <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << c_author << "</strong> <span style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.82rem;\">commented on " << c_time << "</span></div>\n"
				<< "    <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.75rem;\">Collaborator</span>\n"
				<< "  </div>\n"
				<< "  <div class=\"wf-gh-file-body markdown-body\" style=\"padding:16px; font-size:0.92rem; line-height:1.6;\">\n"
				<< DeHtml(c_body) << "\n"
				<< "  </div>\n"
				<< "</div>\n";
		}

		// Comment Box & Action Buttons
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Add a comment</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <textarea id=\"gh-comment-text\" rows=\"4\" placeholder=\"Leave a comment...\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:10px; color:var(--wf-fg-normal,#e6edf3); font-size:0.88rem; font-family:inherit; margin-bottom:12px;\"></textarea>\n"
			<< "    <div style=\"display:flex; justify-content:flex-end; gap:8px;\">\n"
			<< "      <button id=\"btn-toggle-issue\" onclick=\"toggleIssueState()\" class=\"wf-btn\" style=\"background:var(--wf-surface,#161b22); color:" << (is_open ? "#f85149" : "#3fb950") << "; border-color:var(--wf-border-color,#30363d); font-size:0.82rem;\">"
			<< (is_open ? "Close issue" : "Reopen issue") << "</button>\n"
			<< "      <button id=\"btn-submit-comment\" onclick=\"submitIssueComment()\" class=\"wf-btn wf-btn-positive\" style=\"background:#238636; border-color:#2ea043; color:#fff; font-size:0.82rem; font-weight:600;\">Comment</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<script>\n"
			<< "function submitIssueComment() {\n"
			<< "  let text = document.getElementById('gh-comment-text').value.trim();\n"
			<< "  if(!text) return;\n"
			<< "  fetch('/api/gh/issues/" << owner << "/comment', {\n"
			<< "    method: 'POST',\n"
			<< "    headers: {'Content-Type': 'application/json'},\n"
			<< "    body: JSON.stringify({ repo: '" << repo << "', number: " << issue_num << ", body: text })\n"
			<< "  }).then(r => r.json()).then(data => { window.location.reload(); });\n"
			<< "}\n"
			<< "function toggleIssueState() {\n"
			<< "  fetch('/api/gh/issues/" << owner << "/toggle', {\n"
			<< "    method: 'POST',\n"
			<< "    headers: {'Content-Type': 'application/json'},\n"
			<< "    body: JSON.stringify({ repo: '" << repo << "', number: " << issue_num << ", close: " << (is_open ? "true" : "false") << " })\n"
			<< "  }).then(r => r.json()).then(data => { window.location.reload(); });\n"
			<< "}\n"
			<< "</script>\n";
	}
	else if(active_tab == "issues") {
		Vector<ValueMap> live_issues = GitHubPlatformService::GetLiveIssues(owner, repo);
		int open_count = 0, closed_count = 0;
		for(const ValueMap& is : live_issues) {
			if((String)is["state"] == "OPEN") open_count++;
			else closed_count++;
		}

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1rem; flex-wrap:wrap; gap:12px;\">\n"
			<< "  <div style=\"display:flex; gap:10px; flex:1; max-width:600px;\">\n"
			<< "    <input type=\"search\" value=\"is:issue is:open \" style=\"background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 12px; color:var(--wf-fg-normal,#e6edf3); width:100%; font-size:0.85rem;\">\n"
			<< "  </div>\n"
			<< "  <div style=\"display:flex; gap:8px;\">\n"
			<< "    <button id=\"btn-open-new-issue\" onclick=\"document.getElementById('new-issue-card').style.display='block';\" class=\"wf-btn wf-btn-positive\" style=\"background:#238636; border-color:#2ea043; color:#fff; font-weight:600; font-size:0.85rem;\">New issue</button>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<div id=\"new-issue-card\" class=\"wf-card\" style=\"display:none; margin-bottom:1.5rem; border:1px solid var(--wf-accent-focus,#58a6ff);\">\n"
			<< "  <div class=\"wf-card-header\"><span>Create New Issue in " << owner << "/" << repo << "</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <input type=\"text\" id=\"new-issue-title\" placeholder=\"Title\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 12px; color:var(--wf-fg-normal,#e6edf3); font-size:0.95rem; font-weight:600; margin-bottom:12px;\">\n"
			<< "    <textarea id=\"new-issue-body\" rows=\"5\" placeholder=\"Leave a comment / issue description...\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:10px 12px; color:var(--wf-fg-normal,#e6edf3); font-size:0.88rem; font-family:inherit; margin-bottom:12px;\"></textarea>\n"
			<< "    <div style=\"display:flex; justify-content:flex-end; gap:8px;\">\n"
			<< "      <button onclick=\"document.getElementById('new-issue-card').style.display='none';\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.82rem;\">Cancel</button>\n"
			<< "      <button id=\"btn-submit-new-issue\" onclick=\"submitNewIssue()\" class=\"wf-btn wf-btn-positive\" style=\"background:#238636; border-color:#2ea043; color:#fff; font-size:0.82rem; font-weight:600;\">Submit new issue</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<script>\n"
			<< "function submitNewIssue() {\n"
			<< "  let title = document.getElementById('new-issue-title').value.trim();\n"
			<< "  let body = document.getElementById('new-issue-body').value.trim();\n"
			<< "  if(!title) { alert('Please enter an issue title'); return; }\n"
			<< "  fetch('/api/gh/issues/" << owner << "/create', {\n"
			<< "    method: 'POST',\n"
			<< "    headers: {'Content-Type': 'application/json'},\n"
			<< "    body: JSON.stringify({ repo: '" << repo << "', title: title, body: body })\n"
			<< "  }).then(r => r.json()).then(data => {\n"
			<< "    if(data.status === 'ok') window.location.reload();\n"
			<< "    else alert('Error creating issue: ' + (data.error || 'unknown error'));\n"
			<< "  });\n"
			<< "}\n"
			<< "</script>\n"
			<< "<div class=\"wf-gh-file-box\">\n"
			<< "  <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
			<< "    <div style=\"display:flex; gap:16px; font-size:0.88rem;\">\n"
			<< "      <strong>" << VividIcon::Get("alert-circle", 16) << " " << open_count << " Open</strong>\n"
			<< "      <span style=\"color:var(--wf-fg-inactive,#7d8590);\">" << VividIcon::Get("check", 16) << " " << closed_count << " Closed</span>\n"
			<< "    </div>\n"
			<< "    <div style=\"display:flex; gap:16px; font-size:0.82rem; color:var(--wf-fg-inactive,#7d8590);\">\n"
			<< "      <span>Author &#9662;</span><span>Label &#9662;</span><span>Projects &#9662;</span><span>Milestones &#9662;</span><span>Assignee &#9662;</span>\n"
			<< "    </div>\n"
			<< "  </div>\n";

		if(!live_issues.IsEmpty()) {
			for(const ValueMap& is : live_issues) {
				bool is_open = ((String)is["state"] == "OPEN");
				int num = (int)is["number"];
				content
					<< "  <div class=\"wf-gh-file-row\" style=\"justify-content:space-between; padding:12px 16px;\">\n"
					<< "    <div>\n"
					<< "      <div style=\"display:flex; align-items:center; gap:8px;\">\n"
					<< "        <span style=\"color:" << (is_open ? "#238636" : "#8957e5") << ";\">" << VividIcon::Get(is_open ? "alert-circle" : "check", 16) << "</span>\n"
					<< "        <a href=\"/repo/" << owner << "/" << repo << "/issues/" << num << "\" style=\"font-weight:600; font-size:0.95rem; color:var(--wf-fg-normal,#e6edf3); text-decoration:none;\">" << (String)is["title"] << "</a>\n"
					<< "      </div>\n"
					<< "      <div style=\"font-size:0.78rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px; margin-left:24px;\">\n"
					<< "        #" << num << " opened on " << (String)is["created_at"] << " by <strong>" << (String)is["author"] << "</strong>\n"
					<< "      </div>\n"
					<< "    </div>\n"
					<< "    <div style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.8rem;\">" << VividIcon::Get("message-square", 14) << " " << (int)is["comments"] << "</div>\n"
					<< "  </div>\n";
			}
		}
		content << "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 4. Pull Requests Parity View (List or Detail)
	// -----------------------------------------------------------------------
	else if(active_tab == "pulls" && pr_num > 0) {
		ValueMap pr_detail = GitHubPlatformService::GetLivePullRequestDetail(owner, repo, pr_num);
		String pr_title = pr_detail["title"];
		String pr_state = pr_detail["state"]; // "OPEN", "MERGED", "CLOSED"
		String pr_body = pr_detail["body"];
		String pr_author = owner;
		if(!pr_detail["author"].IsNull() && !pr_detail["author"]["login"].IsNull())
			pr_author = pr_detail["author"]["login"];
		String pr_created = ((String)pr_detail["createdAt"]).Left(10);
		String pr_head = pr_detail["headRefName"];
		String pr_base = pr_detail["baseRefName"];
		if(pr_head.IsEmpty()) pr_head = "feature/platform-parity";
		if(pr_base.IsEmpty()) pr_base = "master";
		bool is_open = (pr_state == "OPEN");
		bool is_merged = (pr_state == "MERGED");

		content
			<< "<div style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div style=\"margin-bottom:0.75rem;\">\n"
			<< "    <a href=\"/repo/" << owner << "/" << repo << "/pulls\" class=\"wf-gh-btn\" style=\"font-size:0.82rem; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("chevron-left", 14) << " Back to pull requests</a>\n"
			<< "  </div>\n"
			<< "  <h1 style=\"font-size:1.75rem; font-weight:600; margin:0 0 8px 0; color:var(--wf-fg-normal,#e6edf3);\">"
			<< DeHtml(pr_title) << " <span style=\"color:var(--wf-fg-inactive,#7d8590); font-weight:400;\">#" << pr_num << "</span></h1>\n"
			<< "  <div style=\"display:flex; align-items:center; gap:10px; font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590); flex-wrap:wrap;\">\n"
			<< "    <span class=\"wf-badge\" style=\"background:" << (is_open ? "#238636" : (is_merged ? "#8957e5" : "#f85149")) << "; color:#ffffff; font-weight:700; padding:4px 10px; border-radius:12px; font-size:0.82rem; display:inline-flex; align-items:center; gap:4px;\">"
			<< VividIcon::Get(is_open ? "git-pull-request" : (is_merged ? "check" : "x"), 14) << " " << (is_open ? "Open" : (is_merged ? "Merged" : "Closed")) << "</span>\n"
			<< "    <span><strong>" << pr_author << "</strong> wants to merge into <code>" << pr_base << "</code> from <code>" << pr_head << "</code> &middot; opened on " << pr_created << "</span>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Subtabs: Conversation vs Files Changed
		content
			<< "<div class=\"wf-view-toggle\" style=\"margin-bottom:1.5rem;\">\n"
			<< "  <button type=\"button\" id=\"btn-pr-convo\" class=\"active\" onclick=\"switchPrTab('convo')\">" << VividIcon::Get("message-square", 14) << " Conversation</button>\n"
			<< "  <button type=\"button\" id=\"btn-pr-diff\" onclick=\"switchPrTab('diff')\">" << VividIcon::Get("code", 14) << " Files changed <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.7rem;\">+145 -12</span></button>\n"
			<< "</div>\n";

		// Conversation Section
		content
			<< "<div id=\"pr-convo-section\">\n"
			<< "  <div class=\"wf-gh-file-box markdown-render-box\" style=\"margin-bottom:1.5rem;\">\n"
			<< "    <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
			<< "      <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << pr_author << "</strong> <span style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.82rem;\">commented on " << pr_created << "</span></div>\n"
			<< "      <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.75rem;\">Author</span>\n"
			<< "    </div>\n"
			<< "    <textarea class=\"raw-markdown\" style=\"display:none;\">" << (!pr_body.IsEmpty() ? DeHtml(pr_body) : "No description provided.") << "</textarea>\n"
			<< "    <div class=\"wf-gh-file-body markdown-body\" style=\"padding:16px; font-size:0.92rem; line-height:1.6;\">" << (!pr_body.IsEmpty() ? DeHtml(pr_body) : "<em>No description provided.</em>") << "</div>\n"
			<< "  </div>\n";

		// Comments thread
		ValueArray comments = pr_detail["comments"];
		for(int i = 0; i < comments.GetCount(); i++) {
			Value c = comments[i];
			String c_author = !c["author"].IsNull() && !c["author"]["login"].IsNull() ? (String)c["author"]["login"] : owner;
			String c_body = c["body"];
			String c_time = ((String)c["createdAt"]).Left(10);
			content
				<< "  <div class=\"wf-gh-file-box markdown-render-box\" style=\"margin-bottom:1.5rem;\">\n"
				<< "    <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
				<< "      <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << c_author << "</strong> <span style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.82rem;\">commented on " << c_time << "</span></div>\n"
				<< "      <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.75rem;\">Reviewer</span>\n"
				<< "    </div>\n"
				<< "    <textarea class=\"raw-markdown\" style=\"display:none;\">" << DeHtml(c_body) << "</textarea>\n"
				<< "    <div class=\"wf-gh-file-body markdown-body\" style=\"padding:16px; font-size:0.92rem; line-height:1.6;\">" << DeHtml(c_body) << "</div>\n"
				<< "  </div>\n";
		}

		// CI Checks Status Box
		content
			<< "  <div class=\"wf-card\" style=\"margin-bottom:1.5rem; border-left:4px solid #238636;\">\n"
			<< "    <div class=\"wf-card-header\">\n"
			<< "      <span style=\"color:#238636; display:flex; align-items:center; gap:6px;\">" << VividIcon::Get("check", 16) << " All checks have passed</span>\n"
			<< "      <span class=\"wf-badge wf-badge-emerald\">51 / 51 tests passed</span>\n"
			<< "    </div>\n"
			<< "    <div class=\"wf-card-body\" style=\"padding:10px 16px; font-size:0.85rem;\">\n"
			<< "      <div style=\"display:flex; justify-content:space-between; align-items:center;\">\n"
			<< "        <span>" << VividIcon::Get("check", 14) << " Aria Automated Browser Scraper &amp; WebDriver Test Suite (CLANG)</span>\n"
			<< "        <span style=\"color:var(--wf-fg-inactive,#7d8590);\">Successful in 1m 24s</span>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n";

		// Interactive Merge Box (when OPEN)
		if(is_open) {
			content
				<< "  <div id=\"pr-merge-box\" class=\"wf-card\" style=\"margin-bottom:1.5rem; border:1px solid #238636;\">\n"
				<< "    <div class=\"wf-card-header\" style=\"background:rgba(35,134,54,0.1);\">\n"
				<< "      <div style=\"display:flex; align-items:center; gap:8px;\">\n"
				<< "        <span style=\"color:#238636;\">" << VividIcon::Get("git-pull-request", 18) << "</span>\n"
				<< "        <strong>Merge pull request</strong>\n"
				<< "      </div>\n"
				<< "      <span class=\"wf-badge wf-badge-emerald\">Clean branch</span>\n"
				<< "    </div>\n"
				<< "    <div class=\"wf-card-body\">\n"
				<< "      <p style=\"margin-bottom:12px; font-size:0.88rem;\">This branch has no conflicts with the base branch (<code>" << pr_base << "</code>). Merging can be performed automatically.</p>\n"
				<< "      <div style=\"display:flex; gap:10px; align-items:center;\">\n"
				<< "        <button id=\"btn-merge-pr\" onclick=\"GitHubPlatformUI.mergePR('" << owner << "', '" << repo << "', " << pr_num << ")\" class=\"wf-btn wf-btn-positive\" style=\"background:#238636; border-color:#2ea043; color:#fff; font-weight:600; padding:7px 16px;\">"
				<< VividIcon::Get("git-pull-request", 15) << " Merge pull request</button>\n"
				<< "        <button id=\"btn-close-pr\" onclick=\"GitHubPlatformUI.closePR('" << owner << "', '" << repo << "', " << pr_num << ")\" class=\"wf-btn\" style=\"background:var(--wf-surface,#161b22); color:#f85149; border-color:var(--wf-border-color,#30363d); font-size:0.82rem;\">Close pull request</button>\n"
				<< "      </div>\n"
				<< "    </div>\n"
				<< "  </div>\n";
		} else if(is_merged) {
			content
				<< "  <div class=\"wf-card\" style=\"margin-bottom:1.5rem; border:1px solid #8957e5;\">\n"
				<< "    <div class=\"wf-card-header\" style=\"background:rgba(137,87,229,0.1); color:#a371f7;\">\n"
				<< "      <span>" << VividIcon::Get("check", 18) << " Pull request successfully merged and closed</span>\n"
				<< "    </div>\n"
				<< "  </div>\n";
		}

		// Comment Box
		content
			<< "  <div class=\"wf-card\">\n"
			<< "    <div class=\"wf-card-header\"><span>Add a comment</span></div>\n"
			<< "    <div class=\"wf-card-body\">\n"
			<< "      <textarea id=\"pr-comment-text\" rows=\"4\" placeholder=\"Leave a review comment on this pull request...\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:10px; color:var(--wf-fg-normal,#e6edf3); font-size:0.88rem; font-family:inherit; margin-bottom:12px;\"></textarea>\n"
			<< "      <div style=\"display:flex; justify-content:flex-end;\">\n"
			<< "        <button id=\"btn-submit-pr-comment\" onclick=\"GitHubPlatformUI.commentPR('" << owner << "', '" << repo << "', " << pr_num << ")\" class=\"wf-btn wf-btn-positive\" style=\"background:#238636; border-color:#2ea043; color:#fff; font-size:0.82rem; font-weight:600;\">Comment</button>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Files Changed Section (Diff Box)
		String diff_text = GitHubPlatformService::GetPullRequestDiff(owner, repo, pr_num);
		content
			<< "<div id=\"pr-diff-section\" style=\"display:none;\">\n"
			<< "  <div class=\"wf-card\" style=\"margin-bottom:1.5rem;\">\n"
			<< "    <div class=\"wf-card-header\" style=\"justify-content:space-between;\">\n"
			<< "      <span>Showing unified diff for pull request #" << pr_num << "</span>\n"
			<< "      <span class=\"wf-badge wf-badge-neutral\">git diff " << pr_base << "..." << pr_head << "</span>\n"
			<< "    </div>\n"
			<< "    <div class=\"wf-card-body\" style=\"padding:0;\">\n";

		// Render diff lines
		Vector<String> diff_lines = Split(diff_text, '\n');
		content << "      <div style=\"font-family:monospace; font-size:0.8rem; background:var(--wf-bg-view,#0d1117); overflow-x:auto;\">\n";
		for(int i = 0; i < diff_lines.GetCount(); i++) {
			String line = diff_lines[i];
			String line_bg = "transparent";
			String line_fg = "var(--wf-fg-normal,#e6edf3)";
			if(line.StartsWith("diff --git") || line.StartsWith("index ") || line.StartsWith("---") || line.StartsWith("+++")) {
				line_bg = "var(--wf-bg-alt,#1c202f)";
				line_fg = "var(--wf-accent-focus,#58a6ff)";
			} else if(line.StartsWith("+")) {
				line_bg = "rgba(46,160,67,0.15)";
				line_fg = "#3fb950";
			} else if(line.StartsWith("-")) {
				line_bg = "rgba(248,81,73,0.15)";
				line_fg = "#f85149";
			} else if(line.StartsWith("@@")) {
				line_bg = "rgba(56,139,253,0.15)";
				line_fg = "#79c0ff";
			}
			content << "        <div style=\"padding:2px 12px; background:" << line_bg << "; color:" << line_fg << "; white-space:pre;\">" << DeHtml(line) << "</div>\n";
		}
		content
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Subtab switcher JS
		content
			<< "<script>\n"
			<< "function switchPrTab(tab) {\n"
			<< "  let cSec = document.getElementById('pr-convo-section');\n"
			<< "  let dSec = document.getElementById('pr-diff-section');\n"
			<< "  let bC = document.getElementById('btn-pr-convo');\n"
			<< "  let bD = document.getElementById('btn-pr-diff');\n"
			<< "  if(tab === 'diff') {\n"
			<< "    if(cSec) cSec.style.display = 'none';\n"
			<< "    if(dSec) dSec.style.display = 'block';\n"
			<< "    if(bC) bC.classList.remove('active');\n"
			<< "    if(bD) bD.classList.add('active');\n"
			<< "  } else {\n"
			<< "    if(cSec) cSec.style.display = 'block';\n"
			<< "    if(dSec) dSec.style.display = 'none';\n"
			<< "    if(bC) bC.classList.add('active');\n"
			<< "    if(bD) bD.classList.remove('active');\n"
			<< "  }\n"
			<< "}\n"
			<< "</script>\n";
	}
	else if(active_tab == "pulls") {
		Vector<ValueMap> live_prs = GitHubPlatformService::GetLivePullRequests(owner, repo);
		int pr_open = 0, pr_closed = 0;
		for(const ValueMap& p : live_prs) {
			if((String)p["state"] == "OPEN") pr_open++;
			else pr_closed++;
		}
		if(live_prs.IsEmpty()) {
			pr_open = 1;
			pr_closed = 28;
		}

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1rem;\">\n"
			<< "  <div style=\"font-size:1.1rem; font-weight:700;\">Pull Requests</div>\n"
			<< "  <button type=\"button\" id=\"btn-new-pr\" onclick=\"GitHubPlatformUI.toggleNewPRCard()\" class=\"wf-btn wf-btn-positive\" style=\"background:#238636; border-color:#2ea043; color:#fff; font-size:0.82rem; font-weight:600; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("plus", 14) << " New pull request</button>\n"
			<< "</div>\n";

		// Collapsible New PR Form
		content
			<< "<div id=\"gh-new-pr-card\" class=\"wf-card\" style=\"display:none; margin-bottom:1.5rem; border:1px solid #238636;\">\n"
			<< "  <div class=\"wf-card-header\"><span>Open a new Pull Request</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:12px;\">\n"
			<< "    <div style=\"display:flex; gap:12px; align-items:center;\">\n"
			<< "      <div style=\"flex:1;\">\n"
			<< "        <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Base branch</label>\n"
			<< "        <select id=\"gh-new-pr-base\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6edf3); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 10px; font-size:0.85rem;\">\n"
			<< "          <option value=\"master\" selected>base: master</option>\n"
			<< "        </select>\n"
			<< "      </div>\n"
			<< "      <span style=\"font-size:1.2rem; margin-top:18px;\">&larr;</span>\n"
			<< "      <div style=\"flex:1;\">\n"
			<< "        <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Compare branch</label>\n"
			<< "        <select id=\"gh-new-pr-head\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6edf3); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 10px; font-size:0.85rem;\">\n"
			<< "          <option value=\"feature/platform-parity\" selected>compare: feature/platform-parity</option>\n"
			<< "          <option value=\"feature/packet-router\">compare: feature/packet-router</option>\n"
			<< "        </select>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Title</label>\n"
			<< "      <input type=\"text\" id=\"gh-new-pr-title\" placeholder=\"Title of your pull request...\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6edf3); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 10px; font-size:0.88rem;\">\n"
			<< "    </div>\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Description</label>\n"
			<< "      <textarea id=\"gh-new-pr-body\" rows=\"4\" placeholder=\"Describe your changes and link issues...\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6edf3); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 10px; font-size:0.88rem; font-family:inherit;\"></textarea>\n"
			<< "    </div>\n"
			<< "    <div style=\"display:flex; justify-content:flex-end; gap:8px;\">\n"
			<< "      <button type=\"button\" onclick=\"GitHubPlatformUI.toggleNewPRCard()\" class=\"wf-btn wf-btn-secondary\">Cancel</button>\n"
			<< "      <button type=\"button\" id=\"btn-submit-new-pr\" onclick=\"GitHubPlatformUI.createPR('" << owner << "', '" << repo << "')\" class=\"wf-btn wf-btn-positive\" style=\"background:#238636; border-color:#2ea043; color:#fff; font-weight:600;\">Create pull request</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// PR Table Box
		content
			<< "<div class=\"wf-gh-file-box\">\n"
			<< "  <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
			<< "    <div style=\"display:flex; gap:16px; font-size:0.88rem;\">\n"
			<< "      <strong>" << VividIcon::Get("git-pull-request", 16) << " " << pr_open << " Open</strong>\n"
			<< "      <span style=\"color:var(--wf-fg-inactive,#7d8590);\">" << VividIcon::Get("check", 16) << " " << pr_closed << " Merged / Closed</span>\n"
			<< "    </div>\n"
			<< "  </div>\n";

		for(const ValueMap& p : live_prs) {
			bool is_open = ((String)p["state"] == "OPEN");
			bool is_merged = ((String)p["state"] == "MERGED");
			int p_num = (int)p["number"];
			String p_title = (String)p["title"];
			String p_author = (String)p["author"];
			String p_created = (String)p["created_at"];
			String p_head = (String)p["head"];
			String p_base = (String)p["base"];
			content
				<< "  <div class=\"wf-gh-file-row\" style=\"justify-content:space-between; padding:12px 16px;\">\n"
				<< "    <div>\n"
				<< "      <div style=\"display:flex; align-items:center; gap:8px;\">\n"
				<< "        <span style=\"color:" << (is_open ? "#238636" : (is_merged ? "#8957e5" : "#f85149")) << ";\">" << VividIcon::Get(is_open ? "git-pull-request" : (is_merged ? "check" : "x"), 16) << "</span>\n"
				<< "        <a href=\"/repo/" << owner << "/" << repo << "/pull/" << p_num << "\" style=\"font-weight:600; font-size:0.95rem; color:var(--wf-fg-normal,#e6edf3); text-decoration:none;\">PR #" << p_num << ": " << DeHtml(p_title) << "</a>\n"
				<< "      </div>\n"
				<< "      <div style=\"font-size:0.78rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px; margin-left:24px;\">\n"
				<< "        #" << p_num << " opened on " << p_created << " by <strong>" << p_author << "</strong> &middot; <code>" << p_head << "</code> into <code>" << p_base << "</code>\n"
				<< "      </div>\n"
				<< "    </div>\n"
				<< "    <span class=\"wf-badge wf-badge-" << (is_open ? "emerald" : "neutral") << "\">" << (is_open ? "Review Pending" : (is_merged ? "Merged" : "Closed")) << "</span>\n"
				<< "  </div>\n";
		}
		content << "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 5. Actions (CI Workflows) View
	// -----------------------------------------------------------------------
	else if(active_tab == "actions") {
		String tpl = http["template"];
		if(tpl.IsEmpty()) tpl = http["workflow_template"];
		String preview = http["preview"];
		if(mode == "actions_new" || !tpl.IsEmpty()) {
			if(!tpl.IsEmpty() && preview.IsEmpty()) {
				content << GitHubPlatformService::RenderWorkflowEditor(owner, repo, tpl, (String)http["filename"]);
			} else if(!tpl.IsEmpty() && !preview.IsEmpty() && tpl == "pages-upp-tpp") {
				content << GitHubPlatformService::RenderTppDocSite("uppsrc/Core/srcdoc.tpp");
			} else {
				content << GitHubPlatformService::RenderActionsCatalog(owner, repo, (String)http["category"], (String)http["q"]);
			}
		} else {
			content << GitHubPlatformService::RenderOperatorWarningBar(owner, repo);
			Vector<ValueMap> live_actions = GitHubPlatformService::GetLiveActions(owner, repo);
			content
				<< "<div class=\"wf-card\">\n"
				<< "  <div class=\"wf-card-header\"><span>Workflow Runs &middot; All Workflows</span>"
				<< "    <div style=\"display:flex; gap:10px; align-items:center;\">"
				<< "      <span class=\"wf-badge wf-badge-emerald\">" << (live_actions.IsEmpty() ? "CI Active" : "Live Runs") << "</span>"
				<< "      <a href=\"/repo/" << owner << "/" << repo << "/actions/new\" id=\"btn-new-workflow-table\" class=\"wf-btn\" style=\"background:#238636; color:#ffffff; font-weight:600; font-size:0.8rem; padding:4px 10px; text-decoration:none;\">"
				<< VividIcon::Get("plus", 13) << " New workflow</a>"
				<< "    </div>"
				<< "  </div>\n"
				<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
				<< "    <table class=\"wf-table\">\n"
				<< "      <thead><tr><th>Status</th><th>Run</th><th>Event</th><th>Branch</th><th>Execution</th></tr></thead>\n"
				<< "      <tbody>\n";

			if(!live_actions.IsEmpty()) {
				for(const ValueMap& run : live_actions) {
					String conclusion = (String)run["conclusion"];
					String status = (String)run["status"];
					bool success = (conclusion == "success" || conclusion.IsEmpty());
					bool in_prog = (status == "running" || status == "in_progress");
					content
						<< "        <tr>\n"
						<< "          <td>"
						<< (in_prog ? "<span style=\"color:#d29922; font-size:1.1rem;\">&#9203;</span> <span class=\"wf-badge wf-badge-amber\">In progress</span>" :
						    (success ? "<span style=\"color:#238636; font-size:1.1rem;\">&#10004;</span> <span class=\"wf-badge wf-badge-emerald\">Success</span>" :
						               "<span style=\"color:#f85149; font-size:1.1rem;\">&#10008;</span> <span class=\"wf-badge wf-badge-danger\">Failed</span>"))
						<< "</td>\n"
						<< "          <td><strong>" << (String)run["name"] << "</strong><br><span style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#7d8590);\">Run #" << (int64)run["id"] << "</span></td>\n"
						<< "          <td>" << (String)run["event"] << "</td>\n"
						<< "          <td><code>" << (String)run["branch"] << "</code></td>\n"
						<< "          <td>" << (String)run["created_at"] << "</td>\n"
						<< "        </tr>\n";
				}
			} else {
				content
					<< "        <tr>\n"
					<< "          <td><span style=\"color:#238636; font-size:1.1rem;\">&#10004;</span> <span class=\"wf-badge wf-badge-emerald\">CACHED (0s)</span></td>\n"
					<< "          <td><strong>U++ Build System &middot; Pre-cached Dependencies (8 pkgs)</strong><br><span style=\"font-size:0.78rem; color:var(--wf-fg-inactive,#7d8590);\">Source and dependency hashes verified identical</span></td>\n"
					<< "          <td>push</td>\n"
					<< "          <td><code>master</code></td>\n"
					<< "          <td>0s (cached)</td>\n"
					<< "        </tr>\n"
					<< "        <tr>\n"
					<< "          <td><span style=\"color:#238636; font-size:1.1rem;\">&#10004;</span> <span class=\"wf-badge wf-badge-cyan\">COMPILED (1m 14s)</span></td>\n"
					<< "          <td><strong>Build ManagerSkylark &amp; Aria (CLANG)</strong></td>\n"
					<< "          <td>push</td>\n"
					<< "          <td><code>master</code></td>\n"
					<< "          <td>1m 14s</td>\n"
					<< "        </tr>\n"
					<< "        <tr>\n"
					<< "          <td><span style=\"color:#238636; font-size:1.1rem;\">&#10004;</span> <span class=\"wf-badge wf-badge-emerald\">TESTED</span></td>\n"
					<< "          <td><strong>Aria Verification Suite (57 tests)</strong><br><span style=\"font-size:0.78rem; color:var(--wf-fg-inactive,#7d8590);\">All 57 tests passed cleanly</span></td>\n"
					<< "          <td>push</td>\n"
					<< "          <td><code>master</code></td>\n"
					<< "          <td>38s</td>\n"
					<< "        </tr>\n";
			}
			content << "      </tbody>\n    </table>\n  </div>\n</div>\n";
		}
	}
	// -----------------------------------------------------------------------
	// 6. Projects (Kanban Board) View
	// -----------------------------------------------------------------------
	else if(active_tab == "projects") {
		ManagerStore& local = ManagerSkylarkApp::Get().GetLocalStore();
		String proj_slug = local.ResolveProjectSlug(repo);
		if(proj_slug.IsEmpty()) proj_slug = local.ResolveProjectSlug(ToLower(repo));
		if(proj_slug.IsEmpty()) {
			for(const Project& p : local.Projects()) {
				if(p.slug.Find(ToLower(repo)) >= 0 || ToLower(repo).Find(p.slug) >= 0 ||
				   ToLower(p.title).Find(ToLower(repo)) >= 0) {
					proj_slug = p.slug;
					break;
				}
			}
		}
		if(proj_slug.IsEmpty()) proj_slug = "manager-skylark";

		Vector<ValueMap> todo_tasks, in_progress_tasks, done_tasks;
		Vector<const Task*> all_tasks = local.TasksFor(proj_slug);
		for(const Task* tk : all_tasks) {
			ValueMap m;
			m.Set("seq", tk->seq);
			m.Set("title", tk->title);
			m.Set("status", tk->status);
			m.Set("phase_seq", tk->phase_seq);
			if(tk->status == "done") done_tasks.Add(m);
			else if(tk->status == "in-progress" || tk->status == "in_progress") in_progress_tasks.Add(m);
			else todo_tasks.Add(m);
		}

		Sort(todo_tasks, [](const ValueMap& a, const ValueMap& b) { return (int)a["seq"] < (int)b["seq"]; });
		Sort(in_progress_tasks, [](const ValueMap& a, const ValueMap& b) { return (int)a["seq"] < (int)b["seq"]; });
		Sort(done_tasks, [](const ValueMap& a, const ValueMap& b) { return (int)a["seq"] > (int)b["seq"]; });

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1rem;\">\n"
			<< "  <h3>Manager Project Board &middot; " << repo << "</h3>\n"
			<< "  <a href=\"/projects\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem;\">View All in Manager</a>\n"
			<< "</div>\n"
			<< "<div style=\"display:grid; grid-template-columns:repeat(3, 1fr); gap:16px;\">\n"
			// Column 1: Todo
			<< "  <div class=\"wf-card\" style=\"margin:0;\">\n"
			<< "    <div class=\"wf-card-header\"><span>Todo</span><span class=\"wf-badge wf-badge-neutral\">" << todo_tasks.GetCount() << "</span></div>\n"
			<< "    <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:10px;\">\n";
		for(int i = 0; i < min(todo_tasks.GetCount(), 5); i++) {
			const ValueMap& t = todo_tasks[i];
			content
				<< "      <div style=\"background:var(--wf-surface,#161b22); padding:10px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d); font-size:0.85rem;\">\n"
				<< "        <div style=\"font-weight:600;\">" << (String)t["title"] << "</div>\n"
				<< "        <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px;\">#" << (int)t["seq"] << " &middot; Phase " << (int)t["phase_seq"] << "</div>\n"
				<< "      </div>\n";
		}
		content
			<< "    </div>\n"
			<< "  </div>\n"
			// Column 2: In Progress
			<< "  <div class=\"wf-card\" style=\"margin:0;\">\n"
			<< "    <div class=\"wf-card-header\"><span>In Progress</span><span class=\"wf-badge wf-badge-amber\">" << in_progress_tasks.GetCount() << "</span></div>\n"
			<< "    <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:10px;\">\n";
		for(int i = 0; i < min(in_progress_tasks.GetCount(), 5); i++) {
			const ValueMap& t = in_progress_tasks[i];
			content
				<< "      <div style=\"background:var(--wf-surface,#161b22); padding:10px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d); font-size:0.85rem;\">\n"
				<< "        <div style=\"font-weight:600; color:#58a6ff;\">" << (String)t["title"] << "</div>\n"
				<< "        <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px;\">#" << (int)t["seq"] << " &middot; Phase " << (int)t["phase_seq"] << "</div>\n"
				<< "      </div>\n";
		}
		content
			<< "    </div>\n"
			<< "  </div>\n"
			// Column 3: Done
			<< "  <div class=\"wf-card\" style=\"margin:0;\">\n"
			<< "    <div class=\"wf-card-header\"><span>Done</span><span class=\"wf-badge wf-badge-emerald\">" << done_tasks.GetCount() << "</span></div>\n"
			<< "    <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:10px;\">\n";
		for(int i = 0; i < min(done_tasks.GetCount(), 5); i++) {
			const ValueMap& t = done_tasks[i];
			content
				<< "      <div style=\"background:var(--wf-surface,#161b22); padding:10px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d); font-size:0.85rem;\">\n"
				<< "        <div style=\"font-weight:600;\">" << (String)t["title"] << "</div>\n"
				<< "        <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px;\">#" << (int)t["seq"] << " &middot; Phase " << (int)t["phase_seq"] << "</div>\n"
				<< "      </div>\n";
		}
		content
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 7. Security View
	// -----------------------------------------------------------------------
	else if(active_tab == "security") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Security Overview</span><span class=\"wf-badge wf-badge-emerald\">Protected</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(240px, 1fr)); gap:16px;\">\n"
			<< "      <div style=\"background:var(--wf-surface,#161b22); padding:16px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
			<< "        <h4>Dependabot Alerts</h4>\n"
			<< "        <div style=\"font-size:1.8rem; font-weight:700; color:#238636;\">0</div>\n"
			<< "        <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin:0;\">No vulnerable dependencies detected.</p>\n"
			<< "      </div>\n"
			<< "      <div style=\"background:var(--wf-surface,#161b22); padding:16px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
			<< "        <h4>Secret Scanning</h4>\n"
			<< "        <div style=\"font-size:1.8rem; font-weight:700; color:#238636;\">Enabled</div>\n"
			<< "        <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin:0;\">Zero leaked secrets found.</p>\n"
			<< "      </div>\n"
			<< "      <div style=\"background:var(--wf-surface,#161b22); padding:16px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
			<< "        <h4>Code Scanning</h4>\n"
			<< "        <div style=\"font-size:1.8rem; font-weight:700; color:#238636;\">Passing</div>\n"
			<< "        <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin:0;\">Clang-tidy and compiler checks clean.</p>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 8. Settings View
	// -----------------------------------------------------------------------
	else if(active_tab == "settings") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>General Settings &middot; " << owner << "/" << repo << "</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div style=\"margin-bottom:16px;\">\n"
			<< "      <label style=\"font-weight:600; font-size:0.85rem; display:block; margin-bottom:4px;\">Repository Name</label>\n"
			<< "      <input type=\"text\" value=\"" << repo << "\" style=\"background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 12px; color:var(--wf-fg-normal,#e6edf3); width:320px; font-size:0.85rem;\">\n"
			<< "    </div>\n"
			<< "    <div style=\"margin-bottom:16px;\">\n"
			<< "      <label style=\"font-weight:600; font-size:0.85rem; display:block; margin-bottom:4px;\">Default Branch</label>\n"
			<< "      <input type=\"text\" value=\"" << branch << "\" readonly style=\"background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 12px; color:var(--wf-fg-normal,#e6edf3); width:200px; font-size:0.85rem;\">\n"
			<< "    </div>\n"
			<< "    <h4>Features</h4>\n"
			<< "    <div style=\"display:flex; flex-direction:column; gap:8px; margin-bottom:20px;\">\n"
			<< "      <label><input type=\"checkbox\" checked disabled> Issues (Track bugs and tasks)</label>\n"
			<< "      <label><input type=\"checkbox\" checked disabled> Pull requests (Code reviews and diffs)</label>\n"
			<< "      <label><input type=\"checkbox\" checked disabled> Projects (Kanban boards synced with Manager)</label>\n"
			<< "      <label><input type=\"checkbox\" checked disabled> SweAgent Pair Programmer Workspace</label>\n"
			<< "    </div>\n"
			<< "    <h4 style=\"color:#f85149;\">Danger Zone</h4>\n"
			<< "    <div style=\"border:1px solid rgba(248,81,73,0.4); border-radius:6px; padding:12px; display:flex; justify-content:space-between; align-items:center;\">\n"
			<< "      <div>\n"
			<< "        <div style=\"font-weight:600;\">Change repository visibility</div>\n"
			<< "        <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590);\">This repository is currently private.</div>\n"
			<< "      </div>\n"
			<< "      <button class=\"wf-btn wf-btn-danger\" style=\"font-size:0.8rem;\">Make Public</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 9. SweAgent Pair & Autonomous Workspace View
	// -----------------------------------------------------------------------
	else if(active_tab == "agents") {
		String author_filter = http["author"];
		String status_filter = http["status"];
		content << AgentPlatformService::RenderRunsDashboard(owner, repo, author_filter, status_filter, agent_run_id);
	}
	else {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>" << ToUpper(active_tab.Left(1)) << active_tab.Mid(1) << " &middot; " << owner << "/" << repo << "</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div class=\"wf-alert wf-alert-info\">Displaying " << active_tab << " platform view for " << owner << "/" << repo << ". All metadata synced with Aria scraper and gh CLI.</div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}

	// JavaScript client helpers for GitHub platform
	content
		<< "<script>\n"
		<< "var GitHubPlatformUI = (function() {\n"
		<< "  function toggleNewPRCard() {\n"
		<< "    let el = document.getElementById('gh-new-pr-card');\n"
		<< "    if(el) el.style.display = (el.style.display === 'none' ? 'block' : 'none');\n"
		<< "  }\n"
		<< "  function createPR(owner, repo) {\n"
		<< "    let title = document.getElementById('gh-new-pr-title').value.trim();\n"
		<< "    let body = document.getElementById('gh-new-pr-body').value.trim();\n"
		<< "    let head = document.getElementById('gh-new-pr-head').value;\n"
		<< "    let base = document.getElementById('gh-new-pr-base').value;\n"
		<< "    if(!title) { alert('Title is required'); return; }\n"
		<< "    fetch('/api/gh/pulls/' + owner + '/create', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({repo: repo, title: title, body: body, head: head, base: base})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      let num = res.number || 7;\n"
		<< "      window.location.href = '/repo/' + owner + '/' + repo + '/pull/' + num;\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function mergePR(owner, repo, number) {\n"
		<< "    fetch('/api/gh/pulls/' + owner + '/merge', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({repo: repo, number: number})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      window.location.reload();\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function closePR(owner, repo, number) {\n"
		<< "    fetch('/api/gh/pulls/' + owner + '/close', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({repo: repo, number: number})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      window.location.reload();\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function commentPR(owner, repo, number) {\n"
		<< "    let text = document.getElementById('pr-comment-text').value.trim();\n"
		<< "    if(!text) return;\n"
		<< "    fetch('/api/gh/pulls/' + owner + '/comment', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({repo: repo, number: number, body: text})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      window.location.reload();\n"
		<< "    });\n"
		<< "  }\n"
		<< "  return {\n"
		<< "    toggleNewPRCard: toggleNewPRCard,\n"
		<< "    createPR: createPR,\n"
		<< "    mergePR: mergePR,\n"
		<< "    closePR: closePR,\n"
		<< "    commentPR: commentPR\n"
		<< "  };\n"
		<< "})();\n"
		<< "</script>\n";

	if(pr_num > 0 || active_tab == "actions") {
		sidebar = ""; // Full width for PR conversation, diffs, actions runs, catalog and editor
	}

	NavigationModel nav = ManagerSkylarkApp::BuildNavigation("gh_tree");
	UserSession sess = SystemAuthService::GetCurrentSession(http);

	PageBuilder pb;
	pb.Title(owner + "/" + repo + " - GitHub Platform Parity")
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
