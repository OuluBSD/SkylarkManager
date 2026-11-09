#include "PrivateHubWorkspace.h"

NAMESPACE_UPP

bool PrivateHubWorkspaceBackend::ValidName(const String& name)
{
	if(name.IsEmpty() || name.GetCount() > 100)
		return false;
	if(name == "." || name == "..")
		return false;
	for(int i = 0; i < name.GetCount(); i++) {
		int c = name[i];
		bool safe = IsAlpha(c) || IsDigit(c) || c == '_' || c == '-' || c == '.';
		if(!safe)
			return false;
	}
	return true;
}

PrivateHubWorkspaceBackend::PrivateHubWorkspaceBackend(const String& workspace_root_,
                                                       const String& issues_root_)
{
	ASSERT_(workspace_root_.GetCount(), "PrivateHubWorkspace needs a workspace root");
	workspace_root = NormalizePath(workspace_root_);
	RealizeDirectory(workspace_root);
	issues_root = issues_root_;
	caps.list_issues = true;
	caps.get_issue = true;
	caps.create_issue = true;
	caps.add_comment = true;
	caps.set_issue_state = true;
	caps.list_comments = true;
	RegisterSchemas();
}

String PrivateHubWorkspaceBackend::CollectionFor(const char *kind, const String& project)
{
	// Store collection ids must be filesystem-safe: flatten "org/repo".
	String safe = project;
	safe.Replace("/", "__");
	return String(kind) + "-" + safe;
}

IRecordCollection& PrivateHubWorkspaceBackend::Store(const char *kind, const String& project)
{
	return TheStoreManager().JsonCollection("privatehub", issues_root, CollectionFor(kind, project));
}

IRecordCollection& PrivateHubWorkspaceBackend::Issues(const String& project)
{
	return Store("issues", project);
}

bool PrivateHubWorkspaceBackend::IsContained(const String& rel) const
{
	String full = NormalizePath(AppendFileName(workspace_root, rel));
	return full.StartsWith(workspace_root) && full.GetCount() > workspace_root.GetCount();
}

String PrivateHubWorkspaceBackend::RepoPath(const String& org, const String& repo) const
{
	return AppendFileName(AppendFileName(workspace_root, org), repo);
}

static Value IssueToJson(ForgeIssue& iss)
{
	return StoreAsJsonValue(iss);
}

static Value CommentToJson(ForgeComment& c)
{
	return StoreAsJsonValue(c);
}

// git repository facts, executed argv-only (no shell).
static bool GitStatusPorcelain(const String& repo_path, String& branch, String& head,
                               int& dirty, String& error)
{
	LocalProcess p;
	if(!p.Start2("git", {"-C", repo_path, "rev-parse", "--abbrev-ref", "HEAD"})) {
		error = "cannot start git";
		return false;
	}
	p.CloseWrite();
	String out;
	uint32 t0 = GetTickCount();
	for(;;) {
		String co, ce;
		bool more = p.Read2(co, ce);
		out << co;
		if(!more)
			break;
		if(GetTickCount() - t0 > 20000) {
			p.Kill();
			error = "git timed out";
			return false;
		}
		Sleep(2);
	}
	branch = TrimBoth(out);

	LocalProcess p2;
	if(!p2.Start2("git", {"-C", repo_path, "rev-parse", "HEAD"})) {
		error = "cannot start git";
		return false;
	}
	p2.CloseWrite();
	out.Clear();
	t0 = GetTickCount();
	for(;;) {
		String co, ce;
		bool more = p2.Read2(co, ce);
		out << co;
		if(!more)
			break;
		if(GetTickCount() - t0 > 20000) {
			p2.Kill();
			error = "git timed out";
			return false;
		}
		Sleep(2);
	}
	head = TrimBoth(out);

	LocalProcess p3;
	if(!p3.Start2("git", {"-C", repo_path, "status", "--porcelain"})) {
		error = "cannot start git";
		return false;
	}
	p3.CloseWrite();
	out.Clear();
	t0 = GetTickCount();
	for(;;) {
		String co, ce;
		bool more = p3.Read2(co, ce);
		out << co;
		if(!more)
			break;
		if(GetTickCount() - t0 > 20000) {
			p3.Kill();
			error = "git timed out";
			return false;
		}
		Sleep(2);
	}
	dirty = 0;
	for(const String& line : Split(out, '\n'))
		if(TrimBoth(line).GetCount())
			dirty++;
	return true;
}

