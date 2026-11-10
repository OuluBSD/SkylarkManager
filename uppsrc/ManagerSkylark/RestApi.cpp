#include "ManagerSkylark.h"
#include "SfLegacyMigrationCli.h"
#include "CandidateWorkflow.h"
#include "ReviewPackage.h"
#include "AssessmentViews.h"

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
	ManagerSkylarkApp::Get().StopEngines();
	ManagerSkylarkApp::Get().Quit();
	http.ContentType("text/plain") << "Server shutting down\n";
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

	if(args.GetCount() > 0 && args[0] == "--migrate-sf-local") {
		Vector<String> rest;
		for(int i = 1; i < args.GetCount(); i++)
			rest.Add(args[i]);
		int rc = RunMigrateSfLocalCommand(rest);
		fflush(stdout);
		_exit(rc);
	}

	ManagerSkylarkApp app;
	if(app.Initialize(args))
		app.Run();
}
