#include "WebFramework.h"

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// TreeNode Implementation
// ---------------------------------------------------------------------------

TreeNode::TreeNode()
	: is_expanded(false)
	, is_selected(false)
{
}

TreeNode::TreeNode(const String& id, const String& label, const String& url, const String& icon)
	: id(id)
	, label(label)
	, url(url)
	, icon(icon)
	, is_expanded(false)
	, is_selected(false)
{
}

TreeNode::TreeNode(const TreeNode& o)
	: id(o.id)
	, label(o.label)
	, url(o.url)
	, icon(o.icon)
	, icon_open(o.icon_open)
	, badge(o.badge)
	, is_expanded(o.is_expanded)
	, is_selected(o.is_selected)
	, tooltip(o.tooltip)
{
	children = clone(o.children);
}

TreeNode& TreeNode::operator=(const TreeNode& o)
{
	if(this != &o) {
		id = o.id;
		label = o.label;
		url = o.url;
		icon = o.icon;
		icon_open = o.icon_open;
		badge = o.badge;
		is_expanded = o.is_expanded;
		is_selected = o.is_selected;
		tooltip = o.tooltip;
		children = clone(o.children);
	}
	return *this;
}

TreeNode& TreeNode::AddChild(const TreeNode& child)
{
	children.Add(child);
	return children.Top();
}

TreeNode& TreeNode::AddChild(const String& id, const String& label, const String& url, const String& icon)
{
	return AddChild(TreeNode(id, label, url, icon));
}

void TreeNode::Jsonize(JsonIO& io)
{
	io("id", id)("label", label)("url", url)("icon", icon)("icon_open", icon_open)
	  ("badge", badge)("is_expanded", is_expanded)("is_selected", is_selected)
	  ("tooltip", tooltip)("children", children);
}

Value TreeNode::ToValue() const
{
	ValueMap m;
	m.Add("id", id);
	m.Add("label", label);
	m.Add("url", url);
	m.Add("icon", icon);
	if(!badge.text.IsEmpty()) {
		ValueMap b;
		b.Add("text", badge.text);
		b.Add("style", badge.style);
		m.Add("badge", b);
	}
	m.Add("is_expanded", is_expanded);
	m.Add("is_selected", is_selected);
	if(!tooltip.IsEmpty()) m.Add("tooltip", tooltip);

	if(!children.IsEmpty()) {
		ValueArray arr;
		for(int i = 0; i < children.GetCount(); i++)
			arr.Add(children[i].ToValue());
		m.Add("children", arr);
	}
	return m;
}

// ---------------------------------------------------------------------------
// TreeModel Implementation
// ---------------------------------------------------------------------------

TreeModel::TreeModel()
{
}

TreeModel::TreeModel(const TreeModel& o)
{
	roots = clone(o.roots);
}

TreeModel& TreeModel::operator=(const TreeModel& o)
{
	if(this != &o) {
		roots = clone(o.roots);
	}
	return *this;
}

TreeNode& TreeModel::Add(const TreeNode& node)
{
	roots.Add(node);
	return roots.Top();
}

TreeNode& TreeModel::Add(const String& id, const String& label, const String& url, const String& icon)
{
	return Add(TreeNode(id, label, url, icon));
}

TreeNode* TreeModel::Find(const String& id)
{
	return FindRecursive(roots, id);
}

const TreeNode* TreeModel::Find(const String& id) const
{
	return FindRecursive(roots, id);
}

TreeNode* TreeModel::FindRecursive(Vector<TreeNode>& list, const String& id)
{
	for(int i = 0; i < list.GetCount(); i++) {
		if(list[i].id == id)
			return &list[i];
		TreeNode* sub = FindRecursive(list[i].children, id);
		if(sub) return sub;
	}
	return NULL;
}

const TreeNode* TreeModel::FindRecursive(const Vector<TreeNode>& list, const String& id) const
{
	for(int i = 0; i < list.GetCount(); i++) {
		if(list[i].id == id)
			return &list[i];
		const TreeNode* sub = FindRecursive(list[i].children, id);
		if(sub) return sub;
	}
	return NULL;
}

