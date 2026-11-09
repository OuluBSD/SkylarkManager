#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "GitHubRepoPage.h"
#include "GitHubTemplates.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"

NAMESPACE_UPP

// Pull request detail: conversation, CI status, merge box, and diff.
// Markup is templates/github-pull-detail.witz.
void RenderRepoPullDetail(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	const int& pr_num = page.pr_num;

	String unavailable;
	ValueMap pr_detail = GitHubPlatformService::GetLivePullRequestDetail(owner, repo, pr_num, &unavailable);
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

	ValueArray body_items;
	ValueArray empty_items;
	if(pr_body.IsEmpty())
		empty_items.Add(ValueMap());
	else {
		ValueMap body;
		body.Add("body_text", pr_body);
		body_items.Add(body);
	}

	ValueArray comments;
	ValueArray detail_comments = pr_detail["comments"];
	for(int i = 0; i < detail_comments.GetCount(); i++) {
		Value c = detail_comments[i];
		ValueMap row;
		row.Add("author", !c["author"].IsNull() && !c["author"]["login"].IsNull() ? (String)c["author"]["login"] : owner);
		row.Add("time", ((String)c["createdAt"]).Left(10));
		row.Add("body", (String)c["body"]);
		comments.Add(row);
	}

	// Files Changed: diff lines are tinted by their prefix.
	String diff_error;
	String diff_text;
	if(unavailable.IsEmpty())
		diff_text = GitHubPlatformService::GetPullRequestDiff(owner, repo, pr_num, &diff_error);
	ValueArray diff_rows;
	Vector<String> diff_lines = Split(diff_text, '\n');
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
		ValueMap row;
		row.Add("bg", line_bg);
		row.Add("fg", line_fg);
		row.Add("text", line);
		diff_rows.Add(row);
	}

	Renderer vars;
	vars("has_unavailable", !unavailable.IsEmpty())("unavailable", GitHubPlatformService::EscapeHtml(unavailable))
		("owner", owner)("repo", repo)("pr_num", pr_num)
		("js_owner", GitHubJsEscape(owner))("js_repo", GitHubJsEscape(repo))
		("icon_chevron_left", VividIcon::Get("chevron-left", 14))
		("title", pr_title)
		("state_bg", is_open ? "#238636" : (is_merged ? "#8957e5" : "#f85149"))
		("state_icon", VividIcon::Get(is_open ? "git-pull-request" : (is_merged ? "check" : "x"), 14))
		("state_label", is_open ? "Open" : (is_merged ? "Merged" : "Closed"))
		("author", pr_author)("created", pr_created)
		("base", pr_base)("head", pr_head)("diff_range", pr_base + "..." + pr_head)
		("icon_message_square", VividIcon::Get("message-square", 14))
		("icon_code", VividIcon::Get("code", 14))
		("markdown_text", pr_body.IsEmpty() ? String("No description provided.") : pr_body)
		("body_items", body_items)
		("empty_items", empty_items)
		("comments", comments)
		("is_open", is_open)("is_merged", is_merged)
		("icon_git_pr_18", VividIcon::Get("git-pull-request", 18))
		("icon_git_pr_15", VividIcon::Get("git-pull-request", 15))
		("icon_check_18", VividIcon::Get("check", 18))
		("has_diff_error", !diff_error.IsEmpty())
		("diff_error", GitHubPlatformService::EscapeHtml(diff_error))
		("show_no_diff", diff_error.IsEmpty() && diff_text.IsEmpty())
		("diff_rows", diff_rows);
	page.content << RenderGitHubTemplate(vars, "github-pull-detail");
}

// Pull requests tab: live PR list with the new-PR form.
// Markup is templates/github-pulls-list.witz.
void RenderRepoPullsList(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;

	String unavailable;
	Vector<ValueMap> live_prs = GitHubPlatformService::GetLivePullRequests(owner, repo, &unavailable);
	int pr_open = 0, pr_closed = 0;
	ValueArray rows;
	for(const ValueMap& p : live_prs) {
		bool is_open = ((String)p["state"] == "OPEN");
		bool is_merged = ((String)p["state"] == "MERGED");
		if(is_open) pr_open++;
		else pr_closed++;
		int p_num = (int)p["number"];
		ValueMap row;
		row.Add("tone", is_open ? "#238636" : (is_merged ? "#8957e5" : "#f85149"));
		row.Add("icon", VividIcon::Get(is_open ? "git-pull-request" : (is_merged ? "check" : "x"), 16));
		row.Add("href", "/repo/" + owner + "/" + repo + "/pull/" + AsString(p_num));
		row.Add("num", p_num);
		row.Add("title", (String)p["title"]);
		row.Add("created", (String)p["created_at"]);
		row.Add("author", (String)p["author"]);
		row.Add("head", (String)p["head"]);
		row.Add("base", (String)p["base"]);
		row.Add("badge", is_open ? "emerald" : "neutral");
		row.Add("label", is_open ? "Review Pending" : (is_merged ? "Merged" : "Closed"));
		rows.Add(row);
	}

	Renderer vars;
	vars("owner", owner)("repo", repo)("js_owner", GitHubJsEscape(owner))("js_repo", GitHubJsEscape(repo))
		("has_unavailable", !unavailable.IsEmpty())("unavailable", GitHubPlatformService::EscapeHtml(unavailable))
		("icon_plus", VividIcon::Get("plus", 14))
		("icon_pr", VividIcon::Get("git-pull-request", 16))
		("icon_check", VividIcon::Get("check", 16))
		("pr_open", pr_open)("pr_closed", pr_closed)
		("prs", rows);
	page.content << RenderGitHubTemplate(vars, "github-pulls-list");
}

END_UPP_NAMESPACE
