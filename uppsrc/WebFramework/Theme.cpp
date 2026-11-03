#include "WebFramework.h"

NAMESPACE_UPP

void ThemeColors::Jsonize(JsonIO& io)
{
	io("bg_normal", bg_normal)("bg_alt", bg_alt)("bg_view", bg_view)
	  ("surface", surface)("surface_hover", surface_hover)
	  ("fg_normal", fg_normal)("fg_inactive", fg_inactive)("fg_muted", fg_muted)
	  ("header_bg", header_bg)("header_fg", header_fg)("header_fg_muted", header_fg_muted)("header_border", header_border)
	  ("alert_info_bg", alert_info_bg)("alert_info_border", alert_info_border)("alert_info_fg", alert_info_fg)
	  ("accent_focus", accent_focus)("accent_hover", accent_hover)("accent_link", accent_link)
	  ("color_positive", color_positive)("color_negative", color_negative)("color_neutral", color_neutral)
	  ("border_color", border_color)("border_focus", border_focus)("scrollbar_thumb", scrollbar_thumb);
}

void AppStyle::Jsonize(JsonIO& io)
{
	io("border_radius", border_radius)("button_radius", button_radius)("font_family", font_family)
	  ("shadow_sm", shadow_sm)("shadow_md", shadow_md)("shadow_lg", shadow_lg);
}

void PlasmaStyle::Jsonize(JsonIO& io)
{
	io("backdrop_blur", backdrop_blur)("panel_opacity", panel_opacity)("glow_color", glow_color);
}

WebTheme::WebTheme()
	: is_dark(true)
{
}

WebTheme::WebTheme(const String& id, const String& name, bool is_dark)
	: id(id), name(name), is_dark(is_dark)
{
}

String WebTheme::GetCssVariables() const
{
	String s;
	s << "  --wf-bg-normal: " << colors.bg_normal << ";\n"
	  << "  --wf-bg-alt: " << colors.bg_alt << ";\n"
	  << "  --wf-bg-view: " << colors.bg_view << ";\n"
	  << "  --wf-surface: " << colors.surface << ";\n"
	  << "  --wf-surface-hover: " << colors.surface_hover << ";\n"
	  << "  --wf-fg-normal: " << colors.fg_normal << ";\n"
	  << "  --wf-fg-inactive: " << colors.fg_inactive << ";\n"
	  << "  --wf-fg-muted: " << colors.fg_muted << ";\n"
	  << "  --wf-header-bg: " << (colors.header_bg.IsEmpty() ? colors.bg_normal : colors.header_bg) << ";\n"
	  << "  --wf-header-fg: " << (colors.header_fg.IsEmpty() ? colors.fg_normal : colors.header_fg) << ";\n"
	  << "  --wf-header-fg-muted: " << (colors.header_fg_muted.IsEmpty() ? colors.fg_inactive : colors.header_fg_muted) << ";\n"
	  << "  --wf-header-border: " << (colors.header_border.IsEmpty() ? colors.border_color : colors.header_border) << ";\n"
	  << "  --wf-alert-info-bg: " << (colors.alert_info_bg.IsEmpty() ? "rgba(0,161,197,0.12)" : colors.alert_info_bg) << ";\n"
	  << "  --wf-alert-info-border: " << (colors.alert_info_border.IsEmpty() ? "rgba(0,161,197,0.35)" : colors.alert_info_border) << ";\n"
	  << "  --wf-alert-info-fg: " << (colors.alert_info_fg.IsEmpty() ? "#00acd3" : colors.alert_info_fg) << ";\n"
	  << "  --wf-accent-focus: " << colors.accent_focus << ";\n"
	  << "  --wf-accent-hover: " << colors.accent_hover << ";\n"
	  << "  --wf-accent-link: " << colors.accent_link << ";\n"
	  << "  --wf-color-positive: " << colors.color_positive << ";\n"
	  << "  --wf-color-negative: " << colors.color_negative << ";\n"
	  << "  --wf-color-neutral: " << colors.color_neutral << ";\n"
	  << "  --wf-border-color: " << colors.border_color << ";\n"
	  << "  --wf-border-focus: " << colors.border_focus << ";\n"
	  << "  --wf-radius: " << app_style.border_radius << ";\n"
	  << "  --wf-btn-radius: " << app_style.button_radius << ";\n"
	  << "  --wf-blur: " << plasma_style.backdrop_blur << ";\n"
	  << "  --wf-shadow: " << app_style.shadow_md << ";\n";
	return s;
}

