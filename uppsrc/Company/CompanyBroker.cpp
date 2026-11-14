#include "CompanyBroker.h"

NAMESPACE_UPP

static CompanyBrokerResult Refuse(int status, const String& error)
{
	CompanyBrokerResult r;
	r.status = status;
	r.error = error;
	return r;
}

static bool HasScope(const Vector<String>& scopes, const char *scope)
{
	for(const String& s : scopes)
		if(s == scope)
			return true;
	return false;
}

String CompanyBroker::ContentHash(const String& title, const String& description,
                                  const String& repository, double estimated_hours)
{
	// Unit separator between fields keeps "ab"+"c" and "a"+"bc" distinct.
	String canonical = TrimBoth(title) + "\x1f" + description + "\x1f" + repository
	                 + "\x1f" + Format("%.6f", estimated_hours);
	return SHA256String(canonical); // lowercase hex text
}

CompanyBrokerResult CompanyBroker::Handle(const String& op, const Vector<String>& scopes,
                                          const String& actor_user_id, const ValueMap& payload)
{
	if(op == COMPANY_BROKER_CREATE_DRAFT)
		return CreateDraft(scopes, actor_user_id, payload);
	if(op == COMPANY_BROKER_STATUS)
		return GetStatus(scopes, payload);
	if(op == COMPANY_BROKER_LEDGER_SUMMARY)
		return LedgerSummary(scopes, payload);
	if(op == COMPANY_BROKER_LEDGER_RECENT)
		return LedgerRecent(scopes, payload);
	if(op == COMPANY_BROKER_LEDGER_BALANCES)
		return LedgerBalances(scopes, payload);
	if(op == COMPANY_BROKER_CONTRACTS_LIST)
		return ContractsList(scopes, payload);
	return Refuse(404, "unknown broker operation: " + op);
}

CompanyBrokerResult CompanyBroker::CreateDraft(const Vector<String>& scopes, const String& actor_user_id,
                                               const ValueMap& payload)
{
	if(!HasScope(scopes, COMPANY_SCOPE_PROPOSALS_WRITE))
		return Refuse(403, "missing scope proposals:write");

	String title = TrimBoth((String)payload.Get("title", ""));
	String description = (String)payload.Get("description", "");
	String repository = (String)payload.Get("repository", "");
	double hours = (double)payload.Get("estimated_hours", 0.0);

	// Same validation as the human proposal path: allowlisted repository,
	// positive workload, and the policy maximum.
	if(title.IsEmpty())
		return Refuse(400, "title is required");
	if(!store.IsRepositoryAllowed(repository))
		return Refuse(400, "repository is not on the allowlist: " + repository);
	if(hours <= 0)
		return Refuse(400, "estimated workload (hours) is required");
	const CompanyConfig& cfg = store.GetConfig();
	int max_hours = cfg.reward_policy.Find("max_hours") >= 0 ? (int)cfg.reward_policy["max_hours"] : 40;
	if(max_hours > 0 && hours > max_hours)
		return Refuse(400, "estimated workload exceeds the policy maximum " + AsString(max_hours) + " h");

	String hash = ContentHash(title, description, repository, hours);

	ProposalRecord p;
	p.id = "prp_" + hash.Left(24);
	p.author_user_id = actor_user_id;
	p.author_type = COMPANY_ACTOR_AUTOMATION;
	p.title = title;
	p.description = description;
	p.repository = repository;
	p.estimated_hours = hours;
	p.status = COMPANY_PROPOSAL_PROPOSED;
	p.submission_hash = hash;
	p.submission_snapshot.Add("title", title);
	p.submission_snapshot.Add("description", description);
	p.submission_snapshot.Add("repository", repository);
	p.submission_snapshot.Add("estimated_hours", hours);
	p.created_at = GetSysTime();
	p.updated_at = p.created_at;

	bool created = false;
	ProposalRecord existing;
	String error;
	if(!store.CreateProposalOnce(p, created, existing, error))
		return Refuse(500, "cannot save proposal: " + error);

	const ProposalRecord& kept = created ? p : existing;
	CompanyBrokerResult r;
	r.ok = true;
	r.status = created ? 201 : 200;
	r.payload.Add("proposal_id", kept.id);
	r.payload.Add("submission_hash", kept.submission_hash);
	r.payload.Add("duplicate", !created);
	return r;
}

CompanyBrokerResult CompanyBroker::GetStatus(const Vector<String>& scopes, const ValueMap& payload)
{
	if(!HasScope(scopes, COMPANY_SCOPE_PROPOSALS_READ))
		return Refuse(403, "missing scope proposals:read");

	ProposalRecord p;
	if(!store.FindProposal((String)payload.Get("proposal_id", ""), p))
		return Refuse(404, "proposal not found");

	CompanyBrokerResult r;
	r.ok = true;
	r.status = 200;
	r.payload.Add("proposal_id", p.id);
	r.payload.Add("status", p.status);
	r.payload.Add("repository", p.repository);
	r.payload.Add("submission_hash", p.submission_hash);
	r.payload.Add("snapshot", p.submission_snapshot);
	return r;
}

