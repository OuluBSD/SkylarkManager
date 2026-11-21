#include "PrivateHubService.h"

#ifdef PLATFORM_POSIX
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <errno.h>
#endif

#ifdef PLATFORM_WIN32
#include <windows.h>
#endif

NAMESPACE_UPP

static const char *IDEMPOTENCY_COLLECTION = "broker-idempotency";

static BrokerResult Reply(int status, const String& error)
{
	BrokerResult r;
	r.ok = false;
	r.status = status;
	r.error = error;
	return r;
}

static BrokerResult Done(const ValueMap& payload)
{
	BrokerResult r;
	r.ok = true;
	r.status = 200;
	r.payload = payload;
	return r;
}

// Maps a backend result. Backend refusals are 422 with the backend's message.
static BrokerResult FromForge(const ForgeResult& fr, const ValueMap& payload)
{
	if(!fr.ok)
		return Reply(422, fr.error);
	return Done(payload);
}

static String Arg(const BrokerCall& call, const char *key)
{
	return AsString(call.payload.Get(key));
}

static bool HasScope(const BrokerCallContext& ctx, const char *scope)
{
	for(const String& s : ctx.scopes)
		if(s == scope)
			return true;
	return false;
}

// Caller ids and idempotency keys become record ids, so only these characters
// are allowed.
static bool SafeToken(const String& s, bool allow_dot)
{
	if(s.IsEmpty() || s.GetCount() > 100)
		return false;
	for(int i = 0; i < s.GetCount(); i++) {
		int c = s[i];
		bool ok = IsAlNum(c) || c == '_' || c == '-' || (allow_dot && c == '.');
		if(!ok)
			return false;
	}
	return true;
}

static String IdempotencyId(const BrokerCallContext& ctx, const BrokerCall& call)
{
	String op = call.op;
	op.Replace(".", "_");
	return ctx.caller_id + "__" + op + "__" + call.idempotency_key;
}

static bool IsWriteOp(const String& op)
{
	return op == "issue.create" || op == "issue.comment";
}

static bool IsKnownOp(const String& op)
{
	static const char *ops[] = {
		"issue.list", "issue.get", "issue.create", "issue.comment",
		"wiki.list", "wiki.get", "topic.list", "topic.get", "post.list",
		"file.list", "file.get", "repo.info",
	};
	for(const char *o : ops)
		if(op == o)
			return true;
	return false;
}

template <class T>
static ValueMap AsRecord(T& item)
{
	return (ValueMap)StoreAsJsonValue(item);
}

template <class T>
static ValueArray AsRecords(Vector<T>& items)
{
	ValueArray a;
	for(T& item : items)
		a.Add(StoreAsJsonValue(item));
	return a;
}

static int64 NowSeconds()
{
	return (int64)(GetUtcTime() - Time(1970, 1, 1));
}

DataRootOwner::DataRootOwner()
{
#ifdef PLATFORM_WIN32
	handle = NULL;
#else
	fd = -1;
#endif
	held = false;
}

DataRootOwner::~DataRootOwner()
{
	Release();
}

bool DataRootOwner::Acquire(const String& data_root, String& error)
{
	Release();
	String path = AppendFileName(data_root, "owner.lock");
#ifdef PLATFORM_WIN32
	HANDLE h = CreateFileW(ToSystemCharsetW(path), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
	                       FILE_ATTRIBUTE_NORMAL, NULL);
	if(h == INVALID_HANDLE_VALUE || h == NULL) {
		error = "cannot open owner lock: " + path;
		return false;
	}
	OVERLAPPED ov;
	memset(&ov, 0, sizeof(ov));
	if(!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
		CloseHandle(h);
		error = "data root is owned by another process: " + data_root;
		return false;
	}
	handle = h;
#else
	int h = open(~path, O_CREAT | O_RDWR, 0600);
	if(h < 0) {
		error = "cannot open owner lock: " + path + " (" + AsString(errno) + ")";
		return false;
	}
	if(flock(h, LOCK_EX | LOCK_NB) != 0) {
		close(h);
		error = "data root is owned by another process: " + data_root;
		return false;
	}
	fd = h;
#endif
	held = true;
	return true;
}