void WebTheme::Jsonize(JsonIO& io)
{
	io("id", id)("name", name)("is_dark", is_dark)
	  ("colors", colors)("app_style", app_style)("plasma_style", plasma_style)("icon_theme", icon_theme);
}

// ---------------------------------------------------------------------------
// ThemeRegistry
// ---------------------------------------------------------------------------

static ArrayMap<String, WebTheme>& s_themes()
{
	static ArrayMap<String, WebTheme> map;
	return map;
}

WebTheme ThemeRegistry::CreateVividDark()
{
	WebTheme t("vivid-dark", "Vivid Dark (KDE Plasma)", true);
	// Palettes from L4ki/Vivid-Plasma-Themes VividCyanDarkColorscheme.colors
	t.colors.bg_normal = "#182030";        // 24, 32, 48
	t.colors.bg_alt = "#1c202f";           // 28, 32, 47
	t.colors.bg_view = "#151d2b";          // 21, 29, 43
	t.colors.surface = "#1e283b";
	t.colors.surface_hover = "rgba(0, 161, 197, 0.16)";
	t.colors.fg_normal = "#d3dae3";        // 211, 218, 227
	t.colors.fg_inactive = "#8b919d";      // 139, 145, 157
	t.colors.fg_muted = "#64748b";
	t.colors.header_bg = "rgba(24, 32, 48, 0.88)";
	t.colors.header_fg = "#d3dae3";
	t.colors.header_fg_muted = "#8b919d";
	t.colors.header_border = "rgba(0, 161, 197, 0.25)";
	t.colors.alert_info_bg = "rgba(0, 161, 197, 0.14)";
	t.colors.alert_info_border = "rgba(0, 161, 197, 0.35)";
	t.colors.alert_info_fg = "#7dd3fc";
	t.colors.accent_focus = "#00a1c5";     // 0, 161, 197 (Vivid Cyan)
	t.colors.accent_hover = "#0088a6";     // 0, 136, 166
	t.colors.accent_link = "#00acd3";      // 0, 172, 211
	t.colors.color_positive = "#00aa7f";   // 0, 170, 127 (Emerald)
	t.colors.color_negative = "#c83771";   // 200, 55, 113 (Crimson)
	t.colors.color_neutral = "#6d55aa";    // 109, 85, 170 (Purple)
	t.colors.border_color = "rgba(0, 161, 197, 0.22)";
	t.colors.border_focus = "#00a1c5";
	t.colors.scrollbar_thumb = "#28344c";

	t.app_style.border_radius = "8px";
	t.app_style.button_radius = "6px";
	t.app_style.font_family = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif";
	t.app_style.shadow_sm = "0 1px 3px rgba(0,0,0,0.3)";
	t.app_style.shadow_md = "0 4px 12px rgba(0,0,0,0.35)";
	t.app_style.shadow_lg = "0 12px 28px rgba(0,0,0,0.45)";

	t.plasma_style.backdrop_blur = "14px";
	t.plasma_style.panel_opacity = 0.88;
	t.plasma_style.glow_color = "rgba(0, 161, 197, 0.35)";
	t.icon_theme = "vivid-dark-icons";
	return t;
}

