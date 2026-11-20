#include "AnnotateApp.h"
#include <math.h>

NAMESPACE_UPP

static const int MaxSide = 100000;
static const int MaxSlots = 64;
static const double Pi = 3.14159265358979323846;

static int RoundInt(double v)
{
	return (int)floor(v + 0.5);
}

static int64 Cross(AnnPoint o, AnnPoint a, AnnPoint b)
{
	return (int64)(a.x - o.x) * (b.y - o.y) - (int64)(a.y - o.y) * (b.x - o.x);
}

static bool InsideWindow(const WindowClass& wc, AnnPoint p)
{
	return p.x >= 0 && p.y >= 0 && p.x <= wc.width && p.y <= wc.height;
}

// True when a non-empty rectangle lies inside a width x height area. The test is written as a subtraction:
// x and w come from client JSON and may be near INT_MAX, so x + w could wrap negative and pass a sum check.
static bool RectInside(const AnnRect& r, int64 width, int64 height)
{
	return r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0
	    && (int64)r.w <= width - (int64)r.x && (int64)r.h <= height - (int64)r.y;
}

bool ValidateWindowClass(const WindowClass& wc, String& error)
{
	if(!JsonFileStore::ValidId(wc.id)) {
		error = "invalid window class id: " + wc.id;
		return false;
	}
	if(wc.name.IsEmpty() || wc.name.GetCount() > 200) {
		error = "window class name is required (at most 200 bytes)";
		return false;
	}
	if(wc.width < 1 || wc.height < 1 || wc.width > MaxSide || wc.height > MaxSide) {
		error = "window class size must be between 1 and 100000 pixels per side";
		return false;
	}
	if(wc.anchors.GetCount() != 3) {
		error = "a window class has exactly three anchors";
		return false;
	}
	for(AnnPoint p : wc.anchors)
		if(!InsideWindow(wc, p)) {
			error = "an anchor lies outside the window class";
			return false;
		}
	if(Cross(wc.anchors[0], wc.anchors[1], wc.anchors[2]) == 0) {
		error = "the three anchors of a window class must not lie on one line";
		return false;
	}
	return true;
}

bool SnapToAnchor(const WindowClass& wc, AnnPoint p, int max_distance, AnnPoint& snapped, int& anchor_index)
{
	if(wc.anchors.GetCount() != 3 || max_distance < 0)
		return false;
	int best = 0;
	int64 best_d2 = -1;
	for(int i = 0; i < 3; i++) {
		int64 dx = wc.anchors[i].x - p.x, dy = wc.anchors[i].y - p.y;
		int64 d2 = dx * dx + dy * dy;
		if(best_d2 < 0 || d2 < best_d2) {
			best_d2 = d2;
			best = i;
		}
	}
	if(max_distance > 0 && best_d2 > (int64)max_distance * max_distance) {
		snapped = p;
		anchor_index = -1;
		return false;
	}
	snapped = wc.anchors[best];
	anchor_index = best;
	return true;
}

bool RescaleRect(const WindowClass& from, const WindowClass& to, const AnnRect& in, AnnRect& out, String& error)
{
	String e;
	if(!ValidateWindowClass(from, e) || !ValidateWindowClass(to, e)) {
		error = e;
		return false;
	}
	if(in.w <= 0 || in.h <= 0) {
		error = "rectangle must have a positive size";
		return false;
	}
	// Affine map from the source anchors to the target anchors, anchored at the first anchor.
	double pax = from.anchors[1].x - from.anchors[0].x, pay = from.anchors[1].y - from.anchors[0].y;
	double pbx = from.anchors[2].x - from.anchors[0].x, pby = from.anchors[2].y - from.anchors[0].y;
	double det = pax * pby - pbx * pay;
	double qax = to.anchors[1].x - to.anchors[0].x, qay = to.anchors[1].y - to.anchors[0].y;
	double qbx = to.anchors[2].x - to.anchors[0].x, qby = to.anchors[2].y - to.anchors[0].y;

	double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
	for(int c = 0; c < 4; c++) {
		double px = in.x + ((c & 1) ? in.w : 0);
		double py = in.y + ((c & 2) ? in.h : 0);
		double dx = px - from.anchors[0].x, dy = py - from.anchors[0].y;
		double u = (pby * dx - pbx * dy) / det;
		double v = (-pay * dx + pax * dy) / det;
		double qx = to.anchors[0].x + u * qax + v * qbx;
		double qy = to.anchors[0].y + u * qay + v * qby;
		minx = min(minx, qx); maxx = max(maxx, qx);
		miny = min(miny, qy); maxy = max(maxy, qy);
	}
	out.x = RoundInt(minx);
	out.y = RoundInt(miny);
	out.w = RoundInt(maxx) - out.x;
	out.h = RoundInt(maxy) - out.y;
	if(out.w <= 0 || out.h <= 0) {
		error = "rescaled rectangle collapsed";
		return false;
	}
	return true;
}

