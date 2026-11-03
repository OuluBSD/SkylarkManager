#include "WebFramework.h"

NAMESPACE_UPP

DensityConfig::DensityConfig()
{
	Init(DENSITY_COMFORTABLE);
}

DensityConfig::DensityConfig(DensityLevel level)
{
	Init(level);
}

void DensityConfig::Init(DensityLevel lvl)
{
	level = lvl;
	switch(lvl) {
	case DENSITY_NONE:
		container_max_width = "100%";
		padding_page = "0";
		padding_card = "0";
		gap_grid = "0";
		break;
	case DENSITY_DENSE:
		container_max_width = "100%";
		padding_page = "0.4rem 0.6rem";
		padding_card = "6px 10px";
		gap_grid = "6px";
		break;
	case DENSITY_COMPACT:
		container_max_width = "1500px";
		padding_page = "0.75rem 1rem";
		padding_card = "10px 14px";
		gap_grid = "10px";
		break;
	case DENSITY_SPACIOUS:
		container_max_width = "1200px";
		padding_page = "2.5rem 3rem";
		padding_card = "24px 30px";
		gap_grid = "24px";
		break;
	case DENSITY_COMFORTABLE:
	default:
		container_max_width = "1360px";
		padding_page = "1.5rem 2rem";
		padding_card = "16px 20px";
		gap_grid = "16px";
		break;
	}
}

DensityConfig DensityConfig::ForLevel(DensityLevel lvl)
{
	DensityConfig c;
	c.Init(lvl);
	return c;
}

String DensityConfig::GetCssVariables() const
{
	String s;
	s << "  --wf-container-width: " << container_max_width << ";\n"
	  << "  --wf-pad-page: " << padding_page << ";\n"
	  << "  --wf-pad-card: " << padding_card << ";\n"
	  << "  --wf-gap-grid: " << gap_grid << ";\n";
	return s;
}

String DensityConfig::GetAllDensityStyles()
{
	String s;
	s << "/* Density Engine CSS Selectors */\n"
	  << ":root, html[data-density=\"comfortable\"] {\n"
	  << "  --wf-container-width: 1440px;\n"
	  << "  --wf-pad-page: 1.25rem 1.75rem;\n"
	  << "  --wf-pad-card: 16px 20px;\n"
	  << "  --wf-gap-grid: 16px;\n"
	  << "  --wf-dock-width: 320px;\n"
	  << "  --wf-dock-width-left: 370px;\n"
	  << "  --wf-dock-width-right: 260px;\n"
	  << "  --wf-cell-pad: 10px 14px;\n"
	  << "  --wf-btn-pad: 7px 14px;\n"
	  << "}\n"
	  << "html[data-density=\"spacious\"] {\n"
	  << "  --wf-container-width: 1560px;\n"
	  << "  --wf-pad-page: 2rem 2.5rem;\n"
	  << "  --wf-pad-card: 22px 28px;\n"
	  << "  --wf-gap-grid: 20px;\n"
	  << "  --wf-dock-width: 340px;\n"
	  << "  --wf-dock-width-left: 390px;\n"
	  << "  --wf-dock-width-right: 280px;\n"
	  << "  --wf-cell-pad: 14px 18px;\n"
	  << "  --wf-btn-pad: 10px 20px;\n"
	  << "}\n"
	  << "html[data-density=\"compact\"] {\n"
	  << "  --wf-container-width: 1600px;\n"
	  << "  --wf-pad-page: 0.75rem 1rem;\n"
	  << "  --wf-pad-card: 10px 14px;\n"
	  << "  --wf-gap-grid: 10px;\n"
	  << "  --wf-dock-width: 300px;\n"
	  << "  --wf-dock-width-left: 350px;\n"
	  << "  --wf-dock-width-right: 240px;\n"
	  << "  --wf-cell-pad: 6px 10px;\n"
	  << "  --wf-btn-pad: 5px 10px;\n"
	  << "}\n"
	  << "html[data-density=\"dense\"] {\n"
	  << "  --wf-container-width: 100%;\n"
	  << "  --wf-pad-page: 0.35rem 0.5rem;\n"
	  << "  --wf-pad-card: 6px 8px;\n"
	  << "  --wf-gap-grid: 6px;\n"
	  << "  --wf-dock-width: 260px;\n"
	  << "  --wf-dock-width-left: 310px;\n"
	  << "  --wf-dock-width-right: 220px;\n"
	  << "  --wf-cell-pad: 4px 6px;\n"
	  << "  --wf-btn-pad: 3px 7px;\n"
	  << "}\n"
	  << "html[data-density=\"none\"] {\n"
	  << "  --wf-container-width: 100%;\n"
	  << "  --wf-pad-page: 0px;\n"
	  << "  --wf-pad-card: 2px 4px;\n"
	  << "  --wf-gap-grid: 2px;\n"
	  << "  --wf-dock-width: 220px;\n"
	  << "  --wf-dock-width-left: 260px;\n"
	  << "  --wf-dock-width-right: 200px;\n"
	  << "  --wf-cell-pad: 2px 4px;\n"
	  << "  --wf-btn-pad: 2px 4px;\n"
	  << "}\n";
	return s;
}

