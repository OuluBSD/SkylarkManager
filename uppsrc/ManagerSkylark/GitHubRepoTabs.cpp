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

// Security tab: static security overview cards.
void RenderRepoSecurityTab(GitHubRepoPage& page)
{
	String& content = page.content;

	content
		<< "<div class=\"wf-card\">\n"
		<< "  <div class=\"wf-card-header\"><span>Security Overview</span><span class=\"wf-badge wf-badge-neutral\">Not loaded</span></div>\n"
		<< "  <div class=\"wf-card-body\">\n"
		<< "    <p style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590); margin:0 0 16px 0;\">This view does not read GitHub security data, so nothing below has been checked.</p>\n"
		<< "    <div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(240px, 1fr)); gap:16px;\">\n"
		<< "      <div style=\"background:var(--wf-surface,#161b22); padding:16px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
		<< "        <h4>Dependabot Alerts</h4>\n"
		<< "        <div style=\"font-size:1.8rem; font-weight:700; color:var(--wf-fg-inactive,#7d8590);\">Not loaded</div>\n"
		<< "        <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin:0;\">Dependency alerts are not read by this view.</p>\n"
		<< "      </div>\n"
		<< "      <div style=\"background:var(--wf-surface,#161b22); padding:16px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
		<< "        <h4>Secret Scanning</h4>\n"
		<< "        <div style=\"font-size:1.8rem; font-weight:700; color:var(--wf-fg-inactive,#7d8590);\">Not performed</div>\n"
		<< "        <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin:0;\">Secret scan: not performed.</p>\n"
		<< "      </div>\n"
		<< "      <div style=\"background:var(--wf-surface,#161b22); padding:16px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
		<< "        <h4>Code Scanning</h4>\n"
		<< "        <div style=\"font-size:1.8rem; font-weight:700; color:var(--wf-fg-inactive,#7d8590);\">Not loaded</div>\n"
		<< "        <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin:0;\">Code scanning results are not read by this view.</p>\n"
		<< "      </div>\n"
		<< "    </div>\n"
		<< "  </div>\n"
		<< "</div>\n";
}

// Settings tab: general repository settings and danger zone.
void RenderRepoSettingsTab(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	const String& branch = page.branch;
	String& content = page.content;

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
		<< "    <p style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590); margin-bottom:20px;\">Feature settings are not read from GitHub by this view.</p>\n"
		<< "    <h4 style=\"color:#f85149;\">Danger Zone</h4>\n"
		<< "    <div style=\"border:1px solid rgba(248,81,73,0.4); border-radius:6px; padding:12px;\">\n"
		<< "      <div style=\"font-weight:600;\">Change repository visibility</div>\n"
		<< "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590);\">Visibility changes are not available from this view.</div>\n"
		<< "    </div>\n"
		<< "  </div>\n"
		<< "</div>\n";
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

// Fallback for tabs without a dedicated view.
void RenderRepoFallbackTab(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	const String& active_tab = page.active_tab;
	String& content = page.content;

	content
		<< "<div class=\"wf-card\">\n"
		<< "  <div class=\"wf-card-header\"><span>" << ToUpper(active_tab.Left(1)) << active_tab.Mid(1) << " &middot; " << owner << "/" << repo << "</span></div>\n"
		<< "  <div class=\"wf-card-body\">\n"
		<< "    <div class=\"wf-alert wf-alert-info\">Displaying " << active_tab << " platform view for " << owner << "/" << repo << ". This tab has no dedicated view yet.</div>\n"
		<< "  </div>\n"
		<< "</div>\n";
}

END_UPP_NAMESPACE
