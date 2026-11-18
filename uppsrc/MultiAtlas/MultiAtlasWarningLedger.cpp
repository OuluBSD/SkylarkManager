#include "MultiAtlasWarningLedger.h"
#include "MultiAtlasContract.h"
#include <WebStorage/WebStorage.h>

#ifdef PLATFORM_POSIX
#include <sys/stat.h>
#include <errno.h>
#endif
#ifdef PLATFORM_WIN32
#include <windows.h>
#endif

NAMESPACE_UPP

static bool IsMetaTrack(const String& track_id)
{
	return track_id == "meta";
}

bool MultiAtlasValidWarningTrackId(const String& track_id)
{
	return IsMetaTrack(track_id) || MultiAtlasIsValidTrackId(track_id);
}

bool MultiAtlasValidWarningId(const String& warning_id)
{
	if(warning_id.GetCount() != 16) return false;
	for(int i = 0; i < warning_id.GetCount(); i++) {
		int c = warning_id[i];
		if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return false;
	}
	return true;
}

String MultiAtlasWarningId(const String& track_id, const MultiAtlasRoadmapMessage& m)
{
	if(!MultiAtlasValidWarningTrackId(track_id)) return String();
	String key = track_id + "|" + m.rule + "|" + m.subject_kind + "|" + m.subject_id +
	             "|" + m.vid + "|" + m.text;
	uint64 h = 14695981039346656037ULL;
	for(int i = 0; i < key.GetCount(); i++) {
		h ^= (byte)key[i];
		h *= 1099511628211ULL;
	}
	StringBuffer hex;
	for(int shift = 60; shift >= 0; shift -= 4)
		hex.Cat("0123456789abcdef"[(h >> shift) & 15]);
	return String(hex);
}

