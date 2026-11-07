#include "ReviewPackage.h"

NAMESPACE_UPP

static bool ReviewToken(const String& s, int max = 160)
{
	if(s.IsEmpty() || s.GetCount() > max) return false;
	for(int i = 0; i < s.GetCount(); i++) {
		byte c = s[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':')) return false;
	}
	return true;
}

static bool SafeReviewPath(const String& p)
{
	if(p.IsEmpty() || p.GetCount() > 240 || p[0] == '/' || p.Find('\\') >= 0 || p.Find(':') >= 0 || p.Find('\n') >= 0 || p.Find('\r') >= 0) return false;
	Vector<String> parts = Split(p, '/');
	for(const String& part : parts) {
		if(part.IsEmpty() || part == "." || part == ".." || ToLower(part) == ".git") return false;
		for(int i = 0; i < part.GetCount(); i++) {
			byte c = part[i];
			if(c <= 32 || c == 127 || c == '"' || c == '\'') return false;
		}
	}
	return true;
}

static String DiffHeaderPath(const String& line, const String& prefix)
{
	if(!line.StartsWith(prefix)) return String();
	String path = line.Mid(prefix.GetCount());
	int tab = path.Find('\t');
	if(tab >= 0) path = path.Left(tab);
	return path;
}

static bool ParseDiffNumber(const String& line, int& at, int& value)
{
	if(at >= line.GetCount() || line[at] < '0' || line[at] > '9') return false;
	int n = 0;
	while(at < line.GetCount() && line[at] >= '0' && line[at] <= '9') {
		int digit = line[at++] - '0';
		if(n > (INT_MAX - digit) / 10) return false;
		n = n * 10 + digit;
	}
	value = n;
	return true;
}

static bool ParseHunkHeader(const String& line, int& old_left, int& new_left)
{
	if(!line.StartsWith("@@ -")) return false;
	int at = 4, old_start, old_count = 1, new_start, new_count = 1;
	if(!ParseDiffNumber(line, at, old_start)) return false;
	if(at < line.GetCount() && line[at] == ',') { at++; if(!ParseDiffNumber(line, at, old_count)) return false; }
	if(at >= line.GetCount() || line[at++] != ' ' || at >= line.GetCount() || line[at++] != '+') return false;
	if(!ParseDiffNumber(line, at, new_start)) return false;
	if(at < line.GetCount() && line[at] == ',') { at++; if(!ParseDiffNumber(line, at, new_count)) return false; }
	if(at + 2 >= line.GetCount() || line[at++] != ' ' || line[at++] != '@' || line[at++] != '@') return false;
	if(at < line.GetCount() && line[at] != ' ') return false;
	if(old_start > INT_MAX - old_count || new_start > INT_MAX - new_count) return false;
	old_left = old_count; new_left = new_count;
	return true;
}

