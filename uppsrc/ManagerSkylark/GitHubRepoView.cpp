#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "GitHubRepoPage.h"
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

	GitHubRepoPage page;
	page.owner = owner;
	page.repo = repo;
	page.info = info;
	page.scraped = scraped;
	page.active_tab = active_tab;
	page.mode = mode;
	page.branch = branch;
	page.file_or_folder = file_or_folder;
	page.issue_num = issue_num;
	page.pr_num = pr_num;
	page.agent_run_id = agent_run_id;
	page.sub_nav = sub_nav;

	if(active_tab == "code")
		RenderRepoCodeTab(page);
	else if(active_tab == "commits")
		RenderRepoCommitsTab(page);
	else if(active_tab == "issues" && issue_num > 0)
		RenderRepoIssueDetail(page);
	else if(active_tab == "issues")
		RenderRepoIssuesList(page);
	else if(active_tab == "pulls" && pr_num > 0)
		RenderRepoPullDetail(page);
	else if(active_tab == "pulls")
		RenderRepoPullsList(page);
	else if(active_tab == "actions")
		RenderRepoActionsTab(http, page);
	else if(active_tab == "projects")
		RenderRepoProjectsTab(page);
	else if(active_tab == "security")
		RenderRepoSecurityTab(page);
	else if(active_tab == "settings")
		RenderRepoSettingsTab(page);
	else if(active_tab == "agents")
		RenderRepoAgentsTab(http, page);
	else
		RenderRepoFallbackTab(page);

	RenderRepoClientScript(page);

	if(pr_num > 0 || active_tab == "actions") {
		page.sidebar = ""; // Full width for PR conversation, diffs, actions runs, catalog and editor
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
	  .Sidebar(page.sidebar)
	  .Content(page.content);
	ManagerSkylarkApp::AddMarkdownSupport(pb);
	String html = pb.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}


// Client-side helpers for the GitHub repo tabs (PR create/merge/close/comment).
void RenderRepoClientScript(GitHubRepoPage& page)
{
	String& content = page.content;
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
}

END_UPP_NAMESPACE
