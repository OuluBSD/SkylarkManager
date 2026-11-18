#ifndef _MultiAtlas_MultiAtlasStore_h_
#define _MultiAtlas_MultiAtlasStore_h_

#include "MultiAtlasModel.h"
#include "MultiAtlasWarningLedger.h"

NAMESPACE_UPP

// Resolve a configured track (or the reserved "meta" track) to its
// repository-backed <repo>/<store>/data root. Empty means invalid/unavailable.
String MultiAtlasResolveTrackStore(const MultiAtlasEnsembleDef& ens, const String& track_id);

bool MultiAtlasLoadTrackMap(const String& store_root, const String& key,
                            MultiAtlasMap& out, String& rev, String& err);
bool MultiAtlasSaveTrackMap(const String& store_root, const String& key,
                            const MultiAtlasMap& in, const String& prev_rev, String& err);

bool MultiAtlasLoadTrackFamily(const String& store_root, const String& key,
                               MultiAtlasTrack& out, String& rev, String& err);
bool MultiAtlasSaveTrackFamily(const String& store_root, const String& key,
                               const MultiAtlasTrack& in, const String& prev_rev, String& err);
bool MultiAtlasLoadMetaFamily(const String& store_root, const String& key,
                             MultiAtlasMetaFamily& out, String& rev, String& err);
bool MultiAtlasSaveMetaFamily(const String& store_root, const String& key,
                             const MultiAtlasMetaFamily& in, const String& prev_rev, String& err);

bool MultiAtlasLoadTrackRules(const String& store_root, const String& key,
                              MultiAtlasRules& out, String& rev, String& err);
bool MultiAtlasSaveTrackRules(const String& store_root, const String& key,
                              const MultiAtlasRules& in, const String& prev_rev, String& err);

bool MultiAtlasLoadTrackEvents(const String& store_root, const String& key,
							   const String& expected_track_id,
                               Vector<MultiAtlasEvent>& out, String& err);
// Events are append-only records, created with RecordAbsent().
bool MultiAtlasPutTrackEvent(const String& store_root, const String& key,
							 const String& expected_track_id,
                             const MultiAtlasEvent& event, String& err);

bool MultiAtlasLoadTrackAcks(const String& store_root, const String& track_id,
                             Vector<MultiAtlasWarningAck>& out, String& err);
bool MultiAtlasSaveTrackAck(const String& store_root, const String& track_id,
                            const MultiAtlasWarningAck& ack, const String& prev_rev,
                            String& new_rev, String& err);

END_UPP_NAMESPACE

#endif
