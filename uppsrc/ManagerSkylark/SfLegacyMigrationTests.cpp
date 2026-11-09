#include "SfLegacyMigrationCli.h"
#include <PrivateHubWorkspace/PrivateHubWorkspace.h>
#include <PrivateHubWorkspace/PrivateHubMigration.h>

NAMESPACE_UPP

// Synthetic checks for the one-shot SourceForge legacy migration. Fixtures are
// shaped like ~/.aria/sites/sourceforge/<project>/ inside a temp directory.

static bool MigExpect(bool cond, const char *what)
{
	if(!cond)
		Cerr() << "FAILED: SourceForge legacy migration: " << what << "\n";
	return cond;
}

static void WriteLegacy(const String& dir, const String& name, const String& json)
{
	RealizeDirectory(dir);
	SaveFile(AppendFileName(dir, name), json);
}

static const char *kTickets =
	"[{\"num\":1,\"summary\":\"Crash on start\",\"status\":\"open\",\"priority\":\"high\","
	"\"milestone\":\"Phase 13\",\"author\":\"alice\",\"created_at\":\"2026-10-30\",\"description\":\"segv\","
	"\"comments\":[{\"author\":\"bob\",\"text\":\"repro attached\",\"created_at\":\"2026-10-31\"}]},"
	"{\"num\":2,\"summary\":\"Docs\",\"status\":\"closed\",\"author\":\"bob\",\"created_at\":\"2026-10-30\",\"description\":\"\"},"
	"{\"num\":0,\"summary\":\"no number\"},"
	"{\"summary\":\"missing num\"}]";

static const char *kWiki =
	"[{\"title\":\"Home\",\"slug\":\"home\",\"content\":\"= Welcome\",\"author\":\"sppp\","
	"\"updated_at\":\"2026-10-30\",\"version\":1},"
	"{\"title\":\"Bad\",\"slug\":\"../escape\",\"content\":\"x\"}]";

static const char *kDiscussion =
	"[{\"id\":3,\"forum\":\"general\",\"title\":\"Hello\",\"author\":\"alice\",\"created_at\":\"2026-10-30\","
	"\"posts\":[{\"author\":\"bob\",\"text\":\"first reply\",\"created_at\":\"2026-10-31\"},"
	"{\"author\":\"\",\"text\":\"anonymous\"}]}]";

static const char *kFiles =
	"[{\"name\":\"topside-1.0.0-linux.tar.gz\",\"version\":\"1.0.0\",\"date\":\"2026-10-30\","
	"\"size_bytes\":14889728,\"size_str\":\"14.2 MB\",\"downloads\":142,\"sha256\":\"ab\","
	"\"url\":\"/projects/topside/files/topside-1.0.0-linux.tar.gz\"},"
	"{\"name\":\"a/b.tar\",\"version\":\"1.0.0\",\"date\":\"2026-10-30\",\"size_bytes\":1,\"url\":\"/x\"}]";

