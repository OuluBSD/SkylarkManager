#include "PrivateHubService.h"

NAMESPACE_UPP

// Synthetic checks for PrivateHubService, called through a WebBroker registered
// under a site id, so registration is covered too. Uses a temp workspace and
// a temp data root only.

static bool PhExpect(bool cond, const char *what)
{
	if(!cond)
		Cerr() << "FAILED: PrivateHub broker service: " << what << "\n";
	return cond;
}

static const char *PH_SITE = "privatehub";

static BrokerResult Call(WebBroker& broker, const String& caller, Vector<String> scopes,
                         const String& op, ValueMap payload, const String& key)
{
	BrokerCallContext ctx;
	ctx.from_site = "tests";
	ctx.to_site = PH_SITE;
	ctx.caller_id = caller;
	ctx.scopes = clone(scopes);
	BrokerCall call;
	call.op = op;
	call.payload = payload;
	call.idempotency_key = key;
	return broker.Call(ctx, call);
}

static ValueMap Payload(const char *k1, const String& v1, const char *k2 = NULL, const String& v2 = String(),
                        const char *k3 = NULL, const String& v3 = String())
{
	ValueMap m;
	m.Add("project", "oulubsd/broker");
	if(k1)
		m.Add(k1, v1);
	if(k2)
		m.Add(k2, v2);
	if(k3)
		m.Add(k3, v3);
	return m;
}

static Vector<String> Scopes(bool read, bool write)
{
	Vector<String> s;
	if(read)
		s.Add(PRIVATEHUB_SCOPE_READ);
	if(write)
		s.Add(PRIVATEHUB_SCOPE_WRITE);
	return s;
}

