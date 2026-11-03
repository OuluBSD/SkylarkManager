#include "V2Model.h"

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
	V2GoalNode copy = *this;
	return StoreAsJson(copy, pretty);
}

bool V2GoalNode::FromJSON(const String& json)
{
	return LoadFromJson(*this, json);
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
	V2Goal copy = *this;
	return StoreAsJson(copy, pretty);
}

bool V2Goal::FromJSON(const String& json)
{
	return LoadFromJson(*this, json);
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
	V2Issue copy = *this;
	return StoreAsJson(copy, pretty);
}

bool V2Issue::FromJSON(const String& json)
{
	return LoadFromJson(*this, json);
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
	V2Release copy = *this;
	return StoreAsJson(copy, pretty);
}

bool V2Release::FromJSON(const String& json)
{
	return LoadFromJson(*this, json);
}

// ---------------------------------------------------------------------------

V2Store::V2Store()
{
}

void V2Store::Clear()
{
	goals.Clear();
	issues.Clear();
	releases.Clear();
}

bool V2Store::Load(const String& root_dir)
{
	Clear();
	base_dir = root_dir;

	if(base_dir.IsEmpty()) {
		if(DirectoryExists("../Manager/v2"))
			base_dir = "../Manager/v2";
		else if(DirectoryExists("../../Manager/v2"))
			base_dir = "../../Manager/v2";
		else if(DirectoryExists("v2"))
			base_dir = "v2";
	}

	if(!base_dir.IsEmpty() && DirectoryExists(base_dir)) {
		LoadGoals(AppendFileName(base_dir, "goals"));
		LoadIssues(AppendFileName(base_dir, "issues"));
		LoadReleases(AppendFileName(base_dir, "releases"));
	}

	return true;
}

void V2Store::LoadGoals(const String& dir)
{
	if(!DirectoryExists(dir)) return;

	FindFile ff(AppendFileName(dir, "*"));
	while(ff) {
		if(ff.IsFolder() && ff.GetName() != "." && ff.GetName() != "..") {
			String repo = ff.GetName();
			FindFile gff(AppendFileName(ff.GetPath(), "*.json"));
			while(gff) {
				String fname = gff.GetName();
				if(fname != "schema.json" && !fname.EndsWith(".state.json")) {
					String content = LoadFile(gff.GetPath());
					V2Goal goal;
					if(goal.FromJSON(content)) {
						if(goal.repo.IsEmpty())
							goal.repo = repo;
						goals.Add(goal);
					}
				}
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
		if(ff.IsFolder() && ff.GetName() != "." && ff.GetName() != "..") {
			String repo = ff.GetName();
			FindFile iff(AppendFileName(ff.GetPath(), "*.json"));
			while(iff) {
				String fname = iff.GetName();
				if(fname != "schema.json") {
					String content = LoadFile(iff.GetPath());
					V2Issue issue;
					if(issue.FromJSON(content)) {
						if(issue.repo.IsEmpty())
							issue.repo = repo;
						issues.Add(issue);
					}
				}
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
		if(ff.GetName() != "schema.json") {
			String content = LoadFile(ff.GetPath());
			V2Release rel;
			if(rel.FromJSON(content))
				releases.Add(rel);
		}
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
	for(int i = 0; i < goals.GetCount(); i++)
		if(goals[i].id == id || goals[i].id.EndsWith("/" + id))
			return &goals[i];
	return NULL;
}

const V2Issue* V2Store::FindIssue(const String& id) const
{
	for(int i = 0; i < issues.GetCount(); i++)
		if(issues[i].id == id || issues[i].id.EndsWith("/" + id))
			return &issues[i];
	return NULL;
}

const V2Release* V2Store::FindRelease(const String& id) const
{
	for(int i = 0; i < releases.GetCount(); i++)
		if(releases[i].id == id || releases[i].id.EndsWith("/" + id))
			return &releases[i];
	return NULL;
}

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
