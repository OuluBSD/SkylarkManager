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
	if(id == "youtubeplanner") return "file-text";
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
	else if(!return_url.IsEmpty()) // a stock webapp's sign-in opens inside the frame and returns to it;
		h << "<a class=\"portal-account-link portal-login\" href=\"/login?next=" << EscapeHtml(UrlEncode(return_url))
		  << "\" target=\"portal-frame\">Log in</a>";
	// A composed webapp has its own sign-in inside its frame; a stock sign-in there would
	// return to "/" inside the frame and nest the portal in itself (task 036 audit).
	h << "</div>";
	return h;
}

// One rail link per webapp. The icon sits in a fixed box, and the label shows only while the rail is
// open, so the icon never moves. The href is the webapp's own URL and the target is the frame, so a
// click changes only the frame; aria-current marks the selection.
static String PortalLink(const PortalWebapp& app, bool selected)
{
	String h;
	h << "<a class=\"portal-rail-link\" href=\"" << EscapeHtml(app.url) << "\" target=\"portal-frame\" data-app=\""
	  << EscapeHtml(app.id) << "\"" << (selected ? " aria-current=\"page\"" : "") << " title=\"" << EscapeHtml(app.title) << "\">"
	  << VividIcon::Get(app.icon.IsEmpty() ? String("layers") : app.icon, 18)
	  << "<span class=\"portal-rail-label\">" << EscapeHtml(app.title) << "</span></a>\n";
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
		"  .portal-shell { position: relative; display: flex; gap: 0.75rem; height: calc(100vh - 3.5rem); min-height: 480px; padding: 0.75rem; box-sizing: border-box; --rail-w: 4rem; --rail-w-open: 10rem; --rail-m: 0.625rem; }\n"
	"  /* One rail. It keeps its column at rest and widens over the frame on hover, focus or the toggle: the icons stay put, labels appear beside them. */\n"
	"  .portal-rail-slot { position: relative; flex: 0 0 var(--rail-w); width: var(--rail-w); }\n"
	"  .portal-rail { position: absolute; top: 0; bottom: 0; left: 0; z-index: 20; width: var(--rail-w); box-sizing: border-box; display: flex; flex-direction: column; gap: 0.5rem; padding: 0.75rem 0; overflow-x: hidden; overflow-y: auto; background: #171b33; border-radius: 16px; transition: width 0.15s ease; }\n"
	"  .portal-rail:hover, .portal-rail:focus-within, .portal-shell.portal-open .portal-rail { width: var(--rail-w-open); box-shadow: 0 8px 24px rgba(0,0,0,0.35); }\n"
	"  .portal-rail-link, .portal-rail-toggle { flex: 0 0 auto; box-sizing: border-box; width: 2.75rem; height: 2.75rem; margin: 0 var(--rail-m); display: flex; align-items: center; justify-content: flex-start; padding: 0 0 0 0.8125rem; border: 0; border-radius: 12px; background: transparent; color: #aab3dc; font: inherit; font-size: 1.25rem; text-decoration: none; cursor: pointer; white-space: nowrap; overflow: hidden; transition: width 0.15s ease; }\n"
	"  .portal-rail:hover .portal-rail-link, .portal-rail:hover .portal-rail-toggle, .portal-rail:focus-within .portal-rail-link, .portal-shell.portal-open .portal-rail-link, .portal-shell.portal-open .portal-rail-toggle { width: calc(var(--rail-w-open) - 2 * var(--rail-m)); }\n"
	"  .portal-rail-link:hover, .portal-rail-toggle:hover, .portal-rail-toggle:focus-visible { background: rgba(255,255,255,0.08); color: #ffffff; }\n"
	"  .portal-rail-link[aria-current=\"page\"] { background: #4a4fb8; color: #ffffff; }\n"
	"  .portal-rail-label { display: none; margin-left: 0.5rem; font-size: 0.875rem; }\n"
	"  .portal-rail:hover .portal-rail-label, .portal-rail:focus-within .portal-rail-label, .portal-shell.portal-open .portal-rail-label { display: inline; }\n"
	"  .portal-rail-link svg { flex: 0 0 auto; }\n"
	"  .portal-rail-sep { flex: 0 0 auto; height: 1px; margin: 0.25rem 0.75rem; background: rgba(255,255,255,0.08); }\n"
	"  .portal-account { display: none; margin-top: auto; padding: 0.6rem 0.5rem 0.2rem; border-top: 1px solid rgba(255,255,255,0.08); }\n"
	"  .portal-rail:hover .portal-account, .portal-rail:focus-within .portal-account, .portal-shell.portal-open .portal-account { display: block; }\n"
	"  .portal-shell.portal-open .portal-account { display: block; }\n"
	"  .portal-account-row { display: flex; flex-wrap: wrap; align-items: center; gap: 0.4rem; font-size: 0.82rem; color: #d6dcf5; }\n"
	"  .portal-avatar { width: 24px; height: 24px; border-radius: 50%; background: linear-gradient(135deg,#00a1c5,#6d55aa); display: inline-flex; align-items: center; justify-content: center; font-size: 0.75rem; font-weight: 700; color: #ffffff; }\n"
	"  .portal-account-link { color: #aab3dc; text-decoration: none; }\n"
	"  .portal-account-link:hover { color: #ffffff; }\n"
	"  .portal-login { padding: 0.3rem 0.6rem; border-radius: 8px; background: #4a4fb8; color: #ffffff; }\n"
	"  .portal-main { position: relative; flex: 1 1 auto; min-width: 0; min-height: 420px; background: #0f1326; border-radius: 16px; overflow: hidden; }\n"
	"  .portal-frame { position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }\n"
	"  .portal-status { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 0.75rem; padding: 1rem; background: rgba(15,19,38,0.9); color: #d6dcf5; text-align: center; }\n"
	"  .portal-status button, .portal-status a { padding: 0.4rem 0.9rem; border-radius: 8px; border: 0; background: #4a4fb8; color: #ffffff; text-decoration: none; cursor: pointer; font-size: 0.85rem; }\n"
	"  @media (max-width: 640px) { .portal-shell { --rail-w: 3.25rem; --rail-w-open: 9rem; --rail-m: 0.25rem; } }\n"
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
		"  var shell = document.querySelector('.portal-shell');\n"
		"  var toggle = document.getElementById('portal-rail-toggle');\n"
		"  function setOpen(open) {\n"
		"    shell.classList.toggle('portal-open', open);\n"
		"    toggle.setAttribute('aria-expanded', open ? 'true' : 'false');\n"
		"    toggle.setAttribute('aria-label', open ? 'Hide webapp names' : 'Show webapp names');\n"
		"  }\n"
		"  toggle.addEventListener('click', function () { setOpen(!shell.classList.contains('portal-open')); });\n"
		"  function inOverlay(node) { return !!(node && node.closest && node.closest('.portal-rail')); }\n"
		"  document.addEventListener('focusin', function (e) { setOpen(inOverlay(e.target)); });\n"
		"  frame.addEventListener('focus', function () { setOpen(false); });\n"
		"  window.addEventListener('blur', function () { setOpen(false); });\n"
		"  document.addEventListener('keydown', function (e) {\n"
		"    if (e.key === 'Escape' && shell.classList.contains('portal-open')) { toggle.focus(); setOpen(false); }\n"
		"  });\n"
		"  document.addEventListener('click', function (e) {\n"
		"    if (!inOverlay(e.target)) setOpen(false);\n"
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
		"    toggle.focus();\n"
		"    setOpen(false);\n"
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

	// One rail, one list of links. The rail keeps its column at rest and widens over the frame; no
	// second copy of the links exists, so no icon is drawn twice.
	String rail;
	rail << "<div class=\"portal-rail-slot\"><nav class=\"portal-rail\" aria-label=\"Webapps\">\n"
	     << "<button type=\"button\" id=\"portal-rail-toggle\" class=\"portal-rail-toggle\" aria-expanded=\"false\""
	     << " aria-label=\"Show webapp names\" title=\"Show webapp names\">&#9776;</button>\n";
	for(int i = 0; i < apps.GetCount(); i++)
		if(apps[i].group == "webapp")
			rail << PortalLink(apps[i], i == sel);
	rail << "<span class=\"portal-rail-sep\"></span>\n";
	for(int i = 0; i < apps.GetCount(); i++)
		if(apps[i].group == "settings")
			rail << PortalLink(apps[i], i == sel);
	rail << "<div class=\"portal-account\">" << RenderShellAccountRow(sess, apps[sel].url.StartsWith("/") ? apps[sel].url : String())
	     << "</div>\n</nav></div>";

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
	content << "<div class=\"portal-shell\">\n" << rail << "\n" << main << "\n</div>";

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
	// A MetaAccount session that matches a portal account signs the shell in (task 050).
	UserSession sess = SystemAuthService::GetCurrentSession(http);
	if(!sess.is_authenticated)
		sess = SystemAuthService::BridgeMetaSession(http, false);
	http.ContentType("text/html; charset=utf-8") << RenderPortalShell(http.GetHeader("host"), http["app"],
	                                                               sess,
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
