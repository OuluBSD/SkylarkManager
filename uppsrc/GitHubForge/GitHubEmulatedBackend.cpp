#include "GitHubEmulatedBackend.h"

#ifdef PLATFORM_POSIX
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/file.h>
#endif

NAMESPACE_UPP

// On-disk records. Issues and pull requests share one number sequence and one
// comment thread per number, as on GitHub. Timestamps are ISO-8601 strings, the
// same form the gh connector receives.

struct EmuStoreComment : Moveable<EmuStoreComment> {
	int    id = 0;
	String author;
	String body;
	String created;

	void Jsonize(JsonIO& io) { io("id", id)("author", author)("body", body)("created", created); }
};

struct EmuStoreThread : Moveable<EmuStoreThread> {
	int                         number = 0;
	Vector<EmuStoreComment>     comments;

	void Jsonize(JsonIO& io) { io("number", number)("comments", comments); }
};

struct EmuStoreIssue : Moveable<EmuStoreIssue> {
	int    number = 0;
	String title;
	String body;
	String author;
	String state;      // "open" | "closed"
	String created;
	String updated;

	void Jsonize(JsonIO& io) {
		io("number", number)("title", title)("body", body)("author", author)
		  ("state", state)("created", created)("updated", updated);
	}
};

struct EmuStorePull : Moveable<EmuStorePull> {
	int    number = 0;
	String title;
	String body;
	String author;
	String state;      // "open" | "closed"; merged when merged_at is set
	String created;
	String merged_at;
	String head;
	String base;
	String diff;
	String url;

	void Jsonize(JsonIO& io) {
		io("number", number)("title", title)("body", body)("author", author)("state", state)
		  ("created", created)("merged_at", merged_at)("head", head)("base", base)("diff", diff)("url", url);
	}
};

struct EmuStoreRun : Moveable<EmuStoreRun> {
	int64  id = 0;
	String name;
	String status;
	String conclusion;
	String head_branch;
	String event;
	String created_at;

	void Jsonize(JsonIO& io) {
		io("id", id)("name", name)("status", status)("conclusion", conclusion)
		  ("head_branch", head_branch)("event", event)("created_at", created_at);
	}
};

struct EmuStoreRepo : Moveable<EmuStoreRepo> {
	String                    owner;
	String                    name;
	String                    description;
	String                    language;
	String                    pushed_at;
	bool                      is_private = false;
	int                       stars = 0;
	int                       forks = 0;
	Vector<EmuStoreIssue>     issues;
	Vector<EmuStorePull>      pulls;
	Vector<EmuStoreThread>    threads;
	Vector<EmuStoreRun>       runs;

	void Jsonize(JsonIO& io) {
		io("owner", owner)("name", name)("description", description)("language", language)
		  ("pushed_at", pushed_at)("is_private", is_private)("stars", stars)("forks", forks)
		  ("issues", issues)("pulls", pulls)("threads", threads)("runs", runs);
	}
};

struct EmuStore : Moveable<EmuStore> {
	int                  next_comment_id = 100;
	Vector<EmuStoreRepo> repos;

	void Jsonize(JsonIO& io) { io("next_comment_id", next_comment_id)("repos", repos); }
};

