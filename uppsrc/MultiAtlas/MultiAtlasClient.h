#ifndef _MultiAtlas_MultiAtlasClient_h_
#define _MultiAtlas_MultiAtlasClient_h_

#include "MultiAtlasModel.h"

NAMESPACE_UPP

struct MultiAtlasTrackDetail : Moveable<MultiAtlasTrackDetail> {
	String ensemble, ensemble_title, track_id, title, branch, version, gate;
	String overview_url, track_route_base;
	MultiAtlasMap map;
	MultiAtlasTrack family;
	MultiAtlasRules rules;
	Vector<MultiAtlasEvent> events;
};

String MultiAtlasClientStylesheet();
String MultiAtlasClientScript();
String MultiAtlasClientHtml(const MultiAtlasTrackDetail& detail);

END_UPP_NAMESPACE

#endif
