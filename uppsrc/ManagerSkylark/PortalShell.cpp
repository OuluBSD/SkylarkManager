#include "ManagerSkylark.h"

NAMESPACE_UPP

String PortalLinkHost(const String& host_header)
{
	String host = TrimBoth(host_header);
	if(host.StartsWith("[")) {
		int e = host.Find(']');
		return e < 0 ? String("127.0.0.1") : host.Left(e + 1);
	}
	int colon = host.Find(':');
	if(colon >= 0)
		host = host.Left(colon);
	return host.IsEmpty() ? String("127.0.0.1") : host;
}

Vector<PortalWebapp> ListWebapps(const String& host_header, const Vector<EngineSiteListing>& sites)
{
	String host = PortalLinkHost(host_header);
	Vector<PortalWebapp> out;

	// The five stock hubs, each framed at its root; the first one is the default.
	static const char *hubs[][3] = {
		{ "ai", "Local AI Hub", "/ai" },
		{ "github", "GitHub Hub", "/github" },
		{ "sourceforge", "SourceForge Hub", "/sourceforge" },
		{ "host", "Self-Hosted", "/host" },
		{ "mesh", "Mesh-Hosted", "/mesh" },
	};
	for(const auto& hub : hubs) {
		PortalWebapp app;
		app.id = hub[0];
		app.title = hub[1];
		app.group = "webapp";
		app.mode = "frame";
		app.url = hub[2];
		out.Add(pick(app));
	}

	for(const EngineSiteListing& s : sites) {
		PortalWebapp app;
		app.id = s.id;
		app.title = s.title;
		app.group = "webapp";
		app.mode = "frame";
		app.url = Format("http://%s:%d/", host, s.port);
		out.Add(pick(app));
	}

	PortalWebapp settings;
	settings.id = "settings";
	settings.title = "Settings";
	settings.group = "settings";
	settings.mode = "frame";
	settings.url = "/host/settings";
	out.Add(pick(settings));
	return out;
}

String RenderShellAccountRow(const UserSession& sess, const String& return_url)
{
	String h;
	h << "<div class=\"wf-shell-account\" style=\"display:flex; flex-direction:column; gap:6px; font-size:0.85rem;\">\n";
	if(sess.is_authenticated) {
		h << "  <div style=\"display:flex; align-items:center; gap:8px;\">"
		  << "<span style=\"width:24px; height:24px; border-radius:50%; background:linear-gradient(135deg,#00a1c5,#6d55aa); display:inline-flex; align-items:center; justify-content:center; font-size:0.75rem; font-weight:700; color:#fff;\">"
		  << EscapeHtml(ToUpper(sess.username.Left(1))) << "</span>"
		  << "<strong>" << EscapeHtml(sess.username) << "</strong></div>\n"
		  << "  <div><a href=\"/logout\">Log out</a></div>\n";
	}
	else // the sign-in opens inside the frame, then returns to the selected webapp
		h << "  <div><a href=\"/login?next=" << EscapeHtml(UrlEncode(return_url))
		  << "\" target=\"portal-frame\" class=\"wf-btn wf-btn-primary\">Log in</a></div>\n";
	h << "</div>";
	return h;
}

String RenderPortalShell(const String& host_header, const String& selected, const UserSession& sess,
                         const Vector<EngineSiteListing>& sites)
{
	Vector<PortalWebapp> apps = ListWebapps(host_header, sites);
	int sel = 0;
	for(int i = 0; i < apps.GetCount(); i++)
		if(apps[i].id == selected)
			sel = i;

	TreeModel tree;
	TreeNode& webapps = tree.Add("grp-webapps", "Webapps", "", "layers");
	for(int i = 0; i < apps.GetCount(); i++)
		if(apps[i].group == "webapp")
			webapps.AddChild(apps[i].id, apps[i].title, "/?app=" + apps[i].id, "layers").Selected(i == sel);
	TreeNode& settings = tree.Add("grp-settings", "Settings", "", "settings");
	for(int i = 0; i < apps.GetCount(); i++)
		if(apps[i].group == "settings")
			settings.AddChild(apps[i].id, apps[i].title, "/?app=" + apps[i].id, "settings").Selected(i == sel);

	String frame;
	frame << "<style>.portal-frame { display:block; width:100%; height:calc(100vh - 3rem); border:0; }</style>\n"
	      << "<iframe class=\"portal-frame\" name=\"portal-frame\" src=\"" << EscapeHtml(apps[sel].url) << "\" title=\""
	      << EscapeHtml(apps[sel].title) << "\"></iframe>";

	String return_url = apps[sel].url.StartsWith("/") ? apps[sel].url : String("/");
	PageBuilder pb;
	pb.Title("Portal - Manager Skylark")
	  .Brand("Manager Skylark", "/", "cube")
	  .Template("sidebar")
	  .Theme("vivid-dark")
	  .Density(DENSITY_COMFORTABLE)
	  .LeftTree(tree, "Portal", "portal-tree")
	  .SidebarFooter(RenderShellAccountRow(sess, return_url))
	  .Content(frame);
	return pb.Build();
}

SKYLARK(PortalShellPage, "")
{
	http.ContentType("text/html; charset=utf-8") << RenderPortalShell(http.GetHeader("host"), http["app"],
	                                                               SystemAuthService::GetCurrentSession(http),
	                                                               TheWebEngine().ListSites());
}

// Hub roots (task 035): each hub's address opens its first page, which carries
// that hub's own navigator. The host hub's root is HostOverviewPage ("host").
SKYLARK(HubAiPage, "ai")
{
	http.Redirect("/chat");
}

SKYLARK(HubGitHubPage, "github")
{
	http.Redirect("/repo/SeppoPakonen");
}

SKYLARK(HubSourceForgePage, "sourceforge")
{
	http.Redirect("/projects/topside/");
}

SKYLARK(HubMeshPage, "mesh")
{
	http.Redirect("/host/nodes");
}

END_UPP_NAMESPACE