RepoInfo PrivateHubWorkspaceBackend::RepoStatus(const String& org, const String& repo) const
{
	RepoInfo ri;
	ri.org = org;
	ri.repo = repo;
	if(!ValidName(org) || !ValidName(repo)) {
		ri.error = "invalid org or repo name";
		return ri;
	}
	if(!IsContained(org + "/" + repo)) {
		ri.error = "path escapes workspace root";
		return ri;
	}
	ri.path = RepoPath(org, repo);
	ri.is_repo = DirectoryExists(AppendFileName(ri.path, ".git"));
	if(!ri.is_repo) {
		ri.error = "not a git repository";
		return ri;
	}
	String err;
	if(!GitStatusPorcelain(ri.path, ri.branch, ri.head_commit, ri.dirty_files, err)) {
		ri.error = err;
		ri.is_repo = false;
	}
	return ri;
}

bool PrivateHubWorkspaceBackend::ListOrganizations(Vector<String>& out) const
{
	out.Clear();
	for(const FindFile& ff : FindFile(workspace_root + "/*"))
		if(ff.IsDirectory() && ff.GetName() != "." && ff.GetName() != "..")
			out.Add(ff.GetName());
	Sort(out);
	return true;
}

bool PrivateHubWorkspaceBackend::ListRepositories(const String& org, Vector<String>& out) const
{
	out.Clear();
	if(!ValidName(org) || !IsContained(org + "/x"))
		return false;
	String orgdir = AppendFileName(workspace_root, org);
	if(!DirectoryExists(orgdir))
		return false;
	for(const FindFile& ff : FindFile(orgdir + "/*"))
		if(ff.IsDirectory() && DirectoryExists(AppendFileName(AppendFileName(orgdir, ff.GetName()), ".git")))
			out.Add(ff.GetName());
	Sort(out);
	return true;
}

// --- IForgeBackend over the site's JSON issue store ---