String DensityConfig::GetClientScript()
{
	return
		"var WebDensityManager = (function() {\n"
		"  let levels = ['dense', 'compact', 'comfortable', 'spacious'];\n"
		"  let current = localStorage.getItem('wf_density') || document.documentElement.getAttribute('data-density') || 'comfortable';\n"
		"  function apply(lvl) {\n"
		"    current = lvl;\n"
		"    localStorage.setItem('wf_density', lvl);\n"
		"    document.documentElement.setAttribute('data-density', lvl);\n"
		"    let lbl = document.getElementById('wf-density-label');\n"
		"    if (lbl) lbl.innerText = lvl.charAt(0).toUpperCase() + lvl.slice(1);\n"
		"    let indicators = document.querySelectorAll('.wf-density-indicator');\n"
		"    indicators.forEach(function(el) { el.innerText = lvl; });\n"
		"    if (window.WebAudio) WebAudio.play('switch');\n"
		"  }\n"
		"  function cycle() {\n"
		"    let idx = levels.indexOf(current);\n"
		"    let next = levels[(idx + 1) % levels.length];\n"
		"    apply(next);\n"
		"  }\n"
		"  function init() {\n"
		"    let saved = localStorage.getItem('wf_density');\n"
		"    if (saved) apply(saved);\n"
		"    else {\n"
		"      let curAttr = document.documentElement.getAttribute('data-density');\n"
		"      if (curAttr) current = curAttr;\n"
		"      let lbl = document.getElementById('wf-density-label');\n"
		"      if (lbl) lbl.innerText = current.charAt(0).toUpperCase() + current.slice(1);\n"
		"    }\n"
		"  }\n"
		"  return {\n"
		"    get: function() { return current; },\n"
		"    set: apply,\n"
		"    cycleDensity: cycle,\n"
		"    init: init\n"
		"  };\n"
		"})();\n"
		"if (document.readyState === 'loading') {\n"
		"  document.addEventListener('DOMContentLoaded', WebDensityManager.init);\n"
		"} else {\n"
		"  WebDensityManager.init();\n"
		"}\n";
}

// ---------------------------------------------------------------------------
// Concrete Templates
// ---------------------------------------------------------------------------

MegaMenuTemplate::MegaMenuTemplate()
	: WebTemplate("megamenu", "MegaMenu Web Template")
{
}

String MegaMenuTemplate::GetStyles() const
{
	return
		"  .wf-template-megamenu { min-height: 100vh; display: flex; flex-direction: column; background: var(--wf-bg-normal, #182030); color: var(--wf-fg-normal, #d3dae3); }\n"
		"  .wf-template-megamenu .wf-page-body { flex: 1; width: 100%; max-width: var(--wf-container-width, 1360px); margin: 0 auto; padding: var(--wf-pad-page, 1.5rem 2rem); display: flex; flex-direction: column; gap: var(--wf-gap-grid, 16px); }\n"
		"  .wf-template-megamenu .wf-subnav { padding: 8px 0; border-bottom: 1px solid var(--wf-border-color, rgba(0,161,197,0.2)); margin-bottom: 12px; }\n"
		"  .wf-template-megamenu .wf-footer { padding: 1.5rem 2rem; border-top: 1px solid var(--wf-border-color, rgba(255,255,255,0.08)); background: var(--wf-bg-alt, #1c202f); font-size: 0.85rem; color: var(--wf-fg-inactive, #8b919d); text-align: center; }\n";
}

