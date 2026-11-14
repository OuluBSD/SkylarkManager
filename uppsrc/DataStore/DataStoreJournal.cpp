#include "DataStoreInternal.h"
#include <stdio.h>
#include <errno.h>
#ifdef PLATFORM_POSIX
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>
#endif

NAMESPACE_UPP

static bool JournalText(const ValueMap& map, const char *key, String& out, int max_bytes)
{
	Value v = MapGet(map, key);
	if(!IsString(v)) return false;
	out = (String)v;
	if(out.IsEmpty() || out.GetCount() > max_bytes) return false;
	for(int i = 0; i < out.GetCount(); i++)
		if((byte)out[i] < 0x21 || (byte)out[i] > 0x7e) return false;
	return true;
}

static bool JournalValueBounded(const Value& value, int depth, int& nodes)
{
	if(depth > 8 || ++nodes > 1024) return false;
	if(value.Is<ValueMap>()) {
		ValueMap map = value;
		if(map.GetCount() > 16) return false;
		for(int i = 0; i < map.GetCount(); i++)
			if(!JournalValueBounded(map.GetValue(i), depth + 1, nodes)) return false;
	}
	else if(value.Is<ValueArray>()) {
		ValueArray array = value;
		if(array.GetCount() > 64) return false;
		for(const Value& item : array)
			if(!JournalValueBounded(item, depth + 1, nodes)) return false;
	}
	return true;
}

static bool JournalHasExactKeys(const ValueMap& map, std::initializer_list<const char *> names)
{
	if(map.GetCount() != (int)names.size()) return false;
	for(const char *name : names)
		if(map.Find(name) < 0) return false;
	return true;
}

String CreateJournalPath(const String& qualified_id)
{
	if(qualified_id.IsEmpty()) return String();
	return "records/" + DataStore::ContentDigest(qualified_id) + ".json";
}