WebTheme ThemeRegistry::CreateVividLight()
{
	WebTheme t("vivid-light", "Vivid Light (KDE Plasma)", false);
	t.colors.bg_normal = "#f8fafc";
	t.colors.bg_alt = "#f1f5f9";
	t.colors.bg_view = "#ffffff";
	t.colors.surface = "#ffffff";
	t.colors.surface_hover = "rgba(0, 136, 166, 0.08)";
	t.colors.fg_normal = "#0f172a";
	t.colors.fg_inactive = "#475569";
	t.colors.fg_muted = "#94a3b8";
	t.colors.header_bg = "rgba(255, 255, 255, 0.94)";
	t.colors.header_fg = "#0f172a";
	t.colors.header_fg_muted = "#64748b";
	t.colors.header_border = "rgba(0, 136, 166, 0.22)";
	t.colors.alert_info_bg = "rgba(0, 136, 166, 0.10)";
	t.colors.alert_info_border = "rgba(0, 136, 166, 0.35)";
	t.colors.alert_info_fg = "#0369a1"; // Crisp readable cyan/blue on light background
	t.colors.accent_focus = "#0088a6";
	t.colors.accent_hover = "#00a1c5";
	t.colors.accent_link = "#0284c7";
	t.colors.color_positive = "#059669";
	t.colors.color_negative = "#e11d48";
	t.colors.color_neutral = "#7c3aed";
	t.colors.border_color = "#e2e8f0";
	t.colors.border_focus = "#0088a6";
	t.colors.scrollbar_thumb = "#cbd5e1";

	t.app_style.border_radius = "8px";
	t.app_style.button_radius = "6px";
	t.app_style.font_family = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif";
	t.app_style.shadow_sm = "0 1px 2px rgba(0,0,0,0.06)";
	t.app_style.shadow_md = "0 4px 6px -1px rgba(0,0,0,0.08)";
	t.app_style.shadow_lg = "0 10px 15px -3px rgba(0,0,0,0.1)";

	t.plasma_style.backdrop_blur = "10px";
	t.plasma_style.panel_opacity = 0.92;
	t.plasma_style.glow_color = "rgba(0, 136, 166, 0.25)";
	t.icon_theme = "vivid-light-icons";
	return t;
}

WebTheme ThemeRegistry::CreateVividMixed()
{
	WebTheme t("vivid-mixed", "Vivid Mixed (KDE Plasma Twilight)", false);
	t.colors.bg_normal = "#f4f6fa";
	t.colors.bg_alt = "#e2e8f0";
	t.colors.bg_view = "#ffffff";
	t.colors.surface = "#ffffff";
	t.colors.surface_hover = "rgba(0, 161, 197, 0.08)";
	t.colors.fg_normal = "#0f172a";
	t.colors.fg_inactive = "#475569";
	t.colors.fg_muted = "#64748b";
	t.colors.header_bg = "rgba(24, 32, 48, 0.94)"; // Dark frosted glass top header
	t.colors.header_fg = "#f1f5f9"; // Crisp light text on dark header
	t.colors.header_fg_muted = "#94a3b8";
	t.colors.header_border = "rgba(0, 161, 197, 0.35)"; // Crisp cyan separation lines in dark header
	t.colors.alert_info_bg = "rgba(0, 161, 197, 0.10)";
	t.colors.alert_info_border = "rgba(0, 161, 197, 0.35)";
	t.colors.alert_info_fg = "#0369a1";
	t.colors.accent_focus = "#00a1c5";
	t.colors.accent_hover = "#0088a6";
	t.colors.accent_link = "#0284c7";
	t.colors.color_positive = "#059669";
	t.colors.color_negative = "#e11d48";
	t.colors.color_neutral = "#7c3aed";
	t.colors.border_color = "rgba(15, 23, 42, 0.12)";
	t.colors.border_focus = "#00a1c5";
	t.colors.scrollbar_thumb = "#94a3b8";

	t.app_style.border_radius = "8px";
	t.app_style.button_radius = "6px";
	t.app_style.font_family = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif";
	t.app_style.shadow_sm = "0 1px 2px rgba(0,0,0,0.06)";
	t.app_style.shadow_md = "0 4px 6px -1px rgba(0,0,0,0.08)";
	t.app_style.shadow_lg = "0 10px 15px -3px rgba(0,0,0,0.1)";

	t.plasma_style.backdrop_blur = "14px";
	t.plasma_style.panel_opacity = 0.92;
	t.plasma_style.glow_color = "rgba(0, 161, 197, 0.35)";
	t.icon_theme = "vivid-dark-icons";
	return t;
}

