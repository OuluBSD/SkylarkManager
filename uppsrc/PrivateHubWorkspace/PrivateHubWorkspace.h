#ifndef _PrivateHubWorkspace_PrivateHubWorkspace_h_
#define _PrivateHubWorkspace_PrivateHubWorkspace_h_

#include <Core/Core.h>
#include <ForgeCore/ForgeCore.h>
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// Live local-repo facts: branch, head commit, dirty file count.
struct RepoInfo {
	String org;
	String repo;
	String path;        // absolute, contained
	String branch;
	String head_commit;
	int    dirty_files = 0;
	bool   is_repo = false;
	String error;
};

// ---------------------------------------------------------------------------
// PrivateHub record kinds. Each kind is an envelope-style record in the site
// store with a schema card in SchemaRegistry. The schema id doubles as the
// envelope record_kind, except posts, which are child records of a topic.
// Every record carries data.provenance: "local" for records made in
// PrivateHub, "migrated-from-legacy" for records imported by the one-shot
// SourceForge migration (the source path is in the envelope provenance_ref).
// ---------------------------------------------------------------------------
#define PRIVATEHUB_SCHEMA_WIKI_PAGE         "privatehub/wiki-page"
#define PRIVATEHUB_SCHEMA_DISCUSSION_TOPIC  "privatehub/discussion-topic"
#define PRIVATEHUB_SCHEMA_DISCUSSION_POST   "privatehub/discussion-post"
#define PRIVATEHUB_SCHEMA_RELEASE_FILE      "privatehub/release-file"
#define PRIVATEHUB_PROVENANCE_LOCAL         "local"
#define PRIVATEHUB_PROVENANCE_MIGRATED      "migrated-from-legacy"

struct PrivateHubWikiPage : Moveable<PrivateHubWikiPage> {
	String slug;      // record id: [A-Za-z0-9._-]
	String title;
	String body;      // wikitext
	String updated;   // ISO-8601 date or time
	String provenance = PRIVATEHUB_PROVENANCE_LOCAL;
	String revision;  // envelope revision; filled on read, not stored in data

	void Jsonize(JsonIO& io) {
		io("slug", slug)("title", title)("body", body)("updated", updated)("provenance", provenance);
	}
};

struct PrivateHubDiscussionTopic : Moveable<PrivateHubDiscussionTopic> {
	String slug;      // record id; must not contain '.' (dots mark child post records)
	String forum = "general";
	String title;
	String author;
	String created;
	String provenance = PRIVATEHUB_PROVENANCE_LOCAL;
	String revision;

	void Jsonize(JsonIO& io) {
		io("slug", slug)("forum", forum)("title", title)("author", author)
		  ("created", created)("provenance", provenance);
	}
};

// Child record of a topic, stored as "<topic slug>.<post id>".
struct PrivateHubPost : Moveable<PrivateHubPost> {
	String post_id;   // record id part: [A-Za-z0-9_-]
	String topic;     // topic slug
	String author;
	String body;
	String created;
	String provenance = PRIVATEHUB_PROVENANCE_LOCAL;
	String revision;

	void Jsonize(JsonIO& io) {
		io("post_id", post_id)("topic", topic)("author", author)("body", body)
		  ("created", created)("provenance", provenance);
	}
};

struct PrivateHubReleaseFile : Moveable<PrivateHubReleaseFile> {
	String name;      // record id: file name, [A-Za-z0-9._-]
	String release;   // release label, e.g. "1.0.0" or "Snapshot"
	int64  size_bytes = 0;
	String url;       // download URL or local path
	String updated;
	String provenance = PRIVATEHUB_PROVENANCE_LOCAL;
	String revision;

	void Jsonize(JsonIO& io) {
		io("name", name)("release", release)("size_bytes", size_bytes)
		  ("url", url)("updated", updated)("provenance", provenance);
	}
};

// PrivateHub backend: issues and record kinds in the site's WebStorage store;
// repository inspection over the local filesystem with argv-only git.
class PrivateHubWorkspaceBackend : public IForgeBackend {
public:
	// workspace_root: contained root for <org>/<repo> checkouts.
	// issues_root: site-scoped storage root for PrivateHub's own records.
	PrivateHubWorkspaceBackend(const String& workspace_root, const String& issues_root);

