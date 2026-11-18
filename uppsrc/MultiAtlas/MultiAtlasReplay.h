#ifndef _MultiAtlas_MultiAtlasReplay_h_
#define _MultiAtlas_MultiAtlasReplay_h_

#include "MultiAtlasScheduler.h"

NAMESPACE_UPP

struct MultiAtlasRoadmapMessage : Moveable<MultiAtlasRoadmapMessage> {
	String severity;
	String track_id;
	String vid;
	String subject_kind;
	String subject_id;
	String rule;
	String literal;
	String text;
};

struct MultiAtlasReplayContext : Moveable<MultiAtlasReplayContext> {
	VectorMap<String, Vector<String>> exported_atoms_by_track;
	VectorMap<String, VectorMap<String, Vector<String>>> version_worlds_by_track;
	Vector<MultiAtlasRoadmapMessage> messages;
	int total_errors = 0;
	int total_warnings = 0;
	String fatal_error;
};

struct MultiAtlasReplaySources : Moveable<MultiAtlasReplaySources> {
	VectorMap<String, Vector<String>> final_exports;
	VectorMap<String, VectorMap<String, Vector<String>>> version_exports;
};

// Replays one timeline into caller-owned output containers. Imported atoms are
// namespaced literals. Anchored imports are supplied in version_worlds_by_track.
Vector<String> MultiAtlasSingleTrackReplay(
	const MultiAtlasTrackReplayInput& track,
	const Vector<String>& imported_atoms,
	const MultiAtlasReplaySources& sources,
	VectorMap<String, Vector<String>>& version_atoms,
	Vector<MultiAtlasRoadmapMessage>& messages);

// Replays all child tracks and then the meta-map in deterministic DAG order.
bool MultiAtlasReplayEnsemble(const MultiAtlasReplayInput& input, MultiAtlasReplayContext& context);

END_UPP_NAMESPACE

#endif
