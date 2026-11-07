#include "WebAccounts.h"
#include "WebAccountsTotp.h"
#include <WebAuth/WebAuthCrypto.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// SiteAccounts
// ---------------------------------------------------------------------------

SiteAccounts::SiteAccounts(const String& site_id, const String& data_root, int session_ttl_hours_)
{
	ASSERT_(site_id.GetCount(), "SiteAccounts needs a site id");
	site = site_id;
	root = data_root;
	ttl_hours = session_ttl_hours_ > 0 ? session_ttl_hours_ : 12;
	users_store.Create(site, root, "users");
	sessions_store.Create(site, root, "sessions");
}

String SiteAccounts::CookieName() const
{
	// Cookie names must be filesystem/HTTP safe; site ids are restricted ids.
	StringBuffer sb;
	sb << site;
	for(int i = 0; i < sb.GetCount(); i++)
		if(!IsAlpha(sb[i]) && !IsDigit(sb[i]) && sb[i] != '_' && sb[i] != '-')
			sb[i] = '_';
	return "wfs_" + String(sb);
}

JsonFileStore& SiteAccounts::Users()      { return *users_store; }
JsonFileStore& SiteAccounts::Sessions()   { return *sessions_store; }

bool SiteAccounts::ValidUsername(const String& username)
{
	if(username.IsEmpty() || username.GetCount() > 64)
		return false;
	for(int i = 0; i < username.GetCount(); i++) {
		int c = username[i];
		bool alnum = IsAlpha(c) || IsDigit(c);
		if(!alnum && c != '.' && c != '_' && c != '-' && c != '@')
			return false;
	}
	return true;
}

StoreResult SiteAccounts::CreateUser(const String& username, const String& password,
                                     const String& display_name, const Vector<String>& roles,
                                     String& user_id, bool& existed)
{
	existed = false;
	if(!ValidUsername(username))
		return StoreResult("invalid username '" + username + "'");
	if(password.IsEmpty())
		return StoreResult("password must not be empty");
	mutex.Enter();
	Vector<String> ids;
	Users().List(ids);
	String lower = ToLower(username);
	for(const String& id : ids) {
		String json, rev;
		if(!Users().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(v.Is<ValueMap>()) {
				WebAccountUser u;
				LoadFromJsonValue(u, v);
				if(ToLower(u.username) == lower) {
					existed = true;
					user_id = u.user_id;
					mutex.Leave();
					return StoreResult("username already exists on site '" + site + "'");
				}
			}
		}
		catch(...) {}
	}
	WebAccountUser u;
	u.user_id = "wau_" + AsString(Uuid::Create());
	u.username = username;
	u.display_name = display_name.GetCount() ? display_name : username;
	u.password_hash = WebAuthHashPassword(password);
	u.roles <<= roles;
	u.disabled = false;
	u.created = GetSysTime();
	u.updated = GetSysTime();
	String new_rev;
	StoreResult r = Users().Put(u.user_id, StoreAsJson(u, true), String(), new_rev);
	if(r.ok)
		user_id = u.user_id;
	mutex.Leave();
	return r;
}

bool SiteAccounts::FindUserByUsername(const String& username, WebAccountUser& out)
{
	if(!ValidUsername(username))
		return false;
	mutex.Enter();
	Vector<String> ids;
	Users().List(ids);
	String lower = ToLower(username);
	for(const String& id : ids) {
		String json, rev;
		if(!Users().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(v.Is<ValueMap>()) {
				WebAccountUser u;
				LoadFromJsonValue(u, v);
				if(ToLower(u.username) == lower) {
					out = u;
					mutex.Leave();
					return true;
				}
			}
		}
		catch(...) {}
	}
	mutex.Leave();
	return false;
}

bool SiteAccounts::GetUser(const String& user_id, WebAccountUser& out)
{
	String json, rev;
	if(!Users().Get(user_id, json, rev).ok)
		return false;
	try {
		Value v = ParseJSON(json);
		if(!v.Is<ValueMap>())
			return false;
		LoadFromJsonValue(out, v);
		return true;
	}
	catch(...) {
		return false;
	}
}

