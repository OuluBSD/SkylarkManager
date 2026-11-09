#include "PrivateHubWorkspace.h"
#include "PrivateHubMigration.h"

NAMESPACE_UPP

#define PRIVATEHUB_KIND_ISSUE "privatehub/issue"

String PrivateHubLegacyReport::Summary() const
{
	String s;
	s << "project: " << project << "\n"
	  << "issues imported: " << issues << "\n"
	  << "issue comments imported: " << issue_comments << "\n"
	  << "wiki pages imported: " << wiki_pages << "\n"
	  << "discussion topics imported: " << discussion_topics << "\n"
	  << "discussion posts imported: " << discussion_posts << "\n"
	  << "release files imported: " << release_files << "\n"
	  << "already present (skipped): " << already_present << "\n"
	  << "rejected entries: " << rejected << "\n";
	for(const String& a : absent)
		s << "absent store: " << a << "\n";
	for(const String& c : corrupt)
		s << "corrupt store: " << c << "\n";
	for(const String& n : notes)
		s << "note: " << n << "\n";
	s << (Clean() ? "result: clean\n" : "result: not clean\n");
	return s;
}

static String Str(const ValueMap& m, const char *key)
{
	return AsString(m.Get(key));
}

// Legacy timestamps are "yyyy-mm-dd" (or empty). Anything else is left unset,
// never guessed.
static Time LegacyTime(const String& s)
{
	Vector<String> p = Split(TrimBoth(s), '-', false);
	if(p.GetCount() != 3 || p[0].GetCount() != 4 || p[1].GetCount() != 2 || p[2].GetCount() != 2)
		return Time();
	for(const String& part : p)
		for(int i = 0; i < part.GetCount(); i++)
			if(!IsDigit(part[i]))
				return Time();
	int y = ScanInt(p[0]), m = ScanInt(p[1]), d = ScanInt(p[2]);
	if(m < 1 || m > 12 || d < 1 || d > 31)
		return Time();
	return Time(y, m, d);
}

// Reads a legacy store. Returns false when it is absent or not a JSON array;
// the report records which.
static bool LoadStore(const String& path, const char *name, ValueArray& out, PrivateHubLegacyReport& rep)
{
	if(!FileExists(path)) {
		rep.absent.Add(name);
		return false;
	}
	try {
		Value v = ParseJSON(LoadFile(path));
		if(!v.Is<ValueArray>()) {
			rep.corrupt.Add(String(name) + ": not a JSON array");
			return false;
		}
		out = (ValueArray)v;
	}
	catch(Exc& e) {
		rep.corrupt.Add(String(name) + ": " + e);
		return false;
	}
	return true;
}

static void Reject(PrivateHubLegacyReport& rep, const char *store, const String& why)
{
	rep.rejected++;
	rep.notes.Add(String(store) + ": " + why);
}

// Writes one migrated record unless its id already exists. Returns false on a
// write failure, which marks the report failed.
static bool WriteMigrated(IRecordCollection& coll, const String& schema_id, const String& kind,
                          const String& project, const String& id, const ValueMap& data,
                          const String& ref, int& counter, PrivateHubLegacyReport& rep)
{
	String json, rev;
	if(coll.Get(id, json, rev).ok) {
		rep.already_present++;
		return true;
	}
	String new_rev;
	StoreResult r = PrivateHubWorkspaceBackend::PutRecord(coll, schema_id, kind, project, id, data,
	                                                      ref, "", new_rev);
	if(!r.ok) {
		rep.failed = true;
		rep.notes.Add("write failed for '" + id + "': " + r.error);
		return false;
	}
	counter++;
	return true;
}