void TreeModel::SetSelected(const String& id)
{
	SetSelectedRecursive(roots, id);
}

void TreeModel::SetSelectedRecursive(Vector<TreeNode>& list, const String& id)
{
	for(int i = 0; i < list.GetCount(); i++) {
		list[i].is_selected = (list[i].id == id);
		SetSelectedRecursive(list[i].children, id);
	}
}

void TreeModel::ExpandAll(bool b)
{
	ExpandRecursive(roots, b);
}

void TreeModel::ExpandRecursive(Vector<TreeNode>& list, bool b)
{
	for(int i = 0; i < list.GetCount(); i++) {
		list[i].is_expanded = b;
		ExpandRecursive(list[i].children, b);
	}
}

void TreeModel::Jsonize(JsonIO& io)
{
	io("roots", roots);
}

String TreeModel::ToJSON() const
{
	TreeModel copy = *this;
	return StoreAsJson(copy, true);
}

bool TreeModel::FromJSON(const String& json)
{
	return LoadFromJson(*this, json);
}

Value TreeModel::ToValue() const
{
	ValueArray arr;
	for(int i = 0; i < roots.GetCount(); i++)
		arr.Add(roots[i].ToValue());
	return arr;
}

// ---------------------------------------------------------------------------
// TreeGenerators: Automatic Tree Content Producers
// ---------------------------------------------------------------------------

TreeModel TreeGenerators::FromNavigation(const NavigationModel& nav)
{
	TreeModel tree;

	for(int c = 0; c < nav.GetCount(); c++) {
		const NavCategory& cat = nav[c];
		bool cat_selected = (cat.id == nav.GetActiveCategoryId());
		TreeNode& cat_node = tree.Add(cat.id, cat.title, "#", cat.icon.IsEmpty() ? "folder" : cat.icon)
		                         .Expanded(true)
		                         .Tooltip(cat.description);

		// Direct items
		for(int it = 0; it < cat.direct_items.GetCount(); it++) {
			const NavItem& item = cat.direct_items[it];
			TreeNode& leaf = cat_node.AddChild(item.id, item.title, item.url, item.icon.IsEmpty() ? "file" : item.icon)
			                         .Selected(item.is_active)
			                         .Tooltip(item.description);
			if(!item.badge.text.IsEmpty())
				leaf.Badge(item.badge.text, item.badge.style);
		}

		// Sections
		for(int s = 0; s < cat.sections.GetCount(); s++) {
			const NavSection& sec = cat.sections[s];
			TreeNode& sec_node = cat_node.AddChild(cat.id + "-" + AsString(s), sec.title, "#", "folder")
			                             .Expanded(true);

			for(int it = 0; it < sec.items.GetCount(); it++) {
				const NavItem& item = sec.items[it];
				TreeNode& leaf = sec_node.AddChild(item.id, item.title, item.url, item.icon.IsEmpty() ? "file" : item.icon)
				                         .Selected(item.is_active)
				                         .Tooltip(item.description);
				if(!item.badge.text.IsEmpty())
					leaf.Badge(item.badge.text, item.badge.style);
			}
		}
	}

	return tree;
}

