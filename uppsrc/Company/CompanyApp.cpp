#include "CompanyApp.h"

NAMESPACE_UPP

// The Company module, as the broker sees it. The app owns the adapter's module; the
// module owns the store. The broker never reaches the store, only Handle().
class CompanyBrokerSite : public IBrokerService {
public:
	explicit CompanyBrokerSite(CompanyComposedModule& module) : module(module) {}
	String AuditOperation(const String& op) const override {
		if(op == COMPANY_BROKER_CREATE_DRAFT || op == COMPANY_BROKER_STATUS ||
		   op == COMPANY_BROKER_LEDGER_SUMMARY || op == COMPANY_BROKER_LEDGER_RECENT ||
		   op == COMPANY_BROKER_LEDGER_BALANCES || op == COMPANY_BROKER_CONTRACTS_LIST)
			return op;
		return "unclassified";
	}

	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override {
		CompanyBrokerResult r = module.Handle(call.op, ctx.scopes, ctx.caller_id, call.payload);
		BrokerResult out;
		out.ok = r.ok;
		out.status = r.status;
		out.payload = r.payload;
		out.error = r.error;
		return out;
	}

private:
	CompanyComposedModule& module;
};

bool CompanyApp::Install(EngineSite& site, WebAppContext& ctx, String& error)
{
	String store_root = ctx.Setting("store");
	if(store_root.IsEmpty()) {
		error = "company: no store setting";
		return false;
	}
	String root = ctx.SiteRoot(SiteDir());
	if(!DirectoryExists(root)) {
		error = "company site root is not a directory: " + root;
		return false;
	}
	Vector<String> repos;
	String repo_list = ctx.Setting("repos");
	if(!repo_list.IsEmpty())
		repos = Split(repo_list, ',', false);
	module.Create();
	if(!module->Open(store_root, repos, error))
		return false;
	InstallCompanySite(site, module->Store());
	return true;
}

bool CompanyApp::RegisterBroker(WebAppContext& ctx, String& error)
{
	if(!module) {
		error = "company broker: the module is not installed";
		return false;
	}
	ctx.broker->Register(Id(), new CompanyBrokerSite(*module));
	return true;
}

END_UPP_NAMESPACE
