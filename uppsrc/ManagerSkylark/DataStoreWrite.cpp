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

DataBatchResult DataStore::PublishBatch(const DataWriteAuthorization& authorization,
	                                    const String& operation_id,
	                                    const Vector<DataBatchChange>& changes,
	                                    DataWriteTestFailpoint test_failpoint) const
{
	DataBatchResult result;
	result.operation_id = operation_id;
	result.status = "invalid_request";
	if(source_read_only || !source_write_enabled || !authorization.IsAllowed() || authorization.source_id != source_id ||
	   changes.IsEmpty() || changes.GetCount() > 64 || operation_id.IsEmpty() || operation_id.GetCount() > 64)
		return result;
	if(source_incomplete || source_errors || authorization.workspace_id.IsEmpty()) { result.status = "source_incomplete"; return result; }
	CanonicalWriteGuard write_guard;
#ifndef PLATFORM_POSIX
	result.status = "unsupported_platform";
	return result;
#else
	if(!IsSafeRoot(base_dir) || !IsSafeSourceId(source_id)) { result.status = "source_unavailable"; return result; }
	LockedSourceRoot root;
	if(!root.Open(base_dir)) { result.status = "source_unavailable"; return result; }
	if(synthetic_write_source && (test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_REVOKE_BEFORE_RECEIPT ||
	   test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_REDIRECT_BEFORE_RECEIPT)) {
		DataSourceRegistration revoked = synthetic_registration;
		if(test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_REVOKE_BEFORE_RECEIPT) revoked.write_enabled = false;
		else revoked.root = base_dir + "-redirected";
		SetSyntheticRegistrationForTests(revoked);
	}
	if(!HasCurrentWriteRegistration()) { result.status = "source_capability_changed"; return result; }
	struct stat pending_stat;
	if(fstatat(root.root_fd, ".mgr-journal", &pending_stat, AT_SYMLINK_NOFOLLOW) == 0) {
		result.status = "journal_recovery_required"; return result;
	}
	if(errno != ENOENT) { result.status = "journal_conflict"; return result; }
	for(const DataBatchChange& change : changes) {
		if(change.action != DATA_BATCH_UPDATE && change.action != DATA_BATCH_CREATE) return result;
		if(change.action == DATA_BATCH_CREATE) {
			if(!change.expected_revision.IsEmpty() || !change.expected_source_revision.IsEmpty()) return result;
		}
		else if(change.expected_revision.IsEmpty() || change.expected_source_revision.IsEmpty()) return result;
	}
	DataStore current;
	if(!current.LoadRoot(base_dir, source_id, source_write_enabled, synthetic_write_source) ||
	   current.source_incomplete || current.source_errors || current.base_dir != base_dir || current.source_id != source_id) {
		result.status = "source_incomplete"; return result;
	}
	if(!HasCurrentWriteRegistration()) { result.status = "source_capability_changed"; return result; }
	Vector<String> request_parts;
	for(const DataBatchChange& change : changes) {
		String part = change.qualified_id + "\n" + change.expected_revision + "\n" + change.expected_source_revision + "\n" + AsJSON(change.proposed, true);
		// Keep the legacy update bytes unchanged; distinguish create requests explicitly.
		if(change.action == DATA_BATCH_CREATE) part = "create\n" + part;
		request_parts.Add(part);
	}
	Sort(request_parts);
	String canonical_request;
	for(const String& part : request_parts) canonical_request << part << "\n";
	String request_digest = ContentDigest(canonical_request);
	for(int i = 0; i < operation_id.GetCount(); i++) {
		char c = operation_id[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return result;
	}
	Vector<CommitReceipt> receipts; bool receipt_file_exists = false;
	if(!ReadCommitReceipts(root.root_fd, receipts, receipt_file_exists)) { result.status = "receipt_store_corrupt"; return result; }
	for(const CommitReceipt& receipt : receipts) if(receipt.operation_id == operation_id) {
		if(receipt.source_id != source_id || receipt.workspace_id != authorization.workspace_id || receipt.digest != request_digest) {
			result.status = "idempotency_conflict"; return result;
		}
		result.changed_ids = clone(receipt.ids); result.revisions = clone(receipt.revisions);
		result.status = "already_committed"; return result;
	}
	if(receipts.GetCount() >= MAX_COMMIT_RECEIPTS) { result.status = "receipt_store_full"; return result; }
	Index<String> ids;
	Vector<JournalStageRecord> stages;
	Vector<ValueMap> candidates;
	struct ReferenceSnapshot : Moveable<ReferenceSnapshot> { String qualified_id, rel_path, revision; };
	Vector<ReferenceSnapshot> reference_snapshots;
	for(const DataBatchChange& change : changes) {
		bool create = change.action == DATA_BATCH_CREATE;
		const DataDocument* doc = current.FindDocument(change.qualified_id);
		if(ids.Find(change.qualified_id) >= 0) { result.status = "identity_conflict"; return result; }
		if(create) {
			if(doc || !change.qualified_id.StartsWith(authorization.workspace_id + ":")) { result.status = "identity_conflict"; return result; }
		}
		else if(!doc || !doc->valid || !doc->validated || doc->source_id != source_id ||
		        doc->workspace_id != authorization.workspace_id || doc->revision != change.expected_revision ||
		        doc->source_revision != change.expected_source_revision) {
			result.status = "prevalidation_failed"; return result;
		}
		ids.Add(change.qualified_id);
		ValueMap candidate = change.proposed;
		Value env_value = MapGet(candidate, "envelope");
		if(!env_value.Is<ValueMap>()) { result.status = "identity_mismatch"; return result; }
		ValueMap env = env_value;
		String id = MapGetString(candidate, "id"), kind = MapGetString(candidate, "kind");
		if(id.IsEmpty() || kind.IsEmpty() ||
		   (create ? (change.qualified_id != authorization.workspace_id + ":" + id ||
		              MapGetString(env, "workspace_id") != authorization.workspace_id ||
		              MapGetString(env, "stable_id") != id || MapGetString(env, "record_kind") != kind ||
		              MapGetString(env, "revision") != "1") :
		             (id != doc->id || kind != doc->kind || MapGetString(env, "workspace_id") != doc->workspace_id ||
		              MapGetString(env, "stable_id") != doc->id || MapGetString(env, "record_kind") != doc->kind ||
		              MapGetString(env, "revision") != doc->revision))) {
			result.status = "identity_mismatch"; return result;
		}
		String next_revision;
		if(create) next_revision = "1";
		else {
			int64 rev = 0;
			for(int i = 0; i < doc->revision.GetCount(); i++) {
				char c = doc->revision[i];
				if(c < '0' || c > '9' || rev > (INT64_MAX - (c - '0')) / 10) { result.status = "invalid_revision"; return result; }
				rev = rev * 10 + c - '0';
			}
			if(rev == INT64_MAX) { result.status = "invalid_revision"; return result; }
			next_revision = AsString(rev + 1);
		}
		env.Set("revision", next_revision);
		candidate.Set("envelope", env);
		DataDocument proposed;
		proposed.data = candidate; proposed.id = id; proposed.kind = kind;
		current.ValidateDocument(proposed, current.FindSchemaCard(kind));
		if(!proposed.valid || !proposed.validated) { result.status = "invalid_schema"; return result; }
		JournalStageRecord stage;
		stage.action = create ? "create" : "update";
		stage.qualified_id = change.qualified_id;
		stage.rel_path = create ? CreateJournalPath(change.qualified_id) : doc->rel_path;
		stage.expected_revision = create ? String() : doc->revision; stage.new_revision = next_revision;
		if(!create) {
			int old_parent = -1; String old_leaf, old_bytes;
			if(!OpenRecordParent(root.root_fd, doc->rel_path, old_parent, old_leaf) || !ReadRecordAt(old_parent, old_leaf, old_bytes)) {
				if(old_parent >= 0) close(old_parent);
				result.status = "conflict"; return result;
			}
			close(old_parent);
			if(RecordSourceRevision(doc->rel_path, old_bytes) != change.expected_source_revision) { result.status = "conflict"; return result; }
			stage.old_payload = old_bytes;
		}
		stage.new_payload = AsJSON(candidate, true);
		stages.Add(stage); candidates.Add(candidate);
	}
	// Resolve each reference against the projected set; retain outside targets for
	// exact lock-time rechecking after journal preparation.
	for(int i = 0; i < stages.GetCount(); i++) {
		const SchemaCard* schema = current.FindSchemaCard(MapGetString(candidates[i], "kind"));
		if(!schema) { result.status = "invalid_schema"; return result; }
		for(const SchemaCard::Field& field : schema->fields) {
			if(field.reference_kind.IsEmpty()) continue;
			Value value;
			if(!GetTypedField(candidates[i], field.name, value) || !IsString(value)) { result.status = "invalid_reference"; return result; }
			int matches = 0; String target_kind; bool target_valid = false;
			for(int n = 0; n < candidates.GetCount(); n++) {
				if(n == i || stages[n].action != "create" || MapGetString(candidates[n], "id") != (String)value) continue;
				if(MapGetString(MapGet(candidates[n], "envelope"), "workspace_id") == authorization.workspace_id) {
					matches++; target_kind = MapGetString(candidates[n], "kind"); target_valid = true;
				}
			}
			for(const DataDocument& existing : current.docs) {
				bool replaced = false;
				for(int n = 0; n < stages.GetCount(); n++) if(stages[n].qualified_id == existing.qualified_id) {
					replaced = true;
					if(existing.workspace_id == authorization.workspace_id && MapGetString(candidates[n], "id") == (String)value) {
						matches++; target_kind = MapGetString(candidates[n], "kind"); target_valid = true;
					}
					break;
				}
				if(!replaced && existing.workspace_id == authorization.workspace_id && existing.id == (String)value) {
					matches++; target_kind = existing.kind;
					target_valid = existing.valid && existing.validated && existing.validation_state == "valid";
				}
			}
			if(matches != 1 || target_kind != field.reference_kind || !target_valid) { result.status = "invalid_reference"; return result; }
			for(const DataDocument& existing : current.docs) {
				bool in_batch = false;
				for(const JournalStageRecord& stage : stages) if(stage.qualified_id == existing.qualified_id) in_batch = true;
				if(!in_batch && existing.workspace_id == authorization.workspace_id && existing.id == (String)value) {
					bool found = false;
					for(const ReferenceSnapshot& snapshot : reference_snapshots) if(snapshot.qualified_id == existing.qualified_id) found = true;
					if(!found) { ReferenceSnapshot snapshot; snapshot.qualified_id = existing.qualified_id; snapshot.rel_path = existing.rel_path; snapshot.revision = existing.source_revision; reference_snapshots.Add(snapshot); }
				}
			}
		}
	}
	CommitReceipt capacity_receipt;
	capacity_receipt.operation_id = operation_id; capacity_receipt.source_id = source_id;
	capacity_receipt.workspace_id = authorization.workspace_id; capacity_receipt.digest = request_digest;
	for(const JournalStageRecord& stage : stages) { capacity_receipt.ids.Add(stage.qualified_id); capacity_receipt.revisions.Add(stage.new_revision); }
	if(!StoreCommitReceipt(root.root_fd, capacity_receipt, DATA_WRITE_FAIL_NONE, true)) {
		result.status = "receipt_store_full"; return result;
	}
	bool has_create = false;
	for(const JournalStageRecord& stage : stages) if(stage.action == "create") has_create = true;
	if(has_create && !EnsureCreateRecordDirectory(root.root_fd)) { result.status = "destination_unavailable"; return result; }
	for(const JournalStageRecord& stage : stages) if(stage.action == "create") {
		bool identity_collision = false;
		int entries_scanned = 0; int64 bytes_scanned = 0;
		String record_id = stage.qualified_id.Mid(authorization.workspace_id.GetCount() + 1);
		if(!HasConflictingCreateIdentity(root.root_fd, "", 0, authorization.workspace_id, record_id, stage.rel_path,
		                                 identity_collision, entries_scanned, bytes_scanned)) {
			result.status = "identity_scan_unavailable"; return result;
		}
		int parent = -1; String leaf;
		if(identity_collision || !OpenRecordParent(root.root_fd, stage.rel_path, parent, leaf)) {
			if(parent >= 0) close(parent);
			result.status = "identity_conflict"; return result;
		}
		struct stat existing_target;
		bool absent = fstatat(parent, leaf.ToStd().c_str(), &existing_target, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
		close(parent);
		if(!absent) { result.status = "identity_conflict"; return result; }
	}
	JournalStageResult prepared = current.PrepareJournalLocked(authorization, operation_id, stages, root.root_fd, test_failpoint);
	if(prepared.status != "prepared") { result.status = prepared.status; return result; }
	if(synthetic_write_source && (test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_CHANGE_REFERENCE ||
	   test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_DELETE_REFERENCE) && !reference_snapshots.IsEmpty()) {
		String path = AppendFileName(base_dir, reference_snapshots[0].rel_path);
		if(test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_DELETE_REFERENCE) FileDelete(path);
		else SaveFile(path, LoadFile(path) + " ");
	}
	if(synthetic_write_source && test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_CHANGE_RECORD && !stages.IsEmpty())
		SaveFile(AppendFileName(base_dir, stages[0].rel_path), stages[0].old_payload + " ");
	int jfd = openat(root.root_fd, ".mgr-journal", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	int ofd = jfd >= 0 ? openat(jfd, operation_id.ToStd().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC) : -1;
	String manifest_bytes; JournalManifest manifest;
	if(ofd < 0 || !ReadJournalFile(ofd, "manifest.json", 65536, manifest_bytes) || !ParseJournalManifest(manifest_bytes, manifest)) {
		if(ofd >= 0) close(ofd); if(jfd >= 0) close(jfd); result.status = "recovery_required"; return result;
	}
	manifest.request_digest = request_digest;
	auto resolve_conflict = [&]() -> DataBatchResult {
		close(ofd); close(jfd);
		result.status = DiscardPreparedJournalLocked(root.root_fd, source_id, authorization.workspace_id, operation_id) ? "conflict" : "recovery_required";
		return pick(result);
	};
	for(const JournalStageRecord& stage : stages) {
		int parent = -1; String leaf, actual;
		if(stage.action == "create") {
			if(!OpenRecordParent(root.root_fd, stage.rel_path, parent, leaf)) return resolve_conflict();
			struct stat target_stat;
			bool absent = fstatat(parent, leaf.ToStd().c_str(), &target_stat, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
			close(parent);
			if(!absent) return resolve_conflict();
		}
		else if(!OpenRecordParent(root.root_fd, stage.rel_path, parent, leaf) || !ReadRecordAt(parent, leaf, actual) || actual != stage.old_payload) {
			if(parent >= 0) close(parent); return resolve_conflict();
		}
		else close(parent);
	}
	for(const ReferenceSnapshot& snapshot : reference_snapshots) {
		int parent = -1; String leaf, actual;
		if(!OpenRecordParent(root.root_fd, snapshot.rel_path, parent, leaf) || !ReadRecordAt(parent, leaf, actual) ||
		   RecordSourceRevision(snapshot.rel_path, actual) != snapshot.revision) {
			if(parent >= 0) close(parent); return resolve_conflict();
		}
		close(parent);
	}
	if(synthetic_write_source && test_failpoint == DATA_WRITE_FAIL_BATCH_TEST_REGISTRATION_CHANGE) {
		DataSourceRegistration revoked = synthetic_registration;
		revoked.root = base_dir + "-redirected-at-commit";
		SetSyntheticRegistrationForTests(revoked);
	}
	if(!HasCurrentWriteRegistration()) {
		close(ofd); close(jfd); result.status = "source_capability_changed_recovery_required"; return result;
	}
	manifest.state = "committing";
	String bytes = AsJSON(JournalManifestMap(manifest), true);
	if(AtomicReplaceRecord(ofd, "manifest.json", bytes, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) {
		close(ofd); close(jfd); result.status = "uncertain"; return result;
	}
	for(int i = 0; i < stages.GetCount(); i++) {
		if(test_failpoint == DATA_WRITE_FAIL_BATCH_BEFORE_REPLACE && i == 0) { close(ofd); close(jfd); result.status = "uncommitted"; return result; }
		int parent = -1; String leaf;
		if(!OpenRecordParent(root.root_fd, stages[i].rel_path, parent, leaf)) { close(ofd); close(jfd); result.status = "uncertain"; return result; }
		AtomicReplaceOutcome applied = stages[i].action == "create" ? AtomicCreateRecord(parent, leaf, stages[i].new_payload) :
		                              AtomicReplaceRecord(parent, leaf, stages[i].new_payload, DATA_WRITE_FAIL_NONE);
		close(parent);
		if(applied != ATOMIC_REPLACE_COMMITTED) { close(ofd); close(jfd); result.status = "uncertain"; return result; }
		manifest.records[i].state = "applied";
		bytes = AsJSON(JournalManifestMap(manifest), true);
		if(AtomicReplaceRecord(ofd, "manifest.json", bytes, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) {
			close(ofd); close(jfd); result.status = "uncertain"; return result;
		}
		if(test_failpoint == DATA_WRITE_FAIL_BATCH_AFTER_REPLACE && i == 0) { close(ofd); close(jfd); result.status = "uncommitted"; return result; }
	}
	manifest.state = "committed";
	bytes = AsJSON(JournalManifestMap(manifest), true);
	if(test_failpoint == DATA_WRITE_FAIL_BATCH_COMMIT_MANIFEST || AtomicReplaceRecord(ofd, "manifest.json", bytes, DATA_WRITE_FAIL_NONE) != ATOMIC_REPLACE_COMMITTED) {
		close(ofd); close(jfd); result.status = "uncertain"; return result;
	}
	for(const JournalStageRecord& stage : stages) { result.changed_ids.Add(stage.qualified_id); result.revisions.Add(stage.new_revision); }
	CommitReceipt receipt;
	receipt.operation_id = operation_id; receipt.source_id = source_id; receipt.workspace_id = authorization.workspace_id; receipt.digest = request_digest;
	receipt.ids = clone(result.changed_ids); receipt.revisions = clone(result.revisions);
	if(!StoreCommitReceipt(root.root_fd, receipt, DATA_WRITE_FAIL_NONE)) {
		close(ofd); close(jfd); result.status = "receipt_persist_failed_recovery_required"; return result;
	}
	close(ofd); close(jfd);
	result.status = "committed";
	return result;
#endif
}

DataWriteResult DataStore::WriteRecord(const DataWriteAuthorization& authorization,
	                                   const String& qualified_id,
	                                   const String& expected_record_revision,
					const String& expected_source_revision,
								   const String& idempotency_key,
								   const ValueMap& proposed,
								   DataWriteTestFailpoint test_failpoint)
{
	DataWriteResult result;
	result.status = "invalid_request";
	if(source_read_only || !source_write_enabled || base_dir.IsEmpty() || source_id.IsEmpty()) { result.status = "read_only"; return result; }
	if(!authorization.IsAllowed() || authorization.principal.GetCount() > 128 ||
	   qualified_id.IsEmpty() || expected_record_revision.IsEmpty() ||
	   expected_source_revision.IsEmpty() || idempotency_key.IsEmpty() || idempotency_key.GetCount() > 128)
		return result;
	for(int i = 0; i < authorization.principal.GetCount(); i++)
		if((byte)authorization.principal[i] < 0x21 || (byte)authorization.principal[i] > 0x7e) return result;
	for(int i = 0; i < idempotency_key.GetCount(); i++)
		if((byte)idempotency_key[i] < 0x21 || (byte)idempotency_key[i] > 0x7e) return result;
	if(!IsSafeRoot(base_dir) || !IsSafeSourceId(source_id)) { result.status = "source_unavailable"; return result; }
	if(authorization.source_id != source_id) { result.status = "scope_denied"; return result; }

	Mutex::Lock lock(s_canonical_write_mutex);
	#ifndef PLATFORM_POSIX
	result.status = "unsupported_platform";
	return result;
	#else
	LockedSourceRoot locked_root;
	if(!locked_root.Open(base_dir)) { result.status = "source_unavailable"; return result; }
	if(!synthetic_write_source) {
		DataSourceRegistration authoritative;
		if(!ReadConfiguredRegistration(authoritative) || !authoritative.enabled || authoritative.read_only ||
		   !authoritative.write_enabled || NormalizePath(authoritative.root) != base_dir || authoritative.id != source_id) {
			result.status = "source_capability_changed";
			return result;
		}
	}
	DataStore current;
	if(!current.LoadRoot(base_dir, source_id)) { result.status = "source_unavailable"; return result; }
	const DataDocument* doc = current.FindDocument(qualified_id);
	if(!doc || !doc->valid || !doc->validated || doc->source_id != source_id) { result.status = "identity_or_schema_invalid"; return result; }
	if(authorization.workspace_id != doc->workspace_id) { result.status = "scope_denied"; return result; }
	int parent_fd = -1;
	String leaf;
	if(!OpenRecordParent(locked_root.root_fd, doc->rel_path, parent_fd, leaf)) { result.status = "identity_or_schema_invalid"; return result; }
	struct ParentHandleGuard { int fd; ~ParentHandleGuard() { if(fd >= 0) close(fd); } } parent_guard = { parent_fd };
	String old_bytes;
	if(!ReadRecordAt(parent_fd, leaf, old_bytes)) { result.status = "identity_or_schema_invalid"; return result; }
	ValueMap candidate = proposed;
	Value envelope_value = MapGet(candidate, "envelope");
	if(!envelope_value.Is<ValueMap>()) { result.status = "invalid_schema"; return result; }
	ValueMap envelope = envelope_value;
	if(MapGetString(candidate, "id") != doc->id || MapGetString(candidate, "kind") != doc->kind ||
	   MapGetString(envelope, "workspace_id") != doc->workspace_id || MapGetString(envelope, "stable_id") != doc->id ||
	   MapGetString(envelope, "record_kind") != doc->kind) { result.status = "identity_mismatch"; return result; }
	ValueMap current_envelope = MapGet(doc->data, "envelope");
	String idem = MapGetString(current_envelope, "last_write_key");
	if(idem == idempotency_key && MapGetString(current_envelope, "last_write_actor") == authorization.principal) {
		if(MapGetString(current_envelope, "last_write_expected_revision") != expected_record_revision ||
		   MapGetString(current_envelope, "last_write_expected_source_revision") != expected_source_revision ||
		   MapGetString(current_envelope, "last_write_request_digest") != ContentDigest(AsJSON(proposed, true))) {
			result.status = "idempotency_conflict";
			return result;
		}
		ValueMap expected_retry = proposed;
		ValueMap retry_envelope = MapGet(expected_retry, "envelope");
		retry_envelope.Set("revision", MapGetString(current_envelope, "revision"));
		retry_envelope.Set("last_write_key", idempotency_key);
		retry_envelope.Set("last_write_actor", authorization.principal);
		retry_envelope.Set("last_write_expected_revision", expected_record_revision);
		retry_envelope.Set("last_write_expected_source_revision", expected_source_revision);
		retry_envelope.Set("last_write_request_digest", ContentDigest(AsJSON(proposed, true)));
		expected_retry.Set("envelope", retry_envelope);
		if(AsJSON(expected_retry, true) != AsJSON(doc->data, true)) {
			result.status = "idempotency_conflict";
			return result;
		}
		result.status = "already_committed";
		result.revision = MapGetString(current_envelope, "revision");
		result.source_revision = RecordSourceRevision(doc->rel_path, old_bytes);
		return result;
	}
	if(old_bytes.IsEmpty() || RecordSourceRevision(doc->rel_path, old_bytes) != expected_source_revision || doc->source_revision != expected_source_revision ||
	   MapGetString(current_envelope, "revision") != expected_record_revision) { result.status = "conflict"; return result; }
	if(MapGetString(envelope, "revision") != expected_record_revision) { result.status = "conflict"; return result; }
	int64 revision_number = 0;
	for(int i = 0; i < expected_record_revision.GetCount(); i++) {
		char c = expected_record_revision[i];
		if(c < '0' || c > '9' || revision_number > (INT64_MAX - (c - '0')) / 10) {
			result.status = "invalid_request";
			return result;
		}
		revision_number = revision_number * 10 + c - '0';
	}
	if(revision_number == INT64_MAX) { result.status = "invalid_request"; return result; }
	String next_revision = AsString(revision_number + 1);
	envelope.Set("revision", next_revision);
	envelope.Set("last_write_key", idempotency_key);
	envelope.Set("last_write_actor", authorization.principal);
	envelope.Set("last_write_expected_revision", expected_record_revision);
	envelope.Set("last_write_expected_source_revision", expected_source_revision);
	envelope.Set("last_write_request_digest", ContentDigest(AsJSON(proposed, true)));
	candidate.Set("envelope", envelope);

	DataDocument proposed_doc;
	proposed_doc.data = candidate;
	proposed_doc.kind = doc->kind;
	proposed_doc.id = doc->id;
	proposed_doc.schema_id = MapGetString(envelope, "schema_id");
	proposed_doc.schema_version = MapGetString(envelope, "schema_version");
	current.ValidateDocument(proposed_doc, current.FindSchemaCard(doc->kind));
	if(!proposed_doc.valid || !proposed_doc.validated) { result.status = "invalid_schema"; return result; }
	const SchemaCard* schema = current.FindSchemaCard(doc->kind);
	if(schema) {
		for(const SchemaCard::Field& field : schema->fields) {
			if(field.reference_kind.IsEmpty()) continue;
			Value reference;
			if(!GetTypedField(candidate, field.name, reference) || !IsString(reference)) {
				result.status = "invalid_reference";
				return result;
			}
			int matches = 0;
			const DataDocument* target = NULL;
			for(const DataDocument& item : current.docs) {
				if(&item == doc) continue;
				if(item.workspace_id == proposed_doc.workspace_id && item.id == (String)reference) {
					matches++;
					target = &item;
				}
			}
			if(matches != 1 || !target || !target->valid || target->validation_state != "valid" || target->kind != field.reference_kind) {
				result.status = "invalid_reference";
				return result;
			}
		}
	}
	String bytes = AsJSON(candidate, true);
	// Recheck bytes and idempotency at the commit boundary after validation/serialization.
	String latest;
	if(!ReadRecordAt(parent_fd, leaf, latest)) { result.status = "conflict"; return result; }
	if(latest != old_bytes || RecordSourceRevision(doc->rel_path, latest) != expected_source_revision) { result.status = "conflict"; return result; }
	AtomicReplaceOutcome replacement = AtomicReplaceRecord(parent_fd, leaf, bytes, test_failpoint);
	if(replacement == ATOMIC_REPLACE_FAILED) {
		result.status = test_failpoint == DATA_WRITE_FAIL_TEMP_CREATE || test_failpoint == DATA_WRITE_FAIL_TEMP_WRITE ? "temp_write_failed" : "replacement_failed";
		return result;
	}
	result.status = replacement == ATOMIC_REPLACE_COMMITTED ? "committed" : "committed_durability_uncertain";
	result.revision = next_revision;
	result.source_revision = RecordSourceRevision(doc->rel_path, bytes);
	return result;
	#endif
}

END_UPP_NAMESPACE
