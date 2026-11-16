#include "AtlasSite.h"

NAMESPACE_UPP

// A demo roadmap shipped with the server: a factory line inspected through an XR headset camera.
// Versions 0.0.1 to 1.0.0 are built from events, so the history is what the page and the checker
// replay. The demo is written once, when the family is missing; an existing family is never touched.

static const char *DEMO_FAMILY = "xr-inspect";

static AtlasElement El(const char *id, const char *kind, const char *name, const char *status,
                       int x, int y, const char *notes)
{
	AtlasElement e;
	e.id = id;
	e.kind = kind;
	e.name = name;
	e.status = status;
	e.x = x;
	e.y = y;
	e.w = 200;
	e.h = 110;
	e.notes = notes;
	return pick(e);
}

static AtlasRoad Rd(const char *id, const char *from, const char *to, const char *kind, const char *label)
{
	AtlasRoad r;
	r.id = id;
	r.from = from;
	r.to = to;
	r.kind = kind;
	r.label = label;
	return pick(r);
}

// Events are numbered in the order they are written; the time of day follows that number, so within a
// day the timestamps sort the way the events were written.
static int demo_n = 0;

static AtlasEvent Ev(const char *vid, int month, int day, const char *actor, const char *kind, const char *subject,
                     const char *type)
{
	AtlasEvent e;
	demo_n++;
	e.id = Format("ev-xr-%03d", demo_n);
	e.at = Format("2026-%02d-%02d", month, day) + "T" + Format("%02d:%02d", (demo_n / 60) % 24, demo_n % 60) + ":00Z";
	e.actor = actor;
	e.vid = vid;
	e.subject_kind = kind;
	e.subject_id = subject;
	e.type = type;
	return pick(e);
}

static AtlasEvent Created(const char *vid, int m, int d, const char *id, const char *name)
{
	AtlasEvent e = Ev(vid, m, d, "demo", "element", id, "created");
	e.name = name;
	return pick(e);
}

static AtlasEvent Connected(const char *vid, int m, int d, const char *road, const char *from, const char *to,
                            const char *road_kind, const char *label)
{
	AtlasEvent e = Ev(vid, m, d, "demo", "road", road, "connected");
	e.from = from;
	e.to = to;
	e.road_kind = road_kind;
	e.label = label;
	return pick(e);
}

static AtlasEvent Stage(const char *vid, int m, int d, const char *element, const char *stage)
{
	AtlasEvent e = Ev(vid, m, d, "demo", "element", element, "stage");
	e.stage = stage;
	return pick(e);
}

static AtlasEvent Note(const char *vid, int m, int d, const char *element, const char *text)
{
	AtlasEvent e = Ev(vid, m, d, "demo", "element", element, "note");
	e.text = text;
	return pick(e);
}

static AtlasEvent Act(const char *vid, int m, int d, const char *element, const char *action, const char *text)
{
	AtlasEvent e = Note(vid, m, d, element, text);
	e.action = action;
	return pick(e);
}

static AtlasEvent Propose(const char *vid, int m, int d, const char *id, const char *title, const char *target,
                          const char *link, const char *text)
{
	AtlasEvent e = Ev(vid, m, d, "demo", "task", id, "proposed");
	e.name = title;
	e.target = target;
	e.link = link;
	e.text = text;
	return pick(e);
}

static AtlasEvent TaskStep(const char *vid, int m, int d, const char *id, const char *type)
{
	return Ev(vid, m, d, "demo", "task", id, type);
}