static void WalkDirectory(const String& current_dir, const String& base_dir,
                          TreeNode& parent_node, int current_depth, int max_depth,
                          const Vector<String>& exts)
{
	if(current_depth > max_depth) return;

	Vector<String> subdirs;
	Vector<String> files;

	FindFile ff(AppendFileName(current_dir, "*"));
	while(ff) {
		String name = ff.GetName();
		if(!name.StartsWith(".") && name != ".git" && name != ".cache" && name != "build") {
			if(ff.IsFolder()) {
				subdirs.Add(name);
			} else if(ff.IsFile()) {
				bool match = exts.IsEmpty();
				if(!match) {
					for(int e = 0; e < exts.GetCount(); e++) {
						if(name.EndsWith(exts[e])) {
							match = true;
							break;
						}
					}
				}
				if(match) files.Add(name);
			}
		}
		ff.Next();
	}

	Sort(subdirs);
	Sort(files);

	for(int i = 0; i < subdirs.GetCount(); i++) {
		String sname = subdirs[i];
		String spath = AppendFileName(current_dir, sname);
		String rel_path = spath;
		if(rel_path.StartsWith(base_dir))
			rel_path = rel_path.Mid(base_dir.GetLength());
		if(rel_path.StartsWith("/") || rel_path.StartsWith("\\"))
			rel_path = rel_path.Mid(1);

		TreeNode& dnode = parent_node.AddChild(rel_path, sname, "#", "folder");
		dnode.Icon("folder", "folder-open");
		dnode.Expanded(current_depth < 2); // expand top 2 levels by default

		WalkDirectory(spath, base_dir, dnode, current_depth + 1, max_depth, exts);
	}

	for(int i = 0; i < files.GetCount(); i++) {
		String fname = files[i];
		String fpath = AppendFileName(current_dir, fname);
		String rel_path = fpath;
		if(rel_path.StartsWith(base_dir))
			rel_path = rel_path.Mid(base_dir.GetLength());
		if(rel_path.StartsWith("/") || rel_path.StartsWith("\\"))
			rel_path = rel_path.Mid(1);

		String icon = "file";
		if(fname.EndsWith(".cpp") || fname.EndsWith(".h") || fname.EndsWith(".c") || fname.EndsWith(".py"))
			icon = "code";
		else if(fname.EndsWith(".md") || fname.EndsWith(".txt"))
			icon = "file-text";
		else if(fname.EndsWith(".json") || fname.EndsWith(".yaml") || fname.EndsWith(".upp"))
			icon = "settings";

		parent_node.AddChild(rel_path, fname, "#file/" + rel_path, icon);
	}
}

TreeModel TreeGenerators::FromDirectory(const String& dir_path, int max_depth, const Vector<String>& extensions)
{
	TreeModel tree;
	if(!DirectoryExists(dir_path)) return tree;

	String base_name = GetFileName(dir_path);
	if(base_name.IsEmpty()) base_name = "Workspace";

	TreeNode& root = tree.Add(dir_path, base_name, "#", "folder")
	                     .Icon("folder", "folder-open")
	                     .Expanded(true);

	WalkDirectory(dir_path, dir_path, root, 1, max_depth, extensions);
	return tree;
}

static String MakeSlug(const String& text)
{
	String slug;
	for(int i = 0; i < text.GetLength(); i++) {
		char c = text[i];
		if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
			slug.Cat(c);
		} else if(c >= 'A' && c <= 'Z') {
			slug.Cat(ToLower(c));
		} else if(c == ' ' || c == '-' || c == '_') {
			if(!slug.IsEmpty() && slug[slug.GetLength() - 1] != '-')
				slug.Cat('-');
		}
	}
	if(slug.EndsWith("-")) slug.Remove(slug.GetLength() - 1, 1);
	return slug.IsEmpty() ? "section" : slug;
}