ForgeResult PrivateHubWorkspaceBackend::ListIssues(const String& project, Vector<ForgeIssue>& out)
{
	out.Clear();
	Vector<String> ids;
	StoreResult lr = Issues(project).List(ids);
	if(!lr.ok)
		return Failed("list-issues", project, lr.error);
	for(const String& id : ids) {
		if(id.Find('.') >= 0) // comment records are "<issue>.<comment>"
			continue;
		String json, rev;
		if(!Issues(project).Get(id, json, rev).ok)
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			ForgeIssue iss;
			LoadFromJsonValue(iss, v);
			out.Add(iss);
		}
		catch(...) {}
	}
	return Ok("list-issues", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::GetIssue(const String& project, const String& issue_id, ForgeIssue& out)
{
	String json, rev;
	StoreResult gr = Issues(project).Get(issue_id, json, rev);
	if(!gr.ok)
		return Failed("get-issue", project, gr.error);
	try {
		Value v = ParseJSON(json);
		if(!v.Is<ValueMap>())
			return Failed("get-issue", project, "corrupt issue record");
		LoadFromJsonValue(out, v);
	}
	catch(Exc& e) {
		return Failed("get-issue", project, String("cannot parse issue: ") + e);
	}
	return Ok("get-issue", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out)
{
	if(draft.title.IsEmpty())
		return Failed("create-issue", project, "issue needs a title");
	if(draft.author.IsEmpty())
		return Failed("create-issue", project, "issue needs an authenticated author");
	String id = "phi_" + AsString(Uuid::Create());
	ForgeIssue iss = draft;
	iss.project = project;
	iss.issue_id = id;
	iss.open = true;
	iss.created = GetSysTime();
	iss.updated = iss.created;
	String new_rev;
	StoreResult pr = Issues(project).Put(id, AsJSON(IssueToJson(iss)), String(), new_rev);
	if(!pr.ok)
		return Failed("create-issue", project, pr.error);
	out = iss;
	return Ok("create-issue", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::AddComment(const String& project, const String& issue_id,
                                                   const ForgeComment& comment, ForgeComment& out)
{
	if(comment.body.IsEmpty())
		return Failed("add-comment", project, "comment needs a body");
	if(comment.author.IsEmpty())
		return Failed("add-comment", project, "comment needs an authenticated author");
	ForgeIssue got;
	ForgeResult gr = GetIssue(project, issue_id, got);
	if(!gr.ok)
		return gr;
	ForgeComment c = comment;
	c.issue_id = issue_id;
	c.comment_id = "phc_" + AsString(Uuid::Create());
	c.created = GetSysTime();
	VectorMap<String, String> kv; // comments stored as sibling records: "<issue>#<comment>"
	String new_rev;
	StoreResult pr = Issues(project).Put(issue_id + "." + c.comment_id,
		AsJSON(CommentToJson(c)), String(), new_rev);
	if(!pr.ok)
		return Failed("add-comment", project, pr.error);
	out = c;
	return Ok("add-comment", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::SetIssueState(const String& project, const String& issue_id,
                                                      bool open, ForgeIssue& out)
{
	ForgeIssue got;
	ForgeResult gr = GetIssue(project, issue_id, got);
	if(!gr.ok)
		return gr;
	got.open = open;
	got.updated = GetSysTime();
	String json, rev;
	Issues(project).Get(issue_id, json, rev);
	String new_rev;
	StoreResult pr = Issues(project).Put(issue_id, AsJSON(IssueToJson(got)), rev, new_rev);
	if(!pr.ok)
		return Failed("set-issue-state", project, pr.error);
	out = got;
	return Ok("set-issue-state", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::ListComments(const String& project, const String& issue_id,
                                                     Vector<ForgeComment>& out)
{
	out.Clear();
	ForgeIssue got;
	ForgeResult gr = GetIssue(project, issue_id, got);
	if(!gr.ok)
		return gr;
	Vector<String> ids;
	Issues(project).List(ids);
	String prefix = issue_id + ".";
	for(const String& id : ids) {
		if(!id.StartsWith(prefix))
			continue;
		String json, rev;
		if(!Issues(project).Get(id, json, rev).ok)
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			ForgeComment c;
			LoadFromJsonValue(c, v);
			out.Add(c);
		}
		catch(...) {}
	}
	return Ok("list-comments", project, ForgeProvenance::LocalPersistent);
}

// --- record kinds ---------------------------------------------------------

// Project keys are one or two [A-Za-z0-9._-] segments ("slug" or "org/repo").
bool PrivateHubWorkspaceBackend::ValidProjectKey(const String& project)
{
	if(project.IsEmpty() || project.GetCount() > 201)
		return false;
	Vector<String> parts = Split(project, '/', false);
	if(parts.GetCount() < 1 || parts.GetCount() > 2)
		return false;
	for(const String& part : parts)
		if(!ValidName(part))
			return false;
	return true;
}

// Record ids: [A-Za-z0-9_-], plus '.' where the kind allows it (release files).
bool PrivateHubWorkspaceBackend::SafeRecordId(const String& id, bool allow_dot)
{
	if(id.IsEmpty() || id.GetCount() > 100 || id == "." || id == "..")
		return false;
	for(int i = 0; i < id.GetCount(); i++) {
		int c = id[i];
		bool safe = IsAlpha(c) || IsDigit(c) || c == '_' || c == '-' || (allow_dot && c == '.');
		if(!safe)
			return false;
	}
	return true;
}

static void AddCardField(WebSchemaCard& card, const char *name, const char *type, bool required, int max_len)
{
	WebSchemaField f;
	f.name = name;
	f.type = type;
	f.required = required;
	f.max_len = max_len;
	card.fields.Add(f);
}

void PrivateHubWorkspaceBackend::RegisterSchemas()
{
	WebSchemaCard wiki;
	wiki.schema_id = PRIVATEHUB_SCHEMA_WIKI_PAGE;
	wiki.version = "1";
	AddCardField(wiki, "slug", "string", true, 100);
	AddCardField(wiki, "title", "string", true, 256);
	AddCardField(wiki, "body", "string", false, 0);
	AddCardField(wiki, "updated", "string", false, 64);
	AddCardField(wiki, "provenance", "string", false, 64);
	SchemaRegistry::Register(wiki);

	WebSchemaCard topic;
	topic.schema_id = PRIVATEHUB_SCHEMA_DISCUSSION_TOPIC;
	topic.version = "1";
	AddCardField(topic, "slug", "string", true, 100);
	AddCardField(topic, "forum", "string", false, 100);
	AddCardField(topic, "title", "string", true, 256);
	AddCardField(topic, "author", "string", true, 100);
	AddCardField(topic, "created", "string", false, 64);
	AddCardField(topic, "provenance", "string", false, 64);
	SchemaRegistry::Register(topic);

	WebSchemaCard post;
	post.schema_id = PRIVATEHUB_SCHEMA_DISCUSSION_POST;
	post.version = "1";
	AddCardField(post, "post_id", "string", true, 100);
	AddCardField(post, "topic", "string", true, 100);
	AddCardField(post, "author", "string", true, 100);
	AddCardField(post, "body", "string", true, 0);
	AddCardField(post, "created", "string", false, 64);
	AddCardField(post, "provenance", "string", false, 64);
	SchemaRegistry::Register(post);

	WebSchemaCard file;
	file.schema_id = PRIVATEHUB_SCHEMA_RELEASE_FILE;
	file.version = "1";
	AddCardField(file, "name", "string", true, 100);
	AddCardField(file, "release", "string", false, 100);
	AddCardField(file, "size_bytes", "int", false, 0);
	AddCardField(file, "url", "string", false, 1024);
	AddCardField(file, "updated", "string", false, 64);
	AddCardField(file, "provenance", "string", false, 64);
	SchemaRegistry::Register(file);
}

StoreResult PrivateHubWorkspaceBackend::PutRecord(IRecordCollection& coll, const String& schema_id,
                                                  const String& record_kind, const String& workspace_id,
                                                  const String& id, const ValueMap& data,
                                                  const String& provenance_ref,
                                                  const String& expected_revision, String& new_revision)
{
	JsonFileStore *store = dynamic_cast<JsonFileStore*>(&coll);
	if(!store)
		return StoreResult("collection is not a JSON file store");
	WebSchemaCard card;
	String err;
	if(SchemaRegistry::Find(schema_id, card) && !SchemaRegistry::Validate(card, data, err))
		return StoreResult("schema validation failed: " + err);
	ValueMap env;
	env.Add("schema_id", schema_id);
	env.Add("schema_version", "1");
	env.Add("workspace_id", workspace_id);
	env.Add("stable_id", id);
	env.Add("record_kind", record_kind);
	env.Add("revision", "");
	env.Add("visibility", "private");
	env.Add("provenance_ref", provenance_ref);
	return store->PutOpaque(id, AsJSON(Value(env)), AsJSON(Value(data)), expected_revision, new_revision);
}

// Reads one record's data and envelope revision. Corrupt data is an error.
static bool ReadDoc(IRecordCollection& coll, const String& id, ValueMap& data, String& rev, String& error)
{
	String json;
	StoreResult g = coll.Get(id, json, rev);
	if(!g.ok) {
		error = g.error;
		return false;
	}
	try {
		Value v = ParseJSON(json);
		if(!v.Is<ValueMap>()) {
			error = "corrupt record (data is not an object)";
			return false;
		}
		data = (ValueMap)v;
	}
	catch(Exc& e) {
		error = String("corrupt record: ") + e;
		return false;
	}
	return true;
}

// Lists records of one kind. Top-level records have no '.' in their id; with
// a prefix, only child records "<prefix><id>" are loaded.
template <class T>
static ForgeResult LoadRecords(IRecordCollection& coll, const char *op, const String& project,
                               const String& prefix, Vector<T>& out)
{
	out.Clear();
	Vector<String> ids;
	StoreResult lr = coll.List(ids);
	if(!lr.ok)
		return IForgeBackend::Failed(op, project, lr.error);
	for(const String& id : ids) {
		if(prefix.IsEmpty() ? id.Find('.') >= 0 : !id.StartsWith(prefix))
			continue;
		ValueMap data;
		String rev, err;
		if(!ReadDoc(coll, id, data, rev, err))
			return IForgeBackend::Failed(op, project, "record '" + id + "': " + err);
		T item;
		LoadFromJsonValue(item, Value(data));
		item.revision = rev;
		out.Add(item);
	}
	return IForgeBackend::Ok(op, project, ForgeProvenance::LocalPersistent);
}

template <class T>
static ForgeResult GetRecord(IRecordCollection& coll, const char *op, const String& project,
                             const String& id, T& out)
{
	ValueMap data;
	String rev, err;
	if(!ReadDoc(coll, id, data, rev, err))
		return IForgeBackend::Failed(op, project, "record '" + id + "': " + err);
	LoadFromJsonValue(out, Value(data));
	out.revision = rev;
	return IForgeBackend::Ok(op, project, ForgeProvenance::LocalPersistent);
}

template <class T>
static ForgeResult PutKind(IRecordCollection& coll, const char *op, const String& project,
                           const char *schema_id, const char *record_kind, const String& id,
                           const T& item, const String& expected_revision, String& new_revision)
{
	Value stored = StoreAsJsonValue(item);
	if(!stored.Is<ValueMap>())
		return IForgeBackend::Failed(op, project, "record cannot be serialized");
	StoreResult r = PrivateHubWorkspaceBackend::PutRecord(coll, schema_id, record_kind, project, id,
	                                                      (ValueMap)stored, String(), expected_revision,
	                                                      new_revision);
	if(!r.ok)
		return IForgeBackend::Failed(op, project, r.error);
	return IForgeBackend::Ok(op, project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::ListWikiPages(const String& project, Vector<PrivateHubWikiPage>& out)
{
	if(!ValidProjectKey(project))
		return Failed("wiki-list", project, "invalid project key");
	return LoadRecords(Store("wiki", project), "wiki-list", project, String(), out);
}

ForgeResult PrivateHubWorkspaceBackend::GetWikiPage(const String& project, const String& slug, PrivateHubWikiPage& out)
{
	if(!ValidProjectKey(project) || !SafeRecordId(slug, false))
		return Failed("wiki-get", project, "invalid project key or page slug");
	return GetRecord(Store("wiki", project), "wiki-get", project, slug, out);
}

ForgeResult PrivateHubWorkspaceBackend::PutWikiPage(const String& project, const PrivateHubWikiPage& page,
                                                    const String& expected_revision, String& new_revision)
{
	if(!ValidProjectKey(project) || !SafeRecordId(page.slug, false))
		return Failed("wiki-put", project, "invalid project key or page slug");
	return PutKind(Store("wiki", project), "wiki-put", project, PRIVATEHUB_SCHEMA_WIKI_PAGE,
	               PRIVATEHUB_SCHEMA_WIKI_PAGE, page.slug, page, expected_revision, new_revision);
}

ForgeResult PrivateHubWorkspaceBackend::ListDiscussionTopics(const String& project, Vector<PrivateHubDiscussionTopic>& out)
{
	if(!ValidProjectKey(project))
		return Failed("topic-list", project, "invalid project key");
	return LoadRecords(Store("discussion", project), "topic-list", project, String(), out);
}

ForgeResult PrivateHubWorkspaceBackend::GetDiscussionTopic(const String& project, const String& slug, PrivateHubDiscussionTopic& out)
{
	if(!ValidProjectKey(project) || !SafeRecordId(slug, false))
		return Failed("topic-get", project, "invalid project key or topic slug");
	return GetRecord(Store("discussion", project), "topic-get", project, slug, out);
}

ForgeResult PrivateHubWorkspaceBackend::PutDiscussionTopic(const String& project, const PrivateHubDiscussionTopic& topic,
                                                           const String& expected_revision, String& new_revision)
{
	if(!ValidProjectKey(project) || !SafeRecordId(topic.slug, false))
		return Failed("topic-put", project, "invalid project key or topic slug");
	return PutKind(Store("discussion", project), "topic-put", project, PRIVATEHUB_SCHEMA_DISCUSSION_TOPIC,
	               PRIVATEHUB_SCHEMA_DISCUSSION_TOPIC, topic.slug, topic, expected_revision, new_revision);
}

ForgeResult PrivateHubWorkspaceBackend::ListDiscussionPosts(const String& project, const String& topic_slug, Vector<PrivateHubPost>& out)
{
	if(!ValidProjectKey(project) || !SafeRecordId(topic_slug, false))
		return Failed("post-list", project, "invalid project key or topic slug");
	return LoadRecords(Store("discussion", project), "post-list", project, topic_slug + ".", out);
}

ForgeResult PrivateHubWorkspaceBackend::PutDiscussionPost(const String& project, const PrivateHubPost& post,
                                                          const String& expected_revision, String& new_revision)
{
	if(!ValidProjectKey(project) || !SafeRecordId(post.topic, false) || !SafeRecordId(post.post_id, false))
		return Failed("post-put", project, "invalid project key, topic or post id");
	ValueMap topic_data;
	String topic_rev, err;
	if(!ReadDoc(Store("discussion", project), post.topic, topic_data, topic_rev, err))
		return Failed("post-put", project, "topic '" + post.topic + "' not found: " + err);
	return PutKind(Store("discussion", project), "post-put", project, PRIVATEHUB_SCHEMA_DISCUSSION_POST,
	               PRIVATEHUB_SCHEMA_DISCUSSION_TOPIC, post.topic + "." + post.post_id, post,
	               expected_revision, new_revision);
}

ForgeResult PrivateHubWorkspaceBackend::ListReleaseFiles(const String& project, Vector<PrivateHubReleaseFile>& out)
{
	if(!ValidProjectKey(project))
		return Failed("release-list", project, "invalid project key");
	// Release file names contain dots, so the top-level filter cannot apply here.
	out.Clear();
	Vector<String> ids;
	StoreResult lr = Store("release", project).List(ids);
	if(!lr.ok)
		return Failed("release-list", project, lr.error);
	for(const String& id : ids) {
		ValueMap data;
		String rev, err;
		if(!ReadDoc(Store("release", project), id, data, rev, err))
			return Failed("release-list", project, "record '" + id + "': " + err);
		PrivateHubReleaseFile item;
		LoadFromJsonValue(item, Value(data));
		item.revision = rev;
		out.Add(item);
	}
	return Ok("release-list", project, ForgeProvenance::LocalPersistent);
}

ForgeResult PrivateHubWorkspaceBackend::GetReleaseFile(const String& project, const String& name, PrivateHubReleaseFile& out)
{
	if(!ValidProjectKey(project) || !SafeRecordId(name, true))
		return Failed("release-get", project, "invalid project key or file name");
	return GetRecord(Store("release", project), "release-get", project, name, out);
}

ForgeResult PrivateHubWorkspaceBackend::PutReleaseFile(const String& project, const PrivateHubReleaseFile& file,
                                                       const String& expected_revision, String& new_revision)
{
	if(!ValidProjectKey(project) || !SafeRecordId(file.name, true))
		return Failed("release-put", project, "invalid project key or file name");
	return PutKind(Store("release", project), "release-put", project, PRIVATEHUB_SCHEMA_RELEASE_FILE,
	               PRIVATEHUB_SCHEMA_RELEASE_FILE, file.name, file, expected_revision, new_revision);
}

END_UPP_NAMESPACE
