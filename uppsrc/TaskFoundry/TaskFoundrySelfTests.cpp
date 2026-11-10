#include "TaskFoundryService.h"

NAMESPACE_UPP

// Synthetic checks for the TaskFoundry service (brief 11B step 3). Science,
// PrivateHub and Company are contract stand-ins registered on a local broker:
// they check the scopes the real services check and return the fields the real
// services return. The real composed wiring is exercised by the step 4 fence.

static bool Check(bool cond, const String& what)
{
	if(cond)
		Cout() << "ok    " << what << "\n";
	else
		Cout() << "FAILED: " << what << "\n";
	return cond;
}

static bool HasScopeIn(const BrokerCallContext& ctx, const char *scope)
{
	for(const String& s : ctx.scopes)
		if(s == scope)
			return true;
	return false;
}

static BrokerResult Refuse(int status, const String& error)
{
	BrokerResult r;
	r.ok = false;
	r.status = status;
	r.error = error;
	return r;
}

static BrokerResult Accept(const ValueMap& payload)
{
	BrokerResult r;
	r.ok = true;
	r.status = 200;
	r.payload = payload;
	return r;
}

class StandInScience : public IBrokerService {
public:
	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override {
		if(!HasScopeIn(ctx, "science.read"))
			return Refuse(403, "missing scope science.read");
		String id = (String)call.payload.Get("id", "");
		if(id != "2612.00002")
			return Refuse(404, "paper not found");
		if(call.op == "get-paper") {
			ValueMap out;
			out.Add("id", id);
			out.Add("title", "Stand-in paper");
			out.Add("revision", "3");
			return Accept(out);
		}
		if(call.op == "get-article") {
			ValueArray nodes;
			ValueMap s1;
			s1.Add("id", "s1");
			s1.Add("type", "section");
			nodes.Add(s1);
			ValueMap p1;
			p1.Add("id", "p1");
			p1.Add("type", "paragraph");
			nodes.Add(p1);
			ValueMap article;
			article.Add("title", "Stand-in article");
			article.Add("nodes", nodes);
			ValueMap out;
			out.Add("article", article);
			return Accept(out);
		}
		return Refuse(404, "unknown operation: " + call.op);
	}
};

class StandInPrivateHub : public IBrokerService {
public:
	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override {
		if(!HasScopeIn(ctx, "privatehub:read"))
			return Refuse(403, "missing scope privatehub:read");
		if(call.op != "repo.info")
			return Refuse(404, "unknown operation: " + call.op);
		if((String)call.payload.Get("project", "") != "OuluBSD/CompanySkylark")
			return Refuse(404, "no local checkout");
		ValueMap out;
		out.Add("project", "OuluBSD/CompanySkylark");
		out.Add("branch", "main");
		out.Add("head_commit", "abc123");
		return Accept(out);
	}
};

// Keys a draft by its content, as Company does, so a retry names the same id.
class StandInCompany : public IBrokerService {
public:
	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override {
		if(call.op == "create-proposal-draft") {
			if(!HasScopeIn(ctx, "proposals:write"))
				return Refuse(403, "missing scope proposals:write");
			String key = (String)call.payload.Get("title", "") + "\x1f" + (String)call.payload.Get("repository", "") +
			             "\x1f" + Format("%.6f", (double)call.payload.Get("estimated_hours", 0.0));
			String id = "prp_" + SHA256String(key).Left(24);
			if(ids.Find(key) < 0) {
				ids.Add(key, id);
				status_of.Add(id, "proposed");
			}
			ValueMap out;
			out.Add("proposal_id", ids.Get(key));
			return Accept(out);
		}
		if(call.op == "get-proposal-status") {
			if(!HasScopeIn(ctx, "proposals:read"))
				return Refuse(403, "missing scope proposals:read");
			String id = (String)call.payload.Get("proposal_id", "");
			if(status_of.Find(id) < 0)
				return Refuse(404, "proposal not found");
			ValueMap out;
			out.Add("status", status_of.Get(id));
			return Accept(out);
		}
		return Refuse(404, "unknown operation: " + call.op);
	}

private:
	VectorMap<String, String> ids;
	VectorMap<String, String> status_of;
};

