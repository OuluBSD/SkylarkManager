#include "WebAuth.h"

NAMESPACE_UPP

IWebAuthStore* WebAuthService::s_store = NULL;
WebAuthConfig  WebAuthService::s_cfg;

namespace {

void SendAuthError(Http& http, int code, const String& message)
{
	ValueMap m;
	m.Set("error", message);
	http.Response(code, "Error");
	http.ContentType("application/json; charset=utf-8") << AsJSON(m, true);
}

}

String WebAuthService::Login(Http& http, const String& username, const String& password,
                             const String& ip, const String& user_agent, String& error)
{
	IWebAuthStore* store = GetStore();
	if(!store) {
		error = "authentication service is not initialized";
		return String();
	}
	if(username.IsEmpty() || password.IsEmpty()) {
		error = "username and password are required";
		return String();
	}

	WebAuthUser user;
	if(!store->FindUserByUsername(username, user)) {
		error = "invalid credentials";
		return String();
	}
	// Fail closed: disabled users cannot authenticate; empty password hash
	// means the account has no local password (service identity).
	if(!user.enabled || user.password_hash.IsEmpty()) {
		error = "invalid credentials";
		return String();
	}
	if(!WebAuthVerifyPassword(password, user.password_hash)) {
		error = "invalid credentials";
		return String();
	}

	String raw = GenerateSessionToken();
	WebAuthSession session;
	session.token_hash = HashToken(raw);
	session.user_id = user.user_id;
	session.created_at = GetSysTime();
	session.expires_at = GetSysTime() + (int64)s_cfg.session_ttl_hours * 3600;
	session.ip = ip;
	session.user_agent = user_agent;

	String err;
	if(!store->SaveSession(session, err)) {
		error = "cannot persist session";
		return String();
	}

	http.SetCookie(s_cfg.cookie_name, raw, session.expires_at, "/", NULL, false, true);
	return raw;
}

void WebAuthService::Logout(Http& http)
{
	IWebAuthStore* store = GetStore();
	String raw = ExtractCookie(http, s_cfg.cookie_name);
	if(!raw.IsEmpty() && store) {
		String err;
		store->DeleteSession(HashToken(raw), err);
	}
	// Also revoke bearer session if the request authenticated via header.
	String bearer = ExtractBearer(http);
	if(!bearer.IsEmpty() && store && bearer.StartsWith(s_cfg.session_token_prefix)) {
		String err;
		store->DeleteSession(HashToken(bearer), err);
	}
	http.SetCookie(s_cfg.cookie_name, "", GetSysTime() - 3600, "/", NULL, false, true);
}

WebAuthIdentity WebAuthService::GuestIdentity()
{
	WebAuthIdentity id;
	id.actor_type = WEBAUTH_ACTOR_HUMAN;
	id.display_name = "Guest";
	id.authenticated = false;
	return id;
}

WebAuthIdentity WebAuthService::ResolveSessionToken(const String& raw_token)
{
	IWebAuthStore* store = GetStore();
	if(raw_token.IsEmpty() || !store)
		return GuestIdentity();

	String hash = HashToken(raw_token);
	WebAuthSession session;
	if(!store->FindSession(hash, session))
		return GuestIdentity();
	if(!IsNull(session.expires_at) && session.expires_at < GetSysTime()) {
		String err;
		store->DeleteSession(hash, err);
		return GuestIdentity();
	}
	WebAuthUser user;
	if(!store->FindUserById(session.user_id, user) || !user.enabled)
		return GuestIdentity();

	WebAuthIdentity id;
	id.authenticated = true;
	id.actor_type = WEBAUTH_ACTOR_HUMAN;
	id.user_id = user.user_id;
	id.username = user.username;
	id.display_name = user.display_name;
	id.roles = clone(user.roles);
	id.scopes = s_cfg.role_scopes ? s_cfg.role_scopes(user.roles) : Vector<String>();
	id.session_token_hash = hash;
	return id;
}

WebAuthIdentity WebAuthService::ResolveBearerToken(const String& raw_token, const String& ip)
{
	IWebAuthStore* store = GetStore();
	if(raw_token.IsEmpty() || !store)
		return GuestIdentity();

	// Agent/automation tokens: prefixed, scoped, expiring, revocable.
	if(raw_token.StartsWith(s_cfg.agent_token_prefix)) {
		String hash = HashToken(raw_token);
		WebAuthToken token;
		if(!store->FindTokenByHash(hash, token))
			return GuestIdentity();
		if(token.revoked)
			return GuestIdentity();
		if(!IsNull(token.expires_at) && token.expires_at < GetSysTime())
			return GuestIdentity();

		WebAuthUser owner;
		String owner_name;
		if(!token.user_id.IsEmpty() && store->FindUserById(token.user_id, owner))
			owner_name = owner.display_name;

		// Best-effort last-used metadata, throttled to avoid a write per request.
		if(IsNull(token.last_used_at) ||
		   token.last_used_at < GetSysTime() - (int64)s_cfg.token_lastused_throttle_minutes * 60)
			store->TouchTokenLastUsed(hash, ip);

		WebAuthIdentity id;
		id.authenticated = true;
		id.actor_type = token.actor_type;
		id.token_id = token.id;
		id.user_id = token.user_id;
		id.username = owner_name;
		id.display_name = owner_name.IsEmpty() ? ("token " + token.name) : (owner_name + " (token " + token.name + ")");
		id.scopes = clone(token.scopes);
		id.bearer_token_hash = hash;
		return id;
	}

	// Session tokens double as bearer tokens for API clients.
	if(raw_token.StartsWith(s_cfg.session_token_prefix))
		return ResolveSessionToken(raw_token);

	// Unknown prefix: try both hash spaces before giving up (fail closed).
	WebAuthIdentity via_session = ResolveSessionToken(raw_token);
	if(via_session.authenticated)
		return via_session;
	String hash = HashToken(raw_token);
	WebAuthToken token;
	if(store->FindTokenByHash(hash, token) && !token.revoked &&
	   (IsNull(token.expires_at) || token.expires_at >= GetSysTime())) {
		WebAuthIdentity id;
		id.authenticated = true;
		id.actor_type = token.actor_type;
		id.token_id = token.id;
		id.user_id = token.user_id;
		id.scopes = clone(token.scopes);
		id.bearer_token_hash = hash;
		WebAuthUser owner;
		if(!token.user_id.IsEmpty() && store->FindUserById(token.user_id, owner)) {
			id.username = owner.display_name;
			id.display_name = owner.display_name + " (token " + token.name + ")";
		}
		else
			id.display_name = "token " + token.name;
		return id;
	}
	return GuestIdentity();
}

