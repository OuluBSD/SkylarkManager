#include "WebFramework.h"

NAMESPACE_UPP

static String EscapeNavHtml(const String& text)
{
	String out;
	for(int i = 0; i < text.GetCount(); i++) {
		byte c = (byte)text[i];
		if(c < 0x20 || c == 0x7f) {
			out << "&#xfffd;";
			continue;
		}
		switch(text[i]) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(text[i]); break;
		}
	}
	return out;
}

static String SafeNavIcon(const String& icon, const String& fallback)
{
	if(icon.IsEmpty())
		return fallback;
	for(int i = 0; i < icon.GetCount(); i++) {
		char c = icon[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		     (c >= '0' && c <= '9') || c == '-'))
			return fallback;
	}
	return icon;
}

static String EncodeNavParts(const String& value, char separator)
{
	String out;
	Vector<String> parts = Split(value, separator);
	for(int i = 0; i < parts.GetCount(); i++) {
		if(i) out.Cat(separator);
		out << UrlEncode(parts[i]);
	}
	return out;
}

static bool SafeNavUrlChars(const String& url)
{
	for(int i = 0; i < url.GetCount(); i++) {
		byte c = (byte)url[i];
		if(c <= 0x20 || c == 0x7f || c == '\\' || c == '"' || c == '\'' || c == '<' || c == '>')
			return false;
	}
	return true;
}

