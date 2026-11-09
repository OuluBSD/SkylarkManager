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
	return WebFrameworkAsset("density.css");
}

String DensityConfig::GetClientScript()
{
	return WebFrameworkAsset("density.js");
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
	return WebFrameworkAsset("template-megamenu.css");
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
	return WebFrameworkAsset("template-dashboard.css");
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
	return WebFrameworkAsset("template-appworkspace.css");
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
	return WebFrameworkAsset("template-github.css");
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
	return WebFrameworkAsset("template-sourceforge.css");
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
// SidebarTemplate
// ---------------------------------------------------------------------------

SidebarTemplate::SidebarTemplate()
	: WebTemplate("sidebar", "Left Sidebar Web Template")
{
}

String SidebarTemplate::GetStyles() const
{
	return WebFrameworkAsset("template-sidebar.css");
}

String SidebarTemplate::Render(const TemplateSlots& slots, const DensityConfig& density) const
{
	String h;
	h << "<div class=\"wf-template-sidebar\">\n"
	  << "  <header class=\"wf-mobile-header\">\n"
	  << "    <button type=\"button\" class=\"wf-mobile-header-toggle\" onclick=\"document.body.classList.toggle('wf-sidebar-open')\" aria-label=\"Toggle navigation\">"
	  << VividIcon::Get("menu", 20) << "</button>\n"
	  << "    <span class=\"wf-sidebar-brand\">" << slots.header << "</span>\n"
	  << "  </header>\n"
	  << "  <aside class=\"wf-sidebar\" id=\"wf-sidebar\" role=\"navigation\">\n"
	  << "    <div class=\"wf-sidebar-header\">\n"
	  << "      <span class=\"wf-sidebar-brand wf-sidebar-label\">" << slots.header << "</span>\n"
	  << "      <button type=\"button\" class=\"wf-sidebar-collapse-btn\" onclick=\"document.getElementById('wf-sidebar').classList.toggle('wf-sidebar-collapsed')\" title=\"Collapse/expand navigation\">"
	  << VividIcon::Get("chevron-left", 14) << "</button>\n"
	  << "    </div>\n"
	  << "    <div class=\"wf-sidebar-content\">" << slots.left_sidebar << "</div>\n";
	if(!slots.sidebar_footer.IsEmpty()) {
		h << "    <div class=\"wf-sidebar-footer\">" << slots.sidebar_footer << "</div>\n";
	}
	h << "  </aside>\n"
	  << "  <main class=\"wf-page-body\">\n";
	if(!slots.breadcrumb.IsEmpty()) {
		h << "    <nav class=\"wf-breadcrumb\" aria-label=\"Breadcrumb\">" << slots.breadcrumb << "</nav>\n";
	}
	h << slots.content << "\n"
	  << "  </main>\n";
	if(!slots.footer.IsEmpty()) {
		h << "  <footer class=\"wf-footer\">" << slots.footer << "</footer>\n";
	}
	if(!slots.modals.IsEmpty()) {
		h << "  <div class=\"wf-modals\">" << slots.modals << "</div>\n";
	}
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
		Register(new SidebarTemplate);
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
