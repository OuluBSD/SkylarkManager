#ifndef _ManagerSkylark_AgentPlatform_h_
#define _ManagerSkylark_AgentPlatform_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// SweAgent Data Models
// ---------------------------------------------------------------------------

struct AgentStep : Moveable<AgentStep> {
	int    step_index = 0;
	String thought;
	String tool_name;
	String tool_args;
	String observation;
	String timestamp;

	AgentStep() {}
	AgentStep(const AgentStep& o, int)
		: step_index(o.step_index), thought(o.thought), tool_name(o.tool_name),
		  tool_args(o.tool_args), observation(o.observation), timestamp(o.timestamp) {}
	AgentStep(const AgentStep& o)
		: step_index(o.step_index), thought(o.thought), tool_name(o.tool_name),
		  tool_args(o.tool_args), observation(o.observation), timestamp(o.timestamp) {}
	AgentStep& operator=(const AgentStep& o) {
		step_index = o.step_index; thought = o.thought; tool_name = o.tool_name;
		tool_args = o.tool_args; observation = o.observation; timestamp = o.timestamp;
		return *this;
	}
};

struct AgentRun : Moveable<AgentRun> {
	String id;
	String repo;
	String owner;
	String author;
	String model;
	String task_prompt;      // task description
	int    linked_issue = 0; // issue #
	String branch;           // target branch
	String status;           // "running", "completed", "failed", "waiting_review"
	String created_at;       // ISO date
	String duration;
	int    max_steps = 10;
	Vector<AgentStep> steps;
	String patch_diff;       // unified diff

	AgentRun() {}
	AgentRun(const AgentRun& o, int)
		: id(o.id), repo(o.repo), owner(o.owner), author(o.author),
		  model(o.model), task_prompt(o.task_prompt), linked_issue(o.linked_issue),
		  branch(o.branch), status(o.status), created_at(o.created_at),
		  duration(o.duration), max_steps(o.max_steps), patch_diff(o.patch_diff) {
		steps <<= o.steps;
	}
	AgentRun(const AgentRun& o)
		: id(o.id), repo(o.repo), owner(o.owner), author(o.author),
		  model(o.model), task_prompt(o.task_prompt), linked_issue(o.linked_issue),
		  branch(o.branch), status(o.status), created_at(o.created_at),
		  duration(o.duration), max_steps(o.max_steps), patch_diff(o.patch_diff) {
		steps <<= o.steps;
	}
	AgentRun& operator=(const AgentRun& o) {
		id = o.id; repo = o.repo; owner = o.owner; author = o.author;
		model = o.model; task_prompt = o.task_prompt; linked_issue = o.linked_issue;
		branch = o.branch; status = o.status; created_at = o.created_at;
		duration = o.duration; max_steps = o.max_steps; patch_diff = o.patch_diff;
		steps <<= o.steps;
		return *this;
	}
};

// ---------------------------------------------------------------------------
// SweAgent Platform Service
// ---------------------------------------------------------------------------

class AgentPlatformService {
public:
	static String           GetAgentStoreDir(const String& owner, const String& repo);
	static Vector<AgentRun> GetRuns(const String& owner, const String& repo,
	                                const String& author_filter = "",
	                                const String& status_filter = "");
	static AgentRun         GetRun(const String& owner, const String& repo, const String& run_id);
	static bool             SaveRun(const String& owner, const String& repo, const AgentRun& run);
	static AgentRun         DispatchRun(const String& owner, const String& repo,
	                                    const String& author, const String& model,
	                                    const String& prompt, int linked_issue,
	                                    const String& branch, int max_steps = 10);
	static bool             ApplyPatch(const String& owner, const String& repo,
	                                   const String& run_id, String& out_message);

	// UI Component Renderers
	static String           RenderRunsDashboard(const String& owner, const String& repo,
	                                            const String& active_author,
	                                            const String& active_status,
	                                            const String& active_run_id);
	static String           RenderTrajectoryInspector(const String& owner, const String& repo,
	                                                  const AgentRun& run);
	static String           RenderDiffViewer(const String& diff_text);
};

END_UPP_NAMESPACE

#endif
