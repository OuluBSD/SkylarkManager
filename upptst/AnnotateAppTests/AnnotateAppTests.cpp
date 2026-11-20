#include <AnnotateApp/AnnotateApp.h>
#include <WebAppHost/WebAppHost.h>

using namespace Upp;

static int checks = 0;
static int failures = 0;

static bool Check(bool cond, const String& what)
{
	checks++;
	if(cond)
		Cout() << "ok    " << what << "\n";
	else {
		failures++;
		Cout() << "FAILED: " << what << "\n";
	}
	return cond;
}

static WindowClass Window(const String& id, int w, int h, AnnPoint a, AnnPoint b, AnnPoint c)
{
	WindowClass wc;
	wc.id = id;
	wc.name = "Window " + id;
	wc.width = w;
	wc.height = h;
	wc.anchors.Add(a);
	wc.anchors.Add(b);
	wc.anchors.Add(c);
	return wc;
}

static bool SameRect(const AnnRect& r, int x, int y, int w, int h)
{
	return r.x == x && r.y == y && r.w == w && r.h == h;
}

CONSOLE_APP_MAIN
{
	// Window classes: a 200x100 source with anchors at its corners, and targets for the rescale tests.
	WindowClass src = Window("src", 100, 100, AnnPoint(0, 0), AnnPoint(100, 0), AnnPoint(0, 100));
	WindowClass dbl = Window("dbl", 200, 100, AnnPoint(0, 0), AnnPoint(200, 0), AnnPoint(0, 50));
	WindowClass moved = Window("moved", 300, 200, AnnPoint(5, 5), AnnPoint(205, 5), AnnPoint(5, 55));
	WindowClass line = Window("line", 100, 100, AnnPoint(0, 0), AnnPoint(50, 50), AnnPoint(90, 90));
	WindowClass off = Window("off", 100, 100, AnnPoint(0, 0), AnnPoint(120, 0), AnnPoint(0, 10));
	String error;

	Check(ValidateWindowClass(src, error), "window class: three anchors inside the window are valid");
	Check(!ValidateWindowClass(line, error) && error.Find("one line") >= 0,
	      "window class: three anchors on one line are refused");
	Check(!ValidateWindowClass(off, error) && error.Find("outside") >= 0,
	      "window class: an anchor outside the window is refused");

	// Snapping: the nearest anchor, with a distance limit.
	AnnPoint snapped;
	int index = -1;
	Check(SnapToAnchor(dbl, AnnPoint(190, 8), 0, snapped, index) && snapped.x == 200 && snapped.y == 0 && index == 1,
	      "snap: (190,8) snaps to anchor 1 at (200,0), the nearest");
	Check(SnapToAnchor(src, AnnPoint(3, 96), 0, snapped, index) && snapped.x == 0 && snapped.y == 100 && index == 2,
	      "snap: (3,96) snaps to anchor 2 at (0,100)");
	Check(!SnapToAnchor(src, AnnPoint(50, 50), 10, snapped, index) && snapped.x == 50 && snapped.y == 50 && index == -1,
	      "snap: a point farther than the limit is left where it is");
	Check(SnapToAnchor(src, AnnPoint(6, 8), 10, snapped, index) && snapped.x == 0 && snapped.y == 0 && index == 0,
	      "snap: a point within the limit (distance 10) snaps to anchor 0");

	// Rescale: the affine map from the source anchors to the target anchors.
	AnnRect out;
	Check(RescaleRect(src, dbl, AnnRect(10, 10, 20, 20), out, error) && SameRect(out, 20, 5, 40, 10),
	      "rescale: (10,10,20,20) from 100x100 to 200x100 with y scaled by one half is (20,5,40,10)");
	Check(RescaleRect(src, moved, AnnRect(10, 10, 20, 20), out, error) && SameRect(out, 25, 10, 40, 10),
	      "rescale: the same rectangle moved by the anchor offset (5,5) and scaled by two and one half is (25,10,40,10)");
	Check(RescaleRect(dbl, src, AnnRect(20, 5, 40, 10), out, error) && SameRect(out, 10, 10, 20, 20),
	      "rescale: the mapping back returns the original rectangle (10,10,20,20)");
	Check(!RescaleRect(src, line, AnnRect(0, 0, 10, 10), out, error) && error.Find("one line") >= 0,
	      "rescale: a target window class with collinear anchors is refused");
	Check(!RescaleRect(src, dbl, AnnRect(0, 0, 0, 10), out, error), "rescale: an empty rectangle is refused");

	// Radial slot extrapolation around a centre, one step per slot.
	Vector<AnnPoint> slots;
	Check(ExtrapolateSlots(AnnPoint(100, 100), AnnPoint(200, 100), 90, 4, slots, error)
	      && slots.GetCount() == 4 && slots[0].x == 200 && slots[0].y == 100
	      && slots[1].x == 100 && slots[1].y == 200 && slots[2].x == 0 && slots[2].y == 100
	      && slots[3].x == 100 && slots[3].y == 0,
	      "extrapolate: four slots at 90 degree steps from (200,100) around (100,100) are the four compass points");
	Check(ExtrapolateSlots(AnnPoint(0, 0), AnnPoint(10, 0), 60, 6, slots, error) && slots.GetCount() == 6
	      && slots[1].x == 5 && slots[1].y == 9 && slots[3].x == -10 && slots[3].y == 0,
	      "extrapolate: six slots at 60 degree steps on radius 10 give (5,9) and (-10,0) at steps 1 and 3");
	Check(!ExtrapolateSlots(AnnPoint(5, 5), AnnPoint(5, 5), 30, 3, slots, error),
	      "extrapolate: a seed at the centre is refused");
	Check(!ExtrapolateSlots(AnnPoint(5, 5), AnnPoint(9, 5), 30, 0, slots, error),
	      "extrapolate: a count of zero is refused");
	AnnRect card = CentredRect(AnnPoint(100, 200), 20, 10);
	Check(SameRect(card, 90, 195, 20, 10), "extrapolate: a slot rectangle is centred on its point");

	// Crop export: a 4x4 gray frame whose pixel value is y*4+x.
	String raw;
	for(int y = 0; y < 4; y++)
		for(int x = 0; x < 4; x++)
			raw.Cat((char)(y * 4 + x));
	Vector<Region> regions;
	Region rg;
	rg.id = "r1"; rg.frame = "f1"; rg.layer = "base"; rg.rect = AnnRect(1, 1, 2, 2); rg.label = "marker";
	regions.Add(rg);
	rg.id = "r2"; rg.rect = AnnRect(0, 0, 1, 1); rg.label = "corner";
	regions.Add(rg);
	Vector<CropResult> crops;
	String manifest;
	Vector<FrameLayer> no_layers;
	Check(ExportCrops("f1", raw, 4, 4, "application/x-raw-gray8", no_layers, regions, crops, manifest, error)
	      && crops.GetCount() == 2 && crops[0].bytes == String("\x05\x06\x09\x0a", 4) && crops[0].byte_size == 4,
	      "export: the 2x2 crop at (1,1) holds the pixels 5, 6, 9, 10 in row order");
	Check(crops[1].bytes == String("\x00", 1) && crops[1].checksum == BlobStore::Checksum(String("\x00", 1)),
	      "export: a 1x1 crop at (0,0) holds pixel 0 and its checksum is the SHA-256 of those bytes");
	ValueMap parsed = ParseJSON(manifest);
	ValueArray rows = parsed["crops"];
	Check(parsed["frame_id"] == "f1" && rows.GetCount() == 2 && String(rows[0]["label"]) == "marker"
	      && String(rows[0]["checksum"]) == crops[0].checksum && (int)rows[0]["byte_size"] == 4
	      && (int)rows[0]["rect"]["w"] == 2 && String(parsed["frame_checksum"]) == BlobStore::Checksum(raw),
	      "export: the manifest lists the frame, each rectangle, label, checksum and byte size");

	regions.Clear();
	rg.id = "bad"; rg.rect = AnnRect(3, 3, 2, 2); rg.label = "outside";
	regions.Add(rg);
	Check(!ExportCrops("f1", raw, 4, 4, "application/x-raw-gray8", no_layers, regions, crops, manifest, error)
	      && error.Find("outside the frame") >= 0,
	      "export: a rectangle past the frame edge is refused");
	// A large width or height must not wrap x + w (or y + h) negative and pass: the bounds test is a subtraction.
	regions.Clear();
	rg.id = "wide"; rg.rect = AnnRect(1, 0, 2147483647, 1); rg.label = "wide";
	regions.Add(rg);
	Check(!ExportCrops("f1", raw, 4, 4, "application/x-raw-gray8", no_layers, regions, crops, manifest, error)
	      && error.Find("outside the frame") >= 0,
	      "export: a region at the far edge with a width of INT_MAX is refused, not wrapped into range");
	regions.Clear();
	rg.id = "tall"; rg.rect = AnnRect(0, 1, 1, 2147483647); rg.label = "tall";
	regions.Add(rg);
	Check(!ExportCrops("f1", raw, 4, 4, "application/x-raw-gray8", no_layers, regions, crops, manifest, error)
	      && error.Find("outside the frame") >= 0,
	      "export: a region with a height of INT_MAX below the top edge is refused, not wrapped into range");
	Check(!ExportCrops("f1", raw, 4, 4, "image/png", no_layers, regions, crops, manifest, error)
	      && !ExportCrops("f1", raw.Left(15), 4, 4, "application/x-raw-gray8", no_layers, regions, crops, manifest, error),
	      "export: an unsupported content type and a frame of the wrong size are refused");

	// RGB frames use three bytes per pixel.
	String rgb;
	for(int i = 0; i < 2 * 2 * 3; i++)
		rgb.Cat((char)(i + 1));
	regions.Clear();
	rg.id = "p"; rg.rect = AnnRect(1, 0, 1, 2); rg.label = "column";
	regions.Add(rg);
	Check(ExportCrops("rgb", rgb, 2, 2, "application/x-raw-rgb8", no_layers, regions, crops, manifest, error)
	      && crops[0].bytes == String("\x04\x05\x06\x0a\x0b\x0c", 6),
	      "export: an rgb column crop takes three bytes per pixel");

	// A hidden layer is switched off for the frame: its regions are neither cropped nor listed in the manifest.
	Vector<FrameLayer> layers;
	FrameLayer shown_layer;
	shown_layer.id = "base"; shown_layer.name = "Base";
	FrameLayer hidden_layer;
	hidden_layer.id = "ghost"; hidden_layer.name = "Ghost"; hidden_layer.visible = false;
	layers.Add(shown_layer);
	layers.Add(hidden_layer);
	regions.Clear();
	Region on_shown;
	on_shown.id = "shown"; on_shown.frame = "f1"; on_shown.layer = "base"; on_shown.rect = AnnRect(0, 0, 1, 1); on_shown.label = "visible";
	regions.Add(on_shown);
	Region on_hidden;
	on_hidden.id = "secret"; on_hidden.frame = "f1"; on_hidden.layer = "ghost"; on_hidden.rect = AnnRect(2, 2, 1, 1); on_hidden.label = "hidden";
	regions.Add(on_hidden);
	Check(ExportCrops("f1", raw, 4, 4, "application/x-raw-gray8", layers, regions, crops, manifest, error)
	      && crops.GetCount() == 1 && crops[0].region == "shown" && manifest.Find("secret") < 0
	      && manifest.Find("visible") >= 0,
	      "export: a region on a hidden layer is not exported and not listed in the manifest");
	Check(ExportCrops("f1", raw, 4, 4, "application/x-raw-gray8", no_layers, regions, crops, manifest, error)
	      && crops.GetCount() == 2,
	      "export: with no layer list every region is exported, as before");

	// Store: frames and regions are checked against their window class and layers.
	String root = AppendFileName(GetCurrentDirectory(), "annotate-selftest-data");
	DeleteFolderDeep(root);
	RealizeDirectory(root);
	AnnotateStore store("selftest", root);
	Check(store.PutWindowClass(src, RecordAbsent()) && store.PutWindowClass(dbl, RecordAbsent()),
	      "store: window classes are stored");
	Check(!store.PutWindowClass(line, RecordAbsent()) && !store.PutWindowClass(src, "9"),
	      "store: an invalid window class and a stale revision are refused");

	Frame f;
	f.id = "f1";
	f.window_class = "src";
	FrameLayer base; base.id = "base"; base.name = "Base";
	f.layers.Add(base);
	Check((bool)store.PutFrame(f, RecordAbsent()), "store: a frame with a known window class is stored");
	Frame g;
	g.id = "f2";
	g.window_class = "nowhere";
	Check(!store.PutFrame(g, RecordAbsent()), "store: a frame with an unknown window class is refused");

	Region r1;
	r1.id = "a1"; r1.frame = "f1"; r1.layer = "base"; r1.rect = AnnRect(10, 10, 20, 20); r1.label = "box";
	r1.slot_group = "row";
	Check((bool)store.PutRegion(r1, RecordAbsent()), "store: a region inside the window class is stored");
	Region r2 = r1;
	r2.id = "a2"; r2.layer = "missing";
	Check(!store.PutRegion(r2, RecordAbsent()), "store: a region on a layer that the frame does not have is refused");
	Region r3 = r1;
	r3.id = "a3"; r3.rect = AnnRect(90, 90, 20, 20);
	Check(!store.PutRegion(r3, RecordAbsent()), "store: a region past the window edge is refused");
	Region r3w = r1;
	r3w.id = "a3w"; r3w.rect = AnnRect(1, 10, 2147483647, 20);
	Check(!store.PutRegion(r3w, RecordAbsent()) && !store.GetRegion("a3w", r3w),
	      "store: a region with a width of INT_MAX at the far edge is refused, not wrapped into range");
	Region r4 = r1;
	r4.id = "a4"; r4.label = "";
	Check(!store.PutRegion(r4, RecordAbsent()), "store: a region without a label is refused");

	Vector<Region> listed;
	Check(store.ListRegions("f1", listed) && listed.GetCount() == 1 && listed[0].rect.w == 20,
	      "store: the regions of a frame are listed");

	// Dependents: region a1 (10,10,20,20) sits on layer "base" of frame f1, whose window class is "src".
	WindowClass src_now;
	Check(store.GetWindowClass("src", src_now) && src_now.width == 100, "dependents: the current window class is read back");
	WindowClass shrunk = Window("src", 25, 25, AnnPoint(0, 0), AnnPoint(25, 0), AnnPoint(0, 25));
	StoreResult shrink = store.PutWindowClass(shrunk, src_now.revision);
	Check(!shrink && shrink.error.Find("cannot be resized to 25x25") >= 0 && shrink.error.Find("'a1'") >= 0,
	      "dependents: a window class cannot shrink under a region that is still on it, and the error names it");
	WindowClass fits = Window("src", 35, 35, AnnPoint(0, 0), AnnPoint(35, 0), AnnPoint(0, 35));
	Check((bool)store.PutWindowClass(fits, src_now.revision),
	      "dependents: a window class may shrink to a size that all its regions still fit");

	// Frames are moveable, not copied: each edit reads the frame into a fresh object with its revision.
	Frame no_layer;
	Check((bool)store.GetFrame("f1", no_layer), "dependents: the current frame is read back");
	no_layer.layers.Clear();
	StoreResult drop = store.PutFrame(no_layer, no_layer.revision);
	Check(!drop && drop.error.Find("layer 'base'") >= 0 && drop.error.Find("cannot be removed") >= 0
	      && drop.error.Find("'a1'") >= 0,
	      "dependents: a frame cannot drop a layer that a region is on, and the error names the layer and the region");

	Frame more;
	Check((bool)store.GetFrame("f1", more), "dependents: the frame is read back before adding a layer");
	FrameLayer extra;
	extra.id = "extra"; extra.name = "Extra";
	more.layers.Add(extra);
	Check((bool)store.PutFrame(more, more.revision), "dependents: a frame may add a layer");

	Frame less;
	Check((bool)store.GetFrame("f1", less), "dependents: the frame with the added layer is read back");
	less.layers.Clear();
	less.layers.Add(base);
	Check((bool)store.PutFrame(less, less.revision), "dependents: a frame may drop a layer that no region is on");

	WebApp *app = CreateWebapp("AnnotateApp");
	Check(app && app->Id() == "AnnotateApp" && app->DefaultPort() == 18430,
	      "webapp: AnnotateApp is registered from its .icpp and creates its app");
	delete app;

	DeleteFolderDeep(root);
	Cout() << "\nchecks: " << checks << ", failures: " << failures << "\n";
	SetExitCode(failures ? 1 : 0);
}
