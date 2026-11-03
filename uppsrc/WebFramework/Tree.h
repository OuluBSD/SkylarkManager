#ifndef _WebFramework_Tree_h_
#define _WebFramework_Tree_h_

// Generic Hierarchical Tree View Navigator Model & Automatic Generators

struct TreeNode : Moveable<TreeNode> {
	String           id;
	String           label;
	String           url;
	String           icon;        // Icon name from VividIcon (e.g. "folder", "file", "code")
	String           icon_open;   // Icon when node is expanded (e.g. "folder-open")
	NavBadge         badge;       // Optional badge (count, status)
	bool             is_expanded;
	bool             is_selected;
	String           tooltip;
	Vector<TreeNode> children;

	TreeNode();
	TreeNode(const String& id, const String& label, const String& url = "", const String& icon = "");
	TreeNode(const TreeNode& o);
	TreeNode& operator=(const TreeNode& o);

	TreeNode& Url(const String& u) { url = u; return *this; }
	TreeNode& Icon(const String& ic, const String& ic_open = "") { icon = ic; icon_open = ic_open; return *this; }
	TreeNode& Badge(const String& text, const String& style = "cyan") { badge = NavBadge(text, style); return *this; }
	TreeNode& Expanded(bool b = true) { is_expanded = b; return *this; }
	TreeNode& Selected(bool b = true) { is_selected = b; return *this; }
	TreeNode& Tooltip(const String& tip) { tooltip = tip; return *this; }

	TreeNode& AddChild(const TreeNode& child);
	TreeNode& AddChild(const String& id, const String& label, const String& url = "", const String& icon = "");

	bool HasChildren() const { return !children.IsEmpty(); }
	int  GetCount() const { return children.GetCount(); }

	void Jsonize(JsonIO& io);
	Value ToValue() const;
};

class TreeModel : Moveable<TreeModel> {
public:
	TreeModel();
	TreeModel(const TreeModel& o);
	TreeModel& operator=(const TreeModel& o);

	TreeNode& Add(const TreeNode& node);
	TreeNode& Add(const String& id, const String& label, const String& url = "", const String& icon = "");

	TreeNode*       Find(const String& id);
	const TreeNode* Find(const String& id) const;

	void SetSelected(const String& id);
	void ExpandAll(bool b = true);
	void Clear() { roots.Clear(); }

	const Vector<TreeNode>& GetRoots() const { return roots; }
	Vector<TreeNode>&       GetRoots()       { return roots; }
	int  GetCount() const { return roots.GetCount(); }
	bool IsEmpty() const  { return roots.IsEmpty(); }
	const TreeNode& operator[](int i) const { return roots[i]; }

	void   Jsonize(JsonIO& io);
	String ToJSON() const;
	bool   FromJSON(const String& json);
	Value  ToValue() const;

private:
	Vector<TreeNode> roots;

	TreeNode*       FindRecursive(Vector<TreeNode>& list, const String& id);
	const TreeNode* FindRecursive(const Vector<TreeNode>& list, const String& id) const;
	void SetSelectedRecursive(Vector<TreeNode>& list, const String& id);
	void ExpandRecursive(Vector<TreeNode>& list, bool b);
};

// Automatic Tree Content Generators
class TreeGenerators {
public:
	// Automatically generates tree from top-level NavigationModel
	static TreeModel FromNavigation(const NavigationModel& nav);

	// Automatically walks a directory to generate a folder/file tree
	static TreeModel FromDirectory(const String& dir_path, int max_depth = 3, const Vector<String>& extensions = {});

	// Automatically parses H1, H2, H3 tags in HTML to produce an in-page outline Table of Contents
	static TreeModel FromHeadings(const String& html_body);
};

// Tree Renderer & Client Controls
class TreeRenderer {
public:
	static String Render(const TreeModel& tree, const String& tree_id = "wf-tree",
	                     const String& title = "", bool show_filter = true, bool show_actions = true);

	static String GetStyles();
	static String GetClientScript();

private:
	static void RenderNode(String& h, const TreeNode& node, int level);
};

#endif
