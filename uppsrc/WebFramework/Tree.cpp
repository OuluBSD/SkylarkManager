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
	return WebFrameworkAsset("tree.css");
}

String TreeRenderer::GetClientScript()
{
	return WebFrameworkAsset("tree.js");
}

END_UPP_NAMESPACE
