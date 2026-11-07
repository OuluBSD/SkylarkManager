#include "WebFramework.h"

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// TopologyGraph
// ---------------------------------------------------------------------------

TopologyNode& TopologyGraph::AddNode(const String& id, const String& name, const String& type,
                                     const String& url, const String& parent, const String& badge)
{
	TopologyNode& n = nodes.Add();
	n.id = id;
	n.name = name.IsEmpty() ? id : name;
	n.type = type;
	n.url = url;
	n.parent = parent;
	n.badge = badge;
	return n;
}

TopologyEdge& TopologyGraph::AddEdge(const String& from, const String& to, const String& role, const String& label)
{
	TopologyEdge& e = edges.Add();
	e.from = from;
	e.to = to;
	e.role = role;
	e.label = label;
	return e;
}

TopologyNode* TopologyGraph::FindNode(const String& id)
{
	for(int i = 0; i < nodes.GetCount(); i++)
		if(nodes[i].id == id)
			return &nodes[i];
	return NULL;
}

bool TopologyGraph::FromJSON(const Value& v)
{
	if(IsNull(v) || !v.Is<ValueMap>())
		return false;

	Value t = v["title"];
	if(!IsNull(t))
		title = (String)t;

	Value nv = v["nodes"];
	if(nv.Is<ValueArray>())
		for(int i = 0; i < nv.GetCount(); i++) {
			const Value& n = nv[i];
			if(IsNull(n) || !n.Is<ValueMap>())
				continue;
			AddNode((String)n["id"], (String)n["name"], (String)n["type"],
			        (String)n["url"], (String)n["parent"], (String)n["badge"]);
		}

	Value ev = v["edges"];
	if(ev.Is<ValueArray>())
		for(int i = 0; i < ev.GetCount(); i++) {
			const Value& e = ev[i];
			if(IsNull(e) || !e.Is<ValueMap>())
				continue;
			AddEdge((String)e["from"], (String)e["to"], (String)e["role"], (String)e["label"]);
		}
	return !nodes.IsEmpty();
}

bool TopologyGraph::FromJSONString(const String& json)
{
	return FromJSON(ParseJSON(json));
}

Value TopologyGraph::ToValue() const
{
	ValueMap m;
	m.Add("title", title);
	ValueArray na;
	for(int i = 0; i < nodes.GetCount(); i++) {
		ValueMap nm;
		nm.Add("id", nodes[i].id);
		nm.Add("name", nodes[i].name);
		if(!nodes[i].type.IsEmpty())  nm.Add("type", nodes[i].type);
		if(!nodes[i].url.IsEmpty())   nm.Add("url", nodes[i].url);
		if(!nodes[i].parent.IsEmpty())nm.Add("parent", nodes[i].parent);
		if(!nodes[i].badge.IsEmpty()) nm.Add("badge", nodes[i].badge);
		nm.Add("layer", nodes[i].layer);
		na.Add(nm);
	}
	m.Add("nodes", na);
	ValueArray ea;
	for(int i = 0; i < edges.GetCount(); i++) {
		ValueMap em;
		em.Add("from", edges[i].from);
		em.Add("to", edges[i].to);
		if(!edges[i].role.IsEmpty())  em.Add("role", edges[i].role);
		if(!edges[i].label.IsEmpty()) em.Add("label", edges[i].label);
		ea.Add(em);
	}
	m.Add("edges", ea);
	return m;
}

// ---------------------------------------------------------------------------
// Layered layout
// ---------------------------------------------------------------------------

struct DiagNodeIdx : Moveable<DiagNodeIdx> {
	int    at;
	Vector<int> children;
	Vector<int> parents;
	int    layer;
	bool   placed;
	bool   in_stack;   // cycle detection during layering

	DiagNodeIdx() : at(-1), layer(0), placed(false), in_stack(false) {}
};

static String EscapeXmlText(const String& s)
{
	String r;
	r.Reserve(s.GetCount() + 8);
	for(int i = 0; i < s.GetCount(); i++) {
		char c = s[i];
		switch(c) {
		case '&':  r << "&amp;"; break;
		case '<':  r << "&lt;"; break;
		case '>':  r << "&gt;"; break;
		case '"':  r << "&quot;"; break;
		case '\'': r << "&apos;"; break;
		default:   r.Cat(c);
		}
	}
	return r;
}