	String          BackendId() const override { return "privatehub"; }
	const ForgeCapabilities& Capabilities() const override { return caps; }

	// --- IForgeBackend over the site's JSON issue store ---
	ForgeResult ListIssues(const String& project, Vector<ForgeIssue>& out) override;
	ForgeResult GetIssue(const String& project, const String& issue_id, ForgeIssue& out) override;
	ForgeResult CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out) override;
	ForgeResult AddComment(const String& project, const String& issue_id,
	                       const ForgeComment& comment, ForgeComment& out) override;
	ForgeResult SetIssueState(const String& project, const String& issue_id,
	                          bool open, ForgeIssue& out) override;
	ForgeResult ListComments(const String& project, const String& issue_id,
	                         Vector<ForgeComment>& out) override;

	// --- record kinds: wiki pages, discussion topics and posts, release files ---
	// Put* with an empty expected_revision creates or overwrites; a non-empty
	// one must match the current revision or the call fails with a conflict.
	ForgeResult ListWikiPages(const String& project, Vector<PrivateHubWikiPage>& out);
	ForgeResult GetWikiPage(const String& project, const String& slug, PrivateHubWikiPage& out);
	ForgeResult PutWikiPage(const String& project, const PrivateHubWikiPage& page,
	                        const String& expected_revision, String& new_revision);

	ForgeResult ListDiscussionTopics(const String& project, Vector<PrivateHubDiscussionTopic>& out);
	ForgeResult GetDiscussionTopic(const String& project, const String& slug, PrivateHubDiscussionTopic& out);
	ForgeResult PutDiscussionTopic(const String& project, const PrivateHubDiscussionTopic& topic,
	                               const String& expected_revision, String& new_revision);
	ForgeResult ListDiscussionPosts(const String& project, const String& topic_slug, Vector<PrivateHubPost>& out);
	ForgeResult PutDiscussionPost(const String& project, const PrivateHubPost& post,
	                              const String& expected_revision, String& new_revision);

	ForgeResult ListReleaseFiles(const String& project, Vector<PrivateHubReleaseFile>& out);
	ForgeResult GetReleaseFile(const String& project, const String& name, PrivateHubReleaseFile& out);
	ForgeResult PutReleaseFile(const String& project, const PrivateHubReleaseFile& file,
	                           const String& expected_revision, String& new_revision);

	// Registers the four schema cards. Idempotent; the constructor calls it.
	static void RegisterSchemas();

	// --- workspace inspection ---
	static bool ValidName(const String& name);       // org or repo name
	// Project key: one or two ValidName segments ("slug" or "org/repo").
	static bool ValidProjectKey(const String& project);
	// Record id: [A-Za-z0-9_-], plus '.' when allow_dot (release file names).
	static bool SafeRecordId(const String& id, bool allow_dot);
	bool        IsContained(const String& rel) const;
	String      RepoPath(const String& org, const String& repo) const;

	RepoInfo RepoStatus(const String& org, const String& repo) const;
	bool     ListOrganizations(Vector<String>& out) const;
	bool     ListRepositories(const String& org, Vector<String>& out) const;

	// Collection for one record kind of one project: "<kind>-<project>", with
	// '/' flattened so the id is filesystem-safe.
	static String CollectionFor(const char *kind, const String& project);

	// Writes one envelope record. The envelope carries schema, kind, workspace,
	// visibility "private" and provenance_ref; the store sets the revision.
	// Data is validated against the schema card when one is registered.
	static StoreResult PutRecord(IRecordCollection& coll, const String& schema_id,
	                             const String& record_kind, const String& workspace_id,
	                             const String& id, const ValueMap& data,
	                             const String& provenance_ref, const String& expected_revision,
	                             String& new_revision);

private:
	IRecordCollection& Store(const char *kind, const String& project);
	IRecordCollection& Issues(const String& project);

	String        workspace_root;
	String        issues_root;
	ForgeCapabilities caps;
};

END_UPP_NAMESPACE

#endif