String MegaMenuTemplate::Render(const TemplateSlots& slots, const DensityConfig& density) const
{
	String left_side = !slots.left_sidebar.IsEmpty() ? slots.left_sidebar : slots.sidebar;
	String right_side = slots.right_sidebar;
	bool has_left = !left_side.IsEmpty();
	bool has_right = !right_side.IsEmpty();

	String h;
	h << "<div class=\"wf-template-megamenu\">\n"
	  << slots.header << "\n";
	if(!slots.sub_nav.IsEmpty()) {
		h << "  <div class=\"wf-subnav\">" << slots.sub_nav << "</div>\n";
	}

	if(has_left || has_right) {
		h << "  <div class=\"wf-dual-layout"
		  << (has_left ? " has-left" : "")
		  << (has_right ? " has-right" : "") << "\">\n";

		if(has_left) {
			h << "    <aside class=\"wf-dock wf-dock-left\" id=\"wf-dock-left\" role=\"complementary\" aria-label=\"Left Tree Navigator\">\n"
			  << "      <div class=\"wf-dock-header\">\n"
			  << "        <button type=\"button\" class=\"wf-dock-toggle\" onclick=\"TreeNavigator.toggleDock('wf-dock-left')\" title=\"Collapse/Expand Left Dock\">\n"
			  << "          " << VividIcon::Get("chevron-left", 14) << "\n"
			  << "        </button>\n"
			  << "      </div>\n"
			  << "      <div class=\"wf-dock-content\">" << left_side << "</div>\n"
			  << "    </aside>\n";
		}

		h << "    <main class=\"wf-dual-main\">\n";
		if(!slots.breadcrumb.IsEmpty()) {
			h << "      <nav class=\"wf-breadcrumb\" aria-label=\"Breadcrumb\">" << slots.breadcrumb << "</nav>\n";
		}
		h << slots.content << "\n"
		  << "    </main>\n";

		if(has_right) {
			h << "    <aside class=\"wf-dock wf-dock-right\" id=\"wf-dock-right\" role=\"complementary\" aria-label=\"Right Inspector / Outline\">\n"
			  << "      <div class=\"wf-dock-header\">\n"
			  << "        <button type=\"button\" class=\"wf-dock-toggle\" onclick=\"TreeNavigator.toggleDock('wf-dock-right')\" title=\"Collapse/Expand Right Dock\">\n"
			  << "          " << VividIcon::Get("chevron-right", 14) << "\n"
			  << "        </button>\n"
			  << "      </div>\n"
			  << "      <div class=\"wf-dock-content\">" << right_side << "</div>\n"
			  << "    </aside>\n";
		}

		h << "  </div>\n";
	} else {
		h << "  <main class=\"wf-page-body\">\n";
		if(!slots.breadcrumb.IsEmpty()) {
			h << "    <nav class=\"wf-breadcrumb\" aria-label=\"Breadcrumb\">" << slots.breadcrumb << "</nav>\n";
		}
		h << slots.content << "\n"
		  << "  </main>\n";
	}

	if(!slots.footer.IsEmpty()) {
		h << "  <footer class=\"wf-footer\">" << slots.footer << "</footer>\n";
	}
	if(!slots.modals.IsEmpty()) {
		h << "  <div class=\"wf-modals\">" << slots.modals << "</div>\n";
	}
	h << "</div>\n";
	return h;
}

DashboardTemplate::DashboardTemplate()
	: WebTemplate("dashboard", "Dashboard & Sidebar Web Template")
{
}

String DashboardTemplate::GetStyles() const
{
	return
		"  .wf-template-dashboard { min-height: 100vh; display: flex; flex-direction: column; background: var(--wf-bg-normal, #182030); color: var(--wf-fg-normal, #d3dae3); }\n"
		"  .wf-dashboard-container { flex: 1; display: flex; overflow: hidden; }\n"
		"  .wf-dashboard-sidebar { width: 260px; background: var(--wf-bg-view, #151d2b); border-right: 1px solid var(--wf-border-color, rgba(0,161,197,0.2)); padding: var(--wf-pad-page, 1.25rem); overflow-y: auto; display: flex; flex-direction: column; gap: 8px; }\n"
		"  .wf-dashboard-main { flex: 1; padding: var(--wf-pad-page, 1.5rem 2rem); overflow-y: auto; display: flex; flex-direction: column; gap: var(--wf-gap-grid, 16px); }\n";
}

String DashboardTemplate::Render(const TemplateSlots& slots, const DensityConfig& density) const
{
	String left_side = !slots.left_sidebar.IsEmpty() ? slots.left_sidebar : slots.sidebar;
	String right_side = slots.right_sidebar;
	bool has_right = !right_side.IsEmpty();

	String h;
	h << "<div class=\"wf-template-dashboard\">\n"
	  << slots.header << "\n"
	  << "  <div class=\"wf-dashboard-container\">\n"
	  << "    <aside class=\"wf-dashboard-sidebar\">" << left_side << "</aside>\n"
	  << "    <main class=\"wf-dashboard-main\">\n";
	if(!slots.breadcrumb.IsEmpty()) {
		h << "      <nav class=\"wf-breadcrumb\">" << slots.breadcrumb << "</nav>\n";
	}
	h << slots.content << "\n"
	  << "    </main>\n";
	if(has_right) {
		h << "    <aside class=\"wf-dock wf-dock-right\" id=\"wf-dock-right\" role=\"complementary\" aria-label=\"Inspector\">\n"
		  << "      <div class=\"wf-dock-content\">" << right_side << "</div>\n"
		  << "    </aside>\n";
	}
	h << "  </div>\n";
	if(!slots.footer.IsEmpty()) {
		h << "  <footer class=\"wf-footer\">" << slots.footer << "</footer>\n";
	}
	h << "</div>\n";
	return h;
}

AppWorkspaceTemplate::AppWorkspaceTemplate()
	: WebTemplate("app-workspace", "App Full-Bleed Dense Workspace Template")
{
}

