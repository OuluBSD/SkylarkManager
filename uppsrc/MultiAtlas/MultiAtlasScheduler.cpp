#include "MultiAtlasScheduler.h"

NAMESPACE_UPP

static bool AddRequirements(const String& local_track, const Vector<String>& requirements,
	                        const Index<String>& known, MultiAtlasTrackGraph& graph,
	                        String& error)
{
	for(const String& literal : requirements) {
		MultiAtlasContractRef ref;
		String parse_error;
		if(!MultiAtlasParseContract(literal, ref, parse_error)) {
			error = "track " + local_track + " has invalid dependency contract '" + literal + "': " + parse_error;
			return false;
		}
		if(ref.track_id.IsEmpty())
			continue;
		if(known.Find(ref.track_id) < 0) {
			error = "track " + local_track + " contract '" + literal + "' references unknown track '" + ref.track_id + "'";
			return false;
		}
		if(ref.track_id == local_track) {
			error = "track " + local_track + " has self-dependency contract '" + literal + "'";
			return false;
		}
		MultiAtlasTrackDependency edge;
		edge.from_track = ref.track_id;
		edge.to_track = local_track;
		edge.contract = literal;
		graph.edges.Add(pick(edge));
	}
	return true;
}


static bool AddTrackRequirements(const String& local_track, const MultiAtlasTrackReplayInput& track,
	                             const Index<String>& known, MultiAtlasTrackGraph& graph,
	                             String& error)
{
	for(const MultiAtlasVersion& version : track.versions)
		if(!AddRequirements(local_track, version.requires, known, graph, error))
			return false;
	for(const MultiAtlasAction& action : track.rules.actions)
		if(!AddRequirements(local_track, action.requires, known, graph, error))
			return false;
	for(const MultiAtlasEvent& event : track.events)
		if(!AddRequirements(local_track, event.requires, known, graph, error))
			return false;
	return true;
}

bool MultiAtlasBuildDependencyGraph(const MultiAtlasReplayInput& input,
	                                 MultiAtlasTrackGraph& graph, String& error)
{
	graph.nodes.Clear();
	graph.edges.Clear();
	error.Clear();
	Index<String> known;
	for(const MultiAtlasTrackReplayInput& track : input.tracks) {
		if(!MultiAtlasIsValidTrackId(track.track_id)) {
			error = "invalid replay track_id: " + track.track_id;
			return false;
		}
		if(known.Find(track.track_id) >= 0) {
			error = "duplicate replay track_id: " + track.track_id;
			return false;
		}
		known.Add(track.track_id);
		graph.nodes.Add(track.track_id);
	}
	if(!input.meta.track_id.IsEmpty() && input.meta.track_id != "meta") {
		error = "meta replay input must use track_id 'meta'";
		return false;
	}
	if(known.Find("meta") >= 0) {
		error = "track_id 'meta' is reserved for the meta-map";
		return false;
	}
	known.Add("meta");
	graph.nodes.Add("meta");

	for(const MultiAtlasTrackReplayInput& track : input.tracks)
		if(!AddTrackRequirements(track.track_id, track, known, graph, error))
			return false;
	if(!AddTrackRequirements("meta", input.meta, known, graph, error))
		return false;
	return true;
}

struct SchedulerAdjacency : Moveable<SchedulerAdjacency> {
	Vector<int> edges;
};

