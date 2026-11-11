#ifndef _MetaAccount_MetaAccountSite_h_
#define _MetaAccount_MetaAccountSite_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include <WebAccounts/WebAccounts.h>
#include <WebAccounts/WebAccountsGrants.h>
#include "MetaInbox.h"

NAMESPACE_UPP

// Installs the meta-account webapp routes on a host-created EngineSite.
// The site's SiteAccounts owns identities; WebGrants owns consent state;
// both are rooted at the site's data root.
void InstallMetaAccountSite(EngineSite& site, WebGrants& grants);

// Broker service for the meta site. Construct with the site's accounts and
// grants; register under the meta site id via RegisterMetaAccountService.
class MetaAccountService : public IBrokerService {
public:
	MetaAccountService(SiteAccounts& accounts, WebGrants& grants)
		: accounts(accounts), grants(grants) {}

	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override;

	static bool SupportedOp(const String& op) {
		return op == "ping" || op == "authenticate" || op == "request-consent"
		    || op == "check-grant" || op == "revoke-grant" || op == "notify";
	}

private:
	SiteAccounts& accounts;
	WebGrants&    grants;
};

void RegisterMetaAccountService(const String& meta_site_id, SiteAccounts& accounts, WebGrants& grants);

END_UPP_NAMESPACE

#endif