String AppWorkspaceTemplate::GetStyles() const
{
	return
		"  .wf-template-workspace { height: 100vh; display: flex; flex-direction: column; background: var(--wf-bg-normal, #182030); color: var(--wf-fg-normal, #d3dae3); overflow: hidden; }\n"
		"  .wf-workspace-main { flex: 1; display: flex; overflow: hidden; padding: var(--wf-pad-page, 0); position: relative; }\n"
		"  .wf-workspace-dock { height: 100%; top: 0 !important; max-height: 100% !important; border-radius: 0 !important; border-top: none; border-bottom: none; }\n"
		"  .wf-workspace-center { flex: 1; height: 100%; display: flex; flex-direction: column; overflow: hidden; }\n";
}

String AppWorkspaceTemplate::Render(const TemplateSlots& slots, const DensityConfig& density) const
{
	String left_side = !slots.left_sidebar.IsEmpty() ? slots.left_sidebar : slots.sidebar;
	String right_side = slots.right_sidebar;
	bool has_left = !left_side.IsEmpty();
	bool has_right = !right_side.IsEmpty();

	String h;
	h << "<div class=\"wf-template-workspace\">\n"
	  << slots.header << "\n"
	  << "  <div class=\"wf-workspace-main\">\n";

	if(has_left) {
		h << "    <aside class=\"wf-dock wf-dock-left wf-workspace-dock\" id=\"wf-dock-left\" role=\"complementary\" aria-label=\"Workspace Left Dock\">\n"
		  << "      <div class=\"wf-dock-header\">\n"
		  << "        <button type=\"button\" class=\"wf-dock-toggle\" onclick=\"TreeNavigator.toggleDock('wf-dock-left')\" title=\"Toggle Dock\">\n"
		  << "          " << VividIcon::Get("chevron-left", 14) << "\n"
		  << "        </button>\n"
		  << "      </div>\n"
		  << "      <div class=\"wf-dock-content\">" << left_side << "</div>\n"
		  << "    </aside>\n";
	}

	h << "    <main class=\"wf-workspace-center\">\n"
	  << slots.content << "\n"
	  << "    </main>\n";

	if(has_right) {
		h << "    <aside class=\"wf-dock wf-dock-right wf-workspace-dock\" id=\"wf-dock-right\" role=\"complementary\" aria-label=\"Workspace Right Dock\">\n"
		  << "      <div class=\"wf-dock-header\">\n"
		  << "        <button type=\"button\" class=\"wf-dock-toggle\" onclick=\"TreeNavigator.toggleDock('wf-dock-right')\" title=\"Toggle Dock\">\n"
		  << "          " << VividIcon::Get("chevron-right", 14) << "\n"
		  << "        </button>\n"
		  << "      </div>\n"
		  << "      <div class=\"wf-dock-content\">" << right_side << "</div>\n"
		  << "    </aside>\n";
	}

	h << "  </div>\n"
	  << "</div>\n";
	return h;
}

// ---------------------------------------------------------------------------
// GitHubTemplate
// ---------------------------------------------------------------------------

GitHubTemplate::GitHubTemplate()
	: WebTemplate("github", "GitHub Platform Template")
{
}