// Deterministic fixture: the records the capture harness served before the
// emulation existed. Timestamps and ids are fixed so captures are reproducible.
static EmuStore MakeSeedStore()
{
	EmuStore st;

	EmuStoreRepo& demo = st.repos.Add();
	demo.owner = "SeppoPakonen";
	demo.name = "demo-repo";
	demo.description = "Fixture repo";
	demo.language = "C++";
	demo.pushed_at = "2026-11-03T09:15:00Z";
	demo.stars = 3;
	demo.forks = 1;

	EmuStoreIssue& i1 = demo.issues.Add();
	i1.number = 1;
	i1.title = "Crash on start";
	i1.body = "Start it and it crashes.";
	i1.author = "alice";
	i1.state = "open";
	i1.created = "2026-11-02T10:00:00Z";

	EmuStoreIssue& i2 = demo.issues.Add();
	i2.number = 2;
	i2.title = "Docs gap";
	i2.author = "bob";
	i2.state = "closed";
	i2.created = "2026-11-03T12:00:00Z";

	EmuStorePull& p1 = demo.pulls.Add();
	p1.number = 1;
	p1.title = "Add parity";
	p1.body = "PR body text";
	p1.author = "alice";
	p1.state = "open";
	p1.created = "2026-11-02T09:00:00Z";
	p1.head = "feature/parity";
	p1.base = "master";
	p1.diff = "diff --git a/README.md b/README.md\n+added line\n";
	p1.url = "https://github.com/SeppoPakonen/demo-repo/pull/1";

	EmuStorePull& p2 = demo.pulls.Add();
	p2.number = 2;
	p2.title = "Merged change";
	p2.author = "alice";
	p2.state = "closed";
	p2.created = "2026-11-02T09:00:00Z";
	p2.merged_at = "2026-11-03T10:00:00Z";
	p2.head = "feature/done";
	p2.base = "master";
	p2.url = "https://github.com/SeppoPakonen/demo-repo/pull/2";

	EmuStoreThread& t1 = demo.threads.Add();
	t1.number = 1;
	EmuStoreComment& c11 = t1.comments.Add();
	c11.id = 11;
	c11.author = "carol";
	c11.body = "Repro attached";
	c11.created = "2026-11-02T11:00:00Z";
	demo.threads.Add().number = 2;

	EmuStoreRun& r501 = demo.runs.Add();
	r501.id = 501;
	r501.name = "CI";
	r501.status = "completed";
	r501.conclusion = "success";
	r501.head_branch = "master";
	r501.event = "push";
	r501.created_at = "2026-11-03T08:00:00Z";

	EmuStoreRun& r502 = demo.runs.Add();
	r502.id = 502;
	r502.name = "Docs";
	r502.status = "in_progress";
	r502.head_branch = "master";
	r502.event = "pull_request";
	r502.created_at = "2026-11-04T08:00:00Z";

	EmuStoreRun& r503 = demo.runs.Add();
	r503.id = 503;
	r503.name = "Release";
	r503.status = "completed";
	r503.conclusion = "failure";
	r503.head_branch = "master";
	r503.event = "workflow_dispatch";
	r503.created_at = "2026-11-05T08:00:00Z";

	EmuStoreRepo& second = st.repos.Add();
	second.owner = "SeppoPakonen";
	second.name = "second-repo";
	second.pushed_at = "2026-11-04T08:00:00Z";
	second.is_private = true;

	return st;
}

static String TwoDigits(int v)
{
	return v < 10 ? "0" + AsString(v) : AsString(v);
}

static String IsoNow()
{
	Time t = GetUtcTime();
	return AsString(t.year) + "-" + TwoDigits(t.month) + "-" + TwoDigits(t.day) + "T" +
	       TwoDigits(t.hour) + ":" + TwoDigits(t.minute) + ":" + TwoDigits(t.second) + "Z";
}

static String Slug(const EmuStoreRepo& r)
{
	return r.owner + "/" + r.name;
}

static EmuStoreRepo *FindRepo(EmuStore& st, const String& project)
{
	for(EmuStoreRepo& r : st.repos)
		if(Slug(r) == project)
			return &r;
	return NULL;
}

static EmuStoreIssue *FindIssue(EmuStoreRepo& r, int number)
{
	for(EmuStoreIssue& i : r.issues)
		if(i.number == number)
			return &i;
	return NULL;
}

static EmuStorePull *FindPull(EmuStoreRepo& r, int number)
{
	for(EmuStorePull& p : r.pulls)
		if(p.number == number)
			return &p;
	return NULL;
}

static EmuStoreThread *FindThread(EmuStoreRepo& r, int number, bool create)
{
	for(EmuStoreThread& t : r.threads)
		if(t.number == number)
			return &t;
	if(!create)
		return NULL;
	EmuStoreThread& t = r.threads.Add();
	t.number = number;
	return &t;
}

