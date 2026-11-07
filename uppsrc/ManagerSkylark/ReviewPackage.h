#ifndef _ManagerSkylark_ReviewPackage_h_
#define _ManagerSkylark_ReviewPackage_h_

#include "DataStore.h"

NAMESPACE_UPP

struct ReviewArtifact : Moveable<ReviewArtifact> {
	String path;
	String content_sha256;
	int64 byte_count = 0;
	bool hash_only = true;
};

struct ReviewPackageInput : Moveable<ReviewPackageInput> {
	String workspace_id, goal_id, scope_id, base_revision, base_state = "available", head_revision;
	String previous_revision, author, author_state, capture_state;
	Vector<String> selected_paths, artifact_refs, check_refs;
	Vector<ReviewArtifact> artifacts;
	String patch_bytes;
	bool base_available = true;
	bool head_available = true;
	ReviewPackageInput() {}
	ReviewPackageInput(const ReviewPackageInput& o) : workspace_id(o.workspace_id), goal_id(o.goal_id), scope_id(o.scope_id), base_revision(o.base_revision), base_state(o.base_state), head_revision(o.head_revision), previous_revision(o.previous_revision), author(o.author), author_state(o.author_state), capture_state(o.capture_state), selected_paths(clone(o.selected_paths)), artifact_refs(clone(o.artifact_refs)), check_refs(clone(o.check_refs)), artifacts(clone(o.artifacts)), patch_bytes(o.patch_bytes), base_available(o.base_available), head_available(o.head_available) {}
	ReviewPackageInput& operator=(const ReviewPackageInput& o) { if(this != &o) { workspace_id=o.workspace_id; goal_id=o.goal_id; scope_id=o.scope_id; base_revision=o.base_revision; base_state=o.base_state; head_revision=o.head_revision; previous_revision=o.previous_revision; author=o.author; author_state=o.author_state; capture_state=o.capture_state; selected_paths=clone(o.selected_paths); artifact_refs=clone(o.artifact_refs); check_refs=clone(o.check_refs); artifacts=clone(o.artifacts); patch_bytes=o.patch_bytes; base_available=o.base_available; head_available=o.head_available; } return *this; }
};

struct ReviewPackagePreview : Moveable<ReviewPackagePreview> {
	String status, package_id, revision_id, patch_sha256;
	ValueMap record;
};

class ReviewPackage {
public:
	static const int MAX_PATCH_BYTES = 1024 * 1024;
	static const int MAX_METADATA_ITEMS = 128;
	static ReviewPackagePreview Capture(const DataStore& store, const ReviewPackageInput& input,
	                                    const DataWriteAuthorization& authorization,
	                                    const String& operation_id);
	static bool RunSyntheticTests();
};

END_UPP_NAMESPACE

#endif
