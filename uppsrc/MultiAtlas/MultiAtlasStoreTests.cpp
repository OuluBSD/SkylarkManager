#include "MultiAtlasStore.h"
#include <WebStorage/WebStorage.h>
#include <atomic>
#include <thread>
#ifdef PLATFORM_POSIX
#include <unistd.h>
#endif

NAMESPACE_UPP

static int s_store_checks, s_store_failures;

static void StoreCheck(bool ok, const String& text)
{
	s_store_checks++;
	if(ok) Cout() << "ok   " << text << '\n';
	else { s_store_failures++; Cout() << "FAIL " << text << '\n'; }
}

static String TempName()
{
	byte nonce[12];
	for(byte& b : nonce) b = (byte)Random(256);
	return "multiatlas-store-" + ToLower(HexString(nonce, sizeof(nonce)));
}

static bool MakeDir(const String& path)
{
	return RealizeDirectory(path) && DirectoryExists(path);
}

static MultiAtlasMap StoreMap()
{
	MultiAtlasMap map;
	map.map = "vision"; map.track_id = "vision"; map.version = "0.1.0";
	MultiAtlasElement element;
	element.id = "start"; element.name = "Start"; element.kind = "task";
	element.status = "open"; element.x = 5; element.y = 5; element.w = 100; element.h = 60;
	map.elements.Add(pick(element));
	return map;
}

