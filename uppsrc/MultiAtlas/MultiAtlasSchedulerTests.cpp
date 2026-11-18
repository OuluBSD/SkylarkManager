#include "MultiAtlasScheduler.h"

NAMESPACE_UPP

static int s_scheduler_checks;
static int s_scheduler_failures;

static void SchedulerCheck(bool ok, const String& message)
{
	s_scheduler_checks++;
	if(!ok) {
		s_scheduler_failures++;
		Cout() << "FAIL: " << message << '\n';
	}
}

static void AddVersion(MultiAtlasTrackReplayInput& track, const String& literal)
{
	MultiAtlasVersion version;
	version.vid = "v1";
	version.requires.Add(literal);
	track.versions.Add(pick(version));
}

void RunMultiAtlasSchedulerTests()
{
	s_scheduler_checks = s_scheduler_failures = 0;
	MultiAtlasReplayInput input;
	input.tracks.SetCount(4);
	input.tracks[0].track_id = "vision";
	input.tracks[1].track_id = "ai";
	input.tracks[2].track_id = "agent";
	input.tracks[3].track_id = "hud";
	AddVersion(input.tracks[2], "vision:artifact:frames");
	MultiAtlasAction ai_action;
	ai_action.name = "train";
	input.tracks[1].rules.actions.Add(pick(ai_action));
	MultiAtlasAction agent_action;
	agent_action.name = "plan";
	agent_action.requires.Add("ai:artifact:solver");
	input.tracks[2].rules.actions.Add(pick(agent_action));
	MultiAtlasAction hud_action;
	hud_action.name = "integrate";
	hud_action.requires.Add("agent:done:strategy");
	input.tracks[3].rules.actions.Add(pick(hud_action));
	MultiAtlasEvent hud_event;
	hud_event.requires.Add("agent:artifact:frame");
	input.tracks[3].events.Add(pick(hud_event));
	AddVersion(input.meta, "vision:tested:0.1.999");
	AddVersion(input.meta, "ai:tested:0.2.999");
	AddVersion(input.meta, "agent:tested:0.5.999");
	AddVersion(input.meta, "hud:tested:0.7.999");

	MultiAtlasTrackGraph graph;
	String error;
	bool built = MultiAtlasBuildDependencyGraph(input, graph, error);
	SchedulerCheck(built && error.IsEmpty() && graph.nodes.GetCount() == 5 &&
	               graph.edges.GetCount() == 8,
	               "version, action, event, and meta gate contracts produce all expected dependency edges");
	Vector<String> order;
	String cycle;
	bool sorted = built && MultiAtlasTopologicalSort(graph, order, cycle);
	SchedulerCheck(sorted && order.GetCount() == 5 && order[0] == "vision" &&
	               order[1] == "ai" && order[2] == "agent" && order[3] == "hud" &&
	               order[4] == "meta",
	               "diamond dependencies sort deterministically with meta last");

	MultiAtlasReplayInput independent;
	independent.tracks.SetCount(2);
	independent.tracks[0].track_id = "first";
	independent.tracks[1].track_id = "second";
	MultiAtlasTrackGraph independent_graph;
	bool independent_built = MultiAtlasBuildDependencyGraph(independent, independent_graph, error);
	bool independent_sorted = independent_built &&
	                          MultiAtlasTopologicalSort(independent_graph, order, cycle);
	SchedulerCheck(independent_sorted && order.GetCount() == 3 && order[0] == "first" &&
	               order[1] == "second" && order[2] == "meta",
	               "meta remains last even when no contracts mention it");

	MultiAtlasReplayInput cyclic;
	cyclic.tracks.SetCount(2);
	cyclic.tracks[0].track_id = "trackA";
	cyclic.tracks[1].track_id = "trackB";
	AddVersion(cyclic.tracks[0], "trackB:artifact:atomY");
	AddVersion(cyclic.tracks[1], "trackA:artifact:atomX");
	MultiAtlasTrackGraph cycle_graph;
	bool cycle_built = MultiAtlasBuildDependencyGraph(cyclic, cycle_graph, error);
	cycle.Clear();
	bool cycle_sorted = cycle_built && MultiAtlasTopologicalSort(cycle_graph, order, cycle);
	SchedulerCheck(cycle_built && !cycle_sorted &&
	               cycle == "trackA -[trackA:artifact:atomX]-> trackB -[trackB:artifact:atomY]-> trackA",
	               "cycle diagnostics identify the complete closed path and both contract literals: " + cycle);

	MultiAtlasReplayInput self;
	self.tracks.SetCount(1);
	self.tracks[0].track_id = "trackA";
	AddVersion(self.tracks[0], "trackA:artifact:atomX");
	SchedulerCheck(!MultiAtlasBuildDependencyGraph(self, cycle_graph, error) &&
	               error.Find("self-dependency") >= 0 && error.Find("trackA:artifact:atomX") >= 0,
	               "explicit self-dependencies are rejected with the contract literal");

	MultiAtlasReplayInput unknown;
	unknown.tracks.SetCount(1);
	unknown.tracks[0].track_id = "trackA";
	AddVersion(unknown.tracks[0], "missing:artifact:x");
	SchedulerCheck(!MultiAtlasBuildDependencyGraph(unknown, cycle_graph, error) &&
	               error.Find("unknown track 'missing'") >= 0,
	               "unknown cross-track namespaces are rejected");

	Cout() << "MultiAtlasSchedulerTest checks: " << (s_scheduler_checks - s_scheduler_failures)
	       << "/" << s_scheduler_checks << " passed\n";
	if(s_scheduler_failures)
		SetExitCode(1);
}

END_UPP_NAMESPACE
