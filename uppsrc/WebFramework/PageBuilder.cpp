#include "WebFramework.h"

NAMESPACE_UPP

static String EscapePageText(const String& s)
{
	String out;
	for(int i = 0; i < s.GetCount(); i++) {
		switch(s[i]) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(s[i]); break;
		}
	}
	return out;
}

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
	return
		"  * { box-sizing: border-box; margin: 0; padding: 0; }\n"
		"  body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background: var(--wf-bg-normal, #182030); color: var(--wf-fg-normal, #d3dae3); min-height: 100vh; line-height: 1.5; }\n"
		"  a { color: var(--wf-accent-link, #00acd3); text-decoration: none; }\n"
		"  a:hover { text-decoration: underline; }\n"
		"\n"
		"  /* Cards */\n"
		"  .wf-card { background: var(--wf-surface, #1e283b); border: 1px solid var(--wf-border-color, rgba(0,161,197,0.2)); border-radius: var(--wf-radius, 8px); box-shadow: var(--wf-shadow, 0 4px 12px rgba(0,0,0,0.3)); overflow: hidden; transition: transform 0.15s, box-shadow 0.15s; }\n"
		"  .wf-card:hover { border-color: var(--wf-accent-focus, #00a1c5); }\n"
		"  .wf-card-header { padding: var(--wf-cell-pad, 10px 14px); border-bottom: 1px solid var(--wf-border-color, rgba(255,255,255,0.08)); font-weight: 700; font-size: 0.95rem; display: flex; justify-content: space-between; align-items: center; color: var(--wf-fg-normal, #ffffff); background: var(--wf-bg-alt, #1c202f); }\n"
		"  .wf-card-body { padding: var(--wf-pad-card, 16px 20px); font-size: 0.9rem; }\n"
		"  .wf-card-footer { padding: var(--wf-cell-pad, 8px 14px); border-top: 1px solid var(--wf-border-color, rgba(255,255,255,0.06)); font-size: 0.8rem; color: var(--wf-fg-inactive, #8b919d); display: flex; justify-content: space-between; align-items: center; background: var(--wf-bg-alt, #1c202f); }\n"
		"\n"
		"  /* Buttons */\n"
		"  .wf-btn { display: inline-flex; align-items: center; justify-content: center; gap: 6px; padding: var(--wf-btn-pad, 7px 14px); border-radius: var(--wf-btn-radius, 6px); font-size: 0.85rem; font-weight: 600; cursor: pointer; border: 1px solid transparent; transition: all 0.15s; text-decoration: none; }\n"
		"  .wf-btn-primary { background: var(--wf-accent-focus, #00a1c5); color: #ffffff; }\n"
		"  .wf-btn-primary:hover { background: var(--wf-accent-hover, #0088a6); color: #ffffff; text-decoration: none; }\n"
		"  .wf-btn-secondary { background: var(--wf-surface, #1e283b); color: var(--wf-fg-normal, #d3dae3); border-color: var(--wf-border-color, rgba(255,255,255,0.15)); }\n"
		"  .wf-btn-secondary:hover { background: var(--wf-surface-hover, rgba(0,161,197,0.15)); color: #ffffff; text-decoration: none; }\n"
		"  .wf-btn-positive { background: var(--wf-color-positive, #00aa7f); color: #ffffff; }\n"
		"  .wf-btn-positive:hover { filter: brightness(1.1); color: #ffffff; text-decoration: none; }\n"
		"  .wf-btn-danger { background: var(--wf-color-negative, #c83771); color: #ffffff; }\n"
		"  .wf-btn-danger:hover { filter: brightness(1.1); color: #ffffff; text-decoration: none; }\n"
		"\n"
		"  /* Grids */\n"
		"  .wf-grid { display: grid; gap: var(--wf-gap-grid, 16px); }\n"
		"  .wf-grid-2 { grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); }\n"
		"  .wf-grid-3 { grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); }\n"
		"  .wf-grid-4 { grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); }\n"
		"\n"
		"  /* Tables */\n"
		"  .wf-table { width: 100%; border-collapse: collapse; font-size: 0.88rem; }\n"
		"  .wf-table th { background: var(--wf-bg-alt, #1c202f); color: var(--wf-fg-normal, #ffffff); font-weight: 700; text-align: left; padding: var(--wf-cell-pad, 10px 14px); border-bottom: 2px solid var(--wf-border-color, rgba(0,161,197,0.3)); font-size: 0.8rem; text-transform: uppercase; letter-spacing: 0.05em; }\n"
		"  .wf-table td { padding: var(--wf-cell-pad, 9px 14px); border-bottom: 1px solid var(--wf-border-color, rgba(255,255,255,0.06)); }\n"
		"  .wf-table tr:hover td { background: var(--wf-surface-hover, rgba(0,161,197,0.08)); }\n"
		"\n"
		"  /* Alerts */\n"
		"  .wf-alert { padding: var(--wf-cell-pad, 12px 16px); border-radius: var(--wf-radius, 8px); font-size: 0.88rem; display: flex; align-items: center; gap: 10px; margin-bottom: 1rem; }\n"
		"  .wf-alert-info { background: var(--wf-alert-info-bg, rgba(0, 161, 197, 0.12)); border: 1px solid var(--wf-alert-info-border, rgba(0, 161, 197, 0.35)); color: var(--wf-alert-info-fg, #0284c7); }\n"
		"  .wf-alert-success { background: var(--wf-alert-success-bg, rgba(0, 170, 127, 0.12)); border: 1px solid var(--wf-alert-success-border, rgba(0, 170, 127, 0.35)); color: var(--wf-alert-success-fg, #059669); }\n"
		"  .wf-alert-warning { background: var(--wf-alert-warning-bg, rgba(234, 179, 8, 0.12)); border: 1px solid var(--wf-alert-warning-border, rgba(234, 179, 8, 0.35)); color: var(--wf-alert-warning-fg, #b45309); }\n"
		"  .wf-alert-danger { background: var(--wf-alert-danger-bg, rgba(200, 55, 113, 0.12)); border: 1px solid var(--wf-alert-danger-border, rgba(200, 55, 113, 0.35)); color: var(--wf-alert-danger-fg, #e11d48); }\n"
		"\n"
		"  /* Splash Screen */\n"
		"  .wf-splash-overlay { position: fixed; inset: 0; background: var(--wf-bg-normal, #182030); z-index: 99999; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 16px; transition: opacity 0.3s ease, visibility 0.3s; }\n"
		"  .wf-splash-overlay.hidden { opacity: 0; visibility: hidden; pointer-events: none; }\n"
		"  .wf-splash-spinner { width: 44px; height: 44px; border: 3px solid rgba(0,161,197,0.2); border-top-color: var(--wf-accent-focus, #00a1c5); border-radius: 50%; animation: wf-spin 0.8s linear infinite; }\n"
		"  @keyframes wf-spin { to { transform: rotate(360deg); } }\n";
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
	     << "<html lang=\"en\" data-theme=\"" << EscapePageText(theme_id) << "\" data-density=\"" << density_str << "\">\n"
	     << "<head>\n"
	     << "  <meta charset=\"UTF-8\">\n"
	     << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
	     << "  <title>" << EscapePageText(title) << "</title>\n"
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