TreeModel TreeGenerators::FromHeadings(const String& html_body)
{
	TreeModel tree;
	TreeNode* cur_h1 = NULL;
	TreeNode* cur_h2 = NULL;

	int pos = 0;
	int heading_idx = 1;
	while(pos < html_body.GetLength()) {
		int next_tag = html_body.Find("<h", pos);
		if(next_tag < 0 || next_tag + 3 >= html_body.GetLength()) break;

		char level_char = html_body[next_tag + 2];
		if(level_char >= '1' && level_char <= '3') {
			int level = level_char - '0';
			int tag_end = html_body.Find('>', next_tag);
			if(tag_end < 0) break;

			String close_tag = Format("</h%c>", level_char);
			int close_pos = html_body.Find(close_tag, tag_end);
			if(close_pos < 0) {
				pos = tag_end + 1;
				continue;
			}

			String raw_text = html_body.Mid(tag_end + 1, close_pos - (tag_end + 1));
			// Strip nested HTML tags inside heading
			String clean_text;
			bool in_tag = false;
			for(int i = 0; i < raw_text.GetLength(); i++) {
				if(raw_text[i] == '<') in_tag = true;
				else if(raw_text[i] == '>') in_tag = false;
				else if(!in_tag) clean_text.Cat(raw_text[i]);
			}
			clean_text = TrimBoth(clean_text);

			if(!clean_text.IsEmpty()) {
				// Check for existing id in tag
				String tag_attrs = html_body.Mid(next_tag, tag_end - next_tag);
				String slug;
				int id_pos = tag_attrs.Find("id=\"");
				if(id_pos >= 0) {
					int val_start = id_pos + 4;
					int val_end = tag_attrs.Find('"', val_start);
					if(val_end > val_start)
						slug = tag_attrs.Mid(val_start, val_end - val_start);
				}
				if(slug.IsEmpty()) {
					slug = MakeSlug(clean_text) + "-" + AsString(heading_idx++);
				}

				TreeNode node(slug, clean_text, "#" + slug, "hash");
				node.Expanded(true);

				if(level == 1) {
					TreeNode& r = tree.Add(node);
					cur_h1 = &r;
					cur_h2 = NULL;
				}
				else if(level == 2) {
					if(cur_h1) {
						TreeNode& ch = cur_h1->AddChild(node);
						cur_h2 = &ch;
					} else {
						TreeNode& r = tree.Add(node);
						cur_h2 = &r;
					}
				}
				else if(level == 3) {
					if(cur_h2) {
						cur_h2->AddChild(node);
					} else if(cur_h1) {
						cur_h1->AddChild(node);
					} else {
						tree.Add(node);
					}
				}
			}
			pos = close_pos + close_tag.GetLength();
		} else {
			pos = next_tag + 2;
		}
	}

	return tree;
}

// ---------------------------------------------------------------------------
// TreeRenderer Implementation
// ---------------------------------------------------------------------------

void TreeRenderer::RenderNode(String& h, const TreeNode& node, int level)
{
	bool has_children = node.HasChildren();
	String node_classes = "wf-tree-node";
	if(has_children) node_classes << " has-children";
	if(node.is_expanded) node_classes << " is-expanded";
	if(node.is_selected) node_classes << " is-selected";

	h << "<li class=\"" << node_classes << "\" data-node-id=\"" << node.id << "\" role=\"treeitem\""
	  << (has_children ? Format(" aria-expanded=\"%s\"", node.is_expanded ? "true" : "false") : "")
	  << ">\n"
	  << "  <div class=\"wf-tree-row\" onclick=\"TreeNavigator.rowClick(event, this)\""
	  << (!node.tooltip.IsEmpty() ? " title=\"" + DeHtml(node.tooltip) + "\"" : "")
	  << ">\n";

	// Chevron toggle button (if node has children) or spacer
	if(has_children) {
		h << "    <span class=\"wf-tree-chevron\" onclick=\"TreeNavigator.toggleNode(event, this.parentElement)\" title=\"Toggle branch\">"
		  << VividIcon::Get("chevron-right", 12)
		  << "</span>\n";
	} else {
		h << "    <span class=\"wf-tree-chevron-spacer\"></span>\n";
	}

	// Node Icon
	String ic = !node.icon.IsEmpty() ? node.icon : String(has_children ? "folder" : "file");
	h << "    <span class=\"wf-tree-icon\" data-icon-closed=\"" << ic << "\""
	  << (!node.icon_open.IsEmpty() ? " data-icon-open=\"" + node.icon_open + "\"" : "") << ">"
	  << VividIcon::Get((node.is_expanded && !node.icon_open.IsEmpty()) ? node.icon_open : ic, 15)
	  << "</span>\n";

	// Node Label / Anchor
	String safe_tooltip = !node.tooltip.IsEmpty() ? node.tooltip : node.label;
	if(!node.url.IsEmpty() && node.url != "#") {
		h << "    <a href=\"" << node.url << "\" class=\"wf-tree-label\" title=\"" << DeHtml(safe_tooltip) << "\">" << node.label << "</a>\n";
	} else {
		h << "    <span class=\"wf-tree-label\" title=\"" << DeHtml(safe_tooltip) << "\">" << node.label << "</span>\n";
	}

	// Optional Badge
	if(!node.badge.text.IsEmpty()) {
		h << "    <span class=\"wf-badge wf-badge-" << node.badge.style << "\">" << node.badge.text << "</span>\n";
	}

	h << "  </div>\n"; // .wf-tree-row

	// Render children recursively
	if(has_children) {
		h << "  <ul class=\"wf-tree-children\" role=\"group\">\n";
		for(int i = 0; i < node.children.GetCount(); i++)
			RenderNode(h, node.children[i], level + 1);
		h << "  </ul>\n";
	}

	h << "</li>\n";
}

