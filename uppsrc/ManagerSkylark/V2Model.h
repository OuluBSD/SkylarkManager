#ifndef _reference_ManagerSkylark_V2Model_h_
#define _reference_ManagerSkylark_V2Model_h_

#include <Core/Core.h>

NAMESPACE_UPP

class ManagerSkylarkApp;

struct V2GoalNode : Moveable<V2GoalNode> {
	String                    kind; // "phase" or "task"
	String                    id;
	String                    title;
	int                       order;
	String                    status; // "planned", "active", "done", "blocked"
	VectorMap<String, String> gates;
	VectorMap<String, String> sets;
	Vector<String>            children;
	String                    notes;
	ValueMap                  source_data;

	V2GoalNode();
	V2GoalNode(const V2GoalNode& o);
	V2GoalNode& operator=(const V2GoalNode& o);

	void Jsonize(JsonIO& io);
	String ToJSON(bool pretty = true) const;
	bool FromJSON(const String& json);
};

struct V2Goal : Moveable<V2Goal> {
	String                    id;
	String                    kind; // "goal"
	String                    repo;
	String                    title;
	Vector<String>            tags;
	VectorMap<String, String> state;
	Vector<V2GoalNode>        tasks;
	String                    qualified_id;
	String                    source_revision;
	ValueMap                  source_data;

	V2Goal();
	V2Goal(const V2Goal& o);
	V2Goal& operator=(const V2Goal& o);

	void Jsonize(JsonIO& io);
	String ToJSON(bool pretty = true) const;
	bool FromJSON(const String& json);
	
	int GetTotalTasks() const;
	int GetDoneTasks() const;
	double GetProgressPercent() const;
};

struct V2Issue : Moveable<V2Issue> {
	String id;
	String kind; // "issue"
	String repo;
	String title;
	String status; // "proposed", "verified", "accepted", "resolved", "closed"
	String notes;
	String qualified_id;
	String source_revision;
	ValueMap source_data;

	V2Issue();
	V2Issue(const V2Issue& o);
	V2Issue& operator=(const V2Issue& o);

	void Jsonize(JsonIO& io);
	String ToJSON(bool pretty = true) const;
	bool FromJSON(const String& json);
};

struct V2Release : Moveable<V2Release> {
	String         id;
	String         kind; // "release"
	String         title;
	String         status; // "proposed", "planned", "active", "released", "retired"
	Vector<String> goals;
	Vector<String> issues;
	String qualified_id;
	String source_revision;
	ValueMap source_data;

	V2Release();
	V2Release(const V2Release& o);
	V2Release& operator=(const V2Release& o);

	void Jsonize(JsonIO& io);
	String ToJSON(bool pretty = true) const;
	bool FromJSON(const String& json);
};

class V2Store {
public:
	V2Store();
	V2Store(const V2Store& o);
	V2Store& operator=(const V2Store& o);

	void Clear();

	const Vector<V2Goal>&    GetGoals() const    { return goals; }
	const Vector<V2Issue>&   GetIssues() const   { return issues; }
	const Vector<V2Release>& GetReleases() const { return releases; }
	Vector<String>           GetRepos() const;

	const V2Goal*    FindGoal(const String& id) const;
	const V2Issue*   FindIssue(const String& id) const;
	const V2Release* FindRelease(const String& id) const;

	Vector<const V2Goal*>  GetGoalsForRepo(const String& repo) const;
	Vector<const V2Issue*> GetIssuesForRepo(const String& repo) const;

	String GetRootDir() const { return base_dir; }
	const Vector<String>& GetSourceDiagnostics() const { return source_diagnostics; }
	int GetSourceErrorCount() const { return source_errors; }
	bool IsLoadIncomplete() const { return source_incomplete; }
	String GetLoadStatus() const { return source_incomplete ? "load_incomplete" : source_errors ? "loaded_with_errors" : "loaded"; }
	String GetSourceId() const { return source_id; }
	const V2Goal* FindGoalExact(const String& id) const;
	const V2Issue* FindIssueExact(const String& id) const;
	const V2Release* FindReleaseExact(const String& id) const;

private:
	friend class ManagerSkylarkApp;
	bool LoadFixtureForTests(const String& fixture_root);
	bool LoadRoot(const String& root_dir, const String& source_id);
	String            base_dir;
	Vector<V2Goal>    goals;
	Vector<V2Issue>   issues;
	Vector<V2Release> releases;
	int               entries_scanned;
	int64             source_bytes_scanned;
	String            source_id;
	int               source_errors;
	bool              source_incomplete = false;
	Vector<String>    source_diagnostics;

	void LoadGoals(const String& dir);
	void LoadIssues(const String& dir);
	void LoadReleases(const String& dir);
};

END_UPP_NAMESPACE

#endif
