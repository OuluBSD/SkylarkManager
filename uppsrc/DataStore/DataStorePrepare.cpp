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

JournalStageResult DataStore::PrepareJournal(const DataWriteAuthorization& authorization,
	                                            const String& operation_id,
	                                            const Vector<JournalStageRecord>& records,
	                                            DataWriteTestFailpoint test_failpoint)
{
	JournalStageResult result;
	result.status = "invalid_request";
	if(source_read_only || !source_write_enabled || base_dir.IsEmpty() || !authorization.IsAllowed() ||
	   authorization.action_scope != "record:write" || authorization.source_id != source_id ||
	   authorization.workspace_id.IsEmpty()) return result;
	CanonicalWriteGuard write_guard;
#ifndef PLATFORM_POSIX
	result.status = "unsupported_platform";
	return result;
#else
	if(!IsSafeRoot(base_dir) || !IsSafeSourceId(source_id)) { result.status = "source_unavailable"; return result; }
	LockedSourceRoot root;
	if(!root.Open(base_dir)) { result.status = "source_unavailable"; return result; }
	if(!HasCurrentWriteRegistration()) { result.status = "source_capability_changed"; return result; }
	return PrepareJournalLocked(authorization, operation_id, records, root.root_fd, test_failpoint);
#endif
}

