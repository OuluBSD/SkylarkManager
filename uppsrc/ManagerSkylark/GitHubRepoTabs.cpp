#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "GitHubRepoPage.h"
#include "GitHubTemplates.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"

NAMESPACE_UPP

// Commits tab: recent commit history for the active branch. The markup is
// templates/github-commits.witz; this builds its data model.
void RenderRepoCommitsTab(GitHubRepoPage& page)
{
	Vector<ValueMap> commits = GitHubPlatformService::GetCommits(page.info.path, 30);
	ValueArray rows;
	for(const ValueMap& c : commits)
		rows.Add(c);

	Renderer vars;
	vars("branch", page.branch)("count", Value(commits.GetCount()))("commits", rows);
	page.content << RenderGitHubTemplate(vars, "github-commits");
}

// Actions tab: workflow catalog/editor or the workflow run table.
void RenderRepoActionsTab(Http& http, GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	const String& mode = page.mode;
	String& content = page.content;

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
		String actions_error;
		Vector<ValueMap> live_actions = GitHubPlatformService::GetLiveActions(owner, repo, &actions_error);
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Workflow Runs &middot; All Workflows</span>"
			<< "    <div style=\"display:flex; gap:10px; align-items:center;\">"
			<< "      <span class=\"wf-badge wf-badge-emerald\">" << (live_actions.IsEmpty() ? "No runs" : "Live Runs") << "</span>"
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
		}
		if(!actions_error.IsEmpty())
			content
				<< "        <tr><td colspan=\"5\">Workflow runs are unavailable from GitHub: " << GitHubPlatformService::EscapeHtml(actions_error) << "</td></tr>\n";
		else if(live_actions.IsEmpty())
			content
				<< "        <tr><td colspan=\"5\">No workflow runs were returned by GitHub.</td></tr>\n";
		content << "      </tbody>\n    </table>\n  </div>\n</div>\n";
	}
}

// Projects tab: Kanban view of the matching Manager project tasks. Each column
// shows its first five cards; the header counts are the full column sizes.
// The markup is templates/github-projects.witz.
void RenderRepoProjectsTab(GitHubRepoPage& page)
{
	const String& repo = page.repo;

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

	// Card rows for one column, capped at five.
	auto Cards = [](const Vector<ValueMap>& tasks) {
		ValueArray cards;
		for(int i = 0; i < min(tasks.GetCount(), 5); i++) {
			ValueMap card;
			card.Add("title", (String)tasks[i]["title"]);
			card.Add("seq", (int)tasks[i]["seq"]);
			card.Add("phase", (int)tasks[i]["phase_seq"]);
			cards.Add(card);
		}
		return cards;
	};

	Renderer vars;
	vars("repo", repo)
		("todo_count", todo_tasks.GetCount())
		("in_progress_count", in_progress_tasks.GetCount())
		("done_count", done_tasks.GetCount())
		("todo", Cards(todo_tasks))
		("in_progress", Cards(in_progress_tasks))
		("done", Cards(done_tasks));
	page.content << RenderGitHubTemplate(vars, "github-projects");
}

// Security tab: static security overview cards. The view reads no GitHub
// security data, so the template states that nothing was checked.
void RenderRepoSecurityTab(GitHubRepoPage& page)
{
	Renderer vars;
	page.content << RenderGitHubTemplate(vars, "github-security");
}

// Settings tab: general repository settings and danger zone. The view reads no
// visibility or feature state from GitHub, so the template says so. The markup
// is templates/github-settings.witz.
void RenderRepoSettingsTab(GitHubRepoPage& page)
{
	Renderer vars;
	vars("owner", page.owner)
		("repo", page.repo)
		("branch", page.branch);
	page.content << RenderGitHubTemplate(vars, "github-settings");
}

// SweAgent tab: pair and autonomous workspace runs.
void RenderRepoAgentsTab(Http& http, GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	const String& agent_run_id = page.agent_run_id;
	String& content = page.content;

	String author_filter = http["author"];
	String status_filter = http["status"];
	content << AgentPlatformService::RenderRunsDashboard(owner, repo, author_filter, status_filter, agent_run_id);
}

// Fallback for tabs without a dedicated view. The tab name comes from the URL,
// so the template escapes it. The markup is templates/github-fallback.witz.
void RenderRepoFallbackTab(GitHubRepoPage& page)
{
	const String& active_tab = page.active_tab;

	Renderer vars;
	vars("tab_title", ToUpper(active_tab.Left(1)) + active_tab.Mid(1))
		("owner", page.owner)
		("repo", page.repo)
		("active_tab", active_tab)
		("repo_path", page.owner + "/" + page.repo + ".");
	page.content << RenderGitHubTemplate(vars, "github-fallback");
}

END_UPP_NAMESPACE
