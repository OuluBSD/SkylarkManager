#include "V2Model.h"
#include "DataStore.h"

NAMESPACE_UPP

V2GoalNode::V2GoalNode()
	: order(0)
{
}

V2GoalNode::V2GoalNode(const V2GoalNode& o)
	: kind(o.kind)
	, id(o.id)
	, title(o.title)
	, order(o.order)
	, status(o.status)
	, gates(clone(o.gates))
	, sets(clone(o.sets))
	, children(clone(o.children))
	, notes(o.notes)
	, source_data(o.source_data)
{
}

V2GoalNode& V2GoalNode::operator=(const V2GoalNode& o)
{
	if(this != &o) {
		kind = o.kind;
		id = o.id;
		title = o.title;
		order = o.order;
		status = o.status;
		gates = clone(o.gates);
		sets = clone(o.sets);
		children = clone(o.children);
		notes = o.notes;
		source_data = o.source_data;
	}
	return *this;
}

void V2GoalNode::Jsonize(JsonIO& io)
{
	io("kind", kind);
	io("id", id);
	io("title", title);
	io("order", order);
	io("status", status);
	io("gates", gates);
	io("sets", sets);
	io("children", children);
	io("notes", notes);
}

String V2GoalNode::ToJSON(bool pretty) const
{
	if(!source_data.IsEmpty()) return AsJSON(source_data, pretty);
	V2GoalNode copy = *this;
	return StoreAsJson(copy, pretty);
}

bool V2GoalNode::FromJSON(const String& json)
{
	if(!LoadFromJson(*this, json)) return false;
	Value v = ParseJSON(json);
	if(!v.Is<ValueMap>()) return false;
	source_data = v;
	return true;
}

// ---------------------------------------------------------------------------

V2Goal::V2Goal()
{
}

V2Goal::V2Goal(const V2Goal& o)
	: id(o.id)
	, kind(o.kind)
	, repo(o.repo)
	, title(o.title)
	, tags(clone(o.tags))
	, state(clone(o.state))
	, tasks(clone(o.tasks))
	, qualified_id(o.qualified_id)
	, source_revision(o.source_revision)
	, source_data(o.source_data)
{
}

V2Goal& V2Goal::operator=(const V2Goal& o)
{
	if(this != &o) {
		id = o.id;
		kind = o.kind;
		repo = o.repo;
		title = o.title;
		tags = clone(o.tags);
		state = clone(o.state);
		tasks = clone(o.tasks);
		qualified_id = o.qualified_id;
		source_revision = o.source_revision;
		source_data = o.source_data;
	}
	return *this;
}

void V2Goal::Jsonize(JsonIO& io)
{
	io("id", id);
	io("kind", kind);
	io("repo", repo);
	io("title", title);
	io("tags", tags);
	io("state", state);
	io("tasks", tasks);
}

String V2Goal::ToJSON(bool pretty) const
{
	if(!source_data.IsEmpty()) return AsJSON(source_data, pretty);
	V2Goal copy = *this;
	return StoreAsJson(copy, pretty);
}

bool V2Goal::FromJSON(const String& json)
{
	if(!LoadFromJson(*this, json)) return false;
	Value v = ParseJSON(json);
	if(!v.Is<ValueMap>()) return false;
	source_data = v;
	Value nodes = MapGet(source_data, "tasks");
	if(nodes.Is<ValueArray>()) {
		ValueArray a = nodes;
		for(int i = 0; i < min(a.GetCount(), tasks.GetCount()); i++)
			if(a[i].Is<ValueMap>()) tasks[i].source_data = a[i];
	}
	return true;
}

int V2Goal::GetTotalTasks() const
{
	int count = 0;
	for(int i = 0; i < tasks.GetCount(); i++)
		if(tasks[i].kind == "task") count++;
	return count;
}

int V2Goal::GetDoneTasks() const
{
	int count = 0;
	for(int i = 0; i < tasks.GetCount(); i++)
		if(tasks[i].kind == "task" && tasks[i].status == "done") count++;
	return count;
}

double V2Goal::GetProgressPercent() const
{
	int tot = GetTotalTasks();
	if(tot == 0) return 0.0;
	return (100.0 * GetDoneTasks()) / tot;
}

// ---------------------------------------------------------------------------

V2Issue::V2Issue()
{
}