static String SafeNavUrl(const String& url)
{
	if(!SafeNavUrlChars(url))
		return String();
	if(url == "#")
		return url;
	if(url.StartsWith("https://")) {
		String authority = url.Mid(8);
		int end = authority.GetCount();
		for(int i = 0; i < authority.GetCount(); i++)
			if(authority[i] == '/' || authority[i] == '?' || authority[i] == '#') {
				end = i;
				break;
			}
		String host = authority.Left(end);
		return host.IsEmpty() || host.Find('@') >= 0 ? String() : url;
	}
	if(url.StartsWith("//"))
		return String();
	if(url.StartsWith("#"))
		return "#" + EncodeNavParts(url.Mid(1), '/');
	if(!url.StartsWith("/"))
		return String();

	int frag_pos = url.Find('#');
	String fragment = frag_pos >= 0 ? url.Mid(frag_pos + 1) : String();
	String before_fragment = frag_pos >= 0 ? url.Left(frag_pos) : url;
	int query_pos = before_fragment.Find('?');
	String path = query_pos >= 0 ? before_fragment.Left(query_pos) : before_fragment;
	String query = query_pos >= 0 ? before_fragment.Mid(query_pos + 1) : String();
	String out = "/" + EncodeNavParts(path.Mid(1), '/');
	if(query_pos >= 0) {
		out << "?";
		Vector<String> args = Split(query, '&');
		for(int i = 0; i < args.GetCount(); i++) {
			if(i) out << "&";
			int eq = args[i].Find('=');
			out << UrlEncode(eq >= 0 ? args[i].Left(eq) : args[i]);
			if(eq >= 0) out << "=" << UrlEncode(args[i].Mid(eq + 1));
		}
	}
	if(frag_pos >= 0)
		out << "#" << EncodeNavParts(fragment, '/');
	return out;
}

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
	return
		"  .wf-nav-bar { display: flex; align-items: center; justify-content: space-between; height: 60px; padding: 0 1.5rem; background: var(--wf-header-bg, rgba(24, 32, 48, 0.88)); backdrop-filter: blur(14px); -webkit-backdrop-filter: blur(14px); border-bottom: 1px solid var(--wf-header-border, rgba(0, 161, 197, 0.25)); position: sticky; top: 0; z-index: 1000; box-shadow: 0 4px 20px rgba(0,0,0,0.18); }\n"
		"  .wf-brand { display: flex; align-items: center; gap: 10px; font-weight: 700; font-size: 1.15rem; color: var(--wf-header-fg, #d3dae3); text-decoration: none; transition: opacity 0.15s; }\n"
		"  .wf-brand:hover { opacity: 0.9; color: var(--wf-accent-focus, #00a1c5); }\n"
		"  .wf-brand-icon { display: flex; align-items: center; color: var(--wf-accent-focus, #00a1c5); }\n"
		"  .wf-nav-categories { display: flex; align-items: center; gap: 4px; height: 100%; }\n"
		"  .wf-cat-tab { display: inline-flex; align-items: center; gap: 6px; padding: 6px 14px; border-radius: 6px; font-size: 0.88rem; font-weight: 600; color: var(--wf-header-fg-muted, #8b919d); text-decoration: none; cursor: pointer; transition: all 0.15s; position: relative; }\n"
		"  .wf-cat-tab:hover, .wf-cat-tab.active, .wf-cat-tab.menu-open { color: var(--wf-header-fg, #ffffff); background: var(--wf-surface-hover, rgba(0, 161, 197, 0.12)); }\n"
		"  .wf-cat-tab.active { border-bottom: 2px solid var(--wf-accent-focus, #00a1c5); border-radius: 6px 6px 0 0; }\n"
		"  .wf-cat-arrow { transition: transform 0.2s ease; }\n"
		"  .wf-cat-tab.menu-open .wf-cat-arrow { transform: rotate(180deg); }\n"
		"  .wf-nav-actions { display: flex; align-items: center; gap: 8px; }\n"
		"  .wf-btn-icon { background: transparent; border: 1px solid var(--wf-header-border, rgba(0, 161, 197, 0.25)); color: var(--wf-header-fg, #d3dae3); border-radius: 6px; padding: 6px 10px; cursor: pointer; display: inline-flex; align-items: center; gap: 6px; font-size: 0.8rem; font-weight: 500; transition: all 0.15s; }\n"
		"  .wf-btn-icon:hover { background: var(--wf-surface-hover, rgba(0, 161, 197, 0.15)); border-color: var(--wf-accent-focus, #00a1c5); color: var(--wf-accent-focus, #00a1c5); }\n"
		"  .wf-btn-icon.active { background: var(--wf-accent-focus, #00a1c5); border-color: var(--wf-accent-focus, #00a1c5); color: #ffffff; }\n"
		"\n"
		"  /* Mega Dropdown Panel */\n"
		"  .wf-mega-dropdown { position: absolute; top: 60px; left: 0; width: 100%; background: var(--wf-header-bg, rgba(24, 32, 48, 0.95)); backdrop-filter: blur(20px); -webkit-backdrop-filter: blur(20px); border-bottom: 2px solid var(--wf-accent-focus, #00a1c5); box-shadow: 0 20px 40px rgba(0,0,0,0.35); display: none; opacity: 0; transform: translateY(-8px); transition: opacity 0.2s ease, transform 0.2s ease; z-index: 999; padding: 2rem 2.5rem; }\n"
		"  .wf-mega-dropdown.open { display: block; opacity: 1; transform: translateY(0); }\n"
		"  .wf-mega-grid { display: grid; grid-template-columns: 280px 1fr; gap: 2.5rem; max-width: 1300px; margin: 0 auto; }\n"
		"  .wf-mega-sidebar { border-right: 1px solid var(--wf-header-border, rgba(0, 161, 197, 0.25)); padding-right: 2rem; display: flex; flex-direction: column; gap: 12px; }\n"
		"  .wf-mega-cat-pill { display: flex; align-items: center; gap: 10px; padding: 10px 14px; border-radius: 8px; color: var(--wf-header-fg-muted, #8b919d); text-decoration: none; cursor: pointer; transition: all 0.15s; font-weight: 600; font-size: 0.92rem; }\n"
		"  .wf-mega-cat-pill:hover, .wf-mega-cat-pill.active { background: var(--wf-surface-hover, rgba(0, 161, 197, 0.15)); color: var(--wf-accent-focus, #00a1c5); }\n"
		"  .wf-mega-cat-pill.active { border-left: 3px solid var(--wf-accent-focus, #00a1c5); }\n"
		"  .wf-mega-content { display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr)); gap: 1.5rem 2rem; }\n"
		"  .wf-mega-section-title { font-size: 0.76rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.08em; color: var(--wf-accent-focus, #00a1c5); margin-bottom: 0.85rem; border-bottom: 1px solid var(--wf-header-border, rgba(0, 161, 197, 0.25)); padding-bottom: 4px; }\n"
		"  .wf-mega-item-list { display: flex; flex-direction: column; gap: 6px; }\n"
		"  .wf-mega-item { display: flex; align-items: flex-start; gap: 10px; padding: 8px 10px; border-radius: 6px; text-decoration: none; color: var(--wf-header-fg, #d3dae3); transition: all 0.15s; }\n"
		"  .wf-mega-item:hover, .wf-mega-item.active { background: var(--wf-surface-hover, rgba(0, 161, 197, 0.12)); color: var(--wf-accent-focus, #00a1c5); }\n"
		"  .wf-mega-item-icon { color: var(--wf-accent-focus, #00a1c5); margin-top: 2px; flex-shrink: 0; }\n"
		"  .wf-mega-item-body { display: flex; flex-direction: column; gap: 2px; flex: 1; }\n"
		"  .wf-mega-item-title { font-weight: 600; font-size: 0.88rem; display: flex; align-items: center; justify-content: space-between; }\n"
		"  .wf-mega-item-desc { font-size: 0.75rem; color: var(--wf-header-fg-muted, #8b919d); line-height: 1.35; }\n"
		"  .wf-badge { font-size: 0.68rem; font-weight: 700; padding: 1px 6px; border-radius: 10px; text-transform: uppercase; }\n"
		"  .wf-badge-cyan { background: rgba(0, 161, 197, 0.2); color: #00acd3; border: 1px solid rgba(0, 161, 197, 0.4); }\n"
		"  .wf-badge-emerald { background: rgba(0, 170, 127, 0.2); color: #00aa7f; border: 1px solid rgba(0, 170, 127, 0.4); }\n"
		"  .wf-badge-crimson { background: rgba(200, 55, 113, 0.2); color: #c83771; border: 1px solid rgba(200, 55, 113, 0.4); }\n"
		"  .wf-badge-purple { background: rgba(109, 85, 170, 0.2); color: #a78bfa; border: 1px solid rgba(109, 85, 170, 0.4); }\n"
		"\n"
		"  /* Mobile Drawer */\n"
		"  .wf-mobile-toggle { display: none; background: transparent; border: none; color: var(--wf-header-fg, #d3dae3); cursor: pointer; }\n"
		"  @media (max-width: 900px) {\n"
		"    .wf-nav-categories { display: none; }\n"
		"    .wf-mobile-toggle { display: block; }\n"
		"  }\n";
}