// 0 means absent, 1 means an ordinary filesystem object of the requested
// type, and -1 means a link or an unexpected type. This inspects the final
// path component without following it.
static int InspectPath(const String& path, bool want_directory)
{
#ifdef PLATFORM_POSIX
	struct stat st;
	if(lstat(~path, &st) != 0)
		return errno == ENOENT ? 0 : -1;
	if(S_ISLNK(st.st_mode)) return -1;
	return want_directory ? (S_ISDIR(st.st_mode) ? 1 : -1) : (S_ISREG(st.st_mode) ? 1 : -1);
#elif defined(PLATFORM_WIN32)
	WString wpath = ToSystemCharsetW(path);
	DWORD attr = GetFileAttributesW(~wpath);
	if(attr == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND ||
	                                           GetLastError() == ERROR_PATH_NOT_FOUND ? 0 : -1;
	if(attr & FILE_ATTRIBUTE_REPARSE_POINT) return -1;
	return want_directory ? ((attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : -1) :
	                        ((attr & FILE_ATTRIBUTE_DIRECTORY) ? -1 : 1);
#else
	return (want_directory ? DirectoryExists(path) : FileExists(path)) ? 1 : 0;
#endif
}

static bool EnsureAckDirectory(const String& track_store, bool create, bool& absent, String& error)
{
	absent = false;
	if(track_store.IsEmpty()) { error = "empty track store path"; return false; }
	int root_state = InspectPath(track_store, true);
	if(root_state != 1) {
		error = root_state == 0 ? "track store root does not exist" : "track store root is a symlink or not a directory";
		return false;
	}
	String data_dir = AppendFileName(track_store, "data");
	String ack_dir = AppendFileName(data_dir, "acks");
	int data_state = InspectPath(data_dir, true);
	if(data_state < 0) { error = "track store data path is a symlink or not a directory"; return false; }
	if(data_state == 0) {
		if(!create) { absent = true; return true; }
		RealizeDirectory(data_dir);
		if(InspectPath(data_dir, true) != 1) { error = "cannot create track store data directory"; return false; }
	}
	int ack_state = InspectPath(ack_dir, true);
	if(ack_state < 0) { error = "track acknowledgment directory is a symlink or not a directory"; return false; }
	if(ack_state == 0) {
		if(!create) { absent = true; return true; }
		RealizeDirectory(ack_dir);
		if(InspectPath(ack_dir, true) != 1) { error = "cannot create track acknowledgment directory"; return false; }
	}
	return true;
}

static bool SafeAckFile(const JsonFileStore& store, const String& warning_id, String& error)
{
	String path = AppendFileName(store.CollectionDir(), warning_id + ".json");
	if(InspectPath(path, false) < 0) {
		error = "acknowledgment record is a symlink or not a regular file";
		return false;
	}
	return true;
}

bool MultiAtlasSaveAck(const String& track_store, const String& expected_track_id,
	                   const MultiAtlasWarningAck& ack, const String& expected_revision,
	                   String& new_revision, String& error)
{
	error.Clear();
	new_revision.Clear();
	if(!MultiAtlasValidWarningTrackId(expected_track_id) || ack.track_id != expected_track_id) {
		error = "acknowledgment track scope mismatch";
		return false;
	}
	if(!MultiAtlasValidWarningId(ack.warning_id)) { error = "invalid warning id"; return false; }
	if(ack.rule.IsEmpty() || ack.vid.IsEmpty() || ack.actor.IsEmpty() || ack.at.IsEmpty()) {
		error = "acknowledgment is missing rule, vid, actor, or timestamp";
		return false;
	}
	if(expected_revision.IsEmpty()) { error = "expected acknowledgment revision is required"; return false; }
	bool absent;
	if(!EnsureAckDirectory(track_store, true, absent, error)) return false;
	JsonFileStore store("multiatlas", track_store, "acks");
	if(!SafeAckFile(store, ack.warning_id, error)) return false;
	String json = StoreAsJson(ack);
	StoreResult result = store.Put(ack.warning_id, json, expected_revision, new_revision);
	if(!result) { error = result.error; return false; }
	return true;
}

bool MultiAtlasLoadAcks(const String& track_store, const String& expected_track_id,
	                   Vector<MultiAtlasWarningAck>& acks, String& error)
{
	acks.Clear();
	error.Clear();
	if(!MultiAtlasValidWarningTrackId(expected_track_id)) { error = "invalid acknowledgment track scope"; return false; }
	bool absent;
	if(!EnsureAckDirectory(track_store, false, absent, error)) return false;
	if(absent) return true;
	JsonFileStore store("multiatlas", track_store, "acks");
	Vector<String> ids;
	StoreResult listed = store.List(ids);
	if(!listed) { error = listed.error; return false; }
	for(const String& id : ids) {
		if(!MultiAtlasValidWarningId(id)) { error = "invalid acknowledgment record key"; acks.Clear(); return false; }
		if(!SafeAckFile(store, id, error)) { acks.Clear(); return false; }
		String json, revision;
		StoreResult loaded = store.Get(id, json, revision);
		if(!loaded) { error = loaded.error; acks.Clear(); return false; }
		MultiAtlasWarningAck ack;
		String record_revision;
		if(!MultiAtlasLoadRecord(ack, json, record_revision) || ack.track_id != expected_track_id ||
		   ack.warning_id != id) {
			error = "acknowledgment record does not match its track-scoped key";
			acks.Clear();
			return false;
		}
		acks.Add(pick(ack));
	}
	return true;
}

bool MultiAtlasFindStaleAcks(const String& track_store, const String& track_id,
	                         const Vector<MultiAtlasRoadmapMessage>& active_findings,
	                         Vector<MultiAtlasStaleAck>& stale_acks, String& error)
{
	stale_acks.Clear();
	if(!MultiAtlasValidWarningTrackId(track_id)) { error = "invalid acknowledgment track scope"; return false; }
	Vector<MultiAtlasWarningAck> acks;
	if(!MultiAtlasLoadAcks(track_store, track_id, acks, error)) return false;
	Index<String> active_ids;
	for(const MultiAtlasRoadmapMessage& message : active_findings) {
		if(message.track_id != track_id) continue;
		String id = MultiAtlasWarningId(track_id, message);
		if(!id.IsEmpty()) active_ids.Add(id);
	}
	for(MultiAtlasWarningAck& ack : acks) {
		if(active_ids.Find(ack.warning_id) >= 0) continue;
		MultiAtlasStaleAck stale;
		stale.ack = pick(ack);
		stale.stale = true;
		stale_acks.Add(pick(stale));
	}
	return true;
}

END_UPP_NAMESPACE