static bool FindCycleFrom(int node, const MultiAtlasTrackGraph& graph,
	                      const Vector<SchedulerAdjacency>& adjacency,
	                      const Vector<int>& to_nodes,
	                      Vector<int>& state, Vector<int>& stack_position,
	                      Vector<int>& parent_edge, Vector<int>& stack,
	                      Vector<int>& cycle_edges)
{
	state[node] = 1;
	stack_position[node] = stack.GetCount();
	stack.Add(node);
	for(int edge_index : adjacency[node].edges) {
		int next = to_nodes[edge_index];
		if(state[next] == 0) {
			parent_edge[next] = edge_index;
			if(FindCycleFrom(next, graph, adjacency, to_nodes, state, stack_position,
			                 parent_edge, stack, cycle_edges))
				return true;
		}
		else if(state[next] == 1) {
			for(int s = stack_position[next] + 1; s < stack.GetCount(); s++)
				cycle_edges.Add(parent_edge[stack[s]]);
			cycle_edges.Add(edge_index);
			return true;
		}
	}
	stack.Remove(stack.GetCount() - 1);
	stack_position[node] = -1;
	state[node] = 2;
	return false;
}

bool MultiAtlasTopologicalSort(const MultiAtlasTrackGraph& graph,
	                            Vector<String>& order, String& cycle_err)
{
	order.Clear();
	cycle_err.Clear();
	int n = graph.nodes.GetCount();
	Vector<SchedulerAdjacency> adjacency;
	Vector<int> indegree;
	Vector<int> to_nodes;
	adjacency.SetCount(n);
	indegree.SetCount(n, 0);
	Index<String> node_index;
	for(const String& node : graph.nodes) {
		if(node_index.Find(node) >= 0) {
			cycle_err = "dependency graph contains duplicate node '" + node + "'";
			return false;
		}
		node_index.Add(node);
	}
	for(int e = 0; e < graph.edges.GetCount(); e++) {
		int from = node_index.Find(graph.edges[e].from_track);
		int to = node_index.Find(graph.edges[e].to_track);
		if(from < 0 || to < 0) {
			cycle_err = "dependency graph edge references an unknown node";
			return false;
		}
		adjacency[from].edges.Add(e);
		indegree[to]++;
		to_nodes.Add(to);
	}
	Vector<bool> emitted;
	emitted.SetCount(n, false);
	Vector<int> ready;
	int meta_node = -1;
	for(int i = 0; i < n; i++)
		if(graph.nodes[i] == "meta") meta_node = i;
	for(int i = 0; i < n; i++)
		if(indegree[i] == 0)
			ready.Add(i);
	int remaining = n;
	for(int next_ready = 0; next_ready < ready.GetCount() && remaining; next_ready++) {
		int current = ready[next_ready];
		// Keep meta-map last even when it has no explicit gate requirement.
		if(current == meta_node && remaining > 1) continue;
		emitted[current] = true;
		remaining--;
		order.Add(graph.nodes[current]);
		for(int edge_index : adjacency[current].edges) {
			int to = to_nodes[edge_index];
			indegree[to]--;
			if(indegree[to] == 0)
				ready.Add(to);
		}
	}
	if(remaining == 1 && meta_node >= 0 && !emitted[meta_node] && indegree[meta_node] == 0) {
		order.Add(graph.nodes[meta_node]);
		emitted[meta_node] = true;
		remaining = 0;
	}
	if(!remaining)
		return true;

	Vector<int> state, stack_position, parent_edge, stack, cycle_edges;
	state.SetCount(n, 0);
	stack_position.SetCount(n, -1);
	parent_edge.SetCount(n, -1);
	for(int i = 0; i < n; i++)
		if(!emitted[i] && state[i] == 0 && FindCycleFrom(i, graph, adjacency, to_nodes,
	                                                 state, stack_position, parent_edge,
		                                                 stack, cycle_edges))
			break;
	if(cycle_edges.IsEmpty()) {
		cycle_err = "dependency graph cannot be topologically ordered";
		return false;
	}
	const MultiAtlasTrackDependency& first = graph.edges[cycle_edges[0]];
	cycle_err = first.from_track;
	for(int edge_index : cycle_edges) {
		const MultiAtlasTrackDependency& edge = graph.edges[edge_index];
		cycle_err << " -[" << edge.contract << "]-> " << edge.to_track;
	}
	return false;
}

END_UPP_NAMESPACE
