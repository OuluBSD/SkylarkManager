#include "ManagerSkylark.h"
#include "SystemAuth.h"
#include "AssessmentView.h"
#include "CandidateWorkflow.h"
#include "ReviewDiscussion.h"

NAMESPACE_UPP

SKYLARK(CandidateWorkflowPage, "candidate-workflow")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	String candidate_id = http["candidate"];
	ValueMap view = candidate_id.IsEmpty() ? ValueMap() : CandidateWorkflow::Project(catalog->data, app, (String)http["workspace"], candidate_id);
	if(view.IsEmpty()) { view.Add("status", "unknown"); view.Add("reason", "Select a candidate from a workspace-scoped route or create a draft through the API."); }
	String body = CandidateWorkflow::Render(view);
	body << "<style>.cw-panel{max-width:54rem;margin:1rem auto;padding:1.25rem;border:1px solid var(--wf-border-color);border-radius:.75rem;color:var(--wf-fg-color)}.cw-panel p{line-height:1.55}</style>";
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Proposal Workspace", "candidate-workflow", body, &http);
}

static ValueMap BuildAssessmentHttpView(Http& http, ManagerSkylarkApp& app, const CatalogGeneration& catalog, const String& detail=String())
{
	return AssessmentViewService::Build(app,catalog,(String)http["workspace"],(String)http["run"],
	                                   (String)http["filter"],(String)http["sort"],(String)http["direction"],(String)http["dimension"],detail);
}

SKYLARK(ReviewOverviewPage,"reviews")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); auto catalog=app.PinCatalog();
	ValueMap view=ReviewPackageViewService::Build(app,*catalog);
	http.ContentType("text/html; charset=utf-8")<<ManagerSkylarkApp::RenderHtmlPage("Review Packages","reviews",ReviewPackageViewService::Render(view,false),&http);
}

SKYLARK(ReviewPackagePage,"review-package/*/*")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); auto catalog=app.PinCatalog();
	ValueMap view=ReviewPackageViewService::Build(app,*catalog,(String)http[0],(String)http[1]);
	if(MapGetString(view,"view_state")=="unavailable") http.Response(404,"Review Package Unavailable");
	http.ContentType("text/html; charset=utf-8")<<ManagerSkylarkApp::RenderHtmlPage("Review Discussion","reviews",ReviewPackageViewService::Render(view,true),&http);
}

SKYLARK(ReviewThreadPage,"review-thread/*")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); auto catalog=app.PinCatalog();
	ValueMap view=ReviewPackageViewService::BuildThread(app,*catalog,(String)http[0]);
	if(MapGetString(view,"view_state")=="unavailable") http.Response(404,"Review Thread Unavailable");
	http.ContentType("text/html; charset=utf-8")<<ManagerSkylarkApp::RenderHtmlPage("Review Thread","reviews",ReviewPackageViewService::Render(view,true),&http);
}

SKYLARK(AssessmentOverviewPage,"assessments")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); auto catalog=app.PinCatalog();
	ValueMap view=BuildAssessmentHttpView(http,app,*catalog);
	http.ContentType("text/html; charset=utf-8")<<ManagerSkylarkApp::RenderHtmlPage("Research Assessments","assessments",AssessmentViewService::Render(view,"overview"),&http);
}

SKYLARK(AssessmentDetailPage,"assessments/detail/*")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); auto catalog=app.PinCatalog();
	ValueMap view=BuildAssessmentHttpView(http,app,*catalog,(String)http[0]);
	http.ContentType("text/html; charset=utf-8")<<ManagerSkylarkApp::RenderHtmlPage("Assessment Detail","assessments",AssessmentViewService::Render(view,"detail"),&http);
}

SKYLARK(AssessmentComparePage,"assessments/compare")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); auto catalog=app.PinCatalog();
	ValueMap view=BuildAssessmentHttpView(http,app,*catalog);
	http.ContentType("text/html; charset=utf-8")<<ManagerSkylarkApp::RenderHtmlPage("Candidate Comparison","assessments",AssessmentViewService::Render(view,"compare"),&http);
}

