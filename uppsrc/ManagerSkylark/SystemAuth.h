#ifndef _reference_ManagerSkylark_SystemAuth_h_
#define _reference_ManagerSkylark_SystemAuth_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>

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
	bool   is_demo = false;

	void Jsonize(JsonIO& io) {
		io("token", token)("username", username)("github_username", github_username)
		  ("sf_username", sf_username)("display_name", display_name)
		  ("home_dir", home_dir)("created_at", created_at)("expires_at", expires_at)
		  ("is_authenticated", is_authenticated)("is_admin", is_admin)("is_demo", is_demo);
	}
};

class SystemAuthService {
public:
	static void ConfigureSite(const String& site_dir, bool demo_site);
	static bool SetSitePassword(const String& site_dir, const String& username, const String& password);
	static bool IsDemoSite();
	static bool AuthorizeRequest(Http& http);
	static bool VerifySystemPassword(const String& username, const String& password);
	static UserSession CreateSession(const String& username);
	static UserSession GetCurrentSession(const Http& http);
	static bool Login(Http& http, const String& username, const String& password);
	static UserSession LoginSession(Http& http, const String& username, const String& password);
	static void Logout(Http& http);
	static bool RequireAuth(Http& http);
	static String RenderLoginForm(const String& redirect_url = "", const String& error_msg = "");
	static String RenderUserMenu(const UserSession& session);

private:
	static VectorMap<String, UserSession>& Sessions();
	static String ExtractCookie(const Http& http, const char* name);
};

END_UPP_NAMESPACE

#endif
