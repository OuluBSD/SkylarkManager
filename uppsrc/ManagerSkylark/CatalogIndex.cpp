#include "CatalogIndex.h"

#include <plugin/sqlite3/Sqlite3.h>

#ifdef PLATFORM_POSIX
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

NAMESPACE_UPP

namespace {

static const int INDEX_SCHEMA_VERSION = 2;
static const int INDEX_APPLICATION_ID = 1297304409; // "MSKY"
static const char *INDEX_FILE_NAME = "catalog.sqlite3";
static const char *INDEX_DIRECTORY_NAME = ".manager-skylark-index";

struct IndexDb {
	Sqlite3Session session;
	bool Open(const String& path)
	{
		session.SetBusyTimeout(5000);
		return session.Open(path.ToStd().c_str());
	}
};

static bool Exec(Sqlite3Session& session, const String& statement)
{
	Sql sql(session);
	return sql.Execute(statement) && !session.WasError();
}

static bool ExecuteBound(Sqlite3Session& session, const String& statement, const Vector<Value>& values)
{
	Sql sql(session);
	sql.SetStatement(statement);
	for(int i = 0; i < values.GetCount(); i++) sql.SetParam(i, values[i]);
	return sql.Execute() && !session.WasError();
}

static bool ScalarInt(Sqlite3Session& session, const String& statement, int& value)
{
	Sql sql(session);
	if(!sql.Execute(statement) || !sql.Fetch() || session.WasError()) return false;
	value = (int)sql[0];
	return true;
}

static bool ScalarInt64(Sqlite3Session& session, const String& statement, int64& value)
{
	Sql sql(session);
	if(!sql.Execute(statement) || !sql.Fetch() || session.WasError()) return false;
	value = (int64)sql[0];
	return true;
}

static bool ScalarString(Sqlite3Session& session, const String& statement, String& value)
{
	Sql sql(session);
	if(!sql.Execute(statement) || !sql.Fetch() || session.WasError()) return false;
	value = sql[0];
	return true;
}

static String ProductionDatabasePath()
{
	return AppendFileName(GetHomeDirFile(".cache/" + String(INDEX_DIRECTORY_NAME)), INDEX_FILE_NAME);
}

static String DatabasePathFor(const CatalogGeneration& generation)
{
	if(ManagerSkylarkApp::IsolatedTestModeActive()) {
		String workspace = GetFileFolder(NormalizePath(generation.data.GetRootDir()));
		return AppendFileName(AppendFileName(workspace, "private-index"), INDEX_FILE_NAME);
	}
	return ProductionDatabasePath();
}

static bool HasNoSymlinkComponents(const String& input)
{
#ifdef PLATFORM_POSIX
	String path = NormalizePath(input);
	if(path.IsEmpty() || !IsFullPath(path.ToStd().c_str())) return false;
	String current = "/";
	Vector<String> parts = Split(path.Mid(1), '/');
	for(int i = 0; i < parts.GetCount(); i++) {
		if(parts[i].IsEmpty() || parts[i] == "." || parts[i] == "..") return false;
		current = AppendFileName(current, parts[i]);
		struct stat st;
		if(lstat(current.ToStd().c_str(), &st) == 0) {
			if(S_ISLNK(st.st_mode)) return false;
			if(i + 1 < parts.GetCount() && !S_ISDIR(st.st_mode)) return false;
		}
		else if(errno != ENOENT)
			return false;
	}
	return true;
#else
	(void)input;
	return false;
#endif
}

static bool IsOwnedDirectory(const String& path, bool private_mode)
{
#ifdef PLATFORM_POSIX
	struct stat st;
	if(lstat(path.ToStd().c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode) || st.st_uid != geteuid()) return false;
	return !private_mode || (st.st_mode & 0777) == 0700;
#else
	(void)path; (void)private_mode;
	return false;
#endif
}

static bool SafeParentMode(mode_t mode)
{
	return (mode & 0022) == 0;
}

static bool IsSafeParentDirectory(const String& path)
{
#ifdef PLATFORM_POSIX
	struct stat st;
	return lstat(path.ToStd().c_str(), &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) &&
	       st.st_uid == geteuid() && SafeParentMode(st.st_mode);
#else
	(void)path;
	return false;
#endif
}

static bool IsOwnedDatabase(const String& path)
{
#ifdef PLATFORM_POSIX
	struct stat st;
	return lstat(path.ToStd().c_str(), &st) == 0 && S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode) &&
	       st.st_uid == geteuid() && (st.st_mode & 0777) == 0600;
#else
	(void)path;
	return false;
#endif
}