static String WebViewPath(const String& route, const String& path)
{
	if(route.IsEmpty() || route[0] != '/' || route.StartsWith("//"))
		return String();
	String out = route;
	Vector<String> parts = Split(path, '/');
	for(int i = 0; i < parts.GetCount(); i++) {
		if(parts[i].IsEmpty())
			continue;
		if(!out.EndsWith("/")) out << "/";
		out << UrlEncode(parts[i]);
	}
	return out;
}

SKYLARK(HomePage, "")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();
	auto catalog = app.PinCatalog();
	const V2Store& v2 = catalog->v2;
	const DataStore& data = catalog->data;
	UserSession session = SystemAuthService::GetCurrentSession(http);

	int local_project_count = local.Projects().GetCount();
	int structured_document_count = data.GetDocuments().GetCount();
	int topology_count = data.GetKindCount("topology");
	int paper_count = data.GetKindCount("paper");
	int idea_count = data.GetKindCount("task-proposal") + data.GetKindCount("research-candidate");

	String b;
	// Hero / Ecosystem Command Center Banner
	b << "<div class=\"card\" style=\"background: linear-gradient(135deg, rgba(24,32,48,0.95), rgba(28,32,47,0.95)); border: 1px solid var(--wf-accent-focus, #00a1c5); box-shadow: 0 4px 20px rgba(0,161,197,0.15); margin-bottom: 1.5rem;\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px;\">\n"
	  << "    <div>\n"
	  << "      <div style=\"display:flex; align-items:center; gap:10px; margin-bottom:4px;\">\n"
	  << "        <span style=\"font-size:1.5rem; font-weight:800; color:var(--wf-fg-normal, #ffffff);\">Ecosystem Command Center</span>\n"
	  << "        <span class=\"badge-v2\" style=\"background:var(--wf-color-positive,#00aa7f);\">ONLINE</span>\n"
	  << "      </div>\n"
	  << "      <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem; margin:0;\">\n"
	  << "        Project and repository navigation with local task tracking. Agent execution is currently disabled.\n"
	  << "      </p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; align-items:center; gap:8px; font-size:0.82rem; background:var(--wf-bg-alt,#1c202f); padding:6px 14px; border-radius:6px; border:1px solid var(--wf-border-color,rgba(0,161,197,0.2));\">\n"
	  << "      <span>Structured records: <strong>" << structured_document_count << "</strong></span> &bull; \n"
	  << "      <span>User: <strong style=\"color:var(--wf-accent-focus,#00a1c5);\">" << EscapeHtml(session.is_authenticated ? session.username : String("Guest (Public)")) << "</strong></span> &bull; \n"
	  << "      <span>" << (session.is_authenticated ? "<span style=\"color:#00aa7f;\">&bull; Authenticated</span>" : "<a href=\"/login\" style=\"color:#00acd3;\">Sign In</a>") << "</span>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// 5 Primary Pillars Grid
	b << "<div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(230px, 1fr)); gap:1rem; margin-bottom: 1.5rem;\">\n";

	// Pillar 1: Self-Hosted Local Node
	b << "  <div class=\"stat-card\" style=\"border-left: 4px solid var(--wf-accent-focus, #00a1c5);\">\n"
	  << "    <div style=\"display:flex; justify-content:space-between; align-items:flex-start;\">\n"
	  << "      <span class=\"stat-lbl\">Self-Hosted Machine</span>\n"
	  << "      <span style=\"color:var(--wf-accent-focus,#00a1c5);\">" << VividIcon::Get("monitor", 20) << "</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; align-items:baseline; gap:8px; margin: 4px 0;\">\n"
	  << "      <span class=\"stat-val\">" << local_project_count << "</span>\n"
	  << "      <span style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d);\">local workspaces</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:12px; line-height:1.4;\">\n"
	  << "      Local workstation with read-only host metadata and storage summaries. File browsing and remote operations are disabled.\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:6px; flex-wrap:wrap; margin-top:auto;\">\n"
	  << "      <a href=\"/host/overview\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:4px 8px;\">Overview</a>\n"
	  << "      <span class=\"wf-badge\">Files disabled</span>\n"
	  << "      <a href=\"/host/repos\" class=\"btn\" style=\"font-size:0.75rem; padding:4px 8px; background:var(--wf-surface,#1e283b); color:var(--wf-fg-normal,#fff);\">Repos</a>\n"
	  << "    </div>\n"
	  << "  </div>\n";

	// Structured workspace summaries come from the configured stores.
	b << "  <div class=\"stat-card\" style=\"border-left:4px solid #38bdf8;\"><div class=\"stat-lbl\">Topology records</div>"
	  << "<div class=\"stat-val\" style=\"color:#38bdf8;\">" << topology_count << "</div>"
	  << "<p>Architecture maps from configured structured sources.</p><a href=\"/data/diagrams\" class=\"btn\">Browse topology maps</a></div>\n";
	b << "  <div class=\"stat-card\" style=\"border-left:4px solid #6d55aa;\"><div class=\"stat-lbl\">Task ideas</div>"
	  << "<div class=\"stat-val\" style=\"color:#a78bfa;\">" << idea_count << "</div>"
	  << "<p>Research proposals and candidates with comparison tiers.</p><a href=\"/data/ideas\" class=\"btn\">Compare ideas</a></div>\n";
	b << "  <div class=\"stat-card\" style=\"border-left:4px solid #ee6524;\"><div class=\"stat-lbl\">Paper records</div>"
	  << "<div class=\"stat-val\" style=\"color:#fb923c;\">" << paper_count << "</div>"
	  << "<p>Paper and analysis records with explicit research stages.</p><a href=\"/data/papers\" class=\"btn\">Browse research</a></div>\n";
	b << "  <div class=\"stat-card\" style=\"border-left:4px solid #00aa7f;\"><div class=\"stat-lbl\">Goals</div>"
	  << "<div class=\"stat-val\" style=\"color:#34d399;\">" << v2.GetGoals().GetCount() << "</div>"
	  << "<p>Structured outcomes, dependencies and acceptance criteria.</p><a href=\"/goals\" class=\"btn\">Review goals</a></div>\n";
	b << "  <div class=\"stat-card\" style=\"border-left:4px solid var(--wf-accent-focus,#00a1c5);\"><div class=\"stat-lbl\">Local workspaces</div>"
	  << "<div class=\"stat-val\">" << local_project_count << "</div>"
	  << "<p>Implementation completion is separate from human acceptance.</p><a href=\"/projects\" class=\"btn\">Browse workspaces</a></div>\n";

	b << "</div>\n";

	// Quick links to structured workspace views.
	b << "<div class=\"card\" style=\"margin-bottom: 1.5rem; padding: 1rem 1.25rem;\">\n"
	  << "  <div style=\"display:flex; align-items:center; justify-content:space-between; flex-wrap:wrap; gap:10px;\">\n"
	  << "    <div style=\"font-weight:700; font-size:0.95rem; color:var(--wf-fg-normal,#fff); display:flex; align-items:center; gap:8px;\">\n"
	  << "      " << VividIcon::Get("zap", 18) << " Workspace views\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px; flex-wrap:wrap;\">\n"
	  << "      <a href=\"/data\" class=\"btn btn-primary\" style=\"font-size:0.8rem;\">" << VividIcon::Get("database", 14) << " Data explorer</a>\n"
	  << "      <a href=\"/data/papers\" class=\"btn\" style=\"font-size:0.8rem;\">" << VividIcon::Get("book-open", 14) << " Paper mining</a>\n"
	  << "      <a href=\"/data/ideas\" class=\"btn\" style=\"font-size:0.8rem;\">" << VividIcon::Get("layers", 14) << " Candidate comparison</a>\n"
	  << "      <a href=\"/goals\" class=\"btn\" style=\"font-size:0.8rem;\">" << VividIcon::Get("target", 14) << " Goals</a>\n"
	  << "      <a href=\"/projects\" class=\"btn\" style=\"font-size:0.8rem;\">" << VividIcon::Get("folder", 14) << " Local workspaces</a>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Current Manager Tasks & Local Work
	b << "<div class=\"grid-2\">\n";

	// Left: Active Manager Projects (Local Shadow Drafts & GitHub Integration)
	b << "  <div class=\"card\">\n"
	  << "    <div class=\"card-header\"><span>Local Workspaces</span><a href=\"/projects\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:3px 8px;\">All Workspaces</a></div>\n"
	  << "    <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:10px;\">Implementation records stay local. External publication and human acceptance are tracked separately.</p>\n"
	  << "    <table><thead><tr><th>Workspace</th><th>Implementation State</th><th>Tasks</th><th>Actions</th></tr></thead><tbody>\n";
	const Vector<Project>& projs = local.Projects();
	for(int i = 0; i < projs.GetCount(); i++) {
		const Project& p = projs[i];
		Vector<const Task*> tasks = local.TasksFor(p.slug);
		int done_cnt = 0;
		for(int t = 0; t < tasks.GetCount(); t++)
			if(tasks[t]->status == "done") done_cnt++;

		bool is_all_done = (!tasks.IsEmpty() && done_cnt == tasks.GetCount());
		String state_badge = is_all_done
			? "<span class=\"badge-v2\" style=\"background:#059669; color:#fff;\">Implementation complete &bull; review separate</span>"
			: "<span class=\"badge-v2\" style=\"background:#2563eb; color:#fff;\">In progress (" + AsString(done_cnt) + "/" + AsString(tasks.GetCount()) + ")</span>";

		b << "<tr>"
		  << "<td><strong><a href=\"" << WebViewPath("/project", p.slug) << "\">" << EscapeHtml(p.title) << "</a></strong><br><small style=\"color:var(--wf-fg-inactive,#8b919d);\"><code>" << EscapeHtml(p.slug) << "</code></small></td>"
		  << "<td>" << state_badge << "</td>"
		  << "<td>" << ManagerSkylarkApp::RenderProgressBar(tasks.IsEmpty() ? 0.0 : (100.0 * done_cnt / tasks.GetCount())) << " (" << done_cnt << "/" << tasks.GetCount() << ")</td>"
		  << "<td><a href=\"" << WebViewPath("/project", p.slug) << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Open Workspace</a></td>"
		  << "</tr>\n";
	}
	b << "    </tbody></table>\n  </div>\n";

	// Right: Local Machine Status & Distributed Sync Summary
	b << "  <div class=\"card\">\n"
	  << "    <div class=\"card-header\"><span>Local Machine &amp; Distributed Status</span><a href=\"/host/overview\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:3px 8px;\">Host Details</a></div>\n"
	  << "    <div style=\"display:flex; flex-direction:column; gap:10px; font-size:0.88rem;\">\n"
	  << "      <div style=\"display:flex; justify-content:space-between; padding:8px; background:var(--wf-bg-alt,#1c202f); border-radius:6px;\">"
	  << "        <span style=\"color:var(--wf-fg-inactive,#8b919d);\">Local operations</span><strong>Read-only tools</strong>"
	  << "      </div>\n"
	  << "      <div style=\"display:flex; justify-content:space-between; padding:8px; background:var(--wf-bg-alt,#1c202f); border-radius:6px;\">"
	  << "        <span style=\"color:var(--wf-fg-inactive,#8b919d);\">Configured data</span><strong>Private source paths hidden</strong>"
	  << "      </div>\n"
	  << "      <div style=\"display:flex; justify-content:space-between; padding:8px; background:var(--wf-bg-alt,#1c202f); border-radius:6px;\">"
	  << "        <span style=\"color:var(--wf-fg-inactive,#8b919d);\">Indexed records</span><span><strong>" << structured_document_count << "</strong> across configured sources</span>"
	  << "      </div>\n"
	  << "      <div style=\"display:flex; justify-content:space-between; padding:8px; background:var(--wf-bg-alt,#1c202f); border-radius:6px;\">"
	  << "        <span style=\"color:var(--wf-fg-inactive,#8b919d);\">External operations</span><span class=\"wf-badge\">Disabled pending policy</span>"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "  </div>\n";

	b << "</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Ecosystem Command Center", "home", b, &http);
}

