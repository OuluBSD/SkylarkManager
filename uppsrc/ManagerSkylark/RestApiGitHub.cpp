#include "ManagerSkylark.h"
#include "CandidateWorkflow.h"
#include "ReviewPackage.h"
#include "AssessmentViews.h"

NAMESPACE_UPP

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

END_UPP_NAMESPACE
