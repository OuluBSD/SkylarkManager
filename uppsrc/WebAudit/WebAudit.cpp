#include "WebAudit.h"

NAMESPACE_UPP

static bool SafeToken(const String& s, int max_len)
{
	if(s.IsEmpty() || s.GetCount() > max_len)
		return false;
	for(int i = 0; i < s.GetCount(); i++) {
		byte c = s[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		     (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '/' || c == '*' || c == ':' || c == '{' || c == '}' || c == '.'))
			return false;
	}
	return true;
}

static bool IsSafeStoredEvent(const ValueMap& doc, const String& site)
{
	static const char *fields[] = {
		"schema_version", "at", "site", "kind", "route", "method", "outcome",
		"actor_user_id", "session_ref", "correlation_id", "source_site", "target_site",
		"operation", "status", "duration_ms"
	};
	const Index<Value>& keys = doc.GetKeys();
	if(keys.GetCount() != __countof(fields)) return false;
	for(const Value& key_value : keys) {
		String key = key_value;
		bool known = false;
		for(const char *field : fields)
			known |= key == field;
		if(!known) return false;
	}
	try {
		WebAuditEvent e;
		e.schema_version = doc.Get("schema_version", 0);
		e.at = ScanTime(~(String)doc.Get("at", ""));
		e.site = doc.Get("site", ""); e.kind = doc.Get("kind", "");
		e.route = doc.Get("route", ""); e.method = doc.Get("method", "");
		e.outcome = doc.Get("outcome", ""); e.actor_user_id = doc.Get("actor_user_id", "");
		e.session_ref = doc.Get("session_ref", ""); e.correlation_id = doc.Get("correlation_id", "");
		e.source_site = doc.Get("source_site", ""); e.target_site = doc.Get("target_site", "");
		e.operation = doc.Get("operation", ""); e.status = doc.Get("status", 0);
		e.duration_ms = doc.Get("duration_ms", 0);
		String error;
		return e.site == site && e.Validate(error);
	}
	catch(...) { return false; }
}

bool WebAuditEvent::Validate(String& error) const
{
	if(schema_version != 1 || IsNull(at) || !SafeToken(site, 64) || !SafeToken(kind, 32) ||
	   !SafeToken(route, 160) || !SafeToken(method, 8) || !SafeToken(outcome, 32) ||
	   actor_user_id.GetCount() > 96 || session_ref.GetCount() > 64 ||
	   correlation_id.GetCount() > 64 || source_site.GetCount() > 64 ||
	   target_site.GetCount() > 64 || operation.GetCount() > 64 ||
	   status < 0 || status > 599 || duration_ms < 0 || duration_ms > 3600000) {
		error = "invalid audit event";
		return false;
	}
	if((!actor_user_id.IsEmpty() && !SafeToken(actor_user_id, 96)) ||
	   (!session_ref.IsEmpty() && !SafeToken(session_ref, 64)) ||
	   (!correlation_id.IsEmpty() && !SafeToken(correlation_id, 64)) ||
	   (!source_site.IsEmpty() && !SafeToken(source_site, 64)) ||
	   (!target_site.IsEmpty() && !SafeToken(target_site, 64)) ||
	   (!operation.IsEmpty() && !SafeToken(operation, 64))) {
		error = "invalid audit event";
		return false;
	}
	return true;
}

ValueMap WebAuditEvent::ToValue() const
{
	ValueMap m;
	m.Add("schema_version", schema_version);
	m.Add("at", AsString(at));
	m.Add("site", site);
	m.Add("kind", kind);
	m.Add("route", route);
	m.Add("method", method);
	m.Add("outcome", outcome);
	m.Add("actor_user_id", actor_user_id);
	m.Add("session_ref", session_ref);
	m.Add("correlation_id", correlation_id);
	m.Add("source_site", source_site);
	m.Add("target_site", target_site);
	m.Add("operation", operation);
	m.Add("status", status);
	m.Add("duration_ms", duration_ms);
	return m;
}

WebAuditLog::WebAuditLog(const String& site_id, const String& data_root, int capacity_)
	: site(site_id), root(data_root), capacity(capacity_ < 1 ? 1 : capacity_ > MAX_EVENTS ? MAX_EVENTS : capacity_)
{
}