JournalStageResult DataStore::PrepareJournalLocked(const DataWriteAuthorization& authorization,
	                                               const String& operation_id,
	                                               const Vector<JournalStageRecord>& records,
	                                               int root_fd,
	                                               DataWriteTestFailpoint test_failpoint) const
{
	JournalStageResult result;
	result.status = "invalid_request";
	if(source_read_only || !source_write_enabled || base_dir.IsEmpty() || !authorization.IsAllowed() ||
	   authorization.action_scope != "record:write" || authorization.source_id != source_id ||
	   authorization.workspace_id.IsEmpty() || records.IsEmpty() || records.GetCount() > 64 ||
	   operation_id.IsEmpty() || operation_id.GetCount() > 64) return result;
	for(int i = 0; i < operation_id.GetCount(); i++) {
		char c = operation_id[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return result;
	}
	for(const JournalStageRecord& record : records)
		if((record.action != "update" && record.action != "create") ||
		   (record.action == "update" && record.old_payload.IsEmpty()) ||
		   (record.action == "create" && !record.old_payload.IsEmpty()) || record.new_payload.IsEmpty() ||
		   record.old_payload.GetCount() > MAX_SOURCE_FILE_BYTES || record.new_payload.GetCount() > MAX_SOURCE_FILE_BYTES)
			return result;
#ifndef PLATFORM_POSIX
	result.status = "unsupported_platform";
	return result;
#else
	if(!IsSafeRoot(base_dir) || !IsSafeSourceId(source_id)) { result.status = "source_unavailable"; return result; }
	if(!HasCurrentWriteRegistration()) { result.status = "source_capability_changed"; return result; }
	const String journal_name = ".mgr-journal";
	struct stat st;
	if(fstatat(root_fd, journal_name.ToStd().c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0) {
		result.status = S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 077) ?
		                "journal_recovery_required" : "journal_cleanup_required";
		return result;
	}
	if(errno != ENOENT) { result.status = "journal_cleanup_required"; return result; }
	bool created_journal_dir = mkdirat(root_fd, journal_name.ToStd().c_str(), 0700) == 0;
	if(!created_journal_dir) {
		if(errno == EEXIST) { result.status = "journal_recovery_required"; return result; }
		result.status = "stage_failed"; return result;
	}
	int journal_fd = openat(root_fd, journal_name.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(journal_fd < 0 || fstat(journal_fd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077) != 0) {
		bool clean = true;
		if(journal_fd >= 0 && close(journal_fd) != 0) clean = false;
		if(unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0) clean = false;
		result.status = clean ? "stage_failed" : "journal_cleanup_required"; return result;
	}
	if(test_failpoint == DATA_WRITE_FAIL_JOURNAL_ROOT_SYNC || fsync(root_fd) != 0) {
		bool clean = close(journal_fd) == 0;
		if(created_journal_dir) {
			if(unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0) clean = false;
		}
		result.status = clean ? "stage_failed" : "journal_cleanup_required";
		return result;
	}
	if(mkdirat(journal_fd, operation_id.ToStd().c_str(), 0700) != 0) {
		int mkdir_error = errno;
		bool clean = close(journal_fd) == 0;
		if(mkdir_error == EEXIST) result.status = "journal_recovery_required";
		else {
			if(created_journal_dir && (unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0)) clean = false;
			result.status = clean ? "stage_failed" : "journal_cleanup_required";
		}
		return result;
	}
	int op_fd = openat(journal_fd, operation_id.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(op_fd < 0 || fstat(op_fd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077) != 0) {
		bool clean = true;
		if(op_fd >= 0 && close(op_fd) != 0) clean = false;
		if(unlinkat(journal_fd, operation_id.ToStd().c_str(), AT_REMOVEDIR) != 0) clean = false;
		if(fsync(journal_fd) != 0 || close(journal_fd) != 0) clean = false;
		if(created_journal_dir && (unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0)) clean = false;
		result.status = clean ? "stage_failed" : "journal_cleanup_required"; return result;
	}
	if(test_failpoint == DATA_WRITE_FAIL_JOURNAL_OPERATION_SYNC || fsync(journal_fd) != 0) {
		bool clean = close(op_fd) == 0;
		if(unlinkat(journal_fd, operation_id.ToStd().c_str(), AT_REMOVEDIR) != 0) clean = false;
		if(fsync(journal_fd) != 0 || close(journal_fd) != 0) clean = false;
		if(created_journal_dir && (unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0)) clean = false;
		result.status = clean ? "stage_failed" : "journal_cleanup_required";
		return result;
	}
	// Durability order: journal root, operation directory entry, payloads and their
	// entries, then this directory sync, followed by atomic prepared-manifest publish
	// (AtomicReplaceRecord syncs the operation directory before success).
	auto cleanup = [&]() -> bool {
		bool clean = true;
		for(int n = 0; n < records.GetCount(); n++) {
			String index = AsString(n);
			String backup = "backup-" + index + ".bin", payload = "payload-" + index + ".bin";
			if(!(test_failpoint == DATA_WRITE_FAIL_JOURNAL_CLEANUP && n == 0) && unlinkat(op_fd, backup.ToStd().c_str(), 0) != 0 && errno != ENOENT) clean = false;
			if(unlinkat(op_fd, payload.ToStd().c_str(), 0) != 0 && errno != ENOENT) clean = false;
		}
		if(unlinkat(op_fd, "manifest.json", 0) != 0 && errno != ENOENT) clean = false;
		if(fsync(op_fd) != 0) clean = false;
		if(close(op_fd) != 0) clean = false;
		if(unlinkat(journal_fd, operation_id.ToStd().c_str(), AT_REMOVEDIR) != 0) clean = false;
		if(fsync(journal_fd) != 0) clean = false;
		if(close(journal_fd) != 0) clean = false;
			if(created_journal_dir && (unlinkat(root_fd, journal_name.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(root_fd) != 0)) clean = false;
		return clean;
	};
	auto fail_stage = [&](const String& status) {
		result.status = cleanup() ? status : "journal_cleanup_required";
		return result;
	};
	JournalManifest manifest;
	manifest.version = 1; manifest.operation_id = operation_id; manifest.source_id = source_id;
	manifest.workspace_id = authorization.workspace_id; manifest.state = "prepared";
	int64 total = 0;
	Index<String> staged_create_ids, staged_create_paths;
	for(int n = 0; n < records.GetCount(); n++) {
		const JournalStageRecord& input = records[n];
		const DataDocument* doc = FindDocument(input.qualified_id);
		String rel_path = input.rel_path;
		if(input.action == "create") {
			Value created_value = ParseJSON(input.new_payload);
			if(doc || !created_value.Is<ValueMap>() || !input.expected_revision.IsEmpty()) return fail_stage("identity_conflict");
			ValueMap created = created_value;
			String kind = MapGetString(created, "kind"), id = MapGetString(created, "id");
			if(id.IsEmpty() || id.Find('/') >= 0 || id.Find('\\') >= 0 || id.Find("..") >= 0 ||
			   input.qualified_id != authorization.workspace_id + ":" + id || input.new_revision.IsEmpty()) return fail_stage("identity_or_schema_invalid");
			DataDocument created_doc; created_doc.data = created; created_doc.id = id; created_doc.kind = kind;
			ValidateDocument(created_doc, FindSchemaCard(kind));
			if(!created_doc.valid || !created_doc.validated || created_doc.workspace_id != authorization.workspace_id ||
			   created_doc.revision != "1" || input.new_revision != "1")
				return fail_stage("identity_or_schema_invalid");
			rel_path = CreateJournalPath(input.qualified_id);
			if(rel_path.IsEmpty() || staged_create_ids.Find(input.qualified_id) >= 0 || staged_create_paths.Find(rel_path) >= 0)
				return fail_stage("identity_conflict");
			staged_create_ids.Add(input.qualified_id); staged_create_paths.Add(rel_path);
			for(const DataDocument& existing : docs)
				if(existing.id == id && existing.workspace_id == authorization.workspace_id) return fail_stage("identity_conflict");
			bool identity_collision = false;
			int entries_scanned = 0; int64 bytes_scanned = 0;
			if(!HasConflictingCreateIdentity(root_fd, "", 0, authorization.workspace_id, id, rel_path,
			                                 identity_collision, entries_scanned, bytes_scanned))
				return fail_stage("identity_scan_unavailable");
			if(identity_collision) return fail_stage("identity_conflict");
			manifest.version = 2;
		}
		else if(!doc || !doc->valid || !doc->validated || doc->workspace_id != authorization.workspace_id || doc->rel_path != rel_path ||
		        input.expected_revision != doc->revision || input.new_revision.IsEmpty()) return fail_stage("scope_or_revision_denied");
		int parent = -1; String leaf;
		if(!OpenRecordParent(root_fd, rel_path, parent, leaf)) return fail_stage("invalid_path");
		String actual; bool read_ok = ReadRecordAt(parent, leaf, actual);
		if(input.action == "create") {
			struct stat target_stat;
			bool absent = fstatat(parent, leaf.ToStd().c_str(), &target_stat, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
			close(parent);
			if(!absent) return fail_stage("conflict");
		}
		else { close(parent); if(!read_ok || actual != input.old_payload) return fail_stage("conflict"); }
		total += input.old_payload.GetCount() + input.new_payload.GetCount();
		if(total > 64LL * MAX_SOURCE_FILE_BYTES) return fail_stage("size_limit");
		JournalRecord item;
		item.action = input.action;
		item.source_id = source_id; item.record_id = input.action == "create" ? input.qualified_id.Mid(authorization.workspace_id.GetCount() + 1) : doc->id; item.rel_path = rel_path;
		item.expected_revision = input.expected_revision; item.new_revision = input.new_revision;
		item.backup_ref = input.action == "create" ? String() : "backup-" + AsString(n) + ".bin"; item.payload_ref = "payload-" + AsString(n) + ".bin";
		item.old_hash = input.action == "create" ? String() : ContentDigest(input.old_payload); item.new_hash = ContentDigest(input.new_payload); item.state = "pending";
		manifest.records.Add(item);
		if(test_failpoint == DATA_WRITE_FAIL_JOURNAL_PAYLOAD ||
		   (!item.backup_ref.IsEmpty() && AtomicReplaceRecord(op_fd, item.backup_ref, input.old_payload, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) ||
		   AtomicReplaceRecord(op_fd, item.payload_ref, input.new_payload, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED ||
		   test_failpoint == DATA_WRITE_FAIL_JOURNAL_CLEANUP) {
			return fail_stage("stage_failed");
		}
	}
	if(test_failpoint == DATA_WRITE_FAIL_JOURNAL_PRE_MANIFEST_SYNC || fsync(op_fd) != 0)
		return fail_stage("stage_failed");
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
	ValueMap root_map;
	root_map.Add("version", manifest.version); root_map.Add("operation_id", operation_id); root_map.Add("source_id", source_id);
	root_map.Add("workspace_id", authorization.workspace_id);
	root_map.Add("state", "prepared"); root_map.Add("records", entries);
	root_map.Add("request_digest", "");
	String manifest_bytes = AsJSON(root_map, true);
	JournalManifest verified;
	if(test_failpoint == DATA_WRITE_FAIL_JOURNAL_MANIFEST || manifest_bytes.GetCount() > 65536 || !ParseJournalManifest(manifest_bytes, verified) ||
	   AtomicReplaceRecord(op_fd, "manifest.json", manifest_bytes, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) {
		return fail_stage("stage_failed");
	}
	bool op_close_ok = close(op_fd) == 0;
	bool journal_close_ok = close(journal_fd) == 0;
	result.operation_id = operation_id;
	result.status = test_failpoint == DATA_WRITE_FAIL_JOURNAL_DIRECTORY_CLOSE || !op_close_ok || !journal_close_ok ?
	                "prepared_uncertain" : "prepared";
	return result;
#endif
}


JournalRecoveryResult DataStore::RecoverPendingBatches(const DataWriteAuthorization& authorization,
	                                                      DataWriteTestFailpoint test_failpoint) const
{
	JournalRecoveryResult result;
	result.status = "invalid_request";
	if(source_read_only || !source_write_enabled || base_dir.IsEmpty() || !authorization.IsAllowed() ||
	   authorization.action_scope != "record:write" || authorization.source_id != source_id || authorization.workspace_id.IsEmpty())
		return result;
	if(!IsSafeRoot(base_dir) || !IsSafeSourceId(source_id)) { result.status = "source_unavailable"; return result; }
	Mutex::Lock lock(s_canonical_write_mutex);
#ifndef PLATFORM_POSIX
	result.status = "unsupported_platform";
	return result;
#else
	if(!synthetic_write_source) {
		DataSourceRegistration authoritative;
		if(!ReadConfiguredRegistration(authoritative) || !authoritative.enabled || authoritative.read_only || !authoritative.write_enabled ||
		   NormalizePath(authoritative.root) != base_dir || authoritative.id != source_id) {
			result.status = "source_capability_changed"; return result;
		}
	}
	LockedSourceRoot root;
	if(!root.Open(base_dir)) { result.status = "source_unavailable"; return result; }
	struct stat journal_st;
	if(fstatat(root.root_fd, ".mgr-journal", &journal_st, AT_SYMLINK_NOFOLLOW) != 0) {
		result.status = errno == ENOENT ? "clean" : "journal_conflict"; return result;
	}
	if(!S_ISDIR(journal_st.st_mode) || journal_st.st_uid != geteuid() || (journal_st.st_mode & 077)) {
		result.status = "journal_conflict"; return result;
	}
	int journal_fd = openat(root.root_fd, ".mgr-journal", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if(journal_fd < 0) { result.status = "journal_conflict"; return result; }
	int scan_fd = dup(journal_fd);
	DIR *journal = scan_fd >= 0 ? fdopendir(scan_fd) : NULL;
	if(!journal) { if(scan_fd >= 0) close(scan_fd); close(journal_fd); result.status = "journal_conflict"; return result; }
	bool failed = false;
	bool scope_denied = false;
	bool orphan_unrecognized = false;
	struct dirent *entry;
	while(!failed && (entry = readdir(journal)) != NULL) {
		String op_id = entry->d_name;
		if(op_id == "." || op_id == "..") continue;
		if(op_id.IsEmpty() || op_id.GetCount() > 64) { failed = true; break; }
		for(int i = 0; i < op_id.GetCount(); i++) {
			char c = op_id[i];
			if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) failed = true;
		}
		struct stat op_st;
		if(failed || fstatat(journal_fd, op_id.ToStd().c_str(), &op_st, AT_SYMLINK_NOFOLLOW) != 0 ||
		   !S_ISDIR(op_st.st_mode) || op_st.st_uid != geteuid() || (op_st.st_mode & 077)) { failed = true; break; }
		int op_fd = openat(journal_fd, op_id.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if(op_fd < 0) { failed = true; break; }
		String manifest_bytes;
		bool has_manifest = fstatat(op_fd, "manifest.json", &op_st, AT_SYMLINK_NOFOLLOW) == 0;
		if(!has_manifest) {
			close(op_fd);
			orphan_unrecognized = true; failed = true; break;
		}
		JournalManifest manifest;
		if(!ReadJournalFile(op_fd, "manifest.json", 65536, manifest_bytes) || !ParseJournalManifest(manifest_bytes, manifest)) {
			close(op_fd); orphan_unrecognized = true; failed = true; break;
		}
		if(manifest.operation_id != op_id || manifest.source_id != source_id) { close(op_fd); failed = true; break; }
		if(manifest.workspace_id != authorization.workspace_id) {
			close(op_fd); scope_denied = true; failed = true; break;
		}
		if(!JournalArtifactsMatch(op_fd, manifest)) { close(op_fd); orphan_unrecognized = true; failed = true; break; }
		Vector<String> old_payloads, new_payloads, current_bytes;
		for(const JournalRecord& rec : manifest.records) {
			String old_bytes, new_bytes;
			if((rec.action == "update" && !ReadJournalFile(op_fd, rec.backup_ref, MAX_SOURCE_FILE_BYTES, old_bytes)) ||
			   !ReadJournalFile(op_fd, rec.payload_ref, MAX_SOURCE_FILE_BYTES, new_bytes) ||
			   (rec.action == "update" && ContentDigest(old_bytes) != rec.old_hash) || ContentDigest(new_bytes) != rec.new_hash) { failed = true; break; }
			Value new_value = ParseJSON(new_bytes);
			if(!new_value.Is<ValueMap>()) { failed = true; break; }
			ValueMap new_map = new_value;
			DataDocument new_doc; new_doc.data = new_map;
			new_doc.id = MapGetString(new_map, "id"); new_doc.kind = MapGetString(new_map, "kind");
			if(new_doc.id != rec.record_id || new_doc.kind.IsEmpty()) { failed = true; break; }
			const SchemaCard *schema = FindSchemaCard(new_doc.kind);
			if(!schema) { failed = true; break; }
			ValidateDocument(new_doc, schema);
			if(!new_doc.valid || !new_doc.validated || new_doc.workspace_id != manifest.workspace_id || new_doc.revision != rec.new_revision ||
			   (rec.action == "create" && rec.new_revision != "1")) {
				failed = true; break;
			}
			if(rec.action == "update") {
				Value old_value = ParseJSON(old_bytes);
				if(!old_value.Is<ValueMap>()) { failed = true; break; }
				ValueMap old_map = old_value; DataDocument old_doc; old_doc.data = old_map;
				old_doc.id = MapGetString(old_map, "id"); old_doc.kind = MapGetString(old_map, "kind");
				ValidateDocument(old_doc, schema);
				if(!old_doc.valid || !old_doc.validated || old_doc.id != rec.record_id || old_doc.kind != new_doc.kind ||
				   old_doc.workspace_id != manifest.workspace_id || old_doc.revision != rec.expected_revision) { failed = true; break; }
			}
			else {
				if(rec.action != "create" || manifest.version != 2 || !old_bytes.IsEmpty() ||
				   rec.rel_path != CreateJournalPath(manifest.workspace_id + ":" + rec.record_id)) { failed = true; break; }
			}
			int parent = -1; String leaf, actual;
			if(!OpenRecordParent(root.root_fd, rec.rel_path, parent, leaf)) { failed = true; break; }
			bool read_ok = ReadRecordAt(parent, leaf, actual);
			struct stat target_stat;
			bool absent = fstatat(parent, leaf.ToStd().c_str(), &target_stat, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
			close(parent);
			if(rec.action == "create" ? (!read_ok && !absent) : !read_ok) { failed = true; break; }
			if(rec.action == "create" && read_ok && ContentDigest(actual) != rec.new_hash) { failed = true; break; }
			if(rec.action == "create") {
				bool collision = false;
				int entries_scanned = 0; int64 bytes_scanned = 0;
				if(!HasConflictingCreateIdentity(root.root_fd, "", 0, manifest.workspace_id, rec.record_id, rec.rel_path,
				                                 collision, entries_scanned, bytes_scanned) || collision) {
					failed = true; break;
				}
			}
			old_payloads.Add(old_bytes); new_payloads.Add(new_bytes); current_bytes.Add(actual);
		}
		if(failed) { close(op_fd); break; }
		if(manifest.state == "rolled-back") {
			for(int i = 0; i < current_bytes.GetCount(); i++)
				if(manifest.records[i].action == "create" ? !current_bytes[i].IsEmpty() : current_bytes[i] != old_payloads[i]) failed = true;
		}
		else if(manifest.state != "prepared") {
			for(int i = 0; i < current_bytes.GetCount(); i++)
				if(manifest.records[i].action == "create" ? (!current_bytes[i].IsEmpty() && current_bytes[i] != new_payloads[i]) :
				   (current_bytes[i] != old_payloads[i] && current_bytes[i] != new_payloads[i])) failed = true;
		}
		if(failed || test_failpoint == DATA_WRITE_FAIL_RECOVERY_BEFORE_RECORD) { close(op_fd); failed = true; break; }
		bool rollback = manifest.state == "committing" || manifest.state == "rolled-back";
		if(manifest.state != "prepared") {
			for(int i = 0; i < manifest.records.GetCount(); i++) {
				if(manifest.records[i].action == "create" && rollback) {
					if(current_bytes[i].IsEmpty()) continue;
					int parent = -1; String leaf;
					if(!OpenRecordParent(root.root_fd, manifest.records[i].rel_path, parent, leaf) ||
					   current_bytes[i] != new_payloads[i] || unlinkat(parent, leaf.ToStd().c_str(), 0) != 0 || fsync(parent) != 0) {
						if(parent >= 0) close(parent); failed = true; break;
					}
					close(parent); result.recovered_records++;
					if(test_failpoint == DATA_WRITE_FAIL_RECOVERY_AFTER_RECORD) { failed = true; break; }
					continue;
				}
				const String& target = rollback ? old_payloads[i] : new_payloads[i];
				if(current_bytes[i] == target) continue;
				int parent = -1; String leaf;
				AtomicReplaceOutcome publish = ATOMIC_REPLACE_FAILED;
				if(OpenRecordParent(root.root_fd, manifest.records[i].rel_path, parent, leaf))
					publish = manifest.records[i].action == "create" ? AtomicCreateRecord(parent, leaf, target) :
					          AtomicReplaceRecord(parent, leaf, target, DATA_WRITE_FAIL_NONE);
				if(parent < 0 || publish != ATOMIC_REPLACE_COMMITTED) {
					if(parent >= 0) close(parent); failed = true; break;
				}
				close(parent); result.recovered_records++;
				if(test_failpoint == DATA_WRITE_FAIL_RECOVERY_AFTER_RECORD) { failed = true; break; }
			}
		}
		if(failed) { close(op_fd); failed = true; break; }
		if(manifest.state == "committing") {
			manifest.state = "rolled-back";
			for(JournalRecord& rec : manifest.records) rec.state = "rolled-back";
			String rolled_back_bytes = AsJSON(JournalManifestMap(manifest), true);
			if(AtomicReplaceRecord(op_fd, "manifest.json", rolled_back_bytes, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) {
				close(op_fd); failed = true; break;
			}
		}
		if(manifest.state == "committed") {
			CommitReceipt receipt;
			receipt.operation_id = manifest.operation_id; receipt.source_id = manifest.source_id;
			receipt.workspace_id = manifest.workspace_id; receipt.digest = manifest.request_digest;
			for(const JournalRecord& rec : manifest.records) {
				receipt.ids.Add(manifest.workspace_id + ":" + rec.record_id);
				receipt.revisions.Add(rec.new_revision);
			}
			if(!StoreCommitReceipt(root.root_fd, receipt, DATA_WRITE_FAIL_NONE)) { close(op_fd); failed = true; break; }
		}
		if(test_failpoint == DATA_WRITE_FAIL_RECOVERY_BEFORE_CLEANUP) { close(op_fd); failed = true; break; }
		bool contents_removed = RemoveJournalContents(op_fd, manifest);
		bool op_closed = close(op_fd) == 0;
		if(!contents_removed || !op_closed) { failed = true; break; }
		if(unlinkat(journal_fd, op_id.ToStd().c_str(), AT_REMOVEDIR) != 0 || fsync(journal_fd) != 0) {
			failed = true; break;
		}
	}
	if(closedir(journal) != 0) failed = true;
	if(!failed) {
		if(fsync(journal_fd) != 0) failed = true;
		if(!failed) {
			if(unlinkat(root.root_fd, ".mgr-journal", AT_REMOVEDIR) != 0) failed = true;
		}
		if(!failed) {
			if(fsync(root.root_fd) != 0) failed = true;
		}
	}
	if(close(journal_fd) != 0) failed = true;
	result.status = failed ? (orphan_unrecognized ? "journal_orphan_unrecognized" : scope_denied ? "scope_denied" : "recovery_conflict") : "recovered";
	return result;
#endif
}
END_UPP_NAMESPACE