String GitHubTemplate::GetStyles() const
{
	return
		"  .wf-template-github { min-height: 100vh; display: flex; flex-direction: column; background: var(--wf-bg-normal, #0d1117); color: var(--wf-fg-normal, #e6edf3); }\n"
		"  .wf-gh-topbar { background: var(--wf-header-bg, #010409); border-bottom: 1px solid var(--wf-header-border, #30363d); padding: 12px 1.5rem; display: flex; align-items: center; justify-content: space-between; position: sticky; top: 0; z-index: 1000; }\n"
		"  .wf-gh-repohead { background: var(--wf-bg-alt, #161b22); border-bottom: 1px solid var(--wf-border-color, #30363d); padding: 16px var(--wf-pad-page, 2rem) 0; }\n"
		"  .wf-gh-repohead-row { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; margin-bottom: 16px; }\n"
		"  .wf-gh-title { display: flex; align-items: center; gap: 8px; font-size: 1.25rem; font-weight: 600; }\n"
		"  .wf-gh-title a { color: var(--wf-accent-focus, #58a6ff); text-decoration: none; }\n"
		"  .wf-gh-title a:hover { text-decoration: underline; }\n"
		"  .wf-gh-badge { font-size: 0.75rem; font-weight: 500; padding: 2px 8px; border-radius: 20px; border: 1px solid var(--wf-border-color, #30363d); color: var(--wf-fg-inactive, #7d8590); }\n"
		"  .wf-gh-actions { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }\n"
		"  .wf-gh-btn-group { display: inline-flex; align-items: center; border-radius: 6px; border: 1px solid var(--wf-border-color, #30363d); background: var(--wf-surface, #21262d); overflow: hidden; }\n"
		"  .wf-gh-btn { padding: 4px 10px; font-size: 0.8rem; font-weight: 500; color: var(--wf-fg-normal, #e6edf3); background: transparent; border: none; cursor: pointer; display: inline-flex; align-items: center; gap: 6px; text-decoration: none; transition: background 0.15s; }\n"
		"  .wf-gh-btn:hover { background: var(--wf-surface-hover, #30363d); }\n"
		"  .wf-gh-counter { padding: 1px 7px; font-size: 0.75rem; font-weight: 600; border-radius: 10px; background: rgba(110,118,129,0.2); color: var(--wf-fg-normal, #e6edf3); margin-left: 4px; }\n"
		"  .wf-gh-nav-tabs { display: flex; gap: 4px; overflow-x: auto; margin-bottom: -1px; }\n"
		"  .wf-gh-tab { display: inline-flex; align-items: center; gap: 8px; padding: 8px 14px; font-size: 0.88rem; font-weight: 500; color: var(--wf-fg-inactive, #7d8590); text-decoration: none; border-bottom: 2px solid transparent; cursor: pointer; transition: all 0.15s; white-space: nowrap; }\n"
		"  .wf-gh-tab:hover { color: var(--wf-fg-normal, #e6edf3); border-bottom-color: var(--wf-border-color, #30363d); }\n"
		"  .wf-gh-tab.active { color: var(--wf-fg-normal, #e6edf3); font-weight: 600; border-bottom-color: #fd8c73; }\n"
		"  .wf-gh-main-container { flex: 1; width: 100%; max-width: var(--wf-container-width, 1360px); margin: 0 auto; padding: var(--wf-pad-page, 1.5rem 2rem); }\n"
		"  .wf-gh-grid { display: grid; grid-template-columns: 1fr 310px; gap: 24px; }\n"
		"  .wf-gh-full { width: 100%; }\n"
		"  .wf-gh-sidebar { display: flex; flex-direction: column; gap: 20px; font-size: 0.88rem; }\n"
		"  .wf-gh-sidebar-section { border-bottom: 1px solid var(--wf-border-color, #30363d); padding-bottom: 16px; display: flex; flex-direction: column; gap: 8px; }\n"
		"  .wf-gh-sidebar-title { font-weight: 600; font-size: 0.95rem; color: var(--wf-fg-normal, #e6edf3); }\n"
		"  .wf-gh-file-box { border: 1px solid var(--wf-border-color, #30363d); border-radius: 6px; overflow: hidden; background: var(--wf-bg-view, #0d1117); margin-bottom: 20px; }\n"
		"  .wf-gh-file-header { background: var(--wf-bg-alt, #161b22); padding: 10px 16px; border-bottom: 1px solid var(--wf-border-color, #30363d); font-size: 0.84rem; display: flex; align-items: center; justify-content: space-between; }\n"
		"  .wf-gh-file-row { display: flex; align-items: center; padding: 8px 16px; border-bottom: 1px solid var(--wf-border-color, #21262d); font-size: 0.84rem; gap: 12px; }\n"
		"  .wf-gh-file-row:last-child { border-bottom: none; }\n"
		"  .wf-gh-file-row:hover { background: var(--wf-surface-hover, rgba(110,118,129,0.06)); }\n"
		"  .wf-gh-file-name { display: flex; align-items: center; gap: 8px; flex: 0 0 280px; color: var(--wf-fg-normal, #e6edf3); text-decoration: none; font-weight: 500; }\n"
		"  .wf-gh-file-name:hover { color: var(--wf-accent-focus, #58a6ff); text-decoration: underline; }\n"
		"  .wf-gh-file-msg { flex: 1; color: var(--wf-fg-inactive, #7d8590); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }\n"
		"  .wf-gh-file-msg a { color: var(--wf-fg-inactive, #7d8590); text-decoration: none; }\n"
		"  .wf-gh-file-msg a:hover { color: var(--wf-accent-focus, #58a6ff); text-decoration: underline; }\n"
		"  .wf-gh-file-age { flex: 0 0 100px; text-align: right; color: var(--wf-fg-muted, #848d97); font-size: 0.78rem; }\n"
		"  .wf-gh-readme-box { border: 1px solid var(--wf-border-color, #30363d); border-radius: 6px; overflow: hidden; background: var(--wf-bg-view, #0d1117); margin-top: 20px; }\n"
		"  .wf-gh-readme-header { background: var(--wf-bg-alt, #161b22); padding: 10px 16px; border-bottom: 1px solid var(--wf-border-color, #30363d); font-size: 0.85rem; font-weight: 600; display: flex; align-items: center; gap: 8px; }\n"
		"  .wf-gh-readme-body { padding: 24px 32px; line-height: 1.6; font-size: 0.95rem; }\n"
		"  .wf-gh-footer { border-top: 1px solid var(--wf-border-color, #30363d); padding: 36px 2rem; margin-top: auto; display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 16px; font-size: 0.78rem; color: var(--wf-fg-inactive, #7d8590); }\n"
		"  .wf-gh-footer a { color: var(--wf-accent-focus, #58a6ff); text-decoration: none; }\n"
		"  .wf-gh-footer a:hover { text-decoration: underline; }\n"
		"  .wf-gh-footer-links { display: flex; align-items: center; gap: 16px; flex-wrap: wrap; }\n"
		"  @media (max-width: 992px) {\n"
		"    .wf-gh-grid { grid-template-columns: 1fr; }\n"
		"  }\n";
}

