#include "ManagerSkylark.h"
#include "SfLegacyMigrationCli.h"
#include "PrivateHubService.h"
#include <TaskFoundry/TaskFoundryService.h>
#include "HostExec.h"
#include "CandidateWorkflow.h"
#include "ReviewPackage.h"
#include "AssessmentViews.h"
#include "DataStoreSelfTests.h"

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

// ---------------------------------------------------------------------------
// Automated Self-Test Verification Mode
// ---------------------------------------------------------------------------

// Pure unit sections (task 024), defined in MetaInboxSelfTests.cpp and DocsPortalSelfTests.cpp.
bool RunMetaInboxSelfTests(const String& scratch_dir);
bool RunDocsPortalSelfTests(const String& scratch_dir);
bool RunV2CalendarSelfTests(const String& scratch_dir);
bool RunMarkdownSelfTests();
bool RunAtlasSelfTests(const String& scratch_dir);

// Portal shell (tasks 033, 034): the descriptors, the meta-shell frame and the stock tree.
static bool PortalShellSelfTests()
{
	if(PortalLinkHost("") != "127.0.0.1" || PortalLinkHost("192.168.1.169:8081") != "192.168.1.169"
	   || PortalLinkHost("localhost") != "localhost" || PortalLinkHost("[::1]:8081") != "[::1]") {
		Cerr() << "FAILED: portal link host is not the request's name without its port\n";
		return false;
	}

	Vector<EngineSiteListing> sites;
	EngineSiteListing atlas;
	atlas.id = "atlas";
	atlas.title = "Atlas";
	atlas.port = 18217;
	sites.Add(pick(atlas));

	Vector<PortalWebapp> apps = ListWebapps("192.168.1.169:8081", sites);
	// Atlas leads the list (task 036), then the five hubs, then settings.
	if(apps.GetCount() != 7 || apps[0].id != "atlas" || apps[0].url != "http://192.168.1.169:18217/"
	   || apps[1].id != "ai" || apps[1].url != "/ai" || apps[5].id != "mesh"
	   || apps[6].id != "settings" || apps[6].url != "/host/settings" || apps[6].group != "settings") {
		Cerr() << "FAILED: portal descriptors are not Atlas, the five hubs, then settings\n";
		return false;
	}

	// The meta-nav is two rails of links that target the frame; no tree widget (task 036).
	UserSession guest;
	String atlas_page = RenderPortalShell("192.168.1.169:8081", "atlas", guest, sites);
	if(atlas_page.Find("src=\"http://192.168.1.169:18217/\"") < 0 || atlas_page.Find("name=\"portal-frame\"") < 0
	   || atlas_page.Find("class=\"portal-rail\"") < 0 || atlas_page.Find("class=\"portal-rail-slot\"") < 0
	   || atlas_page.Find("data-app=\"atlas\" aria-current=\"page\"") < 0 || atlas_page.Find("class=\"wf-tree") >= 0
	   || atlas_page.Find("class=\"wf-dock") >= 0 || atlas_page.Find("class=\"wf-template-sidebar\"") >= 0
	   || atlas_page.Find("target=\"portal-frame\"") < 0 || atlas_page.Find("href=\"/?app=") >= 0
	   || atlas_page.Find("href=\"/login?next=") >= 0) { // a composed webapp shows no stock sign-in
		Cerr() << "FAILED: the meta-nav is not two link rails targeting the frame, Atlas selected, with no tree widget\n";
		return false;
	}
	// The loading indicator, the timeout fallback and its controls are present.
	if(atlas_page.Find("id=\"portal-status\"") < 0 || atlas_page.Find("This webapp is taking a while.") < 0
	   || atlas_page.Find("id=\"portal-retry\"") < 0 || atlas_page.Find("Open in new tab") < 0
	   || atlas_page.Find("15000") < 0 || atlas_page.Find("'load', clear") < 0 || atlas_page.Find("'error'") < 0) {
		Cerr() << "FAILED: the frame has no loading indicator or timeout fallback\n";
		return false;
	}
	// One rail, widening in place: the toggle and the links are the only rail controls, the labels show
	// only while it is open, and no second list of links exists (task 051).
	if(atlas_page.Find("id=\"portal-rail-toggle\" class=\"portal-rail-toggle\" aria-expanded=\"false\"") < 0
	   || atlas_page.Find(".portal-rail-slot { position: relative;") < 0 || atlas_page.Find(".portal-rail { position: absolute;") < 0
	   || atlas_page.Find(".portal-rail-label { display: none;") < 0 || atlas_page.Find(".portal-shell.portal-open .portal-rail {") < 0
	   || atlas_page.Find("class=\"portal-rail-link\"") < 0 || atlas_page.Find("'Escape'") < 0
	   || atlas_page.Find("setOpen(false);") < 0 || atlas_page.Find("'focusin'") < 0 || atlas_page.Find("portal-panel") >= 0
	   || atlas_page.Find("portal-webapp-list") >= 0) {
		Cerr() << "FAILED: the webapp rail is not one widening rail with a toggle and no second list\n";
		return false;
	}
	String guest_row = RenderPortalShell("192.168.1.169:8081", "github", guest, sites);
	if(guest_row.Find("href=\"/login?next=%2Fgithub\" target=\"portal-frame\"") < 0
	   || guest_row.Find("data-app=\"github\" aria-current=\"page\"") < 0) {
		Cerr() << "FAILED: a guest's log in does not open inside the frame and return to the selected hub\n";
		return false;
	}
	String fallback = RenderPortalShell("", "no-such-app", guest, sites);
	if(fallback.Find("src=\"http://127.0.0.1:18217/\"") < 0) {
		Cerr() << "FAILED: an unknown app does not fall back to the first webapp\n";
		return false;
	}

	// Each hub's top navigator is its own category alone (task 035).
	String docs = ManagerSkylarkApp::RenderHtmlPage("Docs", "docs", "<p>body</p>");
	String gh = ManagerSkylarkApp::RenderHtmlPage("Profile", "gh_profile", "<p>body</p>");
	String host = ManagerSkylarkApp::RenderHtmlPage("Host", "host", "<p>body</p>");
	String mesh = ManagerSkylarkApp::RenderHtmlPage("Mesh", "mesh_nodes", "<p>body</p>");
	bool scoped = docs.Find("Pair Chat") >= 0 && docs.Find("SeppoPakonen Profile") < 0 && docs.Find("Local Workstation") < 0
	           && gh.Find("SeppoPakonen Profile") >= 0 && gh.Find("Pair Chat") < 0 && gh.Find("Local Workstation") < 0
	           && host.Find("Local Workstation (192.168.1.106)") >= 0 && host.Find("Pair Chat") < 0 && host.Find("SeppoPakonen Profile") < 0
	           && mesh.Find("MediaCenter FX-8350") >= 0 && mesh.Find("Pair Chat") < 0 && mesh.Find("SeppoPakonen Profile") < 0;
	if(!scoped || docs.Find("Webapps") >= 0 || docs.Find("/?app=") >= 0 || gh.Find("Local AI Hub") >= 0 || host.Find("GitHub Hub") >= 0) {
		Cerr() << "FAILED: a hub's top navigator carries another hub's items\n";
		return false;
	}
	String chat = ManagerSkylarkApp::RenderHtmlPage("Chat", "chat", "<p>body</p>");
	if(chat.Find("wf-template-sidebar") >= 0 || chat.Find("Pair Chat") < 0) {
		Cerr() << "FAILED: the full-width chat page must not get the left pane\n";
		return false;
	}
	return true;
}

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
	if(!DataStoreSelfTests::RunSyntheticTests(fixture.path)) {
		Cerr() << "FAILED: DataStore hard-path synthetic tests\n";
		return false;
	}
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
	if(!SourceForgePlatformService::RunSourceForgeSyntheticTests()) {
		Cerr() << "FAILED: SourceForge adapter synthetic tests\n";
		return false;
	}
	if(!GitHubPlatformService::RunGitHubAdapterSyntheticTests(fixture.path)) {
		Cerr() << "FAILED: GitHub adapter synthetic tests\n";
		return false;
	}
	if(!GitHubPlatformService::RunGitHubBackendContractTests(AppendFileName(fixture.path, "github-contract"))) {
		Cerr() << "FAILED: GitHub shared backend contract tests\n";
		return false;
	}
	if(!RunSfLegacyMigrationSelfTests(AppendFileName(fixture.path, "sf-legacy-migration"))) {
		Cerr() << "FAILED: SourceForge legacy migration synthetic tests\n";
		return false;
	}
	if(!RunPrivateHubServiceSelfTests(AppendFileName(fixture.path, "privatehub-service"))) {
		Cerr() << "FAILED: PrivateHub broker service synthetic tests\n";
		return false;
	}
	if(!RunTaskFoundrySelfTests(AppendFileName(fixture.path, "taskfoundry-service"))) {
		Cerr() << "FAILED: TaskFoundry service synthetic tests\n";
		return false;
	}
	if(!RunHostExecSelfTests()) {
		Cerr() << "FAILED: git argument allowlist synthetic tests\n";
		return false;
	}
	if(!RunMetaInboxSelfTests(AppendFileName(fixture.path, "meta-inbox"))) {
		Cerr() << "FAILED: meta inbox unit tests\n";
		return false;
	}
	if(!RunDocsPortalSelfTests(AppendFileName(fixture.path, "docs-portal"))) {
		Cerr() << "FAILED: docs portal resolver unit tests\n";
		return false;
	}
	if(!RunV2CalendarSelfTests(AppendFileName(fixture.path, "v2-calendar"))) {
		Cerr() << "FAILED: v2 calendar unit tests\n";
		return false;
	}
	if(!RunMarkdownSelfTests()) {
		Cerr() << "FAILED: markdown pipeline static tests\n";
		return false;
	}
	if(!RunAtlasSelfTests(AppendFileName(fixture.path, "atlas"))) {
		Cerr() << "FAILED: Atlas map model and store tests\n";
		return false;
	}
	if(!RunYouTubePlannerSelfTests()) {
		Cerr() << "FAILED: YouTubePlanner stub tests\n";
		return false;
	}
	if(!SystemAuthSelfTests(AppendFileName(fixture.path, "system-auth"))) {
		Cerr() << "FAILED: portal bridge host-account and return-port tests\n";
		return false;
	}
	if(!PortalShellSelfTests()) {
		Cerr() << "FAILED: portal shell (left pane, user row, link host) tests\n";
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
		HttpRequest req(base_url + "/home");
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
		// The docs portal (task 021): a fixture repo with an AGENTS.md and a
		// docs/ note; traversal and non-doc files are refused.
		String docs_root = AppendFileName(fixture.path, "docs-fixture");
		RealizeDirectory(AppendFileName(docs_root, "docs"));
		SaveFile(AppendFileName(docs_root, "AGENTS.md"), "# Fixture\n\nSome <b>markup</b> here.\n");
		SaveFile(AppendFileName(docs_root, "docs/note.md"), "- note one\n");
		SaveFile(AppendFileName(docs_root, "Store.cpp"), "not a doc");
		ManagerSkylarkApp::Get().DocsReposSpec() = "DocsFix=" + docs_root;

		HttpRequest req_list(base_url + "/docs");
		req_list.Header("Authorization", "Bearer " + demo_token);
		req_list.Timeout(3000);
		String res_list = req_list.Execute();
		if(req_list.GetStatusCode() != 200 || res_list.Find("DocsFix") < 0) {
			Cerr() << "FAILED: docs repo list\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		HttpRequest req_repo(base_url + "/docs/DocsFix");
		req_repo.Header("Authorization", "Bearer " + demo_token);
		req_repo.Timeout(3000);
		String res_repo = req_repo.Execute();
		if(req_repo.GetStatusCode() != 200 || res_repo.Find("AGENTS.md") < 0
		   || res_repo.Find("docs/note.md") < 0 || res_repo.Find("Store.cpp") >= 0) {
			Cerr() << "FAILED: docs repo file list\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		HttpRequest req_file(base_url + "/docs/DocsFix/AGENTS.md");
		req_file.Header("Authorization", "Bearer " + demo_token);
		req_file.Timeout(3000);
		String res_file = req_file.Execute();
		if(req_file.GetStatusCode() != 200 || res_file.Find("Some &lt;b&gt;markup&lt;/b&gt; here") < 0
		   || res_file.Find("<b>markup</b>") >= 0) {
			Cerr() << "FAILED: docs file renders escaped\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		// The file is embedded for marked.js (the GitHub README pattern), not as a <pre>.
		if(res_file.Find("<textarea class=\"raw-markdown\"") < 0 || res_file.Find("class=\"markdown-body\"") < 0
		   || res_file.Find("<pre style=\"white-space:pre-wrap;background:#111") >= 0) {
			Cerr() << "FAILED: docs file is not embedded for markdown rendering\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		HttpRequest req_raw(base_url + "/docs/DocsFix/docs/note.md?raw=1");
		req_raw.Header("Authorization", "Bearer " + demo_token);
		req_raw.Timeout(3000);
		req_raw.Execute();
		if(req_raw.GetStatusCode() != 200) {
			Cerr() << "FAILED: docs raw file\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		HttpRequest req_up(base_url + "/docs/DocsFix/../ManagerSkylark.h");
		req_up.Header("Authorization", "Bearer " + demo_token);
		req_up.Timeout(3000);
		req_up.Execute();
		if(req_up.GetStatusCode() != 404) {
			Cerr() << "FAILED: docs traversal refused\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		HttpRequest req_non(base_url + "/docs/DocsFix/Store.cpp");
		req_non.Header("Authorization", "Bearer " + demo_token);
		req_non.Timeout(3000);
		req_non.Execute();
		if(req_non.GetStatusCode() != 404) {
			Cerr() << "FAILED: docs non-doc file refused\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		HttpRequest req_unknown(base_url + "/docs/Nope/README.md");
		req_unknown.Header("Authorization", "Bearer " + demo_token);
		req_unknown.Timeout(3000);
		req_unknown.Execute();
		if(req_unknown.GetStatusCode() != 404) {
			Cerr() << "FAILED: docs unknown repo refused\n";
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

