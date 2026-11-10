#include "TaskFoundryService.h"

NAMESPACE_UPP

static bool HasScope(const TfActor& a, const char *scope)
{
	for(const String& s : a.scopes)
		if(s == scope)
			return true;
	return false;
}

static TfResult Fail(int status, const String& error)
{
	TfResult r;
	r.ok = false;
	r.status = status;
	r.error = error;
	return r;
}

static TfResult Done(int status, const ValueMap& payload)
{
	TfResult r;
	r.ok = true;
	r.status = status;
	r.payload = payload;
	return r;
}

static String NewId(const char *prefix)
{
	return String(prefix) + "_" + ToLower(Format(Uuid::Create()));
}

static String Text(const ValueMap& m, const char *key)
{
	return (String)m.Get(key, "");
}

// Loads one record. False when it is absent or unreadable.
static bool LoadDoc(JsonFileStore& col, const String& id, ValueMap& doc, String& revision)
{
	String json;
	if(!JsonFileStore::ValidId(id) || !col.Get(id, json, revision).ok)
		return false;
	try {
		Value v = ParseJSON(json);
		if(!v.Is<ValueMap>())
			return false;
		doc = (ValueMap)v;
	}
	catch(Exc&) {
		return false;
	}
	return true;
}

static bool SaveDoc(JsonFileStore& col, const String& id, const ValueMap& doc,
                    const String& expected_revision, String& error)
{
	String revision;
	StoreResult r = col.Put(id, AsJSON(doc, false), expected_revision, revision);
	if(!r.ok)
		error = r.error;
	return r.ok;
}

// Evidence fields in a fixed order, so the hash does not depend on map order.
static String EvidenceKey(const ValueMap& e)
{
	return Text(e, "kind") + "\x1f" + Text(e, "paper_id") + "\x1f" + Text(e, "paper_revision") + "\x1f" +
	       Text(e, "node_id") + "\x1f" + Text(e, "project") + "\x1f" + Text(e, "branch") + "\x1f" +
	       Text(e, "head_commit");
}

static ValueArray EvidenceOf(const ValueMap& proposal)
{
	ValueArray out;
	Value v = proposal.Get("evidence", Value());
	if(v.Is<ValueArray>())
		out = (ValueArray)v;
	return out;
}

String TaskFoundryContentHash(const ValueMap& proposal)
{
	String canonical = Text(proposal, "title") + "\x1e" + Text(proposal, "body") + "\x1e" +
	                   Text(proposal, "repository") + "\x1e" +
	                   Format("%.6f", (double)proposal.Get("estimated_hours", 0.0));
	ValueArray evidence = EvidenceOf(proposal);
	for(int i = 0; i < evidence.GetCount(); i++) {
		ValueMap e = evidence[i];
		canonical << "\x1e" << EvidenceKey(e);
	}
	return SHA256String(canonical);
}

Vector<String> TaskFoundryGrantableScopes()
{
	Vector<String> s;
	s.Add(TF_SCOPE_WRITE);
	s.Add(TF_SCOPE_SCIENCE_READ);
	s.Add(TF_SCOPE_PRIVATEHUB_READ);
	s.Add(TF_SCOPE_COMPANY_WRITE);
	s.Add(TF_SCOPE_COMPANY_READ);
	return s;
}

Vector<String> TaskFoundryScopesOf(const Vector<String>& granted)
{
	Vector<String> allowed = TaskFoundryGrantableScopes();
	Vector<String> s;
	for(const String& g : granted)
		if(FindIndex(allowed, g) >= 0 && FindIndex(s, g) < 0)
			s.Add(g);
	return s;
}

TaskFoundryService::TaskFoundryService(WebBroker& broker_, const String& data_root)
	: candidates(TASKFOUNDRY_SITE_ID, data_root, "candidates")
	, proposals(TASKFOUNDRY_SITE_ID, data_root, "proposals")
	, submissions(TASKFOUNDRY_SITE_ID, data_root, "submissions")
	, broker(broker_)
{
}

BrokerResult TaskFoundryService::Ask(const TfActor& a, const String& to_site, const String& op,
                                     const ValueMap& payload, const String& idempotency_key)
{
	BrokerCallContext ctx;
	ctx.from_site = TASKFOUNDRY_SITE_ID;
	ctx.to_site = to_site;
	ctx.caller_id = a.user_id;
	ctx.scopes = clone(a.scopes);
	BrokerCall call;
	call.op = op;
	call.payload = payload;
	call.idempotency_key = idempotency_key;
	return broker.Call(ctx, call);
}