String GitHubTemplate::Render(const TemplateSlots& slots, const DensityConfig& density) const
{
	String left_side = !slots.left_sidebar.IsEmpty() ? slots.left_sidebar : "";
	String right_side = slots.sidebar;
	bool has_left = !left_side.IsEmpty();
	bool has_right = !right_side.IsEmpty();

	String h;
	h << "<div class=\"wf-template-github\">\n";

	if(!slots.header.IsEmpty())
		h << slots.header << "\n";

	if(!slots.sub_nav.IsEmpty())
		h << slots.sub_nav << "\n";

	if(!slots.breadcrumb.IsEmpty()) {
		h << "  <div style=\"max-width:var(--wf-container-width,1360px); margin:0 auto; padding:0.75rem var(--wf-pad-page,2rem) 0;\">\n"
		  << "    <nav class=\"wf-breadcrumb\">" << slots.breadcrumb << "</nav>\n"
		  << "  </div>\n";
	}

	if(has_left) {
		h << "  <div class=\"wf-dual-layout has-left\">\n"
		  << "    <aside class=\"wf-dock wf-dock-left\" id=\"wf-dock-left\" role=\"complementary\" aria-label=\"Navigation Tree\">\n"
		  << "      <div class=\"wf-dock-header\">\n"
		  << "        <button type=\"button\" class=\"wf-dock-toggle\" onclick=\"TreeNavigator.toggleDock('wf-dock-left')\" title=\"Toggle Dock\">\n"
		  << "          " << VividIcon::Get("chevron-left", 14) << "\n"
		  << "        </button>\n"
		  << "      </div>\n"
		  << "      <div class=\"wf-dock-content\">" << left_side << "</div>\n"
		  << "    </aside>\n"
		  << "    <main class=\"wf-dual-main\">\n"
		  << slots.content << "\n"
		  << "    </main>\n"
		  << "  </div>\n";
	} else if(has_right) {
		h << "  <main class=\"wf-gh-main-container\">\n"
		  << "    <div class=\"wf-gh-grid\">\n"
		  << "      <div class=\"wf-gh-primary\">" << slots.content << "</div>\n"
		  << "      <aside class=\"wf-gh-sidebar\">" << right_side << "</aside>\n"
		  << "    </div>\n"
		  << "  </main>\n";
	} else {
		h << "  <main class=\"wf-gh-main-container\">\n"
		  << "    <div class=\"wf-gh-full\">" << slots.content << "</div>\n"
		  << "  </main>\n";
	}

	if(!slots.footer.IsEmpty())
		h << slots.footer << "\n";
	else {
		h << "  <footer class=\"wf-gh-footer\">\n"
		  << "    <div style=\"display:flex; align-items:center; gap:8px;\">\n"
		  << "      " << VividIcon::Get("vivid", 20) << "\n"
		  << "      <span>&copy; 2026 Ultimate++ Agentic Framework</span>\n"
		  << "    </div>\n"
		  << "    <div class=\"wf-gh-footer-links\">\n"
		  << "      <a href=\"#\">Terms</a>\n"
		  << "      <a href=\"#\">Privacy</a>\n"
		  << "      <a href=\"#\">Security</a>\n"
		  << "      <a href=\"#\">Status</a>\n"
		  << "      <a href=\"#\">Docs</a>\n"
		  << "      <a href=\"#\">Contact</a>\n"
		  << "    </div>\n"
		  << "  </footer>\n";
	}

	if(!slots.modals.IsEmpty())
		h << "  <div class=\"wf-modals\">" << slots.modals << "</div>\n";

	h << "</div>\n";
	return h;
}

// ---------------------------------------------------------------------------
// SourceForgeTemplate
// ---------------------------------------------------------------------------

SourceForgeTemplate::SourceForgeTemplate()
	: WebTemplate("sourceforge", "SourceForge Platform Template")
{
}