bool RunSfLegacyMigrationSelfTests(const String& scratch_dir)
{
	bool ok = true;
	String legacy = AppendFileName(scratch_dir, "legacy");
	String data = AppendFileName(scratch_dir, "privatehub-data");
	String ws = AppendFileName(scratch_dir, "workspace");
	RealizeDirectory(ws);

	// Full legacy project: 2 tickets (+1 comment), 1 wiki, 1 topic (+1 post), 1 file.
	// Rejected on purpose: 2 tickets, 1 wiki page, 1 post, 1 file = 5.
	String topside = AppendFileName(legacy, "topside");
	WriteLegacy(topside, "tickets_store.json", kTickets);
	WriteLegacy(topside, "wiki_store.json", kWiki);
	WriteLegacy(topside, "discussion_store.json", kDiscussion);
	WriteLegacy(topside, "files_store.json", kFiles);

	// Originals must come out byte-identical.
	Vector<String> before;
	for(const char *n : {"tickets_store.json", "wiki_store.json", "discussion_store.json", "files_store.json"})
		before.Add(LoadFile(AppendFileName(topside, n)));

	PrivateHubLegacyReport r1;
	ok &= MigExpect(PrivateHubMigrateSourceForgeLocal(topside, data, r1), "first import runs");
	ok &= MigExpect(r1.project == "topside", "project is the legacy directory name");
	ok &= MigExpect(r1.issues == 2 && r1.issue_comments == 1, "tickets and comments imported");
	ok &= MigExpect(r1.wiki_pages == 1, "wiki page imported");
	ok &= MigExpect(r1.discussion_topics == 1 && r1.discussion_posts == 1, "topic and post imported");
	ok &= MigExpect(r1.release_files == 1, "release file imported");
	ok &= MigExpect(r1.rejected == 5, "invalid entries rejected, not guessed");
	ok &= MigExpect(!r1.Clean() && !r1.failed, "report with rejections is not clean");
	ok &= MigExpect(r1.absent.IsEmpty() && r1.corrupt.IsEmpty(), "complete project has no absent or corrupt stores");

	// Re-running is safe: every migrated id already exists and is skipped.
	PrivateHubLegacyReport r2;
	ok &= MigExpect(PrivateHubMigrateSourceForgeLocal(topside, data, r2), "second import runs");
	ok &= MigExpect(r2.issues == 0 && r2.issue_comments == 0 && r2.wiki_pages == 0
	   && r2.discussion_topics == 0 && r2.discussion_posts == 0 && r2.release_files == 0,
	   "re-run writes nothing new");
	ok &= MigExpect(r2.already_present == 7, "re-run reports every migrated record as already present");

	// Corrupt and absent stores are reported; the readable store still imports.
	String broken = AppendFileName(legacy, "broken");
	WriteLegacy(broken, "tickets_store.json", "{not json");
	WriteLegacy(broken, "wiki_store.json", kWiki);
	PrivateHubLegacyReport r3;
	ok &= MigExpect(PrivateHubMigrateSourceForgeLocal(broken, data, r3), "import with a corrupt store runs");
	ok &= MigExpect(r3.corrupt.GetCount() == 1 && r3.corrupt[0].StartsWith("tickets_store.json"),
	   "corrupt tickets store reported");
	ok &= MigExpect(r3.absent.GetCount() == 2, "absent discussion and files stores reported");
	ok &= MigExpect(r3.wiki_pages == 1 && r3.issues == 0, "readable wiki store still imports");
	ok &= MigExpect(!r3.Clean(), "corrupt store makes the report not clean");

	// A clean project imports fully and the CLI reports success.
	String clean = AppendFileName(legacy, "clean-proj");
	WriteLegacy(clean, "wiki_store.json", "[{\"title\":\"Home\",\"slug\":\"home\",\"content\":\"hi\"}]");
	PrivateHubLegacyReport r4;
	ok &= MigExpect(PrivateHubMigrateSourceForgeLocal(clean, data, r4) && r4.Clean() && r4.wiki_pages == 1,
	   "clean project imports with a clean report");
	Vector<String> cli_ok;
	cli_ok.Add(clean);
	cli_ok.Add(data);
	ok &= MigExpect(RunMigrateSfLocalCommand(cli_ok) == 0, "CLI exit 0 for a clean import");
	Vector<String> cli_partial;
	cli_partial.Add(topside);
	cli_partial.Add(data);
	ok &= MigExpect(RunMigrateSfLocalCommand(cli_partial) == 1, "CLI exit 1 when the import is not clean");
	Vector<String> cli_usage;
	cli_usage.Add(topside);
	ok &= MigExpect(RunMigrateSfLocalCommand(cli_usage) == 2, "CLI usage error exits 2");

	// A data root inside the legacy directory is refused before anything is written.
	PrivateHubLegacyReport r5;
	ok &= MigExpect(!PrivateHubMigrateSourceForgeLocal(topside, AppendFileName(topside, "out"), r5),
	   "data root inside the legacy directory is refused");

	// Originals untouched: same bytes, same file list.
	int i = 0;
	for(const char *n : {"tickets_store.json", "wiki_store.json", "discussion_store.json", "files_store.json"}) {
		ok &= MigExpect(LoadFile(AppendFileName(topside, n)) == before[i], "legacy store bytes unchanged");
		i++;
	}
	int files_now = 0;
	for(FindFile ff(AppendFileName(topside, "*")); ff; ff.Next())
		if(ff.IsFile())
			files_now++;
	ok &= MigExpect(files_now == 4, "legacy directory keeps exactly its four stores");

	// Provenance and content read back through the PrivateHub backend.
	PrivateHubWorkspaceBackend ph(ws, data);
	Vector<ForgeIssue> issues;
	ok &= MigExpect(ph.ListIssues("topside", issues).ok && issues.GetCount() == 2
	   && issues[0].title.GetCount() && issues[0].issue_id.StartsWith("legacy-"),
	   "migrated tickets listed as issues");
	Vector<ForgeComment> comments;
	ok &= MigExpect(ph.ListComments("topside", "legacy-1", comments).ok && comments.GetCount() == 1
	   && comments[0].body == "repro attached" && comments[0].author == "bob",
	   "migrated ticket comment listed");
	PrivateHubWikiPage page;
	ok &= MigExpect(ph.GetWikiPage("topside", "home", page).ok && page.body == "= Welcome"
	   && page.provenance == PRIVATEHUB_PROVENANCE_MIGRATED && page.revision == "1",
	   "migrated wiki page has migrated-from-legacy provenance");
	PrivateHubDiscussionTopic topic;
	ok &= MigExpect(ph.GetDiscussionTopic("topside", "legacy-3", topic).ok && topic.forum == "general"
	   && topic.provenance == PRIVATEHUB_PROVENANCE_MIGRATED, "migrated discussion topic readable");
	Vector<PrivateHubPost> posts;
	ok &= MigExpect(ph.ListDiscussionPosts("topside", "legacy-3", posts).ok && posts.GetCount() == 1
	   && posts[0].body == "first reply", "migrated post listed under its topic");
	Vector<PrivateHubReleaseFile> files;
	ok &= MigExpect(ph.ListReleaseFiles("topside", files).ok && files.GetCount() == 1
	   && files[0].size_bytes == 14889728 && files[0].release == "1.0.0", "migrated release file readable");

	// Provenance ref names the source store and key in the envelope on disk.
	IRecordCollection& raw = TheStoreManager().JsonCollection("privatehub", data,
		PrivateHubWorkspaceBackend::CollectionFor("issues", "topside"));
	JsonFileStore *store = dynamic_cast<JsonFileStore*>(&raw);
	ok &= MigExpect(store != NULL, "issue collection is a JSON file store");
	if(store) {
		Value doc = ParseJSON(LoadFile(AppendFileName(store->CollectionDir(), "legacy-1.json")));
		ValueMap env = (ValueMap)((ValueMap)doc).Get("envelope");
		ok &= MigExpect(AsString(env.Get("provenance_ref")) == AppendFileName(topside, "tickets_store.json") + "#num=1",
		   "envelope provenance_ref names the legacy store and key");
		ok &= MigExpect(AsString(env.Get("record_kind")) == "privatehub/issue", "migrated issue carries its record kind");
	}
	return ok;
}

END_UPP_NAMESPACE