StoreResult SiteAccounts::VerifyPassword(const String& username, const String& password, WebAccountUser& out)
{
	WebAccountUser u;
	if(!FindUserByUsername(username, u))
		return StoreResult("unknown user '" + username + "'");
	if(u.disabled)
		return StoreResult("user '" + username + "' is disabled");
	if(!WebAuthVerifyPassword(password, u.password_hash))
		return StoreResult("bad password for user '" + username + "'");
	out = u;
	return StoreResult(true);
}

StoreResult SiteAccounts::SetPassword(const String& user_id, const String& new_password)
{
	if(new_password.IsEmpty())
		return StoreResult("password must not be empty");
	WebAccountUser u;
	if(!GetUser(user_id, u))
		return StoreResult("unknown user");
	u.password_hash = WebAuthHashPassword(new_password);
	u.updated = GetSysTime();
	String json, rev;
	Users().Get(user_id, json, rev);
	return Users().Put(user_id, StoreAsJson(u, true), rev, rev);
}

StoreResult SiteAccounts::ChangePassword(const String& user_id, const String& old_password, const String& new_password)
{
	WebAccountUser u;
	if(!GetUser(user_id, u))
		return StoreResult("unknown user");
	if(!WebAuthVerifyPassword(old_password, u.password_hash))
		return StoreResult("old password does not match");
	return SetPassword(user_id, new_password);
}

StoreResult SiteAccounts::SetDisabled(const String& user_id, bool disabled)
{
	WebAccountUser u;
	if(!GetUser(user_id, u))
		return StoreResult("unknown user");
	u.disabled = disabled;
	u.updated = GetSysTime();
	String json, rev;
	Users().Get(user_id, json, rev);
	return Users().Put(user_id, StoreAsJson(u, true), rev, rev);
}

StoreResult SiteAccounts::ListUsers(Vector<WebAccountUser>& users)
{
	users.Clear();
	mutex.Enter();
	Vector<String> ids;
	Users().List(ids);
	for(const String& id : ids) {
		String json, rev;
		if(!Users().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(v.Is<ValueMap>()) {
				WebAccountUser u;
				LoadFromJsonValue(u, v);
				users.Add(u);
			}
		}
		catch(...) {}
	}
	mutex.Leave();
	return StoreResult(true);
}

StoreResult SiteAccounts::CreateGuestSession(String& session_id, String& csrf_token)
{
	WebAccountSession s;
	s.session_id = "was_" + AsString(Uuid::Create());
	s.site = site;
	s.user_id = String();
	s.csrf_token = WebAuthRandomBytes(32);
	s.created = GetSysTime();
	s.expires = s.created + 24 * 3600;
	String new_rev;
	StoreResult r = Sessions().Put(s.session_id, StoreAsJson(s, true), String(), new_rev);
	if(r.ok) {
		session_id = s.session_id;
		csrf_token = s.csrf_token;
	}
	return r;
}

StoreResult SiteAccounts::Login(const String& username, const String& password,
                                const String& ip, String& session_id, String& csrf_token,
                                WebAccountUser& user, String& error)
{
	StoreResult vr = VerifyPassword(username, password, user);
	if(!vr.ok) {
		error = vr.error;
		return vr;
	}
	WebAccountSession s;
	s.session_id = "was_" + AsString(Uuid::Create());
	s.site = site;
	s.user_id = user.user_id;
	s.csrf_token = WebAuthRandomBytes(32);
	s.created = GetSysTime();
	s.expires = s.created + ttl_hours * 3600;
	s.data.Add("last_ip", ip);
	String new_rev;
	StoreResult r = Sessions().Put(s.session_id, StoreAsJson(s, true), String(), new_rev);
	if(r.ok) {
		session_id = s.session_id;
		csrf_token = s.csrf_token;
	}
	else
		error = r.error;
	return r;
}

bool SiteAccounts::ResolveSession(const String& raw_session_id, WebAccountSession& out)
{
	if(raw_session_id.IsEmpty())
		return false;
	String json, rev;
	if(!Sessions().Get(raw_session_id, json, rev).ok)
		return false;
	try {
		Value v = ParseJSON(json);
		if(!v.Is<ValueMap>())
			return false;
		WebAccountSession s;
		LoadFromJsonValue(s, v);
		// Fence 1: the record must belong to THIS site. A cookie copied to
		// another site/port is rejected here even if the raw id collides.
		if(s.site != site)
			return false;
		if(s.expires < GetSysTime())
			return false;
		out = s;
		return true;
	}
	catch(...) {
		return false;
	}
}