static void MigrateTickets(const ValueArray& tickets, const String& project, const String& store_path,
                           IRecordCollection& issues, PrivateHubLegacyReport& rep)
{
	const char *store = "tickets_store.json";
	for(int i = 0; i < tickets.GetCount(); i++) {
		if(!tickets[i].Is<ValueMap>()) {
			Reject(rep, store, "entry is not an object");
			continue;
		}
		ValueMap t = (ValueMap)tickets[i];
		int num = (int)t.Get("num");
		String title = Str(t, "summary");
		if(num <= 0 || title.IsEmpty()) {
			Reject(rep, store, "ticket needs a positive num and a summary");
			continue;
		}
		String id = "legacy-" + AsString(num);
		String ref = store_path + "#num=" + AsString(num);

		ForgeIssue iss;
		iss.issue_id = id;
		iss.project = project;
		iss.title = title;
		iss.body = Str(t, "description");
		iss.author = Str(t, "author");
		iss.open = Str(t, "status") != "closed";
		iss.created = LegacyTime(Str(t, "created_at"));
		iss.updated = iss.created;
		Value stored = StoreAsJsonValue(iss);
		if(!stored.Is<ValueMap>()) {
			Reject(rep, store, "ticket " + AsString(num) + " cannot be serialized");
			continue;
		}
		ValueMap data = (ValueMap)stored;
		data.Set("provenance", PRIVATEHUB_PROVENANCE_MIGRATED);
		if(!WriteMigrated(issues, "", PRIVATEHUB_KIND_ISSUE, project, id, data, ref, rep.issues, rep))
			return;

		if(!t.Get("comments").Is<ValueArray>())
			continue;
		ValueArray comments = (ValueArray)t.Get("comments");
		for(int c = 0; c < comments.GetCount(); c++) {
			if(!comments[c].Is<ValueMap>()) {
				Reject(rep, store, "comment of ticket " + AsString(num) + " is not an object");
				continue;
			}
			ValueMap cm = (ValueMap)comments[c];
			String body = Str(cm, "text");
			if(body.IsEmpty()) {
				Reject(rep, store, "comment " + AsString(c) + " of ticket " + AsString(num) + " has no text");
				continue;
			}
			ForgeComment fc;
			fc.comment_id = "c" + AsString(c);
			fc.issue_id = id;
			fc.author = Str(cm, "author");
			fc.body = body;
			fc.created = LegacyTime(Str(cm, "created_at"));
			Value cstored = StoreAsJsonValue(fc);
			if(!cstored.Is<ValueMap>())
				continue;
			ValueMap cdata = (ValueMap)cstored;
			cdata.Set("provenance", PRIVATEHUB_PROVENANCE_MIGRATED);
			WriteMigrated(issues, "", PRIVATEHUB_KIND_ISSUE, project, id + "." + fc.comment_id, cdata,
			              ref + "#comment=" + AsString(c), rep.issue_comments, rep);
			if(rep.failed)
				return;
		}
	}
}

static void MigrateWiki(const ValueArray& pages, const String& project, const String& store_path,
                        IRecordCollection& coll, PrivateHubLegacyReport& rep)
{
	const char *store = "wiki_store.json";
	for(int i = 0; i < pages.GetCount(); i++) {
		if(!pages[i].Is<ValueMap>()) {
			Reject(rep, store, "entry is not an object");
			continue;
		}
		ValueMap p = (ValueMap)pages[i];
		String slug = Str(p, "slug");
		if(!PrivateHubWorkspaceBackend::SafeRecordId(slug, false) || Str(p, "title").IsEmpty()) {
			Reject(rep, store, "page needs a safe slug and a title");
			continue;
		}
		PrivateHubWikiPage page;
		page.slug = slug;
		page.title = Str(p, "title");
		page.body = Str(p, "content");
		page.updated = Str(p, "updated_at");
		page.provenance = PRIVATEHUB_PROVENANCE_MIGRATED;
		Value stored = StoreAsJsonValue(page);
		if(!stored.Is<ValueMap>()) {
			Reject(rep, store, "page '" + slug + "' cannot be serialized");
			continue;
		}
		if(!WriteMigrated(coll, PRIVATEHUB_SCHEMA_WIKI_PAGE, PRIVATEHUB_SCHEMA_WIKI_PAGE, project, slug,
		                  (ValueMap)stored, store_path + "#slug=" + slug, rep.wiki_pages, rep))
			return;
	}
}

