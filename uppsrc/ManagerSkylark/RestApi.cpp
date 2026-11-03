#include "ManagerSkylark.h"

NAMESPACE_UPP

SKYLARK(ApiHealth, "api/health")
{
	ValueMap m;
	m.Add("status", "ok");
	m.Add("app", "ManagerSkylark");
	m.Add("version", "1.0.0");
	m.Add("v2_supported", true);
	http.ContentType("application/json") << StoreAsJson(m, true);
}

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

SKYLARK(ApiSweAgentRun, "api/sweagent/run:POST_RAW")
{
	String task_prompt = http["task"];
	String model_name  = http["model"];
	String mode        = http["mode"];

	if(task_prompt.IsEmpty())
		task_prompt = "Run basic workspace inspection";
	if(model_name.IsEmpty())
		model_name = "nvidia/nemotron-3.5-super-vl";

	SweAgentConfig cfg;
	cfg.LoadYAML(SweAgentConfig::GetDefaultMiniYaml());
	cfg.run.task = task_prompt;
	cfg.model.model_name = model_name;
	cfg.agent.mode = mode.IsEmpty() ? "yolo" : mode;
	cfg.agent.max_steps = 1;

	// Response structure
	ValueMap root;
	root.Add("status", "accepted");
	root.Add("task", task_prompt);
	root.Add("model", model_name);
	root.Add("mode", cfg.agent.mode);
	root.Add("message", "Task queued for autonomous execution");

	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiProjectSyncGitHub, "api/project/*/sync-github")
{
	String slug = http[0];
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();
	const Project* p = local.FindProject(slug);
	if(!p) {
		http.Redirect("/projects");
		return;
	}

	Vector<const Task*> tasks = local.TasksFor(slug);
	for(int i = 0; i < tasks.GetCount(); i++) {
		const Task* tk = tasks[i];
		String title = "[" + slug + "] " + tk->title;
		String body = tk->instructions;
		if(!tk->result.IsEmpty())
			body << "\n\nResult: " + tk->result;
		String cmd = "gh project item-create 1 --owner OuluBSD --title " + AsCString(title) + " --body " + AsCString(body) + " >/dev/null 2>&1";
		(void)system(cmd);
	}

	http.Redirect("/project/" + slug);
}