bool RunPrivateHubServiceSelfTests(const String& scratch_dir)
{
	bool ok = true;
	String ws = AppendFileName(scratch_dir, "workspace");
	String data = AppendFileName(scratch_dir, "privatehub-data");
	RealizeDirectory(ws);
	RealizeDirectory(data);

	PrivateHubWorkspaceBackend backend(ws, data);
	WebBroker broker;
	String register_error;
	ok &= PhExpect(RegisterPrivateHubService(broker, PH_SITE, backend, data, register_error),
	   "service registers on a fresh data root");

	// Gates: unknown op, scopes, caller, and idempotency key are all checked before any work.
	BrokerResult r = Call(broker, "alice", Scopes(true, true), "issue.delete", Payload(NULL, ""), "k-x");
	ok &= PhExpect(!r.ok && r.status == 404, "unknown op is 404");
	r = Call(broker, "alice", Scopes(false, false), "issue.list", Payload(NULL, ""), "");
	ok &= PhExpect(!r.ok && r.status == 403, "read op without read scope is 403 (deny by default)");
	r = Call(broker, "alice", Scopes(true, false), "issue.create", Payload("title", "T"), "k-scope");
	ok &= PhExpect(!r.ok && r.status == 403, "write op without write scope is 403");
	r = Call(broker, "", Scopes(true, true), "issue.create", Payload("title", "T"), "k-anon");
	ok &= PhExpect(!r.ok && r.status == 401, "write op without a caller is 401");
	r = Call(broker, "alice", Scopes(true, true), "issue.create", Payload("title", "T"), "");
	ok &= PhExpect(!r.ok && r.status == 400, "write op without an idempotency key is 400");
	r = Call(broker, "alice", Scopes(true, true), "issue.create", Payload("title", "T"), "bad key!");
	ok &= PhExpect(!r.ok && r.status == 400, "idempotency key with unsafe characters is 400");
	r = Call(broker, "alice", Scopes(true, true), "issue.create", Payload(NULL, ""), "k-empty");
	ok &= PhExpect(!r.ok && r.status == 422, "backend refusal (empty title) is 422");
	r = Call(broker, "alice", Scopes(true, true), "issue.list", Payload(NULL, ""), "");
	ok &= PhExpect(r.ok && ((ValueArray)r.payload.Get("issues")).GetCount() == 0, "a refused write leaves nothing behind");

	// Writes: author is the caller, and a repeat with the same key returns the prior result.
	ValueMap create = Payload("title", "Crash on start", "body", "segv");
	create.Add("author", "mallory");   // a payload author must be ignored
	r = Call(broker, "alice", Scopes(true, true), "issue.create", create, "k-create-1");
	ok &= PhExpect(r.ok && r.status == 200, "issue.create succeeds");
	String issue_id = AsString(((ValueMap)r.payload.Get("issue")).Get("issue_id"));
	ok &= PhExpect(issue_id.StartsWith("phi_"), "created issue has a backend id");
	ok &= PhExpect(AsString(((ValueMap)r.payload.Get("issue")).Get("author")) == "alice",
	   "author is the authenticated caller, not the payload author");
	r = Call(broker, "alice", Scopes(true, true), "issue.create", create, "k-create-1");
	ok &= PhExpect(r.ok && AsString(((ValueMap)r.payload.Get("issue")).Get("issue_id")) == issue_id,
	   "repeat with the same key returns the prior issue");
	r = Call(broker, "alice", Scopes(true, true), "issue.list", Payload(NULL, ""), "");
	ok &= PhExpect(r.ok && ((ValueArray)r.payload.Get("issues")).GetCount() == 1,
	   "repeat with the same key did not create a second issue");
	r = Call(broker, "bob", Scopes(true, true), "issue.create", create, "k-create-1");
	ok &= PhExpect(r.ok && AsString(((ValueMap)r.payload.Get("issue")).Get("issue_id")) != issue_id,
	   "the same key from a different caller is a different write");

	ValueMap comment = Payload("issue_id", issue_id, "body", "repro attached");
	r = Call(broker, "bob", Scopes(true, true), "issue.comment", comment, "k-comment-1");
	ok &= PhExpect(r.ok && AsString(((ValueMap)r.payload.Get("comment")).Get("author")) == "bob",
	   "issue.comment succeeds with the caller as author");
	r = Call(broker, "bob", Scopes(true, true), "issue.comment", comment, "k-comment-1");
	ok &= PhExpect(r.ok, "repeat comment with the same key succeeds");
	Vector<ForgeComment> comments;
	ok &= PhExpect(backend.ListComments("oulubsd/broker", issue_id, comments).ok && comments.GetCount() == 1,
	   "repeat comment with the same key did not add a second comment");
	r = Call(broker, "bob", Scopes(true, true), "issue.comment", Payload("issue_id", "phi_missing", "body", "x"),
	         "k-comment-2");
	ok &= PhExpect(!r.ok && r.status == 422, "comment on a missing issue is 422");

	// Reads: get and list, with scope.
	r = Call(broker, "reader", Scopes(true, false), "issue.get", Payload("issue_id", issue_id), "");
	ok &= PhExpect(r.ok && AsString(((ValueMap)r.payload.Get("issue")).Get("title")) == "Crash on start",
	   "issue.get returns the issue");
	r = Call(broker, "reader", Scopes(true, false), "issue.get", Payload("issue_id", "phi_missing"), "");
	ok &= PhExpect(!r.ok && r.status == 422, "issue.get on a missing id is 422");
	r = Call(broker, "reader", Scopes(true, false), "issue.list", Payload(NULL, ""), "");
	ok &= PhExpect(r.ok && ((ValueArray)r.payload.Get("issues")).GetCount() == 2,
	   "issue.list returns both issues without comment records");
	ValueMap bad_project;
	bad_project.Add("project", "bad project!");
	r = Call(broker, "reader", Scopes(true, false), "issue.list", bad_project, "");
	ok &= PhExpect(!r.ok && r.status == 400, "invalid project is 400");

	// Wiki, topics, posts, and release files are written through the backend and read through the broker.
	PrivateHubWikiPage page;
	page.slug = "home";
	page.title = "Home";
	page.body = "= Welcome";
	page.updated = "2026-11-09";
	String rev;
	ok &= PhExpect(backend.PutWikiPage("oulubsd/broker", page, "", rev).ok, "wiki page stored for the read checks");
	r = Call(broker, "reader", Scopes(true, false), "wiki.list", Payload(NULL, ""), "");
	ok &= PhExpect(r.ok && ((ValueArray)r.payload.Get("pages")).GetCount() == 1, "wiki.list returns the page");
	r = Call(broker, "reader", Scopes(true, false), "wiki.get", Payload("slug", "home"), "");
	ok &= PhExpect(r.ok && AsString(((ValueMap)r.payload.Get("page")).Get("body")) == "= Welcome",
	   "wiki.get returns the body");
	r = Call(broker, "reader", Scopes(true, false), "wiki.get", Payload("slug", "nope"), "");
	ok &= PhExpect(!r.ok && r.status == 422, "wiki.get on a missing slug is 422");

	PrivateHubDiscussionTopic topic;
	topic.slug = "welcome";
	topic.title = "Welcome thread";
	topic.author = "alice";
	topic.created = "2026-11-09";
	ok &= PhExpect(backend.PutDiscussionTopic("oulubsd/broker", topic, "", rev).ok, "topic stored");
	PrivateHubPost post;
	post.post_id = "p1";
	post.topic = "welcome";
	post.author = "bob";
	post.body = "hello";
	post.created = "2026-11-09";
	ok &= PhExpect(backend.PutDiscussionPost("oulubsd/broker", post, "", rev).ok, "post stored");
	r = Call(broker, "reader", Scopes(true, false), "topic.list", Payload(NULL, ""), "");
	ok &= PhExpect(r.ok && ((ValueArray)r.payload.Get("topics")).GetCount() == 1, "topic.list excludes posts");
	r = Call(broker, "reader", Scopes(true, false), "topic.get", Payload("slug", "welcome"), "");
	ok &= PhExpect(r.ok && AsString(((ValueMap)r.payload.Get("topic")).Get("title")) == "Welcome thread",
	   "topic.get returns the topic");
	r = Call(broker, "reader", Scopes(true, false), "post.list", Payload("topic", "welcome"), "");
	ok &= PhExpect(r.ok && ((ValueArray)r.payload.Get("posts")).GetCount() == 1
	   && AsString(((ValueMap)((ValueArray)r.payload.Get("posts"))[0]).Get("body")) == "hello",
	   "post.list returns the post under its topic");

	PrivateHubReleaseFile file;
	file.name = "topside-1.0.0-linux.tar.gz";
	file.release = "1.0.0";
	file.size_bytes = 14889728;
	file.url = "/files/topside-1.0.0-linux.tar.gz";
	file.updated = "2026-11-09";
	ok &= PhExpect(backend.PutReleaseFile("oulubsd/broker", file, "", rev).ok, "release file stored");
	r = Call(broker, "reader", Scopes(true, false), "file.list", Payload(NULL, ""), "");
	ok &= PhExpect(r.ok && ((ValueArray)r.payload.Get("files")).GetCount() == 1, "file.list returns the file");
	r = Call(broker, "reader", Scopes(true, false), "file.get", Payload("name", file.name), "");
	ok &= PhExpect(r.ok && (int64)((ValueMap)r.payload.Get("file")).Get("size_bytes") == 14889728,
	   "file.get returns the size");

	// Unknown site id is refused by the broker, not by the service.
	BrokerCallContext ctx;
	ctx.from_site = "tests";
	ctx.to_site = "not-registered";
	ctx.scopes = Scopes(true, false);
	BrokerCall call;
	call.op = "issue.list";
	call.payload = Payload(NULL, "");
	r = broker.Call(ctx, call);
	ok &= PhExpect(!r.ok, "a call to an unregistered site id is refused");
	// Single owner: a second owner of the same data root in this process is refused.
	WebBroker rival;
	String owner_error;
	ok &= PhExpect(!RegisterPrivateHubService(rival, PH_SITE, backend, data, owner_error) &&
	   owner_error.Find("owned by another process") >= 0,
	   "a second owner of the same data root is refused");

	// Expiry: with a zero TTL a recorded result is expired at once, so the key runs
	// the write again and gets a new issue.
	String ttl_data = AppendFileName(scratch_dir, "privatehub-data-ttl");
	RealizeDirectory(ttl_data);
	WebBroker expiring;
	ok &= PhExpect(RegisterPrivateHubService(expiring, PH_SITE, backend, ttl_data, owner_error, 0),
	   "a zero-TTL service registers on its own data root");
	BrokerResult first = Call(expiring, "alice", Scopes(true, true), "issue.create", Payload("title", "Expiring"), "k-ttl-1");
	BrokerResult second = Call(expiring, "alice", Scopes(true, true), "issue.create", Payload("title", "Expiring"), "k-ttl-1");
	ok &= PhExpect(first.ok && second.ok &&
	   AsString(((ValueMap)first.payload.Get("issue")).Get("issue_id")) !=
	   AsString(((ValueMap)second.payload.Get("issue")).Get("issue_id")),
	   "an expired idempotency key runs the write again");

	return ok;
}

END_UPP_NAMESPACE