static bool ValidateReviewPatch(const String& patch, const Index<String>& selected,
	                           const Index<String>& hash_only_artifacts, String& result)
{
	result = "invalid_patch";
	Vector<String> lines = Split(patch, '\n');
	Index<String> seen;
	String diff_old, diff_new, file_old, file_new;
	int headers = 0, old_left = 0, new_left = 0;
	bool block = false, in_hunk = false, hunk_has_content = false, has_hunk = false, hash_only_binary = false;
	auto finish_hunk = [&]() -> bool {
		if(!in_hunk) return true;
		if(old_left || new_left) return false;
		in_hunk = false;
		return true;
	};
	auto finish_block = [&]() -> bool {
		if(!block || !finish_hunk() || headers != 2) return false;
		if((file_old != "/dev/null" && (!SafeReviewPath(file_old) || file_old != diff_old)) ||
		   (file_new != "/dev/null" && (!SafeReviewPath(file_new) || file_new != diff_new)) ||
		   (file_old == "/dev/null" && file_new == "/dev/null")) return false;
		String member = file_old == "/dev/null" ? file_new : file_old;
		if(file_old != "/dev/null" && file_new != "/dev/null" && file_old != file_new) return false;
		if(selected.Find(member) < 0 || seen.Find(member) >= 0 || (!has_hunk && !(hash_only_binary && hash_only_artifacts.Find(member) >= 0))) return false;
		seen.Add(member);
		return true;
	};
	for(String line : lines) {
		if(line.GetCount() && line[line.GetCount() - 1] == '\r') line = line.Left(line.GetCount() - 1);
		for(int i = 0; i < line.GetCount(); i++) if(line[i] == 0) return false;
		if(line.Find("SYMLINK") >= 0 || line.StartsWith("new file mode 120000") || line.StartsWith("old mode 120000") ||
		   line.StartsWith("new mode 120000") || line.StartsWith("rename from ") || line.StartsWith("rename to ") ||
		   line.StartsWith("copy from ") || line.StartsWith("copy to ")) return false;
		if(in_hunk && line.StartsWith("\\ No newline at end of file")) {
			if(!hunk_has_content) return false;
			continue;
		}
		if(in_hunk && (old_left || new_left)) {
			if(line.IsEmpty()) return false;
			if(line[0] == ' ') { if(old_left <= 0 || new_left <= 0) return false; old_left--; new_left--; hunk_has_content = true; continue; }
			if(line[0] == '-') { if(old_left <= 0) return false; old_left--; hunk_has_content = true; continue; }
			if(line[0] == '+') { if(new_left <= 0) return false; new_left--; hunk_has_content = true; continue; }
			return false;
		}
		if(line.StartsWith("diff --git ")) {
			if(block && !finish_block()) return false;
			Vector<String> fields = Split(line, ' ');
			if(fields.GetCount() != 4 || fields[0] != "diff" || fields[1] != "--git" || !fields[2].StartsWith("a/") || !fields[3].StartsWith("b/")) return false;
			diff_old = fields[2].Mid(2); diff_new = fields[3].Mid(2);
			if(!SafeReviewPath(diff_old) || !SafeReviewPath(diff_new) || diff_old != diff_new || selected.Find(diff_old) < 0) return false;
			file_old.Clear(); file_new.Clear(); headers = 0; block = true; has_hunk = false; hash_only_binary = false; hunk_has_content = false;
			continue;
		}
		if(line.StartsWith("diff ")) return false;
		if(line.StartsWith("--- ")) {
			if(!block || headers != 0) return false;
			file_old = DiffHeaderPath(line, "--- ");
			if(file_old != "/dev/null" && file_old.StartsWith("a/")) file_old = file_old.Mid(2);
			if(file_old != "/dev/null" && (!SafeReviewPath(file_old) || file_old != diff_old)) return false;
			headers = 1; continue;
		}
		if(line.StartsWith("+++ ")) {
			if(!block || headers != 1) return false;
			file_new = DiffHeaderPath(line, "+++ ");
			if(file_new != "/dev/null" && file_new.StartsWith("b/")) file_new = file_new.Mid(2);
			if(file_new != "/dev/null" && (!SafeReviewPath(file_new) || file_new != diff_new)) return false;
			headers = 2; continue;
		}
		if(line.StartsWith("@@")) {
			if(!block || headers != 2 || !finish_hunk() || !ParseHunkHeader(line, old_left, new_left)) return false;
			in_hunk = true; hunk_has_content = false; has_hunk = true; continue;
		}
		if(line.StartsWith("Binary files ")) {
			if(!block || headers != 2 || has_hunk) return false;
			Vector<String> fields = Split(line, ' ');
			if(fields.GetCount() != 6 || fields[0] != "Binary" || fields[1] != "files" || fields[3] != "and" || fields[5] != "differ") return false;
			String left = fields[2], right = fields[4];
			if(left.StartsWith("a/")) left = left.Mid(2);
			if(right.StartsWith("b/")) right = right.Mid(2);
			if(left != diff_old || right != diff_new || hash_only_artifacts.Find(diff_old) < 0) return false;
			hash_only_binary = true; continue;
		}
		if(!block) { if(!line.IsEmpty()) return false; continue; }
		if(headers < 2) {
			if(line.IsEmpty() || line.StartsWith("index ") || line.StartsWith("new file mode ") || line.StartsWith("deleted file mode ") ||
			   line.StartsWith("old mode ") || line.StartsWith("new mode ") || line.StartsWith("similarity index ") || line.StartsWith("dissimilarity index ")) continue;
			return false;
		}
		if(!line.IsEmpty()) return false;
	}
	if(block && !finish_block()) return false;
	if(seen.GetCount() != selected.GetCount()) return false;
	result.Clear();
	return true;
}

static bool ValidReviewArtifact(const ReviewArtifact& a)
{
	if(!SafeReviewPath(a.path) || a.content_sha256.GetCount() != 64 || a.byte_count < 0 || a.byte_count > (1LL << 40) || !a.hash_only) return false;
	for(int i = 0; i < a.content_sha256.GetCount(); i++) { char c = a.content_sha256[i]; if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false; }
	return true;
}

static ValueArray StringsToArray(const Vector<String>& values) { ValueArray a; for(const String& s : values) a.Add(s); return a; }