// Issues and pulls share one number sequence, as on GitHub.
static int NextNumber(const EmuStoreRepo& r)
{
	int n = 0;
	for(const EmuStoreIssue& i : r.issues)
		n = max(n, i.number);
	for(const EmuStorePull& p : r.pulls)
		n = max(n, p.number);
	return n + 1;
}

static int CommentCount(const EmuStoreRepo& r, int number)
{
	for(const EmuStoreThread& t : r.threads)
		if(t.number == number)
			return t.comments.GetCount();
	return 0;
}

static ForgeIssue ToIssue(const EmuStoreRepo& r, const EmuStoreIssue& i)
{
	ForgeIssue iss;
	iss.issue_id = AsString(i.number);
	iss.project = Slug(r);
	iss.title = i.title;
	iss.body = i.body;
	iss.author = i.author;
	iss.open = i.state != "closed";
	iss.created = GitHubForgeBackend::ParseGhTime(i.created);
	iss.updated = GitHubForgeBackend::ParseGhTime(i.updated);
	iss.comment_count = CommentCount(r, i.number);
	return iss;
}

static ForgeComment ToComment(const EmuStoreComment& c, const String& issue_id)
{
	ForgeComment out;
	out.comment_id = AsString(c.id);
	out.issue_id = issue_id;
	out.author = c.author;
	out.body = c.body;
	out.created = GitHubForgeBackend::ParseGhTime(c.created);
	return out;
}

static ForgePullRequest ToPull(const String& project, const EmuStorePull& p)
{
	ForgePullRequest pr;
	pr.pr_id = AsString(p.number);
	pr.project = project;
	pr.title = p.title;
	pr.body = p.body;
	pr.author = p.author;
	pr.state = !p.merged_at.IsEmpty() ? String("merged") : p.state;
	pr.created = GitHubForgeBackend::ParseGhTime(p.created);
	pr.head = p.head;
	pr.base = p.base;
	pr.url = p.url;
	return pr;
}

static ForgeRepo ToRepo(const EmuStoreRepo& r)
{
	ForgeRepo out;
	out.owner = r.owner;
	out.name = r.name;
	out.description = r.description;
	out.language = r.language;
	out.pushed_at = r.pushed_at;
	out.is_private = r.is_private;
	out.stars = r.stars;
	out.forks = r.forks;
	return out;
}

static ForgeRun ToRun(const EmuStoreRun& r)
{
	ForgeRun out;
	out.run_id = r.id;
	out.name = r.name;
	out.status = r.status;
	out.conclusion = r.conclusion;
	out.head_branch = r.head_branch;
	out.event = r.event;
	out.created_at = r.created_at;
	return out;
}

static const char *kAuthor = "emulator";

// One locked read or read-modify-write over the store file. The constructor
// takes the lock and loads the store (seeding it when absent); Commit writes
// the store back atomically; the destructor releases the lock.
class EmuTransaction {
public:
	EmuTransaction(const String& store_dir, bool write_back);
	~EmuTransaction();

	bool   Ok() const { return error.IsEmpty(); }
	bool   Commit();

	EmuStore store;
	String   error;

private:
	String path;
	int    fd = -1;
	bool   write_back;
};