static bool IsSafeLocalPath(const String& s)
{
	if(s.IsEmpty() || s[0] != '/' || s.StartsWith("//"))
		return false;
	for(int i = 0; i < s.GetCount(); i++) {
		byte c = (byte)s[i];
		if(c < 0x20 || c == 0x7f || c == '\\' || c == '"' || c == '\'')
			return false;
	}
	return true;
}

static double NodeWidth(const TopologyNode& n)
{
	int len = n.name.GetCount();
	if(n.badge.GetCount() > len)
		len = n.badge.GetCount();
	return min(220.0, max(96.0, 26.0 + 7.2 * len));
}

bool DiagramRenderer::Layout(TopologyGraph& g)
{
	if(g.nodes.IsEmpty())
		return false;

	int N = g.nodes.GetCount();
	Vector<DiagNodeIdx> idx;
	idx.SetCount(N);
	Index<String> id_index;
	for(int i = 0; i < N; i++) {
		idx[i].at = i;
		id_index.FindAdd(g.nodes[i].id);
	}

	// Containment hierarchy from parent links.
	for(int i = 0; i < N; i++) {
		const String& p = g.nodes[i].parent;
		if(p.IsEmpty())
			continue;
		int pi = id_index.Find(p);
		if(pi < 0 || pi == i)
			continue;
		idx[pi].children.Add(i);
		idx[i].parents.Add(pi);
	}

	// Dependency edges also constrain layering (target goes below source).
	struct DiagEdgeIdx { int from; int to; };
	Vector<DiagEdgeIdx> deps;
	for(int i = 0; i < g.edges.GetCount(); i++) {
		int fi = id_index.Find(g.edges[i].from);
		int ti = id_index.Find(g.edges[i].to);
		if(fi < 0 || ti < 0 || fi == ti)
			continue;
		if(g.edges[i].role == "contains")
			continue; // already handled by parent hierarchy
		deps.Add({ fi, ti });
		idx[ti].parents.Add(fi);
	}

	// DFS layering with cycle detection.
	Vector<int> stack;
	stack.Reserve(N);
	for(int i = 0; i < N; i++) {
		if(idx[i].parents.GetCount() > 0)
			continue;
		stack.Add(i);
		idx[i].in_stack = true;
	}
	while(stack.GetCount()) {
		int at = stack.Top();
		stack.Drop();
		idx[at].in_stack = false;
		int next_layer = idx[at].layer + 1;
		const Vector<int>& ch = idx[at].children;
		for(int c = 0; c < ch.GetCount(); c++) {
			if(idx[ch[c]].in_stack)
				return false; // containment cycle
			if(idx[ch[c]].layer < next_layer) {
				idx[ch[c]].layer = next_layer;
				if(!idx[ch[c]].placed) {
					idx[ch[c]].in_stack = true;
					stack.Add(ch[c]);
				}
			}
		}
		idx[at].placed = true;
	}

	// Cross-tree dependency relaxation (longest path, iterative, bounded).
	for(int round = 0; round < N + 2; round++) {
		bool changed = false;
		for(int d = 0; d < deps.GetCount(); d++) {
			int f = deps[d].from, t = deps[d].to;
			if(idx[t].layer < idx[f].layer + 1) {
				idx[t].layer = idx[f].layer + 1;
				changed = true;
			}
		}
		if(!changed)
			break;
	}

	// Deterministic order: containment DFS (subtrees stay together), then any
	// unplaced nodes in declaration order. Assign horizontal order per layer.
	Vector<Vector<int>> layers;
	int max_layer = 0;
	for(int i = 0; i < N; i++)
		max_layer = max(max_layer, idx[i].layer);
	layers.SetCount(max_layer + 1);

	Vector<bool> visited;
	visited.SetCount(N, false);
	Vector<int> dfs;
	for(int i = 0; i < N; i++)
		if(idx[i].parents.IsEmpty())
			dfs.Add(i);
	for(int pos = 0; pos < dfs.GetCount(); pos++) {
		int at = dfs[pos];
		if(visited[at])
			continue;
		visited[at] = true;
		layers[idx[at].layer].Add(at);
		const Vector<int>& ch = idx[at].children;
		// Insert children right after this node in the DFS queue.
		for(int c = 0; c < ch.GetCount(); c++)
			if(!visited[ch[c]])
				dfs.Insert(pos + 1 + c, ch[c]);
	}
	for(int i = 0; i < N; i++)
		if(!visited[i])
			layers[idx[i].layer].Add(i);

	// Horizontal placement with subtree-preserving order, layers centered.
	const double NODE_H = 46.0;
	const double X_GAP = 34.0;
	const double LAYER_GAP = 72.0;

	Vector<double> layer_w(max_layer + 1, 0.0);
	for(int L = 0; L <= max_layer; L++) {
		double w = 0;
		for(int k = 0; k < layers[L].GetCount(); k++)
			w += NodeWidth(g.nodes[layers[L][k]]) + (k ? X_GAP : 0);
		layer_w[L] = w;
	}
	double content_w = 0;
	for(int L = 0; L <= max_layer; L++)
		content_w = max(content_w, layer_w[L]);

	for(int L = 0; L <= max_layer; L++) {
		double x = (content_w - layer_w[L]) / 2.0;
		for(int k = 0; k < layers[L].GetCount(); k++) {
			int at = layers[L][k];
			TopologyNode& n = g.nodes[at];
			n.layer = idx[at].layer;
			n.w = NodeWidth(n);
			n.h = idx[at].children.GetCount() || n.badge.GetCount() ? NODE_H : NODE_H - 4;
			n.x = x;
			n.y = L * (NODE_H + LAYER_GAP);
			x += n.w + X_GAP;
		}
	}
	g.content_w = content_w;
	g.content_h = (max_layer + 1) * (NODE_H + LAYER_GAP) - LAYER_GAP;
	return true;
}

