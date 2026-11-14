#include "GitHubPlatform.h"
#include "GitHubRepoPage.h"
#include <GitHubForge/GitHubTemplates.h>
#include <WebFramework/WebFramework.h>

NAMESPACE_UPP

// Issue detail: issue header, body, comments, and comment/close controls.
// Markup is templates/github-issue-detail.witz.
void RenderRepoIssueDetail(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	const int& issue_num = page.issue_num;

	String unavailable;
	ValueMap is_detail = GitHubPlatformService::GetLiveIssueDetail(owner, repo, issue_num, &unavailable);
	String is_title = is_detail["title"];
	String is_state = is_detail["state"];
	String is_body = is_detail["body"];
	String is_author = owner;
	if(!is_detail["author"].IsNull() && !is_detail["author"]["login"].IsNull())
		is_author = is_detail["author"]["login"];
	String is_created = ((String)is_detail["createdAt"]).Left(10);
	bool is_open = (is_state == "OPEN");

	ValueArray comments;
	ValueArray detail_comments = is_detail["comments"];
	for(int i = 0; i < detail_comments.GetCount(); i++) {
		Value c = detail_comments[i];
		ValueMap row;
		row.Add("author", !c["author"].IsNull() && !c["author"]["login"].IsNull() ? (String)c["author"]["login"] : owner);
		row.Add("time", ((String)c["createdAt"]).Left(10));
		row.Add("body", c["body"]);
		comments.Add(row);
	}

	Renderer vars;
	vars("has_unavailable", !unavailable.IsEmpty())("unavailable", GitHubPlatformService::EscapeHtml(unavailable))("issue_num", issue_num)
		("owner", owner)("repo", repo)("js_owner", GitHubJsEscape(owner))("js_repo", GitHubJsEscape(repo))
		("icon_chevron_left", VividIcon::Get("chevron-left", 14))
		("title", is_title)
		("state_bg", is_open ? "#238636" : "#8957e5")
		("state_icon", VividIcon::Get(is_open ? "alert-circle" : "check", 14))
		("state_label", is_open ? "Open" : "Closed")
		("author", is_author)("created", is_created)
		("has_body", !is_body.IsEmpty())("issue_body", is_body)
		("comments", comments)
		("toggle_color", is_open ? "#f85149" : "#3fb950")
		("toggle_label", is_open ? "Close issue" : "Reopen issue")
		("close_flag", is_open ? "true" : "false");
	page.content << RenderGitHubTemplate(vars, "github-issue-detail");
}

// Issues tab: live issue list with the create-issue form.
// Markup is templates/github-issues-list.witz.
void RenderRepoIssuesList(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;

	String unavailable;
	Vector<ValueMap> live_issues = GitHubPlatformService::GetLiveIssues(owner, repo, &unavailable);
	int open_count = 0, closed_count = 0;
	ValueArray rows;
	for(const ValueMap& is : live_issues) {
		bool is_open = ((String)is["state"] == "OPEN");
		if(is_open) open_count++;
		else closed_count++;
		int num = (int)is["number"];
		ValueMap row;
		row.Add("tone", is_open ? "#238636" : "#8957e5");
		row.Add("icon", VividIcon::Get(is_open ? "alert-circle" : "check", 16));
		row.Add("href", "/repo/" + owner + "/" + repo + "/issues/" + AsString(num));
		row.Add("title", is["title"]);
		row.Add("num", num);
		row.Add("created", is["created_at"]);
		row.Add("author", is["author"]);
		row.Add("comments", (int)is["comments"]);
		rows.Add(row);
	}

	Renderer vars;
	vars("owner", owner)("repo", repo)("js_owner", GitHubJsEscape(owner))("js_repo", GitHubJsEscape(repo))
		("has_unavailable", !unavailable.IsEmpty())("unavailable", GitHubPlatformService::EscapeHtml(unavailable))
		("open_count", open_count)("closed_count", closed_count)
		("icon_alert_circle", VividIcon::Get("alert-circle", 16))
		("icon_check", VividIcon::Get("check", 16))
		("comment_icon", VividIcon::Get("message-square", 14))
		("issues", rows);
	page.content << RenderGitHubTemplate(vars, "github-issues-list");
}

END_UPP_NAMESPACE