// ---------------------------------------------------------------------------
// Read-only financial surface (task 016). The ledger is append-only and its
// writes stay internal to Company; these ops only read. A subject filter and
// the per-user identifiers need the admin scope; plain ledger:read callers
// get aggregates and rows without user ids.
// ---------------------------------------------------------------------------

CompanyBrokerResult CompanyBroker::LedgerSummary(const Vector<String>& scopes, const ValueMap& payload)
{
	if(!HasScope(scopes, COMPANY_SCOPE_LEDGER_READ))
		return Refuse(403, "missing scope ledger:read");

	Vector<LedgerEntry> entries = store.GetLedger();
	long credit = 0, debit = 0, correction = 0;
	Time newest;
	bool any = false;
	for(const LedgerEntry& e : entries) {
		if(e.type == "credit")
			credit += e.amount;
		else if(e.type == "debit")
			debit += e.amount;
		else if(e.type == "correction")
			correction += e.amount;
		if(!any || e.created_at > newest) {
			newest = e.created_at;
			any = true;
		}
	}
	CompanyBrokerResult r;
	r.ok = true;
	r.status = 200;
	r.payload.Add("credit_total", (int)credit);
	r.payload.Add("debit_total", (int)debit);
	r.payload.Add("correction_total", (int)correction);
	r.payload.Add("entry_count", entries.GetCount());
	r.payload.Add("newest_entry_at", any ? AsString(newest) : String());
	return r;
}

CompanyBrokerResult CompanyBroker::LedgerRecent(const Vector<String>& scopes, const ValueMap& payload)
{
	if(!HasScope(scopes, COMPANY_SCOPE_LEDGER_READ))
		return Refuse(403, "missing scope ledger:read");
	bool admin = HasScope(scopes, COMPANY_SCOPE_ADMIN);
	String subject;
	{
		Value sv = payload.Get("subject_user_id", "");
		if(sv.Is<String>())
			subject = (String)sv;
	}
	if(subject.GetCount() > 0 && !admin)
		return Refuse(403, "a subject filter needs the admin scope");
	int limit = 20;
	{
		Value lim = payload.Get("limit", "");
		String want;
		if(lim.Is<int>() || lim.Is<int64>())
			want = AsString(lim);
		else if(lim.Is<String>())
			want = (String)lim;
		if(!want.IsEmpty()) {
			int n = ScanInt(want);
			if(n < 1)
				n = 1;
			if(n > 50)
				n = 50;
			limit = n;
		}
	}

	Vector<LedgerEntry> entries = store.GetLedger();
	Vector<int> order;
	for(int i = 0; i < entries.GetCount(); i++) {
		if(subject.GetCount() && entries[i].user_id != subject)
			continue;
		order.Add(i);
	}
	Sort(order, [&](int a, int b) { return entries[a].created_at > entries[b].created_at; });
	if(order.GetCount() > limit)
		order.Remove(limit, order.GetCount() - limit);

	ValueArray rows;
	for(int i : order) {
		const LedgerEntry& e = entries[i];
		ValueMap row;
		row.Add("seq", e.seq);
		row.Add("type", e.type);
		row.Add("amount", e.amount);
		if(admin)
			row.Add("user_id", e.user_id);
		row.Add("reason", e.reason);
		row.Add("created_at", AsString(e.created_at));
		rows.Add(row);
	}
	CompanyBrokerResult r;
	r.ok = true;
	r.status = 200;
	r.payload.Add("entries", rows);
	r.payload.Add("total", entries.GetCount());
	return r;
}

CompanyBrokerResult CompanyBroker::LedgerBalances(const Vector<String>& scopes, const ValueMap& payload)
{
	if(!HasScope(scopes, COMPANY_SCOPE_ADMIN))
		return Refuse(403, "missing scope admin");

	Vector<LedgerEntry> entries = store.GetLedger();
	Index<String> users;
	Vector<int> balances;
	for(const LedgerEntry& e : entries) {
		int i = users.Find(e.user_id);
		if(i < 0) {
			users.Add(e.user_id);
			balances.Add(e.amount);
		}
		else
			balances[i] += e.amount;
	}
	ValueArray rows;
	for(int i = 0; i < users.GetCount(); i++) {
		ValueMap row;
		row.Add("user_id", users[i]);
		row.Add("balance", balances[i]);
		rows.Add(row);
	}
	CompanyBrokerResult r;
	r.ok = true;
	r.status = 200;
	r.payload.Add("balances", rows);
	return r;
}

CompanyBrokerResult CompanyBroker::ContractsList(const Vector<String>& scopes, const ValueMap& payload)
{
	if(!HasScope(scopes, COMPANY_SCOPE_ADMIN))
		return Refuse(403, "missing scope admin");

	Vector<ContractRecord> contracts = store.GetContracts();
	ValueArray rows;
	for(const ContractRecord& c : contracts) {
		ValueMap row;
		row.Add("id", c.id);
		row.Add("title", c.title);
		row.Add("status", c.status);
		row.Add("currency", c.currency);
		row.Add("amount", c.amount);
		row.Add("client_user_id", c.client_user_id);
		rows.Add(row);
	}
	CompanyBrokerResult r;
	r.ok = true;
	r.status = 200;
	r.payload.Add("contracts", rows);
	return r;
}

END_UPP_NAMESPACE