static void DemoRules(AtlasRules& r)
{
	const char *atoms[][3] = {
		{ "line_running", "the production line runs during inspection", "0" },
		{ "gate_camera_calibrated", "the headset camera is calibrated to the floor markers", "1" },
		{ "gate_detector_trained", "the defect detector is trained on the labelled set", "1" },
		{ "gate_field_trial_passed", "a two-shift trial on line 3 passed without a missed defect", "1" },
	};
	for(const auto& a : atoms) {
		AtlasAtomDef d;
		d.name = a[0];
		d.text = a[1];
		d.gate = a[2][0] == '1';
		r.atoms.Add(pick(d));
	}
	r.initial.Add("line_running");

	AtlasAction calibrate;
	calibrate.name = "calibrate_camera";
	calibrate.text = "Calibrate against the floor markers and record the reprojection error";
	calibrate.effects.Add("gate_camera_calibrated");
	r.actions.Add(pick(calibrate));

	AtlasAction train;
	train.name = "train_detector";
	train.text = "Train the defect detector on the labelled frames";
	train.requires.Add("task:t_dataset:done");
	train.effects.Add("gate_detector_trained");
	r.actions.Add(pick(train));

	AtlasAction trial;
	trial.name = "run_field_trial";
	trial.text = "Run the two-shift trial on line 3";
	trial.requires.Add("gate_detector_trained");
	trial.requires.Add("line_running");
	trial.effects.Add("gate_field_trial_passed");
	r.actions.Add(pick(trial));

	AtlasConstraint overlay;
	overlay.id = "overlay_needs_calibration";
	overlay.text = "The live overlay is only drawn on a calibrated camera";
	overlay.when.Add("stage:overlay:implementation");
	overlay.must.Add("gate_camera_calibrated");
	overlay.from_vid = "v4";
	r.constraints.Add(pick(overlay));
}

// The version list. Codes: major*1000000 + minor*1000 + patch.
static void DemoVersions(AtlasFamily& f)
{
	f.head_key = "xr-inspect";
	const char *vids[] = { "v1", "v2", "v3", "v4", "v5", "v6" };
	int codes[] = { 1, 2, 3, 4, 1000, 1000000 };
	for(int i = 0; i < 6; i++) {
		AtlasVersion v;
		v.vid = vids[i];
		v.code = codes[i];
		f.versions.Add(pick(v));
	}
	// A version's contract: what must hold on entry to it.
	f.versions[1].requires.Add("gate_camera_calibrated");
	f.versions[2].requires.Add("gate_detector_trained");
	f.versions[5].requires.Add("gate_field_trial_passed");
}

// The head map: the elements and roads as they stand at 1.0.0.
static void DemoHead(AtlasMap& m)
{
	m.map = DEMO_FAMILY;
	m.version = "v1.0.0";
	m.title = "XR factory inspection";
	m.world.width = 1600;
	m.world.height = 640;
	m.elements.Add(El("cam", "place", "XR headset camera", "active", 60, 120, "Streams 1920x1080 at 30 fps over Wi-Fi 6."));
	m.elements.Add(El("ingest", "block", "Frame ingest", "active", 340, 120, "Decodes the stream and drops frames when the detector is behind."));
	m.elements.Add(El("detect", "block", "Defect detector", "hot", 620, 40, "Bounding boxes and a defect class per box."));
	m.elements.Add(El("rules", "block", "Inspection rules", "planned", 620, 280, "Turns detections into pass or fail verdicts per part."));
	m.elements.Add(El("overlay", "block", "XR overlay", "not-ready", 900, 40, "Draws the boxes on the headset view, aligned to the floor markers."));
	m.elements.Add(El("log", "block", "Defect log", "planned", 900, 280, "Every verdict with its frame, part number and time."));
	m.elements.Add(El("plc", "place", "Line PLC bridge", "future", 340, 380, "Reads line state so inspection pauses when the line stops."));
	m.elements.Add(El("dash", "block", "Supervisor dashboard", "planned", 1180, 160, "Live view and the defect report for the shift lead."));
	m.elements.Add(El("line3", "place", "Line 3 field trial", "future", 1180, 380, "The place the 1.0.0 release is trialled."));

	m.roads.Add(Rd("r_video", "cam", "ingest", "communication", "video frames"));
	m.roads.Add(Rd("r_frames", "ingest", "detect", "progress", "decoded frames"));
	m.roads.Add(Rd("r_hits", "detect", "rules", "communication", "defect hits"));
	m.roads.Add(Rd("r_boxes", "detect", "overlay", "communication", "boxes"));
	m.roads.Add(Rd("r_verdicts", "rules", "log", "communication", "verdicts"));
	m.roads.Add(Rd("r_live", "overlay", "dash", "communication", "live view"));
	m.roads.Add(Rd("r_reports", "log", "dash", "communication", "reports"));
	m.roads.Add(Rd("r_line", "plc", "rules", "dependency", "line state"));
	m.roads.Add(Rd("r_deploy", "dash", "line3", "progress", "deploy"));
}