WebAuthIdentity WebAuthService::GetCurrentIdentity(const Http& http)
{
	String bearer = ExtractBearer(http);
	if(!bearer.IsEmpty())
		return ResolveBearerToken(bearer, ClientIp(http));
	String cookie = ExtractCookie(http, s_cfg.cookie_name);
	if(!cookie.IsEmpty())
		return ResolveSessionToken(cookie);
	return GuestIdentity();
}

bool WebAuthService::RequireAuth(Http& http)
{
	WebAuthIdentity id = GetCurrentIdentity(http);
	if(!id.authenticated) {
		http.Redirect("/login");
		return false;
	}
	return true;
}

bool WebAuthService::RequireApiAuth(Http& http)
{
	WebAuthIdentity id = GetCurrentIdentity(http);
	if(!id.authenticated) {
		SendAuthError(http, 401, "authentication required");
		return false;
	}
	return true;
}

bool WebAuthService::RequireRole(Http& http, const String& role)
{
	WebAuthIdentity id = GetCurrentIdentity(http);
	if(!id.authenticated) {
		http.Redirect("/login");
		return false;
	}
	if(!id.HasRole(role)) {
		SendAuthError(http, 403, "role required: " + role);
		return false;
	}
	return true;
}

bool WebAuthService::RequireApiRole(Http& http, const String& role)
{
	WebAuthIdentity id = GetCurrentIdentity(http);
	if(!id.authenticated) {
		SendAuthError(http, 401, "authentication required");
		return false;
	}
	if(!id.HasRole(role)) {
		SendAuthError(http, 403, "role required: " + role);
		return false;
	}
	return true;
}

bool WebAuthService::RequireApiScope(Http& http, const String& scope)
{
	WebAuthIdentity id = GetCurrentIdentity(http);
	if(!id.authenticated) {
		SendAuthError(http, 401, "authentication required");
		return false;
	}
	if(!id.HasScope(scope)) {
		SendAuthError(http, 403, "scope required: " + scope);
		return false;
	}
	return true;
}

bool WebAuthService::RequireHuman(Http& http)
{
	WebAuthIdentity id = GetCurrentIdentity(http);
	if(!id.authenticated) {
		http.Redirect("/login");
		return false;
	}
	if(!id.IsHuman()) {
		SendAuthError(http, 403, "this action requires a human actor");
		return false;
	}
	return true;
}

bool WebAuthService::RequireApiHuman(Http& http)
{
	WebAuthIdentity id = GetCurrentIdentity(http);
	if(!id.authenticated) {
		SendAuthError(http, 401, "authentication required");
		return false;
	}
	if(!id.IsHuman()) {
		SendAuthError(http, 403, "this action requires a human actor");
		return false;
	}
	return true;
}

String WebAuthService::GenerateSessionToken() { return s_cfg.session_token_prefix + WebAuthRandomBytes(24); }
String WebAuthService::GenerateAgentToken()   { return s_cfg.agent_token_prefix + WebAuthRandomBytes(24); }

Vector<String> WebAuthService::EffectiveScopes(const WebAuthIdentity& id)
{
	if(id.IsAgent() || id.IsAutomation())
		return clone(id.scopes);
	return s_cfg.role_scopes ? s_cfg.role_scopes(id.roles) : Vector<String>();
}

String WebAuthService::ExtractCookie(const Http& http, const String& name)
{
	String cookie_hdr = http.GetHeader("cookie");
	Vector<String> parts = Split(cookie_hdr, ';');
	String prefix = name + "=";
	for(int i = 0; i < parts.GetCount(); i++) {
		String p = TrimBoth(parts[i]);
		if(p.StartsWith(prefix))
			return TrimBoth(p.Mid(prefix.GetCount()));
	}
	return String();
}

String WebAuthService::ExtractBearer(const Http& http)
{
	String auth_hdr = http.GetHeader("authorization");
	if(auth_hdr.StartsWith("Bearer ") || auth_hdr.StartsWith("bearer "))
		return TrimBoth(auth_hdr.Mid(7));
	return String();
}

String WebAuthService::ClientIp(const Http& http)
{
	String fwd = http.GetHeader("x-forwarded-for");
	if(!fwd.IsEmpty()) {
		Vector<String> parts = Split(fwd, ',');
		if(parts.GetCount() > 0)
			return TrimBoth(parts[0]);
	}
	return http.GetPeerAddr();
}

String WebAuthService::MintCsrfIdentity(Http& http)
{
	Value current = http[".__identity__"];
	if(IsNull(current)) {
		String s = AsString(Uuid::Create());
		http.SessionSet("__identity__", s);
		return s;
	}
	return current.ToString();
}

END_UPP_NAMESPACE
