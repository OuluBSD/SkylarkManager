#ifndef _WebFramework_PageBuilder_h_
#define _WebFramework_PageBuilder_h_

// Complete Page Assembly Pipeline

class PageBuilder {
public:
	PageBuilder();

	PageBuilder& Title(const String& title) { this->title = title; return *this; }
	PageBuilder& Brand(const String& title, const String& url = "/", const String& icon = "vivid") {
		brand_title = title; brand_url = url; brand_icon = icon; return *this;
	}
	PageBuilder& Navigation(const NavigationModel& nav) { this->nav = nav; return *this; }
	PageBuilder& Template(const String& template_id) { this->template_id = template_id; return *this; }
	PageBuilder& Theme(const String& theme_id) { this->theme_id = theme_id; return *this; }
	PageBuilder& Density(DensityLevel level) { this->density = DensityConfig::ForLevel(level); return *this; }

	// Raw-markup slots are explicitly trusted APIs, not escaping/sanitizing
	// boundaries. Callers must construct markup with context-escaped values.
	PageBuilder& TrustedContent(const String& html) { slots.content = html; return *this; }
	PageBuilder& TrustedHeader(const String& html) { slots.header = html; return *this; }
	PageBuilder& TrustedNavActions(const String& html) { nav_actions = html; return *this; }
	PageBuilder& TrustedSidebar(const String& html) { slots.sidebar = html; slots.left_sidebar = html; return *this; }
	PageBuilder& TrustedLeftSidebar(const String& html) { slots.left_sidebar = html; return *this; }
	PageBuilder& TrustedRightSidebar(const String& html) { slots.right_sidebar = html; return *this; }
	PageBuilder& LeftTree(const TreeModel& tree, const String& title = "Navigator", const String& tree_id = "wf-left-tree", bool show_filter = true);
	PageBuilder& RightTree(const TreeModel& tree, const String& title = "Outline", const String& tree_id = "wf-right-tree", bool show_filter = true);
	PageBuilder& AutoOutline(bool enable = true) { auto_outline = enable; return *this; }
	PageBuilder& TrustedBreadcrumb(const String& html) { slots.breadcrumb = html; return *this; }
	PageBuilder& TrustedSubNav(const String& html) { slots.sub_nav = html; return *this; }
	PageBuilder& TrustedFooter(const String& html) { slots.footer = html; return *this; }
	PageBuilder& TrustedModals(const String& html) { slots.modals = html; return *this; }
	// Static trusted markup or executable source only. Never concatenate
	// imported data or request values into these slots.
	PageBuilder& TrustedExtraHead(const String& html) { extra_head << html << "\n"; return *this; }
	PageBuilder& TrustedExtraScripts(const String& js) { extra_scripts << js << "\n"; return *this; }
	PageBuilder& ShowSplash(bool b = true) { show_splash = b; return *this; }

	String Build() const;

	static String GetBaseComponentStyles();
	static String GetSplashScreenHtml();

private:
	String          title;
	String          brand_title;
	String          brand_url;
	String          brand_icon;
	NavigationModel nav;
	String          template_id;
	String          theme_id;
	DensityConfig   density;
	TemplateSlots   slots;
	String          extra_head;
	String          extra_scripts;
	String          nav_actions;
	bool            show_splash;
	bool            auto_outline;
};

#endif