static ReviewPackagePreview SafeReviewResult(const String& status)
{
	ReviewPackagePreview p; p.status = status; return p;
}

ReviewPackagePreview ReviewPackage::Capture(const DataStore& store, const ReviewPackageInput& in,
	                                           const DataWriteAuthorization& auth, const String& operation)
{
	if(!auth.AllowsWorkspace(in.workspace_id) || !auth.AllowsSource(in.workspace_id, store.GetSourceId())) return SafeReviewResult("denied");
	if(!in.base_available || in.base_state == "unavailable" || in.base_state == "unknown" || in.base_revision.IsEmpty() || !ReviewToken(in.base_revision, 256)) return SafeReviewResult("blocked_base_unavailable");
	if(in.base_state != "available") return SafeReviewResult("invalid");
	if(!ReviewToken(in.workspace_id) || !ReviewToken(in.goal_id) || !ReviewToken(in.scope_id) ||
	   !ReviewToken(in.author) || in.author != auth.GetPrincipal() || (in.author_state != "known-local-author" && in.author_state != "unknown") || !ReviewToken(in.capture_state, 64) ||
	   !ReviewToken(operation, 160) || !in.head_available || in.head_revision.IsEmpty() || !ReviewToken(in.head_revision, 256) ||
	   in.patch_bytes.GetCount() > MAX_PATCH_BYTES || in.selected_paths.IsEmpty() || in.selected_paths.GetCount() > MAX_METADATA_ITEMS ||
	   in.artifacts.GetCount() > MAX_METADATA_ITEMS || in.artifact_refs.GetCount() > MAX_METADATA_ITEMS || in.check_refs.GetCount() > MAX_METADATA_ITEMS)
		return SafeReviewResult("invalid");
	Index<String> selected;
	for(const String& path : in.selected_paths) if(!SafeReviewPath(path) || selected.Find(path) >= 0) return SafeReviewResult("invalid"); else selected.Add(path);
	for(const String& ref : in.artifact_refs) if(!ReviewToken(ref)) return SafeReviewResult("invalid");
	for(const String& ref : in.check_refs) if(!ReviewToken(ref)) return SafeReviewResult("invalid");
	Index<String> hash_only_artifacts;
	for(const ReviewArtifact& a : in.artifacts) {
		if(!ValidReviewArtifact(a)) return SafeReviewResult("invalid");
		hash_only_artifacts.Add(a.path);
	}
	String patch_error;
	if(!ValidateReviewPatch(in.patch_bytes, selected, hash_only_artifacts, patch_error)) return SafeReviewResult(patch_error.IsEmpty() ? String("invalid_patch") : patch_error);
	String pid = "review-p-" + DataStore::ContentDigest(in.workspace_id + "\n" + in.goal_id + "\n" + in.scope_id).Left(48);
	String rid = "review-r-" + DataStore::ContentDigest(pid + "\n" + in.base_revision + "\n" + in.head_revision).Left(48);
	String patch_hash = DataStore::ContentDigest(in.patch_bytes);
	ValueMap record;
	record.Add("id", rid); record.Add("kind", "review-package-revision"); record.Add("workspace_id", in.workspace_id);
	record.Add("package_id", pid); record.Add("goal_id", in.goal_id); record.Add("scope_id", in.scope_id);
	record.Add("base_revision", in.base_revision); record.Add("base_state", in.base_state); record.Add("head_revision", in.head_revision);
	record.Add("previous_revision", in.previous_revision.IsEmpty() ? String("none") : in.previous_revision); record.Add("selected_paths", StringsToArray(in.selected_paths));
	record.Add("patch_bytes", in.patch_bytes); record.Add("patch_sha256", patch_hash); record.Add("author", in.author);
	record.Add("author_state", in.author_state); record.Add("capture_state", in.capture_state);
	record.Add("artifact_refs", StringsToArray(in.artifact_refs)); record.Add("check_refs", StringsToArray(in.check_refs));
	ValueArray artifacts;
	for(const ReviewArtifact& a : in.artifacts) { ValueMap item; item.Add("path", a.path); item.Add("content_sha256", a.content_sha256); item.Add("byte_count", a.byte_count); item.Add("hash_only", true); artifacts.Add(item); }
	record.Add("artifacts", artifacts);
	ValueMap env; env.Add("schema_id", "skylark/review-package-revision"); env.Add("schema_version", "1"); env.Add("workspace_id", in.workspace_id); env.Add("stable_id", rid); env.Add("record_kind", "review-package-revision"); env.Add("revision", "1"); env.Add("visibility", "private"); env.Add("provenance_ref", "trusted-review-capture"); record.Add("envelope", env);
	const DataDocument *existing = NULL;
	Vector<const DataDocument*> chain;
	for(const DataDocument& d : store.GetDocuments()) if(d.kind == "review-package-revision" && d.workspace_id == in.workspace_id && MapGetString(d.data, "package_id") == pid) {
		if(!d.valid || !d.validated) return SafeReviewResult("revision_conflict");
		chain.Add(&d);
		if(d.id == rid) { if(existing) return SafeReviewResult("revision_conflict"); existing = &d; }
	}
	const DataDocument *chain_head = NULL;
	if(chain.GetCount()) {
		Vector<int> children; children.SetCount(chain.GetCount(), 0);
		int roots = 0, root_index = -1;
		for(int i = 0; i < chain.GetCount(); i++) {
			String parent = MapGetString(chain[i]->data, "previous_revision");
			if(parent == "none") { roots++; root_index = i; continue; }
			int match = -1, matches = 0;
			for(int j = 0; j < chain.GetCount(); j++) if(chain[j]->id == parent) { match = j; matches++; }
			if(matches != 1) return SafeReviewResult("revision_conflict");
			if(++children[match] > 1) return SafeReviewResult("revision_conflict");
		}
		if(roots != 1) return SafeReviewResult("revision_conflict");
		Index<int> visited;
		int at = root_index;
		while(at >= 0) {
			if(visited.Find(at) >= 0) return SafeReviewResult("revision_conflict");
			visited.Add(at);
			if(children[at] == 0) { chain_head = chain[at]; break; }
			int next = -1;
			for(int i = 0; i < chain.GetCount(); i++) if(MapGetString(chain[i]->data, "previous_revision") == chain[at]->id) { if(next >= 0) return SafeReviewResult("revision_conflict"); next = i; }
			at = next;
		}
		if(!chain_head || visited.GetCount() != chain.GetCount()) return SafeReviewResult("revision_conflict");
	}
	if(existing) {
		if(!existing->valid || !existing->validated || MapGetString(existing->data, "patch_sha256") != patch_hash || AsJSON(existing->data, true) != AsJSON(record, true)) return SafeReviewResult("revision_conflict");
		ReviewPackagePreview p = SafeReviewResult("already_committed"); p.package_id = pid; p.revision_id = rid; p.patch_sha256 = patch_hash; return p;
	}
	if((chain.IsEmpty() && !in.previous_revision.IsEmpty()) || (!chain.IsEmpty() && in.previous_revision != chain_head->id)) return SafeReviewResult("revision_conflict");
	DataBatchChange c; c.action = DATA_BATCH_CREATE; c.qualified_id = in.workspace_id + ":" + rid; c.proposed = record;
	Vector<DataBatchChange> changes; changes.Add(c);
	DataBatchResult result = store.PublishBatch(auth, operation, changes);
	if(result.status != "committed") return SafeReviewResult(result.status);
	ReviewPackagePreview p = SafeReviewResult("committed"); p.package_id = pid; p.revision_id = rid; p.patch_sha256 = patch_hash; return p;
}

