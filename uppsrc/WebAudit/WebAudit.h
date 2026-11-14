#ifndef _WebAudit_WebAudit_h_
#define _WebAudit_WebAudit_h_

#include <Core/Core.h>
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// The durable audit schema intentionally has no fields for request payloads,
// URLs, headers, addresses, user agents, exception text, or bearer credentials.
struct WebAuditEvent : Moveable<WebAuditEvent> {
	int schema_version = 1;
	Time at;
	String site;
	String kind;
	String route;
	String method;
	String outcome;
	String actor_user_id;
	String session_ref;
	String correlation_id;
	String source_site;
	String target_site;
	String operation;
	int status = 0;
	int duration_ms = 0;

	bool Validate(String& error) const;
	ValueMap ToValue() const;
};

// A single site-scoped store. Retention and hard capacity are enforced on
// every append; writes fail closed and callers must not let failures affect HTTP.
class WebAuditLog {
public:
	enum { MAX_EVENTS = 10000, RETENTION_DAYS = 30 };

	WebAuditLog(const String& site_id, const String& data_root, int capacity = MAX_EVENTS);
	StoreResult Append(const WebAuditEvent& event);
	void Record(const WebAuditEvent& event); // best-effort; never throws into request handling
	StoreResult List(Time from, Time to, int offset, int limit, Vector<ValueMap>& out);
	bool Healthy() const;

private:
	String site;
	String root;
	mutable Mutex mutex;
	bool healthy = true;
	int capacity;
};

// Canonical route labels contain registered patterns only. Dynamic values,
// query strings and fragments are never accepted as labels.
String WebAuditRouteLabel(const String& method, const String& registered_pattern);
String WebAuditSessionRef();

END_UPP_NAMESPACE

#endif
