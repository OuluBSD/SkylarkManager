#ifndef _WebFramework_Navigation_h_
#define _WebFramework_Navigation_h_

// Scalable Mega-Menu and Categorized Navigation Model

struct NavBadge : Moveable<NavBadge> {
	String text;
	String style; // "cyan", "emerald", "crimson", "purple", "neutral"

	NavBadge() {}
	NavBadge(const String& t, const String& s = "cyan") : text(t), style(s) {}
	void Jsonize(JsonIO& io) { io("text", text)("style", style); }
};

struct NavItem : Moveable<NavItem> {
	String   id;
	String   title;
	String   url;
	String   icon;
	String   description;
	String   shortcut;
	NavBadge badge;
	bool     is_active;

	NavItem();
	NavItem(const String& id, const String& title, const String& url, const String& icon = "", const String& desc = "");

	NavItem& Badge(const String& text, const String& style = "cyan") { badge = NavBadge(text, style); return *this; }
	NavItem& Shortcut(const String& s) { shortcut = s; return *this; }
	NavItem& Active(bool b = true) { is_active = b; return *this; }

	void Jsonize(JsonIO& io);
};

struct NavSection : Moveable<NavSection> {
	String          title;
	Vector<NavItem> items;

	NavSection() {}
	NavSection(const String& t) : title(t) {}
	NavSection(const NavSection& o);
	NavSection& operator=(const NavSection& o);

	NavItem& Add(const NavItem& item) { items.Add(item); return items.Top(); }
	NavItem& Add(const String& id, const String& title, const String& url, const String& icon = "", const String& desc = "") {
		return Add(NavItem(id, title, url, icon, desc));
	}
	void Jsonize(JsonIO& io);
};

struct NavCategory : Moveable<NavCategory> {
	String              id;
	String              title;
	String              icon;
	String              description;
	Vector<NavSection>  sections;
	Vector<NavItem>     direct_items;
	bool                is_active;

	NavCategory();
	NavCategory(const String& id, const String& title, const String& icon = "", const String& desc = "");
	NavCategory(const NavCategory& o);
	NavCategory& operator=(const NavCategory& o);

	NavSection& AddSection(const String& title);
	NavItem&    AddItem(const NavItem& item);
	NavItem&    AddItem(const String& id, const String& title, const String& url, const String& icon = "", const String& desc = "");

	void Jsonize(JsonIO& io);
};

class NavigationModel : Moveable<NavigationModel> {
public:
	NavigationModel();
	NavigationModel(const NavigationModel& o);
	NavigationModel& operator=(const NavigationModel& o);

	NavCategory& AddCategory(const String& id, const String& title, const String& icon = "", const String& desc = "");
	NavCategory* FindCategory(const String& id);
	const NavCategory* FindCategory(const String& id) const;

	void SetActive(const String& category_id, const String& item_id = "");
	String GetActiveCategoryId() const { return active_category; }
	String GetActiveItemId() const { return active_item; }

	const Vector<NavCategory>& GetCategories() const { return categories; }
	int GetCount() const { return categories.GetCount(); }
	const NavCategory& operator[](int i) const { return categories[i]; }

	String ToJSON() const;
	bool FromJSON(const String& json);
	void Jsonize(JsonIO& io);

private:
	Vector<NavCategory> categories;
	String              active_category;
	String              active_item;
};

struct MegaMenuRenderer {
	static String Render(const NavigationModel& nav, const String& brand_title = "WebFramework", const String& brand_url = "/", const String& brand_icon = "vivid", const String& extra_actions = "");
	static String GetStyles();
	static String GetClientScript();
};

#endif