V2Issue::V2Issue(const V2Issue& o)
	: id(o.id)
	, kind(o.kind)
	, repo(o.repo)
	, title(o.title)
	, status(o.status)
	, notes(o.notes)
	, qualified_id(o.qualified_id)
	, source_revision(o.source_revision)
	, source_data(o.source_data)
{
}

V2Issue& V2Issue::operator=(const V2Issue& o)
{
	if(this != &o) {
		id = o.id;
		kind = o.kind;
		repo = o.repo;
		title = o.title;
		status = o.status;
		notes = o.notes;
		qualified_id = o.qualified_id;
		source_revision = o.source_revision;
		source_data = o.source_data;
	}
	return *this;
}

void V2Issue::Jsonize(JsonIO& io)
{
	io("id", id);
	io("kind", kind);
	io("repo", repo);
	io("title", title);
	io("status", status);
	io("notes", notes);
}

String V2Issue::ToJSON(bool pretty) const
{
	if(!source_data.IsEmpty()) return AsJSON(source_data, pretty);
	V2Issue copy = *this;
	return StoreAsJson(copy, pretty);
}

bool V2Issue::FromJSON(const String& json)
{
	if(!LoadFromJson(*this, json)) return false;
	Value v = ParseJSON(json);
	if(!v.Is<ValueMap>()) return false;
	source_data = v;
	return true;
}

// ---------------------------------------------------------------------------

V2Release::V2Release()
{
}

V2Release::V2Release(const V2Release& o)
	: id(o.id)
	, kind(o.kind)
	, title(o.title)
	, status(o.status)
	, goals(clone(o.goals))
	, issues(clone(o.issues))
	, qualified_id(o.qualified_id)
	, source_revision(o.source_revision)
	, source_data(o.source_data)
{
}

V2Release& V2Release::operator=(const V2Release& o)
{
	if(this != &o) {
		id = o.id;
		kind = o.kind;
		title = o.title;
		status = o.status;
		goals = clone(o.goals);
		issues = clone(o.issues);
		qualified_id = o.qualified_id;
		source_revision = o.source_revision;
		source_data = o.source_data;
	}
	return *this;
}

void V2Release::Jsonize(JsonIO& io)
{
	io("id", id);
	io("kind", kind);
	io("title", title);
	io("status", status);
	io("goals", goals);
	io("issues", issues);
}

String V2Release::ToJSON(bool pretty) const
{
	if(!source_data.IsEmpty()) return AsJSON(source_data, pretty);
	V2Release copy = *this;
	return StoreAsJson(copy, pretty);
}

bool V2Release::FromJSON(const String& json)
{
	if(!LoadFromJson(*this, json)) return false;
	Value v = ParseJSON(json);
	if(!v.Is<ValueMap>()) return false;
	source_data = v;
	return true;
}

// ---------------------------------------------------------------------------

V2Store::V2Store()
	: entries_scanned(0)
	, source_bytes_scanned(0)
	, source_errors(0)
{
}

V2Store::V2Store(const V2Store& o) { *this = o; }

V2Store& V2Store::operator=(const V2Store& o)
{
	if(this == &o) return *this;
	base_dir = o.base_dir; goals = clone(o.goals); issues = clone(o.issues); releases = clone(o.releases);
	entries_scanned = o.entries_scanned; source_bytes_scanned = o.source_bytes_scanned;
	source_id = o.source_id; source_errors = o.source_errors; source_incomplete = o.source_incomplete; source_diagnostics = clone(o.source_diagnostics);
	return *this;
}

void V2Store::Clear()
{
	base_dir.Clear();
	goals.Clear();
	issues.Clear();
	releases.Clear();
	entries_scanned = 0;
	source_bytes_scanned = 0;
	source_id.Clear();
	source_errors = 0;
	source_incomplete = false;
	source_diagnostics.Clear();
}

bool V2Store::LoadFixtureForTests(const String& fixture_root)
{
	String temp_root = NormalizePath(GetTempDirectory());
	String root = NormalizePath(fixture_root);
	if(!root.StartsWith(temp_root + DIR_SEP) || !DataStore::IsSafeRoot(root))
		return false;
	return LoadRoot(root, "self-test-fixture");
}