bool DataStore::ParseJournalManifest(const String& bytes, JournalManifest& manifest)
{
	if(bytes.IsEmpty() || bytes.GetCount() > 65536) return false;
	JsonFidelity fidelity = JsonFidelityScanner(bytes).Scan();
	if(!fidelity.syntax_ok || fidelity.duplicate_key || fidelity.trailing_input || fidelity.numeric_precision || fidelity.escaped_key)
		return false;
	Value root = ParseJSON(bytes);
	if(!root.Is<ValueMap>()) return false;
	int nodes = 0;
	if(!JournalValueBounded(root, 0, nodes)) return false;
	ValueMap map = root;
	Value version = MapGet(map, "version"), records_value = MapGet(map, "records");
	if(!version.Is<double>() || (version != Value(1.0) && version != Value(2.0)) || !records_value.Is<ValueArray>()) return false;
	JournalManifest parsed;
	parsed.version = version == Value(1.0) ? 1 : 2;
	if(!JournalText(map, "operation_id", parsed.operation_id, 64) ||
	   !JournalText(map, "source_id", parsed.source_id, 64) ||
	   !JournalText(map, "workspace_id", parsed.workspace_id, 256) ||
	   !JournalText(map, "state", parsed.state, 16) ||
	   (parsed.state != "prepared" && parsed.state != "committing" && parsed.state != "committed" && parsed.state != "rolled-back")) return false;
	bool root_keys_ok = JournalHasExactKeys(map, { "version", "operation_id", "source_id", "workspace_id", "state", "records", "request_digest" }) ||
	                    (parsed.version == 1 && parsed.state == "prepared" &&
	                     JournalHasExactKeys(map, { "version", "operation_id", "source_id", "workspace_id", "state", "records" }));
	if(!root_keys_ok) return false;
	String workspace_diagnostic;
	if(!MatchIdPattern(parsed.workspace_id, "^[A-Za-z0-9][A-Za-z0-9._/-]*$", workspace_diagnostic)) return false;
	Value digest_value = MapGet(map, "request_digest");
	if(parsed.state == "prepared") {
		if(!digest_value.IsNull() && (!IsString(digest_value) || (String)digest_value != "")) return false;
	}
	else {
		if(!IsString(digest_value)) return false;
		parsed.request_digest = digest_value;
		if(parsed.request_digest.GetCount() != 64) return false;
		for(int i = 0; i < parsed.request_digest.GetCount(); i++)
			if(!((parsed.request_digest[i] >= '0' && parsed.request_digest[i] <= '9') || (parsed.request_digest[i] >= 'a' && parsed.request_digest[i] <= 'f'))) return false;
	}
	for(int i = 0; i < parsed.operation_id.GetCount(); i++) {
		char c = parsed.operation_id[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
	}
	ValueArray records = records_value;
	if(records.GetCount() < 1 || records.GetCount() > 64) return false;
	Index<String> identities, references;
	Index<String> paths;
	for(const Value& item : records) {
		if(!item.Is<ValueMap>()) return false;
		ValueMap rm = item;
		if(parsed.version == 1 ? !JournalHasExactKeys(rm, { "source_id", "record_id", "path", "expected_revision", "new_revision", "backup_ref", "payload_ref", "old_hash", "new_hash", "state" }) :
		   !JournalHasExactKeys(rm, { "source_id", "record_id", "path", "action", "expected_revision", "new_revision", "backup_ref", "payload_ref", "old_hash", "new_hash", "state" })) return false;
		JournalRecord record;
		record.action = parsed.version == 1 ? "update" : MapGetString(rm, "action");
		if(record.action != "update" && record.action != "create") return false;
		Value backup_value = MapGet(rm, "backup_ref"), old_hash_value = MapGet(rm, "old_hash");
		Value expected_revision_value = MapGet(rm, "expected_revision");
		bool expected_revision_ok = record.action == "create" ? (IsString(expected_revision_value) && (String)expected_revision_value == "") :
		                            JournalText(rm, "expected_revision", record.expected_revision, 32);
		if(record.action == "create" && IsString(expected_revision_value)) record.expected_revision = expected_revision_value;
		bool backup_ok = record.action == "create" ? (IsString(backup_value) && (String)backup_value == "") : JournalText(rm, "backup_ref", record.backup_ref, 64);
		if(record.action == "create" && IsString(backup_value)) record.backup_ref = backup_value;
		bool old_hash_ok = record.action == "create" ? (IsString(old_hash_value) && (String)old_hash_value == "") : JournalText(rm, "old_hash", record.old_hash, 64);
		if(record.action == "create" && IsString(old_hash_value)) record.old_hash = old_hash_value;
		if(!JournalText(rm, "source_id", record.source_id, 64) || record.source_id != parsed.source_id ||
		   !JournalText(rm, "record_id", record.record_id, 128) ||
		   !JournalText(rm, "path", record.rel_path, 512) || record.rel_path.StartsWith("/") ||
		   !expected_revision_ok ||
		   !JournalText(rm, "new_revision", record.new_revision, 32) ||
		   !backup_ok ||
		   !JournalText(rm, "payload_ref", record.payload_ref, 64) ||
		   !old_hash_ok ||
		   (record.action == "update" ? record.old_hash.GetCount() != 64 : !record.old_hash.IsEmpty()) ||
		   !JournalText(rm, "new_hash", record.new_hash, 64) || record.new_hash.GetCount() != 64 ||
		   !JournalText(rm, "state", record.state, 16) ||
		   (record.state != "pending" && record.state != "applied" && record.state != "rolled-back") ||
		   identities.Find(record.record_id) >= 0) return false;
		if((record.action == "update" && !record.backup_ref.StartsWith("backup-")) ||
		   (record.action == "create" && !record.backup_ref.IsEmpty()) || !record.payload_ref.StartsWith("payload-")) return false;
		if((!record.backup_ref.IsEmpty() && references.Find(record.backup_ref) >= 0) || references.Find(record.payload_ref) >= 0) return false;
		if(!record.backup_ref.IsEmpty()) references.Add(record.backup_ref);
		references.Add(record.payload_ref);
		for(const String& ref : { record.backup_ref, record.payload_ref }) {
			if(ref.IsEmpty()) continue;
			for(int i = 0; i < ref.GetCount(); i++) {
				char c = ref[i];
				if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
			}
		}
		for(const String& part : Split(record.rel_path, '/'))
			if(part.IsEmpty() || part == "." || part == ".." || part.Find('\\') >= 0) return false;
		if(record.rel_path.StartsWith(".mgr-journal/") || paths.Find(record.rel_path) >= 0) return false;
		paths.Add(record.rel_path);
		identities.Add(record.record_id);
		parsed.records.Add(record);
	}
	manifest = pick(parsed);
	return true;
}


#ifdef PLATFORM_POSIX
bool ReadJournalFile(int dir_fd, const String& name, int64 limit, String& bytes)
{
	int fd = openat(dir_fd, name.ToStd().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
	if(fd < 0) return false;
	struct stat st;
	bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
	          st.st_nlink == 1 && !(st.st_mode & 077) && st.st_size >= 0 && st.st_size <= limit;
	bytes.Clear();
	char buffer[8192];
	while(ok) {
		ssize_t n = read(fd, buffer, sizeof(buffer));
		if(n < 0 && errno == EINTR) continue;
		if(n < 0) { ok = false; break; }
		if(!n) break;
		bytes.Cat(buffer, (int)n);
		if(bytes.GetCount() > limit) ok = false;
	}
	if(close(fd) != 0) ok = false;
	return ok;
}

bool HasConflictingCreateIdentity(int dir_fd, const String& prefix, int depth,
	                                         const String& workspace_id, const String& record_id,
	                                         const String& expected_path, bool& conflict,
	                                         int& entries_scanned, int64& bytes_scanned)
{
	if(depth > DataStore::MAX_DEPTH) return false;
	int scan_fd = openat(dir_fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(scan_fd < 0) return false;
	DIR *dir = fdopendir(scan_fd);
	if(!dir) { close(scan_fd); return false; }
	bool ok = true;
	struct dirent *entry;
	while(ok && (entry = readdir(dir)) != NULL) {
		String name = entry->d_name;
		if(name == "." || name == ".." || name.StartsWith(".mgr-")) continue;
		if(++entries_scanned > DataStore::MAX_FILES) { ok = false; break; }
		String rel = prefix.IsEmpty() ? name : prefix + "/" + name;
		struct stat st;
		if(fstatat(dir_fd, name.ToStd().c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) { ok = false; break; }
		if(S_ISDIR(st.st_mode)) {
			int child = openat(dir_fd, name.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if(child < 0) { ok = false; break; }
			ok = HasConflictingCreateIdentity(child, rel, depth + 1, workspace_id, record_id, expected_path, conflict,
			                                  entries_scanned, bytes_scanned);
			close(child);
		}
		else if(S_ISREG(st.st_mode) && name.EndsWith(".json")) {
			if(st.st_size < 0 || st.st_size > DataStore::MAX_SOURCE_FILE_BYTES ||
			   bytes_scanned > DataStore::MAX_TOTAL_SOURCE_BYTES - st.st_size) { ok = false; break; }
			bytes_scanned += st.st_size;
			String bytes;
			int fd = openat(dir_fd, name.ToStd().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
			if(fd < 0) { ok = false; break; }
			struct stat opened;
			ok = fstat(fd, &opened) == 0 && S_ISREG(opened.st_mode) && opened.st_dev == st.st_dev && opened.st_ino == st.st_ino &&
			     opened.st_size == st.st_size && opened.st_size <= DataStore::MAX_SOURCE_FILE_BYTES;
			char buffer[8192];
			while(ok) {
				ssize_t n = read(fd, buffer, sizeof(buffer));
				if(n < 0 && errno == EINTR) continue;
				if(n < 0) { ok = false; break; }
				if(n == 0) break;
				bytes.Cat(buffer, (int)n);
				if(bytes.GetCount() > DataStore::MAX_SOURCE_FILE_BYTES) ok = false;
			}
			if(close(fd) != 0) ok = false;
			if(!ok) break;
			Value value;
			JsonFidelity fidelity = JsonFidelityScanner(bytes).Scan();
			if(!fidelity.syntax_ok || fidelity.duplicate_key || fidelity.trailing_input || fidelity.numeric_precision || fidelity.escaped_key) { ok = false; break; }
			try { value = ParseJSON(bytes); }
			catch(Exc) { ok = false; break; }
			if(value.IsError()) { ok = false; break; }
			if(value.Is<ValueMap>()) {
				ValueMap record = value, envelope = MapGet(record, "envelope");
				if(MapGetString(record, "id") == record_id && MapGetString(envelope, "workspace_id") == workspace_id && rel != expected_path)
					conflict = true;
			}
		}
	}
	if(closedir(dir) != 0) ok = false;
	return ok;
}

bool JournalArtifactsMatch(int op_fd, const JournalManifest& manifest)
{
	Index<String> expected;
	expected.Add("manifest.json");
	for(const JournalRecord& record : manifest.records) {
		if(!record.backup_ref.IsEmpty()) expected.Add(record.backup_ref);
		expected.Add(record.payload_ref);
	}
	int scan_fd = openat(op_fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(scan_fd < 0) return false;
	DIR *dir = fdopendir(scan_fd);
	if(!dir) { close(scan_fd); return false; }
	bool ok = true;
	Index<String> seen;
	struct dirent *entry;
	while((entry = readdir(dir)) != NULL) {
		String name = entry->d_name;
		if(name == "." || name == "..") continue;
		struct stat st;
		if(expected.Find(name) < 0 || fstatat(op_fd, name.ToStd().c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 ||
		   !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 || (st.st_mode & 077))
			ok = false;
		else seen.Add(name);
	}
	if(closedir(dir) != 0) ok = false;
	return ok && seen.GetCount() == expected.GetCount();
}

bool RemoveJournalContents(int op_fd, const JournalManifest& manifest)
{
	if(!JournalArtifactsMatch(op_fd, manifest)) return false;
	for(const JournalRecord& record : manifest.records) {
		if((!record.backup_ref.IsEmpty() && unlinkat(op_fd, record.backup_ref.ToStd().c_str(), 0) != 0) ||
		   unlinkat(op_fd, record.payload_ref.ToStd().c_str(), 0) != 0) return false;
	}
	if(unlinkat(op_fd, "manifest.json", 0) != 0) return false;
	return fsync(op_fd) == 0;
}


bool DiscardPreparedJournalLocked(int root_fd, const String& source_id,
	                                     const String& workspace_id, const String& operation_id)
{
	if(operation_id.IsEmpty() || operation_id.GetCount() > 64) return false;
	for(int i = 0; i < operation_id.GetCount(); i++) {
		char c = operation_id[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
	}
	int journal_fd = openat(root_fd, ".mgr-journal", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(journal_fd < 0) return false;
	int op_fd = openat(journal_fd, operation_id.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(op_fd < 0) { close(journal_fd); return false; }
	String bytes; JournalManifest manifest;
	bool valid = ReadJournalFile(op_fd, "manifest.json", 65536, bytes) && DataStore::ParseJournalManifest(bytes, manifest) &&
	             manifest.operation_id == operation_id && manifest.source_id == source_id &&
	             manifest.workspace_id == workspace_id && manifest.state == "prepared";
	bool clean = valid && JournalArtifactsMatch(op_fd, manifest) && RemoveJournalContents(op_fd, manifest);
	if(close(op_fd) != 0) clean = false;
	if(clean && (unlinkat(journal_fd, operation_id.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(journal_fd) != 0)) clean = false;
	if(close(journal_fd) != 0) clean = false;
	if(clean && (unlinkat(root_fd, ".mgr-journal", AT_REMOVEDIR) != 0 || fsync(root_fd) != 0)) clean = false;
	return clean;
}
#endif

ValueMap JournalManifestMap(const JournalManifest& manifest)
{
	ValueArray entries;
	for(const JournalRecord& item : manifest.records) {
		ValueMap row;
		row.Add("source_id", item.source_id); row.Add("record_id", item.record_id); row.Add("path", item.rel_path);
		if(manifest.version >= 2) row.Add("action", item.action);
		row.Add("expected_revision", item.expected_revision); row.Add("new_revision", item.new_revision);
		row.Add("backup_ref", item.backup_ref); row.Add("payload_ref", item.payload_ref);
		row.Add("old_hash", item.old_hash); row.Add("new_hash", item.new_hash); row.Add("state", item.state);
		entries.Add(row);
	}
	ValueMap root;
	root.Add("version", manifest.version); root.Add("operation_id", manifest.operation_id); root.Add("source_id", manifest.source_id);
	root.Add("workspace_id", manifest.workspace_id);
	root.Add("state", manifest.state); root.Add("records", entries);
	root.Add("request_digest", manifest.request_digest);
	return root;
}

#ifdef PLATFORM_POSIX
bool ReadCommitReceipts(int root_fd, Vector<CommitReceipt>& receipts, bool& exists)
{
	exists = false;
	struct stat st;
	if(fstatat(root_fd, ".mgr-commits", &st, AT_SYMLINK_NOFOLLOW) != 0) return errno == ENOENT;
	if(!S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077)) return false;
	int dir = openat(root_fd, ".mgr-commits", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(dir < 0) return false;
	String bytes;
	bool ok = ReadJournalFile(dir, "receipts.json", MAX_COMMIT_RECEIPTS_BYTES, bytes);
	close(dir);
	if(!ok) return false;
	exists = true;
	JsonFidelity fidelity = JsonFidelityScanner(bytes).Scan();
	if(!fidelity.syntax_ok || fidelity.duplicate_key || fidelity.trailing_input || fidelity.numeric_precision || fidelity.escaped_key) return false;
	Value parsed = ParseJSON(bytes);
	if(!parsed.Is<ValueMap>()) return false;
	ValueMap top = parsed;
	if(MapGet(top, "version") != 1) return false;
	Value rows_value = MapGet(top, "receipts");
	if(!rows_value.Is<ValueArray>()) return false;
	ValueArray rows = rows_value;
	if(rows.GetCount() > MAX_COMMIT_RECEIPTS) return false;
	Index<String> seen;
	for(const Value& row_value : rows) {
		if(!row_value.Is<ValueMap>()) return false;
		ValueMap row = row_value;
		CommitReceipt receipt;
		receipt.operation_id = MapGetString(row, "operation_id"); receipt.source_id = MapGetString(row, "source_id");
		receipt.workspace_id = MapGetString(row, "workspace_id"); receipt.digest = MapGetString(row, "request_digest");
		Value ids_value = MapGet(row, "changed_ids"), revisions_value = MapGet(row, "revisions");
		if(receipt.operation_id.IsEmpty() || receipt.operation_id.GetCount() > 64 || !IsSafeSourceId(receipt.source_id) ||
		   receipt.workspace_id.IsEmpty() || receipt.workspace_id.GetCount() > 256 || receipt.digest.GetCount() != 64 ||
		   !ids_value.Is<ValueArray>() || !revisions_value.Is<ValueArray>() || seen.Find(receipt.operation_id) >= 0) return false;
		for(int i = 0; i < receipt.operation_id.GetCount(); i++) {
			char c = receipt.operation_id[i];
			if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
		}
		for(int i = 0; i < receipt.digest.GetCount(); i++)
			if(!((receipt.digest[i] >= '0' && receipt.digest[i] <= '9') || (receipt.digest[i] >= 'a' && receipt.digest[i] <= 'f'))) return false;
		ValueArray id_array = ids_value, revision_array = revisions_value;
		if(id_array.IsEmpty() || id_array.GetCount() > 64 || id_array.GetCount() != revision_array.GetCount()) return false;
		for(int i = 0; i < id_array.GetCount(); i++) {
			if(!IsString(id_array[i]) || !IsString(revision_array[i]) || ((String)id_array[i]).IsEmpty() || ((String)revision_array[i]).IsEmpty()) return false;
			receipt.ids.Add(id_array[i]); receipt.revisions.Add(revision_array[i]);
		}
		seen.Add(receipt.operation_id); receipts.Add(pick(receipt));
	}
	return true;
}

bool StoreCommitReceipt(int root_fd, const CommitReceipt& receipt, DataWriteTestFailpoint failpoint, bool dry_run)
{
	Vector<CommitReceipt> receipts; bool exists = false;
	if(!ReadCommitReceipts(root_fd, receipts, exists)) return false;
	for(const CommitReceipt& prior : receipts)
			if(prior.operation_id == receipt.operation_id) {
				if(prior.source_id != receipt.source_id || prior.workspace_id != receipt.workspace_id || prior.digest != receipt.digest ||
				   prior.ids.GetCount() != receipt.ids.GetCount() || prior.revisions.GetCount() != receipt.revisions.GetCount()) return false;
				for(int i = 0; i < prior.ids.GetCount(); i++) if(prior.ids[i] != receipt.ids[i] || prior.revisions[i] != receipt.revisions[i]) return false;
				return true;
			}
	if(receipts.GetCount() >= MAX_COMMIT_RECEIPTS) return false;
	struct stat st;
	int dir = -1;
	if(exists || !dry_run) {
		if(!exists && (mkdirat(root_fd, ".mgr-commits", 0700) != 0 || fsync(root_fd) != 0)) return false;
		dir = openat(root_fd, ".mgr-commits", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if(dir < 0) return false;
		if(fstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077)) { close(dir); return false; }
	}
	receipts.Add(receipt);
	ValueArray rows;
	for(const CommitReceipt& item : receipts) {
		ValueArray ids, revisions;
		for(const String& id : item.ids) ids.Add(id);
		for(const String& rev : item.revisions) revisions.Add(rev);
		ValueMap row; row.Add("operation_id", item.operation_id); row.Add("source_id", item.source_id);
		row.Add("workspace_id", item.workspace_id); row.Add("request_digest", item.digest);
		row.Add("changed_ids", ids); row.Add("revisions", revisions); rows.Add(row);
	}
	ValueMap top; top.Add("version", 1); top.Add("receipts", rows);
	String bytes = AsJSON(top, true);
	bool ok = bytes.GetCount() <= MAX_COMMIT_RECEIPTS_BYTES;
	if(ok && !dry_run)
		ok = failpoint != DATA_WRITE_FAIL_BATCH_COMMIT_MANIFEST &&
		     AtomicReplaceRecord(dir, "receipts.json", bytes, DATA_WRITE_FAIL_NONE) == ATOMIC_REPLACE_COMMITTED && fsync(dir) == 0;
	if(dir >= 0 && close(dir) != 0) ok = false;
	return ok;
}
#endif
END_UPP_NAMESPACE