void DataRootOwner::Release()
{
	if(!held)
		return;
#ifdef PLATFORM_WIN32
	if(handle) {
		OVERLAPPED ov;
		memset(&ov, 0, sizeof(ov));
		UnlockFileEx((HANDLE)handle, 0, 1, 0, &ov);
		CloseHandle((HANDLE)handle);
	}
	handle = NULL;
#else
	if(fd >= 0) {
		flock(fd, LOCK_UN);
		close(fd);
	}
	fd = -1;
#endif
	held = false;
}

PrivateHubService::PrivateHubService(PrivateHubWorkspaceBackend& backend_, const String& data_root_,
                                     int64 idempotency_ttl_seconds)
	: backend(backend_), data_root(data_root_), ttl(idempotency_ttl_seconds)
{
}

bool PrivateHubService::AcquireDataRoot(String& error)
{
	RealizeDirectory(data_root);
	return owner.Acquire(data_root, error);
}

BrokerResult PrivateHubService::Handle(const BrokerCallContext& ctx, const BrokerCall& call)
{
	if(!IsKnownOp(call.op))
		return Reply(404, "unknown operation: " + call.op);
	bool write = IsWriteOp(call.op);
	if(!HasScope(ctx, write ? PRIVATEHUB_SCOPE_WRITE : PRIVATEHUB_SCOPE_READ))
		return Reply(403, "missing scope for " + call.op);
	if(!write)
		return Execute(ctx, call);

	if(ctx.caller_id.IsEmpty())
		return Reply(401, "writes need an authenticated caller");
	if(!SafeToken(ctx.caller_id, true))
		return Reply(400, "caller_id is not usable as an idempotency scope");
	if(!SafeToken(call.idempotency_key, false))
		return Reply(400, "writes need an idempotency_key of [A-Za-z0-9_-], up to 100 characters");

	Mutex::Lock hold(lock);
	bool found = false;
	String rev;
	BrokerResult prior = Recorded(ctx, call, found, rev);
	if(found)
		return prior;
	BrokerResult res = Execute(ctx, call);
	if(res.ok)
		Record(ctx, call, res, rev);
	return res;
}

String PrivateHubService::AuditOperation(const String& op) const
{
	if(op == "repo.info" || op == "issue.list" || op == "issue.get" ||
	   op == "issue.create" || op == "issue.comment" || op == "wiki.list" ||
	   op == "wiki.get" || op == "topic.list" || op == "topic.get" ||
	   op == "post.list" || op == "file.list" || op == "file.get")
		return op;
	return "unclassified";
}

BrokerResult PrivateHubService::Recorded(const BrokerCallContext& ctx, const BrokerCall& call, bool& found, String& rev)
{
	found = false;
	rev.Clear();
	IRecordCollection& coll = TheStoreManager().JsonCollection("privatehub", data_root, IDEMPOTENCY_COLLECTION);
	String json;
	if(!coll.Get(IdempotencyId(ctx, call), json, rev).ok) {
		rev.Clear();
		return BrokerResult();
	}
	BrokerResult res;
	try {
		Value v = ParseJSON(json);
		if(!v.Is<ValueMap>())
			return Reply(500, "corrupt idempotency record");
		ValueMap rec = (ValueMap)v;
		// A record older than the TTL is expired: the write runs again and overwrites
		// it. Records written before the TTL existed carry no timestamp and do not expire.
		Value at = rec.Get("recorded_unix", Value());
		if(IsNumber(at) && NowSeconds() - (int64)at >= ttl)
			return BrokerResult();
		res = Done((ValueMap)rec.Get("payload"));
	}
	catch(Exc& e) {
		return Reply(500, String("cannot read idempotency record: ") + e);
	}
	found = true;
	return res;
}

void PrivateHubService::Record(const BrokerCallContext& ctx, const BrokerCall& call, const BrokerResult& res,
                               const String& rev)
{
	ValueMap rec;
	rec.Add("payload", res.payload);
	rec.Add("recorded_unix", NowSeconds());
	IRecordCollection& coll = TheStoreManager().JsonCollection("privatehub", data_root, IDEMPOTENCY_COLLECTION);
	String new_rev;
	coll.Put(IdempotencyId(ctx, call), AsJSON(Value(rec)), rev, new_rev);
}

