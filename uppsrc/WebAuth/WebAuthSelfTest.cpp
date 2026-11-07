#include "WebAuth.h"

NAMESPACE_UPP

namespace {

struct MockStore : IWebAuthStore {
	WebAuthUser    user;
	WebAuthSession session;
	bool           has_session = false;

	static void CopyUser(const WebAuthUser& u, WebAuthUser& out)
	{
		out.user_id = u.user_id;
		out.username = u.username;
		out.display_name = u.display_name;
		out.password_hash = u.password_hash;
		out.roles = clone(u.roles);
		out.enabled = u.enabled;
	}

	bool FindUserByUsername(const String& username, WebAuthUser& out) override
	{
		if(username != user.username) return false;
		CopyUser(user, out);
		return true;
	}
	bool FindUserById(const String& user_id, WebAuthUser& out) override
	{
		if(user_id != user.user_id) return false;
		CopyUser(user, out);
		return true;
	}
	bool FindSession(const String& token_hash, WebAuthSession& out) override
	{
		if(!has_session || token_hash != session.token_hash) return false;
		out = session;
		return true;
	}
	bool SaveSession(const WebAuthSession& s, String& error) override
	{
		session = s;
		has_session = true;
		return true;
	}
	bool DeleteSession(const String& token_hash, String& error) override
	{
		if(has_session && token_hash == session.token_hash)
			has_session = false;
		return true;
	}
	bool FindTokenByHash(const String& token_hash, WebAuthToken& out) override { return false; }
	bool TouchTokenLastUsed(const String& token_hash, const String& ip) override { return true; }
	String GetSecret() override { return "webauth-selftest-secret"; }
};

}

bool WebAuthSelfTest(String& error)
{
	// Canonical ($-delimited) password round-trip.
	String hash = WebAuthHashPassword("correct-horse-battery", 1000);
	if(!WebAuthVerifyPassword("correct-horse-battery", hash)) {
		error = "canonical ($-delimited) password round-trip failed";
		return false;
	}
	if(WebAuthVerifyPassword("wrong-password", hash)) {
		error = "canonical password verify incorrectly accepted a wrong password";
		return false;
	}

	// Rewrite a hash into the legacy SystemAuth ":"-delimited encoding and
	// confirm it still verifies -- same PBKDF2-HMAC-SHA256 bytes, different
	// stored encoding. This is the dual-format compatibility both
	// migrations (CompanyAuth and SystemAuth) rely on. Uses a realistic
	// SystemAuth-range iteration count (VerifyColonFormat rejects anything
	// below 100000, matching real auth.json data), unlike the fast 1000-
	// iteration hash above used only for the canonical-format check.
	hash = WebAuthHashPassword("correct-horse-battery", 100000);
	Vector<String> parts = Split(hash, '$');
	if(parts.GetCount() != 4) {
		error = "unexpected canonical hash shape";
		return false;
	}
	String legacy = "pbkdf2-sha256:" + parts[1] + ":" + parts[2] + ":" + parts[3];
	if(!WebAuthVerifyPassword("correct-horse-battery", legacy)) {
		error = "legacy (:-delimited) password verify failed";
		return false;
	}

	// Session token lifecycle against a mock, storage-agnostic store.
	MockStore store;
	store.user.user_id = "u1";
	store.user.username = "alice";
	store.user.display_name = "Alice";
	store.user.password_hash = WebAuthHashPassword("s3cret", 1000);
	store.user.enabled = true;

	WebAuthService::SetStore(&store);
	WebAuthConfig cfg;
	cfg.role_scopes = [](const Vector<String>& roles) { return clone(roles); };
	WebAuthService::Configure(cfg);

	String raw = WebAuthService::GenerateSessionToken();
	if(!raw.StartsWith(cfg.session_token_prefix)) {
		WebAuthService::SetStore(NULL);
		error = "generated session token missing configured prefix";
		return false;
	}

	WebAuthSession session;
	session.token_hash = WebAuthService::HashToken(raw);
	session.user_id = "u1";
	session.created_at = GetSysTime();
	session.expires_at = GetSysTime() + 3600;
	String save_err;
	store.SaveSession(session, save_err);

	WebAuthIdentity id = WebAuthService::ResolveSessionToken(raw);
	WebAuthService::SetStore(NULL);
	if(!id.authenticated || id.user_id != "u1" || id.username != "alice") {
		error = "session token did not resolve to the expected identity";
		return false;
	}

	return true;
}

END_UPP_NAMESPACE