TfResult TaskFoundryService::CreateCandidate(const TfActor& a, const String& title_in, const String& summary,
                                             const String& paper_id)
{
	if(!HasScope(a, TF_SCOPE_WRITE))
		return Fail(403, "missing scope taskfoundry:write");
	if(!HasScope(a, TF_SCOPE_SCIENCE_READ))
		return Fail(403, "missing scope science.read");
	String title = TrimBoth(title_in);
	if(title.IsEmpty())
		return Fail(400, "candidate title is required");

	ValueMap ask;
	ask.Add("id", paper_id);
	BrokerResult paper = Ask(a, "science", "get-paper", ask);
	if(!paper.ok)
		return paper.status == 404 ? Fail(400, "paper not found") : Fail(paper.status, paper.error);

	Mutex::Lock hold(lock);
	ValueMap doc;
	doc.Add("title", title);
	doc.Add("summary", summary);
	doc.Add("paper_id", paper_id);
	doc.Add("paper_title", Text(paper.payload, "title"));
	doc.Add("paper_revision", Text(paper.payload, "revision"));
	doc.Add("created_by", a.user_id);
	doc.Add("created", AsString(GetSysTime()));
	String id = NewId("cand");
	String error;
	if(!SaveDoc(candidates, id, doc, "", error))
		return Fail(500, "cannot save candidate: " + error);
	ValueMap out;
	out.Add("candidate_id", id);
	return Done(201, out);
}

TfResult TaskFoundryService::CreateProposal(const TfActor& a, const String& candidate_id)
{
	if(!HasScope(a, TF_SCOPE_WRITE))
		return Fail(403, "missing scope taskfoundry:write");

	Mutex::Lock hold(lock);
	ValueMap candidate;
	String candidate_rev;
	if(!LoadDoc(candidates, candidate_id, candidate, candidate_rev))
		return Fail(404, "candidate not found");

	ValueMap doc;
	doc.Add("candidate_id", candidate_id);
	doc.Add("title", Text(candidate, "title"));
	doc.Add("body", Text(candidate, "summary"));
	doc.Add("repository", "");
	doc.Add("estimated_hours", 0.0);
	doc.Add("evidence", ValueArray());
	doc.Add("status", TF_STATUS_DRAFT);
	doc.Add("created_by", a.user_id);
	doc.Add("created", AsString(GetSysTime()));
	doc.Add("updated", doc.Get("created"));
	String id = NewId("prop");
	String error;
	if(!SaveDoc(proposals, id, doc, "", error))
		return Fail(500, "cannot save proposal: " + error);
	ValueMap out;
	out.Add("proposal_id", id);
	return Done(201, out);
}

TfResult TaskFoundryService::EditProposal(const TfActor& a, const String& proposal_id, const String& title,
                                          const String& body, const String& repository, double estimated_hours)
{
	if(!HasScope(a, TF_SCOPE_WRITE))
		return Fail(403, "missing scope taskfoundry:write");

	Mutex::Lock hold(lock);
	ValueMap doc;
	String rev;
	if(!LoadDoc(proposals, proposal_id, doc, rev))
		return Fail(404, "proposal not found");

	// The live content changes; a submitted snapshot does not (it lives in
	// the submission record). Any content change needs a new validation.
	doc.Set("title", TrimBoth(title));
	doc.Set("body", body);
	doc.Set("repository", repository);
	doc.Set("estimated_hours", estimated_hours);
	doc.Set("status", TF_STATUS_DRAFT);
	doc.Set("content_hash", "");
	doc.Set("messages", ValueArray());
	doc.Set("updated", AsString(GetSysTime()));

	String error;
	if(!SaveDoc(proposals, proposal_id, doc, rev, error))
		return Fail(500, "cannot save proposal: " + error);
	return Done(200, doc);
}

