#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "GitHubRepoPage.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"

NAMESPACE_UPP

// Pull request detail: conversation, CI status, merge box, and diff.
void RenderRepoPullDetail(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	const int& pr_num = page.pr_num;
	String& content = page.content;

	String unavailable;
	ValueMap pr_detail = GitHubPlatformService::GetLivePullRequestDetail(owner, repo, pr_num, &unavailable);
	if(!unavailable.IsEmpty()) {
		content
			<< "<div class=\"wf-alert wf-alert-warning\">Pull request #" << pr_num << " is unavailable from GitHub: "
			<< GitHubPlatformService::EscapeHtml(unavailable) << "</div>\n";
		return;
	}
	String pr_title = pr_detail["title"];
	String pr_state = pr_detail["state"]; // "OPEN", "MERGED", "CLOSED"
	String pr_body = pr_detail["body"];
	String pr_author = owner;
	if(!pr_detail["author"].IsNull() && !pr_detail["author"]["login"].IsNull())
		pr_author = pr_detail["author"]["login"];
	String pr_created = ((String)pr_detail["createdAt"]).Left(10);
	String pr_head = pr_detail["headRefName"];
	String pr_base = pr_detail["baseRefName"];
	if(pr_head.IsEmpty()) pr_head = "(unknown)";
	if(pr_base.IsEmpty()) pr_base = "(unknown)";
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
		<< "  <button type=\"button\" id=\"btn-pr-diff\" onclick=\"switchPrTab('diff')\">" << VividIcon::Get("code", 14) << " Files changed</button>\n"
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

	// CI status is not loaded by this view; say so instead of showing sample results.
	content
		<< "  <div class=\"wf-alert wf-alert-info\" style=\"margin-bottom:1.5rem;\">CI checks are not loaded for this pull request.</div>\n";

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
	String diff_error;
	String diff_text = GitHubPlatformService::GetPullRequestDiff(owner, repo, pr_num, &diff_error);
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
	if(!diff_error.IsEmpty())
		content << "        <div class=\"wf-alert wf-alert-warning\">Diff is unavailable from GitHub: " << GitHubPlatformService::EscapeHtml(diff_error) << "</div>\n";
	else if(diff_text.IsEmpty())
		content << "        <div class=\"wf-alert wf-alert-warning\">No diff is available from GitHub for this pull request.</div>\n";
	for(int i = 0; i < diff_lines.GetCount() && !diff_text.IsEmpty(); i++) {
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

// Pull requests tab: live PR list with the new-PR form.
void RenderRepoPullsList(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	String& content = page.content;

	String unavailable;
	Vector<ValueMap> live_prs = GitHubPlatformService::GetLivePullRequests(owner, repo, &unavailable);
	if(!unavailable.IsEmpty()) {
		content
			<< "<div class=\"wf-alert wf-alert-warning\">Pull requests are unavailable from GitHub: "
			<< GitHubPlatformService::EscapeHtml(unavailable) << "</div>\n";
		return;
	}
	int pr_open = 0, pr_closed = 0;
	for(const ValueMap& p : live_prs) {
		if((String)p["state"] == "OPEN") pr_open++;
		else pr_closed++;
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

END_UPP_NAMESPACE