EmuTransaction::EmuTransaction(const String& store_dir, bool wb)
: write_back(wb)
{
#ifndef PLATFORM_POSIX
	error = "emulated GitHub store needs POSIX file locks";
#else
	if(store_dir.IsEmpty()) {
		error = "emulated GitHub store directory is not configured";
		return;
	}
	if(!RealizeDirectory(store_dir)) {
		error = "cannot create emulated GitHub store directory: " + store_dir;
		return;
	}
	path = AppendFileName(store_dir, GitHubEmulatedBackend::StoreFileName());
	String lock_path = AppendFileName(store_dir, "github-emulated.lock");
	fd = open(~lock_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if(fd < 0) {
		error = "cannot open emulated GitHub lock: " + lock_path;
		return;
	}
	while(flock(fd, LOCK_EX) != 0)
		if(errno != EINTR) {
			close(fd);
			fd = -1;
			error = "cannot lock emulated GitHub store";
			return;
		}
	if(!FileExists(path)) {
		store = MakeSeedStore();
		this->write_back = true;
		Commit();
		return;
	}
	if(!LoadFromJson(store, ~LoadFile(path)))
		error = "emulated GitHub store is unreadable: " + path;
#endif
}

EmuTransaction::~EmuTransaction()
{
#ifdef PLATFORM_POSIX
	if(fd >= 0) {
		flock(fd, LOCK_UN);
		close(fd);
	}
#endif
}

bool EmuTransaction::Commit()
{
	if(!Ok() || !write_back)
		return Ok();
	String tmp = path + ".tmp";
	if(!SaveFile(tmp, StoreAsJson(store, true))) {
		error = "cannot write " + tmp;
		return false;
	}
#ifdef PLATFORM_POSIX
	int f = open(~tmp, O_RDONLY | O_CLOEXEC);
	if(f >= 0) {
		fsync(f);
		close(f);
	}
#endif
	if(!FileMove(tmp, path)) {
		error = "cannot replace " + path;
		return false;
	}
	return true;
}

static ForgeResult NotFound(const char *op, const String& project)
{
	return GitHubForgeBackend::Failed(op, project, "HTTP 404: Not Found");
}

GitHubEmulatedBackend::GitHubEmulatedBackend(const String& dir)
: store_dir(dir)
{
	caps.list_issues = true;
	caps.get_issue = true;
	caps.create_issue = true;
	caps.add_comment = true;
	caps.set_issue_state = true;
	caps.list_comments = true;
}

ForgeResult GitHubEmulatedBackend::ListIssues(const String& project, Vector<ForgeIssue>& out)
{
	out.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("list-issues", project, "invalid repository slug");
	EmuTransaction tx(store_dir, false);
	if(!tx.Ok())
		return Failed("list-issues", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	if(!r)
		return NotFound("list-issues", project);
	for(const EmuStoreIssue& i : r->issues)
		out.Add(ToIssue(*r, i));
	return Ok("list-issues", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::GetIssue(const String& project, const String& issue_id, ForgeIssue& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("get-issue", project, "invalid repository slug");
	if(!IsValidIssueId(issue_id))
		return Failed("get-issue", project, "invalid issue id");
	EmuTransaction tx(store_dir, false);
	if(!tx.Ok())
		return Failed("get-issue", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	EmuStoreIssue *i = r ? FindIssue(*r, ScanInt(issue_id)) : NULL;
	if(!i)
		return NotFound("get-issue", project);
	out = ToIssue(*r, *i);
	return Ok("get-issue", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("create-issue", project, "invalid repository slug");
	if(draft.title.IsEmpty())
		return Failed("create-issue", project, "issue needs a title");
	EmuTransaction tx(store_dir, true);
	if(!tx.Ok())
		return Failed("create-issue", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	if(!r)
		return NotFound("create-issue", project);
	EmuStoreIssue& i = r->issues.Add();
	i.number = NextNumber(*r);
	i.title = draft.title;
	i.body = draft.body;
	i.author = kAuthor;
	i.state = "open";
	i.created = IsoNow();
	if(!tx.Commit())
		return Failed("create-issue", project, tx.error);
	out = ToIssue(*r, i);
	return Ok("create-issue", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::AddComment(const String& project, const String& issue_id,
                                              const ForgeComment& comment, ForgeComment& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("add-comment", project, "invalid repository slug");
	if(!IsValidIssueId(issue_id))
		return Failed("add-comment", project, "invalid issue id");
	if(comment.body.IsEmpty())
		return Failed("add-comment", project, "comment needs a body");
	EmuTransaction tx(store_dir, true);
	if(!tx.Ok())
		return Failed("add-comment", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	int number = ScanInt(issue_id);
	// Comments go on issues and pull requests alike, as on GitHub.
	if(!r || (!FindIssue(*r, number) && !FindPull(*r, number)))
		return NotFound("add-comment", project);
	EmuStoreComment c;
	c.id = tx.store.next_comment_id++;
	c.author = kAuthor;
	c.body = comment.body;
	c.created = IsoNow();
	FindThread(*r, number, true)->comments.Add(pick(c));
	const EmuStoreComment& stored = FindThread(*r, number, false)->comments.Top();
	if(!tx.Commit())
		return Failed("add-comment", project, tx.error);
	out = ToComment(stored, issue_id);
	return Ok("add-comment", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::SetIssueState(const String& project, const String& issue_id,
                                                 bool open, ForgeIssue& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("set-issue-state", project, "invalid repository slug");
	if(!IsValidIssueId(issue_id))
		return Failed("set-issue-state", project, "invalid issue id");
	EmuTransaction tx(store_dir, true);
	if(!tx.Ok())
		return Failed("set-issue-state", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	EmuStoreIssue *i = r ? FindIssue(*r, ScanInt(issue_id)) : NULL;
	if(!i)
		return NotFound("set-issue-state", project);
	i->state = open ? "open" : "closed";
	i->updated = IsoNow();
	if(!tx.Commit())
		return Failed("set-issue-state", project, tx.error);
	out = ToIssue(*r, *i);
	return Ok("set-issue-state", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::ListComments(const String& project, const String& issue_id,
                                                Vector<ForgeComment>& out)
{
	out.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("list-comments", project, "invalid repository slug");
	if(!IsValidIssueId(issue_id))
		return Failed("list-comments", project, "invalid issue id");
	EmuTransaction tx(store_dir, false);
	if(!tx.Ok())
		return Failed("list-comments", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	int number = ScanInt(issue_id);
	if(!r || (!FindIssue(*r, number) && !FindPull(*r, number)))
		return NotFound("list-comments", project);
	const EmuStoreThread *t = FindThread(*r, number, false);
	if(t)
		for(const EmuStoreComment& c : t->comments)
			out.Add(ToComment(c, issue_id));
	return Ok("list-comments", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::ListPullRequests(const String& project, Vector<ForgePullRequest>& out)
{
	out.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("list-pulls", project, "invalid repository slug");
	EmuTransaction tx(store_dir, false);
	if(!tx.Ok())
		return Failed("list-pulls", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	if(!r)
		return NotFound("list-pulls", project);
	for(const EmuStorePull& p : r->pulls)
		out.Add(ToPull(project, p));
	return Ok("list-pulls", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::GetPullRequest(const String& project, const String& pr_id, ForgePullRequest& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("get-pull", project, "invalid repository slug");
	if(!IsValidIssueId(pr_id))
		return Failed("get-pull", project, "invalid pull request number");
	EmuTransaction tx(store_dir, false);
	if(!tx.Ok())
		return Failed("get-pull", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	EmuStorePull *p = r ? FindPull(*r, ScanInt(pr_id)) : NULL;
	if(!p)
		return NotFound("get-pull", project);
	out = ToPull(project, *p);
	return Ok("get-pull", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::GetPullRequestDiff(const String& project, const String& pr_id, String& out)
{
	out.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("pull-diff", project, "invalid repository slug");
	if(!IsValidIssueId(pr_id))
		return Failed("pull-diff", project, "invalid pull request number");
	EmuTransaction tx(store_dir, false);
	if(!tx.Ok())
		return Failed("pull-diff", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	EmuStorePull *p = r ? FindPull(*r, ScanInt(pr_id)) : NULL;
	if(!p)
		return NotFound("pull-diff", project);
	out = p->diff;
	return Ok("pull-diff", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::CreatePullRequest(const String& project, const String& title, const String& body,
                                                     const String& head, const String& base, ForgePullRequest& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("create-pull", project, "invalid repository slug");
	if(title.IsEmpty())
		return Failed("create-pull", project, "pull request needs a title");
	if(!IsValidBranchName(head))
		return Failed("create-pull", project, "invalid head branch");
	if(!IsValidBranchName(base))
		return Failed("create-pull", project, "invalid base branch");
	EmuTransaction tx(store_dir, true);
	if(!tx.Ok())
		return Failed("create-pull", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	if(!r)
		return NotFound("create-pull", project);
	EmuStorePull& p = r->pulls.Add();
	p.number = NextNumber(*r);
	p.title = title;
	p.body = body;
	p.author = kAuthor;
	p.state = "open";
	p.created = IsoNow();
	p.head = head;
	p.base = base;
	p.url = "https://github.com/" + project + "/pull/" + AsString(p.number);
	if(!tx.Commit())
		return Failed("create-pull", project, tx.error);
	out = ToPull(project, p);
	return Ok("create-pull", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::SetPullRequestState(const String& project, const String& pr_id, bool open, ForgePullRequest& out)
{
	if(!IsValidRepoSlug(project))
		return Failed("set-pull-state", project, "invalid repository slug");
	if(!IsValidIssueId(pr_id))
		return Failed("set-pull-state", project, "invalid pull request number");
	EmuTransaction tx(store_dir, true);
	if(!tx.Ok())
		return Failed("set-pull-state", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	EmuStorePull *p = r ? FindPull(*r, ScanInt(pr_id)) : NULL;
	if(!p)
		return NotFound("set-pull-state", project);
	if(!p->merged_at.IsEmpty())
		return Failed("set-pull-state", project, "pull request is already merged");
	p->state = open ? "open" : "closed";
	if(!tx.Commit())
		return Failed("set-pull-state", project, tx.error);
	out = ToPull(project, *p);
	return Ok("set-pull-state", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::MergePullRequest(const String& project, const String& pr_id, String& message)
{
	message.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("merge-pull", project, "invalid repository slug");
	if(!IsValidIssueId(pr_id))
		return Failed("merge-pull", project, "invalid pull request number");
	EmuTransaction tx(store_dir, true);
	if(!tx.Ok())
		return Failed("merge-pull", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	EmuStorePull *p = r ? FindPull(*r, ScanInt(pr_id)) : NULL;
	if(!p)
		return NotFound("merge-pull", project);
	// Only an open, unmerged pull request can merge; GitHub answers 405 otherwise.
	if(p->state != "open" || !p->merged_at.IsEmpty()) {
		message = "Pull Request is not mergeable";
		return Failed("merge-pull", project, message);
	}
	p->merged_at = IsoNow();
	p->state = "closed";
	message = "Pull Request successfully merged";
	if(!tx.Commit())
		return Failed("merge-pull", project, tx.error);
	return Ok("merge-pull", project, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::ListRepos(const String& owner, Vector<ForgeRepo>& out)
{
	out.Clear();
	if(!IsValidLogin(owner))
		return Failed("list-repos", owner, "invalid owner login");
	EmuTransaction tx(store_dir, false);
	if(!tx.Ok())
		return Failed("list-repos", owner, tx.error);
	for(const EmuStoreRepo& r : tx.store.repos)
		if(r.owner == owner)
			out.Add(ToRepo(r));
	return Ok("list-repos", owner, ForgeProvenance::LocalPersistent);
}

ForgeResult GitHubEmulatedBackend::ListRuns(const String& project, int limit, Vector<ForgeRun>& out)
{
	out.Clear();
	if(!IsValidRepoSlug(project))
		return Failed("list-runs", project, "invalid repository slug");
	if(limit < 1)
		limit = 1;
	if(limit > 100)
		limit = 100;
	EmuTransaction tx(store_dir, false);
	if(!tx.Ok())
		return Failed("list-runs", project, tx.error);
	EmuStoreRepo *r = FindRepo(tx.store, project);
	if(!r)
		return NotFound("list-runs", project);
	for(const EmuStoreRun& run : r->runs) {
		if(out.GetCount() >= limit)
			break;
		out.Add(ToRun(run));
	}
	return Ok("list-runs", project, ForgeProvenance::LocalPersistent);
}

END_UPP_NAMESPACE