SKYLARK(ProjectsPage, "projects")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();

	String b;
	b << "<div class=\"card\">\n"
  << "  <div class=\"card-header\"><span>Local Workspaces</span></div>\n"
  << "  <p style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:1rem;\">Implementation records stay local. External publication and human acceptance are tracked separately.</p>\n"
  << "  <table><thead><tr><th>Slug</th><th>Workspace</th><th>Implementation State</th><th>Tasks</th><th>Actions</th></tr></thead><tbody>\n";

	const Vector<Project>& projs = local.Projects();
	for(int i = 0; i < projs.GetCount(); i++) {
		const Project& p = projs[i];
		Vector<const Phase*> phases = local.PhasesFor(p.slug);
		Vector<const Task*>  tasks  = local.TasksFor(p.slug);

		int done = 0;
		for(int t = 0; t < tasks.GetCount(); t++)
			if(tasks[t]->status == "done") done++;

		double pct = tasks.IsEmpty() ? 0.0 : (100.0 * done / tasks.GetCount());
		bool is_all_done = (!tasks.IsEmpty() && done == tasks.GetCount());
		String state_badge = is_all_done
			? "<span class=\"badge-v2\" style=\"background:#059669; color:#fff;\">Implementation complete &bull; review separate</span>"
			: "<span class=\"badge-v2\" style=\"background:#2563eb; color:#fff;\">In progress</span>";

		b << "<tr>"
		  << "<td><code>" << EscapeHtml(p.slug) << "</code></td>"
		  << "<td><strong><a href=\"" << WebViewPath("/project", p.slug) << "\">" << EscapeHtml(p.title) << "</a></strong></td>"
		  << "<td>" << state_badge << "</td>"
		  << "<td>" << ManagerSkylarkApp::RenderProgressBar(pct) << " (" << done << "/" << tasks.GetCount() << ")</td>"
		  << "<td><a href=\"" << WebViewPath("/project", p.slug) << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Open Workspace</a></td>"
		  << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Projects", "projects", b, &http);
}

