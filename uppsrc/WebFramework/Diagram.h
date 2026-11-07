#ifndef _WebFramework_Diagram_h_
#define _WebFramework_Diagram_h_

#include <Core/Core.h>

// ---------------------------------------------------------------------------
// Generic architectural topology graph: nodes + typed edges, rendered to a
// layered (top-down hierarchy) SVG with theme-integrated colors and hover
// states, wrapped in an interactive pan/zoom/maximize viewer.
// ---------------------------------------------------------------------------

struct TopologyNode : Moveable<TopologyNode> {
	String id;
	String name;
	String type;      // free-form node type (machine, service, pipeline, component, ...)
	String url;       // optional clickable target
	String parent;    // optional containment parent id
	String badge;     // optional status label rendered under the name
	// Computed by layout:
	int    layer;
	double x, y, w, h;

	TopologyNode() : layer(0), x(0), y(0), w(0), h(0) {}
	TopologyNode(const TopologyNode& o) : id(o.id), name(o.name), type(o.type), url(o.url), parent(o.parent), badge(o.badge), layer(o.layer), x(o.x), y(o.y), w(o.w), h(o.h) {}
	TopologyNode& operator=(const TopologyNode& o) {
		if(this != &o) {
			id = o.id; name = o.name; type = o.type; url = o.url; parent = o.parent; badge = o.badge;
			layer = o.layer; x = o.x; y = o.y; w = o.w; h = o.h;
		}
		return *this;
	}
};

struct TopologyEdge : Moveable<TopologyEdge> {
	String from;
	String to;
	String role;    // "contains", "depends-on", "feeds", ... affects line style
	String label;

	TopologyEdge() {}
	TopologyEdge(const TopologyEdge& o) : from(o.from), to(o.to), role(o.role), label(o.label) {}
	TopologyEdge& operator=(const TopologyEdge& o) {
		if(this != &o) { from = o.from; to = o.to; role = o.role; label = o.label; }
		return *this;
	}
};

struct TopologyGraph : Moveable<TopologyGraph> {
	String               title;
	Vector<TopologyNode> nodes;
	Vector<TopologyEdge> edges;
	// Computed layout extents:
	double content_w, content_h;

	TopologyGraph() : content_w(0), content_h(0) {}
	TopologyGraph(const TopologyGraph& o)
		: title(o.title), nodes(clone(o.nodes)), edges(clone(o.edges)), content_w(o.content_w), content_h(o.content_h) {}
	TopologyGraph& operator=(const TopologyGraph& o) {
		if(this != &o) {
			title = o.title; nodes = clone(o.nodes); edges = clone(o.edges);
			content_w = o.content_w; content_h = o.content_h;
		}
		return *this;
	}

	TopologyNode& AddNode(const String& id, const String& name, const String& type = "",
	                      const String& url = "", const String& parent = "", const String& badge = "");
	TopologyEdge& AddEdge(const String& from, const String& to, const String& role = "", const String& label = "");
	TopologyNode* FindNode(const String& id);
	bool          IsEmpty() const { return nodes.IsEmpty(); }

	// Generic JSON input: {"title": .., "nodes": [{id,name,type,url,parent,badge}],
	// "edges": [{from,to,role,label}]}
	bool FromJSON(const Value& v);
	bool FromJSONString(const String& json);

	Value ToValue() const;
};

// Layered graph layout + themed SVG generation + interactive viewer chrome.
class DiagramRenderer {
public:
	// Layered (top-down) layout: containment parent hierarchy defines the
	// primary layering; cross-tree dependency edges push targets deeper.
	// Deterministic ordering keeps subtrees together. Returns false on cycles.
	static bool Layout(TopologyGraph& g);

	// Standalone, theme-integrated SVG (uses --wf-* CSS variables so it follows
	// the active theme; hover colors via CSS). add_viewer wraps it in the
	// interactive pan/zoom/maximize chrome.
	static String ToSVG(const TopologyGraph& g, bool add_viewer = true, const String& viewer_id = "wf-diag");

	static String GetStyles();
	static String GetClientScript();

	// Optional PlantUML passthrough: renders .puml text to SVG using the
	// external plantuml binary (env PLANTUML_BIN or "plantuml"), cached under
	// the temp dir. Returns false when the binary is unavailable.
	static bool FromPlantUml(const String& puml_source, const String& cache_key, String& svg_out, String& error);

private:
	// Only generated allowlisted SVG from ToSVG may enter the raw viewer slot.
	static String WrapSvgViewer(const String& svg, const String& viewer_id, const String& title);
};

#endif
