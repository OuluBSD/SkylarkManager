#ifndef _AnnotateApp_AnnotateApp_h_
#define _AnnotateApp_AnnotateApp_h_

#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// AnnotateApp: frames of a window class, with layers and labelled regions.
//
// A window class is a named picture size with three anchors (pixel points that are not on one line).
// Anchors give the geometry: a point snaps to its nearest anchor, a rectangle moves from one window class
// to another through the affine map that carries the three source anchors onto the three target anchors,
// and a slot group is extrapolated around a centre by a fixed angle step.
// Crops are exported from raw 8-bit frames (gray or rgb), with a manifest that lists the source frame,
// the rectangle, the label and the SHA-256 of each crop.
// ---------------------------------------------------------------------------

struct AnnPoint : Moveable<AnnPoint> {
	int x = 0;
	int y = 0;

	AnnPoint() {}
	AnnPoint(int x_, int y_) : x(x_), y(y_) {}
	void Jsonize(JsonIO& io) { io("x", x)("y", y); }
};

struct AnnRect : Moveable<AnnRect> {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;

	AnnRect() {}
	AnnRect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
	void Jsonize(JsonIO& io) { io("x", x)("y", y)("w", w)("h", h); }
};

struct WindowClass : Moveable<WindowClass> {
	String          id;
	String          name;
	int             width = 0;
	int             height = 0;
	Vector<AnnPoint> anchors;   // exactly three
	String          revision;   // from the envelope

	void Jsonize(JsonIO& io) {
		io("id", id)("name", name)("width", width)("height", height)("anchors", anchors);
	}
};

struct FrameLayer : Moveable<FrameLayer> {
	String id;
	String name;
	bool   visible = true;

	void Jsonize(JsonIO& io) { io("id", id)("name", name)("visible", visible); }
};

struct Frame : Moveable<Frame> {
	String            id;
	String            window_class;   // WindowClass id
	String            image_blob;     // BlobStore id of the raw pixels, or empty
	String            content_type;   // raw format of the blob: application/x-raw-gray8 or application/x-raw-rgb8
	String            source_ref;
	Vector<FrameLayer> layers;
	String            revision;

	void Jsonize(JsonIO& io) {
		io("id", id)("window_class", window_class)("image_blob", image_blob)
		  ("content_type", content_type)("source_ref", source_ref)("layers", layers);
	}
};

struct Region : Moveable<Region> {
	String  id;
	String  frame;        // Frame id
	String  layer;        // FrameLayer id of that frame
	AnnRect rect;
	String  label;        // label text written to the manifest
	String  slot_group;   // optional group id; regions of one group are extrapolated together
	String  text_hint;    // optional hint for the labeller
	String  revision;

	void Jsonize(JsonIO& io) {
		io("id", id)("frame", frame)("layer", layer)("rect", rect)("label", label)
		  ("slot_group", slot_group)("text_hint", text_hint);
	}
};

// Pure operations. Each returns false with an error when the input is outside the model.

bool ValidateWindowClass(const WindowClass& wc, String& error);
bool SnapToAnchor(const WindowClass& wc, AnnPoint p, int max_distance, AnnPoint& snapped, int& anchor_index);
bool RescaleRect(const WindowClass& from, const WindowClass& to, const AnnRect& in, AnnRect& out, String& error);
bool ExtrapolateSlots(AnnPoint centre, AnnPoint seed, double step_degrees, int count,
                      Vector<AnnPoint>& slots, String& error);
AnnRect CentredRect(AnnPoint centre, int w, int h);

struct CropResult : Moveable<CropResult> {
	String  region;
	AnnRect rect;
	String  label;
	String  checksum;     // SHA-256 of the crop bytes
	int64   byte_size = 0;
	String  bytes;        // the crop itself
};

// Crops every region of one raw frame. content_type selects the pixel size (1 or 3 bytes).
// Each rectangle must lie inside the frame. The manifest is a JSON object with the frame id,
// the frame checksum and one entry per crop.
bool ExportCrops(const String& frame_id, const String& raw, int width, int height,
                 const String& content_type, const Vector<Region>& regions,
                 Vector<CropResult>& crops, String& manifest_json, String& error);

// Store of window classes, frames and regions. Each is a JsonFileStore collection of the site.
class AnnotateStore {
public:
	AnnotateStore(const String& site_id, const String& data_root);

	StoreResult PutWindowClass(const WindowClass& wc, const String& expected_revision);
	StoreResult GetWindowClass(const String& id, WindowClass& out);
	StoreResult ListWindowClasses(Vector<WindowClass>& out);

	StoreResult PutFrame(const Frame& frame, const String& expected_revision);
	StoreResult GetFrame(const String& id, Frame& out);
	StoreResult ListFrames(Vector<Frame>& out);

	// The region's frame must exist, its layer must belong to that frame and its rectangle must lie
	// inside the frame's window class.
	StoreResult PutRegion(const Region& region, const String& expected_revision);
	StoreResult GetRegion(const String& id, Region& out);
	StoreResult ListRegions(const String& frame_id, Vector<Region>& out);

private:
	JsonFileStore windows;
	JsonFileStore frames;
	JsonFileStore regions;
};

END_UPP_NAMESPACE

#endif