// ---------------------------------------------------------------------------
// SVG generation
// ---------------------------------------------------------------------------

static String TypeCssClass(const String& type)
{
	String t = ToLower(type);
	String r;
	for(int i = 0; i < t.GetCount(); i++) {
		char c = t[i];
		if((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
			r.Cat(c);
		else
			r.Cat('-');
	}
	return r;
}

static String EdgeCssClass(const String& role)
{
	if(role == "contains")   return "wf-diag-e-contains";
	if(role == "depends-on") return "wf-diag-e-depends";
	if(role == "feeds")      return "wf-diag-e-feeds";
	return "wf-diag-e-other";
}

static void RenderSvgNode(String& svg, const TopologyNode& n)
{
	svg << "<g class=\"wf-diag-n\" transform=\"translate(" << Format("%.1f", n.x) << "," << Format("%.1f", n.y) << ")\"";
	if(IsSafeLocalPath(n.url))
		svg << " data-href=\"" << EscapeXmlText(n.url) << "\"";
	svg << ">\n";
	svg << "  <rect class=\"wf-diag-nr wf-diag-nt-" << TypeCssClass(n.type)
	    << "\" width=\"" << Format("%.1f", n.w) << "\" height=\"" << Format("%.1f", n.h) << "\" rx=\"7\"/>\n";

	String line1 = n.name, line2;
	if(line1.GetCount() > 26) {
		int cut = -1;
		for(int i = 13; i < line1.GetCount() - 6; i++)
			if(line1[i] == ' ')
				cut = i;
		if(cut > 0) {
			line2 = line1.Mid(cut + 1);
			line1 = line1.Left(cut);
		}
	}
	double cx = n.w / 2.0;
	double cy = n.h / 2.0 - (line2.IsEmpty() ? 0 : 5);
	svg << "  <text class=\"wf-diag-nlabel\" x=\"" << Format("%.1f", cx) << "\" y=\"" << Format("%.1f", cy) << "\">"
	    << EscapeXmlText(line1) << "</text>\n";
	if(!line2.IsEmpty())
		svg << "  <text class=\"wf-diag-nlabel\" x=\"" << Format("%.1f", cx) << "\" y=\""
		    << Format("%.1f", cy + 14) << "\">" << EscapeXmlText(line2) << "</text>\n";
	if(!n.badge.IsEmpty())
		svg << "  <text class=\"wf-diag-nbadge\" x=\"" << Format("%.1f", cx) << "\" y=\""
		    << Format("%.1f", n.h - 6) << "\">" << EscapeXmlText(n.badge) << "</text>\n";
	svg << "</g>\n";
}

static void RenderSvgEdge(String& svg, const TopologyGraph& g, const TopologyEdge& e, int fi, int ti, const String& marker_id)
{
	const TopologyNode& f = g.nodes[fi];
	const TopologyNode& t = g.nodes[ti];

	double x1, y1, x2, y2;
	if(t.layer > f.layer) {
		x1 = f.x + f.w / 2.0; y1 = f.y + f.h;
		x2 = t.x + t.w / 2.0; y2 = t.y;
	}
	else {
		x1 = f.x + f.w; y1 = f.y + f.h / 2.0;
		x2 = t.x;       y2 = t.y + t.h / 2.0;
	}
	double dx = x2 - x1, dy = y2 - y1;
	double bend = (t.layer > f.layer) ? dy * 0.45 : dx * 0.45;
	String path;
	if(t.layer > f.layer)
		path << "M" << Format("%.1f", x1) << " " << Format("%.1f", y1)
		     << " C" << Format("%.1f", x1) << " " << Format("%.1f", y1 + bend)
		     << " " << Format("%.1f", x2) << " " << Format("%.1f", y2 - bend)
		     << " " << Format("%.1f", x2) << " " << Format("%.1f", y2);
	else
		path << "M" << Format("%.1f", x1) << " " << Format("%.1f", y1)
		     << " C" << Format("%.1f", x1 + bend) << " " << Format("%.1f", y1)
		     << " " << Format("%.1f", x2 - bend) << " " << Format("%.1f", y2)
		     << " " << Format("%.1f", x2) << " " << Format("%.1f", y2);

	svg << "<path class=\"wf-diag-e " << EdgeCssClass(e.role) << "\" d=\"" << path
	    << "\" marker-end=\"url(#" << EscapeXmlText(marker_id) << ")\"/>\n";
	if(!e.label.IsEmpty()) {
		double lx = (x1 + x2) / 2.0, ly = (y1 + y2) / 2.0;
		svg << "<text class=\"wf-diag-elabel\" x=\"" << Format("%.1f", lx) << "\" y=\""
		    << Format("%.1f", ly - 5) << "\">" << EscapeXmlText(e.label) << "</text>\n";
	}
}

String DiagramRenderer::ToSVG(const TopologyGraph& g, bool add_viewer, const String& viewer_id)
{
	String svg;
	if(g.nodes.IsEmpty())
		return svg;

	String marker_seed = g.title;
	for(int i = 0; i < g.nodes.GetCount(); i++)
		marker_seed << "|" << g.nodes[i].id << "|" << g.nodes[i].name;
	for(int i = 0; i < g.edges.GetCount(); i++)
		marker_seed << "|" << g.edges[i].from << "|" << g.edges[i].to << "|" << g.edges[i].role;
	String marker_id = "wf-diag-arrow-" + MD5String(marker_seed).Left(16);
	svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" class=\"wf-diag-svg\" viewBox=\"0 0 "
	    << Format("%.0f", max(g.content_w, 400.0)) << " " << Format("%.0f", max(g.content_h, 200.0)) << "\">\n";
	svg << "<defs>\n"
	    << "  <marker id=\"" << marker_id << "\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" markerWidth=\"7\" markerHeight=\"7\" orient=\"auto-start-reverse\">\n"
	    << "    <path d=\"M0,0 L10,5 L0,10 z\" class=\"wf-diag-arrow-shape\"/>\n"
	    << "  </marker>\n"
	    << "</defs>\n";
	if(!g.title.IsEmpty())
		svg << "<text class=\"wf-diag-title\" x=\"14\" y=\"22\">" << EscapeXmlText(g.title) << "</text>\n";

	for(int i = 0; i < g.edges.GetCount(); i++) {
		int fi = -1, ti = -1;
		for(int n = 0; n < g.nodes.GetCount(); n++) {
			if(g.nodes[n].id == g.edges[i].from) fi = n;
			if(g.nodes[n].id == g.edges[i].to)   ti = n;
		}
		if(fi >= 0 && ti >= 0)
			RenderSvgEdge(svg, g, g.edges[i], fi, ti, marker_id);
	}
	for(int i = 0; i < g.nodes.GetCount(); i++)
		RenderSvgNode(svg, g.nodes[i]);
	svg << "</svg>";

	return add_viewer ? WrapSvgViewer(svg, viewer_id, g.title) : svg;
}

// ---------------------------------------------------------------------------
// Interactive viewer chrome (pan / zoom / maximize)
// ---------------------------------------------------------------------------

String DiagramRenderer::GetStyles()
{
	String s;
	s <<
	"/* Diagram viewer (WebFramework) */\n"
	".wf-diag { position: relative; border: 1px solid var(--wf-border-color, rgba(0,161,197,0.25)); border-radius: var(--wf-radius, 8px); background: var(--wf-bg-view, #151d2b); overflow: hidden; min-height: 220px; }\n"
	".wf-diag-toolbar { display: flex; gap: 6px; align-items: center; padding: 6px 10px; background: var(--wf-bg-alt, #1c202f); border-bottom: 1px solid var(--wf-border-color, rgba(0,161,197,0.2)); z-index: 5; }\n"
	".wf-diag-btn { background: var(--wf-surface, #1e283b); color: var(--wf-fg-normal, #d3dae3); border: 1px solid var(--wf-border-color, rgba(0,161,197,0.25)); border-radius: 5px; padding: 3px 10px; font-size: 12px; cursor: pointer; }\n"
	".wf-diag-btn:hover { border-color: var(--wf-accent-focus, #00a1c5); color: var(--wf-accent-focus, #00a1c5); }\n"
	".wf-diag-status { margin-left: auto; font-size: 11px; color: var(--wf-fg-inactive, #8b919d); }\n"
	".wf-diag-viewport { position: relative; overflow: hidden; height: 540px; cursor: grab; touch-action: none; }\n"
	".wf-diag-viewport.wf-diag-dragging { cursor: grabbing; }\n"
	".wf-diag-canvas { position: absolute; top: 0; left: 0; transform-origin: 0 0; will-change: transform; }\n"
	".wf-diag.wf-diag-max { position: fixed; inset: 0; z-index: 9999; border-radius: 0; }\n"
	".wf-diag.wf-diag-max .wf-diag-viewport { height: calc(100vh - 42px); }\n"
	".wf-diag-svg { display: block; width: 100%; height: auto; }\n"
	".wf-diag-e { fill: none; stroke: var(--wf-fg-inactive, #8b919d); stroke-width: 1.6; }\n"
	".wf-diag-e-contains { stroke-dasharray: 5 4; stroke: var(--wf-accent-neutral, #6d55aa); }\n"
	".wf-diag-e-depends { stroke: var(--wf-accent-focus, #00a1c5); }\n"
	".wf-diag-e-feeds { stroke: var(--wf-accent-positive, #00aa7f); }\n"
	".wf-diag-arrow-shape { fill: var(--wf-fg-inactive, #8b919d); }\n"
	".wf-diag-elabel { fill: var(--wf-fg-inactive, #8b919d); font-size: 9px; text-anchor: middle; }\n"
	".wf-diag-title { fill: var(--wf-fg-normal, #d3dae3); font-size: 13px; font-weight: 600; }\n"
	".wf-diag-nr { fill: var(--wf-bg-alt, #1c202f); stroke: var(--wf-border-color, rgba(0,161,197,0.4)); stroke-width: 1.4; transition: stroke 0.15s, fill 0.15s; }\n"
	".wf-diag-n:hover .wf-diag-nr { stroke: var(--wf-accent-focus, #00a1c5); fill: var(--wf-surface-hover, #2d3748); }\n"
	".wf-diag-n[data-href]:hover { cursor: pointer; }\n"
	".wf-diag-nlabel { fill: var(--wf-fg-normal, #d3dae3); font-size: 11.5px; text-anchor: middle; }\n"
	".wf-diag-nbadge { fill: var(--wf-fg-inactive, #8b919d); font-size: 9px; text-anchor: middle; }\n"
	/* Node type accents */
	".wf-diag-nt-machine    { stroke: var(--wf-accent-focus, #00a1c5); }\n"
	".wf-diag-nt-service    { stroke: var(--wf-accent-neutral, #6d55aa); }\n"
	".wf-diag-nt-pipeline   { stroke: var(--wf-accent-positive, #00aa7f); }\n"
	".wf-diag-nt-process    { stroke: var(--wf-accent-hover, #0088a6); }\n"
	".wf-diag-nt-component  { stroke: var(--wf-border-color, rgba(0,161,197,0.4)); }\n"
	".wf-diag-nt-input      { stroke: var(--wf-color-warning, #c8a415); }\n"
	".wf-diag-nt-artifact   { stroke: var(--wf-color-positive, #00aa7f); }\n"
	".wf-diag-nt-evidence   { stroke: var(--wf-fg-inactive, #8b919d); }\n"
	".wf-diag-nt-phase      { stroke: var(--wf-accent-focus, #00a1c5); }\n"
	".wf-diag-nt-iteration  { stroke: var(--wf-accent-hover, #0088a6); }\n";
	return s;
}

String DiagramRenderer::GetClientScript()
{
	String js;
	js <<
	"(function() {\n"
	"  if (window.__wfDiagInit) return;\n"
	"  window.__wfDiagInit = true;\n"
	"  function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }\n"
	"  function setup(viewer) {\n"
	"    var vp = viewer.querySelector('.wf-diag-viewport');\n"
	"    var canvas = viewer.querySelector('.wf-diag-canvas');\n"
	"    var status = viewer.querySelector('.wf-diag-status');\n"
	"    var state = { s: 1, tx: 0, ty: 0, drag: null };\n"
	"    function apply() {\n"
	"      canvas.style.transform = 'translate(' + state.tx + 'px,' + state.ty + 'px) scale(' + state.s + ')';\n"
	"      if (status) status.textContent = Math.round(state.s * 100) + '%';\n"
	"    }\n"
	"    function zoomAt(factor, cx, cy) {\n"
	"      var old = state.s;\n"
	"      state.s = clamp(old * factor, 0.15, 8);\n"
	"      var k = state.s / old;\n"
	"      state.tx = cx - (cx - state.tx) * k;\n"
	"      state.ty = cy - (cy - state.ty) * k;\n"
	"      apply();\n"
	"    }\n"
	"    function fit() {\n"
	"      var nat = canvas.getBoundingClientRect();\n"
	"      var sw = nat.width / state.s, sh = nat.height / state.s;\n"
	"      state.s = clamp(Math.min(vp.clientWidth / sw, vp.clientHeight / sh), 0.15, 2.5);\n"
	"      state.tx = Math.max(0, (vp.clientWidth - sw * state.s) / 2);\n"
	"      state.ty = Math.max(0, (vp.clientHeight - sh * state.s) / 2);\n"
	"      apply();\n"
	"    }\n"
	"    vp.addEventListener('wheel', function(e) {\n"
	"      e.preventDefault();\n"
	"      var r = vp.getBoundingClientRect();\n"
	"      zoomAt(e.deltaY < 0 ? 1.12 : 1 / 1.12, e.clientX - r.left, e.clientY - r.top);\n"
	"    }, { passive: false });\n"
	"    vp.addEventListener('mousedown', function(e) {\n"
	"      if (e.target.closest('.wf-diag-n[data-href]') && e.detail > 1) return;\n"
	"      state.drag = { x: e.clientX, y: e.clientY, tx: state.tx, ty: state.ty, moved: 0 };\n"
	"      vp.classList.add('wf-diag-dragging');\n"
	"      e.preventDefault();\n"
	"    });\n"
	"    window.addEventListener('mousemove', function(e) {\n"
	"      if (!state.drag) return;\n"
	"      state.tx = state.drag.tx + (e.clientX - state.drag.x);\n"
	"      state.ty = state.drag.ty + (e.clientY - state.drag.y);\n"
	"      state.drag.moved += Math.abs(e.clientX - state.drag.x) + Math.abs(e.clientY - state.drag.y);\n"
	"      apply();\n"
	"    });\n"
	"    window.addEventListener('mouseup', function() { state.drag = null; vp.classList.remove('wf-diag-dragging'); });\n"
	"    canvas.addEventListener('click', function(e) {\n"
	"      var n = e.target.closest('.wf-diag-n[data-href]');\n"
	"      if (n && state.drag && state.drag.moved < 6) window.location.href = n.getAttribute('data-href');\n"
	"    });\n"
	"    vp.addEventListener('dblclick', function(e) { e.preventDefault(); fit(); });\n"
	"    viewer.querySelectorAll('.wf-diag-btn').forEach(function(btn) {\n"
	"      btn.addEventListener('click', function() {\n"
	"        var a = btn.getAttribute('data-diag-action');\n"
	"        if (a === 'zoom-in')  zoomAt(1.3, vp.clientWidth / 2, vp.clientHeight / 2);\n"
	"        else if (a === 'zoom-out') zoomAt(1 / 1.3, vp.clientWidth / 2, vp.clientHeight / 2);\n"
	"        else if (a === 'reset') { state.s = 1; state.tx = 0; state.ty = 0; apply(); }\n"
	"        else if (a === 'fit') fit();\n"
	"        else if (a === 'max') {\n"
	"          viewer.classList.toggle('wf-diag-max');\n"
	"          btn.textContent = viewer.classList.contains('wf-diag-max') ? 'Restore' : 'Maximize';\n"
	"          fit();\n"
	"        }\n"
	"      });\n"
	"    });\n"
	"    requestAnimationFrame(function() { fit(); });\n"
	"    apply();\n"
	"  }\n"
	"  document.querySelectorAll('.wf-diag').forEach(setup);\n"
	"}());\n";
	return js;
}

String DiagramRenderer::WrapSvgViewer(const String& svg, const String& viewer_id, const String& title)
{
	String id = viewer_id.IsEmpty() ? "wf-diag" : viewer_id;
	for(int i = 0; i < id.GetCount(); i++)
		if(!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= 'A' && id[i] <= 'Z') ||
		     (id[i] >= '0' && id[i] <= '9') || id[i] == '-' || id[i] == '_'))
			id.Set(i, '-');
	String h;
	h << "<div class=\"wf-diag\" id=\"" << EscapeXmlText(id) << "\" data-diag-title=\"" << EscapeXmlText(title) << "\">\n";
	h << "  <div class=\"wf-diag-toolbar\">\n"
	  << "    <button type=\"button\" class=\"wf-diag-btn\" data-diag-action=\"zoom-in\">+</button>\n"
	  << "    <button type=\"button\" class=\"wf-diag-btn\" data-diag-action=\"zoom-out\">&minus;</button>\n"
	  << "    <button type=\"button\" class=\"wf-diag-btn\" data-diag-action=\"fit\">Fit</button>\n"
	  << "    <button type=\"button\" class=\"wf-diag-btn\" data-diag-action=\"reset\">Reset</button>\n"
	  << "    <button type=\"button\" class=\"wf-diag-btn\" data-diag-action=\"max\">Maximize</button>\n"
	  << "    <span class=\"wf-diag-status\">100%</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"wf-diag-viewport\">\n"
	  << "    <div class=\"wf-diag-canvas\">\n" << svg << "\n    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";
	return h;
}

// ---------------------------------------------------------------------------
// Optional PlantUML passthrough (external binary, cached)
// ---------------------------------------------------------------------------

bool DiagramRenderer::FromPlantUml(const String& puml_source, const String& cache_key, String& svg_out, String& error)
{
	(void)puml_source;
	(void)cache_key;
	svg_out.Clear();
	error = "PlantUML rendering is disabled because external SVG output is not safely isolated or sanitized.";
	return false;
}

END_UPP_NAMESPACE
