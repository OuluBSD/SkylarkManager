#include "Request.h"

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
	if(code != 200)
		http.Response(code, code == 404 ? "Not found" : code == 403 ? "Forbidden" : "Error");
	return *this;
}

WebRequest& WebRequest::Status(int code, const String& text)
{
	http.Response(code, text);
	return *this;
}

WebRequest& WebRequest::Redirect(const String& url)
{
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
		http.SetRawCookie(~site->CookieName(), sid, s.expires, "/", NULL, false, true, "Strict");
	}
	return *this;
}

WebRequest& WebRequest::AdoptSession(const String& session_id)
{
	WebAccountSession s;
	if(!site->Accounts().ResolveSession(session_id, s))
		return *this;
	session = s;
	has_session = true;
	if(s.user_id.GetCount()) {
		WebAccountUser u;
		if(site->Accounts().GetUser(s.user_id, u) && !u.disabled) {
			user = u;
			has_user = true;
		}
	}
	http.SetRawCookie(~site->CookieName(), session_id, s.expires, "/", NULL, false, true, "Strict");
	return *this;
}

WebRequest& WebRequest::DropSession()
{
	if(has_session)
		site->Accounts().Logout(session.session_id);
	has_session = false;
	has_user = false;
	http.SetRawCookie(~site->CookieName(), "", GetSysTime() - 3600, "/", NULL, false, true, "Strict");
	return *this;
}

void WebRequest::SetSessionData(const ValueMap& data)
{
	if(has_session)
		site->Accounts().SaveSessionData(session.session_id, data);
}

END_UPP_NAMESPACE