WebTheme ThemeRegistry::CreateGitHubDark()
{
	WebTheme t("github-dark", "GitHub Dark Default", true);
	t.colors.bg_normal = "#0d1117";
	t.colors.bg_alt = "#161b22";
	t.colors.bg_view = "#0d1117";
	t.colors.surface = "#21262d";
	t.colors.surface_hover = "#30363d";
	t.colors.fg_normal = "#e6edf3";
	t.colors.fg_inactive = "#7d8590";
	t.colors.fg_muted = "#848d97";
	t.colors.header_bg = "#010409";
	t.colors.header_fg = "#f0f6fc";
	t.colors.header_fg_muted = "#7d8590";
	t.colors.header_border = "#30363d";
	t.colors.alert_info_bg = "rgba(56,139,253,0.15)";
	t.colors.alert_info_border = "rgba(56,139,253,0.4)";
	t.colors.alert_info_fg = "#58a6ff";
	t.colors.accent_focus = "#2f81f7";
	t.colors.accent_hover = "#58a6ff";
	t.colors.accent_link = "#2f81f7";
	t.colors.color_positive = "#238636";
	t.colors.color_negative = "#da3633";
	t.colors.color_neutral = "#8957e5";
	t.colors.border_color = "#30363d";
	t.colors.border_focus = "#58a6ff";
	t.colors.scrollbar_thumb = "#484f58";

	t.app_style.border_radius = "6px";
	t.app_style.button_radius = "6px";
	t.app_style.font_family = "-apple-system, BlinkMacSystemFont, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif";
	t.app_style.shadow_sm = "0 1px 0 rgba(1,4,9,0.85)";
	t.app_style.shadow_md = "0 3px 6px rgba(1,4,9,0.85)";
	t.app_style.shadow_lg = "0 8px 24px rgba(1,4,9,0.85)";

	t.plasma_style.backdrop_blur = "0px";
	t.plasma_style.panel_opacity = 1.0;
	t.plasma_style.glow_color = "rgba(56,139,253,0.2)";
	t.icon_theme = "octicons";
	return t;
}

WebTheme ThemeRegistry::CreateGitHubLight()
{
	WebTheme t("github-light", "GitHub Light Default", false);
	t.colors.bg_normal = "#ffffff";
	t.colors.bg_alt = "#f6f8fa";
	t.colors.bg_view = "#ffffff";
	t.colors.surface = "#f6f8fa";
	t.colors.surface_hover = "#eaeef2";
	t.colors.fg_normal = "#1f2328";
	t.colors.fg_inactive = "#656d76";
	t.colors.fg_muted = "#6e7781";
	t.colors.header_bg = "#24292f";
	t.colors.header_fg = "#ffffff";
	t.colors.header_fg_muted = "#d0d7de";
	t.colors.header_border = "#d0d7de";
	t.colors.alert_info_bg = "#ddf4ff";
	t.colors.alert_info_border = "#54aeff";
	t.colors.alert_info_fg = "#0969da";
	t.colors.accent_focus = "#0969da";
	t.colors.accent_hover = "#218bff";
	t.colors.accent_link = "#0969da";
	t.colors.color_positive = "#1a7f37";
	t.colors.color_negative = "#cf222e";
	t.colors.color_neutral = "#8250df";
	t.colors.border_color = "#d0d7de";
	t.colors.border_focus = "#0969da";
	t.colors.scrollbar_thumb = "#afb8c1";

	t.app_style.border_radius = "6px";
	t.app_style.button_radius = "6px";
	t.app_style.font_family = "-apple-system, BlinkMacSystemFont, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif";
	t.app_style.shadow_sm = "0 1px 0 rgba(31,35,40,0.04)";
	t.app_style.shadow_md = "0 3px 6px rgba(140,149,159,0.15)";
	t.app_style.shadow_lg = "0 8px 24px rgba(140,149,159,0.2)";

	t.plasma_style.backdrop_blur = "0px";
	t.plasma_style.panel_opacity = 1.0;
	t.plasma_style.glow_color = "rgba(9,105,218,0.2)";
	t.icon_theme = "octicons";
	return t;
}