StoreResult WebAuditLog::Append(const WebAuditEvent& event)
{
	String error;
	if(event.site != site || !event.Validate(error))
		return StoreResult("invalid audit event");
	Mutex::Lock __(mutex);
	IRecordCollection& store = TheStoreManager().JsonCollection(site, root, "audit_events");
	JsonFileStore::FileLock append_lock;
	String lock_error;
	if(!append_lock.Acquire(AppendFileName(AppendFileName(AppendFileName(root, "data"), "audit_events"), ".append.lock"), lock_error))
		return StoreResult("audit storage unavailable");
	Vector<String> ids;
	StoreResult r = store.List(ids);
	if(!r)
		return r;
	Time cutoff = GetSysTime() - RETENTION_DAYS * 24 * 60 * 60;
	for(int i = 0; i < ids.GetCount(); i++) {
		String json, revision;
		if(!store.Get(ids[i], json, revision))
			continue;
		try {
			ValueMap doc = ParseJSON(json);
			if(!IsSafeStoredEvent(doc, site)) continue;
			Time at = ScanTime(~(String)doc.Get("at", Null));
			if(at < cutoff) store.Delete(ids[i], revision);
		}
		catch(...) {}
	}
	ids.Clear();
	r = store.List(ids);
	if(!r)
		return r;
	if(ids.GetCount() >= capacity)
		return StoreResult("audit capacity reached");
	String id = AsString(Uuid::Create());
	String revision;
	r = store.Put(id, AsJSON(event.ToValue(), true), "", revision);
	return r;
}

void WebAuditLog::Record(const WebAuditEvent& event)
{
	try {
		bool ok = !!Append(event);
		Mutex::Lock __(mutex);
		if(!ok)
			healthy = false;
	}
	catch(...) {
		healthy = false;
	}
}

StoreResult WebAuditLog::List(Time from, Time to, int offset, int limit, Vector<ValueMap>& out)
{
	if(offset < 0 || offset > MAX_EVENTS || limit < 1 || limit > 200 || IsNull(from) || IsNull(to) ||
	   to < from || to - from > 31 * 24 * 60 * 60)
		return StoreResult("invalid audit range");
	Mutex::Lock __(mutex);
	IRecordCollection& store = TheStoreManager().JsonCollection(site, root, "audit_events");
	Vector<String> ids;
	StoreResult r = store.List(ids);
	if(!r)
		return r;
	struct AuditListRow : Moveable<AuditListRow> {
		Time at;
		String id;
		ValueMap value;
	};
	Vector<AuditListRow> rows;
	for(int i = 0; i < ids.GetCount(); i++) {
		String json, revision;
		if(!store.Get(ids[i], json, revision))
			continue;
		try {
			ValueMap doc = ParseJSON(json);
			if(!IsSafeStoredEvent(doc, site)) continue;
			Time at = ScanTime(~(String)doc.Get("at", Null));
			if(!IsNull(at) && at >= from && at <= to) {
				AuditListRow row;
				row.at = at; row.id = ids[i]; row.value = doc;
				rows.Add(pick(row));
			}
		}
		catch(...) {}
	}
	Sort(rows, [](const AuditListRow& a, const AuditListRow& b) {
		return a.at == b.at ? a.id < b.id : a.at < b.at;
	});
	for(int i = rows.GetCount() - 1 - offset; i >= 0 && out.GetCount() < limit; i--)
		out.Add(rows[i].value);
	return StoreResult();
}

bool WebAuditLog::Healthy() const { Mutex::Lock __(mutex); return healthy; }

String WebAuditRouteLabel(const String& method, const String& pattern)
{
	if(method.GetCount() > 8 || pattern.GetCount() > 150 || pattern.Find('?') >= 0 || pattern.Find('#') >= 0)
		return String();
	return ToUpper(method) + ":/" + pattern;
}

String WebAuditSessionRef()
{
	String id = AsString(Uuid::Create());
	id.Replace("-", "");
	return "as_" + ToLower(id);
}

END_UPP_NAMESPACE
