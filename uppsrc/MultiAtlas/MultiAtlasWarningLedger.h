#ifndef _MultiAtlas_MultiAtlasWarningLedger_h_
#define _MultiAtlas_MultiAtlasWarningLedger_h_

#include "MultiAtlasReplay.h"

NAMESPACE_UPP

struct MultiAtlasWarningAck : Moveable<MultiAtlasWarningAck> {
	String track_id, warning_id, rule, vid, actor, comment, at;
	void Jsonize(JsonIO& io) {
		io("track_id", track_id)("warning_id", warning_id)("rule", rule)
		  ("vid", vid)("actor", actor)("comment", comment)("at", at);
	}
};

struct MultiAtlasStaleAck : Moveable<MultiAtlasStaleAck> {
	MultiAtlasWarningAck ack;
	bool stale = true;
};

// Reuses the registry's canonical syntax check; `meta` is the reserved
// meta-map namespace and is accepted explicitly here.
bool MultiAtlasValidWarningTrackId(const String& track_id);
bool MultiAtlasValidWarningId(const String& warning_id);
String MultiAtlasWarningId(const String& track_id, const MultiAtlasRoadmapMessage& message);

// Records live in <track_store>/data/acks. Symlinked data or acks directories
// are rejected so the JsonFileStore cannot escape the supplied worktree.
bool MultiAtlasSaveAck(const String& track_store, const String& expected_track_id,
	                   const MultiAtlasWarningAck& ack, const String& expected_revision,
	                   String& new_revision, String& error);
bool MultiAtlasLoadAcks(const String& track_store, const String& expected_track_id,
	                   Vector<MultiAtlasWarningAck>& acks, String& error);
bool MultiAtlasFindStaleAcks(const String& track_store, const String& track_id,
	                         const Vector<MultiAtlasRoadmapMessage>& active_findings,
	                         Vector<MultiAtlasStaleAck>& stale_acks, String& error);

END_UPP_NAMESPACE

#endif