SKYLARK(ProjectDetailPage, "project/*")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();
	String slug = http[0];

	const Project* p = local.FindProject(slug);
	if(!p) {
		http.Redirect(ProjectsPage);
		return;
	}

	String b;

	// Generic local workspace header; repository bindings are separate structured records.

	Vector<const Task*> all_tasks = local.TasksFor(slug);
	int done_cnt = 0;
	for(int t = 0; t < all_tasks.GetCount(); t++)
		if(all_tasks[t]->status == "done") done_cnt++;

	bool implementation_done = (!all_tasks.IsEmpty() && done_cnt == all_tasks.GetCount());

	b << "<div class=\"card\" style=\"margin-bottom:1.5rem; border-left:4px solid #6366f1;\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:flex-start; flex-wrap:wrap; gap:12px; margin-bottom:1rem;\">\n"
	  << "    <div>\n"
	  << "      <div style=\"font-size:0.75rem; text-transform:uppercase; letter-spacing:0.05em; color:var(--wf-fg-inactive,#8b919d); margin-bottom:4px;\">Local Workspace</div>\n"
	  << "      <h2 style=\"margin:0; font-size:1.35rem; display:flex; align-items:center; gap:8px;\">" << EscapeHtml(p->title)
	  << "        <span style=\"font-size:0.8rem; font-weight:normal; background:var(--wf-bg-alt,#1c202f); border:1px solid var(--wf-border-color,#30363d); padding:2px 8px; border-radius:4px; color:#cbd5e1;\"><code>" << EscapeHtml(slug) << "</code></span>\n"
	  << "      </h2>\n"
	  << "      <p style=\"color:var(--wf-fg-inactive,#8b919d); margin:6px 0 0 0; font-size:0.88rem;\">" << EscapeHtml(p->description) << "</p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px; align-items:center;\">\n"
	  << "      " << (implementation_done ? "<span class=\"badge-v2\" style=\"background:#059669; color:#fff;\">Implementation complete &bull; review separate</span>" : "<span class=\"badge-v2\" style=\"background:#2563eb; color:#fff;\">In progress (" + AsString(done_cnt) + "/" + AsString(all_tasks.GetCount()) + ")</span>") << "\n"
	  << "      <span class=\"wf-badge\">External publication disabled</span>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(210px, 1fr)); gap:12px; font-size:0.84rem; padding-top:12px; border-top:1px solid var(--wf-border-color,#30363d);\">\n"
	  << "    <div><span style=\"color:var(--wf-fg-inactive,#8b919d);\">Workspace scope:</span><br><code>" << EscapeHtml(slug) << "</code></div>\n"
	  << "    <div><span style=\"color:var(--wf-fg-inactive,#8b919d);\">Phases:</span><br><strong>" << local.PhasesFor(slug).GetCount() << "</strong> structured records</div>\n"
	  << "    <div><span style=\"color:var(--wf-fg-inactive,#8b919d);\">Tasks:</span><br><strong>" << all_tasks.GetCount() << "</strong> structured records</div>\n"
	  << "    <div><span style=\"color:var(--wf-fg-inactive,#8b919d);\">Acceptance:</span><br><span>Awaiting explicit review policy/decision</span></div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>Phases &amp; Tasks Breakdown</span>" << ManagerSkylarkApp::RenderBadge(p->status) << "</div>\n";

	Vector<const Phase*> phases = local.PhasesFor(slug);
	for(int i = 0; i < phases.GetCount(); i++) {
		const Phase* ph = phases[i];
		Vector<const Task*> tasks = local.TasksForPhase(slug, ph->seq);

		b << "  <div style=\"margin-top:1.5rem; background:#f8fafc; border:1px solid #e2e8f0; border-radius:6px; padding:1rem;\">\n"
		  << "    <div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:0.75rem;\">\n"
		  << "      <h3 style=\"font-size:1.1rem;\">Phase " << ph->seq << ": " << EscapeHtml(ph->title) << "</h3>\n"
		  << "      " << ManagerSkylarkApp::RenderBadge(ph->status) << "\n"
		  << "    </div>\n";

		if(!tasks.IsEmpty()) {
			b << "    <table><thead><tr><th>#</th><th>Task</th><th>Status</th><th>Instructions / Result</th></tr></thead><tbody>\n";
			for(int t = 0; t < tasks.GetCount(); t++) {
				const Task* tk = tasks[t];
				b << "<tr>"
				  << "<td><code>" << tk->seq << "</code></td>"
				  << "<td><strong>" << EscapeHtml(tk->title) << "</strong></td>"
				  << "<td>" << ManagerSkylarkApp::RenderBadge(tk->status) << "</td>"
				  << "<td>"
				  << (!tk->instructions.IsEmpty() ? ("<div style=\"font-size:0.85rem; color:#475569;\">" + EscapeHtml(tk->instructions) + "</div>") : "")
				  << (!tk->result.IsEmpty() ? ("<div style=\"font-size:0.85rem; color:#059669; font-weight:500; margin-top:2px;\">&bull; " + EscapeHtml(tk->result) + "</div>") : "")
				  << "</td></tr>\n";
			}
			b << "    </tbody></table>\n";
		}
		else {
			b << "    <p style=\"font-size:0.85rem; color:#94a3b8;\">No tasks defined in this phase.</p>\n";
		}
		b << "  </div>\n";
	}
	b << "</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(p->title, "projects", b, &http);
}

