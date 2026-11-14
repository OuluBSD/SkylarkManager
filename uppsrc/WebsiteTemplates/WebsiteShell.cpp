#include "WebsiteTemplates.h"

NAMESPACE_UPP

// The admin shell. Own styling: the layout pattern is common to admin dashboards, the CSS, icons
// and markup here are written for this package.

static String ShellEsc(const String& value)
{
	StringBuffer out;
	for(int i = 0; i < value.GetCount(); i++) {
		switch(value[i]) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(value[i]);
		}
	}
	return String(out);
}

// Inline icons: 24-unit outline paths, drawn with currentColor.
static String ShellIcon(const String& id)
{
	String path;
	if(id == "home")        path = "M3 11l9-7 9 7v9h-6v-6H9v6H3z";
	else if(id == "map")    path = "M9 4L3 6v14l6-2 6 2 6-2V4l-6 2-6-2zM9 4v14M15 6v14";
	else if(id == "list")   path = "M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01";
	else if(id == "clock")  path = "M12 3a9 9 0 100 18 9 9 0 000-18zM12 7v5l3 2";
	else if(id == "sliders")path = "M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0M14 4v4M8 10v4M16 16v4";
	else if(id == "check")  path = "M4 12l5 5L20 6";
	else if(id == "alert")  path = "M12 3l10 18H2zM12 10v5M12 18h.01";
	else return String();
	return "<svg class=\"ad-icon\" viewBox=\"0 0 24 24\" aria-hidden=\"true\" focusable=\"false\"><path d=\"" + path + "\"/></svg>";
}

