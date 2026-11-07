#ifndef _reference_ManagerSkylark_SystemAuth_h_
#define _reference_ManagerSkylark_SystemAuth_h_

#include "ManagerSkylark.h"

NAMESPACE_UPP

struct UserSession : Moveable<UserSession> {
	String token;
	String username;
	String github_username;
	String sf_username;
	String display_name;
	String home_dir;
	Time   created_at;
	Time   expires_at;
	bool   is_authenticated = false;
	bool   is_admin = false;

	void Jsonize(JsonIO& io) {
		// Never serialize the bearer token or local home path into JSON.
		io("username", username)("github_username", github_username)
		  ("sf_username", sf_username)("display_name", display_name)
		  ("created_at", created_at)("expires_at", expires_at)
		  ("is_authenticated", is_authenticated)("is_admin", is_admin);
	}
};

class SystemAuthService {
public:
	static bool VerifySystemPassword(const String& username, const String& password);
	static UserSession GetCurrentSession(const Http& http);
	static bool Login(Http& http, const String& username, const String& password);
	static UserSession LoginSession(Http& http, const String& username, const String& password);
	static ManagerSkylarkApp::BatchFinalizationResult FinalizeBatchForSession(
		ManagerSkylarkApp& app, const UserSession& session, const String& source_id,
		const String& workspace_id, const String& operation_id,
		const Vector<DataBatchChange>& changes);
	static bool AuthorizeWriteForSession(ManagerSkylarkApp& app, const UserSession& session,
	                                     const String& source_id, const String& workspace_id);
	static bool AuthorizeWriteForSession(ManagerSkylarkApp& app, const UserSession& session,
	                                     const String& workspace_id);
	static void Logout(Http& http);
	static bool RequireAuth(Http& http);
	static bool AuthorizeRequest(Http& http, const char *route_path = NULL);
	static String CurrentOsUsername();
	static String RenderLoginForm(const String& redirect_url = "", const String& error_msg = "");
	static String RenderUserMenu(const UserSession& session);

private:
	friend class ManagerSkylarkApp;
	static UserSession CreateSession(const String& username);
	static UserSession CreateTestSessionForTests();
	static String ExtractCookie(const Http& http, const char* name);
	static void ExpireSessionForTest(const String& token);
};

END_UPP_NAMESPACE

#endif
