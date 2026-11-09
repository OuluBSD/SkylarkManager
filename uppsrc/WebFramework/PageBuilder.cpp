#include "WebFramework.h"

NAMESPACE_UPP

PageBuilder::PageBuilder()
	: title("Web Application")
	, brand_title("WebFramework")
	, brand_url("/")
	, brand_icon("vivid")
	, template_id("megamenu")
	, theme_id("vivid-dark")
	, show_splash(true)
	, auto_outline(false)
{
}

PageBuilder& PageBuilder::LeftTree(const TreeModel& tree, const String& title, const String& tree_id, bool show_filter)
{
	slots.left_sidebar = TreeRenderer::Render(tree, tree_id, title, show_filter);
	return *this;
}

PageBuilder& PageBuilder::RightTree(const TreeModel& tree, const String& title, const String& tree_id, bool show_filter)
{
	slots.right_sidebar = TreeRenderer::Render(tree, tree_id, title, show_filter);
	return *this;
}

String PageBuilder::GetBaseComponentStyles()
{
	return WebFrameworkAsset("base-components.css");
}

String PageBuilder::GetSplashScreenHtml()
{
	return
		"<div id=\"wf-splash\" class=\"wf-splash-overlay\">\n"
		"  <div class=\"wf-splash-spinner\"></div>\n"
		"  <div style=\"font-size:0.9rem; font-weight:600; color:var(--wf-fg-normal,#d3dae3);\">Loading...</div>\n"
		"</div>\n"
		"<script>\n"
		"  window.addEventListener('load', function() {\n"
		"    let s = document.getElementById('wf-splash');\n"
		"    if (s) setTimeout(function() { s.classList.add('hidden'); }, 120);\n"
		"  });\n"
		"</script>\n";
}

String PageBuilder::Build() const
{
	const WebTemplate* tmpl = TemplateRegistry::Find(template_id);
	if(!tmpl) tmpl = &TemplateRegistry::GetDefault();

	// Prepare slots
	TemplateSlots effective_slots = slots;
	if(effective_slots.header.IsEmpty()) {
		effective_slots.header = MegaMenuRenderer::Render(nav, brand_title, brand_url, brand_icon, nav_actions);
	}

	// Automatic Page Outline generation (Table of Contents)
	if(auto_outline && effective_slots.right_sidebar.IsEmpty() && !effective_slots.content.IsEmpty()) {
		TreeModel toc = TreeGenerators::FromHeadings(effective_slots.content);
		if(!toc.IsEmpty()) {
			effective_slots.right_sidebar = TreeRenderer::Render(toc, "wf-toc-tree", "Page Outline", false, true);
		}
	}

	String body = tmpl->Render(effective_slots, density);

	String density_str;
	switch(density.level) {
	case DENSITY_NONE: density_str = "none"; break;
	case DENSITY_DENSE: density_str = "dense"; break;
	case DENSITY_COMPACT: density_str = "compact"; break;
	case DENSITY_SPACIOUS: density_str = "spacious"; break;
	default: density_str = "comfortable"; break;
	}

	String html;
	html << "<!DOCTYPE html>\n"
	     << "<html lang=\"en\" data-theme=\"" << theme_id << "\" data-density=\"" << density_str << "\">\n"
	     << "<head>\n"
	     << "  <meta charset=\"UTF-8\">\n"
	     << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
	     << "  <title>" << title << "</title>\n"
	     << "  <style>\n"
	     << ThemeRegistry::GetGlobalStyles() << "\n"
	     << DensityConfig::GetAllDensityStyles() << "\n"
	     << GetBaseComponentStyles() << "\n"
	     << MegaMenuRenderer::GetStyles() << "\n"
	     << TreeRenderer::GetStyles() << "\n"
	     << tmpl->GetStyles() << "\n"
	     << "  </style>\n"
	     << extra_head << "\n"
	     << "</head>\n"
	     << "<body>\n";

	if(show_splash) {
		html << GetSplashScreenHtml() << "\n";
	}

	html << body << "\n";

	// Scripts
	html << "<script>\n"
	     << WebAudioSystem::GetClientScript() << "\n"
	     << ThemeRegistry::GetClientScript() << "\n"
	     << DensityConfig::GetClientScript() << "\n"
	     << MegaMenuRenderer::GetClientScript() << "\n"
	     << TreeRenderer::GetClientScript() << "\n"
	     << extra_scripts << "\n"
	     << "</script>\n"
	     << "</body>\n"
	     << "</html>\n";

	return html;
}

END_UPP_NAMESPACE