bool V2Store::LoadRoot(const String& root_dir, const String& configured_source_id)
{
	Clear();
	if(!DataStore::IsSafeRoot(root_dir))
		return false;
	base_dir = NormalizePath(root_dir);
	source_id = configured_source_id;
	LoadGoals(AppendFileName(base_dir, "goals"));
	LoadIssues(AppendFileName(base_dir, "issues"));
	LoadReleases(AppendFileName(base_dir, "releases"));
	return true;
}

static String V2Revision(const String& json)
{
	return DataStore::ContentDigest(json);
}

void V2Store::LoadGoals(const String& dir)
{
	if(!DirectoryExists(dir)) return;

	FindFile ff(AppendFileName(dir, "*"));
	while(ff) {
		if(entries_scanned >= DataStore::MAX_FILES) {
			source_incomplete = true; source_errors++; source_diagnostics.Add("goals:entry_limit");
			break;
		}
		entries_scanned++;
		if(ff.GetName() != "." && ff.GetName() != ".." && !ff.IsSymLink() && ff.IsFolder()) {
			String repo = ff.GetName();
			FindFile gff(AppendFileName(ff.GetPath(), "*.json"));
			while(gff) {
				if(entries_scanned >= DataStore::MAX_FILES) {
					source_incomplete = true; source_errors++; source_diagnostics.Add("goals/" + repo + ":entry_limit");
					break;
				}
				entries_scanned++;
				String fname = gff.GetName();
				int64 len = GetFileLength(gff.GetPath());
				if(gff.IsFile() && !gff.IsSymLink() && !DataStore::ExceedsSourceBudget(len, source_bytes_scanned) && fname != "schema.json" && !fname.EndsWith(".state.json")) {
					source_bytes_scanned += len;
					String content = LoadFile(gff.GetPath());
					V2Goal goal;
					try {
						if(goal.FromJSON(content)) {
							if(goal.repo.IsEmpty())
								goal.repo = repo;
							goal.qualified_id = repo + ":goal:" + goal.id;
							goal.source_revision = V2Revision(content);
							goals.Add(goal);
						} else { source_errors++; source_diagnostics.Add("goals/" + repo + "/" + fname + ":parse_error:" + V2Revision(content)); }
					}
					catch(Exc) {
						source_errors++; source_diagnostics.Add("goals/" + repo + "/" + fname + ":parse_error:" + V2Revision(content));
					}
				}
				else if(gff.IsFile() && fname != "schema.json" && !fname.EndsWith(".state.json")) { source_incomplete = true; source_errors++; source_diagnostics.Add("goals/" + repo + "/" + fname + ":source_limit"); }
				gff.Next();
			}
		}
		ff.Next();
	}
}

void V2Store::LoadIssues(const String& dir)
{
	if(!DirectoryExists(dir)) return;

	FindFile ff(AppendFileName(dir, "*"));
	while(ff) {
		if(entries_scanned >= DataStore::MAX_FILES) {
			source_incomplete = true; source_errors++; source_diagnostics.Add("issues:entry_limit");
			break;
		}
		entries_scanned++;
		if(ff.GetName() != "." && ff.GetName() != ".." && !ff.IsSymLink() && ff.IsFolder()) {
			String repo = ff.GetName();
			FindFile iff(AppendFileName(ff.GetPath(), "*.json"));
			while(iff) {
				if(entries_scanned >= DataStore::MAX_FILES) {
					source_incomplete = true; source_errors++; source_diagnostics.Add("issues/" + repo + ":entry_limit");
					break;
				}
				entries_scanned++;
				String fname = iff.GetName();
				int64 len = GetFileLength(iff.GetPath());
				if(iff.IsFile() && !iff.IsSymLink() && !DataStore::ExceedsSourceBudget(len, source_bytes_scanned) && fname != "schema.json") {
					source_bytes_scanned += len;
					String content = LoadFile(iff.GetPath());
					V2Issue issue;
					try {
						if(issue.FromJSON(content)) {
							if(issue.repo.IsEmpty())
								issue.repo = repo;
							issue.qualified_id = repo + ":issue:" + issue.id;
							issue.source_revision = V2Revision(content);
							issues.Add(issue);
						} else { source_errors++; source_diagnostics.Add("issues/" + repo + "/" + fname + ":parse_error:" + V2Revision(content)); }
					}
					catch(Exc) {
						source_errors++; source_diagnostics.Add("issues/" + repo + "/" + fname + ":parse_error:" + V2Revision(content));
					}
				}
				else if(iff.IsFile() && fname != "schema.json") { source_incomplete = true; source_errors++; source_diagnostics.Add("issues/" + repo + "/" + fname + ":source_limit"); }
				iff.Next();
			}
		}
		ff.Next();
	}
}