StoreResult SiteAccounts::Logout(const String& raw_session_id)
{
	if(raw_session_id.IsEmpty())
		return StoreResult(true);
	String json, rev;
	if(!Sessions().Get(raw_session_id, json, rev).ok)
		return StoreResult(true); // already gone: idempotent
	return Sessions().Delete(raw_session_id, rev);
}

StoreResult SiteAccounts::SaveSessionData(const String& raw_session_id, const ValueMap& data)
{
	String json, rev;
	if(!Sessions().Get(raw_session_id, json, rev).ok)
		return StoreResult("unknown session");
	try {
		Value v = ParseJSON(json);
		if(!v.Is<ValueMap>())
			return StoreResult("corrupt session record");
		WebAccountSession s;
		LoadFromJsonValue(s, v);
		s.data = data;
		String new_rev;
		return Sessions().Put(raw_session_id, StoreAsJson(s, true), rev, new_rev);
	}
	catch(Exc& e) {
		return StoreResult(String("cannot update session: ") + e);
	}
}

void SiteAccounts::CleanupExpired()
{
	mutex.Enter();
	Vector<String> ids;
	Sessions().List(ids);
	Time now = GetSysTime();
	for(const String& id : ids) {
		String json, rev;
		if(!Sessions().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			WebAccountSession s;
			LoadFromJsonValue(s, v);
			if(s.expires < now)
				Sessions().Delete(id, rev);
		}
		catch(...) {}
	}
	mutex.Leave();
}


// ---------------------------------------------------------------------------
// TOTP / recovery codes (meta-account second factor)
// ---------------------------------------------------------------------------

StoreResult SiteAccounts::BeginTotpEnrollment(const String& user_id, const String& password,
                                              String& secret_b32, Vector<String>& recovery_plain)
{
	recovery_plain.Clear();
	WebAccountUser u;
	if(!GetUser(user_id, u))
		return StoreResult("unknown user");
	if(u.disabled)
		return StoreResult("user is disabled");
	if(!WebAuthVerifyPassword(password, u.password_hash))
		return StoreResult("password does not match");
	secret_b32 = TotpSecretGenerate();
	Vector<String> codes = RecoveryCodesGenerate();
	recovery_plain <<= codes;
	WebAccountUser upd = u;
	upd.totp_secret = secret_b32;
	upd.totp_enrolled = false;
	upd.recovery_hashes.Clear();
	String pepper = user_id + ":" + upd.totp_secret;
	for(const String& c : codes)
		upd.recovery_hashes.Add(RecoveryCodeHash(pepper, c));
	upd.updated = GetSysTime();
	String json, rev;
	Users().Get(user_id, json, rev);
	return Users().Put(user_id, StoreAsJson(upd, true), rev, rev);
}

StoreResult SiteAccounts::ConfirmTotpEnrollment(const String& user_id, const String& code)
{
	WebAccountUser u;
	if(!GetUser(user_id, u))
		return StoreResult("unknown user");
	if(u.totp_secret.IsEmpty())
		return StoreResult("no pending enrollment");
	if(!TotpVerify(u.totp_secret, code, TotpUnixNow()))
		return StoreResult("code does not match");
	WebAccountUser upd = u;
	upd.totp_enrolled = true;
	upd.updated = GetSysTime();
	String json, rev;
	Users().Get(user_id, json, rev);
	return Users().Put(user_id, StoreAsJson(upd, true), rev, rev);
}

StoreResult SiteAccounts::DisableTotp(const String& user_id, const String& password)
{
	WebAccountUser u;
	if(!GetUser(user_id, u))
		return StoreResult("unknown user");
	if(!WebAuthVerifyPassword(password, u.password_hash))
		return StoreResult("password does not match");
	WebAccountUser upd = u;
	upd.totp_secret.Clear();
	upd.totp_enrolled = false;
	upd.recovery_hashes.Clear();
	upd.updated = GetSysTime();
	String json, rev;
	Users().Get(user_id, json, rev);
	return Users().Put(user_id, StoreAsJson(upd, true), rev, rev);
}