WebTheme ThemeRegistry::CreateSourceForgeDark()
{
	WebTheme t("sourceforge-dark", "SourceForge Dark (Tangerine)", true);
	t.colors.bg_normal = "#1a1d21";
	t.colors.bg_alt = "#22262b";
	t.colors.bg_view = "#181a1d";
	t.colors.surface = "#272b30";
	t.colors.surface_hover = "#32373e";
	t.colors.fg_normal = "#e6e8eb";
	t.colors.fg_inactive = "#8c929a";
	t.colors.fg_muted = "#9da4ad";
	t.colors.header_bg = "#121417";
	t.colors.header_fg = "#ffffff";
	t.colors.header_fg_muted = "#8c929a";
	t.colors.header_border = "#2e333a";
	t.colors.alert_info_bg = "rgba(238,101,36,0.12)";
	t.colors.alert_info_border = "rgba(238,101,36,0.4)";
	t.colors.alert_info_fg = "#f76707";
	t.colors.accent_focus = "#ee6524"; // SourceForge Orange
	t.colors.accent_hover = "#ff7836";
	t.colors.accent_link = "#ee6524";
	t.colors.color_positive = "#069c56";
	t.colors.color_negative = "#d9230f";
	t.colors.color_neutral = "#2c88d9";
	t.colors.border_color = "#393e46";
	t.colors.border_focus = "#ee6524";
	t.colors.scrollbar_thumb = "#4a515a";

	t.app_style.border_radius = "4px";
	t.app_style.button_radius = "4px";
	t.app_style.font_family = "Helvetica Neue, Helvetica, Arial, sans-serif";
	t.app_style.shadow_sm = "0 1px 2px rgba(0,0,0,0.3)";
	t.app_style.shadow_md = "0 2px 6px rgba(0,0,0,0.4)";
	t.app_style.shadow_lg = "0 4px 14px rgba(0,0,0,0.5)";

	t.plasma_style.backdrop_blur = "0px";
	t.plasma_style.panel_opacity = 1.0;
	t.plasma_style.glow_color = "rgba(238,101,36,0.3)";
	t.icon_theme = "sf-icons";
	return t;
}

WebTheme ThemeRegistry::CreateSourceForgeClassic()
{
	WebTheme t("sourceforge-classic", "SourceForge Classic (Light)", false);
	t.colors.bg_normal = "#f4f6f8";
	t.colors.bg_alt = "#ffffff";
	t.colors.bg_view = "#ffffff";
	t.colors.surface = "#ffffff";
	t.colors.surface_hover = "#eef2f5";
	t.colors.fg_normal = "#262a2e";
	t.colors.fg_inactive = "#687078";
	t.colors.fg_muted = "#78818a";
	t.colors.header_bg = "#22252a";
	t.colors.header_fg = "#ffffff";
	t.colors.header_fg_muted = "#d6dade";
	t.colors.header_border = "#16181b";
	t.colors.alert_info_bg = "#fff4e6";
	t.colors.alert_info_border = "#ffd8a8";
	t.colors.alert_info_fg = "#d9480f";
	t.colors.accent_focus = "#ee6524"; // SourceForge Orange
	t.colors.accent_hover = "#d85416";
	t.colors.accent_link = "#00739c";
	t.colors.color_positive = "#069c56";
	t.colors.color_negative = "#d9230f";
	t.colors.color_neutral = "#00739c";
	t.colors.border_color = "#d6dade";
	t.colors.border_focus = "#ee6524";
	t.colors.scrollbar_thumb = "#b8bfc6";

	t.app_style.border_radius = "4px";
	t.app_style.button_radius = "4px";
	t.app_style.font_family = "Helvetica Neue, Helvetica, Arial, sans-serif";
	t.app_style.shadow_sm = "0 1px 2px rgba(0,0,0,0.06)";
	t.app_style.shadow_md = "0 2px 6px rgba(0,0,0,0.08)";
	t.app_style.shadow_lg = "0 4px 14px rgba(0,0,0,0.12)";

	t.plasma_style.backdrop_blur = "0px";
	t.plasma_style.panel_opacity = 1.0;
	t.plasma_style.glow_color = "rgba(238,101,36,0.2)";
	t.icon_theme = "sf-icons";
	return t;
}

void ThemeRegistry::EnsureBuiltIn()
{
	if(s_themes().IsEmpty()) {
		Register(CreateVividDark());
		Register(CreateVividLight());
		Register(CreateVividMixed());
		Register(CreateGitHubDark());
		Register(CreateGitHubLight());
		Register(CreateSourceForgeDark());
		Register(CreateSourceForgeClassic());
	}
}