String SourceForgeTemplate::GetStyles() const
{
	return
		"  .wf-template-sf { min-height: 100vh; display: flex; flex-direction: column; background: var(--wf-bg-normal, #1a1d21); color: var(--wf-fg-normal, #e6e8eb); font-family: Helvetica Neue, Helvetica, Arial, sans-serif; }\n"
		"  .wf-sf-topbar { background: var(--wf-header-bg, #121417); border-bottom: 1px solid var(--wf-header-border, #2e333a); padding: 10px 1.5rem; display: flex; align-items: center; justify-content: space-between; position: sticky; top: 0; z-index: 1000; font-size: 0.86rem; }\n"
		"  .wf-sf-topbar a { color: var(--wf-header-fg, #ffffff); text-decoration: none; font-weight: 500; }\n"
		"  .wf-sf-topbar a:hover { color: var(--wf-accent-focus, #ee6524); }\n"
		"  .wf-sf-masthead { background: var(--wf-bg-alt, #22262b); border-bottom: 1px solid var(--wf-border-color, #393e46); padding: 20px var(--wf-pad-page, 2rem) 0; }\n"
		"  .wf-sf-masthead-hero { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 20px; margin-bottom: 20px; }\n"
		"  .wf-sf-hero-left { display: flex; align-items: center; gap: 16px; }\n"
		"  .wf-sf-logo-box { width: 64px; height: 64px; border-radius: 6px; background: linear-gradient(135deg, #ee6524, #d85416); display: flex; align-items: center; justify-content: center; color: #ffffff; font-size: 1.8rem; font-weight: 700; box-shadow: 0 2px 8px rgba(0,0,0,0.3); }\n"
		"  .wf-sf-hero-title h1 { margin: 0; font-size: 1.6rem; font-weight: 700; color: var(--wf-fg-normal, #ffffff); }\n"
		"  .wf-sf-hero-title .wf-sf-by { font-size: 0.85rem; color: var(--wf-fg-inactive, #8c929a); margin-top: 4px; }\n"
		"  .wf-sf-hero-title .wf-sf-by a { color: var(--wf-accent-focus, #ee6524); text-decoration: none; font-weight: 600; }\n"
		"  .wf-sf-hero-right { display: flex; flex-direction: column; align-items: flex-end; gap: 8px; }\n"
		"  .wf-sf-btn-download { background: #ee6524; color: #ffffff; padding: 12px 24px; border-radius: 4px; font-size: 1rem; font-weight: 700; text-decoration: none; display: inline-flex; align-items: center; gap: 10px; box-shadow: 0 4px 12px rgba(238,101,36,0.35); transition: background 0.15s, transform 0.1s; }\n"
		"  .wf-sf-btn-download:hover { background: #d85416; color: #ffffff; transform: translateY(-1px); }\n"
		"  .wf-sf-stats-row { font-size: 0.78rem; color: var(--wf-fg-inactive, #8c929a); }\n"
		"  .wf-sf-tabs { display: flex; gap: 2px; overflow-x: auto; margin-bottom: -1px; }\n"
		"  .wf-sf-tab { display: inline-flex; align-items: center; gap: 6px; padding: 10px 16px; font-size: 0.88rem; font-weight: 600; color: var(--wf-fg-inactive, #8c929a); text-decoration: none; border-bottom: 3px solid transparent; cursor: pointer; transition: all 0.15s; white-space: nowrap; }\n"
		"  .wf-sf-tab:hover { color: var(--wf-fg-normal, #ffffff); border-bottom-color: var(--wf-border-color, #393e46); }\n"
		"  .wf-sf-tab.active { color: var(--wf-accent-focus, #ee6524); border-bottom-color: #ee6524; }\n"
		"  .wf-sf-container { flex: 1; width: 100%; max-width: var(--wf-container-width, 1360px); margin: 0 auto; padding: var(--wf-pad-page, 1.5rem 2rem); }\n"
		"  .wf-sf-grid { display: grid; grid-template-columns: 1fr 310px; gap: 24px; }\n"
		"  .wf-sf-full { width: 100%; }\n"
		"  .wf-sf-sidebar { display: flex; flex-direction: column; gap: 20px; font-size: 0.88rem; }\n"
		"  .wf-sf-sidebar-card { background: var(--wf-surface, #272b30); border: 1px solid var(--wf-border-color, #393e46); border-radius: 4px; padding: 16px; display: flex; flex-direction: column; gap: 12px; }\n"
		"  .wf-sf-sidebar-title { font-size: 0.95rem; font-weight: 700; color: var(--wf-fg-normal, #ffffff); border-bottom: 1px solid var(--wf-border-color, #393e46); padding-bottom: 8px; }\n"
		"  .wf-sf-footer { border-top: 1px solid var(--wf-border-color, #393e46); background: var(--wf-bg-alt, #22262b); padding: 36px 2rem; margin-top: auto; font-size: 0.8rem; color: var(--wf-fg-inactive, #8c929a); display: flex; flex-direction: column; gap: 12px; align-items: center; text-align: center; }\n"
		"  .wf-sf-footer-links { display: flex; gap: 16px; flex-wrap: wrap; justify-content: center; }\n"
		"  .wf-sf-footer a { color: var(--wf-accent-focus, #ee6524); text-decoration: none; }\n"
		"  .wf-sf-footer a:hover { text-decoration: underline; }\n"
		"  @media (max-width: 992px) {\n"
		"    .wf-sf-grid { grid-template-columns: 1fr; }\n"
		"  }\n";
}

