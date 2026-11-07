#include "WebBroker.h"

NAMESPACE_UPP

void WebBroker::Register(const String& to_site, IBrokerService* service)
{
	ASSERT_(to_site.GetCount(), "broker service needs a target site");
	ASSERT_(service, "broker service must not be null");
	mutex.Enter();
	services.GetAdd(to_site) = service;
	mutex.Leave();
}

BrokerResult WebBroker::Call(const BrokerCallContext& ctx, const BrokerCall& call)
{
	BrokerCallContext c = ctx;
	if(c.correlation_id.IsEmpty())
		c.correlation_id = AsString(Uuid::Create());
	BrokerResult r;
	uint64 t0 = msecs();
	IBrokerService* svc = NULL;
	mutex.Enter();
	int q = services.Find(c.to_site);
	if(q >= 0)
		svc = &*services[q];
	mutex.Leave();
	if(!svc) {
		r.ok = false;
		r.status = 404;
		r.error = "no broker service registered for site '" + c.to_site + "'";
	}
	else {
		// Value-boundary: the service receives copies of the payload values.
		BrokerCall copy = call;
		r = svc->Handle(c, copy);
	}
	int ms = (int)(msecs() - t0);

	BrokerDiagEntry& e = diag.Add();
	e.at = GetSysTime();
	e.correlation_id = c.correlation_id;
	e.from_site = c.from_site;
	e.to_site = c.to_site;
	e.op = call.op;
	e.caller_id = c.caller_id;
	e.ok = r.ok;
	e.ms = ms;
	e.error = r.error;
	mutex.Enter();
	while(diag.GetCount() > DIAG_MAX)
		diag.Remove(0);
	mutex.Leave();
	return r;
}

BrokerResult WebBroker::CallRemote(const BrokerCallContext& ctx, const BrokerCall& call)
{
	BrokerResult r;
	r.ok = false;
	r.status = 501;
	r.error = TCP_NOT_IMPLEMENTED();

	BrokerDiagEntry& e = diag.Add();
	e.at = GetSysTime();
	e.correlation_id = ctx.correlation_id.GetCount() ? ctx.correlation_id : AsString(Uuid::Create());
	e.from_site = ctx.from_site;
	e.to_site = ctx.to_site;
	e.op = call.op;
	e.caller_id = ctx.caller_id;
	e.ok = false;
	e.ms = 0;
	e.error = r.error;
	mutex.Enter();
	while(diag.GetCount() > DIAG_MAX)
		diag.Remove(0);
	mutex.Leave();
	return r;
}

String WebBroker::DiagnosticsJSON(int max_entries)
{
	ValueArray entries;
	mutex.Enter();
	int from = max(diag.GetCount() - max_entries, 0);
	for(int i = from; i < diag.GetCount(); i++)
		entries.Add(ParseJSON(StoreAsJson(diag[i])));
	mutex.Leave();
	ValueMap out;
	out.Add("entries", entries);
	return AsJSON(out, true);
}

void WebBroker::ClearDiagnostics()
{
	mutex.Enter();
	diag.Clear();
	mutex.Leave();
}

END_UPP_NAMESPACE
