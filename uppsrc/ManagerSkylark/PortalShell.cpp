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

// The rail icon of a composed site, by its id; unknown sites get a generic layers icon.
static String SiteIcon(const String& id)
{
	if(id == "atlas") return "layout";
	if(id == "science") return "book";
	if(id == "privatehub") return "lock";
	if(id == "taskfoundry") return "check";
	if(id == "meta") return "user";
	if(id == "channels") return "chat";
	if(id == "financials") return "database";
	return "layers";
}

static PortalWebapp SiteWebapp(const String& host, const EngineSiteListing& s)
{
	PortalWebapp app;
	app.id = s.id;
	app.title = s.title;
	app.group = "webapp";
	app.mode = "frame";
	app.icon = SiteIcon(s.id);
	app.url = Format("http://%s:%d/", host, s.port);
	return app;
}

Vector<PortalWebapp> ListWebapps(const String& host_header, const Vector<EngineSiteListing>& sites)
{
	String host = PortalLinkHost(host_header);
	Vector<PortalWebapp> out;

	// Atlas leads the list for now (task 036).
	for(const EngineSiteListing& s : sites)
		if(s.id == "atlas")
			out.Add(pick(SiteWebapp(host, s)));

	// The five stock hubs, each framed at its root.
	static const char *hubs[][4] = {
		{ "ai", "Local AI Hub", "/ai", "robot" },
		{ "github", "GitHub Hub", "/github", "github" },
		{ "sourceforge", "SourceForge Hub", "/sourceforge", "external-link" },
		{ "host", "Self-Hosted", "/host", "server" },
		{ "mesh", "Mesh-Hosted", "/mesh", "cpu" },
	};
	for(const auto& hub : hubs) {
		PortalWebapp app;
		app.id = hub[0];
		app.title = hub[1];
		app.group = "webapp";
		app.mode = "frame";
		app.icon = hub[3];
		app.url = hub[2];
		out.Add(pick(app));
	}

	for(const EngineSiteListing& s : sites)
		if(s.id != "atlas")
			out.Add(pick(SiteWebapp(host, s)));

	PortalWebapp settings;
	settings.id = "settings";
	settings.title = "Settings";
	settings.group = "settings";
	settings.mode = "frame";
	settings.icon = "settings";
	settings.url = "/host/settings";
	out.Add(pick(settings));
	return out;
}

String RenderShellAccountRow(const UserSession& sess, const String& return_url)
{
	String h;
	h << "<div class=\"portal-account-row\">";
	if(sess.is_authenticated) {
		h << "<span class=\"portal-avatar\">" << EscapeHtml(ToUpper(sess.username.Left(1))) << "</span>"
		  << "<span class=\"portal-user\">" << EscapeHtml(sess.username) << "</span>"
		  << "<a class=\"portal-account-link\" href=\"/logout\">Log out</a>";
	}
	else // the sign-in opens inside the frame, then returns to the selected webapp
		h << "<a class=\"portal-account-link portal-login\" href=\"/login?next=" << EscapeHtml(UrlEncode(return_url))
		  << "\" target=\"portal-frame\">Log in</a>";
	h << "</div>";
	return h;
}

// One link per webapp. The href is the webapp's own URL and the target is the
// frame, so a click changes only the frame; aria-current marks the selection.
static String PortalLink(const PortalWebapp& app, bool selected, const String& css_class)
{
	String h;
	h << "<a class=\"" << css_class << "\" href=\"" << EscapeHtml(app.url) << "\" target=\"portal-frame\" data-app=\""
	  << EscapeHtml(app.id) << "\"" << (selected ? " aria-current=\"page\"" : "") << " title=\"" << EscapeHtml(app.title) << "\">"
	  << VividIcon::Get(app.icon.IsEmpty() ? String("layers") : app.icon, 18);
	if(css_class == "portal-link")
		h << "<span>" << EscapeHtml(app.title) << "</span>";
	h << "</a>\n";
	return h;
}

