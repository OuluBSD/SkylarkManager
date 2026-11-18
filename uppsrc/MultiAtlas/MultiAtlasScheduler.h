#ifndef _MultiAtlas_MultiAtlasScheduler_h_
#define _MultiAtlas_MultiAtlasScheduler_h_

#include "MultiAtlasContract.h"

NAMESPACE_UPP

struct MultiAtlasTrackDependency : Moveable<MultiAtlasTrackDependency> {
	String from_track;
	String to_track;
	String contract;
};

struct MultiAtlasTrackGraph : Moveable<MultiAtlasTrackGraph> {
	Vector<String> nodes;
	Vector<MultiAtlasTrackDependency> edges;
};

// Nodes follow input track order, with the meta node always last. Unknown
// namespaces and explicit local self-dependencies are rejected with the
// offending literal in error.
bool MultiAtlasBuildDependencyGraph(const MultiAtlasReplayInput& input,
	                                 MultiAtlasTrackGraph& graph, String& error);

// Returns a deterministic topological order. Meta is held until all child
// tracks have been emitted. On failure, cycle_err gives a literal-bearing
// path such as "vision -[ai:artifact:x]-> ai -[vision:done:y]-> vision".
bool MultiAtlasTopologicalSort(const MultiAtlasTrackGraph& graph,
	                            Vector<String>& order, String& cycle_err);

END_UPP_NAMESPACE

#endif
