#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "GitHubRepoPage.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"

NAMESPACE_UPP

// Code tab: branch bar, blob viewer, directory tree, and About sidebar.
void RenderRepoCodeTab(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	const String& branch = page.branch;
	const String& file_or_folder = page.file_or_folder;
	const String& mode = page.mode;
	const LocalRepoInfo& info = page.info;
	const ValueMap& scraped = page.scraped;
	String& content = page.content;
	String& sidebar = page.sidebar;

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
		<< "  <div>" << VividIcon::Get("star", 14) << " Stars: <strong>" << (info.stars < 0 ? String("not loaded") : AsString(info.stars)) << "</strong></div>\n"
		<< "  <div>" << VividIcon::Get("git-fork", 14) << " Forks: <strong>" << (info.forks < 0 ? String("not loaded") : AsString(info.forks)) << "</strong></div>\n"
		<< "</div>\n"
		<< "<div class=\"wf-gh-sidebar-section\">\n"
		<< "  <span class=\"wf-gh-sidebar-title\">Languages</span>\n"
		<< "  <div style=\"font-size:0.85rem;\"><span style=\"display:inline-block; width:10px; height:10px; border-radius:50%; background:#f34b7d; margin-right:4px;\"></span> " << info.language << "</div>\n"
		<< "</div>\n";
}

END_UPP_NAMESPACE