SKYLARK(GoalsPage, "goals")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const V2Store& v2 = catalog->v2;

	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>v2 Goals</span></div>\n"
	  << "  <table><thead><tr><th>Repo</th><th>Id</th><th>Title</th><th>Tags</th><th>Progress</th><th>Tasks</th><th>Action</th></tr></thead><tbody>\n";

	const Vector<V2Goal>& goals = v2.GetGoals();
	for(int i = 0; i < goals.GetCount(); i++) {
		const V2Goal& g = goals[i];
		b << "<tr>"
		  << "<td><span class=\"badge-v2\">" << EscapeHtml(g.repo) << "</span></td>"
		  << "<td><code>" << EscapeHtml(g.id) << "</code></td>"
		  << "<td><strong><a href=\"" << WebViewPath("/goal", g.qualified_id) << "\">" << EscapeHtml(g.title) << "</a></strong></td>"
		  << "<td>" << EscapeHtml(Join(g.tags, ", ")) << "</td>"
		  << "<td>" << ManagerSkylarkApp::RenderProgressBar(g.GetProgressPercent()) << "</td>"
		  << "<td>" << g.GetDoneTasks() << " / " << g.GetTotalTasks() << "</td>"
		  << "<td><a href=\"" << WebViewPath("/goal", g.qualified_id) << "\" class=\"btn btn-primary\" style=\"padding:4px 10px;\">View</a></td>"
		  << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Goals", "goals", b, &http);
}