bool ExtrapolateSlots(AnnPoint centre, AnnPoint seed, double step_degrees, int count,
                      Vector<AnnPoint>& slots, String& error)
{
	slots.Clear();
	if(count < 1 || count > MaxSlots) {
		error = "slot count must be between 1 and 64";
		return false;
	}
	double dx = seed.x - centre.x, dy = seed.y - centre.y;
	double radius = sqrt(dx * dx + dy * dy);
	if(radius <= 0) {
		error = "the seed slot must differ from the centre";
		return false;
	}
	double base = atan2(dy, dx);
	for(int k = 0; k < count; k++) {
		double a = base + k * step_degrees * Pi / 180.0;
		slots.Add(AnnPoint(RoundInt(centre.x + radius * cos(a)), RoundInt(centre.y + radius * sin(a))));
	}
	return true;
}

AnnRect CentredRect(AnnPoint centre, int w, int h)
{
	return AnnRect(centre.x - w / 2, centre.y - h / 2, w, h);
}

static int BytesPerPixel(const String& content_type)
{
	if(content_type == "application/x-raw-gray8")
		return 1;
	if(content_type == "application/x-raw-rgb8")
		return 3;
	return 0;
}

bool ExportCrops(const String& frame_id, const String& raw, int width, int height,
                 const String& content_type, const Vector<Region>& regions,
                 Vector<CropResult>& crops, String& manifest_json, String& error)
{
	crops.Clear();
	int bpp = BytesPerPixel(content_type);
	if(!bpp) {
		error = "unsupported frame content type: " + content_type;
		return false;
	}
	if(width < 1 || height < 1 || (int64)raw.GetCount() != (int64)width * height * bpp) {
		error = "frame bytes do not match its size";
		return false;
	}
	for(const Region& rg : regions) {
		const AnnRect& r = rg.rect;
		if(!RectInside(r, width, height)) {
			error = "region " + rg.id + " lies outside the frame";
			return false;
		}
		CropResult c;
		c.region = rg.id;
		c.rect = r;
		c.label = rg.label;
		for(int row = 0; row < r.h; row++) {
			int offset = ((r.y + row) * width + r.x) * bpp;
			c.bytes << raw.Mid(offset, r.w * bpp);
		}
		c.byte_size = c.bytes.GetCount();
		c.checksum = BlobStore::Checksum(c.bytes);
		crops.Add(pick(c));
	}

	ValueArray rows;
	for(const CropResult& c : crops) {
		ValueMap rect;
		rect.Add("x", c.rect.x);
		rect.Add("y", c.rect.y);
		rect.Add("w", c.rect.w);
		rect.Add("h", c.rect.h);
		ValueMap row;
		row.Add("frame_id", frame_id);
		row.Add("region", c.region);
		row.Add("rect", rect);
		row.Add("label", c.label);
		row.Add("checksum", c.checksum);
		row.Add("byte_size", (int)c.byte_size);
		rows.Add(row);
	}
	ValueMap manifest;
	manifest.Add("frame_id", frame_id);
	manifest.Add("frame_checksum", BlobStore::Checksum(raw));
	manifest.Add("content_type", content_type);
	manifest.Add("crops", rows);
	manifest_json = AsJSON(manifest, true);
	return true;
}

// ---------------------------------------------------------------------------
// AnnotateStore
// ---------------------------------------------------------------------------

AnnotateStore::AnnotateStore(const String& site_id, const String& data_root)
	: windows(site_id, data_root, "window-classes"), frames(site_id, data_root, "annotate-frames"),
	  regions(site_id, data_root, "annotate-regions")
{
}

StoreResult AnnotateStore::PutWindowClass(const WindowClass& wc, const String& expected_revision)
{
	String error;
	if(!ValidateWindowClass(wc, error))
		return error;
	if(expected_revision.IsEmpty())
		return "revision required: use the current revision, or RecordAbsent for a new window class";
	String revision;
	return windows.Put(wc.id, StoreAsJson(wc), expected_revision, revision);
}