// Styles, the loading and timeout fallback, and the selection script. The
// fallback is timeout and load-event only: a cross-origin frame cannot be read,
// so an HTTP error inside it is not detected from the shell.
static String PortalShellStyles()
{
	return
		"<style>\n"
		"  [hidden] { display: none !important; }\n"
		"  .portal-shell { display: flex; gap: 0.75rem; height: calc(100vh - 3.5rem); min-height: 480px; padding: 0.75rem; box-sizing: border-box; }\n"
		"  .portal-rail { width: 4rem; flex: 0 0 4rem; display: flex; flex-direction: column; align-items: center; gap: 0.5rem; padding: 0.75rem 0; background: #171b33; border-radius: 16px; }\n"
		"  .portal-rail-link { width: 2.75rem; height: 2.75rem; display: flex; align-items: center; justify-content: center; border-radius: 12px; color: #aab3dc; text-decoration: none; }\n"
		"  .portal-rail-link:hover { background: rgba(255,255,255,0.08); color: #ffffff; }\n"
		"  .portal-rail-link[aria-current=\"page\"] { background: #4a4fb8; color: #ffffff; }\n"
		"  .portal-panel { width: 10rem; flex: 0 0 10rem; display: flex; flex-direction: column; gap: 0.2rem; padding: 0.75rem 0.5rem; background: #1d2242; border-radius: 16px; overflow-y: auto; }\n"
		"  .portal-group { font-size: 0.68rem; text-transform: uppercase; letter-spacing: 0.08em; color: #8b93c4; padding: 0.5rem 0.5rem 0.25rem; }\n"
		"  .portal-link { display: flex; align-items: center; gap: 0.5rem; padding: 0.5rem 0.6rem; border-radius: 10px; color: #d6dcf5; text-decoration: none; font-size: 0.875rem; }\n"
		"  .portal-link:hover { background: rgba(255,255,255,0.07); }\n"
		"  .portal-link[aria-current=\"page\"] { background: #3a3f95; color: #ffffff; font-weight: 600; }\n"
		"  .portal-account { margin-top: auto; padding: 0.6rem 0.5rem 0.2rem; border-top: 1px solid rgba(255,255,255,0.08); }\n"
		"  .portal-account-row { display: flex; flex-wrap: wrap; align-items: center; gap: 0.4rem; font-size: 0.82rem; color: #d6dcf5; }\n"
		"  .portal-avatar { width: 24px; height: 24px; border-radius: 50%; background: linear-gradient(135deg,#00a1c5,#6d55aa); display: inline-flex; align-items: center; justify-content: center; font-size: 0.75rem; font-weight: 700; color: #ffffff; }\n"
		"  .portal-account-link { color: #aab3dc; text-decoration: none; }\n"
		"  .portal-account-link:hover { color: #ffffff; }\n"
		"  .portal-login { padding: 0.3rem 0.6rem; border-radius: 8px; background: #4a4fb8; color: #ffffff; }\n"
		"  .portal-main { position: relative; flex: 1 1 auto; min-width: 0; min-height: 420px; background: #0f1326; border-radius: 16px; overflow: hidden; }\n"
		"  .portal-frame { position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }\n"
		"  .portal-status { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 0.75rem; padding: 1rem; background: rgba(15,19,38,0.9); color: #d6dcf5; text-align: center; }\n"
		"  .portal-status button, .portal-status a { padding: 0.4rem 0.9rem; border-radius: 8px; border: 0; background: #4a4fb8; color: #ffffff; text-decoration: none; cursor: pointer; font-size: 0.85rem; }\n"
		"  @media (max-width: 640px) { .portal-panel { display: none; } .portal-rail { width: 3.25rem; flex-basis: 3.25rem; } }\n"
		"</style>\n";
}

