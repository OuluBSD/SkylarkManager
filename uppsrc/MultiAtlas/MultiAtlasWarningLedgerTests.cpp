#include "MultiAtlasWarningLedger.h"
#include <MultiAtlas/MultiAtlasContract.h>
#include <WebStorage/WebStorage.h>
#ifdef PLATFORM_POSIX
#include <unistd.h>
#include <stdio.h>
#endif

NAMESPACE_UPP

static int s_warning_checks, s_warning_failures;

static void WarningCheck(bool ok, const String& text)
{
	s_warning_checks++;
	if(ok) Cout() << "ok   " << text << '\n';
	else { s_warning_failures++; Cout() << "FAIL " << text << '\n'; }
}

static MultiAtlasRoadmapMessage WarningFinding(const String& track, const String& text)
{
	MultiAtlasRoadmapMessage message;
	message.severity = "warning";
	message.track_id = track;
	message.vid = "0.1.0";
	message.subject_kind = "element";
	message.subject_id = "same-element";
	message.rule = "missing-note";
	message.text = text;
	return message;
}

static MultiAtlasWarningAck WarningAck(const String& track, const String& id)
{
	MultiAtlasWarningAck ack;
	ack.track_id = track;
	ack.warning_id = id;
	ack.rule = "missing-note";
	ack.vid = "0.1.0";
	ack.actor = "operator";
	ack.comment = "reviewed";
	ack.at = "2026-11-18T12:00:00Z";
	return ack;
}

void RunMultiAtlasWarningTests()
{
	s_warning_checks = s_warning_failures = 0;
	// Avoid reliance on a fixed temporary directory name across independent runs.
	byte nonce[16];
	for(int i = 0; i < 16; i++) nonce[i] = (byte)Random(256);
	String root = AppendFileName(GetTempDirectory(), "multiatlas-warning-" + ToLower(HexString(nonce, 16)));
	String store1 = AppendFileName(root, "track-vision");
	String store2 = AppendFileName(root, "track-ai");
	RealizeDirectory(store1);
	RealizeDirectory(store2);

	MultiAtlasRoadmapMessage finding1 = WarningFinding("vision", "the warning text");
	MultiAtlasRoadmapMessage finding2 = WarningFinding("ai", "the warning text");
	String id1 = MultiAtlasWarningId("vision", finding1);
	String id2 = MultiAtlasWarningId("ai", finding2);
	WarningCheck(id1.GetCount() == 16 && MultiAtlasValidWarningId(id1) &&
	             id2.GetCount() == 16 && MultiAtlasValidWarningId(id2),
	             "warning IDs are exactly 16 lowercase hexadecimal characters");
	WarningCheck(id1 != id2, "identical warning fields hash to distinct track-scoped IDs");
	WarningCheck(!MultiAtlasValidWarningId("0123456789ABCDEF") &&
	             !MultiAtlasValidWarningId("1234") && !MultiAtlasValidWarningTrackId("bad.track"),
	             "warning ID and track scope validators reject malformed names");

	String error, revision;
	MultiAtlasWarningAck ack = WarningAck("vision", id1);
	bool saved = MultiAtlasSaveAck(store1, "vision", ack, RecordAbsent(), revision, error);
	WarningCheck(saved && revision == "1", "acknowledgment is persisted under its track store");
	Vector<MultiAtlasWarningAck> loaded1, loaded2;
	bool read1 = MultiAtlasLoadAcks(store1, "vision", loaded1, error);
	bool read2 = MultiAtlasLoadAcks(store2, "ai", loaded2, error);
	WarningCheck(read1 && loaded1.GetCount() == 1 && loaded1[0].warning_id == id1 &&
	             read2 && loaded2.IsEmpty() && !DirectoryExists(AppendFileName(store2, "data")),
	             "Track 1 ack is isolated and reading absent Track 2 acks creates no directories");
	MultiAtlasWarningAck ack2 = WarningAck("ai", id2);
	String revision2;
	bool saved2 = MultiAtlasSaveAck(store2, "ai", ack2, RecordAbsent(), revision2, error);
	WarningCheck(saved2 && revision2 == "1", "Track 2 can persist its own acknowledgment independently");

	Vector<MultiAtlasRoadmapMessage> active;
	active.Add(finding1);
	Vector<MultiAtlasStaleAck> stale;
	bool checked = MultiAtlasFindStaleAcks(store1, "vision", active, stale, error);
	WarningCheck(checked && stale.IsEmpty(), "unchanged active finding keeps its acknowledgment current");
	active[0].text = "updated warning text";
	checked = MultiAtlasFindStaleAcks(store1, "vision", active, stale, error);
	WarningCheck(checked && stale.GetCount() == 1 && stale[0].stale && stale[0].ack.warning_id == id1,
	             "updated finding marks only its old acknowledgment stale");
	Vector<MultiAtlasRoadmapMessage> no_ai_findings;
	no_ai_findings.Add(finding2);
	checked = MultiAtlasFindStaleAcks(store2, "ai", no_ai_findings, stale, error);
	WarningCheck(checked && stale.IsEmpty(), "stale detection remains isolated to the selected track");

#ifdef PLATFORM_POSIX
	String data = AppendFileName(store1, "data");
	String ack_dir = AppendFileName(data, "acks");
	String other_ack_dir = AppendFileName(AppendFileName(store2, "data"), "acks");
	String displaced = AppendFileName(data, "acks.saved");
	bool moved = rename(~ack_dir, ~displaced) == 0;
	bool linked = moved && symlink(~other_ack_dir, ~ack_dir) == 0;
	Vector<MultiAtlasWarningAck> symlink_read;
	bool rejected = linked && !MultiAtlasLoadAcks(store1, "vision", symlink_read, error);
	WarningCheck(rejected, "ack directory symlink cannot redirect reads into another store");
	if(linked) { unlink(~ack_dir); rename(~displaced, ~ack_dir); }

	String ack_path = AppendFileName(ack_dir, id1 + ".json");
	String moved_ack = AppendFileName(ack_dir, "ack.saved");
	bool file_moved = rename(~ack_path, ~moved_ack) == 0;
	String other_ack_file = AppendFileName(other_ack_dir, id2 + ".json");
	linked = file_moved && symlink(~other_ack_file, ~ack_path) == 0;
	rejected = linked && !MultiAtlasLoadAcks(store1, "vision", symlink_read, error);
	WarningCheck(rejected, "ack record symlink cannot redirect reads into another store");
	if(linked) { unlink(~ack_path); rename(~moved_ack, ~ack_path); }
#else
	WarningCheck(true, "filesystem symlink isolation checks are POSIX-specific");
#endif

	DeleteFolderDeep(root);
	Cout() << "MultiAtlasWarningLedgerTest checks: " << (s_warning_checks - s_warning_failures)
	       << "/" << s_warning_checks << " passed\n";
	if(s_warning_failures) SetExitCode(1);
}

END_UPP_NAMESPACE