static void MigrateDiscussion(const ValueArray& topics, const String& project, const String& store_path,
                              IRecordCollection& coll, PrivateHubLegacyReport& rep)
{
	const char *store = "discussion_store.json";
	for(int i = 0; i < topics.GetCount(); i++) {
		if(!topics[i].Is<ValueMap>()) {
			Reject(rep, store, "entry is not an object");
			continue;
		}
		ValueMap t = (ValueMap)topics[i];
		int id = (int)t.Get("id");
		String title = Str(t, "title");
		String author = Str(t, "author");
		if(id <= 0 || title.IsEmpty() || author.IsEmpty()) {
			Reject(rep, store, "topic needs a positive id, a title and an author");
			continue;
		}
		String slug = "legacy-" + AsString(id);
		String ref = store_path + "#id=" + AsString(id);

		PrivateHubDiscussionTopic topic;
		topic.slug = slug;
		String forum = Str(t, "forum");
		topic.forum = forum.IsEmpty() ? String("general") : forum;
		topic.title = title;
		topic.author = author;
		topic.created = Str(t, "created_at");
		topic.provenance = PRIVATEHUB_PROVENANCE_MIGRATED;
		Value stored = StoreAsJsonValue(topic);
		if(!stored.Is<ValueMap>()) {
			Reject(rep, store, "topic " + AsString(id) + " cannot be serialized");
			continue;
		}
		if(!WriteMigrated(coll, PRIVATEHUB_SCHEMA_DISCUSSION_TOPIC, PRIVATEHUB_SCHEMA_DISCUSSION_TOPIC,
		                  project, slug, (ValueMap)stored, ref, rep.discussion_topics, rep))
			return;

		if(!t.Get("posts").Is<ValueArray>())
			continue;
		ValueArray posts = (ValueArray)t.Get("posts");
		for(int k = 0; k < posts.GetCount(); k++) {
			if(!posts[k].Is<ValueMap>()) {
				Reject(rep, store, "post of topic " + AsString(id) + " is not an object");
				continue;
			}
			ValueMap pm = (ValueMap)posts[k];
			String body = Str(pm, "text");
			String post_author = Str(pm, "author");
			if(body.IsEmpty() || post_author.IsEmpty()) {
				Reject(rep, store, "post " + AsString(k) + " of topic " + AsString(id) + " needs an author and text");
				continue;
			}
			PrivateHubPost post;
			post.post_id = "p" + AsString(k);
			post.topic = slug;
			post.author = post_author;
			post.body = body;
			post.created = Str(pm, "created_at");
			post.provenance = PRIVATEHUB_PROVENANCE_MIGRATED;
			Value pstored = StoreAsJsonValue(post);
			if(!pstored.Is<ValueMap>())
				continue;
			WriteMigrated(coll, PRIVATEHUB_SCHEMA_DISCUSSION_POST, PRIVATEHUB_SCHEMA_DISCUSSION_TOPIC, project,
			              slug + "." + post.post_id, (ValueMap)pstored, ref + "#post=" + AsString(k),
			              rep.discussion_posts, rep);
			if(rep.failed)
				return;
		}
	}
}

