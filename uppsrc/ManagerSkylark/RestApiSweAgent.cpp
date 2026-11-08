#include "ManagerSkylark.h"
#include "CandidateWorkflow.h"
#include "ReviewPackage.h"
#include "AssessmentViews.h"

NAMESPACE_UPP

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

END_UPP_NAMESPACE
