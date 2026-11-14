#include "Request.h"
#include "Engine.h"

NAMESPACE_UPP

WebRequest::WebRequest(Http& http_, EngineSite& site_, const String& method_, const String& path_,
                       const Vector<String>& args)
	: http(http_), site(&site_), method(method_), path(path_)
{
	path_args <<= args;
}

String WebRequest::Param(const char *name) const
{
	Value v = http[name];
	return v.IsNull() ? String() : (String)v;
}

int WebRequest::IntParam(const char *name) const
{
	return http.Int(name);
}

String WebRequest::Header(const char *name) const
{
	return http.GetHeader(name);
}

String WebRequest::Body() const
{
	return http.GetRequestContent();
}

String WebRequest::Cookie(const char *name) const
{
	String key = String("@") + name;
	return http[~key];
}

String WebRequest::ClientIp() const
{
	return http.GetPeerAddr();
}

bool WebRequest::IsMutation() const
{
	return method == "POST" || method == "PUT" || method == "PATCH" || method == "DELETE";
}

bool WebRequest::IsSecureTransport() const
{
	TcpSocket *socket = http.GetSocket();
	if(socket && socket->IsSSL())
		return true;
	if(!socket)
		return false;
	String peer = socket->GetPeerAddr(); // direct socket peer; Http::GetPeerAddr trusts X-Forwarded-For
	bool trusted_peer = false;
	for(const String& configured_peer : site->trusted_proxy_peers)
		if(configured_peer == peer) {
			trusted_peer = true;
			break;
		}
	if(!trusted_peer)
		return false;
	return ToLower(TrimBoth(http.GetHeader("x-forwarded-proto"))) == "https";
}

String WebRequest::CsrfToken() const
{
	return has_session ? session.csrf_token : String();
}

WebRequest& WebRequest::operator<<(const Value& v)
{
	http << v;
	return *this;
}

WebRequest& WebRequest::Html(const String& html)
{
	http << html;
	return *this;
}

WebRequest& WebRequest::Plain(const String& text, const char *ctype)
{
	http.ContentType(ctype) << text;
	return *this;
}

WebRequest& WebRequest::Json(const Value& v, int code)
{
	http.ContentType("application/json; charset=UTF-8");
	http << AsJSON(v, true);
	response_status = code;
	if(code != 200)
		http.Response(code, code == 404 ? "Not found" : code == 403 ? "Forbidden" : "Error");
	return *this;
}

WebRequest& WebRequest::Status(int code, const String& text)
{
	response_status = code;
	http.Response(code, text);
	return *this;
}

String WebRequest::RequestCookie(const String& name)
{
	return http[~(String("@") + name)];
}

WebRequest& WebRequest::SetCookieValue(const String& name, const String& value, Time expires, bool secure)
{
	http.SetRawCookie(~name, value, expires, "/", NULL, secure, true, "Strict");
	return *this;
}

WebRequest& WebRequest::Redirect(const String& url)
{
	response_status = 302;
	WebAuditEvent e;
	e.at = GetSysTime(); e.site = site->id; e.kind = "redirect";
	e.route = "redirect"; e.method = method; e.status = 302;
	bool configured_origin = false, configured_sso = false;
	for(const EngineSiteListing& entry : TheWebEngine().ListSites()) {
		EngineSite *target = TheWebEngine().FindSite(entry.id);
		if(!target) continue;
		String public_root = target->PublicUrl("/");
		if(!public_root.IsEmpty() && url.StartsWith(public_root)) {
			configured_origin = true;
			String path = url.Mid(public_root.GetCount() - 1);
			int query = path.Find('?'), fragment = path.Find('#');
			int end = query < 0 ? fragment : (fragment < 0 ? query : min(query, fragment));
			if(end >= 0) path = path.Left(end);
			configured_sso |= path.Find("session/sso") >= 0 ||
			                  (entry.id == "meta" && path == "/login");
		}
	}
	bool relative_local = url.StartsWith("/") && !url.StartsWith("//") &&
	                      url.Find('\\') < 0 && url.Find('\r') < 0 && url.Find('\n') < 0;
	String relative_path = url;
	int relative_end = relative_path.Find('?');
	if(relative_end >= 0) relative_path = relative_path.Left(relative_end);
	e.outcome = configured_sso || relative_path.Find("session/sso") >= 0 ? "approved_sso" :
	            (relative_local || configured_origin ? "approved_local" : "external");
	if(has_user) e.actor_user_id = user.user_id;
	if(has_session) e.session_ref = session.data.Get("__audit_ref", "");
	site->Audit().Record(e);
	http.Redirect(~url);
	return *this;
}

