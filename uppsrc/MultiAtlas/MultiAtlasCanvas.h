#ifndef _MultiAtlas_MultiAtlasCanvas_h_
#define _MultiAtlas_MultiAtlasCanvas_h_

#include "MultiAtlasModel.h"

NAMESPACE_UPP

struct MultiAtlasCanvasLane : Moveable<MultiAtlasCanvasLane> {
	String id, title, branch;
	MultiAtlasMap map;
	Vector<MultiAtlasVersion> versions;
	VectorMap<String, String> element_versions;
	VectorMap<String, String> gate_status;
};

struct MultiAtlasCanvasData : Moveable<MultiAtlasCanvasData> {
	String ensemble, title;
	Vector<MultiAtlasCanvasLane> lanes;
};

Value MultiAtlasCanvasValue(const MultiAtlasCanvasData& data);
String MultiAtlasCanvasHtml(const MultiAtlasCanvasData& data);
String MultiAtlasCanvasScript();
String MultiAtlasCanvasStylesheet();

END_UPP_NAMESPACE

#endif