void ThemeRegistry::Register(const WebTheme& theme)
{
	s_themes().GetAdd(theme.id) = theme;
}

const WebTheme* ThemeRegistry::Find(const String& id)
{
	EnsureBuiltIn();
	int q = s_themes().Find(id);
	return q >= 0 ? &s_themes()[q] : NULL;
}

const WebTheme& ThemeRegistry::GetDefault()
{
	EnsureBuiltIn();
	return s_themes()[0]; // VividDark
}

Vector<String> ThemeRegistry::ListThemeIds()
{
	EnsureBuiltIn();
	Vector<String> res;
	for(int i = 0; i < s_themes().GetCount(); i++)
		res.Add(s_themes().GetKey(i));
	return res;
}

String ThemeRegistry::GetGlobalStyles()
{
	EnsureBuiltIn();
	String s;
	s << ":root {\n"
	  << CreateVividDark().GetCssVariables()
	  << "}\n";
	for(int i = 0; i < s_themes().GetCount(); i++) {
		const WebTheme& t = s_themes()[i];
		s << "[data-theme=\"" << t.id << "\"], html[data-theme=\"" << t.id << "\"], [data-wf-theme=\"" << t.id << "\"] {\n"
		  << t.GetCssVariables()
		  << "}\n";
	}
	return s;
}

String ThemeRegistry::GetClientScript()
{
	return
		"var WebThemeManager = (function() {\n"
		"  let themes = ['vivid-dark', 'vivid-light', 'vivid-mixed', 'github-dark', 'github-light', 'sourceforge-dark', 'sourceforge-classic'];\n"
		"  let current = localStorage.getItem('wf_theme') || 'vivid-dark';\n"
		"  function apply(themeId) {\n"
		"    current = themeId;\n"
		"    localStorage.setItem('wf_theme', themeId);\n"
		"    document.documentElement.setAttribute('data-theme', themeId);\n"
		"    let btn = document.getElementById('wf-btn-theme');\n"
		"    if (btn) {\n"
		"      let label = btn.querySelector('.wf-btn-label');\n"
		"      if (label) {\n"
		"        if (themeId === 'vivid-dark') label.innerText = 'Dark';\n"
		"        else if (themeId === 'vivid-light') label.innerText = 'Light';\n"
		"        else if (themeId === 'vivid-mixed') label.innerText = 'Mixed';\n"
		"        else if (themeId === 'github-dark') label.innerText = 'GH Dark';\n"
		"        else if (themeId === 'github-light') label.innerText = 'GH Light';\n"
		"        else if (themeId === 'sourceforge-dark') label.innerText = 'SF Dark';\n"
		"        else if (themeId === 'sourceforge-classic') label.innerText = 'SF Classic';\n"
		"        else label.innerText = themeId;\n"
		"      }\n"
		"    }\n"
		"    let indicators = document.querySelectorAll('.wf-theme-indicator');\n"
		"    indicators.forEach(function(el) { el.innerText = themeId; });\n"
		"    if (window.WebAudio) WebAudio.play('switch');\n"
		"  }\n"
		"  function cycle() {\n"
		"    let idx = themes.indexOf(current);\n"
		"    let next = themes[(idx + 1) % themes.length];\n"
		"    apply(next);\n"
		"  }\n"
		"  function init() {\n"
		"    let saved = localStorage.getItem('wf_theme');\n"
		"    if (saved) apply(saved);\n"
		"    else {\n"
		"      let curAttr = document.documentElement.getAttribute('data-theme');\n"
		"      if (curAttr) current = curAttr;\n"
		"      apply(current);\n"
		"    }\n"
		"  }\n"
		"  return {\n"
		"    get: function() { return current; },\n"
		"    set: apply,\n"
		"    toggleTheme: cycle,\n"
		"    init: init\n"
		"  };\n"
		"})();\n"
		"if (document.readyState === 'loading') {\n"
		"  document.addEventListener('DOMContentLoaded', WebThemeManager.init);\n"
		"} else {\n"
		"  WebThemeManager.init();\n"
		"}\n";
}

END_UPP_NAMESPACE