void V2Store::LoadReleases(const String& dir)
{
	if(!DirectoryExists(dir)) return;

	FindFile ff(AppendFileName(dir, "*.json"));
	while(ff) {
		if(entries_scanned >= DataStore::MAX_FILES) {
			source_incomplete = true; source_errors++; source_diagnostics.Add("releases:entry_limit");
			break;
		}
		entries_scanned++;
		int64 len = GetFileLength(ff.GetPath());
		if(ff.IsFile() && !ff.IsSymLink() && !DataStore::ExceedsSourceBudget(len, source_bytes_scanned) && ff.GetName() != "schema.json") {
			source_bytes_scanned += len;
			String content = LoadFile(ff.GetPath());
			V2Release rel;
			try {
				if(rel.FromJSON(content)) {
					rel.qualified_id = "release:" + rel.id;
					rel.source_revision = V2Revision(content);
					releases.Add(rel);
				} else { source_errors++; source_diagnostics.Add("releases/" + ff.GetName() + ":parse_error:" + V2Revision(content)); }
			}
			catch(Exc) {
				source_errors++; source_diagnostics.Add("releases/" + ff.GetName() + ":parse_error:" + V2Revision(content));
			}
		}
		else if(ff.IsFile() && ff.GetName() != "schema.json") { source_incomplete = true; source_errors++; source_diagnostics.Add("releases/" + ff.GetName() + ":source_limit"); }
		ff.Next();
	}
}

Vector<String> V2Store::GetRepos() const
{
	Index<String> set;
	for(int i = 0; i < goals.GetCount(); i++)
		if(!goals[i].repo.IsEmpty())
			set.FindAdd(goals[i].repo);
	for(int i = 0; i < issues.GetCount(); i++)
		if(!issues[i].repo.IsEmpty())
			set.FindAdd(issues[i].repo);

	Vector<String> res;
	for(int i = 0; i < set.GetCount(); i++)
		res.Add(set[i]);
	Sort(res);
	return res;
}

const V2Goal* V2Store::FindGoal(const String& id) const
{
	const V2Goal* found = NULL;
	for(const V2Goal& g : goals)
		if(g.qualified_id == id || g.id == id || g.id.EndsWith("/" + id)) {
			if(found) return NULL;
			found = &g;
		}
	return found;
}

const V2Issue* V2Store::FindIssue(const String& id) const
{
	const V2Issue* found = NULL;
	for(const V2Issue& v : issues)
		if(v.qualified_id == id || v.id == id || v.id.EndsWith("/" + id)) {
			if(found) return NULL;
			found = &v;
		}
	return found;
}

const V2Release* V2Store::FindRelease(const String& id) const
{
	const V2Release* found = NULL;
	for(const V2Release& v : releases)
		if(v.qualified_id == id || v.id == id || v.id.EndsWith("/" + id)) {
			if(found) return NULL;
			found = &v;
		}
	return found;
}

const V2Goal* V2Store::FindGoalExact(const String& id) const { const V2Goal* found = NULL; for(const V2Goal& g : goals) if(g.qualified_id == id) { if(found) return NULL; found = &g; } return found; }
const V2Issue* V2Store::FindIssueExact(const String& id) const { const V2Issue* found = NULL; for(const V2Issue& v : issues) if(v.qualified_id == id) { if(found) return NULL; found = &v; } return found; }
const V2Release* V2Store::FindReleaseExact(const String& id) const { const V2Release* found = NULL; for(const V2Release& v : releases) if(v.qualified_id == id) { if(found) return NULL; found = &v; } return found; }

Vector<const V2Goal*> V2Store::GetGoalsForRepo(const String& repo) const
{
	Vector<const V2Goal*> res;
	for(int i = 0; i < goals.GetCount(); i++)
		if(goals[i].repo == repo)
			res.Add(&goals[i]);
	return res;
}

Vector<const V2Issue*> V2Store::GetIssuesForRepo(const String& repo) const
{
	Vector<const V2Issue*> res;
	for(int i = 0; i < issues.GetCount(); i++)
		if(issues[i].repo == repo)
			res.Add(&issues[i]);
	return res;
}

END_UPP_NAMESPACE
