#ifndef _ManagerSkylark_DataStoreInternal_h_
#define _ManagerSkylark_DataStoreInternal_h_

#include "DataStore.h"

NAMESPACE_UPP

// Helpers shared by the DataStore*.cpp translation units (locking, atomic
// record I/O, journal and receipts, schema helpers). Not part of the public API.

extern Mutex s_canonical_write_mutex;

struct JsonFidelity {
	bool syntax_ok = true;
	bool duplicate_key = false;
	bool trailing_input = false;
	bool numeric_precision = false;
	bool escaped_key = false;
};

class JsonFidelityScanner {
public:
	JsonFidelityScanner(const String& json) : json(json), pos(0) {}
	JsonFidelity Scan() {
		SkipSpace();
		if(!Value(0)) result.syntax_ok = false;
		SkipSpace();
		if(pos != json.GetCount()) result.trailing_input = true;
		return result;
	}
private:
	const String& json;
	int pos;
	JsonFidelity result;

	void SkipSpace() {
		while(pos < json.GetCount() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) pos++;
	}
	bool StringToken(String& raw, bool& escaped) {
		if(pos >= json.GetCount() || json[pos++] != '"') return false;
		int start = pos;
		escaped = false;
		while(pos < json.GetCount()) {
			char c = json[pos++];
			if(c == '"') { raw = json.Mid(start, pos - start - 1); return true; }
			if((byte)c < 0x20) return false;
			if(c == '\\') {
				escaped = true;
				if(pos >= json.GetCount()) return false;
				char e = json[pos++];
				if(e == 'u') {
					if(pos + 4 > json.GetCount()) return false;
					for(int n = 0; n < 4; n++) {
						char h = json[pos++];
						if(!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'))) return false;
					}
				}
				else if(e != '"' && e != '\\' && e != '/' && e != 'b' && e != 'f' && e != 'n' && e != 'r' && e != 't') return false;
			}
		}
		return false;
	}
	bool Number() {
		int start = pos;
		if(pos < json.GetCount() && json[pos] == '-') pos++;
		if(pos >= json.GetCount()) return false;
		if(json[pos] == '0') pos++;
		else if(json[pos] >= '1' && json[pos] <= '9') while(pos < json.GetCount() && json[pos] >= '0' && json[pos] <= '9') pos++;
		else return false;
		bool decimal = false;
		if(pos < json.GetCount() && json[pos] == '.') {
			decimal = true; pos++;
			int digits = pos;
			while(pos < json.GetCount() && json[pos] >= '0' && json[pos] <= '9') pos++;
			if(pos == digits) return false;
		}
		if(pos < json.GetCount() && (json[pos] == 'e' || json[pos] == 'E')) {
			decimal = true; pos++;
			if(pos < json.GetCount() && (json[pos] == '+' || json[pos] == '-')) pos++;
			int digits = pos;
			while(pos < json.GetCount() && json[pos] >= '0' && json[pos] <= '9') pos++;
			if(pos == digits) return false;
		}
		String token = json.Mid(start, pos - start);
	// U++ floating JSON values are not an exact-decimal contract; quarantine all fractions/exponents.
		if(decimal) result.numeric_precision = true;
		if(!decimal) {
			String digits = token.StartsWith("-") ? token.Mid(1) : token;
			if(digits.GetCount() > 16 || (digits.GetCount() == 16 && digits > "9007199254740991"))
				result.numeric_precision = true;
		}
		if(token.Find('e') >= 0 || token.Find('E') >= 0) result.numeric_precision = true;
		return true;
	}
	bool Value(int depth) {
		if(depth > 128) return false;
		SkipSpace();
		if(pos >= json.GetCount()) return false;
		char c = json[pos];
		if(c == '"') { String raw; bool escaped; return StringToken(raw, escaped); }
		if(c == '{') {
			pos++; SkipSpace();
			Index<String> keys;
			if(pos < json.GetCount() && json[pos] == '}') { pos++; return true; }
			for(;;) {
				String key; bool escaped;
				if(!StringToken(key, escaped)) return false;
				// Escaped names need full Unicode normalization to compare; fail closed instead.
				if(escaped) result.escaped_key = true;
				if(keys.Find(key) >= 0) result.duplicate_key = true;
				else keys.Add(key);
				SkipSpace(); if(pos >= json.GetCount() || json[pos++] != ':') return false;
				if(!Value(depth + 1)) return false;
				SkipSpace(); if(pos >= json.GetCount()) return false;
				if(json[pos] == '}') { pos++; return true; }
				if(json[pos++] != ',') return false;
				SkipSpace();
			}
		}
		if(c == '[') {
			pos++; SkipSpace();
			if(pos < json.GetCount() && json[pos] == ']') { pos++; return true; }
			for(;;) {
				if(!Value(depth + 1)) return false;
				SkipSpace(); if(pos >= json.GetCount()) return false;
				if(json[pos] == ']') { pos++; return true; }
				if(json[pos++] != ',') return false;
			}
		}
		if(c == '-' || (c >= '0' && c <= '9')) return Number();
		static const char* literals[] = { "true", "false", "null" };
		for(int i = 0; i < 3; i++) {
			String literal = literals[i];
			if(json.Mid(pos, literal.GetCount()) == literal) { pos += literal.GetCount(); return true; }
		}
		return false;
	}
};

struct CanonicalWriteGuard {
	CanonicalWriteGuard() { s_canonical_write_mutex.Enter(); }
	~CanonicalWriteGuard() { s_canonical_write_mutex.Leave(); }
};

enum AtomicReplaceOutcome { ATOMIC_REPLACE_FAILED, ATOMIC_REPLACE_COMMITTED, ATOMIC_REPLACE_DURABILITY_UNCERTAIN };

#ifdef PLATFORM_POSIX
struct LockedSourceRoot {
	int root_fd = -1;
	int lock_fd = -1;
	~LockedSourceRoot() {
		Close();
	}
	void Close() { if(lock_fd >= 0) { flock(lock_fd, LOCK_UN); close(lock_fd); lock_fd = -1; } if(root_fd >= 0) { close(root_fd); root_fd = -1; } }
	bool Open(const String& root) {
		if(!IsFullPath(root.ToStd().c_str())) return false;
		root_fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if(root_fd < 0) return false;
		Vector<String> parts = Split(root.Mid(1), '/');
		for(const String& part : parts) {
			if(part.IsEmpty()) continue;
			if(part == "." || part == "..") return false;
			int next = openat(root_fd, part.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if(next < 0) return false;
			close(root_fd);
			root_fd = next;
		}
		struct stat root_stat;
		if(fstat(root_fd, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode) || root_stat.st_uid != geteuid() ||
		   (root_stat.st_mode & (S_IWGRP | S_IWOTH))) return false;
		lock_fd = openat(root_fd, ".mgr-write.lock", O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
		if(lock_fd < 0) return false;
		struct stat lock_stat;
		if(fstat(lock_fd, &lock_stat) != 0 || !S_ISREG(lock_stat.st_mode) || lock_stat.st_uid != geteuid() || lock_stat.st_nlink != 1 ||
		   fchmod(lock_fd, 0600) != 0 || fstat(lock_fd, &lock_stat) != 0 || (lock_stat.st_mode & 077) != 0)
			return false;
		while(flock(lock_fd, LOCK_EX) != 0) {
			if(errno != EINTR) return false;
		}
		return true;
	}
};

#endif
#ifdef PLATFORM_POSIX
static const int MAX_COMMIT_RECEIPTS = 128;
static const int MAX_COMMIT_RECEIPTS_BYTES = 256 * 1024;
struct CommitReceipt : Moveable<CommitReceipt> {
	String operation_id, source_id, workspace_id, digest;
	Vector<String> ids, revisions;
	CommitReceipt() {}
	CommitReceipt(const CommitReceipt& o) : operation_id(o.operation_id), source_id(o.source_id), workspace_id(o.workspace_id), digest(o.digest), ids(clone(o.ids)), revisions(clone(o.revisions)) {}
	CommitReceipt& operator=(const CommitReceipt& o) {
		if(this != &o) { operation_id = o.operation_id; source_id = o.source_id; workspace_id = o.workspace_id; digest = o.digest; ids = clone(o.ids); revisions = clone(o.revisions); }
		return *this;
	}
};

#endif
AtomicReplaceOutcome AtomicCreateRecord(int parent_fd, const String& leaf, const String& bytes);
AtomicReplaceOutcome AtomicReplaceRecord(int parent_fd, const String& leaf, const String& bytes,
	                                                DataWriteTestFailpoint failpoint);
String CreateJournalPath(const String& qualified_id);
bool DiscardPreparedJournalLocked(int root_fd, const String& source_id,
	                                     const String& workspace_id, const String& operation_id);
bool EnsureCreateRecordDirectory(int root_fd);
bool GetTypedField(const ValueMap& map, const String& key, Value& value);
bool HasConflictingCreateIdentity(int dir_fd, const String& prefix, int depth,
	                                         const String& workspace_id, const String& record_id,
	                                         const String& expected_path, bool& conflict,
	                                         int& entries_scanned, int64& bytes_scanned);
bool IsSafeSourceId(const String& id);
bool JournalArtifactsMatch(int op_fd, const JournalManifest& manifest);
ValueMap JournalManifestMap(const JournalManifest& manifest);
bool OpenRecordParent(int root_fd, const String& rel_path, int& parent_fd, String& leaf);
#ifdef PLATFORM_POSIX
bool ReadCommitReceipts(int root_fd, Vector<CommitReceipt>& receipts, bool& exists);
#endif
bool ReadJournalFile(int dir_fd, const String& name, int64 limit, String& bytes);
bool ReadRecordAt(int parent_fd, const String& leaf, String& bytes);
bool RemoveJournalContents(int op_fd, const JournalManifest& manifest);
#ifdef PLATFORM_POSIX
bool StoreCommitReceipt(int root_fd, const CommitReceipt& receipt, DataWriteTestFailpoint failpoint, bool dry_run = false);
#endif

END_UPP_NAMESPACE

#endif