TfResult TaskFoundryService::AttachEvidence(const TfActor& a, const String& proposal_id, const String& kind,
                                            const String& ref, const String& node_id)
{
	if(!HasScope(a, TF_SCOPE_WRITE))
		return Fail(403, "missing scope taskfoundry:write");

	ValueMap entry;
	entry.Add("kind", kind);
	if(kind == "science") {
		if(!HasScope(a, TF_SCOPE_SCIENCE_READ))
			return Fail(403, "missing scope science.read");
		if(node_id.IsEmpty())
			return Fail(400, "science evidence needs a node id");
		ValueMap ask;
		ask.Add("id", ref);
		BrokerResult paper = Ask(a, "science", "get-paper", ask);
		if(!paper.ok)
			return paper.status == 404 ? Fail(400, "paper not found") : Fail(paper.status, paper.error);
		BrokerResult article = Ask(a, "science", "get-article", ask);
		if(!article.ok)
			return Fail(400, "paper has no article: " + article.error);
		bool found = false;
		ValueMap tree = article.payload.Get("article", ValueMap());
		ValueArray nodes = tree.Get("nodes", ValueArray());
		for(int i = 0; i < nodes.GetCount() && !found; i++) {
			ValueMap n = nodes[i];
			found = Text(n, "id") == node_id;
		}
		if(!found)
			return Fail(400, "node " + node_id + " is not in the article");
		entry.Add("paper_id", ref);
		entry.Add("paper_revision", Text(paper.payload, "revision"));
		entry.Add("node_id", node_id);
	}
	else if(kind == "privatehub") {
		if(!HasScope(a, TF_SCOPE_PRIVATEHUB_READ))
			return Fail(403, "missing scope privatehub:read");
		ValueMap ask;
		ask.Add("project", ref);
		BrokerResult repo = Ask(a, "privatehub", "repo.info", ask);
		if(!repo.ok)
			return Fail(repo.status == 404 ? 400 : repo.status, repo.error);
		entry.Add("project", ref);
		entry.Add("branch", Text(repo.payload, "branch"));
		entry.Add("head_commit", Text(repo.payload, "head_commit"));
	}
	else
		return Fail(400, "evidence kind must be science or privatehub");

	Mutex::Lock hold(lock);
	ValueMap doc;
	String rev;
	if(!LoadDoc(proposals, proposal_id, doc, rev))
		return Fail(404, "proposal not found");
	ValueArray evidence = EvidenceOf(doc);
	evidence.Add(entry);
	doc.Set("evidence", evidence);
	doc.Set("status", TF_STATUS_DRAFT);
	doc.Set("content_hash", "");
	doc.Set("messages", ValueArray());
	doc.Set("updated", AsString(GetSysTime()));
	String error;
	if(!SaveDoc(proposals, proposal_id, doc, rev, error))
		return Fail(500, "cannot save proposal: " + error);
	return Done(200, doc);
}

TfResult TaskFoundryService::Validate(const TfActor& a, const String& proposal_id)
{
	if(!HasScope(a, TF_SCOPE_WRITE))
		return Fail(403, "missing scope taskfoundry:write");

	Mutex::Lock hold(lock);
	ValueMap doc;
	String rev;
	if(!LoadDoc(proposals, proposal_id, doc, rev))
		return Fail(404, "proposal not found");

	ValueArray messages;
	if(TrimBoth(Text(doc, "title")).IsEmpty())
		messages.Add("title is required");
	if(TrimBoth(Text(doc, "body")).IsEmpty())
		messages.Add("body is required");
	if(Text(doc, "repository").IsEmpty())
		messages.Add("repository is required");
	if((double)doc.Get("estimated_hours", 0.0) <= 0)
		messages.Add("estimated workload (hours) is required");
	if(EvidenceOf(doc).GetCount() == 0)
		messages.Add("at least one evidence reference is required");

	bool valid = messages.GetCount() == 0;
	String hash = TaskFoundryContentHash(doc);
	doc.Set("status", valid ? TF_STATUS_VALIDATED : TF_STATUS_DRAFT);
	doc.Set("content_hash", valid ? hash : String());
	doc.Set("messages", messages);
	doc.Set("updated", AsString(GetSysTime()));
	String error;
	if(!SaveDoc(proposals, proposal_id, doc, rev, error))
		return Fail(500, "cannot save proposal: " + error);

	ValueMap out;
	out.Add("valid", valid);
	out.Add("messages", messages);
	out.Add("content_hash", valid ? hash : String());
	return Done(200, out);
}