WebRequest& WebRequest::EnsureSession()
{
	if(has_session)
		return *this;
	String sid, csrf;
	if(!site->Accounts().CreateGuestSession(sid, csrf).ok)
		return *this;
	WebAccountSession s;
	if(site->Accounts().ResolveSession(sid, s)) {
		session = s;
		has_session = true;
		WebAuditEvent e;
		e.at = GetSysTime(); e.site = site->id; e.kind = "session"; e.route = "session:create";
		e.method = method; e.outcome = "created"; e.session_ref = s.data.Get("__audit_ref", "");
		site->Audit().Record(e);
		http.SetRawCookie(~site->CookieName(), sid, s.expires, "/", NULL, IsSecureTransport(), true, "Strict");
	}
	return *this;
}

WebRequest& WebRequest::AdoptSession(const String& session_id, const String& source)
{
	WebAccountSession s;
	if(!site->Accounts().ResolveSession(session_id, s))
		return *this;
	session = s;
	has_session = true;
	WebAuditEvent e;
	e.at = GetSysTime(); e.site = site->id; e.kind = "session"; e.route = "session:create";
	e.method = method; e.outcome = "created"; e.actor_user_id = s.user_id;
	e.session_ref = s.data.Get("__audit_ref", "");
	e.operation = source.IsEmpty() ? (String)s.data.Get("sign_in", "local") : source;
	site->Audit().Record(e);
	e.kind = "session"; e.route = "session:adopt";
	e.method = method; e.outcome = "adopted"; e.actor_user_id = s.user_id;
	e.operation = source.IsEmpty() ? (String)s.data.Get("sign_in", "local") : source;
	site->Audit().Record(e);
	if(e.operation == "meta-account" || e.operation == "sso") {
		e.kind = "login"; e.route = "session/sso"; e.outcome = "success"; e.operation = "sso";
		site->Audit().Record(e);
	}
	if(s.user_id.GetCount()) {
		WebAccountUser u;
		if(site->Accounts().GetUser(s.user_id, u) && !u.disabled) {
			user = u;
			has_user = true;
		}
	}
	http.SetRawCookie(~site->CookieName(), session_id, s.expires, "/", NULL, IsSecureTransport(), true, "Strict");
	return *this;
}

WebRequest& WebRequest::DropSession()
{
	if(has_session) {
		WebAuditEvent e;
		e.at = GetSysTime(); e.site = site->id; e.kind = "session"; e.route = "session:logout";
		e.method = method; e.outcome = "revoked"; e.actor_user_id = has_user ? user.user_id : String();
		e.session_ref = session.data.Get("__audit_ref", "");
		site->Audit().Record(e);
		site->Accounts().Logout(session.session_id);
	}
	has_session = false;
	has_user = false;
	http.SetRawCookie(~site->CookieName(), "", GetSysTime() - 3600, "/", NULL, IsSecureTransport(), true, "Strict");
	return *this;
}

void WebRequest::SetSessionData(const ValueMap& data)
{
	if(has_session)
		site->Accounts().SaveSessionData(session.session_id, data);
}

END_UPP_NAMESPACE