String AdminDashboardCss()
{
	return R"ADC(
*, *::before, *::after { box-sizing: border-box; }
body.ad-body { margin: 0; min-height: 100vh; background: var(--bg); color: var(--fg); font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }
h1, h2, h3 { line-height: 1.25; margin: 0; }
h1 { font-size: 1.5rem; font-weight: 650; }
h2 { font-size: 1.1rem; font-weight: 650; }
.ad-shell { display: grid; grid-template-columns: 228px minmax(0, 1fr); min-height: 100vh; }
html[data-nav="collapsed"] .ad-shell { grid-template-columns: 60px minmax(0, 1fr); }
.ad-sidebar { position: sticky; top: 0; align-self: start; height: 100vh; overflow: hidden; background: var(--panel); border-right: 1px solid var(--line); padding: 14px 10px; }
.ad-brand { display: block; font-weight: 700; font-size: 1.1rem; color: var(--fg); padding: 4px 8px 16px; white-space: nowrap; }
.ad-sidebar ul { list-style: none; margin: 0; padding: 0; }
.ad-link { display: flex; align-items: center; gap: 10px; padding: 8px 10px; border-radius: 6px; color: var(--muted); white-space: nowrap; }
.ad-link:hover { background: var(--control); color: var(--fg); text-decoration: none; }
.ad-link.is-active { background: var(--sea); color: var(--fg); font-weight: 600; }
.ad-icon { width: 18px; height: 18px; flex: 0 0 18px; fill: none; stroke: currentColor; stroke-width: 1.8; stroke-linecap: round; stroke-linejoin: round; }
html[data-nav="collapsed"] .ad-sidebar { padding: 14px 6px; }
html[data-nav="collapsed"] .ad-brand, html[data-nav="collapsed"] .ad-label { display: none; }
html[data-nav="collapsed"] .ad-link { justify-content: center; }
.ad-main { min-width: 0; display: flex; flex-direction: column; }
.ad-topbar { position: sticky; top: 0; z-index: 5; display: flex; align-items: center; gap: 12px; padding: 8px 16px; background: var(--panel); border-bottom: 1px solid var(--line); }
.ad-toggle { appearance: none; background: transparent; color: var(--fg); border: 1px solid var(--line); border-radius: 6px; width: 34px; height: 34px; display: grid; place-items: center; cursor: pointer; }
.ad-topbar-end { margin-left: auto; display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
.ad-head { padding: 16px 20px 0; }
.ad-crumbs ol { list-style: none; display: flex; flex-wrap: wrap; gap: 6px; margin: 0 0 6px; padding: 0; font-size: .85rem; color: var(--muted); }
.ad-crumbs li + li::before { content: "/"; margin-right: 6px; color: var(--line); }
.ad-content { padding: 8px 20px 32px; min-width: 0; }
.ad-cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin: 12px 0 16px; }
.ad-card { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 12px 14px; }
.ad-card .ad-card-value { font-size: 1.6rem; font-weight: 700; line-height: 1.1; }
.ad-card .ad-card-label { color: var(--muted); font-size: .85rem; }
.ad-panel { background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 14px 16px; margin: 0 0 16px; min-width: 0; }
.ad-panel > header { display: flex; align-items: baseline; gap: 10px; flex-wrap: wrap; margin-bottom: 10px; }
.ad-panel > header p { margin: 0; color: var(--muted); font-size: .9rem; }
.ad-table-wrap { overflow-x: auto; }
.ad-table { width: 100%; border-collapse: collapse; font-size: .92rem; }
.ad-table th, .ad-table td { text-align: left; vertical-align: top; padding: 7px 8px; border-bottom: 1px solid var(--line); }
.ad-table th { color: var(--muted); font-weight: 600; font-size: .82rem; }
.ad-pill { display: inline-block; padding: 1px 8px; border-radius: 999px; border: 1px solid var(--line); font-size: .8rem; margin: 1px 4px 1px 0; white-space: nowrap; }
.ad-pill.is-ok { color: var(--st-active); border-color: var(--st-active); }
.ad-pill.is-warn { color: var(--st-hot); border-color: var(--st-hot); }
.ad-pill.is-err { color: var(--st-trouble); border-color: var(--st-trouble); font-weight: 600; }
.ad-pill.is-info { color: var(--st-planned); border-color: var(--st-planned); }
.ad-pill.is-muted { color: var(--muted); }
.ad-form { display: flex; flex-wrap: wrap; gap: 10px; align-items: end; margin-top: 10px; }
.ad-form label { display: flex; flex-direction: column; gap: 3px; font-size: .82rem; color: var(--muted); min-width: 0; }
.ad-form input, .ad-form select, .ad-form textarea { font: inherit; color: var(--fg); background: var(--control); border: 1px solid var(--line); border-radius: 6px; padding: 6px 8px; min-width: 0; }
.ad-form textarea { min-height: 60px; width: min(100%, 420px); }
.ad-form input.ad-wide { width: min(100%, 320px); }
.ad-btn { font: inherit; color: var(--fg); background: var(--control); border: 1px solid var(--line); border-radius: 6px; padding: 6px 12px; cursor: pointer; }
.ad-btn.is-primary { color: #fff; background: var(--accent); border-color: var(--accent); }
.ad-btn.is-danger { color: var(--st-trouble); }
.ad-btn:disabled { opacity: .5; cursor: default; }
.ad-note { color: var(--muted); font-size: .9rem; margin: 6px 0 0; }
.ad-err { color: var(--st-trouble); border: 1px solid var(--st-trouble); border-radius: 6px; padding: 6px 10px; }
.ad-empty { color: var(--muted); }
.ad-text { white-space: pre-wrap; }
.ad-mono { font-family: ui-monospace, monospace; font-size: .86rem; }
.ad-inline { display: inline; margin: 0; }
@media (max-width: 760px) {
  .ad-shell, html[data-nav="collapsed"] .ad-shell { grid-template-columns: minmax(0, 1fr); }
  .ad-sidebar { position: fixed; z-index: 20; top: 0; left: 0; width: 240px; height: 100vh; transform: translateX(-100%); transition: transform .18s ease; box-shadow: 0 0 24px rgba(0,0,0,.3); }
  body.ad-nav-open .ad-sidebar { transform: none; }
  html[data-nav="collapsed"] .ad-brand, html[data-nav="collapsed"] .ad-label { display: block; }
  html[data-nav="collapsed"] .ad-link { justify-content: flex-start; }
  .ad-content, .ad-head { padding-left: 16px; padding-right: 16px; }
}
@media (prefers-reduced-motion: reduce) { .ad-sidebar { transition: none; } }
)ADC";
}