String TreeRenderer::Render(const TreeModel& tree, const String& tree_id,
                            const String& title, bool show_filter, bool show_actions)
{
	String h;
	h << "<div class=\"wf-tree-container\" id=\"" << tree_id << "\">\n";

	// Tree Header
	if(!title.IsEmpty() || show_actions) {
		h << "  <div class=\"wf-tree-header\">\n";
		if(!title.IsEmpty()) {
			h << "    <span class=\"wf-tree-title\">" << title << "</span>\n";
		}
		if(show_actions) {
			h << "    <div class=\"wf-tree-actions\">\n"
			  << "      <button type=\"button\" class=\"wf-tree-btn\" onclick=\"TreeNavigator.expandAll('" << tree_id << "')\" title=\"Expand All Branches\">"
			  << VividIcon::Get("chevron-down", 12) << " Expand"
			  << "</button>\n"
			  << "      <button type=\"button\" class=\"wf-tree-btn\" onclick=\"TreeNavigator.collapseAll('" << tree_id << "')\" title=\"Collapse All Branches\">"
			  << VividIcon::Get("chevron-right", 12) << " Collapse"
			  << "</button>\n"
			  << "    </div>\n";
		}
		h << "  </div>\n";
	}

	// Search / Filter bar
	if(show_filter) {
		h << "  <div class=\"wf-tree-filter-bar\">\n"
		  << "    <span class=\"wf-tree-filter-icon\">" << VividIcon::Get("search", 13) << "</span>\n"
		  << "    <input type=\"text\" class=\"wf-tree-filter-input\" placeholder=\"Filter tree... (/)\" oninput=\"TreeNavigator.filter('" << tree_id << "', this.value)\">\n"
		  << "    <button type=\"button\" class=\"wf-tree-clear-btn\" onclick=\"TreeNavigator.clearFilter('" << tree_id << "')\" title=\"Clear filter\">&times;</button>\n"
		  << "  </div>\n";
	}

	// Root List
	h << "  <ul class=\"wf-tree-root\" role=\"tree\">\n";
	for(int i = 0; i < tree.GetCount(); i++)
		RenderNode(h, tree[i], 0);
	h << "  </ul>\n";

	h << "</div>\n";
	return h;
}