String SourceForgeTemplate::Render(const TemplateSlots& slots, const DensityConfig& density) const
{
	String left_side = !slots.left_sidebar.IsEmpty() ? slots.left_sidebar : "";
	String right_side = slots.sidebar;
	bool has_left = !left_side.IsEmpty();
	bool has_right = !right_side.IsEmpty();

	String h;
	h << "<div class=\"wf-template-sf\">\n";

	if(!slots.header.IsEmpty())
		h << slots.header << "\n";

	if(!slots.sub_nav.IsEmpty())
		h << slots.sub_nav << "\n";

	if(!slots.breadcrumb.IsEmpty()) {
		h << "  <div style=\"max-width:var(--wf-container-width,1360px); margin:0 auto; padding:0.75rem var(--wf-pad-page,2rem) 0;\">\n"
		  << "    <nav class=\"wf-breadcrumb\">" << slots.breadcrumb << "</nav>\n"
		  << "  </div>\n";
	}

	if(has_left) {
		h << "  <div class=\"wf-dual-layout has-left\">\n"
		  << "    <aside class=\"wf-dock wf-dock-left\" id=\"wf-dock-left\" role=\"complementary\" aria-label=\"Navigation Tree\">\n"
		  << "      <div class=\"wf-dock-header\">\n"
		  << "        <button type=\"button\" class=\"wf-dock-toggle\" onclick=\"TreeNavigator.toggleDock('wf-dock-left')\" title=\"Toggle Dock\">\n"
		  << "          " << VividIcon::Get("chevron-left", 14) << "\n"
		  << "        </button>\n"
		  << "      </div>\n"
		  << "      <div class=\"wf-dock-content\">" << left_side << "</div>\n"
		  << "    </aside>\n"
		  << "    <main class=\"wf-dual-main\">\n"
		  << slots.content << "\n"
		  << "    </main>\n"
		  << "  </div>\n";
	} else if(has_right) {
		h << "  <main class=\"wf-sf-container\">\n"
		  << "    <div class=\"wf-sf-grid\">\n"
		  << "      <div class=\"wf-sf-primary\">" << slots.content << "</div>\n"
		  << "      <aside class=\"wf-sf-sidebar\">" << right_side << "</aside>\n"
		  << "    </div>\n"
		  << "  </main>\n";
	} else {
		h << "  <main class=\"wf-sf-container\">\n"
		  << "    <div class=\"wf-sf-full\">" << slots.content << "</div>\n"
		  << "  </main>\n";
	}

	if(!slots.footer.IsEmpty())
		h << slots.footer << "\n";
	else {
		h << "  <footer class=\"wf-sf-footer\">\n"
		  << "    <div class=\"wf-sf-footer-links\">\n"
		  << "      <a href=\"#\">About</a>\n"
		  << "      <a href=\"#\">Team</a>\n"
		  << "      <a href=\"#\">Careers</a>\n"
		  << "      <a href=\"#\">Support Request</a>\n"
		  << "      <a href=\"#\">Site Status</a>\n"
		  << "      <a href=\"#\">Terms</a>\n"
		  << "      <a href=\"#\">Privacy</a>\n"
		  << "      <a href=\"#\">Opt Out</a>\n"
		  << "    </div>\n"
		  << "    <div>&copy; 2026 SourceForge. All Rights Reserved. Headquarters: 1320 Columbia Street, Suite 310, San Diego, CA 92101</div>\n"
		  << "  </footer>\n";
	}

	if(!slots.modals.IsEmpty())
		h << "  <div class=\"wf-modals\">" << slots.modals << "</div>\n";

	h << "</div>\n";
	return h;
}

// ---------------------------------------------------------------------------
// TemplateRegistry
// ---------------------------------------------------------------------------

static ArrayMap<String, One<WebTemplate>>& s_templates()
{
	static ArrayMap<String, One<WebTemplate>> map;
	return map;
}

void TemplateRegistry::EnsureBuiltIn()
{
	if(s_templates().IsEmpty()) {
		Register(new MegaMenuTemplate);
		Register(new DashboardTemplate);
		Register(new AppWorkspaceTemplate);
		Register(new GitHubTemplate);
		Register(new SourceForgeTemplate);
	}
}

void TemplateRegistry::Register(WebTemplate* t)
{
	if(!t) return;
	int q = s_templates().Find(t->id);
	if(q >= 0)
		s_templates()[q] = t;
	else
		s_templates().Add(t->id, t);
}

const WebTemplate* TemplateRegistry::Find(const String& id)
{
	EnsureBuiltIn();
	int q = s_templates().Find(id);
	return q >= 0 ? ~s_templates()[q] : NULL;
}

const WebTemplate& TemplateRegistry::GetDefault()
{
	EnsureBuiltIn();
	return *~s_templates()[0]; // MegaMenuTemplate
}

Vector<String> TemplateRegistry::ListTemplateIds()
{
	EnsureBuiltIn();
	Vector<String> res;
	for(int i = 0; i < s_templates().GetCount(); i++)
		res.Add(s_templates().GetKey(i));
	return res;
}

END_UPP_NAMESPACE