// The history: what each version added, moved and decided, in the order it happened.
static void DemoEvents(Vector<AtlasEvent>& ev)
{
	// 0.0.1: the headset streams into ingest; calibration is the first gate.
	ev.Add(Created("v1", 9, 1, "cam", "XR headset camera"));
	ev.Add(Created("v1", 9, 1, "ingest", "Frame ingest"));
	ev.Add(Connected("v1", 9, 1, "r_video", "cam", "ingest", "communication", "video frames"));
	ev.Add(Stage("v1", 9, 2, "cam", "planned"));
	ev.Add(Stage("v1", 9, 2, "cam", "implementation_research"));
	ev.Add(Stage("v1", 9, 3, "cam", "implementation"));
	ev.Add(Note("v1", 9, 3, "cam", "Stream works at 30 fps. Frame timestamps drift by 40 ms over a shift; fix in calibration."));
	ev.Add(Propose("v1", 9, 4, "t_calib", "Calibrate the headset camera on the floor markers", "v2", "element:cam",
	               "Needed before the overlay can be aligned to parts."));
	ev.Add(Act("v1", 9, 5, "cam", "calibrate_camera", "Calibrated: reprojection error 0.4 px after 24 frames."));
	ev.Add(TaskStep("v1", 9, 5, "t_calib", "accepted"));
	ev.Add(TaskStep("v1", 9, 5, "t_calib", "done"));
	ev.Add(Stage("v1", 9, 6, "ingest", "planned"));
	ev.Add(Stage("v1", 9, 6, "ingest", "implementation_research"));

	// 0.0.2: the detector gets its data and is trained; ingest feeds it.
	ev.Add(Created("v2", 9, 15, "detect", "Defect detector"));
	ev.Add(Connected("v2", 9, 15, "r_frames", "ingest", "detect", "progress", "decoded frames"));
	ev.Add(Stage("v2", 9, 15, "detect", "planned"));
	ev.Add(Stage("v2", 9, 16, "detect", "implementation_research"));
	ev.Add(Stage("v2", 9, 16, "detect", "implementation"));
	ev.Add(Stage("v2", 9, 17, "ingest", "implementation"));
	ev.Add(Propose("v2", 9, 17, "t_dataset", "Label 2000 frames of defects by hand", "v3", "element:detect",
	               "The detector cannot be trained without it."));
	ev.Add(TaskStep("v2", 9, 22, "t_dataset", "accepted"));
	ev.Add(TaskStep("v2", 9, 28, "t_dataset", "done"));
	ev.Add(Act("v2", 9, 29, "detect", "train_detector", "Trained: 0.91 mAP on the held-out set of 400 frames."));

	// 0.0.3: the detector is tested on the line; rules are written from its output.
	ev.Add(Stage("v3", 10, 1, "detect", "trials"));
	ev.Add(Stage("v3", 10, 1, "ingest", "trials"));
	ev.Add(Stage("v3", 10, 8, "detect", "merge_to_master"));
	ev.Add(Stage("v3", 10, 8, "ingest", "merge_to_master"));
	ev.Add(Created("v3", 10, 9, "rules", "Inspection rules"));
	ev.Add(Connected("v3", 10, 9, "r_hits", "detect", "rules", "communication", "defect hits"));
	ev.Add(Stage("v3", 10, 9, "rules", "planned"));
	ev.Add(Note("v3", 10, 10, "detect", "Misses small scratches under 3 mm; the rules must not treat a miss as a pass."));

	// 0.0.4: the rules and the overlay are built; the PLC bridge decides when inspection runs.
	ev.Add(Created("v4", 10, 14, "plc", "Line PLC bridge"));
	ev.Add(Connected("v4", 10, 14, "r_line", "plc", "rules", "dependency", "line state"));
	ev.Add(Stage("v4", 10, 14, "plc", "planned"));
	ev.Add(Created("v4", 10, 15, "overlay", "XR overlay"));
	ev.Add(Connected("v4", 10, 15, "r_boxes", "detect", "overlay", "communication", "boxes"));
	ev.Add(Stage("v4", 10, 15, "overlay", "planned"));
	ev.Add(Stage("v4", 10, 16, "overlay", "implementation_research"));
	ev.Add(Stage("v4", 10, 16, "overlay", "implementation"));
	ev.Add(Stage("v4", 10, 17, "rules", "implementation_research"));
	ev.Add(Stage("v4", 10, 20, "rules", "implementation"));
	ev.Add(Created("v4", 10, 21, "log", "Defect log"));
	ev.Add(Connected("v4", 10, 21, "r_verdicts", "rules", "log", "communication", "verdicts"));
	ev.Add(Stage("v4", 10, 21, "log", "planned"));

	// 0.1.0: the dashboard shows the shift; the overlay and rules reach merge; the field trial is proposed.
	ev.Add(Stage("v5", 10, 28, "overlay", "trials"));
	ev.Add(Stage("v5", 10, 30, "overlay", "merge_to_master"));
	ev.Add(Stage("v5", 10, 30, "rules", "trials"));
	ev.Add(Stage("v5", 10, 30, "rules", "merge_to_master"));
	ev.Add(Created("v5", 10, 31, "dash", "Supervisor dashboard"));
	ev.Add(Connected("v5", 10, 31, "r_live", "overlay", "dash", "communication", "live view"));
	ev.Add(Connected("v5", 10, 31, "r_reports", "log", "dash", "communication", "reports"));
	ev.Add(Stage("v5", 10, 31, "dash", "planned"));
	ev.Add(Stage("v5", 11, 3, "log", "implementation_research"));
	ev.Add(Created("v5", 11, 4, "line3", "Line 3 field trial"));
	ev.Add(Connected("v5", 11, 4, "r_deploy", "dash", "line3", "progress", "deploy"));
	ev.Add(Stage("v5", 11, 4, "line3", "planned"));
	ev.Add(Propose("v5", 11, 4, "t_field", "Two-shift trial on line 3 without a missed defect", "v6", "element:line3",
	               "The 1.0.0 gate."));
	ev.Add(TaskStep("v5", 11, 5, "t_field", "accepted"));
	ev.Add(Note("v5", 11, 6, "line3", "Trial not scheduled yet: waiting for the PLC bridge to stop the line on a fault."));

	// 1.0.0 is planned, not reached: the checker shows what is still missing.
	ev.Add(Stage("v6", 11, 10, "log", "implementation"));
	ev.Add(Stage("v6", 11, 10, "plc", "implementation_research"));
}

static bool DemoStored(const String& data_root)
{
	AtlasFamily f;
	String rev, err;
	return AtlasLoadFamily(data_root, DEMO_FAMILY, f, rev, err);
}

void AtlasDemoModel(AtlasFamily& f, AtlasMap& head, AtlasRules& rules, Vector<AtlasEvent>& events)
{
	demo_n = 0;
	DemoVersions(f);
	DemoHead(head);
	DemoRules(rules);
	DemoEvents(events);
}

void AtlasInstallDemo(const String& data_root)
{
	if(DemoStored(data_root))
		return;
	AtlasFamily f;
	AtlasMap head;
	AtlasRules rules;
	Vector<AtlasEvent> events;
	AtlasDemoModel(f, head, rules, events);

	String rev, err;
	if(!AtlasSaveMap(data_root, f.head_key, head, String(), rev, err))
		return;
	if(!AtlasCreateFamily(data_root, DEMO_FAMILY, f, rev, err))
		return;
	String rules_rev;
	AtlasSaveRules(data_root, DEMO_FAMILY, rules, String(), rules_rev, err);
	for(const AtlasEvent& e : events)
		AtlasPutEvent(data_root, DEMO_FAMILY, e, err);
}

END_UPP_NAMESPACE