String TreeRenderer::GetStyles()
{
	return
		"  /* WebFramework Tree View Navigator Component */\n"
		"  .wf-tree-container { display: flex; flex-direction: column; width: 100%; font-size: 0.88rem; user-select: none; }\n"
		"  .wf-tree-header { display: flex; align-items: center; justify-content: space-between; padding: 6px 8px 10px 8px; border-bottom: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); margin-bottom: 8px; }\n"
		"  .wf-tree-title { font-weight: 700; font-size: 0.82rem; text-transform: uppercase; letter-spacing: 0.06em; color: var(--wf-accent-focus, #00a1c5); display: flex; align-items: center; gap: 6px; }\n"
		"  .wf-tree-actions { display: flex; gap: 4px; }\n"
		"  .wf-tree-btn { background: transparent; border: 1px solid var(--wf-border-color, rgba(255,255,255,0.1)); border-radius: 4px; color: var(--wf-fg-inactive, #8b919d); font-size: 0.72rem; padding: 2px 6px; cursor: pointer; display: flex; align-items: center; gap: 3px; transition: all 0.15s; }\n"
		"  .wf-tree-btn:hover { background: var(--wf-surface-hover, rgba(0,161,197,0.15)); color: #ffffff; border-color: var(--wf-accent-focus, #00a1c5); }\n"
		"\n"
		"  /* Search Filter Bar */\n"
		"  .wf-tree-filter-bar { display: flex; align-items: center; gap: 6px; background: var(--wf-bg-alt, #1c202f); border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); border-radius: 6px; padding: 4px 8px; margin-bottom: 8px; }\n"
		"  .wf-tree-filter-icon { color: var(--wf-fg-inactive, #8b919d); display: flex; align-items: center; }\n"
		"  .wf-tree-filter-input { flex: 1; background: transparent; border: none; outline: none; color: var(--wf-fg-normal, #d3dae3); font-size: 0.82rem; font-family: inherit; }\n"
		"  .wf-tree-clear-btn { background: transparent; border: none; color: var(--wf-fg-inactive, #8b919d); cursor: pointer; font-size: 1rem; line-height: 1; padding: 0 2px; display: none; }\n"
		"  .wf-tree-filter-bar.has-text .wf-tree-clear-btn { display: block; }\n"
		"\n"
		"  /* Tree Hierarchy */\n"
		"  .wf-tree-root, .wf-tree-children { list-style: none; margin: 0; padding: 0; }\n"
		"  .wf-tree-children { padding-left: 7px; border-left: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.15)); margin-left: 3px; display: none; }\n"
		"  .wf-tree-node.is-expanded > .wf-tree-children { display: block; }\n"
		"  .wf-tree-node { margin: 1px 0; position: relative; }\n"
		"\n"
		"  /* Tree Row */\n"
		"  .wf-tree-row { display: flex; align-items: center; gap: 4px; padding: 3px 6px; border-radius: 5px; color: var(--wf-fg-normal, #d3dae3); cursor: pointer; transition: background 0.12s, color 0.12s; min-height: 26px; }\n"
		"  .wf-tree-row:hover { background: var(--wf-surface-hover, rgba(0, 161, 197, 0.12)); color: #ffffff; }\n"
		"  .wf-tree-node.is-selected > .wf-tree-row { background: rgba(0, 161, 197, 0.2); color: var(--wf-accent-focus, #00a1c5); font-weight: 600; border-left: 2px solid var(--wf-accent-focus, #00a1c5); border-radius: 0 5px 5px 0; }\n"
		"\n"
		"  /* Chevrons */\n"
		"  .wf-tree-chevron { display: inline-flex; align-items: center; justify-content: center; width: 14px; height: 14px; color: var(--wf-fg-inactive, #8b919d); transition: transform 0.18s ease, color 0.15s; flex-shrink: 0; border-radius: 3px; }\n"
		"  .wf-tree-chevron:hover { color: var(--wf-accent-focus, #00a1c5); background: rgba(255,255,255,0.06); }\n"
		"  .wf-tree-node.is-expanded > .wf-tree-row > .wf-tree-chevron { transform: rotate(90deg); color: var(--wf-accent-focus, #00a1c5); }\n"
		"  .wf-tree-chevron-spacer { width: 14px; flex-shrink: 0; }\n"
		"\n"
		"  /* Icons & Labels */\n"
		"  .wf-tree-icon { display: flex; align-items: center; color: var(--wf-accent-focus, #00a1c5); flex-shrink: 0; }\n"
		"  .wf-tree-icon svg { width: 14px; height: 14px; }\n"
		"  .wf-tree-label { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; color: inherit; text-decoration: none; font-size: 0.85rem; }\n"
		"  .wf-tree-row a.wf-tree-label:hover { text-decoration: underline; }\n"
		"  .wf-tree-node.filter-hidden { display: none !important; }\n"
		"\n"
		"  /* Dual Dock Sidebars */\n"
		"  .wf-dual-layout { display: flex; width: 100%; max-width: var(--wf-container-width, 1500px); margin: 0 auto; gap: var(--wf-gap-grid, 16px); padding: var(--wf-pad-page, 1.25rem); align-items: flex-start; }\n"
		"  .wf-dock { width: var(--wf-dock-width, 320px); flex-shrink: 0; background: var(--wf-bg-view, #151d2b); border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); border-radius: var(--wf-radius, 8px); box-shadow: var(--wf-shadow); position: sticky; top: 76px; max-height: calc(100vh - 96px); overflow-y: auto; transition: width 0.2s ease, opacity 0.2s ease; display: flex; flex-direction: column; }\n"
		"  .wf-dock-left { width: var(--wf-dock-width-left, 370px); }\n"
		"  .wf-dock-right { width: var(--wf-dock-width-right, var(--wf-dock-width, 260px)); }\n"
		"  .wf-dock.collapsed { width: 38px; overflow: hidden; }\n"
		"  .wf-dock.collapsed .wf-dock-content { display: none; }\n"
		"  .wf-dock-content { flex: 1; padding: 8px 10px; overflow-y: auto; }\n"
		"  .wf-dual-main { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: var(--wf-gap-grid, 16px); }\n"
		"  .wf-dock-toggle { background: var(--wf-bg-alt, #1c202f); border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); color: var(--wf-fg-inactive, #8b919d); cursor: pointer; border-radius: 4px; padding: 4px 6px; display: flex; align-items: center; justify-content: center; margin: 6px; transition: all 0.15s; }\n"
		"  .wf-dock-toggle:hover { color: #ffffff; background: var(--wf-surface-hover, rgba(0, 161, 197, 0.15)); border-color: var(--wf-accent-focus, #00a1c5); }\n"
		"  .wf-dock.collapsed .wf-dock-toggle svg { transform: rotate(180deg); }\n"
		"\n"
		"  @media (max-width: 1024px) {\n"
		"    .wf-dual-layout { flex-direction: column; }\n"
		"    .wf-dock { width: 100% !important; position: static; max-height: none; }\n"
		"  }\n";
}

