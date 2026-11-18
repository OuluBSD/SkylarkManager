#include "MultiAtlasModel.h"

NAMESPACE_UPP

static int s_multiatlas_checks;
static int s_multiatlas_failures;

static void MultiAtlasCheckTest(bool ok, const String& message)
{
	s_multiatlas_checks++;
	if(!ok) {
		s_multiatlas_failures++;
		Cout() << "FAIL: " << message << '\n';
	}
}

void RunMultiAtlasModelTests()
{
	s_multiatlas_checks = 0;
	s_multiatlas_failures = 0;

	MultiAtlasMetaFamily family;
	family.ensemble_name = "brownbear-suite";
	family.title = "BrownBear Multi-Track System";
	family.meta_family = "brownbear-meta";
	MultiAtlasTrack track;
	track.track_id = "vision";
	track.family = "brownbear-vision";
	track.repo_path = "/work/BrownBear-vision";
	track.branch = "track/vision";
	track.store_rel_path = "multiatlas/brownbear-vision/store";
	track.head_key = "brownbear-vision";
	MultiAtlasVersion track_version;
	track_version.vid = "vision-v0-1";
	track_version.code = 1001;
	track_version.requires.Add("built:Vision");
	track.versions.Add(pick(track_version));
	family.tracks.Add(pick(track));
	MultiAtlasVersion meta_version;
	meta_version.vid = "meta-v0-1";
	meta_version.code = 1001;
	meta_version.requires.Add("vision:tested:0.1.999");
	family.versions.Add(pick(meta_version));

	String family_json = StoreAsJson(family);
	MultiAtlasMetaFamily family_copy;
	MultiAtlasCheckTest(LoadFromJson(family_copy, family_json), "meta-family JSON parses");
	MultiAtlasCheckTest(family_copy.ensemble_name == family.ensemble_name &&
	                    family_copy.title == family.title && family_copy.meta_family == family.meta_family &&
	                    family_copy.tracks.GetCount() == 1 && family_copy.versions.GetCount() == 1,
	                    "meta-family round-trip preserves fields and vectors");
	MultiAtlasCheckTest(family_copy.tracks[0].track_id == "vision" &&
	                    family_copy.tracks[0].versions[0].code == 1001 &&
	                    family_copy.tracks[0].versions[0].requires[0] == "built:Vision" &&
	                    family_copy.versions[0].requires[0] == "vision:tested:0.1.999",
	                    "nested version fields and contract literals round-trip");

	String track_json = StoreAsJson(family.tracks[0]);
	MultiAtlasTrack track_copy;
	MultiAtlasCheckTest(LoadFromJson(track_copy, track_json), "track JSON parses");
	MultiAtlasCheckTest(track_copy.track_id == "vision" && track_copy.family == "brownbear-vision" &&
	                    track_copy.repo_path == "/work/BrownBear-vision" && track_copy.branch == "track/vision" &&
	                    track_copy.store_rel_path == "multiatlas/brownbear-vision/store" &&
	                    track_copy.head_key == "brownbear-vision" &&
	                    track_copy.versions.GetCount() == 1 && track_copy.versions[0].code == 1001,
	                    "track round-trip preserves every declared track field");

	// WebStorage's established record envelope wraps the data without changing
	// the model's Jsonize payload. Direct JSON remains readable for old records.
	ValueMap envelope;
	envelope.Add("schema_id", "atlas.family");
	envelope.Add("schema_version", "1");
	envelope.Add("revision", "7");
	ValueMap record;
	record.Add("envelope", envelope);
	record.Add("data", StoreAsJsonValue(family));
	MultiAtlasMetaFamily enveloped_copy;
	String revision;
	MultiAtlasCheckTest(MultiAtlasLoadRecord(enveloped_copy, AsJSON(record), revision) && revision == "7" &&
	                    enveloped_copy.ensemble_name == family.ensemble_name,
	                    "Atlas/WebStorage envelope record loads with its revision");
	MultiAtlasMetaFamily legacy_copy;
	MultiAtlasCheckTest(MultiAtlasLoadRecord(legacy_copy, family_json, revision) && revision.IsEmpty() &&
	                    legacy_copy.ensemble_name == family.ensemble_name,
	                    "legacy direct-data JSON remains readable");
	ValueMap malformed;
	malformed.Add("envelope", envelope);
	MultiAtlasMetaFamily unchanged;
	unchanged.ensemble_name = "sentinel";
	MultiAtlasCheckTest(!MultiAtlasLoadRecord(unchanged, AsJSON(malformed), revision) &&
	                    unchanged.ensemble_name == "sentinel",
	                    "malformed envelope is rejected without mutating the destination");

	MultiAtlasMap map;
	map.map = "vision-map";
	map.track_id = "vision";
	map.version = "0.1.0";
	map.world.width = 1000;
	map.world.height = 800;
	MultiAtlasElement root;
	root.id = "vision";
	root.name = "Vision track";
	root.kind = "block";
	root.goal.picked = true;
	root.goal.note = "coordinate track work";
	root.w = 600;
	root.h = 500;
	map.elements.Add(pick(root));
	MultiAtlasElement child;
	child.id = "detector";
	child.name = "Detector";
	child.kind = "box";
	child.parent = "vision";
	child.x = 10;
	child.y = 20;
	child.w = 200;
	child.h = 100;
	map.elements.Add(pick(child));
	MultiAtlasElement grandchild;
	grandchild.id = "ocr";
	grandchild.name = "OCR";
	grandchild.kind = "place";
	grandchild.parent = "detector";
	grandchild.x = 4;
	grandchild.y = 5;
	grandchild.w = 50;
	grandchild.h = 30;
	map.elements.Add(pick(grandchild));
	MultiAtlasCheckTest(MultiAtlasDepth(map, "ocr") == 2, "hierarchy depth counts ancestors");
	Vector<String> path = MultiAtlasPath(map, "ocr");
	MultiAtlasCheckTest(path.GetCount() == 3 && path[0] == "vision" && path[1] == "detector" && path[2] == "ocr",
	                    "hierarchy path returns root through selected element");
	Vector<String> children = MultiAtlasPlaneElements(map, "vision");
	MultiAtlasCheckTest(children.GetCount() == 1 && children[0] == "detector",
	                    "containment query returns direct children of a parent plane");
	MultiAtlasCheckTest(MultiAtlasDepth(map, "missing") == -1 && MultiAtlasPath(map, "missing").IsEmpty(),
	                    "unknown hierarchy elements fail closed");
	MultiAtlasCheck valid;
	MultiAtlasValidate(map, valid);
	MultiAtlasCheckTest(valid.Ok(), "valid nested geometry passes validation");
	String map_json = StoreAsJson(map);
	MultiAtlasMap map_copy;
	MultiAtlasCheckTest(LoadFromJson(map_copy, map_json) && map_copy.elements[0].goal.picked &&
	                    map_copy.elements[0].goal.note == "coordinate track work",
	                    "Atlas-compatible goal object preserves picked and note fields");

	map.elements[0].parent = "ocr";
	MultiAtlasCheck cyclic;
	MultiAtlasValidate(map, cyclic);
	MultiAtlasCheckTest(!cyclic.Ok() && MultiAtlasDepth(map, "ocr") == -1,
	                    "cyclic parent relationships are rejected");
	map.elements[0].parent.Clear();
	map.elements[1].parent = "absent";
	MultiAtlasCheck missing_ancestor;
	MultiAtlasValidate(map, missing_ancestor);
	bool missing_reported = false;
	for(const String& error : missing_ancestor.errors)
		if(error.Find("missing parent") >= 0 || error.Find("missing ancestor") >= 0)
			missing_reported = true;
	MultiAtlasCheckTest(missing_reported, "missing ancestor is distinguished from a parent cycle");

	Cout() << "MultiAtlasModelTest checks: " << (s_multiatlas_checks - s_multiatlas_failures)
	       << "/" << s_multiatlas_checks << " passed\n";
	SetExitCode(s_multiatlas_failures ? 1 : 0);
}

END_UPP_NAMESPACE