bool ReviewPackage::RunSyntheticTests()
{
	Index<String> paths; paths.Add("src/synthetic.cpp");
	String diff = "diff --git a/src/synthetic.cpp b/src/synthetic.cpp\nindex 1111111..2222222 100644\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n@@ -1 +1 @@\n-old\n+new\n";
	String error;
	Index<String> no_artifacts;
	if(!ValidateReviewPatch(diff, paths, no_artifacts, error)) return false;
	for(const String& unsafe : { String("../escape"), String("/absolute"), String(".git/config"), String("src\\bad"), String("C:/drive") }) if(SafeReviewPath(unsafe)) return false;
	auto invalid_patch = [&](const String& value, const Index<String>& selected, const Index<String>& hashes) {
		String why;
		return !ValidateReviewPatch(value, selected, hashes, why);
	};
	Index<String> mismatch; mismatch.Add("src/other.cpp");
	if(!invalid_patch(diff, mismatch, no_artifacts)) return false;
	String orphan = "--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n";
	if(!invalid_patch(orphan, paths, no_artifacts)) return false;
	String detached = diff + "--- a/src/synthetic.cpp\n";
	if(!invalid_patch(detached, paths, no_artifacts)) return false;
	String rename = "diff --git a/src/old.cpp b/src/new.cpp\n--- a/src/old.cpp\n+++ b/src/new.cpp\n";
	if(!invalid_patch(rename, paths, no_artifacts)) return false;
	String multi = "diff --git a/src/synthetic.cpp b/src/synthetic.cpp\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n@@ -1,2 +1,2 @@ first\n-a\n+b\n c\n@@ -9 +10,2 @@ second\n-x\n+y\n+z\n";
	if(!ValidateReviewPatch(multi, paths, no_artifacts, error)) return false;
	for(const String& bad : {
		String("diff --git a/src/synthetic.cpp b/src/synthetic.cpp\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n@@ -1,2 +1 @@\n-a\n+new\n"),
		String("diff --git a/src/synthetic.cpp b/src/synthetic.cpp\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n@@ -1 +1 @@\n"),
		String("diff --git a/src/synthetic.cpp b/src/synthetic.cpp\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n@@ -x +1 @@\n-a\n+b\n"),
		String("@@ -1 +1 @@\n-a\n+b\n"),
		String("diff --git a/src/synthetic.cpp b/src/synthetic.cpp\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\ngarbage\n@@ -1 +1 @@\n-a\n+b\n"),
		String("diff --git a/src/synthetic.cpp b/src/synthetic.cpp\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n@@ -2147483647,1 +1 @@\n-a\n+b\n"),
		String("diff --git a/src/synthetic.cpp b/src/synthetic.cpp\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n@@ -1 +1 @@\n-a\n+b\ngarbage\n"),
		String("diff --git a/src/synthetic.cpp b/src/synthetic.cpp\n--- a/src/synthetic.cpp\n+++ b/src/synthetic.cpp\n"),
		String("diff --git a/src/synthetic.cpp b/src/synthetic.cpp\n--- a/src/other.cpp\n+++ b/src/synthetic.cpp\n@@ -1 +1 @@\n-a\n+b\n")
	}) if(!invalid_patch(bad, paths, no_artifacts)) return false;
	Index<String> added_path; added_path.Add("src/new.cpp");
	String added = "diff --git a/src/new.cpp b/src/new.cpp\nnew file mode 100644\n--- /dev/null\n+++ b/src/new.cpp\n@@ -0,0 +1 @@\n+new\n";
	if(!ValidateReviewPatch(added, added_path, no_artifacts, error)) return false;
	Index<String> deleted_path; deleted_path.Add("src/old.cpp");
	String deleted = "diff --git a/src/old.cpp b/src/old.cpp\ndeleted file mode 100644\n--- a/src/old.cpp\n+++ /dev/null\n@@ -1 +0,0 @@\n-old\n";
	if(!ValidateReviewPatch(deleted, deleted_path, no_artifacts, error)) return false;
	String crlf;
	for(int i = 0; i < diff.GetCount(); i++) { if(diff[i] == '\n') crlf.Cat('\r'); crlf.Cat(diff[i]); }
	if(!ValidateReviewPatch(crlf, paths, no_artifacts, error)) return false;
	Index<String> binary_path; binary_path.Add("assets/blob.bin");
	Index<String> binary_hash; binary_hash.Add("assets/blob.bin");
	String binary_diff = "diff --git a/assets/blob.bin b/assets/blob.bin\n--- a/assets/blob.bin\n+++ b/assets/blob.bin\nBinary files a/assets/blob.bin and b/assets/blob.bin differ\n";
	if(!ValidateReviewPatch(binary_diff, binary_path, binary_hash, error) || !invalid_patch(binary_diff, binary_path, no_artifacts)) return false;
	if(diff.GetCount() > MAX_PATCH_BYTES || String('x', MAX_PATCH_BYTES + 1).GetCount() <= MAX_PATCH_BYTES) return false;
	ReviewArtifact binary; binary.path = "fixtures/blob.bin"; binary.content_sha256 = String('a', 64); binary.byte_count = 4096; binary.hash_only = true;
	if(!ValidReviewArtifact(binary)) return false;
	binary.hash_only = false; if(ValidReviewArtifact(binary)) return false;
	String package = DataStore::ContentDigest("ws\ngoal\nscope");
	String revision = DataStore::ContentDigest(package + "\nbase-r1\nhead-r1");
	String patch_hash = DataStore::ContentDigest(diff);
	String changed_patch_hash = DataStore::ContentDigest(diff + " ");
	if(package != DataStore::ContentDigest("ws\ngoal\nscope") || revision == DataStore::ContentDigest(package + "\nbase-r1\nhead-r2") ||
	   revision != DataStore::ContentDigest(package + "\nbase-r1\nhead-r1") || patch_hash == changed_patch_hash) return false;
	return true;
}

END_UPP_NAMESPACE
