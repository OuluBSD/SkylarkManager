#include "PrivateHubService.h"

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
		"file.list", "file.get",
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

PrivateHubService::PrivateHubService(PrivateHubWorkspaceBackend& backend_, const String& data_root_)
	: backend(backend_), data_root(data_root_)
{
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
	BrokerResult prior = Recorded(ctx, call, found);
	if(found)
		return prior;
	BrokerResult res = Execute(ctx, call);
	if(res.ok)
		Record(ctx, call, res);
	return res;
}

BrokerResult PrivateHubService::Recorded(const BrokerCallContext& ctx, const BrokerCall& call, bool& found)
{
	found = false;
	IRecordCollection& coll = TheStoreManager().JsonCollection("privatehub", data_root, IDEMPOTENCY_COLLECTION);
	String json, rev;
	if(!coll.Get(IdempotencyId(ctx, call), json, rev).ok)
		return BrokerResult();
	BrokerResult res;
	try {
		Value v = ParseJSON(json);
		if(!v.Is<ValueMap>())
			return Reply(500, "corrupt idempotency record");
		ValueMap rec = (ValueMap)v;
		res = Done((ValueMap)rec.Get("payload"));
	}
	catch(Exc& e) {
		return Reply(500, String("cannot read idempotency record: ") + e);
	}
	found = true;
	return res;
}

void PrivateHubService::Record(const BrokerCallContext& ctx, const BrokerCall& call, const BrokerResult& res)
{
	ValueMap rec;
	rec.Add("payload", res.payload);
	IRecordCollection& coll = TheStoreManager().JsonCollection("privatehub", data_root, IDEMPOTENCY_COLLECTION);
	String new_rev;
	coll.Put(IdempotencyId(ctx, call), AsJSON(Value(rec)), String(), new_rev);
}

BrokerResult PrivateHubService::Execute(const BrokerCallContext& ctx, const BrokerCall& call)
{
	const String& op = call.op;
	String project = Arg(call, "project");
	if(!PrivateHubWorkspaceBackend::ValidProjectKey(project))
		return Reply(400, "project is missing or invalid");

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

void RegisterPrivateHubService(WebBroker& broker, const String& site_id,
                               PrivateHubWorkspaceBackend& backend, const String& data_root)
{
	broker.Register(site_id, new PrivateHubService(backend, data_root));
}

END_UPP_NAMESPACE