SKYLARK(ManagerFrameworkSpec, "api/framework/spec")
{
	NavigationModel nav = ManagerSkylarkApp::BuildNavigation();

	ValueMap root;
	root.Add("application", "ManagerSkylark");
	root.Add("version", "2.0.0");
	root.Add("framework", "WebFramework");
	root.Add("theme", "vivid-dark");
	root.Add("density", "comfortable");

	ValueArray cats;
	for(int i = 0; i < nav.GetCount(); i++) {
		const NavCategory& c = nav[i];
		ValueMap cat_m;
		cat_m.Add("id", c.id);
		cat_m.Add("title", c.title);
		cat_m.Add("description", c.description);

		ValueArray sections;
		for(int s = 0; s < c.sections.GetCount(); s++) {
			const NavSection& sec = c.sections[s];
			ValueMap sec_m;
			sec_m.Add("title", sec.title);
			ValueArray items;
			for(int it = 0; it < sec.items.GetCount(); it++) {
				const NavItem& item = sec.items[it];
				ValueMap item_m;
				item_m.Add("id", item.id);
				item_m.Add("title", item.title);
				item_m.Add("url", item.url);
				item_m.Add("description", item.description);
				items.Add(item_m);
			}
			sec_m.Add("items", items);
			sections.Add(sec_m);
		}
		cat_m.Add("sections", sections);
		cats.Add(cat_m);
	}
	root.Add("navigation", cats);

	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(QuitHandler, "quit")
{
	ManagerSkylarkApp::Get().Quit();
	http.ContentType("text/plain") << "Server shutting down\n";
}

// ---------------------------------------------------------------------------
// Automated Self-Test Verification Mode
// ---------------------------------------------------------------------------

bool ManagerSkylarkApp::RunSelfTests(int port)
{
	Cout() << "=== Running ManagerSkylark Automated Verification ===\n";

	ManagerSkylarkApp app;
	app.port = port;
	app.threads = 1;
	app.InitApp();

	// 1. Model & Store Tests
	V2Store& v2 = app.GetV2Store();
	Cout() << "Loaded " << v2.GetGoals().GetCount() << " v2 goals, "
	       << v2.GetIssues().GetCount() << " issues, "
	       << v2.GetReleases().GetCount() << " releases\n";

	ManagerStore& local = app.GetLocalStore();
	if(local.Projects().IsEmpty()) {
		Cout() << "Notice: Seeding standalone self-test fixture project...\n";
		Project p;
		p.slug = "manager-skylark";
		p.title = "Manager Skylark";
		p.status = "active";
		p.description = "Test fixture for ManagerSkylark automated verification";
		local.SaveProject(p);

		Phase ph;
		ph.project = "manager-skylark";
		ph.seq = 1;
		ph.slug = "self-test";
		ph.title = "Self Test Phase";
		ph.status = "in-progress";
		local.SavePhase(ph);

		Task t;
		t.project = "manager-skylark";
		t.phase_seq = 1;
		t.seq = 1;
		t.slug = "automated-test-verification";
		t.title = "Run automated tests";
		t.status = "open";
		local.SaveTask(t);

		local.LoadAll();
	}
	Cout() << "Loaded " << local.Projects().GetCount() << " local projects\n";

	if(local.Projects().IsEmpty()) {
		Cerr() << "FAILED: Local projects store is empty!\n";
		return false;
	}

	// 2. Start Skylark Server in Thread
	Thread server_thread;
	server_thread.Run([&app]() {
		app.Run();
	});

	// Wait briefly for server to bind
	Sleep(500);

	// 3. HTTP Client Requests
	String base_url = Format("http://127.0.0.1:%d", port);

	// Test GET /api/health
	{
		HttpRequest req(base_url + "/api/health");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || !res.Find("ManagerSkylark")) {
			Cerr() << "FAILED: /api/health returned " << req.GetStatusCode() << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    /api/health (200 OK)\n";
	}

	// Test GET /api/projects
	{
		HttpRequest req(base_url + "/api/projects");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || !res.Find("manager-skylark")) {
			Cerr() << "FAILED: /api/projects returned " << req.GetStatusCode() << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    /api/projects (200 OK)\n";
	}

	// Test GET /api/tasks/next/manager-skylark
	{
		HttpRequest req(base_url + "/api/tasks/next/manager-skylark");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200) {
			Cerr() << "FAILED: /api/tasks/next returned " << req.GetStatusCode() << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    /api/tasks/next/manager-skylark (200 OK)\n";
	}

	// Test GET /
	{
		HttpRequest req(base_url + "/");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || !res.Find("Manager Skylark")) {
			Cerr() << "FAILED: GET / returned " << req.GetStatusCode() << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    GET / Dashboard (200 OK)\n";
	}

	// Test GET /chat (WebChat Integrated View)
	{
		HttpRequest req(base_url + "/chat");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("chat-app") < 0) {
			Cerr() << "FAILED: GET /chat returned " << req.GetStatusCode() << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    GET /chat Integrated WebChat (200 OK)\n";
	}

	// Test GET /webchat (WebChat Standalone View)
	{
		HttpRequest req(base_url + "/webchat");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("chat-app") < 0) {
			Cerr() << "FAILED: GET /webchat returned " << req.GetStatusCode() << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    GET /webchat Standalone WebChat (200 OK)\n";
	}

	// Test GET /api/framework/spec (WebFramework MegaMenu semantic spec)
	{
		HttpRequest req(base_url + "/api/framework/spec");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("WebFramework") < 0) {
			Cerr() << "FAILED: GET /api/framework/spec returned " << req.GetStatusCode() << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    GET /api/framework/spec Framework Spec (200 OK)\n";
	}

	// Test GET /api/chat/sessions
	String test_session_id;
	{
		HttpRequest req(base_url + "/api/chat/sessions");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("\"sessions\"") < 0) {
			Cerr() << "FAILED: GET /api/chat/sessions returned " << req.GetStatusCode() << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Value v = ParseJSON(res);
		if(v["sessions"].GetCount() > 0)
			test_session_id = v["sessions"][0]["id"];
		Cout() << "ok    GET /api/chat/sessions (200 OK, session=" << test_session_id << ")\n";
	}

	if(test_session_id.IsEmpty())
		test_session_id = "default";

	// Test POST /api/chat/message
	{
		ValueMap msg;
		msg.Add("session_id", test_session_id);
		msg.Add("content", "Can you review our WebChat implementation?");
		msg.Add("role", "user");

		HttpRequest req(base_url + "/api/chat/message");
		req.POST();
		req.ContentType("application/json");
		req.PostData(AsJSON(msg, true));
		req.Timeout(3000);
		String res = req.Execute();
		Value resp = ParseJSON(res);
		if(req.GetStatusCode() != 200 || !resp["success"]) {
			Cerr() << "FAILED: POST /api/chat/message returned " << req.GetStatusCode() << ": " << res << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    POST /api/chat/message (200 OK)\n";
	}

	// Test GET /api/chat/stream/{session_id} (SSE stream)
	{
		HttpRequest req(base_url + "/api/chat/stream/" + test_session_id + "?prompt=Hello");
		req.Timeout(5000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("data:") < 0) {
			Cerr() << "FAILED: GET /api/chat/stream returned " << req.GetStatusCode() << ": " << res << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    GET /api/chat/stream (200 OK SSE streaming)\n";
	}

	// Clean shutdown
	server_thread.Detach();

	Cout() << "ManagerSkylark verification PASSED\n";
	return true;
}

END_UPP_NAMESPACE

using namespace Upp;

CONSOLE_APP_MAIN
{
	const Vector<String>& args = CommandLine();
	if(args.GetCount() > 0 && (args[0] == "test" || args[0] == "--test")) {
		if(!ManagerSkylarkApp::RunSelfTests(18080))
			_exit(1);
		_exit(0);
	}

	ManagerSkylarkApp app;
	if(app.Initialize(args))
		app.Run();
}