StoreResult AnnotateStore::GetWindowClass(const String& id, WindowClass& out)
{
	String json, revision;
	StoreResult r = windows.Get(id, json, revision);
	if(!r)
		return r;
	try {
		LoadFromJson(out, json);
	}
	catch(Exc& e) {
		return "window class record is corrupt: " + e;
	}
	out.revision = revision;
	return true;
}

StoreResult AnnotateStore::ListWindowClasses(Vector<WindowClass>& out)
{
	out.Clear();
	Vector<String> ids;
	StoreResult listed = windows.List(ids);
	if(!listed)
		return listed;
	for(const String& id : ids) {
		WindowClass wc;
		if(GetWindowClass(id, wc))
			out.Add(pick(wc));
	}
	return true;
}

StoreResult AnnotateStore::PutFrame(const Frame& f, const String& expected_revision)
{
	if(!JsonFileStore::ValidId(f.id))
		return "invalid frame id: " + f.id;
	WindowClass wc;
	if(!GetWindowClass(f.window_class, wc))
		return "unknown window class '" + f.window_class + "'";
	if(f.image_blob.GetCount() && !JsonFileStore::ValidId(f.image_blob))
		return "invalid image blob id: " + f.image_blob;
	if(f.content_type.GetCount() && !BytesPerPixel(f.content_type))
		return "unsupported frame content type: " + f.content_type;
	if(f.source_ref.GetCount() > 500)
		return "source reference is too long (at most 500 bytes)";
	Vector<String> seen;
	for(const FrameLayer& l : f.layers) {
		if(!JsonFileStore::ValidId(l.id) || l.name.IsEmpty() || FindIndex(seen, l.id) >= 0)
			return "layers need unique safe ids and names";
		seen.Add(l.id);
	}
	if(expected_revision.IsEmpty())
		return "revision required: use the current revision, or RecordAbsent for a new frame";
	String revision;
	return frames.Put(f.id, StoreAsJson(f), expected_revision, revision);
}

StoreResult AnnotateStore::GetFrame(const String& id, Frame& out)
{
	String json, revision;
	StoreResult r = frames.Get(id, json, revision);
	if(!r)
		return r;
	try {
		LoadFromJson(out, json);
	}
	catch(Exc& e) {
		return "frame record is corrupt: " + e;
	}
	out.revision = revision;
	return true;
}

StoreResult AnnotateStore::ListFrames(Vector<Frame>& out)
{
	out.Clear();
	Vector<String> ids;
	StoreResult listed = frames.List(ids);
	if(!listed)
		return listed;
	for(const String& id : ids) {
		Frame f;
		if(GetFrame(id, f))
			out.Add(pick(f));
	}
	return true;
}

StoreResult AnnotateStore::PutRegion(const Region& rg, const String& expected_revision)
{
	if(!JsonFileStore::ValidId(rg.id))
		return "invalid region id: " + rg.id;
	Frame f;
	if(!GetFrame(rg.frame, f))
		return "unknown frame '" + rg.frame + "'";
	bool layer_ok = false;
	for(const FrameLayer& l : f.layers)
		layer_ok = layer_ok || l.id == rg.layer;
	if(!layer_ok)
		return "layer '" + rg.layer + "' is not a layer of frame " + rg.frame;
	WindowClass wc;
	if(!GetWindowClass(f.window_class, wc))
		return "frame " + rg.frame + " has an unknown window class";
	const AnnRect& r = rg.rect;
	if(!RectInside(r, wc.width, wc.height))
		return "region " + rg.id + " lies outside the window class";
	if(rg.label.IsEmpty() || rg.label.GetCount() > 200)
		return "region label is required (at most 200 bytes)";
	if(expected_revision.IsEmpty())
		return "revision required: use the current revision, or RecordAbsent for a new region";
	String revision;
	return regions.Put(rg.id, StoreAsJson(rg), expected_revision, revision);
}

StoreResult AnnotateStore::GetRegion(const String& id, Region& out)
{
	String json, revision;
	StoreResult r = regions.Get(id, json, revision);
	if(!r)
		return r;
	try {
		LoadFromJson(out, json);
	}
	catch(Exc& e) {
		return "region record is corrupt: " + e;
	}
	out.revision = revision;
	return true;
}

StoreResult AnnotateStore::ListRegions(const String& frame_id, Vector<Region>& out)
{
	out.Clear();
	Vector<String> ids;
	StoreResult listed = regions.List(ids);
	if(!listed)
		return listed;
	for(const String& id : ids) {
		Region rg;
		if(GetRegion(id, rg) && rg.frame == frame_id)
			out.Add(pick(rg));
	}
	return true;
}

END_UPP_NAMESPACE
