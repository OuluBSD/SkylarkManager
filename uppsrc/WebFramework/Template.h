#ifndef _WebFramework_Template_h_
#define _WebFramework_Template_h_

// Templates & Density System (Decoupled from Theme Styling)

enum DensityLevel {
	DENSITY_NONE = 0,    // 0px padding, edge-to-edge
	DENSITY_DENSE = 1,   // 4-8px padding, tight spacing for dense/crowded content
	DENSITY_COMPACT = 2, // 10-14px padding
	DENSITY_COMFORTABLE = 3, // 16-24px padding (default)
	DENSITY_SPACIOUS = 4 // 28-36px padding
};

struct DensityConfig : Moveable<DensityConfig> {
	DensityLevel level;
	String       container_max_width; // e.g. "1400px", "100%"
	String       padding_page;        // e.g. "0.5rem", "1.5rem"
	String       padding_card;        // e.g. "8px", "16px"
	String       gap_grid;            // e.g. "8px", "16px"

	DensityConfig();
	DensityConfig(DensityLevel level);

	void Init(DensityLevel lvl);
	String GetCssVariables() const;
	static DensityConfig ForLevel(DensityLevel lvl);
	static String GetAllDensityStyles();
	static String GetClientScript();
};

class TemplateSlots : Moveable<TemplateSlots> {
public:
	const String& GetHeader() const { return header; }
	const String& GetSubNav() const { return sub_nav; }
	const String& GetSidebar() const { return sidebar; }
	const String& GetLeftSidebar() const { return left_sidebar; }
	const String& GetRightSidebar() const { return right_sidebar; }
	const String& GetBreadcrumb() const { return breadcrumb; }
	const String& GetContent() const { return content; }
	const String& GetFooter() const { return footer; }
	const String& GetModals() const { return modals; }

private:
	friend class PageBuilder;
	friend class MegaMenuTemplate;
	friend class DashboardTemplate;
	friend class AppWorkspaceTemplate;
	friend class GitHubTemplate;
	friend class SourceForgeTemplate;
	String header;
	String sub_nav;
	String sidebar;
	String left_sidebar;
	String right_sidebar;
	String breadcrumb;
	String content;
	String footer;
	String modals;

	TemplateSlots() {}
};

class WebTemplate : Moveable<WebTemplate> {
public:
	String id;
	String name;

	WebTemplate() {}
	WebTemplate(const String& id, const String& name) : id(id), name(name) {}
	virtual ~WebTemplate() {}

	virtual String Render(const TemplateSlots& slots, const DensityConfig& density) const = 0;
	virtual String GetStyles() const = 0;
};

class MegaMenuTemplate : public WebTemplate {
public:
	MegaMenuTemplate();
	String Render(const TemplateSlots& slots, const DensityConfig& density) const override;
	String GetStyles() const override;
};

class DashboardTemplate : public WebTemplate {
public:
	DashboardTemplate();
	String Render(const TemplateSlots& slots, const DensityConfig& density) const override;
	String GetStyles() const override;
};

class AppWorkspaceTemplate : public WebTemplate {
public:
	AppWorkspaceTemplate();
	String Render(const TemplateSlots& slots, const DensityConfig& density) const override;
	String GetStyles() const override;
};

class GitHubTemplate : public WebTemplate {
public:
	GitHubTemplate();
	String Render(const TemplateSlots& slots, const DensityConfig& density) const override;
	String GetStyles() const override;
};

class SourceForgeTemplate : public WebTemplate {
public:
	SourceForgeTemplate();
	String Render(const TemplateSlots& slots, const DensityConfig& density) const override;
	String GetStyles() const override;
};

class TemplateRegistry {
public:
	static void Register(WebTemplate* t);
	static const WebTemplate* Find(const String& id);
	static const WebTemplate& GetDefault();
	static Vector<String> ListTemplateIds();

private:
	static void EnsureBuiltIn();
};

#endif
