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

	PortalWebapp manager;
	manager.id = "manager";
	manager.title = "Manager";
	manager.group = "webapp";
	manager.mode = "frame";
	manager.url = "/home";
	out.Add(pick(manager));

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

String RenderShellAccountRow(const UserSession& sess)
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
	else
		h << "  <div><a href=\"/login\" class=\"wf-btn wf-btn-primary\">Log in</a></div>\n";
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
	      << "<iframe class=\"portal-frame\" src=\"" << EscapeHtml(apps[sel].url) << "\" title=\""
	      << EscapeHtml(apps[sel].title) << "\"></iframe>";

	PageBuilder pb;
	pb.Title("Portal - Manager Skylark")
	  .Brand("Manager Skylark", "/", "cube")
	  .Template("sidebar")
	  .Theme("vivid-dark")
	  .Density(DENSITY_COMFORTABLE)
	  .LeftTree(tree, "Portal", "portal-tree")
	  .SidebarFooter(RenderShellAccountRow(sess))
	  .Content(frame);
	return pb.Build();
}

SKYLARK(PortalShellPage, "")
{
	http.ContentType("text/html; charset=utf-8") << RenderPortalShell(http.GetHeader("host"), http["app"],
	                                                               SystemAuthService::GetCurrentSession(http),
	                                                               TheWebEngine().ListSites());
}

END_UPP_NAMESPACE