TfResult TaskFoundryService::Submit(const TfActor& a, const String& proposal_id)
{
	if(!HasScope(a, TF_SCOPE_WRITE))
		return Fail(403, "missing scope taskfoundry:write");
	if(!HasScope(a, TF_SCOPE_COMPANY_WRITE))
		return Fail(403, "missing scope proposals:write");

	Mutex::Lock hold(lock);
	ValueMap doc;
	String rev;
	if(!LoadDoc(proposals, proposal_id, doc, rev))
		return Fail(404, "proposal not found");

	String hash = TaskFoundryContentHash(doc);
	// The submission id is per proposal and content: a repeat submit of the
	// same content returns the prior submission and creates nothing new.
	String sub_id = "sub_" + SHA256String(proposal_id + "\x1f" + hash).Left(24);
	ValueMap prior;
	String prior_rev;
	if(LoadDoc(submissions, sub_id, prior, prior_rev)) {
		ValueMap out = prior;
		out.Set("duplicate", true);
		return Done(200, out);
	}

	if(Text(doc, "status") != TF_STATUS_VALIDATED || Text(doc, "content_hash") != hash)
		return Fail(409, "validate the proposal before submitting it");

	ValueMap snapshot;
	snapshot.Add("title", Text(doc, "title"));
	snapshot.Add("body", Text(doc, "body"));
	snapshot.Add("repository", Text(doc, "repository"));
	snapshot.Add("estimated_hours", (double)doc.Get("estimated_hours", 0.0));
	snapshot.Add("evidence", EvidenceOf(doc));

	// Company owns the review. Its own idempotency by content makes a retry
	// name the same proposal; the key here is the content hash, as briefed.
	ValueMap company;
	company.Add("title", Text(snapshot, "title"));
	company.Add("description", Text(snapshot, "body"));
	company.Add("repository", Text(snapshot, "repository"));
	company.Add("estimated_hours", (double)snapshot.Get("estimated_hours", 0.0));
	BrokerResult made = Ask(a, "company", "create-proposal-draft", company, hash);
	if(!made.ok)
		return Fail(made.status, made.error);

	ValueMap record;
	record.Add("id", sub_id);
	record.Add("proposal_id", proposal_id);
	record.Add("content_hash", hash);
	record.Add("snapshot", snapshot);
	record.Add("company_proposal_id", Text(made.payload, "proposal_id"));
	record.Add("submitted_by", a.user_id);
	record.Add("submitted", AsString(GetSysTime()));
	String error;
	if(!SaveDoc(submissions, sub_id, record, "", error))
		return Fail(500, "cannot save submission: " + error);

	doc.Set("status", TF_STATUS_SUBMITTED);
	doc.Set("submission_id", sub_id);
	doc.Set("updated", AsString(GetSysTime()));
	if(!SaveDoc(proposals, proposal_id, doc, rev, error))
		return Fail(500, "cannot save proposal: " + error);

	record.Add("duplicate", false);
	return Done(201, record);
}

TfResult TaskFoundryService::Status(const TfActor& a, const String& submission_id)
{
	if(!HasScope(a, TF_SCOPE_COMPANY_READ))
		return Fail(403, "missing scope proposals:read");

	ValueMap record;
	String rev;
	if(!LoadDoc(submissions, submission_id, record, rev))
		return Fail(404, "submission not found");

	ValueMap ask;
	ask.Add("proposal_id", Text(record, "company_proposal_id"));
	BrokerResult company = Ask(a, "company", "get-proposal-status", ask);
	if(!company.ok)
		return Fail(company.status, company.error);

	ValueMap out = record;
	out.Set("company_status", Text(company.payload, "status"));
	return Done(200, out);
}

TfResult TaskFoundryService::Board()
{
	Mutex::Lock hold(lock);
	ValueArray cands;
	Vector<String> ids;
	candidates.List(ids);
	for(const String& id : ids) {
		ValueMap doc;
		String rev;
		if(LoadDoc(candidates, id, doc, rev)) {
			doc.Set("id", id);
			cands.Add(doc);
		}
	}
	ValueArray props;
	ids.Clear();
	proposals.List(ids);
	for(const String& id : ids) {
		ValueMap doc;
		String rev;
		if(LoadDoc(proposals, id, doc, rev)) {
			doc.Set("id", id);
			props.Add(doc);
		}
	}
	ValueMap out;
	out.Add("candidates", cands);
	out.Add("proposals", props);
	return Done(200, out);
}

TfResult TaskFoundryService::GetCandidate(const String& candidate_id)
{
	ValueMap doc;
	String rev;
	if(!LoadDoc(candidates, candidate_id, doc, rev))
		return Fail(404, "candidate not found");
	doc.Set("id", candidate_id);
	return Done(200, doc);
}

TfResult TaskFoundryService::GetProposal(const String& proposal_id)
{
	ValueMap doc;
	String rev;
	if(!LoadDoc(proposals, proposal_id, doc, rev))
		return Fail(404, "proposal not found");
	doc.Set("id", proposal_id);
	return Done(200, doc);
}

END_UPP_NAMESPACE