static bool ValidateIndexPath(const String& supplied, const CatalogGeneration *generation,
	                          String& directory, bool allow_create_directory)
{
#ifndef PLATFORM_POSIX
	(void)supplied; (void)generation; (void)directory; (void)allow_create_directory;
	return false;
#else
	if(supplied.IsEmpty() || NormalizePath(supplied) != supplied || GetFileName(supplied) != INDEX_FILE_NAME ||
	   !HasNoSymlinkComponents(GetFileFolder(supplied))) return false;
	directory = GetFileFolder(supplied);
	String expected;
	if(ManagerSkylarkApp::IsolatedTestModeActive()) {
		if(!generation) {
			String temp = NormalizePath(GetTempDirectory());
			String parent = GetFileFolder(directory);
			return directory.StartsWith(temp + DIR_SEP) && GetFileName(directory) == "private-index" &&
			       IsOwnedDirectory(directory, true) && IsOwnedDirectory(parent, true);
		}
		String data_root = NormalizePath(generation->data.GetRootDir());
		String temp = NormalizePath(GetTempDirectory());
		if(data_root.IsEmpty() || !data_root.StartsWith(temp + DIR_SEP) || !DataStore::IsSafeRoot(data_root)) return false;
		String workspace = GetFileFolder(data_root);
		if(!IsOwnedDirectory(workspace, true)) return false;
		expected = AppendFileName(workspace, "private-index");
	}
	else {
		if(generation) return false;
		expected = GetFileFolder(ProductionDatabasePath());
	}
	if(directory != expected) return false;
	String parent = GetFileFolder(directory);
	if(!HasNoSymlinkComponents(parent) || !DirectoryExists(parent) || !IsSafeParentDirectory(parent)) return false;
	if(!DirectoryExists(directory)) {
		if(!allow_create_directory) return false;
		if(mkdir(directory.ToStd().c_str(), 0700) != 0 && errno != EEXIST) return false;
	}
	return IsOwnedDirectory(directory, true);
#endif
}

static bool PrepareDatabaseFile(const String& path, bool& created)
{
	created = false;
#ifdef PLATFORM_POSIX
	if(!HasNoSymlinkComponents(path)) return false;
	struct stat st;
	if(lstat(path.ToStd().c_str(), &st) == 0)
		return S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode) && st.st_uid == geteuid() && (st.st_mode & 0777) == 0600;
	if(errno != ENOENT) return false;
	int fd = open(path.ToStd().c_str(), O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW, 0600);
	if(fd < 0) return false;
	created = true;
	return close(fd) == 0;
#else
	(void)path;
	return false;
#endif
}

static bool CheckSidecar(const String& path)
{
#ifdef PLATFORM_POSIX
	struct stat st;
	if(lstat(path.ToStd().c_str(), &st) != 0) return errno == ENOENT;
	return S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode) && st.st_uid == geteuid() && (st.st_mode & 0777) == 0600;
#else
	(void)path;
	return false;
#endif
}

