#ifndef _GitHubForge_GitHubForgeHost_h_
#define _GitHubForge_GitHubForgeHost_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include <Manager/Manager.h>
#include <WebFramework/WebFramework.h>

NAMESPACE_UPP

// Host-owned services used by the GitHub UI. The forge package stays independent
// of any executable package; the application installs these callbacks at startup.
struct GitHubForgeHost {
	Function<NavigationModel(const String&)> build_navigation;
	Function<String(Http&)> render_user_menu;
	Function<bool(Http&)> is_admin;
	Function<void(PageBuilder&)> add_markdown_support;
	Function<ManagerStore*()> get_manager_store;
	Function<String(const String&, const String&, const String&, const String&, const String&)> render_agent_dashboard;
};

GitHubForgeHost& GetGitHubForgeHost();

END_UPP_NAMESPACE

#endif