String TreeRenderer::GetClientScript()
{
	return
		"var TreeNavigator = (function() {\n"
		"  function init() {\n"
		"    // Bind keyboard shortcut '/' to focus nearest filter input\n"
		"    document.addEventListener('keydown', function(e) {\n"
		"      if (e.key === '/' && !['INPUT', 'TEXTAREA'].includes(document.activeElement.tagName)) {\n"
		"        e.preventDefault();\n"
		"        let inp = document.querySelector('.wf-tree-filter-input');\n"
		"        if (inp) { inp.focus(); inp.select(); }\n"
		"      }\n"
		"    });\n"
		"  }\n"
		"  function toggleNode(event, row) {\n"
		"    if (event) event.stopPropagation();\n"
		"    let node = row.closest('.wf-tree-node');\n"
		"    if (!node) return;\n"
		"    let isExpanded = node.classList.toggle('is-expanded');\n"
		"    node.setAttribute('aria-expanded', isExpanded ? 'true' : 'false');\n"
		"    // Toggle open icon if configured\n"
		"    let iconEl = row.querySelector('.wf-tree-icon');\n"
		"    if (iconEl) {\n"
		"      let closedIc = iconEl.getAttribute('data-icon-closed');\n"
		"      let openIc = iconEl.getAttribute('data-icon-open');\n"
		"      if (openIc) {\n"
		"        // SVG swap handled dynamically if needed\n"
		"      }\n"
		"    }\n"
		"    if (window.WebAudio) WebAudio.play('click');\n"
		"  }\n"
		"  function rowClick(event, row) {\n"
		"    // If clicked directly on row (and not on an <a> link or chevron), toggle expansion\n"
		"    if (event.target.tagName !== 'A' && !event.target.closest('.wf-tree-chevron')) {\n"
		"      toggleNode(event, row);\n"
		"    }\n"
		"  }\n"
		"  function expandAll(treeId) {\n"
		"    let container = document.getElementById(treeId);\n"
		"    if (!container) return;\n"
		"    container.querySelectorAll('.wf-tree-node.has-children').forEach(n => {\n"
		"      n.classList.add('is-expanded');\n"
		"      n.setAttribute('aria-expanded', 'true');\n"
		"    });\n"
		"    if (window.WebAudio) WebAudio.play('switch');\n"
		"  }\n"
		"  function collapseAll(treeId) {\n"
		"    let container = document.getElementById(treeId);\n"
		"    if (!container) return;\n"
		"    container.querySelectorAll('.wf-tree-node.has-children').forEach(n => {\n"
		"      n.classList.remove('is-expanded');\n"
		"      n.setAttribute('aria-expanded', 'false');\n"
		"    });\n"
		"    if (window.WebAudio) WebAudio.play('switch');\n"
		"  }\n"
		"  function filter(treeId, query) {\n"
		"    let container = document.getElementById(treeId);\n"
		"    if (!container) return;\n"
		"    let q = (query || '').trim().toLowerCase();\n"
		"    let bar = container.querySelector('.wf-tree-filter-bar');\n"
		"    if (bar) bar.classList.toggle('has-text', q.length > 0);\n"
		"\n"
		"    let allNodes = container.querySelectorAll('.wf-tree-node');\n"
		"    if (!q) {\n"
		"      allNodes.forEach(n => n.classList.remove('filter-hidden'));\n"
		"      return;\n"
		"    }\n"
		"    allNodes.forEach(n => {\n"
		"      let labelEl = n.querySelector('.wf-tree-label');\n"
		"      let txt = labelEl ? labelEl.innerText.toLowerCase() : '';\n"
		"      let matches = txt.includes(q);\n"
		"      if (matches) {\n"
		"        n.classList.remove('filter-hidden');\n"
		"        // Expand all parents up to root\n"
		"        let p = n.parentElement;\n"
		"        while (p && p !== container) {\n"
		"          if (p.classList.contains('wf-tree-node')) {\n"
		"            p.classList.remove('filter-hidden');\n"
		"            p.classList.add('is-expanded');\n"
		"          }\n"
		"          p = p.parentElement;\n"
		"        }\n"
		"      } else {\n"
		"        n.classList.add('filter-hidden');\n"
		"      }\n"
		"    });\n"
		"    // Ensure matched parents with children unhide their matching descendants\n"
		"    container.querySelectorAll('.wf-tree-node:not(.filter-hidden)').forEach(n => {\n"
		"      let parent = n.parentElement;\n"
		"      while (parent && parent !== container) {\n"
		"        if (parent.classList.contains('wf-tree-node')) parent.classList.remove('filter-hidden');\n"
		"        parent = parent.parentElement;\n"
		"      }\n"
		"    });\n"
		"  }\n"
		"  function clearFilter(treeId) {\n"
		"    let container = document.getElementById(treeId);\n"
		"    if (!container) return;\n"
		"    let inp = container.querySelector('.wf-tree-filter-input');\n"
		"    if (inp) { inp.value = ''; filter(treeId, ''); inp.focus(); }\n"
		"  }\n"
		"  function toggleDock(dockId) {\n"
		"    let el = document.getElementById(dockId);\n"
		"    if (!el) return;\n"
		"    let isCol = el.classList.toggle('collapsed');\n"
		"    localStorage.setItem('wf_dock_' + dockId, isCol ? 'collapsed' : 'open');\n"
		"    if (window.WebAudio) WebAudio.play('switch');\n"
		"  }\n"
		"  return {\n"
		"    init: init,\n"
		"    toggleNode: toggleNode,\n"
		"    rowClick: rowClick,\n"
		"    expandAll: expandAll,\n"
		"    collapseAll: collapseAll,\n"
		"    filter: filter,\n"
		"    clearFilter: clearFilter,\n"
		"    toggleDock: toggleDock\n"
		"  };\n"
		"})();\n"
		"document.addEventListener('DOMContentLoaded', TreeNavigator.init);\n";
}

END_UPP_NAMESPACE