String MegaMenuRenderer::GetClientScript()
{
	return
		"var MegaMenu = (function() {\n"
		"  let activeCat = '';\n"
		"  let closeTimer = null;\n"
		"  let dropdownEl = null;\n"
		"  function init() {\n"
		"    dropdownEl = document.getElementById('wf-mega-dropdown');\n"
		"    if (!dropdownEl) return;\n"
		"    document.querySelectorAll('.wf-cat-tab').forEach(function(tab) {\n"
		"      tab.addEventListener('mouseenter', function() {\n"
		"        clearTimeout(closeTimer);\n"
		"        let catId = tab.getAttribute('data-cat-id');\n"
		"        openCategory(catId);\n"
		"      });\n"
		"    });\n"
		"    let sidebar = document.querySelector('.wf-mega-sidebar');\n"
		"    if (sidebar) sidebar.addEventListener('mouseover', function(e) {\n"
		"      let pill = e.target.closest('.wf-mega-cat-pill');\n"
		"      if (pill && !pill.contains(e.relatedTarget)) openCategory(pill.getAttribute('data-cat-id'));\n"
		"    });\n"
		"    document.addEventListener('click', function(e) {\n"
		"      let mobile = e.target.closest('.wf-mobile-toggle');\n"
		"      if (mobile) { e.preventDefault(); openCategory(mobile.getAttribute('data-cat-id')); }\n"
		"    });\n"
		"    let navBar = document.querySelector('.wf-nav-bar');\n"
		"    if (navBar) {\n"
		"      navBar.addEventListener('mouseleave', scheduleClose);\n"
		"    }\n"
		"    dropdownEl.addEventListener('mouseenter', function() { clearTimeout(closeTimer); });\n"
		"    dropdownEl.addEventListener('mouseleave', scheduleClose);\n"
		"  }\n"
		"  function openCategory(catId) {\n"
		"    if (!dropdownEl) return;\n"
		"    activeCat = catId;\n"
		"    document.querySelectorAll('.wf-cat-tab').forEach(t => {\n"
		"      let isMatch = t.getAttribute('data-cat-id') === catId;\n"
		"      t.classList.toggle('menu-open', isMatch);\n"
		"    });\n"
		"    document.querySelectorAll('.wf-mega-cat-pill').forEach(p => {\n"
		"      p.classList.toggle('active', p.getAttribute('data-cat-id') === catId);\n"
		"    });\n"
		"    document.querySelectorAll('.wf-mega-cat-content').forEach(c => {\n"
		"      c.style.display = (c.getAttribute('data-cat-id') === catId) ? 'grid' : 'none';\n"
		"    });\n"
		"    dropdownEl.classList.add('open');\n"
		"    if (window.WebAudio) WebAudio.play('click');\n"
		"  }\n"
		"  function scheduleClose() {\n"
		"    closeTimer = setTimeout(closeMenu, 180);\n"
		"  }\n"
		"  function closeMenu() {\n"
		"    if (!dropdownEl) return;\n"
		"    dropdownEl.classList.remove('open');\n"
		"    document.querySelectorAll('.wf-cat-tab').forEach(t => t.classList.remove('menu-open'));\n"
		"  }\n"
		"  return {\n"
		"    init: init,\n"
		"    openCategory: openCategory,\n"
		"    closeMenu: closeMenu\n"
		"  };\n"
		"})();\n"
		"document.addEventListener('DOMContentLoaded', MegaMenu.init);\n";
}