bool SiteAccounts::VerifyTotpOrRecovery(const String& user_id, const String& code)
{
	WebAccountUser u;
	if(!GetUser(user_id, u) || u.totp_secret.IsEmpty())
		return false;
	if(TotpVerify(u.totp_secret, code, TotpUnixNow()))
		return true;
	// Single-use recovery code: consume the matching hash.
	String pepper = user_id + ":" + u.totp_secret;
	for(int i = 0; i < u.recovery_hashes.GetCount(); i++) {
		if(RecoveryCodeVerify(pepper, u.recovery_hashes[i], code)) {
			WebAccountUser upd = u;
			upd.recovery_hashes.Remove(i);
			upd.updated = GetSysTime();
			String json, rev;
			if(Users().Get(user_id, json, rev).ok)
				Users().Put(user_id, StoreAsJson(upd, true), rev, rev);
			return true;
		}
	}
	return false;
}

bool SiteAccounts::TotpEnrolled(const String& user_id)
{
	WebAccountUser u;
	return GetUser(user_id, u) && u.totp_enrolled;
}

// ---------------------------------------------------------------------------
// Self tests
// ---------------------------------------------------------------------------

namespace WebAccountsSelfTest {

static bool Check(Vector<String>& report, bool cond, const String& what)
{
	report << (cond ? "ok   " : "FAIL ") << what << "\n";
	return cond;
}

StoreResult RunAll(const String& root, String& report)
{
	Vector<String> rep;
	bool all = true;
	String site_a = "selftest_a", site_b = "selftest_b";
	SiteAccounts a(site_a, AppendFileName(root, "a"), 12), b(site_b, AppendFileName(root, "b"), 12);

	all &= Check(rep, a.CookieName() == "wfs_selftest_a", "cookie name is site-specific");
	all &= Check(rep, a.CookieName() != b.CookieName(), "cookie names differ between sites");

	String ua, ub;
	bool ex;
	all &= Check(rep, a.CreateUser("alice", "pw-a", "Alice A", {"member"}, ua, ex).ok, "alice registered on site A");
	all &= Check(rep, b.CreateUser("alice", "pw-b", "Alice B", {"member"}, ub, ex).ok, "alice registered on site B");
	all &= Check(rep, ua != ub, "same username has different user ids per site");

	WebAccountUser chk;
	{
		StoreResult vr = a.VerifyPassword("alice", "pw-a", chk);
		if(!vr.ok)
			rep << "     verify err: " << vr.error << "\n";
		all &= Check(rep, vr.ok, "site A alice verifies with pw-a");
	}
	all &= Check(rep, !a.VerifyPassword("alice", "pw-b", chk).ok, "site A alice rejects pw-b");
	all &= Check(rep, b.VerifyPassword("alice", "pw-b", chk).ok, "site B alice verifies with pw-b");

	String sa, ca, sb, cb, err;
	{
		StoreResult lr = a.Login("alice", "pw-a", "127.0.0.1", sa, ca, chk, err);
		if(!lr.ok)
			rep << "     login err: " << err << "\n";
		all &= Check(rep, lr.ok, "login on site A");
	}
	all &= Check(rep, b.Login("alice", "pw-b", "127.0.0.1", sb, cb, chk, err).ok, "login on site B");
	all &= Check(rep, sa != sb, "session ids differ between sites");

	WebAccountSession sess;
	all &= Check(rep, a.ResolveSession(sa, sess) && sess.site == site_a, "session A resolves on site A");
	all &= Check(rep, !b.ResolveSession(sa, sess), "session A cookie rejected by site B (site stamp)");
	all &= Check(rep, !a.ResolveSession(sb, sess), "session B cookie rejected by site A");

	all &= Check(rep, a.Logout(sa).ok, "logout A");
	all &= Check(rep, !a.ResolveSession(sa, sess), "logged-out session no longer resolves");

	String guest, gcsrf;
	all &= Check(rep, a.CreateGuestSession(guest, gcsrf).ok && gcsrf.GetCount() >= 32, "guest session with csrf token");

	report = Join(rep, "");
	return all ? StoreResult(true) : StoreResult("WebAccounts self test failures:\n" + report);
}

}

END_UPP_NAMESPACE
