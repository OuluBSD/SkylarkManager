#include "GitHubPlatform.h"
#include <GitHubForge/GitHubTemplates.h>
#include "GitHubForgeHost.h"
#include <WebFramework/WebFramework.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// SKYLARK Views: User Profile & Repository Parity
// ---------------------------------------------------------------------------

void RenderUserProfile(Http& http, const String& username)
{
	String uname = username.IsEmpty() ? "SeppoPakonen" : username;
	Vector<LocalRepoInfo> repos = GitHubPlatformService::GetLiveGitHubRepos(uname);

	String active_tab = http["tab"];
	if(active_tab.IsEmpty()) active_tab = "overview";

	String content;

	// User header / profile subnav
	Renderer subnav;
	subnav("uname", uname)
		("overview_active", String(active_tab == "overview" ? " active" : ""))
		("repositories_active", String(active_tab == "repositories" ? " active" : ""))
		("projects_active", String(active_tab == "projects" ? " active" : ""))
		("packages_active", String(active_tab == "packages" ? " active" : ""))
		("agents_active", String(active_tab == "agents" ? " active" : ""))
		("repo_count", repos.GetCount())
		("icon_book_open", VividIcon::Get("book-open", 16))
		("icon_git_repo", VividIcon::Get("git-repo", 16))
		("icon_layout_dashboard", VividIcon::Get("layout-dashboard", 16))
		("icon_package", VividIcon::Get("package", 16))
		("icon_robot", VividIcon::Get("robot", 16));
	String sub_nav = RenderGitHubTemplate(subnav, "github-user-subnav");


	Renderer none;
	if(active_tab == "overview") {
		const char* pinned_names[] = { "ai-upp-priv", "Manager", "nes-recompiled", "AR-AirHockey", "PulseVideoGen", "Shaders" };
		ValueArray pinned;
		for(int i = 0; i < __countof(pinned_names); i++) {
			LocalRepoInfo r = GitHubPlatformService::GetRepo(uname, pinned_names[i]);
			ValueMap row;
			row.Add("name", r.name);
			row.Add("icon", VividIcon::Get("git-repo", 16));
			row.Add("visibility", GitHubPlatformService::VisibilityLabel(r.is_private));
			row.Add("description", !r.description.IsEmpty() ? r.description : "Ultimate++ Agentic repository in ~/Dev/" + r.name);
			row.Add("language", r.language);
			pinned.Add(row);
		}
		Renderer vars;
		vars("uname", uname)("pinned", pinned);
		content << RenderGitHubTemplate(vars, "github-user-overview");
	}
	else if(active_tab == "repositories") {
		ValueArray rows;
		for(int i = 0; i < repos.GetCount(); i++) {
			const LocalRepoInfo& r = repos[i];
			ValueMap row;
			row.Add("name", r.name);
			row.Add("visibility", GitHubPlatformService::VisibilityLabel(r.is_private));
			row.Add("has_stars", r.stars > 0);
			row.Add("stars", r.stars);
			row.Add("has_forks", r.forks > 0);
			row.Add("forks", r.forks);
			row.Add("description", !r.description.IsEmpty() ? r.description : "~/Dev/" + r.name);
			row.Add("language", r.language);
			row.Add("updated", r.last_commit_time);
			rows.Add(row);
		}
		Renderer vars;
		vars("uname", uname)("repo_count", repos.GetCount())("repos", rows);
		content << RenderGitHubTemplate(vars, "github-user-repositories");
	}
	else if(active_tab == "projects")
		content << RenderGitHubTemplate(none, "github-user-projects");
	else if(active_tab == "packages")
		content << RenderGitHubTemplate(none, "github-user-packages");
	else if(active_tab == "agents")
		content << RenderGitHubTemplate(none, "github-user-agents");

	// Sidebar: Profile Card
	Renderer side;
	side("uname", uname)
		("repo_count", repos.GetCount())
		("icon_users", VividIcon::Get("users", 14))
		("icon_user", VividIcon::Get("user", 64))
		("icon_map_pin", VividIcon::Get("map-pin", 14))
		("icon_folder", VividIcon::Get("folder", 14))
		("icon_git_repo", VividIcon::Get("git-repo", 14));
	String sidebar = RenderGitHubTemplate(side, "github-user-sidebar");


	GitHubForgeHost& host = GetGitHubForgeHost();

	PageBuilder pb;
	pb.Title(uname + " (Seppo Pakonen) - GitHub Platform Parity")
	  .Brand("GitHub", "/", "octicon-mark-github")
	  .Navigation(host.build_navigation("gh_profile"))
	  .NavActions(host.render_user_menu(http))
	  .Template("github")
	  .Theme("github-dark")
	  .Density(DENSITY_COMFORTABLE)
	  .SubNav(sub_nav)
	  .Sidebar(sidebar)
	  .Content(content);
	host.add_markdown_support(pb);
	String html = pb.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

END_UPP_NAMESPACE
