#ifndef _WebBroker_WebBroker_h_
#define _WebBroker_WebBroker_h_

#include <Core/Core.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// WebBroker: transport-independent communication between fenced websites.
//
// Cross-site access ALWAYS goes through here -- never direct object or store
// sharing. The in-process transport enforces the same value-boundary and
// observability rules the future TCP transport will; a remote call currently
// fails with an explicit "not implemented" error.
// ---------------------------------------------------------------------------

struct BrokerCall {
	String   op;
	ValueMap payload;
	String   idempotency_key;
	int      deadline_ms = 30000;

	void Jsonize(JsonIO& io) {
		io("op", op)("payload", payload)("idempotency_key", idempotency_key)
		  ("deadline_ms", deadline_ms);
	}
};

struct BrokerCallContext : Moveable<BrokerCallContext> {
	String from_site;
	String to_site;
	String caller_id;   // authenticated principal at the calling site ("" = guest)
	Vector<String> scopes;
	String correlation_id;

	BrokerCallContext() {}
	BrokerCallContext(const BrokerCallContext& o)
		: from_site(o.from_site), to_site(o.to_site), caller_id(o.caller_id)
		, scopes(clone(o.scopes)), correlation_id(o.correlation_id) {}
	BrokerCallContext& operator=(const BrokerCallContext& o) {
		if(this != &o) {
			from_site = o.from_site;
			to_site = o.to_site;
			caller_id = o.caller_id;
			scopes = clone(o.scopes);
			correlation_id = o.correlation_id;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("from_site", from_site)("to_site", to_site)("caller_id", caller_id)
		  ("correlation_id", correlation_id)("scopes", scopes);
	}
};

struct BrokerResult {
	bool     ok = false;
	int      status = 500;
	ValueMap payload;
	String   error;

	void Jsonize(JsonIO& io) {
		io("ok", ok)("status", status)("payload", payload)("error", error);
	}
};

class IBrokerService {
public:
	virtual ~IBrokerService() {}
	virtual BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) = 0;
};

struct BrokerDiagEntry : Moveable<BrokerDiagEntry> {
	Time   at;
	String correlation_id;
	String from_site;
	String to_site;
	String op;
	String caller_id;
	bool   ok = false;
	int    ms = 0;
	String error;

	void Jsonize(JsonIO& io) {
		io("at", at)
		  ("correlation_id", correlation_id)
		  ("from_site", from_site)("to_site", to_site)("op", op)("caller_id", caller_id)
		  ("ok", ok)("ms", ms)("error", error);
	}
};

class WebBroker {
public:
	// Takes ownership of the service for the target site.
	void Register(const String& to_site, IBrokerService* service);

	// Synchronous in-process call. Copies values across the boundary, records
	// diagnostics, and generates a correlation id when the caller did not.
	BrokerResult Call(const BrokerCallContext& ctx, const BrokerCall& call);

	// Remote (separate process/computer) transport: explicit stub.
	BrokerResult CallRemote(const BrokerCallContext& ctx, const BrokerCall& call);

	// Diagnostics ledger (bounded ring buffer).
	Vector<BrokerDiagEntry>& Diagnostics() { return diag; }
	String DiagnosticsJSON(int max_entries = 100);
	void   ClearDiagnostics();

	static const char* TCP_NOT_IMPLEMENTED() { return "tcp broker transport is not implemented yet"; }

private:
	Vector<BrokerDiagEntry> diag;
	StaticMutex mutex;
	VectorMap<String, One<IBrokerService> > services;
	static const int DIAG_MAX = 1000;
};

END_UPP_NAMESPACE

#endif
