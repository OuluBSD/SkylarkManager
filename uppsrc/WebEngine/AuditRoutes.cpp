#include "Request.h"

NAMESPACE_UPP

static void AuditEvents(WebRequest& r)
{
	r.SetHeader("Cache-Control", "no-store, no-cache, must-revalidate");
	r.SetHeader("Pragma", "no-cache");
	if(!r.HasUser()) {
		ValueMap e; e.Add("ok", false); e.Add("error", "authentication required");
		r.Json(e, 401);
		return;
	}
	if(FindIndex(r.User().roles, String("admin")) < 0) {
		ValueMap e; e.Add("ok", false); e.Add("error", "admin role required");
		r.Json(e, 403);
		return;
	}
	String limit_arg = r.Param("limit"), offset_arg = r.Param("offset");
	int limit = limit_arg.IsEmpty() ? 100 : r.IntParam("limit");
	int offset = offset_arg.IsEmpty() ? 0 : r.IntParam("offset");
	String to_arg = r.Param("to"), from_arg = r.Param("from");
	Time to = to_arg.IsEmpty() ? GetSysTime() : ScanTime(~to_arg);
	Time from = from_arg.IsEmpty() ? to - 24 * 60 * 60 : ScanTime(~from_arg);
	Vector<ValueMap> rows;
	StoreResult result = r.Site().Audit().List(from, to, offset, limit, rows);
	if(!result) {
		ValueMap e; e.Add("ok", false); e.Add("error", "audit history unavailable");
		r.Json(e, result.error == "invalid audit range" ? 400 : 503);
		return;
	}
	ValueArray events;
	for(const ValueMap& row : rows)
		events.Add(row);
	ValueMap out;
	out.Add("ok", true);
	out.Add("site", r.Site().id);
	out.Add("offset", offset);
	out.Add("limit", limit);
	out.Add("events", events);
	r.Json(out);
}

static void AuditPage(WebRequest& r)
{
	r.SetHeader("Cache-Control", "no-store, no-cache, must-revalidate");
	if(!r.HasUser()) { r.Status(401, "Authentication required"); return; }
	if(FindIndex(r.User().roles, String("admin")) < 0) { r.Status(403, "Admin role required"); return; }
	r.Html("<!doctype html><meta charset=utf-8><title>Audit</title><h1>Session and communication audit</h1>"
	       "<p>Sanitized, site-scoped snapshots. API reads are excluded from the event ledger.</p>"
	       "<h2>Diagnostics</h2><pre id=health></pre><h2>Active sessions</h2><pre id=sessions></pre>"
	       "<h2>Recent events</h2><pre id=events></pre><script>"
	       "for(const id of ['health','sessions','events']){const path=id==='health'?'api/audit/health':id==='sessions'?'api/audit/sessions':'api/audit/events';"
	       "fetch(path,{cache:'no-store',credentials:'same-origin'}).then(r=>r.json()).then(x=>document.getElementById(id).textContent=JSON.stringify(x,null,2)).catch(()=>document.getElementById(id).textContent='Unavailable');}"
	       "</script>");
}

static void AuditSessions(WebRequest& r)
{
	r.SetHeader("Cache-Control", "no-store, no-cache, must-revalidate");
	r.SetHeader("Pragma", "no-cache");
	if(!r.HasUser()) { ValueMap e; e.Add("ok", false); e.Add("error", "authentication required"); r.Json(e, 401); return; }
	if(FindIndex(r.User().roles, String("admin")) < 0) { ValueMap e; e.Add("ok", false); e.Add("error", "admin role required"); r.Json(e, 403); return; }
	String offset_arg = r.Param("offset"), limit_arg = r.Param("limit");
	int offset = offset_arg.IsEmpty() ? 0 : r.IntParam("offset");
	int limit = limit_arg.IsEmpty() ? 100 : r.IntParam("limit");
	Vector<WebAccountSessionSnapshot> snapshots;
	StoreResult listed = r.Site().Accounts().ListActiveSessionSnapshots(snapshots, offset, limit);
	if(!listed) {
		ValueMap e; e.Add("ok", false); e.Add("error", "session snapshots unavailable");
		r.Json(e, listed.error == "invalid snapshot limit" ? 400 : 503); return;
	}
	ValueArray rows;
	for(const WebAccountSessionSnapshot& s : snapshots) {
		ValueMap row;
		row.Add("user_id", s.user_id);
		row.Add("session_ref", s.audit_ref);
		row.Add("created", AsString(s.created));
		row.Add("expires", AsString(s.expires));
		rows.Add(row);
	}
	ValueMap out; out.Add("ok", true); out.Add("site", r.Site().id); out.Add("offset", offset); out.Add("limit", limit); out.Add("sessions", rows);
	r.Json(out);
}

static void AuditHealth(WebRequest& r)
{
	r.SetHeader("Cache-Control", "no-store, no-cache, must-revalidate");
	r.SetHeader("Pragma", "no-cache");
	if(!r.HasUser()) { ValueMap e; e.Add("ok", false); e.Add("error", "authentication required"); r.Json(e, 401); return; }
	if(FindIndex(r.User().roles, String("admin")) < 0) { ValueMap e; e.Add("ok", false); e.Add("error", "admin role required"); r.Json(e, 403); return; }
	ValueMap status; status.Add("ok", true); status.Add("site", r.Site().id);
	status.Add("audit_write_healthy", r.Site().Audit().Healthy());
	status.Add("durable_ledger", "webstorage");
	status.Add("broker_diagnostics", "operational_only; communications_are_durable_in_WebAudit");
	r.Json(status);
}

void InstallAuditRoutes(EngineSite& site)
{
	site.Router().Add("audit", AuditPage);
	site.Router().Add("api/audit/events", AuditEvents);
	site.Router().Add("api/audit/sessions", AuditSessions);
	site.Router().Add("api/audit/health", AuditHealth);
}

END_UPP_NAMESPACE
