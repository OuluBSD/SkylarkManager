#include "ManagerSkylark.h"
#include "CandidateWorkflow.h"
#include "ReviewPackage.h"
#include "AssessmentViews.h"

NAMESPACE_UPP

SKYLARK(ApiProjects, "api/projects")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();

	ValueArray va;
	const Vector<Project>& projs = local.Projects();
	for(int i = 0; i < projs.GetCount(); i++) {
		const Project& p = projs[i];
		ValueMap jm;
		jm.Add("slug", p.slug);
		jm.Add("title", p.title);
		jm.Add("status", p.status);
		jm.Add("description", p.description);

		Vector<const Phase*> phases = local.PhasesFor(p.slug);
		Vector<const Task*>  tasks  = local.TasksFor(p.slug);
		jm.Add("phases_count", phases.GetCount());
		jm.Add("tasks_count", tasks.GetCount());

		int done = 0;
		for(int t = 0; t < tasks.GetCount(); t++)
			if(tasks[t]->status == "done") done++;
		jm.Add("tasks_done", done);

		va.Add(jm);
	}

	ValueMap root;
	root.Add("projects", va);
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiProjectDetail, "api/project/*")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();
	String slug = http[0];

	const Project* p = local.FindProject(slug);
	if(!p) {
		ValueMap err;
		err.Add("error", "Project not found");
		http.ContentType("application/json") << StoreAsJson(err, true);
		return;
	}

	ValueMap root;
	root.Add("slug", p->slug);
	root.Add("title", p->title);
	root.Add("status", p->status);
	root.Add("description", p->description);

	ValueArray phase_arr;
	Vector<const Phase*> phases = local.PhasesFor(slug);
	for(int i = 0; i < phases.GetCount(); i++) {
		const Phase* ph = phases[i];
		ValueMap pm;
		pm.Add("seq", ph->seq);
		pm.Add("title", ph->title);
		pm.Add("status", ph->status);
		pm.Add("description", ph->description);

		ValueArray task_arr;
		Vector<const Task*> tasks = local.TasksForPhase(slug, ph->seq);
		for(int t = 0; t < tasks.GetCount(); t++) {
			const Task* tk = tasks[t];
			ValueMap tm;
			tm.Add("seq", tk->seq);
			tm.Add("title", tk->title);
			tm.Add("status", tk->status);
			tm.Add("instructions", tk->instructions);
			tm.Add("result", tk->result);
			tm.Add("link_next_task", tk->link_next_task);
			task_arr.Add(tm);
		}
		pm.Add("tasks", task_arr);
		phase_arr.Add(pm);
	}
	root.Add("phases", phase_arr);

	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiNextTask, "api/tasks/next/*")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();
	String slug = http[0];

	const Project* p = local.FindProject(slug);
	if(!p) {
		ValueMap err;
		err.Add("error", "Project not found");
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}

	Vector<const Phase*> phases = local.PhasesFor(p->slug);
	const Task* next_task = NULL;
	int phase_seq = 0;

	for(int i = 0; i < phases.GetCount(); i++) {
		Vector<const Task*> tasks = local.TasksForPhase(p->slug, phases[i]->seq);
		for(int t = 0; t < tasks.GetCount(); t++) {
			if(tasks[t]->status == "open") {
				next_task = tasks[t];
				phase_seq = phases[i]->seq;
				break;
			}
		}
		if(next_task) break;
	}

	ValueMap root;
	if(next_task) {
		root.Add("found", true);
		root.Add("project", p->slug);
		root.Add("phase_seq", phase_seq);
		root.Add("seq", next_task->seq);
		root.Add("title", next_task->title);
		root.Add("instructions", next_task->instructions);
		root.Add("status", next_task->status);
		root.Add("link_next_task", next_task->link_next_task);
	}
	else {
		root.Add("found", false);
		root.Add("message", "No open tasks in project");
	}

	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiClaimTask, "api/tasks/claim:POST_RAW")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();

	String req = http.GetRequestContent();
	Value v = ParseJSON(req);

	String proj = v["project"];
	String phase = v["phase"];
	String task = v["task"];

	if(proj.IsEmpty() || phase.IsEmpty() || task.IsEmpty()) {
		ValueMap err;
		err.Add("error", "Missing project, phase, or task parameters");
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}

	int rc = CmdSetTask(local, {proj, phase, task, "status", "in-progress"});
	ValueMap res;
	res.Add("success", rc == 0);
	res.Add("status", rc == 0 ? "in-progress" : "failed");
	http.ContentType("application/json") << AsJSON(res, true);
}

SKYLARK(ApiDoneTask, "api/tasks/done:POST_RAW")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();

	String req = http.GetRequestContent();
	Value v = ParseJSON(req);

	String proj = v["project"];
	String phase = v["phase"];
	String task = v["task"];
	String result_txt = v["result"];

	if(proj.IsEmpty() || phase.IsEmpty() || task.IsEmpty()) {
		ValueMap err;
		err.Add("error", "Missing project, phase, or task parameters");
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}

	int rc = CmdSetTask(local, {proj, phase, task, "status", "done"});
	if(rc == 0 && !result_txt.IsEmpty()) {
		CmdSetTask(local, {proj, phase, task, "result", result_txt});
	}

	ValueMap res;
	res.Add("success", rc == 0);
	res.Add("status", rc == 0 ? "done" : "failed");
	res.Add("result", result_txt);
	http.ContentType("application/json") << AsJSON(res, true);
}

SKYLARK(ApiGoals, "api/goals")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	V2Store& v2 = app.GetV2Store();

	ValueArray va;
	const Vector<V2Goal>& goals = v2.GetGoals();
	for(int i = 0; i < goals.GetCount(); i++) {
		ValueMap gm;
		gm.Add("id", goals[i].id);
		gm.Add("repo", goals[i].repo);
		gm.Add("title", goals[i].title);
		gm.Add("progress", goals[i].GetProgressPercent());
		gm.Add("total_tasks", goals[i].GetTotalTasks());
		gm.Add("done_tasks", goals[i].GetDoneTasks());
		va.Add(gm);
	}

	ValueMap root;
	root.Add("goals", va);
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiIssues, "api/issues")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	V2Store& v2 = app.GetV2Store();

	ValueArray va;
	const Vector<V2Issue>& issues = v2.GetIssues();
	for(int i = 0; i < issues.GetCount(); i++) {
		ValueMap im;
		im.Add("id", issues[i].id);
		im.Add("repo", issues[i].repo);
		im.Add("title", issues[i].title);
		im.Add("status", issues[i].status);
		im.Add("notes", issues[i].notes);
		va.Add(im);
	}

	ValueMap root;
	root.Add("issues", va);
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiReleases, "api/releases")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	V2Store& v2 = app.GetV2Store();

	ValueArray va;
	const Vector<V2Release>& rels = v2.GetReleases();
	for(int i = 0; i < rels.GetCount(); i++) {
		ValueMap rm;
		rm.Add("id", rels[i].id);
		rm.Add("title", rels[i].title);
		rm.Add("status", rels[i].status);
		rm.Add("goals_count", rels[i].goals.GetCount());
		rm.Add("issues_count", rels[i].issues.GetCount());
		va.Add(rm);
	}

	ValueMap root;
	root.Add("releases", va);
	http.ContentType("application/json") << AsJSON(root, true);
}

END_UPP_NAMESPACE
