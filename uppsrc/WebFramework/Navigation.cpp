#include "WebFramework.h"

NAMESPACE_UPP

NavItem::NavItem()
	: is_active(false)
{
}

NavItem::NavItem(const String& id, const String& title, const String& url, const String& icon, const String& desc)
	: id(id), title(title), url(url), icon(icon), description(desc), is_active(false)
{
}

void NavItem::Jsonize(JsonIO& io)
{
	io("id", id)("title", title)("url", url)("icon", icon)
	  ("description", description)("shortcut", shortcut)("badge", badge)("is_active", is_active);
}

NavSection::NavSection(const NavSection& o)
	: title(o.title)
	, items(clone(o.items))
{
}

NavSection& NavSection::operator=(const NavSection& o)
{
	if(this != &o) {
		title = o.title;
		items = clone(o.items);
	}
	return *this;
}

void NavSection::Jsonize(JsonIO& io)
{
	io("title", title)("items", items);
}

NavCategory::NavCategory()
	: is_active(false)
{
}

NavCategory::NavCategory(const String& id, const String& title, const String& icon, const String& desc)
	: id(id), title(title), icon(icon), description(desc), is_active(false)
{
}

NavCategory::NavCategory(const NavCategory& o)
	: id(o.id)
	, title(o.title)
	, icon(o.icon)
	, description(o.description)
	, sections(clone(o.sections))
	, direct_items(clone(o.direct_items))
	, is_active(o.is_active)
{
}

NavCategory& NavCategory::operator=(const NavCategory& o)
{
	if(this != &o) {
		id = o.id;
		title = o.title;
		icon = o.icon;
		description = o.description;
		sections = clone(o.sections);
		direct_items = clone(o.direct_items);
		is_active = o.is_active;
	}
	return *this;
}

NavSection& NavCategory::AddSection(const String& title)
{
	sections.Add(NavSection(title));
	return sections.Top();
}

NavItem& NavCategory::AddItem(const NavItem& item)
{
	direct_items.Add(item);
	return direct_items.Top();
}

NavItem& NavCategory::AddItem(const String& id, const String& title, const String& url, const String& icon, const String& desc)
{
	return AddItem(NavItem(id, title, url, icon, desc));
}

void NavCategory::Jsonize(JsonIO& io)
{
	io("id", id)("title", title)("icon", icon)("description", description)
	  ("sections", sections)("direct_items", direct_items)("is_active", is_active);
}

NavigationModel::NavigationModel()
{
}

NavigationModel::NavigationModel(const NavigationModel& o)
	: categories(clone(o.categories))
	, active_category(o.active_category)
	, active_item(o.active_item)
{
}

NavigationModel& NavigationModel::operator=(const NavigationModel& o)
{
	if(this != &o) {
		categories = clone(o.categories);
		active_category = o.active_category;
		active_item = o.active_item;
	}
	return *this;
}

NavCategory& NavigationModel::AddCategory(const String& id, const String& title, const String& icon, const String& desc)
{
	categories.Add(NavCategory(id, title, icon, desc));
	return categories.Top();
}

NavCategory* NavigationModel::FindCategory(const String& id)
{
	for(int i = 0; i < categories.GetCount(); i++)
		if(categories[i].id == id)
			return &categories[i];
	return NULL;
}

const NavCategory* NavigationModel::FindCategory(const String& id) const
{
	for(int i = 0; i < categories.GetCount(); i++)
		if(categories[i].id == id)
			return &categories[i];
	return NULL;
}

void NavigationModel::SetActive(const String& category_id, const String& item_id)
{
	active_category = category_id;
	active_item = item_id;

	for(int i = 0; i < categories.GetCount(); i++) {
		NavCategory& c = categories[i];
		c.is_active = (c.id == category_id);
		for(int s = 0; s < c.sections.GetCount(); s++) {
			for(int it = 0; it < c.sections[s].items.GetCount(); it++) {
				NavItem& item = c.sections[s].items[it];
				item.is_active = (!item_id.IsEmpty() && item.id == item_id);
			}
		}
		for(int it = 0; it < c.direct_items.GetCount(); it++) {
			NavItem& item = c.direct_items[it];
			item.is_active = (!item_id.IsEmpty() && item.id == item_id);
		}
	}
}

