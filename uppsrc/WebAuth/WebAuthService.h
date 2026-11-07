#ifndef _WebAuth_WebAuthService_h_
#define _WebAuth_WebAuthService_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include "WebAuthModel.h"

NAMESPACE_UPP

struct WebAuthConfig : Moveable<WebAuthConfig> {
	String cookie_name                     = "webauth_session";
	String session_token_prefix            = "wast_";
	String agent_token_prefix              = "waat_";
	int    session_ttl_hours               = 12;
	int    token_lastused_throttle_minutes = 5;
	// Human sessions get the union of scopes their roles grant; agent/
	// automation tokens already carry explicit scopes. Supplied by the
	// embedding app -- WebAuth has no domain role/scope table of its own.
	Function<Vector<String>(const Vector<String>&)> role_scopes;

	WebAuthConfig() {}
};

// ---------------------------------------------------------------------------
// WebAuthService: session-cookie and bearer-token authentication, storage-
// agnostic (see IWebAuthStore).
//
// Security invariants:
//  - Passwords verified with PBKDF2-HMAC-SHA256; no developer fallback
//    passwords, no admin-by-default.
//  - Session and agent tokens are random, prefixed, and only server-secret-
//    peppered hashes are persisted; raw tokens are returned exactly once.
//  - Tokens enforce expiry and revocation; sessions enforce expiry.
//  - Deny by default: an unauthenticated or unresolvable request is a guest.
// ---------------------------------------------------------------------------
class WebAuthService {
public:
	static void SetStore(IWebAuthStore *store) { s_store = store; }
	static IWebAuthStore* GetStore()           { return s_store; }
	static void Configure(const WebAuthConfig& cfg) { s_cfg = cfg; }
	static const WebAuthConfig& GetConfig()    { return s_cfg; }

	// ---------- Browser session login ----------
	static String Login(Http& http, const String& username, const String& password,
	                    const String& ip, const String& user_agent, String& error);
	static void   Logout(Http& http);

	// ---------- Identity resolution ----------
	static WebAuthIdentity GetCurrentIdentity(const Http& http);
	static WebAuthIdentity ResolveSessionToken(const String& raw_token);
	static WebAuthIdentity ResolveBearerToken(const String& raw_token, const String& ip = "");
	static WebAuthIdentity GuestIdentity();

	// ---------- Guards (deny by default) ----------
	static bool RequireAuth(Http& http);
	static bool RequireApiAuth(Http& http);
	static bool RequireRole(Http& http, const String& role);
	static bool RequireApiRole(Http& http, const String& role);
	static bool RequireApiScope(Http& http, const String& scope);
	static bool RequireHuman(Http& http);
	static bool RequireApiHuman(Http& http);

	// ---------- Token issuance ----------
	static String GenerateSessionToken();
	static String GenerateAgentToken();
	static String HashToken(const String& raw) { return WebAuthPepperedHash(GetSecret(), raw); }

	// ---------- Role-derived scopes ----------
	static Vector<String> EffectiveScopes(const WebAuthIdentity& id);

	// ---------- HTTP helpers ----------
	static String ExtractCookie(const Http& http, const String& name);
	static String ExtractBearer(const Http& http);
	static String ClientIp(const Http& http);

	// ---------- CSRF ----------
	// Mints or reuses the per-session POST identity for :POST handlers.
	// Skylark's GetIdentity has a pre-seeding flaw in this assembly (dispatch
	// always creates var[".__identity__"] with Null, making GetIdentity's
	// minting branch unreachable on first visit), so identities are minted
	// explicitly via the public SessionSet, keeping the dispatch-side check
	// `__post_identity__ == .__identity__` intact.
	static String MintCsrfIdentity(Http& http);

private:
	static String GetSecret() { return s_store ? s_store->GetSecret() : String(); }
	static IWebAuthStore* s_store;
	static WebAuthConfig  s_cfg;
};

END_UPP_NAMESPACE

#endif