void RunMultiAtlasStoreTests()
{
	s_store_checks = s_store_failures = 0;
	String base = AppendFileName(GetTempPath(), TempName());
	String repo1 = AppendFileName(base, "repo-one"), repo2 = AppendFileName(base, "repo-two");
	String store1 = AppendFileName(repo1, "multiatlas/vision/store");
	String store2 = AppendFileName(repo2, "multiatlas/ai/store");
	MakeDir(store1); MakeDir(store2);
	MultiAtlasEnsembleDef ens;
	ens.meta_repo = repo1; ens.meta_store = "multiatlas/meta/store";
	MakeDir(AppendFileName(repo1, ens.meta_store));
	MultiAtlasTrackDef vision; vision.track_id = "vision"; vision.repo = repo1;
	vision.store = "multiatlas/vision/store"; ens.tracks.Add(pick(vision));
	MultiAtlasTrackDef ai; ai.track_id = "ai"; ai.repo = repo2; ai.store = "multiatlas/ai/store";
	ens.tracks.Add(pick(ai));
	String root1 = AppendFileName(store1, "data"), root2 = AppendFileName(store2, "data");
	String meta_root = AppendFileName(AppendFileName(repo1, ens.meta_store), "data");
	StoreCheck(MultiAtlasResolveTrackStore(ens, "vision") == root1 &&
	           MultiAtlasResolveTrackStore(ens, "ai") == root2 &&
	           MultiAtlasResolveTrackStore(ens, "meta") == meta_root &&
	           MultiAtlasResolveTrackStore(ens, "unknown").IsEmpty(),
	           "resolver maps configured tracks and meta to isolated data roots");
	MultiAtlasEnsembleDef legacy;
	legacy.meta_repo = repo1;
	legacy.meta_store = "atlas/legacy/multiatlas/meta/store";
	MakeDir(AppendFileName(repo1, legacy.meta_store));
	StoreCheck(MultiAtlasResolveTrackStore(legacy, "meta").IsEmpty(),
	           "resolver rejects nested MultiAtlas paths under the legacy Atlas namespace");

	MultiAtlasMap map = StoreMap();
	String error;
	StoreCheck(MultiAtlasSaveTrackMap(root1, "vision", map, RecordAbsent(), error),
	           "map create uses an absent revision and persists atomically");
	MultiAtlasMap loaded_map; String revision;
	bool map_loaded = MultiAtlasLoadTrackMap(root1, "vision", loaded_map, revision, error);
	StoreCheck(map_loaded && revision == "1" && loaded_map.version == "0.1.0" &&
	           loaded_map.elements.GetCount() == 1 && loaded_map.elements[0].id == "start",
	           "map envelope round-trips through the track store");
	String raw_map = LoadFile(AppendFileName(AppendFileName(root1, "maps"), "vision.json"));
	Value raw_doc = ParseJSON(raw_map);
	StoreCheck(raw_doc.Is<ValueMap>() && ValueMap(raw_doc).Find("envelope") >= 0 &&
	           ValueMap(raw_doc).Find("data") >= 0,
	           "stored map uses the Atlas-compatible envelope/data document shape");
	StoreCheck(!MultiAtlasSaveTrackMap(root1, "../escaped", map, RecordAbsent(), error) &&
	           !DirectoryExists(AppendFileName(root1, "../escaped")),
	           "record keys containing escaped paths are rejected before filesystem access");
	StoreCheck(!MultiAtlasSaveTrackMap(root1, "vision", map, String(), error) &&
	           error.Find("expected revision is required") >= 0,
	           "all writes require an explicit expected revision");

	MultiAtlasTrack family;
	family.track_id = "vision"; family.family = "brownbear-vision"; family.branch = "track/vision";
	StoreCheck(MultiAtlasSaveTrackFamily(root1, "brownbear-vision", family, RecordAbsent(), error),
	           "track family create succeeds");
	MultiAtlasTrack loaded_family;
	StoreCheck(MultiAtlasLoadTrackFamily(root1, "brownbear-vision", loaded_family, revision, error) &&
	           revision == "1" && loaded_family.family == "brownbear-vision",
	           "track family record round-trips");
	MultiAtlasMetaFamily meta;
	meta.ensemble_name = "suite"; meta.title = "Integration"; meta.meta_family = "suite-meta";
	StoreCheck(MultiAtlasSaveMetaFamily(meta_root, "suite-meta", meta, RecordAbsent(), error) &&
	           MultiAtlasLoadMetaFamily(meta_root, "suite-meta", meta, revision, error) &&
	           revision == "1" && meta.ensemble_name == "suite",
	           "meta-family record round-trips in its separate repository store");

	MultiAtlasRules rules;
	rules.initial.Add("built:Core");
	StoreCheck(MultiAtlasSaveTrackRules(root1, "brownbear-vision", rules, RecordAbsent(), error),
	           "rules document create succeeds");
	MultiAtlasRules loaded_rules;
	StoreCheck(MultiAtlasLoadTrackRules(root1, "brownbear-vision", loaded_rules, revision, error) &&
	           revision == "1" && loaded_rules.initial.GetCount() == 1 && loaded_rules.initial[0] == "built:Core",
	           "rules document round-trips");

	MultiAtlasEvent event;
	event.id = "ev-001"; event.at = "2026-11-18T00:00:00Z"; event.actor = "agent";
	event.track_id = "vision"; event.vid = "0.1.0"; event.type = "task-done"; event.text = "passed";
	String event_family = "brownbear-vision";
	bool event_saved = MultiAtlasPutTrackEvent(root1, event_family, "vision", event, error);
	Vector<MultiAtlasEvent> events;
	bool events_loaded = MultiAtlasLoadTrackEvents(root1, event_family, "vision", events, error);
	StoreCheck(event_saved && events_loaded && events.GetCount() == 1 && events[0].id == "ev-001" &&
	           events[0].text == "passed",
	           "event append and family-scoped list round-trip");
	StoreCheck(!MultiAtlasPutTrackEvent(root1, event_family, "vision", event, error) && error == "revision conflict",
	           "event records are immutable and duplicate append conflicts");
	StoreCheck(!MultiAtlasPutTrackEvent(root1, event_family, "ai", event, error) &&
	           error.Find("scope") >= 0,
	           "event track scope is checked separately from its family store key");

	MultiAtlasWarningAck ack;
	ack.track_id = "vision"; ack.warning_id = "0123456789abcdef"; ack.rule = "gate";
	ack.vid = "0.1.0"; ack.actor = "agent"; ack.at = "2026-11-18T00:00:00Z";
	String ack_rev;
	bool ack_saved = MultiAtlasSaveTrackAck(root1, "vision", ack, RecordAbsent(), ack_rev, error);
	Vector<MultiAtlasWarningAck> acks;
	bool ack_loaded = MultiAtlasLoadTrackAcks(root1, "vision", acks, error);
	StoreCheck(ack_saved && ack_rev == "1" && ack_loaded && acks.GetCount() == 1 &&
	           acks[0].warning_id == ack.warning_id,
	           "track acknowledgment wrappers reuse the scoped ledger");
	Vector<MultiAtlasWarningAck> absent_acks;
	bool absent_ack_read = MultiAtlasLoadTrackAcks(root2, "ai", absent_acks, error);
	StoreCheck(absent_ack_read && absent_acks.IsEmpty() && !DirectoryExists(root2),
	           "absent store acknowledgment read creates no data directory");
	MultiAtlasMap absent_map;
	StoreCheck(!MultiAtlasLoadTrackMap(root2, "nothing", absent_map, revision, error) &&
	           !DirectoryExists(root2),
	           "absent record read creates no storage directories");
	StoreCheck(MultiAtlasSaveTrackMap(root2, "ai-map", map, RecordAbsent(), error) &&
	           MultiAtlasLoadTrackMap(root1, "vision", loaded_map, revision, error) &&
	           loaded_map.track_id == "vision",
	           "writes to a second track worktree do not alter the first track record");

	// Both writers are released together and use one base revision. CAS must
	// serialize them so exactly one update commits and the other observes rev 2.
	MultiAtlasTrack concurrent;
	concurrent.track_id = "race"; concurrent.family = "race"; concurrent.branch = "base";
	String race_key = "race-family";
	bool created = MultiAtlasSaveTrackFamily(root1, race_key, concurrent, RecordAbsent(), error);
	MultiAtlasTrack base_record; String base_revision;
	bool base_read = MultiAtlasLoadTrackFamily(root1, race_key, base_record, base_revision, error);
	std::atomic<bool> go(false);
	bool write_a = false, write_b = false;
	String error_a, error_b;
	MultiAtlasTrack a, b;
	a.track_id = b.track_id = base_record.track_id;
	a.family = b.family = base_record.family;
	a.branch = "writer-a"; b.branch = "writer-b";
	std::thread ta([&] { while(!go.load(std::memory_order_acquire)) std::this_thread::yield(); write_a = MultiAtlasSaveTrackFamily(root1, race_key, a, base_revision, error_a); });
	std::thread tb([&] { while(!go.load(std::memory_order_acquire)) std::this_thread::yield(); write_b = MultiAtlasSaveTrackFamily(root1, race_key, b, base_revision, error_b); });
	go.store(true, std::memory_order_release);
	ta.join(); tb.join();
	MultiAtlasTrack final_record; String final_revision;
	bool final_read = MultiAtlasLoadTrackFamily(root1, race_key, final_record, final_revision, error);
	bool one_won = write_a != write_b;
	String losing_error = write_a ? error_b : error_a;
	StoreCheck(created && base_read && one_won && losing_error.Find("revision conflict") >= 0 &&
	           final_read && final_revision == "2" &&
	           (final_record.branch == "writer-a" || final_record.branch == "writer-b"),
	           "simultaneous writes from one base revision yield one commit and one CAS conflict");

#ifdef PLATFORM_POSIX
	String outside = AppendFileName(base, "outside"); MakeDir(outside);
	String nested_multiatlas = AppendFileName(repo1, "atlas/legacy/multiatlas/vision/store/data");
	MakeDir(nested_multiatlas);
	StoreCheck(!MultiAtlasSaveTrackMap(nested_multiatlas, "escape", map, RecordAbsent(), error),
	           "record operations reject a nested multiatlas child under legacy Atlas");
	MakeDir(AppendFileName(repo1, "multiatlas/alias"));
	String internal_alias = AppendFileName(repo1, "multiatlas/alias/store");
	bool internal_link = symlink(~AppendFileName(repo1, ens.meta_store), ~internal_alias) == 0;
	MultiAtlasTrackDef alias_track;
	alias_track.track_id = "alias"; alias_track.repo = repo1; alias_track.store = "multiatlas/alias/store";
	ens.tracks.Add(pick(alias_track));
	StoreCheck(internal_link && MultiAtlasResolveTrackStore(ens, "alias") == meta_root,
	           "resolver canonicalizes aliases that remain inside the configured MultiAtlas namespace");
	if(internal_link) unlink(~internal_alias);
	String maps_dir = AppendFileName(root1, "maps");
	String moved_maps = AppendFileName(root1, "maps.saved");
	String other_maps = AppendFileName(root2, "maps");
	String map_file = AppendFileName(maps_dir, "vision.json");
	String moved_map = AppendFileName(maps_dir, "vision.saved");
	String other_map = AppendFileName(other_maps, "ai-map.json");
	bool moved_collection = rename(~maps_dir, ~moved_maps) == 0;
	bool linked_collection = moved_collection && symlink(~other_maps, ~maps_dir) == 0;
	bool rejected_collection = linked_collection &&
		!MultiAtlasLoadTrackMap(root1, "vision", absent_map, revision, error);
	StoreCheck(rejected_collection, "symlinked collection cannot redirect reads into another worktree");
	if(linked_collection) unlink(~maps_dir);
	if(moved_collection) rename(~moved_maps, ~maps_dir);
	bool moved_record = rename(~map_file, ~moved_map) == 0;
	bool linked_record = moved_record && symlink(~other_map, ~map_file) == 0;
	bool rejected_record = linked_record &&
		!MultiAtlasLoadTrackMap(root1, "vision", absent_map, revision, error);
	StoreCheck(rejected_record, "symlinked record cannot redirect reads into another worktree");
	if(linked_record) unlink(~map_file);
	if(moved_record) rename(~moved_map, ~map_file);
	String symlink_data = AppendFileName(AppendFileName(repo2, "multiatlas/ai/store"), "data");
	DeleteFolderDeep(symlink_data);
	bool link_ok = symlink(~outside, ~symlink_data) == 0;
	StoreCheck(link_ok && !MultiAtlasLoadTrackMap(root2, "unrelated", absent_map, revision, error),
	           "symlinked data directory cannot redirect reads into an unrelated worktree");
	if(link_ok) unlink(~symlink_data);
#else
	StoreCheck(true, "filesystem symlink isolation checks are POSIX-specific");
#endif

	DeleteFolderDeep(base);
	Cout() << "MultiAtlasStoreTest checks: " << (s_store_checks - s_store_failures)
	       << "/" << s_store_checks << " passed\n";
	if(s_store_failures) SetExitCode(1);
}

END_UPP_NAMESPACE