static bool CreateSafetyMarker(const String& path)
{
#ifdef PLATFORM_POSIX
	int fd = open(path.ToStd().c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
	if(fd < 0) return false;
	static const char marker[] = "synthetic-marker-preserve";
	bool ok = write(fd, marker, sizeof(marker) - 1) == (ssize_t)(sizeof(marker) - 1);
	return close(fd) == 0 && ok;
#else
	(void)path;
	return false;
#endif
}

static bool MarkerUnchanged(const String& path, const struct stat& before)
{
#ifdef PLATFORM_POSIX
	struct stat after;
	return lstat(path.ToStd().c_str(), &after) == 0 && S_ISREG(after.st_mode) &&
	       before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_uid == after.st_uid &&
	       before.st_mode == after.st_mode && before.st_size == after.st_size &&
	       LoadFile(path) == "synthetic-marker-preserve";
#else
	(void)path; (void)before;
	return false;
#endif
}

static bool RunSyntheticPathSafetyChecks(const CatalogGeneration& generation, const String& database_path)
{
#ifdef PLATFORM_POSIX
	static bool completed = false;
	if(completed) return true;
	String workspace = GetFileFolder(NormalizePath(generation.data.GetRootDir()));
	String index_dir = GetFileFolder(database_path);
	String marker = AppendFileName(index_dir, ".safety-marker");
	String sidecar_link = database_path + "-wal";
	String arbitrary_dir = AppendFileName(workspace, "arbitrary-index-target");
	String arbitrary_file = AppendFileName(arbitrary_dir, INDEX_FILE_NAME);
	if(mkdir(arbitrary_dir.ToStd().c_str(), 0700) != 0) return false;
	struct stat workspace_before;
	if(!IsOwnedDirectory(workspace, true) || !IsSafeParentDirectory(workspace) ||
	   !SafeParentMode((mode_t)0700) || SafeParentMode((mode_t)0777) ||
	   lstat(workspace.ToStd().c_str(), &workspace_before) != 0 ||
	   !CreateSafetyMarker(marker) || !CreateSafetyMarker(arbitrary_file)) return false;
	struct stat marker_before, arbitrary_before;
	if(lstat(marker.ToStd().c_str(), &marker_before) != 0 || lstat(arbitrary_file.ToStd().c_str(), &arbitrary_before) != 0) return false;
	bool linked = symlink(marker.ToStd().c_str(), sidecar_link.ToStd().c_str()) == 0;
	struct stat link_stat;
	bool sidecar_rejected = linked && !CheckSidecar(sidecar_link) &&
	                        lstat(sidecar_link.ToStd().c_str(), &link_stat) == 0 && S_ISLNK(link_stat.st_mode);
	String ignored;
	bool path_rejected = !ValidateIndexPath(arbitrary_file, &generation, ignored, true);
	struct stat workspace_after;
	bool unchanged = MarkerUnchanged(marker, marker_before) && MarkerUnchanged(arbitrary_file, arbitrary_before) &&
	                lstat(workspace.ToStd().c_str(), &workspace_after) == 0 &&
	                workspace_before.st_dev == workspace_after.st_dev && workspace_before.st_ino == workspace_after.st_ino &&
	                workspace_before.st_uid == workspace_after.st_uid && workspace_before.st_mode == workspace_after.st_mode;
	if(linked) unlink(sidecar_link.ToStd().c_str());
	unlink(marker.ToStd().c_str());
	unlink(arbitrary_file.ToStd().c_str());
	rmdir(arbitrary_dir.ToStd().c_str());
	completed = sidecar_rejected && path_rejected && unchanged;
	return completed;
#else
	(void)generation; (void)database_path;
	return false;
#endif
}

static bool ValidateIdentity(Sqlite3Session& session, bool allow_empty)
{
	int app_id = -1, user_version = -1, tables = -1;
	if(!ScalarInt(session, "PRAGMA application_id", app_id) ||
	   !ScalarInt(session, "PRAGMA user_version", user_version) ||
	   !ScalarInt(session, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'", tables)) return false;
	if(app_id == 0 && user_version == 0 && tables == 0)
		return allow_empty;
	if(app_id != INDEX_APPLICATION_ID || user_version != INDEX_SCHEMA_VERSION) return false;
	int required_tables = -1, all_tables = -1;
	if(!ScalarInt(session,
	   "SELECT count(*) FROM sqlite_master WHERE type='table' AND name IN ('metadata','generations','records','relationships','tombstones','index_state')",
	   required_tables) || !ScalarInt(session, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'", all_tables)) return false;
	if(required_tables != 6 || all_tables != 6) return false;
	String schema_version;
	if(!ScalarString(session, "SELECT value FROM metadata WHERE key='schema_version'", schema_version) || schema_version != "2") return false;
	static const char *shape[] = {
		"SELECT source_revision,schema_version,source_status,source_hash,published FROM generations LIMIT 0",
		"SELECT generation_id,source_id,workspace_id,qualified_id,collection,kind,schema_id,schema_version,revision,source_revision,record_scope,visibility,validation_state,title,status FROM records LIMIT 0",
		"SELECT generation_id,source_id,workspace_id,qualified_id,relation,target_id FROM relationships LIMIT 0",
		"SELECT generation_id,source_id,workspace_id,qualified_id,source_revision FROM tombstones LIMIT 0",
		"SELECT singleton,generation_id FROM index_state LIMIT 0"
	};
	for(const char *query : shape) if(!Exec(session, query)) return false;
	return true;
}

static bool InitializeSchema(Sqlite3Session& session)
{
	static const char *schema[] = {
		"PRAGMA application_id=1297304409",
		"PRAGMA user_version=2",
		"CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL)",
		"CREATE TABLE generations(id INTEGER PRIMARY KEY, source_revision TEXT NOT NULL, schema_version INTEGER NOT NULL, source_status TEXT NOT NULL, source_hash TEXT NOT NULL, published INTEGER NOT NULL DEFAULT 0)",
		"CREATE TABLE records(generation_id INTEGER NOT NULL REFERENCES generations(id) ON DELETE CASCADE, source_id TEXT NOT NULL, workspace_id TEXT NOT NULL, qualified_id TEXT NOT NULL, collection TEXT NOT NULL DEFAULT '', kind TEXT NOT NULL DEFAULT '', schema_id TEXT NOT NULL DEFAULT '', schema_version TEXT NOT NULL DEFAULT '', revision TEXT NOT NULL DEFAULT '', source_revision TEXT NOT NULL DEFAULT '', record_scope TEXT NOT NULL DEFAULT '', visibility TEXT NOT NULL DEFAULT '', validation_state TEXT NOT NULL DEFAULT '', title TEXT NOT NULL DEFAULT '', status TEXT NOT NULL DEFAULT '', PRIMARY KEY(generation_id,source_id,workspace_id,qualified_id))",
		"CREATE TABLE relationships(generation_id INTEGER NOT NULL, source_id TEXT NOT NULL, workspace_id TEXT NOT NULL, qualified_id TEXT NOT NULL, relation TEXT NOT NULL, target_id TEXT NOT NULL, FOREIGN KEY(generation_id,source_id,workspace_id,qualified_id) REFERENCES records(generation_id,source_id,workspace_id,qualified_id) ON DELETE CASCADE)",
		"CREATE TABLE tombstones(generation_id INTEGER NOT NULL REFERENCES generations(id) ON DELETE CASCADE, source_id TEXT NOT NULL, workspace_id TEXT NOT NULL, qualified_id TEXT NOT NULL, source_revision TEXT NOT NULL, PRIMARY KEY(generation_id,source_id,workspace_id,qualified_id))",
		"CREATE TABLE index_state(singleton INTEGER PRIMARY KEY CHECK(singleton=1), generation_id INTEGER NOT NULL REFERENCES generations(id))",
		"INSERT INTO metadata VALUES('schema_version','2')"
	};
	if(!Exec(session, "BEGIN IMMEDIATE")) return false;
	for(const char *statement : schema)
		if(!Exec(session, statement)) { session.Rollback(); return false; }
	if(!Exec(session, "COMMIT")) { session.Rollback(); return false; }
	return true;
}

static bool InsertRecord(Sqlite3Session& session, int64 generation, const DataDocument& d)
{
	Sql sql(session);
	sql.SetStatement("INSERT INTO records(generation_id,source_id,workspace_id,qualified_id) VALUES(?,COALESCE(?,''),COALESCE(?,''),COALESCE(?,''))");
	sql.SetParam(0, generation); sql.SetParam(1, d.source_id); sql.SetParam(2, d.workspace_id); sql.SetParam(3, d.qualified_id);
	if(!sql.Execute() || session.WasError()) return false;
	int64 rowid = 0;
	if(!ScalarInt64(session, "SELECT last_insert_rowid()", rowid)) return false;
	Vector<Value> fields;
	fields.Add(d.collection); fields.Add(rowid);
	if(!ExecuteBound(session, "UPDATE records SET collection=COALESCE(?, '') WHERE rowid=?", fields)) return false;
	fields.Clear();
	fields.Add(d.kind); fields.Add(d.schema_id); fields.Add(d.schema_version); fields.Add(d.revision); fields.Add(rowid);
	if(!ExecuteBound(session, "UPDATE records SET kind=COALESCE(?,''),schema_id=COALESCE(?,''),schema_version=COALESCE(?,''),revision=COALESCE(?, '') WHERE rowid=?", fields)) return false;
	fields.Clear();
	fields.Add(d.source_revision); fields.Add(d.scope); fields.Add(d.visibility); fields.Add(d.validation_state); fields.Add(rowid);
	if(!ExecuteBound(session, "UPDATE records SET source_revision=COALESCE(?,''),record_scope=COALESCE(?,''),visibility=COALESCE(?,''),validation_state=COALESCE(?, '') WHERE rowid=?", fields)) return false;
	fields.Clear();
	fields.Add(d.title); fields.Add(d.status); fields.Add(rowid);
	return ExecuteBound(session, "UPDATE records SET title=COALESCE(?,''),status=COALESCE(?, '') WHERE rowid=?", fields);
}

static bool InsertV2(Sqlite3Session& session, int64 generation, const String& source_id,
	                 const String& workspace, const String& qualified, const String& kind,
	                 const String& revision, const String& title, const String& status)
{
	Vector<Value> v;
	v.Add(generation); v.Add(source_id); v.Add(workspace); v.Add(qualified); v.Add(""); v.Add(kind);
	v.Add("manager/v2/" + kind); v.Add("1"); v.Add(revision); v.Add(revision);
	v.Add(workspace); v.Add("private"); v.Add("typed"); v.Add(title); v.Add(status);
	return ExecuteBound(session, "INSERT INTO records VALUES(?,COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''))", v);
}

static bool InsertRelation(Sqlite3Session& session, int64 generation, const DataDocument& d,
	                       const String& role, const String& target)
{
	Vector<Value> v;
	v.Add(generation); v.Add(d.source_id); v.Add(d.workspace_id); v.Add(d.qualified_id); v.Add(role); v.Add(target);
	return ExecuteBound(session, "INSERT INTO relationships VALUES(?,COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''),COALESCE(?,''))", v);
}

static bool InsertRelations(Sqlite3Session& session, int64 generation, const DataDocument& d)
{
	for(int i = 0; i < d.data.GetCount(); i++) {
		String key = AsString(d.data.GetKey(i));
		Value value = d.data.GetValue(i);
		if(IsString(value) && (key == "parent" || key == "paper" || key == "repo" || key == "goal" || key == "workspace")) {
			if(!InsertRelation(session, generation, d, key, (String)value)) return false;
		}
		else if(value.Is<ValueArray>() && (key == "parents" || key == "references" || key == "depends_on")) {
			ValueArray a = value;
			for(int n = 0; n < a.GetCount(); n++)
				if(IsString(a[n]) && !InsertRelation(session, generation, d, key, (String)a[n])) return false;
		}
	}
	return true;
}

static String SourceHash(const CatalogGeneration& g)
{
	String material = g.source_revision + "\n" + g.v2_status + "\n" + g.data_status;
	for(const String& fingerprint : g.data.GetSourceFingerprints()) material << fingerprint << "\n";
	return DataStore::ContentDigest(material);
}

static bool CurrentIndex(Sqlite3Session& session, const String& revision, int64& generation)
{
	Sql sql(session);
	if(!sql.Execute("SELECT g.id FROM index_state i JOIN generations g ON g.id=i.generation_id WHERE i.singleton=1 AND g.source_revision=? AND g.source_status='complete' AND g.schema_version=2", revision) ||
	   !sql.Fetch() || session.WasError()) return false;
	generation = (int64)sql[0];
	return true;
}

}

CatalogIndexResult CatalogIndex::Rebuild(const CatalogGeneration& generation, const String& path,
	                                     CatalogIndexTestFailpoint test_failpoint)
{
	CatalogIndexResult out;
	out.status = "unsafe_target";
#ifndef PLATFORM_POSIX
	out.status = "unsupported_platform";
	return out;
#endif
	if(!generation.IsComplete() || generation.status == "loaded_with_errors" ||
	   generation.data.GetSourceErrorCount() || generation.v2.GetSourceErrorCount() ||
	   generation.data.IsLoadIncomplete() || generation.v2.IsLoadIncomplete()) {
		out.status = "source_incomplete";
		return out;
	}
	String directory;
	if(!ValidateIndexPath(path, &generation, directory, true)) return out;
	if(ManagerSkylarkApp::IsolatedTestModeActive() && !RunSyntheticPathSafetyChecks(generation, path)) {
		out.status = "safety_self_test_failed";
		return out;
	}
	if(!CheckSidecar(path + "-wal") || !CheckSidecar(path + "-shm") || !CheckSidecar(path + "-journal")) {
		out.status = "permissions_unsafe";
		return out;
	}
	bool created = false;
	if(!PrepareDatabaseFile(path, created)) { out.status = "unsafe_existing_database"; return out; }
	IndexDb db;
	if(!db.Open(path)) {
		out.status = "index_unavailable";
		return out;
	}
	if(!ValidateIdentity(db.session, created)) { out.status = "unsafe_existing_database"; return out; }
	String integrity;
	if(!ScalarString(db.session, "PRAGMA quick_check", integrity) || integrity != "ok") { out.status = "corrupt"; return out; }
	int app_id = 0;
	if(!ScalarInt(db.session, "PRAGMA application_id", app_id)) { out.status = "corrupt"; return out; }
	if(!Exec(db.session, "PRAGMA foreign_keys=ON") || !Exec(db.session, "PRAGMA synchronous=FULL")) { out.status = "index_unavailable"; return out; }
	String journal;
	if(!ScalarString(db.session, "PRAGMA journal_mode=WAL", journal) || ToLower(journal) != "wal") { out.status = "index_unavailable"; return out; }
	if(!CheckSidecar(path + "-wal") || !CheckSidecar(path + "-shm") || !CheckSidecar(path + "-journal")) { out.status = "permissions_unsafe"; return out; }
	if(app_id == 0) {
		if(!created || !InitializeSchema(db.session)) { out.status = "schema_initialization_failed"; return out; }
	}
	if(!Exec(db.session, "BEGIN IMMEDIATE")) { out.status = "busy"; return out; }
	bool ok = Exec(db.session, "UPDATE generations SET published=0") &&
	          Exec(db.session, "INSERT INTO generations(source_revision,schema_version,source_status,source_hash,published) VALUES('',2,'building','',0)");
	int64 id = 0;
	if(ok) ok = ScalarInt64(db.session, "SELECT last_insert_rowid()", id);
	Vector<Value> generation_values;
	generation_values.Add(generation.source_revision); generation_values.Add(generation.status);
	generation_values.Add(SourceHash(generation)); generation_values.Add(id);
	if(ok) ok = ExecuteBound(db.session, "UPDATE generations SET source_revision=?,source_status=?,source_hash=? WHERE id=?", generation_values);
	for(const DataDocument& d : generation.data.GetDocuments()) {
		if(ok) ok = InsertRecord(db.session, id, d) && InsertRelations(db.session, id, d);
		if(ok) out.count++;
	}
	for(const V2Goal& x : generation.v2.GetGoals())
		if(ok) { ok = InsertV2(db.session, id, generation.v2.GetSourceId(), x.repo, x.qualified_id, "goal", x.source_revision, x.title, ""); if(ok) out.count++; }
	for(const V2Issue& x : generation.v2.GetIssues())
		if(ok) { ok = InsertV2(db.session, id, generation.v2.GetSourceId(), x.repo, x.qualified_id, "issue", x.source_revision, x.title, x.status); if(ok) out.count++; }
	for(const V2Release& x : generation.v2.GetReleases())
		if(ok) { ok = InsertV2(db.session, id, generation.v2.GetSourceId(), "", x.qualified_id, "release", x.source_revision, x.title, x.status); if(ok) out.count++; }
	if(ok) {
		Vector<Value> v; v.Add(id); v.Add(id);
		ok = ExecuteBound(db.session,
			"INSERT INTO tombstones(generation_id,source_id,workspace_id,qualified_id,source_revision) SELECT ?,old.source_id,old.workspace_id,old.qualified_id,old.source_revision FROM records old JOIN index_state prior ON prior.generation_id=old.generation_id WHERE NOT EXISTS (SELECT 1 FROM records now WHERE now.generation_id=? AND now.source_id=old.source_id AND now.workspace_id=old.workspace_id AND now.qualified_id=old.qualified_id)", v);
	}
	if(ok) ok = Exec(db.session, "UPDATE generations SET published=1,source_status='complete' WHERE id=(SELECT max(id) FROM generations)");
	if(ok) {
		Vector<Value> v; v.Add(id);
		ok = ExecuteBound(db.session, "INSERT OR REPLACE INTO index_state(singleton,generation_id) VALUES(1,?)", v);
	}
	if(ok && test_failpoint == CATALOG_INDEX_FAIL_BEFORE_PUBLISH) ok = false;
	// Keep retired generations for existing CatalogIndexPin readers; cleanup is intentionally unbounded.
	if(ok) ok = Exec(db.session, "COMMIT");
	else db.session.Rollback();
	if(!ok) { out.status = "rebuild_failed"; return out; }
	if(!IsOwnedDatabase(path) || !CheckSidecar(path + "-wal") || !CheckSidecar(path + "-shm") || !CheckSidecar(path + "-journal")) {
		out.status = "permissions_unsafe";
		return out;
	}
	out.status = "ready";
	out.generation = id;
	out.source_revision = generation.source_revision;
	return out;
}

CatalogIndexResult CatalogIndex::Rebuild(const CatalogGeneration& generation)
{
	return Rebuild(generation, DatabasePathFor(generation));
}

CatalogIndexResult CatalogIndex::Check(const String& path, const String& expected)
{
	CatalogIndexResult out;
	out.status = "missing";
	if(ManagerSkylarkApp::IsolatedTestModeActive()) {
		String temp = NormalizePath(GetTempDirectory());
		String normalized = NormalizePath(path);
		if(normalized.IsEmpty() || normalized != path || !normalized.StartsWith(temp + DIR_SEP) ||
		   !HasNoSymlinkComponents(GetFileFolder(normalized))) { out.status = "unsafe_target"; return out; }
		if(!FileExists(path)) return out;
	}
	String directory;
	if(!ValidateIndexPath(path, NULL, directory, false)) { out.status = "unsafe_target"; return out; }
	if(!FileExists(path)) return out;
	if(!IsOwnedDatabase(path) || !CheckSidecar(path + "-wal") || !CheckSidecar(path + "-shm") || !CheckSidecar(path + "-journal")) {
		out.status = "permissions_unsafe";
		return out;
	}
	IndexDb db;
	if(!db.Open(path)) { out.status = "corrupt"; return out; }
	if(!ValidateIdentity(db.session, false)) { out.status = "unavailable"; return out; }
	String integrity;
	if(!ScalarString(db.session, "PRAGMA quick_check", integrity) || integrity != "ok") { out.status = "corrupt"; return out; }
	int64 generation = 0;
	if(!CurrentIndex(db.session, expected, generation)) {
		String current;
		if(!ScalarString(db.session, "SELECT source_revision FROM generations g JOIN index_state i ON i.generation_id=g.id WHERE i.singleton=1 AND g.source_status='complete' AND g.schema_version=2", current)) {
			out.status = "unavailable";
			return out;
		}
		out.source_revision = current;
		out.status = "stale";
		out.diagnostic = "source_hash_mismatch";
		return out;
	}
	out.status = "ready";
	out.source_revision = expected;
	out.generation = generation;
	return out;
}

CatalogIndexPin CatalogIndex::Pin(const CatalogGeneration& generation, String& status)
{
	status = "unavailable";
	if(!generation.IsComplete() || generation.status != "loaded" || generation.source_revision.IsEmpty()) {
		status = "incomplete";
		return CatalogIndexPin();
	}
	String path = DatabasePathFor(generation);
	CatalogIndexResult check = Check(path, generation.source_revision);
	status = check.status;
	if(status != "ready") return CatalogIndexPin();
	return CatalogIndexPin(check.generation, generation.source_revision, path);
}

Vector<CatalogIndexRow> CatalogIndex::Page(const CatalogIndexPin& pin, const CatalogIndexFilter& filter,
	                                       int offset, int limit, String& status)
{
	Vector<CatalogIndexRow> rows;
	status = "unavailable";
	if(!pin.IsValid() || offset < 0 || limit < 1 || limit > 500) return rows;
	String directory;
	if(!ValidateIndexPath(pin.private_database_path, NULL, directory, false)) { status = "unsafe_target"; return rows; }
	if(!IsOwnedDatabase(pin.private_database_path) || !CheckSidecar(pin.private_database_path + "-wal") ||
	   !CheckSidecar(pin.private_database_path + "-shm") || !CheckSidecar(pin.private_database_path + "-journal")) {
		status = "permissions_unsafe";
		return rows;
	}
	IndexDb db;
	if(!db.Open(pin.private_database_path)) { status = "corrupt"; return rows; }
	if(!Exec(db.session, "BEGIN")) { status = "busy"; return rows; }
	if(!ValidateIdentity(db.session, false)) { db.session.Rollback(); status = "unavailable"; return rows; }
	String integrity;
	if(!ScalarString(db.session, "PRAGMA quick_check", integrity) || integrity != "ok") { db.session.Rollback(); status = "corrupt"; return rows; }
	{
		Sql verify(db.session);
		if(!verify.Execute("SELECT 1 FROM generations WHERE id=? AND source_revision=? AND source_status='complete' AND schema_version=2",
		                   pin.generation_id, pin.source_revision) || !verify.Fetch() || db.session.WasError()) {
			db.session.Rollback(); status = "stale"; return rows;
		}
	}
	Sql sql(db.session);
	if(!sql.Execute("SELECT r.source_id,r.workspace_id,r.qualified_id,r.collection,r.kind,r.schema_id,r.schema_version,r.revision,r.source_revision,r.record_scope,r.visibility,r.validation_state,r.title,r.status FROM records r WHERE r.generation_id=? AND (?=0 OR r.schema_id NOT LIKE 'manager/v2/%') AND (COALESCE(?, '')='' OR r.source_id=COALESCE(?,'')) AND (COALESCE(?, '')='' OR r.workspace_id=COALESCE(?,'')) AND (COALESCE(?, '')='' OR r.kind=COALESCE(?,'')) AND (COALESCE(?, '')='' OR r.collection=COALESCE(?,'')) AND (COALESCE(?, '')='' OR r.record_scope=COALESCE(?,'')) ORDER BY r.source_id,r.workspace_id,r.qualified_id LIMIT ? OFFSET ?",
	                pin.generation_id,
	                filter.data_documents_only,
	                filter.source_id, filter.source_id, filter.workspace_id, filter.workspace_id,
	                filter.kind, filter.kind, filter.collection, filter.collection, filter.scope, filter.scope,
	                limit, offset)) {
		db.session.Rollback(); status = "corrupt"; return rows;
	}
	while(sql.Fetch()) {
		CatalogIndexRow& r = rows.Add();
		r.source_id=sql[0]; r.workspace_id=sql[1]; r.qualified_id=sql[2]; r.collection=sql[3]; r.kind=sql[4];
		r.schema_id=sql[5]; r.schema_version=sql[6]; r.revision=sql[7]; r.source_revision=sql[8];
		r.scope=sql[9]; r.visibility=sql[10]; r.validation_state=sql[11]; r.title=sql[12]; r.status=sql[13];
	}
	if(db.session.WasError()) { db.session.Rollback(); rows.Clear(); status = "corrupt"; return rows; }
	if(!Exec(db.session, "COMMIT")) { rows.Clear(); status = "corrupt"; return rows; }
	status = "ready";
	return rows;
}

Vector<CatalogIndexRow> CatalogIndex::Page(const String& path, const String& revision, int offset, int limit, String& status)
{
	Vector<CatalogIndexRow> rows;
	status = "unavailable";
	if(offset < 0 || limit < 1 || limit > 500) return rows;
	CatalogIndexResult check = Check(path, revision);
	if(check.status != "ready") { status = check.status; return rows; }
	CatalogIndexPin pin(check.generation, revision, path);
	return Page(pin, CatalogIndexFilter(), offset, limit, status);
}

END_UPP_NAMESPACE
