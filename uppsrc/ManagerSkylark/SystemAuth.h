#ifndef _reference_ManagerSkylark_SystemAuth_h_
#define _reference_ManagerSkylark_SystemAuth_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include <WebAudit/WebAudit.h>

NAMESPACE_UPP

struct UserSession : Moveable<UserSession> {
	String token;
	String username;
	String github_username;
	String sf_username;
	String display_name;
	String home_dir;
	String audit_ref; // non-bearer reference; deliberately omitted from Jsonize
	Time   created_at;
	Time   expires_at;
	bool   is_authenticated = false;
	bool   is_admin = false;
	bool   is_demo = false;

	void Jsonize(JsonIO& io) {
		io("token", token)("username", username)("github_username", github_username)
		  ("sf_username", sf_username)("display_name", display_name)
		  ("home_dir", home_dir)("created_at", created_at)("expires_at", expires_at)
		  ("is_authenticated", is_authenticated)("is_admin", is_admin)("is_demo", is_demo);
	}
};

struct PortalAuditSessionSnapshot : Moveable<PortalAuditSessionSnapshot> {
	String user_id;
	String session_ref;
	Time created;
	Time expires;
};

bool SystemAuthSelfTests(const String& scratch_dir);

class SystemAuthService {
public:
	static void ConfigureSite(const String& site_dir, bool demo_site);
	static bool SetSitePassword(const String& site_dir, const String& username, const String& password);
	static bool IsDemoSite();
	static bool AuthorizeRequest(Http& http, const UserSession *resolved = NULL, int *rejection_status = NULL);
	static void SetRootAuditContext(const Http& http, const UserSession& session, bool allowed);
	static void RecordRootRequestComplete(const Http& http, int duration_ms);
	static WebAuditLog& AuditLog();
	static StoreResult ListAuditSessions(int offset, int limit, Vector<PortalAuditSessionSnapshot>& out);
	static bool VerifySystemPassword(const String& username, const String& password);
	static UserSession CreateSession(const String& username, const String& source = "password");
	static UserSession GetCurrentSession(const Http& http);
	static bool Login(Http& http, const String& username, const String& password);
	static UserSession LoginSession(Http& http, const String& username, const String& password);
	// Portal bridge to MetaAccount (task 050): adopts the host session of the host account that has the
	// central user's username. Never creates an account; returns a guest when nothing matches.
	static UserSession BridgeMetaSession(Http& http, bool ignore_suppression, String* central_user = nullptr);
	static void SetHostSsoSuppressed(Http& http, bool on);
	static bool HasSiteAccount(const String& username);
	static UserSession AdoptHostAccount(const String& username, const String& source = "local");
	static void Logout(Http& http);
	static bool RequireAuth(Http& http);
	static String RenderLoginForm(const String& redirect_url = "", const String& error_msg = "");
	static String RenderUserMenu(const UserSession& session);

	static String ExtractCookie(const Http& http, const char* name);

private:
	static VectorMap<String, UserSession>& Sessions();
};

END_UPP_NAMESPACE

#endif