SKYLARK(GoalDetailPage, "goal/**")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();

	String full_path;
	for(int i = 0; i < http.GetParamCount(); i++) {
		if(i > 0) full_path << "/";
		full_path << http[i];
	}

	CatalogRecordRead read = ReadCatalogRecord(*catalog, full_path, true);
	String status = read.status;
	if(status == "ok" && MapGetString(read.response, "record_type") != "goal") status = "wrong_record_type";
	if(status != "ok") {
		int code = status == "unavailable" ? 503 : status == "ambiguous" ? 409 : status == "unsafe_metadata" ? 409 : 404;
		http.Response(code, status == "ambiguous" ? "Ambiguous Goal ID" : "Goal Unavailable");
		String error = "<h1>Goal lookup failed</h1><div class=\"card\"><p>Status: <code>" + EscapeHtml(status) + "</code></p></div>";
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Goal lookup", "goals", error, &http);
		return;
	}
	const String canonical_id = MapGetString(read.response, "qualified_id");
	const V2Goal* g = catalog->FindGoal(canonical_id);
	if(!g) {
		http.Response(409, "Catalog Identity Conflict");
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Goal lookup", "goals", "<h1>Goal identity conflict</h1>", &http);
		return;
	}

	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>Goal: " << EscapeHtml(g->title) << "</span><span class=\"badge-v2\">" << EscapeHtml(g->repo) << "</span></div>\n"
	  << "  <p><code>" << EscapeHtml(g->qualified_id) << "</code></p>\n"
	  << "  <div style=\"margin-bottom:1.5rem;\">" << ManagerSkylarkApp::RenderProgressBar(g->GetProgressPercent()) << "</div>\n"
	  << "  <h3 style=\"font-size:1.1rem; margin-bottom:0.75rem;\">Decomposition &amp; Tasks</h3>\n"
	  << "  <table><thead><tr><th>Kind</th><th>Id</th><th>Title</th><th>Status</th><th>Gates</th><th>Sets</th></tr></thead><tbody>\n";

	for(int i = 0; i < g->tasks.GetCount(); i++) {
		const V2GoalNode& n = g->tasks[i];
		b << "<tr>"
		  << "<td><span style=\"font-size:11px; text-transform:uppercase; font-weight:700; color:" << (n.kind == "phase" ? "#3b82f6" : "#64748b") << ";\">" << EscapeHtml(n.kind) << "</span></td>"
		  << "<td><code>" << EscapeHtml(n.id) << "</code></td>"
		  << "<td><strong>" << EscapeHtml(n.title) << "</strong>" << (!n.notes.IsEmpty() ? ("<br><small style=\"color:#64748b;\">" + EscapeHtml(n.notes) + "</small>") : "") << "</td>"
		  << "<td>" << (n.kind == "task" ? ManagerSkylarkApp::RenderBadge(n.status) : "-") << "</td>"
		  << "<td><code style=\"font-size:11px;\">" << EscapeHtml(n.gates.IsEmpty() ? String() : n.gates.GetKey(0)) << "</code></td>"
		  << "<td><code style=\"font-size:11px;\">" << EscapeHtml(n.sets.IsEmpty() ? String() : n.sets.GetKey(0)) << "</code></td>"
		  << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";

	// State vector card
	if(!g->state.IsEmpty()) {
		b << "<div class=\"card\">\n"
		  << "  <div class=\"card-header\"><span>Target State Vector</span></div>\n"
		  << "  <table><thead><tr><th>State Key</th><th>Target Value</th></tr></thead><tbody>\n";
		for(int i = 0; i < g->state.GetCount(); i++) {
			b << "<tr><td><code>" << EscapeHtml(g->state.GetKey(i)) << "</code></td><td><strong>" << EscapeHtml(AsString(g->state[i])) << "</strong></td></tr>\n";
		}
		b << "</tbody></table>\n</div>\n";
	}
	b << "<div class=\"card\"><details><summary>Complete source record</summary><pre>" << EscapeHtml(AsJSON(g->source_data, true)) << "</pre></details></div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(g->title, "goals", b, &http);
}

