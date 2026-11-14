#ifndef _Company_CompanyBroker_h_
#define _Company_CompanyBroker_h_

#include <Core/Core.h>
#include <Company/CompanyStore.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// Company broker operations: the surface a composed caller (TaskFoundry, via
// the host's broker) uses to hand Company a draft and read its status. The
// operations are plain functions here; a host adapts them to its transport.
// Standalone CompanySkylark does not expose them.
//
// Deny by default: create needs proposals:write and status needs
// proposals:read in the caller's scopes. Review and compensation stay
// internal to Company; a draft is never decided or paid by this surface.
// ---------------------------------------------------------------------------

#define COMPANY_BROKER_CREATE_DRAFT  "create-proposal-draft"
#define COMPANY_BROKER_STATUS        "get-proposal-status"

// Read-only financial surface (task 016). The ledger is append-only and its
// writes stay internal to Company; these ops only read it.
#define COMPANY_BROKER_LEDGER_SUMMARY "ledger-summary"
#define COMPANY_BROKER_LEDGER_RECENT  "ledger-recent"
#define COMPANY_BROKER_LEDGER_BALANCES "ledger-balances"
#define COMPANY_BROKER_CONTRACTS_LIST "contracts-list"

struct CompanyBrokerResult : Moveable<CompanyBrokerResult> {
	bool     ok = false;
	int      status = 500;
	String   error;
	ValueMap payload;
};

class CompanyBroker {
public:
	explicit CompanyBroker(CompanyStore& store) : store(store) {}

	// Dispatches one operation. payload carries the operation's fields;
	// actor_user_id is the authenticated caller at the calling site.
	CompanyBrokerResult Handle(const String& op, const Vector<String>& scopes,
	                           const String& actor_user_id, const ValueMap& payload);

	// Content hash of a draft: the same content always hashes the same, and
	// the proposal id is derived from it, so a retry names the same proposal.
	static String ContentHash(const String& title, const String& description,
	                          const String& repository, double estimated_hours);

private:
	CompanyBrokerResult CreateDraft(const Vector<String>& scopes, const String& actor_user_id,
	                                const ValueMap& payload);
	CompanyBrokerResult GetStatus(const Vector<String>& scopes, const ValueMap& payload);
	CompanyBrokerResult LedgerSummary(const Vector<String>& scopes, const ValueMap& payload);
	CompanyBrokerResult LedgerRecent(const Vector<String>& scopes, const ValueMap& payload);
	CompanyBrokerResult LedgerBalances(const Vector<String>& scopes, const ValueMap& payload);
	CompanyBrokerResult ContractsList(const Vector<String>& scopes, const ValueMap& payload);

	CompanyStore& store;
};

END_UPP_NAMESPACE

#endif