bool RunTaskFoundrySelfTests(const String& scratch_dir)
{
	String root = AppendFileName(scratch_dir, "taskfoundry");
	DeleteFolderDeep(~root);
	RealizeDirectory(root);

	WebBroker broker;
	broker.Register("science", new StandInScience);
	broker.Register("privatehub", new StandInPrivateHub);
	broker.Register("company", new StandInCompany);
	TaskFoundryService svc(broker, root);

	// Actors are built from scope lists: TfActor is pick-only, so it is not copied.
	auto make = [](const Vector<String>& scopes) {
		TfActor a;
		a.user_id = "user-alice";
		a.username = "alice";
		a.scopes = clone(scopes);
		return a;
	};
	// Scopes come from an approved grant (the site applies it), so the checks
	// below use the grantable set as a granted set.
	Vector<String> member = TaskFoundryGrantableScopes();
	TfActor alice = make(member);

	Vector<String> reader_scopes;
	reader_scopes.Add(TF_SCOPE_SCIENCE_READ);
	TfActor reader = make(reader_scopes);   // no write scope

	bool all = true;

	// A signed-in user with no approved grant holds no scopes at all.
	TfActor ungranted = make(Vector<String>());
	all &= Check(svc.CreateCandidate(ungranted, "Idea", "x", "2612.00002").status == 403,
	             "taskfoundry: a user without an approved grant cannot create a candidate");

	// science.author is never grantable, whatever a grant asks for.
	Vector<String> asked;
	asked.Add(TF_SCOPE_SCIENCE_READ);
	asked.Add("science.author");
	asked.Add(TF_SCOPE_WRITE);
	Vector<String> kept = TaskFoundryScopesOf(asked);
	all &= Check(kept.GetCount() == 2 && FindIndex(kept, String("science.author")) < 0,
	             "taskfoundry: a grant cannot carry science.author");

	Vector<String> no_company_scopes;   // can edit and validate, cannot submit
	for(const String& s : member)
		if(s != TF_SCOPE_COMPANY_WRITE)
			no_company_scopes.Add(s);
	TfActor no_company = make(no_company_scopes);
	all &= Check(svc.CreateCandidate(reader, "Idea", "x", "2612.00002").status == 403 &&
	             !svc.CreateCandidate(reader, "Idea", "x", "2612.00002").ok,
	             "taskfoundry: a candidate needs taskfoundry:write (403 without it)");

	TfResult cand = svc.CreateCandidate(alice, "Idea", "Summary", "2612.00002");
	all &= Check(cand.ok && cand.status == 201, "taskfoundry: candidate from a known paper is created");
	String cand_id = (String)cand.payload.Get("candidate_id", "");
	all &= Check(svc.CreateCandidate(alice, "Idea", "x", "9999.99999").status == 400,
	             "taskfoundry: unknown paper is refused (400)");
	all &= Check(svc.CreateCandidate(alice, "  ", "x", "2612.00002").status == 400,
	             "taskfoundry: blank candidate title is refused (400)");

	TfResult prop = svc.CreateProposal(alice, cand_id);
	all &= Check(prop.ok && prop.status == 201, "taskfoundry: proposal starts from the candidate");
	String pid = (String)prop.payload.Get("proposal_id", "");
	all &= Check(svc.CreateProposal(alice, "cand_absent").status == 404,
	             "taskfoundry: unknown candidate is 404");

	TfResult empty = svc.Validate(alice, pid);
	all &= Check(empty.ok && !(bool)empty.payload.Get("valid", true),
	             "taskfoundry: an empty proposal does not validate");
	all &= Check((String)svc.GetProposal(pid).payload.Get("status", "") == TF_STATUS_DRAFT,
	             "taskfoundry: a failed validation leaves the proposal a draft");

	all &= Check(svc.AttachEvidence(alice, pid, "science", "2612.00002", "missing-node").status == 400,
	             "taskfoundry: evidence with a node not in the article is refused");
	all &= Check(svc.AttachEvidence(alice, pid, "science", "2612.00002", "p1").ok,
	             "taskfoundry: science evidence with a real node is attached");
	TfResult repo = svc.AttachEvidence(alice, pid, "privatehub", "OuluBSD/CompanySkylark", "");
	ValueArray repo_evidence = repo.payload.Get("evidence", ValueArray());
	ValueMap repo_entry = repo_evidence[repo_evidence.GetCount() - 1];
	all &= Check(repo.ok && (String)repo_entry.Get("branch", "") == "main" &&
	             (String)repo_entry.Get("head_commit", "") == "abc123",
	             "taskfoundry: privatehub evidence records the branch and head");
	all &= Check(svc.AttachEvidence(alice, pid, "wiki", "x", "").status == 400,
	             "taskfoundry: an unknown evidence kind is refused");

	all &= Check(svc.AttachEvidence(reader, pid, "science", "2612.00002", "p1").status == 403,
	             "taskfoundry: evidence needs taskfoundry:write (403 without it)");

	TfResult edit = svc.EditProposal(alice, pid, "Draft title", "Proposal body", "OuluBSD/CompanySkylark", 2.0);
	all &= Check(edit.ok, "taskfoundry: proposal fields can be edited");

	TfResult valid = svc.Validate(alice, pid);
	all &= Check(valid.ok && (bool)valid.payload.Get("valid", false) && !((String)valid.payload.Get("content_hash", "")).IsEmpty(),
	             "taskfoundry: a complete proposal validates with a content hash");

	all &= Check(svc.Submit(no_company, pid).status == 403,
	             "taskfoundry: submit needs proposals:write (403 without it)");

	TfResult sub = svc.Submit(alice, pid);
	all &= Check(sub.ok && sub.status == 201, "taskfoundry: submit creates a submission");
	String sub_id = (String)sub.payload.Get("id", "");
	String company_id = (String)sub.payload.Get("company_proposal_id", "");
	all &= Check(company_id.StartsWith("prp_"), "taskfoundry: submission records the Company proposal id");

	TfResult again = svc.Submit(alice, pid);
	all &= Check(again.ok && (String)again.payload.Get("id", "") == sub_id &&
	             (String)again.payload.Get("company_proposal_id", "") == company_id &&
	             (bool)again.payload.Get("duplicate", false),
	             "taskfoundry: a repeat submit returns the same submission and Company id");

	svc.EditProposal(alice, pid, "Edited title", "Edited body", "OuluBSD/CompanySkylark", 2.0);
	all &= Check((String)svc.GetProposal(pid).payload.Get("status", "") == TF_STATUS_DRAFT,
	             "taskfoundry: an edit after submit makes the live proposal a draft");

	TfResult status = svc.Status(alice, sub_id);
	ValueMap snap = status.payload.Get("snapshot", ValueMap());
	all &= Check(status.ok && (String)snap.Get("title", "") == "Draft title" &&
	             (String)snap.Get("body", "") == "Proposal body",
	             "taskfoundry: the submitted snapshot is unchanged by a later edit");
	all &= Check((String)status.payload.Get("company_status", "") == "proposed",
	             "taskfoundry: status reads the Company status through the broker");

	all &= Check(svc.Submit(alice, pid).status == 409,
	             "taskfoundry: an edited proposal must be validated before it is submitted again");

	svc.Validate(alice, pid);
	TfResult second = svc.Submit(alice, pid);
	all &= Check(second.ok && (String)second.payload.Get("id", "") != sub_id,
	             "taskfoundry: edited content submits as a new submission, the old snapshot kept");
	TfResult first = svc.Status(alice, sub_id);
	ValueMap first_snap = first.payload.Get("snapshot", ValueMap());
	all &= Check(first.ok && (String)first_snap.Get("title", "") == "Draft title",
	             "taskfoundry: the first submission still holds its original snapshot");

	all &= Check(svc.Status(reader, sub_id).status == 403,
	             "taskfoundry: status needs proposals:read (403 without it)");
	all &= Check(svc.Status(alice, "sub_absent").status == 404,
	             "taskfoundry: unknown submission is 404");

	TfResult board = svc.Board();
	all &= Check(board.ok && ((ValueArray)board.payload.Get("candidates", ValueArray())).GetCount() == 1 &&
	             ((ValueArray)board.payload.Get("proposals", ValueArray())).GetCount() == 1,
	             "taskfoundry: the board lists the candidate and the proposal");

	return all;
}

END_UPP_NAMESPACE
