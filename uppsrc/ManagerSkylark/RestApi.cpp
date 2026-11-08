#include "ManagerSkylark.h"
#include "CandidateWorkflow.h"
#include "ReviewPackage.h"
#include "AssessmentViews.h"

NAMESPACE_UPP

namespace {

struct SelfTestSiteRoot {
	String path;
	SelfTestSiteRoot()
	{
		path = GetTempFileName("ManagerSkylarkSelfTest");
		if(FileExists(path)) FileDelete(path);
		RealizeDirectory(path);
	}
	~SelfTestSiteRoot()
	{
		if(DirectoryExists(path)) DeleteFolderDeep(path);
	}
};

static ValueMap TestPolicyIdentity(const String& workspace, const String& object)
{
	ValueMap identity;
	identity.Add("workspace_id", workspace);
	identity.Add("object_id", object);
	return identity;
}

static ValueMap TestDataEnvelope(const String& kind, const String& workspace, const String& id,
	                            const String& visibility, const String& provenance)
{
	ValueMap envelope;
	envelope.Add("schema_id", "skylark/" + kind);
	envelope.Add("schema_version", "1");
	envelope.Add("workspace_id", workspace);
	envelope.Add("stable_id", id);
	envelope.Add("record_kind", kind);
	envelope.Add("revision", "1");
	envelope.Add("visibility", visibility);
	envelope.Add("provenance_ref", provenance);
	return envelope;
}

static ValueMap TestCandidateRecord(const String& id, const String& source_identity = String())
{
	ValueMap record;
	record.Add("id", id);
	record.Add("kind", "local-candidate");
	record.Add("workspace_id", "synthetic-workspace");
	record.Add("schema_id", "skylark/local-candidate");
	record.Add("schema_version", "1");
	record.Add("origin_kind", "locally-authored");
	record.Add("owner_kind", "local-author");
	record.Add("provenance", "synthetic-test");
	record.Add("uncertainty", "synthetic test candidate");
	record.Add("candidate_kind", "candidate");
	if(!source_identity.IsEmpty()) record.Add("source_identity", source_identity);
	record.Add("relationships", ValueArray());
	ValueMap envelope;
	envelope.Add("schema_id", "skylark/local-candidate");
	envelope.Add("schema_version", "1");
	envelope.Add("workspace_id", "synthetic-workspace");
	envelope.Add("stable_id", id);
	envelope.Add("record_kind", "local-candidate");
	envelope.Add("revision", "1");
	envelope.Add("visibility", "private");
	envelope.Add("provenance_ref", "synthetic-test");
	record.Add("envelope", envelope);
	return record;
}

static ValueMap TestPolicyConfiguration()
{
	ValueMap instance;
	instance.Add("configured", true);
	instance.Add("remote_enabled", false);
	instance.Add("export_enabled", false);
	instance.Add("review_granularity", "record");
	instance.Add("secret_reference", "");
	ValueArray workspaces, sources, providers, ancestry, protected_ancestry;
	workspaces.Add(TestPolicyIdentity("synthetic-workspace", "synthetic-workspace"));
	auto add_source = [&](const String& id, const String& classification, bool visible) {
		ValueMap source;
		source.Add("workspace", TestPolicyIdentity("synthetic-workspace", "synthetic-workspace"));
		source.Add("identity", TestPolicyIdentity("synthetic-workspace", id));
		source.Add("classification", classification);
		source.Add("visible", visible);
		sources.Add(source);
	};
	add_source("public-source", "public", true);
	add_source("private-source", "private", false);
	ValueMap provider;
	provider.Add("workspace", TestPolicyIdentity("synthetic-workspace", "synthetic-workspace"));
	provider.Add("identity", TestPolicyIdentity("synthetic-workspace", "offline-provider"));
	provider.Add("training_policy", "ineligible");
	provider.Add("enabled", false);
	provider.Add("config_revision", "");
	provider.Add("secret_reference", "");
	providers.Add(provider);
	ValueMap registry;
	registry.Add("version", "trusted-policy-v2");
	registry.Add("workspaces", workspaces);
	registry.Add("sources", sources);
	registry.Add("ancestry", ancestry);
	registry.Add("providers", providers);
	registry.Add("protected_ancestry", protected_ancestry);
	ValueMap config;
	config.Add("instance", instance);
	config.Add("registry", registry);
	return config;
}

}

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

	SelfTestSiteRoot fixture;
	if(fixture.path.IsEmpty() || !DirectoryExists(fixture.path)) {
		Cerr() << "FAILED: cannot create isolated self-test site root\n";
		return false;
	}
	if(!SystemAuthService::SetSitePassword(fixture.path, "synthetic-user", "synthetic-password")) {
		Cerr() << "FAILED: cannot write hashed synthetic site-auth fixture\n";
		return false;
	}
	String data_root = AppendFileName(fixture.path, "data-source");
	RealizeDirectory(data_root);
	ValueMap sample_paper;
	sample_paper.Add("id", "demo-paper");
	sample_paper.Add("kind", "paper");
	sample_paper.Add("title", "Synthetic Paper");
	sample_paper.Add("source", "test-fixture");
	sample_paper.Add("status", "draft");
	sample_paper.Add("envelope", TestDataEnvelope("paper", "synthetic-workspace", "demo-paper", "public", "synthetic-test"));
	if(!SaveFile(AppendFileName(data_root, "paper.json"), AsJSON(sample_paper, true))) {
		Cerr() << "FAILED: cannot write synthetic data fixture\n";
		return false;
	}
	ValueMap hidden_paper;
	hidden_paper.Add("id", "private-paper");
	hidden_paper.Add("kind", "paper");
	hidden_paper.Add("title", "Synthetic Hidden Paper");
	hidden_paper.Add("source", "test-fixture");
	hidden_paper.Add("status", "draft");
	hidden_paper.Add("envelope", TestDataEnvelope("paper", "synthetic-workspace", "private-paper", "private", "synthetic-test"));
	if(!SaveFile(AppendFileName(data_root, "private-paper.json"), AsJSON(hidden_paper, true))) {
		Cerr() << "FAILED: cannot write synthetic hidden-data fixture\n";
		return false;
	}
	ValueMap hidden_run;
	hidden_run.Add("id", "private-run");
	hidden_run.Add("kind", "paper-research-run");
	hidden_run.Add("title", "Synthetic Hidden Run");
	hidden_run.Add("status", "done");
	hidden_run.Add("envelope", TestDataEnvelope("paper-research-run", "synthetic-workspace", "private-run", "private", "synthetic-test"));
	if(!SaveFile(AppendFileName(data_root, "private-run.json"), AsJSON(hidden_run, true))) {
		Cerr() << "FAILED: cannot write synthetic hidden-envelope fixture\n";
		return false;
	}
	if(!SaveFile(AppendFileName(data_root, "candidate-public.json"), AsJSON(TestCandidateRecord("synthetic-candidate"), true)) ||
	   !SaveFile(AppendFileName(data_root, "candidate-hidden.json"), AsJSON(TestCandidateRecord("hidden-candidate", "private-source"), true)) ||
	   !SaveFile(AppendFileName(fixture.path, "policy.json"), AsJSON(TestPolicyConfiguration(), true)) ||
	   !ManagerSkylarkApp::SeedSyntheticCandidateWorkflow(data_root, "synthetic-workspace", "synthetic-candidate", "public-source") ||
	   !ManagerSkylarkApp::SeedSyntheticReviewData(data_root, "synthetic-workspace", "public-source")) {
		Cerr() << "FAILED: cannot write synthetic candidate and policy fixtures\n";
		return false;
	}
	ValueMap registration;
	registration.Add("root", data_root);
	if(!SaveFile(AppendFileName(fixture.path, "data-root.json"), AsJSON(registration, true))) {
		Cerr() << "FAILED: cannot write synthetic data-root registration\n";
		return false;
	}
	DataStore::SetRootRegistrationPath(AppendFileName(fixture.path, "data-root.json"));
	if(!ScopedPolicyService::RunSyntheticTests()) {
		Cerr() << "FAILED: scoped-policy synthetic tests\n";
		return false;
	}
	if(!CandidateWorkflow::RunSyntheticTests()) {
		Cerr() << "FAILED: candidate-workflow synthetic validation tests\n";
		return false;
	}
	if(!ReviewPackage::RunSyntheticTests()) {
		Cerr() << "FAILED: immutable review-package synthetic tests\n";
		return false;
	}
	if(!AssessmentViewService::RunSyntheticTests()) {
		Cerr() << "FAILED: assessment projection synthetic tests\n";
		return false;
	}
	if(!GitHubPlatformService::RunIssueAdapterSyntheticTests(fixture.path)) {
		Cerr() << "FAILED: GitHub issue adapter synthetic tests\n";
		return false;
	}
	Value auth_config = ParseJSON(LoadFile(AppendFileName(fixture.path, "auth.json")));
	if(!auth_config.Is<ValueMap>()) {
		Cerr() << "FAILED: site credential file was not a JSON object\n";
		return false;
	}
	ValueMap test_credentials = auth_config;
	test_credentials.Add("legacy-user", "legacy-password");
	if(!SaveFile(AppendFileName(fixture.path, "auth.json"), AsJSON(test_credentials, true))) {
		Cerr() << "FAILED: cannot write legacy synthetic site-auth fixture\n";
		return false;
	}
	SystemAuthService::ConfigureSite(fixture.path, false);
	bool hashed_ok = SystemAuthService::VerifySystemPassword("synthetic-user", "synthetic-password");
	bool hashed_bad = SystemAuthService::VerifySystemPassword("synthetic-user", "wrong-password");
	bool legacy_ok = SystemAuthService::VerifySystemPassword("legacy-user", "legacy-password");
	bool dummy_ok = SystemAuthService::VerifySystemPassword("sblo", "password");
	if(!hashed_ok || hashed_bad || !legacy_ok || dummy_ok) {
		Cerr() << "FAILED: site auth configuration contract\n";
		return false;
	}
	SystemAuthService::ConfigureSite(fixture.path, true);
	if(!SystemAuthService::VerifySystemPassword("demo", "demo") ||
	   SystemAuthService::VerifySystemPassword("sblo", "password") ||
	   SystemAuthService::VerifySystemPassword("demo", "wrong-password")) {
		Cerr() << "FAILED: explicit synthetic demo auth contract\n";
		return false;
	}
	SystemAuthService::ConfigureSite(fixture.path, false);

	ManagerSkylarkApp app;
	app.port = port;
	app.threads = 1;
	app.ip = "127.0.0.1";
	app.test_mode = true;
	app.custom_site_dir = true;
	app.site_dir = fixture.path;
	app.demo_site = false;
	SystemAuthService::ConfigureSite(fixture.path, false);
	DataStore::SetRootRegistrationPath(AppendFileName(fixture.path, "data-root.json"));
	app.InitApp();
	if(app.GetDataStore().GetDocuments().GetCount() != 20 ||
	   !app.IsAssessmentSourceVisible("synthetic-workspace", "public-source") ||
	   app.IsAssessmentSourceVisible("synthetic-workspace", "private-source")) {
		Cerr() << "FAILED: explicitly registered synthetic data root\n";
		return false;
	}
	ValueMap assessment_view = AssessmentViewService::Build(app, "synthetic-workspace");
	ValueArray assessed_candidates = MapGet(assessment_view, "candidate_assessments");
	if(MapGetString(assessment_view, "status") != "configured" || assessed_candidates.GetCount() != 1 ||
	   MapGetString((ValueMap)assessed_candidates[0], "tier") != "promising") {
		Cerr() << "FAILED: site-scoped evidence assessment projection\n";
		return false;
	}
	ValueMap initial_workflow = CandidateWorkflow::Project(app.GetDataStore(), app, "synthetic-workspace", "synthetic-candidate");
	if(MapGetString(initial_workflow, "status") != "visible" || MapGetString(initial_workflow, "stage") != "draft") {
		Cerr() << "FAILED: synthetic candidate workflow projection\n";
		return false;
	}
	int before_denied_mutations = app.GetDataStore().GetDocuments().GetCount();
	ValueMap disabled_create = CandidateWorkflow::CreateProposal(app.GetDataStore(), app, ValueMap(), "synthetic-workspace", "synthetic-source", "synthetic-op", UserSession());
	ValueMap disabled_revise = CandidateWorkflow::ReviseProposal(app.GetDataStore(), app, "synthetic-workspace", "synthetic-candidate", "synthetic-proposal", "1", "r1", ValueMap(), "synthetic-source", "synthetic-revise", UserSession());
	ValueMap disabled_advance = CandidateWorkflow::AdvanceStage(app.GetDataStore(), app, "synthetic-workspace", "synthetic-candidate", "r1", "1", "1", "event", "source-inspected", "synthetic", ValueArray(), "synthetic-source", "synthetic-advance", UserSession());
	if(MapGetString(disabled_create, "status") != "denied" || MapGetString(disabled_revise, "status") != "denied" ||
	   MapGetString(disabled_advance, "status") != "denied" || app.GetDataStore().GetDocuments().GetCount() != before_denied_mutations) {
		Cerr() << "FAILED: candidate workflow mutations must remain disabled\n";
		return false;
	}

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
	{
		HttpRequest req(base_url + "/api/data/docs?kind=paper");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Synthetic Paper") < 0 || res.Find("Synthetic Hidden Paper") >= 0 || res.Find("Synthetic Hidden Run") >= 0 || res.Find(data_root) >= 0) {
			Cerr() << "FAILED: synthetic read-only catalog API or source-path redaction\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    /api/data/docs (synthetic read-only catalog)\n";
	}
	{
		HttpRequest req(base_url + "/api/data/spec?workspace=synthetic-workspace");
		req.Timeout(3000);
		String res = req.Execute();
		ValueMap response = ParseJSON(res);
		ValueMap assessment = MapGet(response, "assessment");
		ValueArray candidates = MapGet(assessment, "candidate_assessments");
		if(req.GetStatusCode() != 200 || MapGetString(assessment, "status") != "configured" ||
		   candidates.GetCount() != 1 || res.Find("demo-evidence-item") >= 0 || res.Find("demo-source") >= 0 || res.Find(data_root) >= 0) {
			Cerr() << "FAILED: synthetic read-only assessment projection or evidence redaction\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    /api/data/spec (synthetic read-only assessment)\n";
	}
	{
		HttpRequest req(base_url + "/data?workspace=synthetic-workspace");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Assessment") < 0 || res.Find("promising") < 0 ||
		   res.Find("demo-evidence-item") >= 0 || res.Find("demo-source") >= 0 || res.Find(data_root) >= 0) {
			Cerr() << "FAILED: synthetic assessment page or evidence redaction\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
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

	app.test_mode = false;
	app.demo_site = false;
	SystemAuthService::ConfigureSite(fixture.path, false);
	{
		HttpRequest req(base_url + "/api/projects");
		req.Timeout(3000);
		req.Execute();
		if(req.GetStatusCode() != 401) {
			Cerr() << "FAILED: unauthenticated site API was not denied\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/data/docs");
		req.Timeout(3000);
		req.Execute();
		if(req.GetStatusCode() != 401) {
			Cerr() << "FAILED: unauthenticated data catalog request was not denied\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	ValueMap site_login;
	site_login.Add("username", "synthetic-user");
	site_login.Add("password", "synthetic-password");
	HttpRequest site_login_req(base_url + "/api/auth/login");
	site_login_req.POST().ContentType("application/json").PostData(AsJSON(site_login));
	site_login_req.Timeout(3000);
	String site_login_body = site_login_req.Execute();
	Value site_login_result = ParseJSON(site_login_body);
	String site_token = site_login_result["token"];
	if(site_login_req.GetStatusCode() != 200 || site_token.IsEmpty()) {
		Cerr() << "FAILED: site-configured login\n";
		app.Quit();
		server_thread.Wait();
		return false;
	}
	{
		HttpRequest req(base_url + "/api/projects");
		req.Header("Authorization", "Bearer " + site_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("manager-skylark") < 0) {
			Cerr() << "FAILED: authenticated site request\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/reviews/packages");
		req.Header("Authorization", "Bearer " + site_token);
		req.Timeout(3000);
		String res = req.Execute();
		String synthetic_patch = "diff --git a/src/synthetic.cpp b/src/synthetic.cpp\nindex 1111111..2222222 100644\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n@@ -1 +1 @@\n-old\n+new\n";
		String patch_hash = DataStore::ContentDigest(synthetic_patch);
		if(req.GetStatusCode() != 200 || res.Find("demo-package") < 0 || res.Find("diff --git") >= 0 ||
		   res.Find("src/synthetic.cpp") >= 0 || res.Find(patch_hash) >= 0 || res.Find("patch_sha256") >= 0 ||
		   res.Find("patch_bytes") >= 0 || res.Find("Synthetic Reviewer") >= 0) {
			Cerr() << "FAILED: read-only review package index redaction\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/reviews/package?workspace=synthetic-workspace&package=demo-package&revision=demo-review-revision");
		req.Header("Authorization", "Bearer " + site_token);
		req.Timeout(3000);
		String res = req.Execute();
		String synthetic_patch = "diff --git a/src/synthetic.cpp b/src/synthetic.cpp\nindex 1111111..2222222 100644\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n@@ -1 +1 @@\n-old\n+new\n";
		String patch_hash = DataStore::ContentDigest(synthetic_patch);
		if(req.GetStatusCode() != 200 || res.Find("Synthetic Goal") < 0 || res.Find("Synthetic Topology") < 0 ||
		   res.Find("Related record unavailable") < 0 || res.Find("private-check") >= 0 || res.Find("missing-check") >= 0 ||
		   res.Find("SYNTHETIC HUMAN COMMENT BODY") >= 0 || res.Find("Private Synthetic Reviewer") >= 0 ||
		   res.Find("diff --git") >= 0 || res.Find("src/synthetic.cpp") >= 0 || res.Find(patch_hash) >= 0 || res.Find("patch_bytes") >= 0) {
			Cerr() << "FAILED: review package API metadata and discussion redaction\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/reviews/related?workspace=synthetic-workspace&kind=goal&ref=demo-goal");
		req.Header("Authorization", "Bearer " + site_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Synthetic Goal") < 0) {
			Cerr() << "FAILED: visible related-record link resolver\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/reviews/related?workspace=synthetic-workspace&kind=paper&ref=private-check");
		req.Header("Authorization", "Bearer " + site_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 404 || res.Find("private-check") >= 0) {
			Cerr() << "FAILED: hidden related-record redaction\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/review-package?workspace=synthetic-workspace&package=demo-package&revision=demo-review-revision");
		req.Header("Authorization", "Bearer " + site_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("diff --git") < 0 || res.Find("SYNTHETIC HUMAN COMMENT BODY") >= 0 ||
		   res.Find("Private Synthetic Reviewer") >= 0 || res.Find("private-check") >= 0 || res.Find("missing-check") >= 0) {
			Cerr() << "FAILED: authenticated review detail rendering\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/candidates/lineage?workspace=synthetic-workspace");
		req.Header("Authorization", "Bearer " + site_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("synthetic-candidate") < 0 ||
		   res.Find("hidden-candidate") >= 0 || res.Find("private-source") >= 0) {
			Cerr() << "FAILED: policy-scoped candidate lineage projection\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/candidates/workflow?workspace=synthetic-workspace&candidate=synthetic-candidate");
		req.Header("Authorization", "Bearer " + site_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("workflow_state") < 0 || res.Find("visible") < 0 ||
		   res.Find("draft") < 0 || res.Find("private-source") >= 0 || res.Find("demo-evidence-item") >= 0 ||
		   res.Find("quality_contract") >= 0) {
			Cerr() << "FAILED: read-only candidate workflow projection\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/data/docs?kind=paper");
		req.Header("Authorization", "Bearer " + site_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Synthetic Paper") < 0) {
			Cerr() << "FAILED: authenticated site data catalog\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}

	app.demo_site = true;
	SystemAuthService::ConfigureSite(fixture.path, true);
	ValueMap demo_login;
	demo_login.Add("username", "demo");
	demo_login.Add("password", "demo");
	HttpRequest demo_login_req(base_url + "/api/auth/login");
	demo_login_req.POST().ContentType("application/json").PostData(AsJSON(demo_login));
	demo_login_req.Timeout(3000);
	String demo_login_body = demo_login_req.Execute();
	Value demo_login_result = ParseJSON(demo_login_body);
	String demo_token = demo_login_result["token"];
	if(demo_login_req.GetStatusCode() != 200 || demo_token.IsEmpty()) {
		Cerr() << "FAILED: explicit demo login\n";
		app.Quit();
		server_thread.Wait();
		return false;
	}
	{
		HttpRequest req(base_url + "/api/reviews/packages");
		req.Header("Authorization", "Bearer " + demo_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("demo-package") < 0 || res.Find("diff --git") >= 0 || res.Find("src/synthetic.cpp") >= 0) {
			Cerr() << "FAILED: synthetic review package index\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/reviews");
		req.Header("Authorization", "Bearer " + demo_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("demo-package") < 0) {
			Cerr() << "FAILED: synthetic review package page\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/review-package?workspace=synthetic-workspace&package=demo-package&revision=demo-review-revision");
		req.Header("Authorization", "Bearer " + demo_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("diff --git") < 0 || res.Find("SYNTHETIC HUMAN COMMENT BODY") >= 0 ||
		   res.Find("Private Synthetic Reviewer") >= 0 || res.Find("private-check") >= 0) {
			Cerr() << "FAILED: synthetic review detail privacy\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/");
		req.Header("Authorization", "Bearer " + demo_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Synthetic ManagerSkylark Demo") < 0 || res.Find("/home/") >= 0) {
			Cerr() << "FAILED: isolated synthetic demo homepage\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/data/docs?kind=paper");
		req.Header("Authorization", "Bearer " + demo_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Synthetic Paper") < 0 || res.Find("Synthetic Hidden Paper") >= 0 || res.Find("Synthetic Hidden Run") >= 0 || res.Find(data_root) >= 0) {
			Cerr() << "FAILED: demo read-only data catalog\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/research/papers");
		req.Header("Authorization", "Bearer " + demo_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Synthetic Paper") < 0 ||
		   res.Find("Synthetic Hidden Paper") >= 0 || res.Find(data_root) >= 0) {
			Cerr() << "FAILED: read-only synthetic research listing\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/candidates/lineage?workspace=synthetic-workspace");
		req.Header("Authorization", "Bearer " + demo_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("synthetic-candidate") < 0 ||
		   res.Find("hidden-candidate") >= 0 || res.Find("private-source") >= 0) {
			Cerr() << "FAILED: read-only synthetic candidate lineage\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/api/candidates/workflow?workspace=synthetic-workspace&candidate=synthetic-candidate");
		req.Header("Authorization", "Bearer " + demo_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("workflow_state") < 0 || res.Find("visible") < 0 ||
		   res.Find("draft") < 0 || res.Find("private-source") >= 0 || res.Find("demo-evidence-item") >= 0 ||
		   res.Find("quality_contract") >= 0) {
			Cerr() << "FAILED: synthetic read-only candidate workflow\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/candidate-workflow?workspace=synthetic-workspace&candidate=synthetic-candidate");
		req.Header("Authorization", "Bearer " + demo_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Candidate synthetic-candidate is at stage draft") < 0 ||
		   res.Find("/api/candidates/proposal") >= 0 || res.Find("/api/candidates/stage") >= 0) {
			Cerr() << "FAILED: read-only candidate workflow page\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		HttpRequest req(base_url + "/data");
		req.Header("Authorization", "Bearer " + demo_token);
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Data Catalog") < 0 || res.Find("Synthetic Hidden Paper") >= 0 || res.Find("Synthetic Hidden Run") >= 0 || res.Find(data_root) >= 0) {
			Cerr() << "FAILED: isolated data catalog page\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	{
		ValueMap claim;
		claim.Add("project", "manager-skylark");
		claim.Add("seq", 1);
		HttpRequest req(base_url + "/api/tasks/claim");
		req.Header("Authorization", "Bearer " + demo_token);
		req.POST().ContentType("application/json").PostData(AsJSON(claim));
		req.Timeout(3000);
		req.Execute();
		const Task *task = app.GetLocalStore().FindTask("manager-skylark", 1);
		if(req.GetStatusCode() != 403 || !task || task->status != "open") {
			Cerr() << "FAILED: demo write isolation\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
	}
	app.test_mode = true;
	app.demo_site = false;
	SystemAuthService::ConfigureSite(fixture.path, false);

	// Clean shutdown before the isolated fixture root is removed.
	app.Quit();
	server_thread.Wait();

	Cout() << "ManagerSkylark verification PASSED\n";
	return true;
}

END_UPP_NAMESPACE

using namespace Upp;

CONSOLE_APP_MAIN
{
	const Vector<String>& args = CommandLine();
	if(args.GetCount() > 0 && (args[0] == "test" || args[0] == "--test")) {
		bool ok = ManagerSkylarkApp::RunSelfTests(18080);
		fflush(stdout);
		_exit(ok ? 0 : 1);
	}

	ManagerSkylarkApp app;
	if(app.Initialize(args))
		app.Run();
}