BrokerResult PrivateHubService::Execute(const BrokerCallContext& ctx, const BrokerCall& call)
{
	const String& op = call.op;
	String project = Arg(call, "project");
	if(!PrivateHubWorkspaceBackend::ValidProjectKey(project))
		return Reply(400, "project is missing or invalid");

	if(op == "repo.info") {
		// Live branch and head of the local checkout, for evidence references.
		Vector<String> parts = Split(project, '/');
		if(parts.GetCount() != 2)
			return Reply(400, "repo.info needs an org/repo project");
		RepoInfo info = backend.RepoStatus(parts[0], parts[1]);
		if(!info.is_repo)
			return Reply(404, "no local checkout for " + project);
		ValueMap out;
		out.Add("project", project);
		out.Add("branch", info.branch);
		out.Add("head_commit", info.head_commit);
		return Done(out);
	}
	if(op == "issue.list") {
		Vector<ForgeIssue> items;
		ForgeResult fr = backend.ListIssues(project, items);
		ValueMap out;
		out.Add("issues", AsRecords(items));
		return FromForge(fr, out);
	}
	if(op == "issue.get") {
		ForgeIssue iss;
		ForgeResult fr = backend.GetIssue(project, Arg(call, "issue_id"), iss);
		ValueMap out;
		out.Add("issue", AsRecord(iss));
		return FromForge(fr, out);
	}
	if(op == "issue.create") {
		ForgeIssue draft;
		draft.title = Arg(call, "title");
		draft.body = Arg(call, "body");
		draft.author = ctx.caller_id;   // authoritative; the payload author is ignored
		ForgeIssue iss;
		ForgeResult fr = backend.CreateIssue(project, draft, iss);
		ValueMap out;
		out.Add("issue", AsRecord(iss));
		return FromForge(fr, out);
	}
	if(op == "issue.comment") {
		ForgeComment c;
		c.issue_id = Arg(call, "issue_id");
		c.body = Arg(call, "body");
		c.author = ctx.caller_id;
		ForgeComment done;
		ForgeResult fr = backend.AddComment(project, c.issue_id, c, done);
		ValueMap out;
		out.Add("comment", AsRecord(done));
		return FromForge(fr, out);
	}
	if(op == "wiki.list") {
		Vector<PrivateHubWikiPage> items;
		ForgeResult fr = backend.ListWikiPages(project, items);
		ValueMap out;
		out.Add("pages", AsRecords(items));
		return FromForge(fr, out);
	}
	if(op == "wiki.get") {
		PrivateHubWikiPage page;
		ForgeResult fr = backend.GetWikiPage(project, Arg(call, "slug"), page);
		ValueMap out;
		out.Add("page", AsRecord(page));
		return FromForge(fr, out);
	}
	if(op == "topic.list") {
		Vector<PrivateHubDiscussionTopic> items;
		ForgeResult fr = backend.ListDiscussionTopics(project, items);
		ValueMap out;
		out.Add("topics", AsRecords(items));
		return FromForge(fr, out);
	}
	if(op == "topic.get") {
		PrivateHubDiscussionTopic topic;
		ForgeResult fr = backend.GetDiscussionTopic(project, Arg(call, "slug"), topic);
		ValueMap out;
		out.Add("topic", AsRecord(topic));
		return FromForge(fr, out);
	}
	if(op == "post.list") {
		Vector<PrivateHubPost> items;
		ForgeResult fr = backend.ListDiscussionPosts(project, Arg(call, "topic"), items);
		ValueMap out;
		out.Add("posts", AsRecords(items));
		return FromForge(fr, out);
	}
	if(op == "file.list") {
		Vector<PrivateHubReleaseFile> items;
		ForgeResult fr = backend.ListReleaseFiles(project, items);
		ValueMap out;
		out.Add("files", AsRecords(items));
		return FromForge(fr, out);
	}
	// file.get is the only remaining known op.
	PrivateHubReleaseFile file;
	ForgeResult fr = backend.GetReleaseFile(project, Arg(call, "name"), file);
	ValueMap out;
	out.Add("file", AsRecord(file));
	return FromForge(fr, out);
}

bool RegisterPrivateHubService(WebBroker& broker, const String& site_id,
                               PrivateHubWorkspaceBackend& backend, const String& data_root,
                               String& error, int64 idempotency_ttl_seconds)
{
	PrivateHubService *svc = new PrivateHubService(backend, data_root, idempotency_ttl_seconds);
	if(!svc->AcquireDataRoot(error)) {
		delete svc;
		return false;
	}
	broker.Register(site_id, svc);
	return true;
}

END_UPP_NAMESPACE