SKYLARK(IssuesPage, "issues")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const V2Store& v2 = catalog->v2;

	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>v2 Issues</span></div>\n"
	  << "  <table><thead><tr><th>Repo</th><th>Id</th><th>Title</th><th>Status</th><th>Notes</th><th>Full record</th></tr></thead><tbody>\n";

	const Vector<V2Issue>& issues = v2.GetIssues();
	for(int i = 0; i < issues.GetCount(); i++) {
		const V2Issue& is = issues[i];
		b << "<tr>"
	  << "<td><span class=\"badge-v2\">" << EscapeHtml(is.repo) << "</span></td>"
	  << "<td><code>" << EscapeHtml(is.id) << "</code></td>"
	  << "<td><strong>" << EscapeHtml(is.title) << "</strong></td>"
		  << "<td>" << ManagerSkylarkApp::RenderBadge(is.status) << "</td>"
	  << "<td>" << EscapeHtml(is.notes) << "</td>"
	  << "<td><details><summary>Inspect</summary><pre>" << EscapeHtml(AsJSON(is.source_data, true)) << "</pre></details></td>"
		  << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Issues", "issues", b, &http);
}

SKYLARK(ReleasesPage, "releases")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const V2Store& v2 = catalog->v2;

	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>v2 Releases</span></div>\n"
	  << "  <table><thead><tr><th>Id</th><th>Title</th><th>Status</th><th>Goals Linked</th><th>Full record</th></tr></thead><tbody>\n";

	const Vector<V2Release>& rels = v2.GetReleases();
	for(int i = 0; i < rels.GetCount(); i++) {
		const V2Release& r = rels[i];
		b << "<tr>"
	  << "<td><code>" << EscapeHtml(r.id) << "</code></td>"
	  << "<td><strong>" << EscapeHtml(r.title) << "</strong></td>"
		  << "<td>" << ManagerSkylarkApp::RenderBadge(r.status) << "</td>"
	  << "<td>" << r.goals.GetCount() << "</td>"
	  << "<td><details><summary>Inspect</summary><pre>" << EscapeHtml(AsJSON(r.source_data, true)) << "</pre></details></td>"
		  << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Releases", "releases", b, &http);
}

