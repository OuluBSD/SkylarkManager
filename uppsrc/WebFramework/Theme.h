#ifndef _WebFramework_Theme_h_
#define _WebFramework_Theme_h_

// KDE Plasma-inspired Modular Theme Engine

struct ThemeColors : Moveable<ThemeColors> {
	String bg_normal;
	String bg_alt;
	String bg_view;
	String surface;
	String surface_hover;
	String fg_normal;
	String fg_inactive;
	String fg_muted;
	String header_bg;
	String header_fg;
	String header_fg_muted;
	String header_border;
	String alert_info_bg;
	String alert_info_border;
	String alert_info_fg;
	String accent_focus;
	String accent_hover;
	String accent_link;
	String color_positive;
	String color_negative;
	String color_neutral;
	String border_color;
	String border_focus;
	String scrollbar_thumb;

	void Jsonize(JsonIO& io);
};

struct AppStyle : Moveable<AppStyle> {
	String border_radius;
	String button_radius;
	String font_family;
	String shadow_sm;
	String shadow_md;
	String shadow_lg;

	void Jsonize(JsonIO& io);
};

struct PlasmaStyle : Moveable<PlasmaStyle> {
	String backdrop_blur;
	double panel_opacity;
	String glow_color;

	void Jsonize(JsonIO& io);
};

class WebTheme : Moveable<WebTheme> {
public:
	String      id;
	String      name;
	bool        is_dark;
	ThemeColors colors;
	AppStyle    app_style;
	PlasmaStyle plasma_style;
	String      icon_theme;

	WebTheme();
	WebTheme(const String& id, const String& name, bool is_dark = true);

	String GetCssVariables() const;
	void Jsonize(JsonIO& io);
};

class ThemeRegistry {
public:
	static void Register(const WebTheme& theme);
	static const WebTheme* Find(const String& id);
	static const WebTheme& GetDefault();
	static Vector<String> ListThemeIds();

	static WebTheme CreateVividDark();
	static WebTheme CreateVividLight();
	static WebTheme CreateVividMixed();
	static WebTheme CreateGitHubDark();
	static WebTheme CreateGitHubLight();
	static WebTheme CreateSourceForgeDark();
	static WebTheme CreateSourceForgeClassic();

	static String GetClientScript();
	static String GetGlobalStyles();

private:
	static void EnsureBuiltIn();
};

#endif