String AdminDashboardScript()
{
	return R"ADS((function () {
  var root = document.documentElement;
  var button = document.getElementById('ad-toggle');
  var key = 'ad-nav';
  try {
    if (window.localStorage && window.localStorage.getItem(key) === 'collapsed') {
      root.setAttribute('data-nav', 'collapsed');
    }
  } catch (e) { /* storage blocked: the sidebar starts expanded */ }
  if (!button) { return; }
  button.addEventListener('click', function () {
    var narrow = window.matchMedia && window.matchMedia('(max-width: 760px)').matches;
    if (narrow) {
      var open = document.body.classList.toggle('ad-nav-open');
      button.setAttribute('aria-expanded', open ? 'true' : 'false');
      return;
    }
    var collapsed = root.getAttribute('data-nav') === 'collapsed';
    if (collapsed) { root.removeAttribute('data-nav'); } else { root.setAttribute('data-nav', 'collapsed'); }
    button.setAttribute('aria-expanded', collapsed ? 'true' : 'false');
    try { window.localStorage.setItem(key, collapsed ? 'expanded' : 'collapsed'); } catch (e) { /* not persisted */ }
  });
})();)ADS";
}

String RenderAdminShell(const WebsiteShell& s)
{
	StringBuffer b;
	b << "<!doctype html><html" << s.html_attributes << "><head><meta charset=\"utf-8\">"
	  << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
	  << s.head_script << "<title>" << ShellEsc(s.title) << "</title>"
	  << "<style>" << WebsiteThemeStyles() << WebsiteDensityStyles() << "</style>"
	  << "<style>" << AdminDashboardCss() << "</style></head>"
	  << "<body class=\"ad-body\"" << s.body_attributes << "><div class=\"ad-shell\">"
	  << "<aside class=\"ad-sidebar\" id=\"ad-sidebar\">"
	  << "<a class=\"ad-brand\" href=\"" << ShellEsc(s.brand_href) << "\">" << ShellEsc(s.brand) << "</a>"
	  << "<nav aria-label=\"Main\"><ul>";
	for(const WebsiteShellLink& l : s.nav)
		b << "<li><a class=\"ad-link" << (l.active ? " is-active" : String()) << "\" href=\"" << ShellEsc(l.href)
		  << "\"" << (l.active ? " aria-current=\"page\"" : String()) << ">" << ShellIcon(l.icon)
		  << "<span class=\"ad-label\">" << ShellEsc(l.label) << "</span></a></li>";
	b << "</ul></nav></aside>"
	  << "<div class=\"ad-main\"><header class=\"ad-topbar\">"
	  << "<button type=\"button\" class=\"ad-toggle\" id=\"ad-toggle\" aria-controls=\"ad-sidebar\" aria-expanded=\"true\""
	  << " aria-label=\"Toggle sidebar\">" << ShellIcon("list") << "</button>"
	  << "<div class=\"ad-topbar-end\">" << s.topbar_html << "</div></header>";
	if(s.crumbs.GetCount()) {
		b << "<div class=\"ad-head\"><nav class=\"ad-crumbs\" aria-label=\"Breadcrumb\"><ol>";
		for(int i = 0; i < s.crumbs.GetCount(); i++) {
			const WebsiteShellLink& c = s.crumbs[i];
			if(i == s.crumbs.GetCount() - 1)
				b << "<li aria-current=\"page\">" << ShellEsc(c.label) << "</li>";
			else
				b << "<li><a href=\"" << ShellEsc(c.href) << "\">" << ShellEsc(c.label) << "</a></li>";
		}
		b << "</ol></nav></div>";
	}
	b << "<main class=\"ad-content\">" << s.body << "</main></div></div>"
	  << "<script>" << AdminDashboardScript() << "</script>"
	  << WebsiteThemeControlsScript() << "</body></html>";
	return String(b);
}

END_UPP_NAMESPACE
