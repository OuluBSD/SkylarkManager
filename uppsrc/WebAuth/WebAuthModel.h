#ifndef _WebAuth_WebAuthModel_h_
#define _WebAuth_WebAuthModel_h_

#include <Core/Core.h>

NAMESPACE_UPP

#define WEBAUTH_ACTOR_HUMAN      "human"
#define WEBAUTH_ACTOR_AGENT      "agent"
#define WEBAUTH_ACTOR_AUTOMATION "automation"

// ---------------------------------------------------------------------------
// WebAuthIdentity: the resolved actor of a request, shared across apps that
// use WebAuth. Deny by default: an unauthenticated request carries no roles
// and no scopes.
// ---------------------------------------------------------------------------

struct WebAuthIdentity : Moveable<WebAuthIdentity> {
	String         user_id;
	String         token_id;          // agent token id when bearer-authenticated
	String         actor_type;        // human | agent | automation
	String         username;
	String         display_name;
	Vector<String> roles;             // session auth: user roles; token auth: empty
	Vector<String> scopes;            // token auth: token scopes; session auth: role-derived
	bool           authenticated = false;
	String         session_token_hash;
	String         bearer_token_hash;

	bool HasRole(const String& role) const;
	bool HasAnyRole(const Vector<String>& roles_) const;
	bool HasScope(const String& scope) const;
	bool IsAdmin() const      { return HasRole("admin"); }
	bool IsHuman() const      { return authenticated && actor_type == WEBAUTH_ACTOR_HUMAN; }
	bool IsAgent() const      { return authenticated && actor_type == WEBAUTH_ACTOR_AGENT; }
	bool IsAutomation() const { return authenticated && actor_type == WEBAUTH_ACTOR_AUTOMATION; }
};

struct WebAuthSession : Moveable<WebAuthSession> {
	String token_hash;
	String user_id;
	Time   created_at;
	Time   expires_at;
	String ip;
	String user_agent;
};

struct WebAuthToken : Moveable<WebAuthToken> {
	String         token_hash;
	String         id;
	String         name;              // human-readable label for display
	String         user_id;           // owner
	String         actor_type = WEBAUTH_ACTOR_AGENT;  // agent | automation; decided by the storage adapter
	Vector<String> scopes;
	Time           created_at;
	Time           expires_at;        // Null == never
	bool           revoked = false;
	Time           last_used_at;
};

struct WebAuthUser : Moveable<WebAuthUser> {
	String         user_id;
	String         username;
	String         display_name;
	String         password_hash;
	Vector<String> roles;
	bool           enabled = true;
};

// Pluggable persistence: WebAuth never touches a filesystem, lock, or atomic
// write itself. The embedding app's own store (e.g. a JSON collection with
// lock+fsync+atomic-rename writes, or a future database-backed user store)
// implements this interface; WebAuth does not assume any particular backend.
struct IWebAuthStore {
	virtual bool   FindUserByUsername(const String& username, WebAuthUser& out) = 0;
	virtual bool   FindUserById(const String& user_id, WebAuthUser& out) = 0;
	virtual bool   FindSession(const String& token_hash, WebAuthSession& out) = 0;
	virtual bool   SaveSession(const WebAuthSession& s, String& error) = 0;
	virtual bool   DeleteSession(const String& token_hash, String& error) = 0;
	virtual bool   FindTokenByHash(const String& token_hash, WebAuthToken& out) = 0;
	// Best-effort last-used bookkeeping; failures are ignored by callers.
	virtual bool   TouchTokenLastUsed(const String& token_hash, const String& ip) = 0;
	virtual String GetSecret() = 0;
	virtual ~IWebAuthStore() {}
};

END_UPP_NAMESPACE

#endif