String MegaMenuRenderer::Render(const NavigationModel& nav, const String& brand_title, const String& brand_url, const String& brand_icon, const String& extra_actions)
{
	String h;

	// Nav bar header
	String safe_brand_url = SafeNavUrl(brand_url);
	if(safe_brand_url.IsEmpty()) safe_brand_url = "/";
	String safe_brand_icon = SafeNavIcon(brand_icon, "vivid");
	h << "<header class=\"wf-nav-bar\" role=\"navigation\">\n"
	  << "  <a href=\"" << EscapeNavHtml(safe_brand_url) << "\" class=\"wf-brand\">\n"
	  << "    <span class=\"wf-brand-icon\">" << VividIcon::Get(safe_brand_icon, 22) << "</span>\n"
	  << "    <span>" << EscapeNavHtml(brand_title) << "</span>\n"
	  << "  </a>\n";

	// Center category tabs
	h << "  <nav class=\"wf-nav-categories\" aria-label=\"Main Navigation\">\n";
	for(int i = 0; i < nav.GetCount(); i++) {
		const NavCategory& cat = nav[i];
		bool is_act = (cat.id == nav.GetActiveCategoryId());
		h << "    <a class=\"wf-cat-tab " << (is_act ? "active" : "") << "\" data-cat-id=\"" << EscapeNavHtml(cat.id) << "\" href=\"#\">\n"
		  << "      <span>" << EscapeNavHtml(cat.title) << "</span>\n"
		  << "      <span class=\"wf-cat-arrow\">" << VividIcon::Get("chevron-down", 12) << "</span>\n"
		  << "    </a>\n";
	}
	h << "  </nav>\n";

	// Right utility actions
	h << "  <div class=\"wf-nav-actions\">\n"
	  << "    <button id=\"wf-btn-theme\" class=\"wf-btn-icon\" onclick=\"WebThemeManager.toggleTheme()\" title=\"Toggle Vivid Dark / Light Theme\">\n"
	  << "      " << VividIcon::Get("palette", 16) << " <span class=\"wf-btn-label\">Theme</span>\n"
	  << "    </button>\n"
	  << "    <button id=\"wf-btn-density\" class=\"wf-btn-icon\" onclick=\"WebDensityManager.cycleDensity()\" title=\"Cycle Layout Density (Dense/Compact/Comfortable)\">\n"
	  << "      " << VividIcon::Get("sliders", 16) << " <span id=\"wf-density-label\">Density</span>\n"
	  << "    </button>\n"
	  << "    <button id=\"wf-btn-sound\" class=\"wf-btn-icon\" onclick=\"WebAudio.toggle()\" title=\"Toggle Procedural UI Sound Cues\">\n"
	  << "      " << VividIcon::Get("volume", 16) << "\n"
	  << "    </button>\n";
	if(!extra_actions.IsEmpty()) {
		// Explicit trusted-HTML extension slot; callers must not pass user content.
		h << "    " << extra_actions << "\n";
	}
	h << "    <button class=\"wf-mobile-toggle wf-btn-icon\" aria-label=\"Menu\" data-cat-id=\"" << EscapeNavHtml(nav.GetCount() > 0 ? nav[0].id : String()) << "\">\n"
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
		h << "        <div class=\"wf-mega-cat-pill " << (is_act ? "active" : "") << "\" data-cat-id=\"" << EscapeNavHtml(cat.id) << "\">\n"
		  << "          " << VividIcon::Get(SafeNavIcon(cat.icon, "folder"), 16) << "\n"
		  << "          <span>" << EscapeNavHtml(cat.title) << "</span>\n"
		  << "        </div>\n";
	}
	h << "      </div>\n";

	// Right content areas: one for each category
	h << "      <div class=\"wf-mega-panes\">\n";
	for(int i = 0; i < nav.GetCount(); i++) {
		const NavCategory& cat = nav[i];
		bool is_act = (i == 0); // show first by default
		h << "        <div class=\"wf-mega-cat-content\" data-cat-id=\"" << EscapeNavHtml(cat.id) << "\" style=\"display:" << (is_act ? "grid" : "none") << ";\">\n";

		// Direct items if any
		if(!cat.direct_items.IsEmpty()) {
			h << "          <div>\n"
			  << "            <div class=\"wf-mega-section-title\">Main</div>\n"
			  << "            <div class=\"wf-mega-item-list\">\n";
			for(int it = 0; it < cat.direct_items.GetCount(); it++) {
				const NavItem& item = cat.direct_items[it];
				String safe_url = SafeNavUrl(item.url);
				String tag = safe_url.IsEmpty() ? "div" : "a";
				if(tag == "a")
					h << "              <a href=\"" << EscapeNavHtml(safe_url) << "\" class=\"wf-mega-item " << (item.is_active ? "active" : "") << "\">\n";
				else
					h << "              <div class=\"wf-mega-item " << (item.is_active ? "active" : "") << "\">\n";
				h << "                <div class=\"wf-mega-item-icon\">" << VividIcon::Get(SafeNavIcon(item.icon, "file"), 16) << "</div>\n"
				  << "                <div class=\"wf-mega-item-body\">\n"
				  << "                  <div class=\"wf-mega-item-title\">" << EscapeNavHtml(item.title);
				if(!item.badge.text.IsEmpty()) {
					String badge_style = item.badge.style;
					if(badge_style != "cyan" && badge_style != "emerald" && badge_style != "crimson" && badge_style != "purple" && badge_style != "neutral")
						badge_style = "neutral";
					h << " <span class=\"wf-badge wf-badge-" << badge_style << "\">" << EscapeNavHtml(item.badge.text) << "</span>";
				}
				h << "                  </div>\n";
				if(!item.description.IsEmpty())
					h << "                  <div class=\"wf-mega-item-desc\">" << EscapeNavHtml(item.description) << "</div>\n";
				h << "                </div>\n"
				  << "              </" << tag << ">\n";
			}
			h << "            </div>\n"
			  << "          </div>\n";
		}

		// Sections
		for(int s = 0; s < cat.sections.GetCount(); s++) {
			const NavSection& sec = cat.sections[s];
			h << "          <div>\n"
			  << "            <div class=\"wf-mega-section-title\">" << EscapeNavHtml(sec.title) << "</div>\n"
			  << "            <div class=\"wf-mega-item-list\">\n";
			for(int it = 0; it < sec.items.GetCount(); it++) {
				const NavItem& item = sec.items[it];
				String safe_url = SafeNavUrl(item.url);
				String tag = safe_url.IsEmpty() ? "div" : "a";
				if(tag == "a")
					h << "              <a href=\"" << EscapeNavHtml(safe_url) << "\" class=\"wf-mega-item " << (item.is_active ? "active" : "") << "\">\n";
				else
					h << "              <div class=\"wf-mega-item " << (item.is_active ? "active" : "") << "\">\n";
				h << "                <div class=\"wf-mega-item-icon\">" << VividIcon::Get(SafeNavIcon(item.icon, "file"), 16) << "</div>\n"
				  << "                <div class=\"wf-mega-item-body\">\n"
				  << "                  <div class=\"wf-mega-item-title\">" << EscapeNavHtml(item.title);
				if(!item.badge.text.IsEmpty()) {
					String badge_style = item.badge.style;
					if(badge_style != "cyan" && badge_style != "emerald" && badge_style != "crimson" && badge_style != "purple" && badge_style != "neutral")
						badge_style = "neutral";
					h << " <span class=\"wf-badge wf-badge-" << badge_style << "\">" << EscapeNavHtml(item.badge.text) << "</span>";
				}
				h << "                  </div>\n";
				if(!item.description.IsEmpty())
					h << "                  <div class=\"wf-mega-item-desc\">" << EscapeNavHtml(item.description) << "</div>\n";
				h << "                </div>\n"
				  << "              </" << tag << ">\n";
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