static String PortalShellScript(const String& first_src)
{
	return
		"(function () {\n"
		"  var frame = document.getElementById('portal-frame');\n"
		"  var status = document.getElementById('portal-status');\n"
		"  var loading = status.querySelector('.portal-loading');\n"
		"  var slow = status.querySelector('.portal-slow');\n"
		"  var openLink = document.getElementById('portal-open');\n"
		"  var retry = document.getElementById('portal-retry');\n"
		"  var current = frame.getAttribute('src');\n"
		"  var timer = null;\n"
		"  function show(which) { status.hidden = false; loading.hidden = which !== 'loading'; slow.hidden = which !== 'slow'; }\n"
		"  function arm(url) {\n"
		"    current = url;\n"
		"    openLink.href = url;\n"
		"    clearTimeout(timer);\n"
		"    show('loading');\n"
		"    timer = setTimeout(function () { show('slow'); }, 15000);\n"
		"  }\n"
		"  function clear() { clearTimeout(timer); status.hidden = true; }\n"
		"  frame.addEventListener('load', clear);\n"
		"  frame.addEventListener('error', function () { clearTimeout(timer); show('slow'); });\n"
		"  retry.addEventListener('click', function () { arm(current); frame.src = current; });\n"
		"  document.addEventListener('click', function (e) {\n"
		"    var a = e.target.closest ? e.target.closest('a[target=\"portal-frame\"]') : null;\n"
		"    if (!a) return;\n"
		"    var id = a.getAttribute('data-app');\n"
		"    if (id) {\n"
		"      document.querySelectorAll('a[data-app]').forEach(function (el) {\n"
		"        if (el.getAttribute('data-app') === id) el.setAttribute('aria-current', 'page');\n"
		"        else el.removeAttribute('aria-current');\n"
		"      });\n"
		"      try { history.replaceState(null, '', '/?app=' + encodeURIComponent(id)); } catch (err) {}\n"
		"    }\n"
		"    arm(a.getAttribute('href'));\n"
		"  });\n"
		"  arm(" + AsCString(first_src) + ");\n"
		"})();\n";
}

String RenderPortalShell(const String& host_header, const String& selected, const UserSession& sess,
                         const Vector<EngineSiteListing>& sites)
{
	Vector<PortalWebapp> apps = ListWebapps(host_header, sites);
	int sel = 0;
	for(int i = 0; i < apps.GetCount(); i++)
		if(apps[i].id == selected)
			sel = i;

	String rail, panel;
	rail << "<nav class=\"portal-rail\" aria-label=\"Webapps\">\n";
	panel << "<nav class=\"portal-panel\" aria-label=\"Webapp list\">\n"
	      << "<div class=\"portal-group\">Webapps</div>\n";
	for(int i = 0; i < apps.GetCount(); i++)
		if(apps[i].group == "webapp") {
			rail << PortalLink(apps[i], i == sel, "portal-rail-link");
			panel << PortalLink(apps[i], i == sel, "portal-link");
		}
	panel << "<div class=\"portal-group\">Settings</div>\n";
	for(int i = 0; i < apps.GetCount(); i++)
		if(apps[i].group == "settings") {
			rail << PortalLink(apps[i], i == sel, "portal-rail-link");
			panel << PortalLink(apps[i], i == sel, "portal-link");
		}
	rail << "</nav>";
	panel << "<div class=\"portal-account\">" << RenderShellAccountRow(sess, apps[sel].url.StartsWith("/") ? apps[sel].url : String("/"))
	      << "</div>\n</nav>";

	String main;
	main << "<div class=\"portal-main\">\n"
	     << "  <iframe id=\"portal-frame\" class=\"portal-frame\" name=\"portal-frame\" src=\"" << EscapeHtml(apps[sel].url)
	     << "\" title=\"" << EscapeHtml(apps[sel].title) << "\"></iframe>\n"
	     << "  <div id=\"portal-status\" class=\"portal-status\" role=\"status\" hidden>\n"
	     << "    <div class=\"portal-loading\">Loading " << EscapeHtml(apps[sel].title) << "...</div>\n"
	     << "    <div class=\"portal-slow\" hidden>\n"
	     << "      <div>This webapp is taking a while.</div>\n"
	     << "      <div style=\"display:flex; gap:0.5rem; justify-content:center; margin-top:0.5rem;\">\n"
	     << "        <button type=\"button\" id=\"portal-retry\">Retry</button>\n"
	     << "        <a id=\"portal-open\" href=\"" << EscapeHtml(apps[sel].url) << "\" target=\"_blank\" rel=\"noopener\">Open in new tab</a>\n"
	     << "      </div>\n"
	     << "    </div>\n"
	     << "  </div>\n"
	     << "</div>";

	String content;
	content << "<div class=\"portal-shell\">\n" << rail << "\n" << panel << "\n" << main << "\n</div>";

	PageBuilder pb;
	pb.Title("Portal - Manager Skylark")
	  .Brand("Manager Skylark", "/", "cube")
	  .Template("megamenu")
	  .Theme("vivid-dark")
	  .Density(DENSITY_NONE)
	  .Header("<div style=\"padding:0.6rem 1rem; font-weight:700;\">Manager Skylark</div>")
	  .Content(content)
	  .ExtraHead(PortalShellStyles())
	  .ExtraScripts(PortalShellScript(apps[sel].url));
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
