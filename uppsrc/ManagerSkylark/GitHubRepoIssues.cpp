#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "GitHubRepoPage.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"

NAMESPACE_UPP

// Issue detail: issue header, body, comments, and comment/close controls.
void RenderRepoIssueDetail(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	const int& issue_num = page.issue_num;
	String& content = page.content;

	String unavailable;
	ValueMap is_detail = GitHubPlatformService::GetLiveIssueDetail(owner, repo, issue_num, &unavailable);
	if(!unavailable.IsEmpty()) {
		content
			<< "<div class=\"wf-alert wf-alert-warning\">Issue #" << issue_num << " is unavailable from GitHub: "
			<< GitHubPlatformService::EscapeHtml(unavailable) << "</div>\n";
		return;
	}
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

// Issues tab: live issue list with the create-issue form.
void RenderRepoIssuesList(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	String& content = page.content;

	String unavailable;
	Vector<ValueMap> live_issues = GitHubPlatformService::GetLiveIssues(owner, repo, &unavailable);
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
		<< "</script>\n";
	if(!unavailable.IsEmpty()) {
		content << "<div class=\"wf-alert wf-alert-warning\">GitHub issues are unavailable: " << GitHubPlatformService::EscapeHtml(unavailable) << "</div>\n";
		return;
	}

	content
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

END_UPP_NAMESPACE
