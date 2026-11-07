#include "ManagerSkylark.h"
#include "AgentPlatform.h"
#include "GitHubPlatform.h"

NAMESPACE_UPP

static String EscapeAgentHtml(const String& text)
{
	String out;
	for(int i = 0; i < text.GetCount(); i++) {
		byte c = (byte)text[i];
		if(c < 0x20 && c != '\n' && c != '\r' && c != '\t') { out << "&#xfffd;"; continue; }
		switch(text[i]) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(text[i]); break;
		}
	}
	return out;
}

static String AgentRoute(const String& owner, const String& repo)
{
	return "/repo/" + UrlEncode(owner) + "/" + UrlEncode(repo);
}

static bool SafeAgentRepo(const String& repo)
{
	if(repo.IsEmpty() || repo == "." || repo == "..") return false;
	for(int i = 0; i < repo.GetCount(); i++) {
		char c = repo[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		     (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
			return false;
	}
	return true;
}

static String AgentCapitalizeWord(const String& s)
{
	if(s.IsEmpty()) return s;
	return ToUpper(s.Left(1)) + s.Mid(1);
}

// ---------------------------------------------------------------------------
// Serialization helpers
// ---------------------------------------------------------------------------

static Value StoreAsJsonValue(const AgentStep& s)
{
	ValueMap m;
	m.Set("step_index", s.step_index);
	m.Set("thought", s.thought);
	m.Set("tool_name", s.tool_name);
	m.Set("tool_args", s.tool_args);
	m.Set("observation", s.observation);
	m.Set("timestamp", s.timestamp);
	return m;
}

static void LoadFromJsonValue(AgentStep& s, const Value& v)
{
	if(!v.Is<ValueMap>()) return;
	ValueMap m = v;
	s.step_index = m["step_index"];
	s.thought = m["thought"];
	s.tool_name = m["tool_name"];
	s.tool_args = m["tool_args"];
	s.observation = m["observation"];
	s.timestamp = m["timestamp"];
}

static Value StoreAsJsonValue(const AgentRun& r)
{
	ValueMap m;
	m.Set("id", r.id);
	m.Set("repo", r.repo);
	m.Set("owner", r.owner);
	m.Set("author", r.author);
	m.Set("model", r.model);
	m.Set("task_prompt", r.task_prompt);
	m.Set("linked_issue", r.linked_issue);
	m.Set("branch", r.branch);
	m.Set("status", r.status);
	m.Set("created_at", r.created_at);
	m.Set("duration", r.duration);
	m.Set("max_steps", r.max_steps);
	m.Set("patch_diff", r.patch_diff);

	ValueArray va;
	for(int i = 0; i < r.steps.GetCount(); i++)
		va.Add(StoreAsJsonValue(r.steps[i]));
	m.Set("steps", va);
	return m;
}

static void LoadFromJsonValue(AgentRun& r, const Value& v)
{
	if(!v.Is<ValueMap>()) return;
	ValueMap m = v;
	r.id = m["id"];
	r.repo = m["repo"];
	r.owner = m["owner"];
	r.author = m["author"];
	r.model = m["model"];
	r.task_prompt = m["task_prompt"];
	r.linked_issue = m["linked_issue"];
	r.branch = m["branch"];
	r.status = m["status"];
	r.created_at = m["created_at"];
	r.duration = m["duration"];
	r.max_steps = m["max_steps"];
	r.patch_diff = m["patch_diff"];

	r.steps.Clear();
	Value sv = m["steps"];
	if(sv.Is<ValueArray>()) {
		ValueArray va = sv;
		for(int i = 0; i < va.GetCount(); i++) {
			AgentStep step;
			LoadFromJsonValue(step, va[i]);
			r.steps.Add(pick(step));
		}
	}
}

// ---------------------------------------------------------------------------
// Store Path & Storage
// ---------------------------------------------------------------------------

String AgentPlatformService::GetAgentStoreDir(const String& owner, const String& repo)
{
	// Agent runs have no canonical DataStore schema or T10 writer yet.
	return String();
}

Vector<AgentRun> AgentPlatformService::GetRuns(const String& owner, const String& repo,
                                               const String& author_filter,
                                               const String& status_filter)
{
	// No typed agent-run records are currently registered in DataStore.
	return Vector<AgentRun>();
}

AgentRun AgentPlatformService::GetRun(const String& owner, const String& repo, const String& run_id)
{
	AgentRun empty;
	empty.id = run_id;
	empty.owner = owner;
	empty.repo = repo;
	return empty;
}

bool AgentPlatformService::SaveRun(const String& owner, const String& repo, const AgentRun& run)
{
	// Persistence remains disabled until the canonical T10 writer exists.
	return false;
}

AgentRun AgentPlatformService::DispatchRun(const String& owner, const String& repo,
                                          const String& author, const String& model,
                                          const String& prompt, int linked_issue,
                                          const String& branch, int max_steps)
{
	AgentRun r;
	r.owner = owner;
	r.repo = repo;
	r.status = "not-configured";
	return r;
}

bool AgentPlatformService::ApplyPatch(const String& owner, const String& repo,
                                     const String& run_id, String& out_message)
{
	out_message = "Patch application is disabled.";
	return false;
}

// ---------------------------------------------------------------------------
// UI Component Renderers
// ---------------------------------------------------------------------------

String AgentPlatformService::RenderDiffViewer(const String& diff_text)
{
	String s;
	s << "<div class=\"wf-code-box\" style=\"background:var(--wf-bg-view,#0d1117); border-radius:6px; font-family:monospace; font-size:0.83rem; line-height:1.45; overflow-x:auto;\">\n";

	Vector<String> lines = Split(diff_text, '\n');
	for(int i = 0; i < lines.GetCount(); i++) {
		String line = lines[i];
		String line_bg = "transparent";
		String line_fg = "var(--wf-fg-normal,#e6e8eb)";

		if(line.StartsWith("+") && !line.StartsWith("+++")) {
			line_bg = "rgba(46, 160, 67, 0.15)";
			line_fg = "#7ee787";
		}
		else if(line.StartsWith("-") && !line.StartsWith("---")) {
			line_bg = "rgba(248, 81, 73, 0.15)";
			line_fg = "#f85149";
		}
		else if(line.StartsWith("@@")) {
			line_bg = "rgba(56, 139, 253, 0.15)";
			line_fg = "#79c0ff";
		}

		s << "  <div style=\"padding:1px 12px; background:" << line_bg << "; color:" << line_fg << "; white-space:pre;\">"
		  << EscapeAgentHtml(line) << "</div>\n";
	}
	s << "</div>\n";
	return s;
}

String AgentPlatformService::RenderTrajectoryInspector(const String& owner, const String& repo,
                                                       const AgentRun& run)
{
	String s;

	s << "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1.25rem;\">\n"
	  << "  <div>\n"
	  << "    <a href=\"" << EscapeAgentHtml(AgentRoute(owner, repo)) << "?tab=agents\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem; display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("arrow-left", 14) << " Back to Agent Runs</a>\n"
	  << "  </div>\n"
	  << "  <div style=\"display:flex; gap:8px;\">\n"
	  << "    <span class=\"wf-badge wf-badge-neutral\">Patch application and publication are disabled</span>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Run Overview Card
	s << "<div class=\"wf-card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"wf-card-header\">\n"
	  << "    <div style=\"display:flex; align-items:center; gap:10px;\">\n"
	  << "      <span style=\"font-size:1.15rem; font-weight:700;\">" << EscapeAgentHtml(run.id) << "</span>\n"
	  << "      <span class=\"wf-badge wf-badge-" << (run.status == "completed" ? "emerald" : (run.status == "running" ? "amber" : "neutral")) << "\">" << EscapeAgentHtml(AgentCapitalizeWord(run.status)) << "</span>\n"
	  << "      <span style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem;\">&middot; Model: <strong>" << EscapeAgentHtml(run.model) << "</strong></span>\n"
	  << "      <span style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem;\">&middot; Branch: <code>" << EscapeAgentHtml(run.branch) << "</code></span>\n";
	if(run.linked_issue > 0)
		s << "      <span style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem;\">&middot; Linked: <a href=\"" << EscapeAgentHtml(AgentRoute(owner, repo)) << "/issues/" << run.linked_issue << "\" style=\"color:var(--wf-accent-link,#00acd3); font-weight:600;\">#" << run.linked_issue << "</a></span>\n";
	s << "    </div>\n"
	  << "    <span style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem;\">Duration: " << EscapeAgentHtml(run.duration) << "</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"wf-card-body\">\n"
	  << "    <div style=\"font-weight:600; font-size:1rem; margin-bottom:6px;\">Task Prompt</div>\n"
	  << "    <pre style=\"background:var(--wf-surface,#161b22); border-left:3px solid var(--wf-color-positive,#00aa7f); padding:10px 14px; border-radius:0 6px 6px 0; font-size:0.9rem; white-space:pre-wrap;\">" << EscapeAgentHtml(run.task_prompt) << "</pre>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Step-by-Step Trajectory
	s << "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1rem;\">\n"
	  << "  <h3 style=\"margin:0;\">Autonomous Execution Trajectory (" << run.steps.GetCount() << " Steps)</h3>\n"
	  << "  <span class=\"wf-badge wf-badge-neutral\">ReAct Loop</span>\n"
	  << "</div>\n";

	for(int i = 0; i < run.steps.GetCount(); i++) {
		const AgentStep& step = run.steps[i];
		s << "<div class=\"wf-card\" style=\"margin-bottom:1rem;\">\n"
		  << "  <div class=\"wf-card-header\" style=\"background:rgba(255,255,255,0.02);\">\n"
		  << "    <div style=\"display:flex; align-items:center; gap:8px;\">\n"
		  << "      <span class=\"wf-badge wf-badge-neutral\" style=\"font-weight:700;\">Step " << step.step_index << "</span>\n"
		  << "      <span style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d);\">" << EscapeAgentHtml(step.timestamp) << "</span>\n"
		  << "    </div>\n"
		  << "    <span class=\"wf-badge wf-badge-cyan\">" << EscapeAgentHtml(step.tool_name) << "</span>\n"
		  << "  </div>\n"
		  << "  <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:10px;\">\n"
		  << "    <div id=\"agent-step-thought\" style=\"background:rgba(0,161,197,0.08); border-left:3px solid #00a1c5; padding:8px 12px; border-radius:0 4px 4px 0; font-size:0.88rem;\">\n"
		  << "      <strong style=\"color:#7dd3fc;\">Thought:</strong> " << EscapeAgentHtml(step.thought) << "\n"
		  << "    </div>\n"
		  << "    <div id=\"agent-step-tool\">\n"
		  << "      <div style=\"font-size:0.75rem; font-weight:600; color:var(--wf-fg-inactive,#8b919d); margin-bottom:2px;\">ACTION &bull; " << EscapeAgentHtml(ToUpper(step.tool_name)) << "</div>\n"
		  << "      <pre style=\"background:var(--wf-bg-view,#0d1117); padding:8px 12px; border-radius:4px; font-family:monospace; font-size:0.82rem; border:1px solid var(--wf-border-color,#30363d); color:#7ee787; white-space:pre-wrap;\">" << EscapeAgentHtml(step.tool_args) << "</pre>\n"
		  << "    </div>\n"
		  << "    <div id=\"agent-step-obs\">\n"
		  << "      <div style=\"font-size:0.75rem; font-weight:600; color:var(--wf-fg-inactive,#8b919d); margin-bottom:2px;\">OBSERVATION</div>\n"
		  << "      <pre style=\"background:var(--wf-bg-view,#0d1117); padding:8px 12px; border-radius:4px; font-family:monospace; font-size:0.82rem; border:1px solid var(--wf-border-color,#30363d); margin:0; white-space:pre-wrap; color:var(--wf-fg-normal,#e6e8eb);\">" << EscapeAgentHtml(step.observation) << "</pre>\n"
		  << "    </div>\n"
		  << "  </div>\n"
		  << "</div>\n";
	}

	// Generated Patch Diff Card
	if(!run.patch_diff.IsEmpty()) {
		s << "<div id=\"agent-patch-card\" class=\"wf-card\" style=\"margin-top:1.5rem;\">\n"
		  << "  <div class=\"wf-card-header\">\n"
		  << "    <span>" << VividIcon::Get("git-commit", 16) << " Generated Code Patch Diff</span>\n"
		  << "    <span class=\"wf-badge wf-badge-emerald\">Ready to Apply</span>\n"
		  << "  </div>\n"
		  << "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
		  << RenderDiffViewer(run.patch_diff)
		  << "  </div>\n"
		  << "</div>\n";
	}

	return s;
}

String AgentPlatformService::RenderRunsDashboard(const String& owner, const String& repo,
                                                const String& active_author,
                                                const String& active_status,
                                                const String& active_run_id)
{
	if(!active_run_id.IsEmpty()) {
		AgentRun run = GetRun(owner, repo, active_run_id);
		return RenderTrajectoryInspector(owner, repo, run);
	}

	Vector<AgentRun> runs = GetRuns(owner, repo, active_author, active_status);
	String s;
	if(runs.IsEmpty())
		return "<div class=\"wf-card\"><div class=\"wf-card-body\">No recorded agent runs. External dispatch is disabled.</div></div>";

	// Top Filter & Action Bar
	s << "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1.25rem; flex-wrap:wrap; gap:10px;\">\n"
	  << "  <div style=\"display:flex; align-items:center; gap:10px; flex-wrap:wrap;\">\n"
	  << "    <input type=\"text\" id=\"agent-search\" placeholder=\"Filter agent runs...\" value=\"" << EscapeAgentHtml(active_author.IsEmpty() ? String() : "author:" + active_author) << "\" style=\"background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 12px; font-size:0.85rem; width:220px;\">\n"
	  << "    <a href=\"" << EscapeAgentHtml(AgentRoute(owner, repo)) << "?tab=agents\" class=\"wf-btn " << (active_author.IsEmpty() && active_status.IsEmpty() ? "wf-btn-primary" : "wf-btn-secondary") << "\" style=\"font-size:0.8rem;\">All Runs</a>\n"
	  << "    <a href=\"" << EscapeAgentHtml(AgentRoute(owner, repo)) << "?tab=agents&status=completed\" class=\"wf-btn " << (active_status == "completed" ? "wf-btn-primary" : "wf-btn-secondary") << "\" style=\"font-size:0.8rem;\">Completed</a>\n"
	  << "  </div>\n"
	  << "  <button type=\"button\" class=\"wf-btn wf-btn-secondary\" disabled title=\"Provider policy is not configured\">External dispatch disabled</button>\n"
	  << "</div>\n";

	// Interactive Dispatch Modal Card (Hidden by default)
	s << "<div id=\"agent-dispatch-card\" class=\"wf-card\" style=\"display:none; margin-bottom:1.5rem; border:1px solid var(--wf-color-positive,#00aa7f);\">\n"
	  << "  <div class=\"wf-card-header\">\n"
	  << "    <span>" << VividIcon::Get("robot", 16) << " Agent dispatch unavailable</span>\n"
	  << "    <span class=\"wf-badge wf-badge-neutral\">External dispatch disabled</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:12px;\">\n"
	  << "    <div>\n"
	  << "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Task Prompt / Instructions</label>\n"
	  << "      <textarea id=\"agent-prompt\" placeholder=\"Describe a task for " << EscapeAgentHtml(owner) << "/" << EscapeAgentHtml(repo) << "...\" style=\"width:100%; height:85px; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 12px; font-family:inherit;\"></textarea>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:grid; grid-template-columns:1fr 1fr 1fr; gap:12px;\">\n"
	  << "      <div>\n"
	  << "        <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">AI Model</label>\n"
	  << "        <select id=\"agent-model\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 10px;\">\n"
	  << "          <option value=\"nvidia/nemotron-3.5-super-vl\">nvidia/nemotron-3.5-super-vl</option>\n"
	  << "          <option value=\"qwen/qwen-2.5-coder-32b-instruct\">qwen/qwen-2.5-coder-32b-instruct</option>\n"
	  << "          <option value=\"anthropic/claude-3.5-sonnet\">anthropic/claude-3.5-sonnet</option>\n"
	  << "          <option value=\"deepseek/deepseek-chat\">deepseek/deepseek-chat</option>\n"
	  << "        </select>\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Link with GitHub Issue</label>\n"
	  << "        <select id=\"agent-issue\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 10px;\">\n"
	  << "          <option value=\"0\">None (Standalone Task)</option>\n"
	  << "          <option value=\"1\">#1 &middot; ManagerSkylark GitHub Parity Integration</option>\n"
	  << "        </select>\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Target Branch</label>\n"
	  << "        <input type=\"text\" id=\"agent-branch\" value=\"master\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 10px;\">\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; justify-content:flex-end; gap:10px; margin-top:6px;\">\n"
	  << "      <button type=\"button\" class=\"wf-btn wf-btn-secondary\" onclick=\"AgentPlatformUI.toggleDispatchModal()\">Cancel</button>\n"
	  << "      <button type=\"button\" id=\"btn-dispatch-agent\" class=\"wf-btn wf-btn-secondary\" disabled>External dispatch disabled</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Runs Table
	s << "<div class=\"wf-card\">\n"
	  << "  <div class=\"wf-card-header\">\n"
	  << "    <span>SweAgent Execution Runs</span>\n"
	  << "    <span class=\"wf-badge wf-badge-neutral\">" << runs.GetCount() << " Runs</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
	  << "    <table class=\"wf-table\">\n"
	  << "      <thead><tr><th>Run ID &amp; Task</th><th>Model</th><th>Author</th><th>Linked Issue</th><th>Steps</th><th>Duration</th><th>Status</th><th>Action</th></tr></thead>\n"
	  << "      <tbody>\n";

	for(int i = 0; i < runs.GetCount(); i++) {
		const AgentRun& r = runs[i];
		String run_url = EscapeAgentHtml(AgentRoute(owner, repo));
		String safe_status = r.status == "completed" ? "completed" : (r.status == "running" ? "running" : "neutral");
		s << "        <tr class=\"agent-run-row\" id=\"row-" << EscapeAgentHtml(r.id) << "\">\n"
		  << "          <td>\n"
		  << "            <a href=\"" << run_url << "?tab=agents&run=" << UrlEncode(r.id) << "\" style=\"font-weight:600; color:var(--wf-accent-link,#00acd3); text-decoration:none;\">" << EscapeAgentHtml(r.id) << "</a><br>\n"
		  << "            <span style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#8b919d);\">" << EscapeAgentHtml(r.task_prompt.Left(60)) << (r.task_prompt.GetLength() > 60 ? "..." : "") << "</span>\n"
		  << "          </td>\n"
		  << "          <td><code style=\"font-size:0.75rem;\">" << EscapeAgentHtml(r.model) << "</code></td>\n"
		  << "          <td><strong>" << EscapeAgentHtml(r.author) << "</strong></td>\n"
		  << "          <td>" << (r.linked_issue > 0 ? ("<a href=\"" + run_url + "/issues/" + AsString(r.linked_issue) + "\" style=\"color:var(--wf-accent-link,#00acd3); font-weight:600;\">#" + AsString(r.linked_issue) + "</a>") : "<span style=\"color:#8b919d;\">-</span>") << "</td>\n"
		  << "          <td><strong>" << r.steps.GetCount() << "</strong></td>\n"
		  << "          <td>" << EscapeAgentHtml(r.duration) << "</td>\n"
		  << "          <td><span class=\"wf-badge wf-badge-" << (safe_status == "completed" ? "emerald" : (safe_status == "running" ? "amber" : "neutral")) << "\">" << EscapeAgentHtml(AgentCapitalizeWord(r.status)) << "</span></td>\n"
		  << "          <td><a href=\"" << run_url << "?tab=agents&run=" << UrlEncode(r.id) << "\" class=\"wf-btn wf-btn-secondary\" style=\"padding:3px 10px; font-size:0.75rem;\">Inspect Trajectory</a></td>\n"
		  << "        </tr>\n";
	}

	s << "      </tbody>\n"
	  << "    </table>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// JavaScript for Agent Platform Actions
	s << "<script>\n"
	  << "window.AgentPlatformUI = {\n"
	  << "  toggleDispatchModal: function() {\n"
	  << "    let el = document.getElementById('agent-dispatch-card');\n"
	  << "    if(el) el.style.display = (el.style.display === 'none' ? 'block' : 'none');\n"
	  << "  },\n"
	  << "  createPr: function() {\n"
	  << "    alert('Pull request publication is disabled.');\n"
	  << "  }\n"
	  << "};\n"
	  << "</script>\n";

	return s;
}

// ---------------------------------------------------------------------------
// SKYLARK REST API Handlers
// ---------------------------------------------------------------------------

SKYLARK(ApiAgentRunsList, "api/agent/runs/*/*")
{
	String owner = http[0];
	String repo  = http[1];
	String author = http["author"];
	String status = http["status"];

	Vector<AgentRun> runs = AgentPlatformService::GetRuns(owner, repo, author, status);
	ValueArray va;
	for(int i = 0; i < runs.GetCount(); i++)
		va.Add(StoreAsJsonValue(runs[i]));

	http.ContentType("application/json") << AsJSON(va, true);
}

SKYLARK(ApiAgentRunDetail, "api/agent/run/*/*/*")
{
	String owner  = http[0];
	String repo   = http[1];
	String run_id = http[2];

	AgentRun run = AgentPlatformService::GetRun(owner, repo, run_id);
	http.ContentType("application/json") << AsJSON(StoreAsJsonValue(run), true);
}

SKYLARK(ApiAgentDispatch, "api/agent/dispatch/*/*:POST_RAW")
{
	ValueMap res;
	res.Set("ok", false);
	res.Set("status", "disabled");
	res.Set("available", false);
	res.Set("error", "external_model_dispatch_disabled");
	http.ContentType("application/json");
	http.Response(503, "Service Unavailable");
	http << AsJSON(res, true);
}

SKYLARK(ApiAgentApplyPatch, "api/agent/apply/*/*/*:POST_RAW")
{
	ValueMap res;
	res.Set("ok", false);
	res.Set("status", "disabled");
	res.Set("available", false);
	res.Set("error", "patch_publication_disabled");
	http.ContentType("application/json");
	http.Response(503, "Service Unavailable");
	http << AsJSON(res, true);
}

END_UPP_NAMESPACE
