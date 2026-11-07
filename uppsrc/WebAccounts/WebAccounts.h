#ifndef _WebAccounts_WebAccounts_h_
#define _WebAccounts_WebAccounts_h_

#include <Core/Core.h>
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// WebAccounts: instance-scoped accounts for fenced websites.
//
// One SiteAccounts == one website identity realm. Users, sessions, CSRF
// tokens and secrets never cross sites; two sites can safely register the
// same username with different passwords.
// ---------------------------------------------------------------------------

struct WebAccountUser : Moveable<WebAccountUser> {
	String        user_id;      // "wau_" prefixed
	String        username;     // unique per site (case-insensitive)
	String        display_name;
	String        password_hash; // pbkdf2_sha256$... (WebAuth format)
	Vector<String> roles;
	bool          disabled = false;
	String        totp_secret;   // base32 TOTP secret (empty = not enrolled)
	bool          totp_enrolled = false;
	Vector<String> recovery_hashes; // single-use recovery code hashes (totp peppered)
	Time          created;
	Time          updated;

	// Explicit copy support: U++ Vector members are pick-only.
	WebAccountUser() {}
	WebAccountUser(const WebAccountUser& o)
		: user_id(o.user_id), username(o.username), display_name(o.display_name)
		, password_hash(o.password_hash), roles(clone(o.roles)), disabled(o.disabled)
		, totp_secret(o.totp_secret), totp_enrolled(o.totp_enrolled)
		, recovery_hashes(clone(o.recovery_hashes))
		, created(o.created), updated(o.updated) {}
	WebAccountUser& operator=(const WebAccountUser& o) {
		if(this != &o) {
			user_id = o.user_id;
			username = o.username;
			display_name = o.display_name;
			password_hash = o.password_hash;
			roles = clone(o.roles);
			disabled = o.disabled;
			totp_secret = o.totp_secret;
			totp_enrolled = o.totp_enrolled;
			recovery_hashes = clone(o.recovery_hashes);
			created = o.created;
			updated = o.updated;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("user_id", user_id)("username", username)("display_name", display_name)
		  ("password_hash", password_hash)("roles", roles)("disabled", disabled)
		  ("totp_secret", totp_secret)("totp_enrolled", totp_enrolled)
		  ("recovery_hashes", recovery_hashes)
		  ("created", created)("updated", updated);
	}
};

struct WebAccountSession : Moveable<WebAccountSession> {
	String   session_id;   // "was_" prefixed, returned once to the browser
	String   site;         // owning site id; ResolveSession rejects mismatches
	String   user_id;
	String   csrf_token;
	Time     created;
	Time     expires;
	ValueMap data;         // webapp-scoped session data

	WebAccountSession() {}
	WebAccountSession(const WebAccountSession& o)
		: session_id(o.session_id), site(o.site), user_id(o.user_id)
		, csrf_token(o.csrf_token), created(o.created), expires(o.expires), data(o.data) {}
	WebAccountSession& operator=(const WebAccountSession& o) {
		if(this != &o) {
			session_id = o.session_id;
			site = o.site;
			user_id = o.user_id;
			csrf_token = o.csrf_token;
			created = o.created;
			expires = o.expires;
			data = o.data;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("session_id", session_id)("site", site)("user_id", user_id)
		  ("csrf_token", csrf_token)("created", created)("expires", expires)("data", data);
	}
};

class SiteAccounts {
public:
	SiteAccounts(const String& site_id, const String& data_root, int session_ttl_hours = 12);

	const String& Site() const           { return site; }
	String        CookieName() const;
	int           SessionTtlHours() const { return ttl_hours; }

	// --- users ---
	StoreResult CreateUser(const String& username, const String& password,
	                       const String& display_name, const Vector<String>& roles,
	                       String& user_id, bool& existed);
	bool        FindUserByUsername(const String& username, WebAccountUser& out);
	bool        GetUser(const String& user_id, WebAccountUser& out);
	StoreResult VerifyPassword(const String& username, const String& password, WebAccountUser& out);
	StoreResult ChangePassword(const String& user_id, const String& old_password, const String& new_password);
	StoreResult SetPassword(const String& user_id, const String& new_password); // admin path
	StoreResult SetDisabled(const String& user_id, bool disabled);
	StoreResult ListUsers(Vector<WebAccountUser>& users);

	// --- sessions (site-stamped) ---
	StoreResult Login(const String& username, const String& password,
	                  const String& ip, String& session_id, String& csrf_token,
	                  WebAccountUser& user, String& error);
	bool        ResolveSession(const String& raw_session_id, WebAccountSession& out);
	StoreResult Logout(const String& raw_session_id);
	StoreResult SaveSessionData(const String& raw_session_id, const ValueMap& data);
	void        CleanupExpired();

	// --- new session without login (guest session, e.g. to carry a CSRF
	// token for anonymous forms). Still site-stamped. ---
	StoreResult CreateGuestSession(String& session_id, String& csrf_token);

	// --- TOTP second factor (meta-account app) ---
	// Generates a base32 secret + single-use recovery codes; enrollment is
	// only active after ConfirmTotpEnrollment verifies a live code.
	StoreResult BeginTotpEnrollment(const String& user_id, const String& password,
	                                String& secret_b32, Vector<String>& recovery_plain);
	StoreResult ConfirmTotpEnrollment(const String& user_id, const String& code);
	StoreResult DisableTotp(const String& user_id, const String& password);
	// Verifies a live TOTP code or consumes one single-use recovery code.
	bool        VerifyTotpOrRecovery(const String& user_id, const String& code);
	bool        TotpEnrolled(const String& user_id);

	static bool ValidUsername(const String& username);

private:
	JsonFileStore& Users();
	JsonFileStore& Sessions();

	String       site;
	String       root;
	int          ttl_hours;
	One<JsonFileStore> users_store;
	One<JsonFileStore> sessions_store;
	Mutex    mutex;
};

// Self-test helpers for host --test suites and the engine fence test.
namespace WebAccountsSelfTest {
	StoreResult RunAll(const String& root, String& report);
}

END_UPP_NAMESPACE

#endif
