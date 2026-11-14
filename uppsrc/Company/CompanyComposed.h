#ifndef _Company_CompanyComposed_h_
#define _Company_CompanyComposed_h_

#include <Core/Core.h>
#include "CompanyStore.h"
#include "CompanyBroker.h"

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// The Company module as a host composes it. Host-compose direction only: this
// package never depends on a host application. The host opens it with its own
// data root, installs the web site over Store(), and calls Handle() for the
// broker operations.
// ---------------------------------------------------------------------------

#define COMPANY_COMPOSED_ID  "company"

class CompanyComposedModule {
public:
	CompanyComposedModule() : broker(store) {}

	// Opens the store under root and sets the repository allowlist. An empty
	// repositories list permits nothing (fail closed).
	bool Open(const String& root, const Vector<String>& repositories, String& error);

	// The store behind the module, for the host to install the web site over.
	CompanyStore& Store() { return store; }

	// Runs one broker operation. scopes and caller_id are the caller's, as the
	// broker supplies them.
	CompanyBrokerResult Handle(const String& op, const Vector<String>& scopes,
	                           const String& caller_id, const ValueMap& payload);

private:
	CompanyStore  store;
	CompanyBroker broker;
};

END_UPP_NAMESPACE

#endif