static void MigrateFiles(const ValueArray& files, const String& project, const String& store_path,
                         IRecordCollection& coll, PrivateHubLegacyReport& rep)
{
	const char *store = "files_store.json";
	for(int i = 0; i < files.GetCount(); i++) {
		if(!files[i].Is<ValueMap>()) {
			Reject(rep, store, "entry is not an object");
			continue;
		}
		ValueMap f = (ValueMap)files[i];
		String name = Str(f, "name");
		if(!PrivateHubWorkspaceBackend::SafeRecordId(name, true)) {
			Reject(rep, store, "file name is missing or unsafe");
			continue;
		}
		PrivateHubReleaseFile rel;
		rel.name = name;
		rel.release = Str(f, "version");
		rel.size_bytes = (int64)f.Get("size_bytes");
		rel.url = Str(f, "url");
		rel.updated = Str(f, "date");
		rel.provenance = PRIVATEHUB_PROVENANCE_MIGRATED;
		Value stored = StoreAsJsonValue(rel);
		if(!stored.Is<ValueMap>()) {
			Reject(rep, store, "file '" + name + "' cannot be serialized");
			continue;
		}
		if(!WriteMigrated(coll, PRIVATEHUB_SCHEMA_RELEASE_FILE, PRIVATEHUB_SCHEMA_RELEASE_FILE, project, name,
		                  (ValueMap)stored, store_path + "#name=" + name, rep.release_files, rep))
			return;
	}
}

// Trims whitespace and trailing '/' so "dir/" and "dir" name the same place.
static String CleanDir(const String& dir)
{
	String s = TrimBoth(dir);
	while(s.GetCount() > 1 && s[s.GetCount() - 1] == '/')
		s = s.Left(s.GetCount() - 1);
	return NormalizePath(s);
}

bool PrivateHubMigrateSourceForgeLocal(const String& legacy_dir_in, const String& data_root_in,
                                       PrivateHubLegacyReport& rep)
{
	String legacy_dir = CleanDir(legacy_dir_in);
	String data_root = CleanDir(data_root_in);
	rep.project = GetFileName(~legacy_dir);
	if(!DirectoryExists(legacy_dir)) {
		rep.notes.Add("legacy directory not found: " + legacy_dir);
		return false;
	}
	if(!PrivateHubWorkspaceBackend::ValidProjectKey(rep.project)) {
		rep.notes.Add("legacy directory name is not a valid project key: " + rep.project);
		return false;
	}
	if(data_root.IsEmpty() || data_root == legacy_dir || data_root.StartsWith(legacy_dir + "/")) {
		rep.notes.Add("data root must be outside the legacy directory: " + data_root);
		return false;
	}
	RealizeDirectory(data_root);

	const String base = "privatehub";
	IRecordCollection& issues = TheStoreManager().JsonCollection(base, data_root,
		PrivateHubWorkspaceBackend::CollectionFor("issues", rep.project));
	IRecordCollection& wiki = TheStoreManager().JsonCollection(base, data_root,
		PrivateHubWorkspaceBackend::CollectionFor("wiki", rep.project));
	IRecordCollection& discussion = TheStoreManager().JsonCollection(base, data_root,
		PrivateHubWorkspaceBackend::CollectionFor("discussion", rep.project));
	IRecordCollection& release = TheStoreManager().JsonCollection(base, data_root,
		PrivateHubWorkspaceBackend::CollectionFor("release", rep.project));

	ValueArray items;
	String path;
	path = AppendFileName(legacy_dir, "tickets_store.json");
	if(LoadStore(path, "tickets_store.json", items, rep))
		MigrateTickets(items, rep.project, path, issues, rep);
	path = AppendFileName(legacy_dir, "wiki_store.json");
	if(!rep.failed && LoadStore(path, "wiki_store.json", items, rep))
		MigrateWiki(items, rep.project, path, wiki, rep);
	path = AppendFileName(legacy_dir, "discussion_store.json");
	if(!rep.failed && LoadStore(path, "discussion_store.json", items, rep))
		MigrateDiscussion(items, rep.project, path, discussion, rep);
	path = AppendFileName(legacy_dir, "files_store.json");
	if(!rep.failed && LoadStore(path, "files_store.json", items, rep))
		MigrateFiles(items, rep.project, path, release, rep);
	return true;
}

END_UPP_NAMESPACE