void NavigationModel::Jsonize(JsonIO& io)
{
	io("categories", categories)("active_category", active_category)("active_item", active_item);
}

String NavigationModel::ToJSON() const
{
	NavigationModel copy = *this;
	return StoreAsJson(copy, true);
}

bool NavigationModel::FromJSON(const String& json)
{
	return LoadFromJson(*this, json);
}

// ---------------------------------------------------------------------------
// MegaMenuRenderer
// ---------------------------------------------------------------------------

String MegaMenuRenderer::GetStyles()
{
	return WebFrameworkAsset("megamenu.css");
}

String MegaMenuRenderer::GetClientScript()
{
	return WebFrameworkAsset("megamenu.js");
}

String MegaMenuRenderer::Render(const NavigationModel& nav, const String& brand_title, const String& brand_url, const String& brand_icon, const String& extra_actions)
{
	String h;

	// Nav bar header
	h << "<header class=\"wf-nav-bar\" role=\"navigation\">\n"
	  << "  <a href=\"" << brand_url << "\" class=\"wf-brand\">\n"
	  << "    <span class=\"wf-brand-icon\">" << VividIcon::Get(brand_icon, 22) << "</span>\n"
	  << "    <span>" << brand_title << "</span>\n"
	  << "  </a>\n";

	// Center category tabs
	h << "  <nav class=\"wf-nav-categories\" aria-label=\"Main Navigation\">\n";
	for(int i = 0; i < nav.GetCount(); i++) {
		const NavCategory& cat = nav[i];
		bool is_act = (cat.id == nav.GetActiveCategoryId());
		h << "    <a class=\"wf-cat-tab " << (is_act ? "active" : "") << "\" data-cat-id=\"" << cat.id << "\" href=\"#\">\n"
		  << "      <span>" << cat.title << "</span>\n"
		  << "      <span class=\"wf-cat-arrow\">" << VividIcon::Get("chevron-down", 12) << "</span>\n"
		  << "    </a>\n";
	}
	h << "  </nav>\n";

	// Right utility actions
	// Theme and density are host-wide controls, rendered by the portal header (RenderThemeDensityControls).
	h << "  <div class=\"wf-nav-actions\">\n"
	  << "    <button id=\"wf-btn-sound\" class=\"wf-btn-icon\" onclick=\"WebAudio.toggle()\" title=\"Toggle Procedural UI Sound Cues\">\n"
	  << "      " << VividIcon::Get("volume", 16) << "\n"
	  << "    </button>\n";
	if(!extra_actions.IsEmpty()) {
		h << "    " << extra_actions << "\n";
	}
	h << "    <button class=\"wf-mobile-toggle wf-btn-icon\" aria-label=\"Menu\" onclick=\"MegaMenu.openCategory('" << (nav.GetCount() > 0 ? nav[0].id : "") << "')\">\n"
	  << "      " << VividIcon::Get("menu", 18) << "\n"
	  << "    </button>\n"
	  << "  </div>\n";

	// Mega Dropdown Panel
	h << "  <div class=\"wf-mega-dropdown\" id=\"wf-mega-dropdown\">\n"
	  << "    <div class=\"wf-mega-grid\">\n";

	// Left sidebar inside mega-menu: Category list
	h << "      <div class=\"wf-mega-sidebar\">\n"
	  << "        <div style=\"font-size:0.75rem; font-weight:700; color:var(--wf-accent-focus,#00a1c5); text-transform:uppercase; letter-spacing:0.08em; margin-bottom:8px;\">Categories</div>\n";
	for(int i = 0; i < nav.GetCount(); i++) {
		const NavCategory& cat = nav[i];
		bool is_act = (cat.id == nav.GetActiveCategoryId());
		h << "        <div class=\"wf-mega-cat-pill " << (is_act ? "active" : "") << "\" data-cat-id=\"" << cat.id << "\" onmouseenter=\"MegaMenu.openCategory('" << cat.id << "')\">\n"
		  << "          " << VividIcon::Get(cat.icon.IsEmpty() ? "folder" : cat.icon, 16) << "\n"
		  << "          <span>" << cat.title << "</span>\n"
		  << "        </div>\n";
	}
	h << "      </div>\n";

	// Right content areas: one for each category
	h << "      <div class=\"wf-mega-panes\">\n";
	for(int i = 0; i < nav.GetCount(); i++) {
		const NavCategory& cat = nav[i];
		bool is_act = (i == 0); // show first by default
		h << "        <div class=\"wf-mega-cat-content\" data-cat-id=\"" << cat.id << "\" style=\"display:" << (is_act ? "grid" : "none") << ";\">\n";

		// Direct items if any
		if(!cat.direct_items.IsEmpty()) {
			h << "          <div>\n"
			  << "            <div class=\"wf-mega-section-title\">Main</div>\n"
			  << "            <div class=\"wf-mega-item-list\">\n";
			for(int it = 0; it < cat.direct_items.GetCount(); it++) {
				const NavItem& item = cat.direct_items[it];
				h << "              <a href=\"" << item.url << "\" class=\"wf-mega-item " << (item.is_active ? "active" : "") << "\">\n"
				  << "                <div class=\"wf-mega-item-icon\">" << VividIcon::Get(item.icon.IsEmpty() ? "file" : item.icon, 16) << "</div>\n"
				  << "                <div class=\"wf-mega-item-body\">\n"
				  << "                  <div class=\"wf-mega-item-title\">" << item.title;
				if(!item.badge.text.IsEmpty()) {
					h << " <span class=\"wf-badge wf-badge-" << item.badge.style << "\">" << item.badge.text << "</span>";
				}
				h << "                  </div>\n";
				if(!item.description.IsEmpty())
					h << "                  <div class=\"wf-mega-item-desc\">" << item.description << "</div>\n";
				h << "                </div>\n"
				  << "              </a>\n";
			}
			h << "            </div>\n"
			  << "          </div>\n";
		}

		// Sections
		for(int s = 0; s < cat.sections.GetCount(); s++) {
			const NavSection& sec = cat.sections[s];
			h << "          <div>\n"
			  << "            <div class=\"wf-mega-section-title\">" << sec.title << "</div>\n"
			  << "            <div class=\"wf-mega-item-list\">\n";
			for(int it = 0; it < sec.items.GetCount(); it++) {
				const NavItem& item = sec.items[it];
				h << "              <a href=\"" << item.url << "\" class=\"wf-mega-item " << (item.is_active ? "active" : "") << "\">\n"
				  << "                <div class=\"wf-mega-item-icon\">" << VividIcon::Get(item.icon.IsEmpty() ? "file" : item.icon, 16) << "</div>\n"
				  << "                <div class=\"wf-mega-item-body\">\n"
				  << "                  <div class=\"wf-mega-item-title\">" << item.title;
				if(!item.badge.text.IsEmpty()) {
					h << " <span class=\"wf-badge wf-badge-" << item.badge.style << "\">" << item.badge.text << "</span>";
				}
				h << "                  </div>\n";
				if(!item.description.IsEmpty())
					h << "                  <div class=\"wf-mega-item-desc\">" << item.description << "</div>\n";
				h << "                </div>\n"
				  << "              </a>\n";
			}
			h << "            </div>\n"
			  << "          </div>\n";
		}

		h << "        </div>\n";
	}
	h << "      </div>\n"; // .wf-mega-panes
	h << "    </div>\n";   // .wf-mega-grid
	h << "  </div>\n";     // .wf-mega-dropdown
	h << "</header>\n";

	return h;
}

END_UPP_NAMESPACE