SKYLARK(SweAgentPage, "sweagent")
{
	String b;
	b << "<div class=\"card\">\n"
	  << "  <p style=\"color:#64748b; margin-bottom:1rem;\">Task dispatch is disabled. It will remain unavailable until provider handling, a no-training policy, and real execution are in place.</p>\n"
	  << "  <button type=\"button\" class=\"btn\" disabled aria-disabled=\"true\">Agent execution unavailable</button>\n"
	  << "</div>\n";

	// API guidance card
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>REST API Integration</span></div>\n"
	  << "  <p style=\"margin-bottom:0.75rem;\">Local project and task pages are available for navigation. No agent work is queued or executed from this page.</p>\n"
	  << "</div>\n";

	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("SweAgent", "sweagent", b, &http);
}

SKYLARK(ManagerChatPage, "chat")
{
	String body = "<div class=\"card\"><p>Embedded chat rendering is unavailable until stored message content is safely sanitized.</p></div>";
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Pair Programming Chat", "chat", body, &http);
}

static String GetCookieVal(const Http& http, const char* name)
{
	String cookie_hdr = http.GetHeader("cookie");
	Vector<String> parts = Split(cookie_hdr, ';');
	for(int i = 0; i < parts.GetCount(); i++) {
		String p = TrimBoth(parts[i]);
		if(p.StartsWith(String(name) + "="))
			return TrimBoth(p.Mid((int)strlen(name) + 1));
	}
	return String();
}

SKYLARK(PlatformViewRedirect, "view/*")
{
	String proj = http[0];
	String pref = GetCookieVal(http, "wf_platform_pref");
	if(pref == "sf") {
		http.Redirect(WebViewPath("/projects", proj) + "/");
	} else {
		http.Redirect(WebViewPath("/project", proj));
	}
}

SKYLARK(PlatformSetPref, "api/platform/set")
{
	String pref = http["platform"];
	if(pref == "sf" || pref == "gh") {
		http.SetCookie("wf_platform_pref", pref, GetSysTime() + 3600 * 24 * 365, "/");
		http.ContentType("application/json") << "{\"status\":\"ok\",\"platform\":\"" << pref << "\"}";
	} else {
		http.ContentType("application/json") << "{\"error\":\"invalid_platform\"}";
	}
}

END_UPP_NAMESPACE
