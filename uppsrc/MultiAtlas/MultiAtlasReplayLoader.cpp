#include "MultiAtlasReplayLoader.h"
#include "MultiAtlasStore.h"

NAMESPACE_UPP

bool MultiAtlasLoadReplayInput(const MultiAtlasEnsembleDef& ensemble,
	                           MultiAtlasReplayInput& input,
	                           Vector<MultiAtlasWarningAck>& acknowledgments,
	                           String& error)
{
	error.Clear(); acknowledgments.Clear(); input.tracks.Clear();
	input.meta = MultiAtlasTrackReplayInput();
	for(const MultiAtlasTrackDef& def : ensemble.tracks) {
		String root = MultiAtlasResolveTrackStore(ensemble, def.track_id), revision;
		if(root.IsEmpty()) { error = "track store unavailable"; return false; }
		MultiAtlasTrack family;
		if(!MultiAtlasLoadTrackFamily(root, def.family, family, revision, error)) return false;
		if(family.track_id != def.track_id || family.family != def.family) {
			error = "track family identity does not match registry"; return false;
		}
		MultiAtlasMap map;
		if(!MultiAtlasLoadTrackMap(root, def.family, map, revision, error)) return false;
		if(map.track_id != def.track_id) { error = "track map identity does not match registry"; return false; }
		MultiAtlasRules rules;
		if(!MultiAtlasLoadTrackRules(root, def.family, rules, revision, error)) return false;
		MultiAtlasTrackReplayInput track;
		track.track_id = def.track_id;
		for(const MultiAtlasVersion& version : family.versions) {
			MultiAtlasVersion copy; copy.vid = version.vid; copy.code = version.code;
			copy.requires <<= version.requires; track.versions.Add(pick(copy));
		}
		track.rules = pick(rules);
		if(!MultiAtlasLoadTrackEvents(root, def.family, def.track_id, track.events, error)) return false;
		input.tracks.Add(pick(track));
		Vector<MultiAtlasWarningAck> track_acks;
		if(!MultiAtlasLoadTrackAcks(root, def.track_id, track_acks, error)) return false;
		for(MultiAtlasWarningAck& ack : track_acks) acknowledgments.Add(pick(ack));
	}

	String root = MultiAtlasResolveTrackStore(ensemble, "meta"), revision;
	if(root.IsEmpty()) { error = "meta store unavailable"; return false; }
	MultiAtlasMetaFamily family;
	if(!MultiAtlasLoadMetaFamily(root, ensemble.meta_family, family, revision, error)) return false;
	MultiAtlasMap map;
	if(!MultiAtlasLoadTrackMap(root, ensemble.meta_family, map, revision, error)) return false;
	if(map.track_id != "meta") { error = "meta map identity does not match registry"; return false; }
	MultiAtlasRules rules;
	if(!MultiAtlasLoadTrackRules(root, ensemble.meta_family, rules, revision, error)) return false;
	input.meta.track_id = "meta";
	for(const MultiAtlasVersion& version : family.versions) {
		MultiAtlasVersion copy; copy.vid = version.vid; copy.code = version.code;
		copy.requires <<= version.requires; input.meta.versions.Add(pick(copy));
	}
	input.meta.rules = pick(rules);
	if(!MultiAtlasLoadTrackEvents(root, ensemble.meta_family, "meta", input.meta.events, error)) return false;
	Vector<MultiAtlasWarningAck> meta_acks;
	if(!MultiAtlasLoadTrackAcks(root, "meta", meta_acks, error)) return false;
	for(MultiAtlasWarningAck& ack : meta_acks) acknowledgments.Add(pick(ack));
	return true;
}

END_UPP_NAMESPACE
