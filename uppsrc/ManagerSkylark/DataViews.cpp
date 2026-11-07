#include "ManagerSkylark.h"
#include "ReviewDiscussion.h"

NAMESPACE_UPP

static Vector<const DataDocument*> GenericVisibleDocuments(const Vector<const DataDocument*>& input)
{
	Vector<const DataDocument*> out; for(const DataDocument *d:input) if(d&&!ReviewDiscussionService::IsPrivateKind(d->kind)) out.Add(d); return out;
}

static bool RejectDataCatalogUnavailable(Http& http, const CatalogGeneration& catalog)
{
	if(catalog.data_status != "load_error") return false;
	ValueMap response;
	response.Add("success", false);
	response.Add("error", "catalog_source_unavailable");
	response.Add("source_status", catalog.data_status);
	response.Add("catalog_generation", (int64)catalog.generation);
	ValueArray diagnostics;
	for(const String& diagnostic : catalog.diagnostics) diagnostics.Add(diagnostic);
	response.Add("diagnostics", diagnostics);
	http.ContentType("application/json");
	http.Response(503, "Source Unavailable");
	http << AsJSON(response, true);
	return true;
}

// ---------------------------------------------------------------------------
// Generic HTML helpers (view side)
// ---------------------------------------------------------------------------

// URL join for wildcard routes that carry ids containing "/".
static String JoinWildcards(Http& http, int from)
{
	String s;
	for(int i = from; i < 32; i++) {
		String w = (String)http[i];
		if(w.IsEmpty())
			break;
		if(!s.IsEmpty()) s << "/";
		s << w;
	}
	return s;
}

static String SafePath(const String& path)
{
	if(path.IsEmpty() || path[0] != '/' || path.StartsWith("//"))
		return String();
	String out = "/";
	Vector<String> parts = Split(path, '/');
	bool first = true;
	for(int i = 0; i < parts.GetCount(); i++) {
		if(parts[i].IsEmpty())
			continue;
		if(parts[i] == "." || parts[i] == "..")
			return String();
		if(!first)
			out << "/";
		out << UrlEncode(parts[i]);
		first = false;
	}
	return out;
}

static String DataDocPath(const String& id) { return SafePath("/data/doc/" + id); }
static String DataKindPath(const String& kind) { return SafePath("/data/k/" + kind); }
static String DataCollectionPath(const String& coll) { return SafePath("/data/c/" + coll); }

static String SafeBadge(const String& text)
{
	return "<span class=\"wf-badge\">" + EscapeHtml(text) + "</span>";
}

static String ValueAsPlainText(const Value& v, int depth = 0)
{
	if(depth > 12)
		return "[nested value omitted]";
	if(IsNull(v))
		return "";
	if(IsString(v))
		return (String)v;
	if(v.Is<ValueMap>()) {
		ValueMap m = v;
		String s;
		for(int i = 0; i < m.GetCount(); i++)
			s << (i ? "; " : "") << AsString(m.GetKey(i)) << "=" << ValueAsPlainText(m.GetValue(i), depth + 1);
		return s;
	}
	if(v.Is<ValueArray>()) {
		ValueArray a = v;
		String s;
		for(int i = 0; i < a.GetCount(); i++)
			s << (i ? ", " : "") << ValueAsPlainText(a[i], depth + 1);
		return s;
	}
	return AsString(v);
}

// ---------------------------------------------------------------------------
// Generic task-idea tier derivation (4-tier status model).
//
// Tiers are derived from document content when no explicit tier field exists:
//   research-ready  — strong on all key comparison dimensions and actionable
//   paper-backed    — supported by reviewed source material, needs refinement
//   interesting-risk— distinctive idea with open risk factors
//   avoid-for-now   — superseded, duplicated or rejected
// ---------------------------------------------------------------------------

static const char* gIdeaTiers[] = { "research-ready", "paper-backed", "interesting-risk", "avoid-for-now" };

static String IdeaTierForDoc(const DataDocument& d)
{
	const ValueMap& m = d.data;
	// Explicit tier field wins (any kind may self-describe its tier).
	for(const char* t : gIdeaTiers)
		if(MapGetString(m, "tier") == t)
			return t;
	// Rejected or clearly duplicated work is never actionable.
	if(d.status == "rejected")
		return "avoid-for-now";
	String dup = MapGetString(m, "duplicate_confidence");
	if(dup == "high" || dup == "duplicate")
		return "avoid-for-now";
	// Score-based derivation from the generic comparison form.
	Value score_value = MapGet(m, "comparison_scores");
	if(score_value.Is<ValueMap>()) {
		ValueMap scores = score_value;
		if(scores.GetCount() > 0) {
			Value nv = MapGet(scores, "novelty");
			Value cv = MapGet(scores, "competition_separation");
			Value pv = MapGet(scores, "paper_strength");
			Value ev = MapGet(scores, "evaluator_readiness");
			Value lv = MapGet(scores, "calibration_readiness");
			int novelty = IsNumber(nv) ? (int)nv : 0;
			int comp    = IsNumber(cv) ? (int)cv : 0;
			int paper   = IsNumber(pv) ? (int)pv : 0;
			int evalr   = IsNumber(ev) ? (int)ev : 0;
			int calib   = IsNumber(lv) ? (int)lv : 0;
			if(novelty >= 3 && comp >= 3 && evalr >= 3 && calib >= 3 && (d.status == "proposed" || d.status == "accepted"))
				return "research-ready";
			if(paper >= 3)
				return "paper-backed";
			if(novelty >= 3)
				return "interesting-risk";
			return "avoid-for-now";
		}
	}
	// No scores: fall back to lifecycle + evidence heuristics.
	if(d.status == "accepted")
		return "research-ready";
	if((MapGet(m, "papers").Is<ValueArray>() && MapGet(m, "papers").GetCount() > 0) || m.Find("paper_review") >= 0)
		return "paper-backed";
	return "interesting-risk";
}

static const char* IdeaTierColor(const String& tier)
{
	if(tier == "research-ready")   return "#00aa7f";
	if(tier == "paper-backed")     return "#00a1c5";
	if(tier == "interesting-risk") return "#c7861e";
	return "#c83771";  // avoid-for-now
}

static const char* ResearchStageColor(const String& stage)
{
	if(stage == "paper-screened")         return "#38bdf8";
	if(stage == "code-spike")             return "#00a1c5";
	if(stage == "deliverables-defined")   return "#6d55aa";
	if(stage == "comparison-ready")       return "#c7861e";
	if(stage == "proposal-ready")         return "#00aa7f";
	return "#8b919d";
}

static String ResearchStageBadge(const String& stage)
{
	return String("<span class=\"wf-badge\" style=\"background:rgba(139,145,157,0.12); color:")
	       << ResearchStageColor(stage) << ";\">" << EscapeHtml(stage.IsEmpty() ? String("unscreened") : stage) << "</span>";
}

// Recursive generic renderer for any JSON value found in a data document.
static String RenderValueHtml(const Value& v, int depth = 0)
{
	if(depth > 12)
		return "<span>[nested value omitted]</span>";
	if(IsNull(v) || v.IsVoid())
		return "<span style=\"color:var(--wf-fg-inactive,#8b919d);\">null</span>";
	if(v.Is<ValueMap>()) {
		ValueMap m = v;
		if(m.GetCount() == 0)
			return "<span style=\"color:var(--wf-fg-inactive,#8b919d);\">{}</span>";
		String h;
		h << "<table style=\"width:100%; border-collapse:collapse; font-size:0.82rem;\">";
		for(int i = 0; i < m.GetCount(); i++) {
			h << "<tr>"
			  << "<td style=\"padding:3px 10px 3px 0; color:var(--wf-accent-focus,#00a1c5); white-space:nowrap; vertical-align:top; width:1%;\">"
			  << EscapeHtml(AsString(m.GetKey(i))) << "</td>"
			  << "<td style=\"padding:3px 0; color:var(--wf-fg-normal,#d3dae3);\">"
			  << RenderValueHtml(m.GetValue(i), depth + 1) << "</td></tr>";
		}
		h << "</table>";
		return h;
	}
	if(v.Is<ValueArray>()) {
		ValueArray a = v;
		if(a.GetCount() == 0)
			return "<span style=\"color:var(--wf-fg-inactive,#8b919d);\">[]</span>";
		// Array of maps -> table with the union of keys as columns.
		bool all_maps = true;
		Index<String> cols;
		for(int i = 0; i < a.GetCount() && all_maps; i++)
			if(!a[i].Is<ValueMap>())
				all_maps = false;
		if(all_maps) {
			for(int i = 0; i < a.GetCount(); i++) {
				ValueMap m = a[i];
				for(int k = 0; k < m.GetCount(); k++) {
					try {
					cols.FindAdd(AsString(m.GetKey(k)));
					}
					catch(Exc e) {
						return String("<span style=\"color:#c83771;\">[column key conversion unavailable]</span>");
					}
				}
			}
			String h;
			h << "<table style=\"width:100%; border-collapse:collapse; font-size:0.82rem; margin:2px 0;\"><thead><tr>";
			for(int c = 0; c < cols.GetCount(); c++)
				h << "<th style=\"text-align:left; padding:3px 10px; color:var(--wf-fg-inactive,#8b919d); border-bottom:1px solid var(--wf-border-color,rgba(0,161,197,0.2));\">"
				  << EscapeHtml(cols[c]) << "</th>";
			h << "</tr></thead><tbody>";
			for(int i = 0; i < a.GetCount(); i++) {
				ValueMap m = a[i];
				h << "<tr>";
				for(int c = 0; c < cols.GetCount(); c++) {
					try {
					Value cell = MapGet(m, cols[c]);
						String cell_s;
						if(cell.IsVoid())
							cell_s = "<span style=\"color:var(--wf-fg-inactive,#8b919d);\">-</span>";
						else if(cols[c] == "status")
							cell_s = SafeBadge(ValueAsPlainText(cell));
						else {
							String t = ValueAsPlainText(cell);
						cell_s = t.GetCount() > 80
							       ? "<span title=\"" + EscapeHtml(t) + "\">" + EscapeHtml(t.Left(80)) + "&hellip;</span>"
							       : EscapeHtml(t);
						}
						h << "<td style=\"padding:3px 10px; border-bottom:1px solid rgba(139,145,157,0.12); vertical-align:top;\">" << cell_s << "</td>";
					}
					catch(Exc e) {
							h << "<td><span style=\"color:#c83771;\">[cell rendering unavailable]</span></td>";
					}
				}
				h << "</tr>";
			}
			h << "</tbody></table>";
			return h;
		}
		// Array of scalars -> inline chips.
		String h;
		for(int i = 0; i < a.GetCount(); i++) {
			try {
				h << (i ? ", " : "") << "<code style=\"background:rgba(139,145,157,0.12); padding:1px 5px; border-radius:4px; font-size:0.78rem;\">"
				  << EscapeHtml(ValueAsPlainText(a[i])) << "</code>";
			}
			catch(Exc e) {
				h << "<span style=\"color:#c83771;\">[value rendering unavailable]</span>";
			}
		}
		return h;
	}
	// Scalars
	String t = AsString(v);
	if(IsString(v) && (t.Find("\n") >= 0 || t.GetCount() > 120))
		return "<div style=\"white-space:pre-wrap; line-height:1.5; color:var(--wf-fg-normal,#d3dae3);\">"
		       + EscapeHtml(t) + "</div>";
	return EscapeHtml(t);
}

// ---------------------------------------------------------------------------
// Topology graph assembly from generic data documents
// ---------------------------------------------------------------------------

static void MergeGraph(TopologyGraph& g, const TopologyGraph& part)
{
	for(int i = 0; i < part.nodes.GetCount(); i++)
		if(!g.FindNode(part.nodes[i].id))
			g.nodes.Add(part.nodes[i]);
	for(int i = 0; i < part.edges.GetCount(); i++) {
		bool dup = false;
		for(int e = 0; e < g.edges.GetCount(); e++)
			if(g.edges[e].from == part.edges[i].from && g.edges[e].to == part.edges[i].to
			   && g.edges[e].role == part.edges[i].role) {
				dup = true;
				break;
			}
		if(!dup)
			g.edges.Add(part.edges[i]);
	}
	if(g.title.IsEmpty())
		g.title = part.title;
}

static void AddPlaceholderNode(TopologyGraph& g, const String& id, const String& type, const String& name)
{
	if(id.IsEmpty() || g.FindNode(id))
		return;
	TopologyNode& n = g.nodes.Add();
	n.id = id;
	n.name = name.IsEmpty() ? id : name;
	n.type = type.IsEmpty() ? "reference" : type;
}

// Assembles a TopologyGraph from topology-kind documents. Supports two generic
// shapes: (a) one node record per document (id / name / node_type / parents /
// edges), and (b) a document carrying full "nodes" + "edges" arrays.
static TopologyGraph BuildTopologyGraph(const DataStore& store, const Vector<const DataDocument*>& docs)
{
	TopologyGraph g;
	Index<String> known;

	for(int i = 0; i < docs.GetCount(); i++) {
		const DataDocument& d = *docs[i];
		ValueMap m = d.data;

		Value nv = MapGet(m, "nodes");
		if(nv.Is<ValueArray>()) {
			TopologyGraph part;
			part.FromJSON(m);
			MergeGraph(g, part);
			for(int k = 0; k < part.nodes.GetCount(); k++)
				known.FindAdd(part.nodes[k].id);
			continue;
		}

		String node_id = d.id;
		known.FindAdd(node_id);
		if(g.FindNode(node_id))
			continue;
		TopologyNode& n = g.nodes.Add();
		n.id = node_id;
		n.name = MapGet(m, "name", "").IsNull() ? d.title : ValueAsPlainText(MapGet(m, "name"));
		if(n.name.IsEmpty())
			n.name = node_id;
		n.type = ValueAsPlainText(MapGet(m, "node_type"));
		n.badge = d.status;
		n.url = DataDocPath(node_id);

		Value pv = MapGet(m, "parents");
		if(pv.Is<ValueArray>() && pv.GetCount() > 0)
			n.parent = ValueAsPlainText(pv[0]);
	}

	// Edges: typed refs {kind, id, role} plus containment from parents.
	for(int i = 0; i < docs.GetCount(); i++) {
		const DataDocument& d = *docs[i];
		ValueMap m = d.data;
		if(MapGet(m, "nodes").Is<ValueArray>())
			continue;

		Value ev = MapGet(m, "edges");
		if(ev.Is<ValueArray>())
			for(int k = 0; k < ev.GetCount(); k++) {
				if(!ev[k].Is<ValueMap>())
					continue;
				ValueMap ref = ev[k];
				String ref_id = ValueAsPlainText(MapGet(ref, "id"));
				String ref_kind = ValueAsPlainText(MapGet(ref, "kind"));
				String role = ValueAsPlainText(MapGet(ref, "role"));
				if(ref_id.IsEmpty())
					continue;
				AddPlaceholderNode(g, ref_id, ref_kind, ref_id.Mid(max(0, ref_id.ReverseFind("/") + 1)));
				g.AddEdge(d.id, ref_id, role, role);
			}

		Value pv = MapGet(m, "parents");
		if(pv.Is<ValueArray>())
			for(int k = 0; k < pv.GetCount(); k++) {
				String pid = ValueAsPlainText(pv[k]);
				if(pid.IsEmpty() || pid == d.id)
					continue;
				AddPlaceholderNode(g, pid, "component", pid.Mid(max(0, pid.ReverseFind("/") + 1)));
				g.AddEdge(pid, d.id, "contains", "contains");
			}
	}

	// Deduplicate containment edges already implied by parent links.
	for(int i = g.nodes.GetCount() - 1; i >= 0; i--) {
		const String& p = g.nodes[i].parent;
		if(p.IsEmpty())
			continue;
		for(int e = g.edges.GetCount() - 1; e >= 0; e--)
			if(g.edges[e].from == p && g.edges[e].to == g.nodes[i].id && g.edges[e].role == "contains")
				g.edges.Remove(e);
	}
	return g;
}

// Collects all topology-kind documents whose id belongs to the given scope
// prefix (e.g. "topology/<scope>/...").
static Vector<const DataDocument*> GetTopologyDocs(const DataStore& store, const String& scope = "")
{
	Vector<const DataDocument*> res;
	const Vector<DataDocument>& docs = store.GetDocuments();
	String kind_scope = scope.IsEmpty() ? "" : "topology/" + scope + "/";
	for(int i = 0; i < docs.GetCount(); i++) {
		if(docs[i].kind != "topology")
			continue;
		if(kind_scope.IsEmpty() || docs[i].id.StartsWith(kind_scope))
			res.Add(&docs[i]);
	}
	return res;
}

static Vector<const DataDocument*> GetTopologyDocs(const ManagerSkylarkApp& app,
	                                                const CatalogGeneration& catalog,
	                                                const String& scope = "")
{
	Vector<const DataDocument*> all = app.QueryCatalogDocuments(catalog, "topology");
	Vector<const DataDocument*> result;
	String prefix = scope.IsEmpty() ? "" : "topology/" + scope + "/";
	for(const DataDocument *document : all)
		if(prefix.IsEmpty() || document->id.StartsWith(prefix)) result.Add(document);
	return result;
}

static bool HasInvalidTopologyDocs(const DataStore& store, const String& scope)
{
	String prefix = "topology/" + scope + "/";
	for(const DataDocument& document : store.GetDocuments())
		if(document.kind == "topology" && document.id.StartsWith(prefix) && !document.valid)
			return true;
	return false;
}

// ---------------------------------------------------------------------------
// Left sidebar tree
// ---------------------------------------------------------------------------

TreeModel ManagerSkylarkApp::BuildDataTree(const DataStore& store, const String& active_id)
{
	TreeModel tree;
	TreeNode& root = tree.Add("data-root", "Data Web", "/data", "database")
	                     .Icon("database", "database")
	                     .Expanded(true)
	                     .Selected(active_id == "data-root");

	Vector<String> kinds = store.GetKinds();
	TreeNode& kinds_node = root.AddChild("data-kinds", "Kinds", "/data", "layers")
	                           .Icon("layers", "layers")
	                           .Expanded(true);
	for(int i = 0; i < kinds.GetCount(); i++) {
		String kind = kinds[i];
		if(ReviewDiscussionService::IsPrivateKind(kind)) continue;
		bool act = (active_id == "k/" + kind);
		kinds_node.AddChild("kind-" + kind, kind, DataKindPath(kind), "tag")
		          .Badge(AsString(store.GetKindCount(kind)), "cyan")
		          .Selected(act);
	}

	Index<String> visible_collections; for(const DataDocument& d:store.GetDocuments()) if(!ReviewDiscussionService::IsPrivateKind(d.kind)) visible_collections.FindAdd(d.collection);
	Vector<String> colls; for(const String& c:store.GetCollections()) if(visible_collections.Find(c)>=0) colls.Add(c);
	if(colls.GetCount() > 1) {
		TreeNode& colls_node = root.AddChild("data-collections", "Collections", "/data", "folder")
		                              .Icon("folder", "folder-open")
		                              .Expanded(true);
		for(int i = 0; i < colls.GetCount(); i++) {
			bool act = (active_id == "c/" + colls[i]);
			colls_node.AddChild("coll-" + colls[i], colls[i], DataCollectionPath(colls[i]), "folder")
			          .Selected(act);
		}
	}

	Vector<const DataDocument*> topo = GetTopologyDocs(store);
	if(!topo.IsEmpty()) {
		Index<String> scopes;
		for(int i = 0; i < topo.GetCount(); i++) {
			Vector<String> parts = Split(topo[i]->id, '/');
			if(parts.GetCount() >= 2)
				scopes.FindAdd(parts[1]);
		}
		TreeNode& topo_node = root.AddChild("data-topology", "Topology Diagrams", "/data/diagrams", "cpu")
		                           .Icon("cpu", "cpu")
		                           .Expanded(true);
		for(int i = 0; i < scopes.GetCount(); i++)
			topo_node.AddChild("topo-" + scopes[i], scopes[i], SafePath("/data/diagram/scope/" + scopes[i]), "cpu")
			         .Selected(active_id == "diagram/" + scopes[i]);
	}
	return tree;
}

// ---------------------------------------------------------------------------
// Web views: generic data explorer
// ---------------------------------------------------------------------------

SKYLARK(DataIndexPage, "data")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const DataStore& store = catalog->data;
	Vector<const DataDocument*> all_docs = GenericVisibleDocuments(app.QueryCatalogDocuments(*catalog));

	String b;
	b << "<h1 style=\"margin-top:0;\">" << VividIcon::Get("database", 22) << " Data Explorer</h1>\n";
	b << "<p style=\"color:var(--wf-fg-inactive,#8b919d);\">Generic discovery of structured JSON documents under the workspace root. "
	     "Kinds are self-described by schema cards found in the scanned data.</p>\n";

	// Quick links to specialized generic views
	b << "<div style=\"display:flex; gap:0.75rem; margin-bottom:1.25rem;\">"
	  << "<a href=\"/data/papers\" class=\"btn btn-primary\">Scientific Paper Mining</a>"
	  << "<a href=\"/data/ideas\" class=\"btn\">Task Ideas</a>"
	  << "</div>\n";

	// Stat row
	Vector<String> visible_kinds; for(const String& k:store.GetKinds()) if(!ReviewDiscussionService::IsPrivateKind(k)) visible_kinds.Add(k);
	Index<String> visible_collection_set; for(const DataDocument *d:all_docs) visible_collection_set.FindAdd(d->collection);
	int kinds_n = visible_kinds.GetCount();
	int colls_n = visible_collection_set.GetCount();
	int invalid_count = 0;
	for(const DataDocument *document : all_docs) if(!document->valid) invalid_count++;
	b << "<div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(170px, 1fr)); gap:1rem; margin-bottom:1.25rem;\">\n";
	struct Stat { const char* label; String value; const char* color; };
	Stat stats[] = {
		{ "Documents",   AsString(all_docs.GetCount()),               "#00a1c5" },
		{ "Kinds",       AsString(kinds_n),                         "#00aa7f" },
		{ "Collections", AsString(colls_n),                         "#6d55aa" },
		{ "Generic records", AsString(all_docs.GetCount()),          "#38bdf8" },
		{ "Source errors", AsString(store.GetSourceErrorCount()),    "#c83771" },
		{ "Schema issues", AsString(invalid_count),                 "#c83771" },
	};
	for(int i = 0; i < (int)__countof(stats); i++)
		b << "<div class=\"stat-card\" style=\"border-left:4px solid " << stats[i].color << "; margin:0;\">"
		  << "<div class=\"stat-lbl\">" << stats[i].label << "</div>"
		  << "<div class=\"stat-val\" style=\"color:" << stats[i].color << ";\">" << stats[i].value << "</div></div>\n";
	b << "</div>\n";

	// Kinds table with schema cards
	b << "<div class=\"card\"><div class=\"card-header\"><span>Document kinds &amp; schema cards</span></div>\n";
	b << "<table><thead><tr><th>Kind</th><th>Count</th><th>Required fields</th><th>Schema source</th></tr></thead><tbody>\n";
	Vector<String> kinds; for(const String& k:visible_kinds) kinds.Add(k);
	for(int i = 0; i < kinds.GetCount(); i++) {
		const SchemaCard* sc = store.FindSchemaCard(kinds[i]);
		String req = "id, kind";
		if(sc) {
			req = "";
			for(int r = 0; r < sc->required.GetCount(); r++)
				req << (r ? ", " : "") << sc->required[r];
		}
		String src = sc ? (sc->collection == "builtin" ? "built-in default" : sc->collection) : "&mdash;";
		b << "<tr><td><a href=\"" << DataKindPath(kinds[i]) << "\"><code>" << EscapeHtml(kinds[i]) << "</code></a></td>"
		  << "<td>" << [&]() { int n = 0; for(const DataDocument *d : all_docs) if(d->kind == kinds[i]) n++; return n; }() << "</td>"
		  << "<td><code>" << EscapeHtml(req) << "</code></td>"
		  << "<td>" << EscapeHtml(src) << "</td></tr>\n";
	}
	b << "</tbody></table></div>\n";

	// Collections table
	Vector<String> colls; for(const String& c:store.GetCollections()) if(visible_collection_set.Find(c)>=0) colls.Add(c);
	if(colls.GetCount() > 0) {
		b << "<div class=\"card\"><div class=\"card-header\"><span>Collections</span></div>\n";
		b << "<table><thead><tr><th>Collection</th><th>Documents</th></tr></thead><tbody>\n";
		for(int i = 0; i < colls.GetCount(); i++) {
			int count = 0;
			for(const DataDocument *d : all_docs) if(d->collection == colls[i]) count++;
			b << "<tr><td><a href=\"" << DataCollectionPath(colls[i]) << "\">" << EscapeHtml(colls[i]) << "</a></td>"
			  << "<td>" << count << "</td></tr>\n";
		}
		b << "</tbody></table></div>\n";
	}

	// Data quality panel
	Vector<const DataDocument*> invalid;
	for(const DataDocument *d : all_docs) if(!d->valid) invalid.Add(d);
	if(!invalid.IsEmpty()) {
		b << "<div class=\"card\" style=\"border-left:4px solid #c83771;\"><div class=\"card-header\"><span>Schema validation issues</span></div>\n";
		b << "<table><thead><tr><th>Document</th><th>Missing required fields</th></tr></thead><tbody>\n";
		for(int i = 0; i < invalid.GetCount(); i++)
			b << "<tr><td><a href=\"" << DataDocPath(invalid[i]->id) << "\"><code>" << EscapeHtml(invalid[i]->id) << "</code></a></td>"
			  << "<td style=\"color:#f87171;\">" << EscapeHtml(invalid[i]->invalid_reason) << "</td></tr>\n";
		b << "</tbody></table></div>\n";
	}

	PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Data Explorer", "data", b, ManagerSkylarkApp::BuildDataTree(store, "data-root"));
	http.ContentType("text/html; charset=utf-8") << pb.Build();
}

// ---------------------------------------------------------------------------
// Scientific paper mining view: the pipeline from mined source material to
// screened candidates. Generic over any kind that carries research-stage or
// paper-evidence fields.
// ---------------------------------------------------------------------------
SKYLARK(DataPapersPage, "data/papers")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const DataStore& store = catalog->data;
	String b;
	b << "<h1 style=\"margin-top:0;\">" << VividIcon::Get("cpu", 22) << " Scientific Paper Mining</h1>\n";
	b << "<p style=\"color:var(--wf-fg-inactive,#8b919d);\">Pipeline view of mined research material: papers and analyses "
	     "are screened, spiked, and refined until they graduate into task candidates. "
	     "Stages are read generically from the research-stage field.</p>\n";

	// Pipeline funnel across the standard research stages.
	static const char* stages[] = { "paper-screened", "code-spike", "deliverables-defined", "comparison-ready", "proposal-ready" };
	int stage_counts[5] = { 0, 0, 0, 0, 0 };
	Vector<const DataDocument*> stage_docs[5];
	Vector<const DataDocument*> all = app.QueryCatalogDocuments(*catalog, "paper");
	Vector<const DataDocument*> analyses = app.QueryCatalogDocuments(*catalog, "paper-analysis");
	for(const DataDocument *document : analyses) all.Add(document);
	int unscreened = 0;
	for(int i = 0; i < all.GetCount(); i++) {
		const DataDocument& d = *all[i];
		bool mining_kind = d.kind == "paper" || d.kind == "paper-analysis"
		                || d.kind == "research-candidate" || d.kind == "task-proposal";
		if(!mining_kind)
			continue;
		String stage = MapGetString(d.data, "research_stage");
		int si = -1;
		for(int s = 0; s < 5; s++)
			if(stage == stages[s])
				si = s;
		if(si < 0) {
			unscreened++;
			continue;
		}
		stage_counts[si]++;
		stage_docs[si].Add(&d);
	}

	// Funnel bars
	int funnel_max = 1;
	for(int s = 0; s < 5; s++)
		funnel_max = max(funnel_max, stage_counts[s]);
	b << "<div class=\"card\"><div class=\"card-header\"><span>Mining pipeline</span>"
	     "<a href=\"/data/ideas\">Task ideas &rarr;</a></div>\n";
	b << "<div style=\"display:grid; gap:0.5rem;\">\n";
	for(int s = 0; s < 5; s++) {
		int w = 100 * stage_counts[s] / funnel_max;
		b << "<div style=\"display:flex; align-items:center; gap:0.75rem;\">"
		  << "<div style=\"width:160px; font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">" << stages[s] << "</div>"
		  << "<div style=\"flex:1; height:22px; background:var(--wf-bg-alt,#1c202f); border-radius:4px; overflow:hidden;\">"
		  << "<div style=\"width:" << w << "%; height:100%; background:" << ResearchStageColor(stages[s]) << "; opacity:0.85;\"></div></div>"
		  << "<div style=\"width:40px; text-align:right; font-weight:700; color:" << ResearchStageColor(stages[s]) << ";\">"
		  << stage_counts[s] << "</div></div>\n";
	}
	b << "</div>\n";
	if(unscreened > 0)
		b << "<p style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#8b919d); margin-top:0.5rem;\">" << unscreened
		  << " mining-kind documents without a research stage.</p>\n";
	b << "</div>\n";

	// Per-stage document tables (newest stage first)
	for(int s = 4; s >= 0; s--) {
		if(stage_docs[s].IsEmpty())
			continue;
		b << "<div class=\"card\"><div class=\"card-header\"><span>" << stages[s]
		  << " <span class=\"wf-badge wf-badge-neutral\">" << stage_docs[s].GetCount() << "</span></span></div>\n";
		b << "<table><thead><tr><th>Document</th><th>Kind</th><th>Title</th><th>Status</th></tr></thead><tbody>\n";
		for(int i = 0; i < stage_docs[s].GetCount(); i++) {
			const DataDocument* d = stage_docs[s][i];
			b << "<tr>"
			  << "<td><a href=\"" << DataDocPath(d->id) << "\"><code>" << EscapeHtml(d->id) << "</code></a></td>"
			  << "<td><code>" << EscapeHtml(d->kind) << "</code></td>"
			  << "<td>" << EscapeHtml(d->title) << "</td>"
			  << "<td>" << SafeBadge(d->status) << "</td></tr>\n";
		}
		b << "</tbody></table></div>\n";
	}

	PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Scientific Paper Mining", "data", b, ManagerSkylarkApp::BuildDataTree(store, "data-papers"));
	http.ContentType("text/html; charset=utf-8") << pb.Build();
}

// ---------------------------------------------------------------------------
// Task ideas view: the proposal pool with the 4-tier generic status model
// (research-ready / paper-backed / interesting-risk / avoid-for-now).
// ---------------------------------------------------------------------------
SKYLARK(DataIdeasPage, "data/ideas")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const DataStore& store = catalog->data;

	String b;
	b << "<h1 style=\"margin-top:0;\">" << VividIcon::Get("database", 22) << " Task Ideas</h1>\n";
	b << "<p style=\"color:var(--wf-fg-inactive,#8b919d);\">Derived task ideas and research proposals with a 4-tier status model. "
	     "Tiers are derived generically from comparison scores and lifecycle status; explicit tier fields win.</p>\n";

	// Collect idea documents
	Vector<const DataDocument*> ideas;
	Vector<const DataDocument*> all = app.QueryCatalogDocuments(*catalog);
	for(int i = 0; i < all.GetCount(); i++)
		if(all[i]->kind == "task-proposal" || all[i]->kind == "research-candidate" || all[i]->kind == "task")
			ideas.Add(all[i]);

	// Tier summary cards
	int tier_counts[4] = { 0, 0, 0, 0 };
	Vector<const DataDocument*> tier_docs[4];
	for(int i = 0; i < ideas.GetCount(); i++) {
		String tier = IdeaTierForDoc(*ideas[i]);
		for(int t = 0; t < 4; t++)
			if(tier == gIdeaTiers[t]) {
				tier_counts[t]++;
				tier_docs[t].Add(ideas[i]);
			}
	}
	b << "<div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(180px, 1fr)); gap:1rem; margin-bottom:1.25rem;\">\n";
	for(int t = 0; t < 4; t++) {
		b << "<div class=\"stat-card\" style=\"border-left:4px solid " << IdeaTierColor(gIdeaTiers[t]) << "; margin:0;\">"
		  << "<div class=\"stat-lbl\">" << gIdeaTiers[t] << "</div>"
		  << "<div class=\"stat-val\" style=\"color:" << IdeaTierColor(gIdeaTiers[t]) << ";\">" << tier_counts[t] << "</div></div>\n";
	}
	b << "</div>\n";

	// Ideas table sorted by tier priority then id
	b << "<div class=\"card\"><div class=\"card-header\"><span>Idea pool</span>"
	     "<a href=\"/data/papers\">&larr; Paper mining</a></div>\n";
	b << "<table><thead><tr><th>Document</th><th>Kind</th><th>Title</th><th>Tier</th><th>Stage</th><th>Status</th><th>Scores</th></tr></thead><tbody>\n";
	for(int t = 0; t < 4; t++) {
		for(int i = 0; i < tier_docs[t].GetCount(); i++) {
			const DataDocument* d = tier_docs[t][i];
			// Score summary from the generic comparison form
			Value scores_value = MapGet(d->data, "comparison_scores");
			String score_s;
			if(scores_value.Is<ValueMap>()) {
				ValueMap scores = scores_value;
				if(scores.GetCount() > 0) {
					for(int k = 0; k < scores.GetCount(); k++) {
						if(k) score_s << " &middot; ";
						score_s << EscapeHtml(AsString(scores.GetKey(k)).Left(3)) << ":" << EscapeHtml(ValueAsPlainText(scores.GetValue(k)));
					}
				}
			}
			String stage = MapGetString(d->data, "research_stage");
			b << "<tr>"
			  << "<td><a href=\"" << DataDocPath(d->id) << "\"><code>" << EscapeHtml(d->id) << "</code></a></td>"
			  << "<td><code>" << EscapeHtml(d->kind) << "</code></td>"
			  << "<td>" << EscapeHtml(d->title) << "</td>"
			  << "<td><span class=\"wf-badge\" style=\"background:rgba(139,145,157,0.12); color:" << IdeaTierColor(gIdeaTiers[t]) << ";\">"
			  << gIdeaTiers[t] << "</span></td>"
			  << "<td>" << ResearchStageBadge(stage) << "</td>"
			  << "<td>" << SafeBadge(d->status) << "</td>"
			  << "<td style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d); white-space:nowrap;\">" << score_s << "</td></tr>\n";
		}
	}
	if(ideas.IsEmpty())
		b << "<tr><td colspan=\"7\" style=\"color:var(--wf-fg-inactive,#8b919d);\">No task idea documents found in the scanned data.</td></tr>\n";
	b << "</tbody></table></div>\n";

	PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Task Ideas", "data", b, ManagerSkylarkApp::BuildDataTree(store, "data-ideas"));
	http.ContentType("text/html; charset=utf-8") << pb.Build();
}

SKYLARK(DataKindPage, "data/k/*")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const DataStore& store = catalog->data;
	String kind = http[0];
	if(ReviewDiscussionService::IsPrivateKind(kind)) { http.Response(404,"Kind Not Found"); http.ContentType("text/html; charset=utf-8")<<ManagerSkylarkApp::RenderHtmlPage("Kind unavailable","data","<h1>Kind unavailable</h1>",&http); return; }
	Vector<const DataDocument*> docs = GenericVisibleDocuments(app.QueryCatalogDocuments(*catalog, kind));

	String b;
	b << "<h1 style=\"margin-top:0;\">" << VividIcon::Get("layers", 22) << " Kind: " << EscapeHtml(kind) << "</h1>\n";
	const SchemaCard* sc = store.FindSchemaCard(kind);
	if(sc) {
		b << "<div class=\"card\" style=\"padding:0.75rem 1.25rem; font-size:0.85rem;\">";
		b << "<strong>Required:</strong> ";
		for(int r = 0; r < sc->required.GetCount(); r++)
			b << "<code>" << EscapeHtml(sc->required[r]) << "</code> ";
		if(!sc->id_pattern.IsEmpty())
			b << " &nbsp;<strong>Id pattern:</strong> <code>" << EscapeHtml(sc->id_pattern) << "</code>";
		b << "</div>\n";
	}
	b << "<div class=\"card\"><div class=\"card-header\"><span>" << docs.GetCount() << " documents of kind \"" << EscapeHtml(kind) << "\"</span>"
	  << "<a href=\"/data\" class=\"btn\">Back</a></div>\n";
	b << "<table><thead><tr><th>Id</th><th>Title</th><th>Status</th><th>Collection</th><th>Scope</th><th>Schema</th></tr></thead><tbody>\n";
	for(int i = 0; i < docs.GetCount(); i++) {
		const DataDocument& d = *docs[i];
		b << "<tr>"
		  << "<td><a href=\"" << DataDocPath(d.id) << "\"><code>" << EscapeHtml(d.id) << "</code></a></td>"
		  << "<td>" << (d.title.IsEmpty() ? "&mdash;" : EscapeHtml(d.title)) << "</td>"
		  << "<td>" << SafeBadge(d.status) << "</td>"
		  << "<td>" << EscapeHtml(d.collection) << "</td>"
		  << "<td>" << (d.scope.IsEmpty() ? "&mdash;" : EscapeHtml(d.scope)) << "</td>"
		  << "<td>" << (d.valid ? "<span class=\"wf-badge wf-badge-emerald\">valid</span>"
	                        : "<span class=\"wf-badge wf-badge-crimson\">" + EscapeHtml(d.invalid_reason) + "</span>")
		  << "</td></tr>\n";
	}
	b << "</tbody></table></div>\n";

	PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Data: " + kind, "data", b, ManagerSkylarkApp::BuildDataTree(store, "k/" + kind));
	http.ContentType("text/html; charset=utf-8") << pb.Build();
}

SKYLARK(DataCollectionPage, "data/c/*")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const DataStore& store = catalog->data;
	String coll = http[0];
	Vector<const DataDocument*> docs = GenericVisibleDocuments(app.QueryCatalogDocuments(*catalog, String(), coll));

	String b;
	b << "<h1 style=\"margin-top:0;\">" << VividIcon::Get("folder", 22) << " Collection: " << EscapeHtml(coll) << "</h1>\n";
	b << "<div class=\"card\"><div class=\"card-header\"><span>" << docs.GetCount() << " documents</span><a href=\"/data\" class=\"btn\">Back</a></div>\n";
	b << "<table><thead><tr><th>Id</th><th>Kind</th><th>Title</th><th>Status</th></tr></thead><tbody>\n";
	for(int i = 0; i < docs.GetCount(); i++) {
		const DataDocument& d = *docs[i];
		b << "<tr><td><a href=\"" << DataDocPath(d.id) << "\"><code>" << EscapeHtml(d.id) << "</code></a></td>"
		  << "<td><a href=\"" << DataKindPath(d.kind) << "\">" << EscapeHtml(d.kind) << "</a></td>"
		  << "<td>" << (d.title.IsEmpty() ? "&mdash;" : EscapeHtml(d.title)) << "</td>"
		  << "<td>" << SafeBadge(d.status) << "</td></tr>\n";
	}
	b << "</tbody></table></div>\n";

	PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Data: " + coll, "data", b, ManagerSkylarkApp::BuildDataTree(store, "c/" + coll));
	http.ContentType("text/html; charset=utf-8") << pb.Build();
}

// Generic record view: renders every field of the document payload.
SKYLARK(DataDocPage, "data/doc/**")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const DataStore& store = catalog->data;
	String doc_id = JoinWildcards(http, 0);
	CatalogRecordRead read = ReadCatalogRecord(*catalog, doc_id, true);
	String status = read.status;
		if(status == "ok" && MapGetString(read.response, "record_type") != "data_document") status = "wrong_record_type";
	String canonical_id = status == "ok" ? MapGetString(read.response, "qualified_id") : String();

	String b;
	if(status != "ok") {
		int code = status == "unavailable" ? 503 : status == "ambiguous" ? 409 : status == "unsafe_metadata" ? 409 : 404;
		http.Response(code, status == "ambiguous" ? "Ambiguous Record ID" : "Record Lookup Failed");
		b << "<h1 style=\"margin-top:0;\">Record lookup failed</h1>\n"
		  << "<div class=\"card\"><p>Status: <code>" << EscapeHtml(status) << "</code></p></div>\n";
	}
	else {
		const DataDocument* resolved = catalog->FindDocument(canonical_id);
		if(!resolved) {
			http.Response(409, "Catalog Identity Conflict");
			b << "<h1>Record identity conflict</h1>";
		}
		else {
		const DataDocument& d = *resolved;
		b << "<h1 style=\"margin-top:0;\">" << EscapeHtml(d.title.IsEmpty() ? d.id : d.title) << "</h1>\n";
		b << "<p><code>" << EscapeHtml(d.qualified_id) << "</code></p>\n";
		b << "<div style=\"display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin-bottom:1rem;\">"
		  << SafeBadge(d.kind)
		  << SafeBadge(d.status)
		  << "<code style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">" << EscapeHtml(d.rel_path) << "</code>"
		  << "</div>\n";

		if(!d.valid) {
			b << "<div class=\"card\" style=\"border-left:4px solid #c83771; padding:0.75rem 1.25rem; font-size:0.85rem;\">"
			  << "<strong>Schema validation:</strong> missing required fields &mdash; " << EscapeHtml(d.invalid_reason) << "</div>\n";
		}

		const SchemaCard* sc = store.FindSchemaCard(d.kind);
		if(sc && !sc->id_pattern.IsEmpty())
			b << "<p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">Kind id pattern: <code>" << EscapeHtml(sc->id_pattern) << "</code></p>\n";

		// Full generic payload
		b << "<div class=\"card\"><div class=\"card-header\"><span>Document payload</span>"
		  << "<a href=\"" << DataKindPath(d.kind) << "\" class=\"btn\">All " << EscapeHtml(d.kind) << "s</a></div>\n";
		b << "<table style=\"width:100%;\"><tbody>\n";
		ValueMap m = d.data;
		for(int i = 0; i < m.GetCount(); i++) {
			String key = m.GetKey(i);
			Value v = m.GetValue(i);
			b << "<tr>"
			  << "<td style=\"padding:6px 14px 6px 0; width:1%; white-space:nowrap; color:var(--wf-accent-focus,#00a1c5); font-size:0.84rem; vertical-align:top;\">"
			  << EscapeHtml(key) << "</td>"
			  << "<td style=\"padding:6px 0; vertical-align:top;\">";
			try {
				b << (key == "status" ? SafeBadge(ValueAsPlainText(v)) : RenderValueHtml(v));
			}
			catch(Exc e) {
				b << "<span style=\"color:#c83771;\">[field rendering unavailable]</span>";
			}
			b << "</td></tr>\n";
		}
		b << "</tbody></table></div>\n";

		// Raw JSON source
		String raw = AsJSON(d.data, true);
		b << "<details class=\"card\" style=\"padding:0.75rem 1.25rem;\"><summary style=\"cursor:pointer; font-weight:600;\">Raw JSON</summary>"
		  << "<pre style=\"white-space:pre-wrap; font-size:0.75rem; margin:8px 0 0 0; color:var(--wf-fg-inactive,#8b919d);\">"
		  << EscapeHtml(raw) << "</pre></details>\n";
		}
	}

	PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome(status=="ok"?"Document: "+canonical_id:"Document lookup", "data", b, ManagerSkylarkApp::BuildDataTree(store, "doc/" + canonical_id));
	http.ContentType("text/html; charset=utf-8") << pb.Build();
}

// ---------------------------------------------------------------------------
// Diagram views: architectural topology maps
// ---------------------------------------------------------------------------

static String DiagramPageChrome(const String& title, const String& body, const String& svg_viewer)
{
	String b = body;
	if(!svg_viewer.IsEmpty())
		b << svg_viewer;
	return b;
}

SKYLARK(DataDiagramsPage, "data/diagrams")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const DataStore& store = catalog->data;

	String b;
	b << "<h1 style=\"margin-top:0;\">" << VividIcon::Get("cpu", 22) << " Diagrams</h1>\n";
	b << "<p style=\"color:var(--wf-fg-inactive,#8b919d);\">Interactive topology maps assembled from generic data documents, plus any PlantUML diagram sources found under the scan root.</p>\n";

	Vector<const DataDocument*> topo = GetTopologyDocs(app, *catalog);
	b << "<div class=\"card\"><div class=\"card-header\"><span>Topology scopes</span></div>\n";
	if(topo.IsEmpty())
		b << "<p style=\"color:var(--wf-fg-inactive,#8b919d);\">No <code>kind: \"topology\"</code> documents discovered.</p>\n";
	else {
		Index<String> scopes;
		for(int i = 0; i < topo.GetCount(); i++) {
			Vector<String> parts = Split(topo[i]->id, '/');
			if(parts.GetCount() >= 2)
				scopes.FindAdd(parts[1]);
		}
		b << "<table><thead><tr><th>Scope</th><th>Nodes</th></tr></thead><tbody>\n";
		for(int i = 0; i < scopes.GetCount(); i++) {
			Vector<const DataDocument*> scope_docs = GetTopologyDocs(app, *catalog, scopes[i]);
			b << "<tr><td><a href=\"" << SafePath("/data/diagram/scope/" + scopes[i]) << "\"><strong>" << EscapeHtml(scopes[i]) << "</strong></a></td>"
			  << "<td>" << scope_docs.GetCount() << "</td></tr>\n";
		}
		b << "</tbody></table>\n";
	}
	b << "</div>\n";

	// PlantUML sources discovered by the generic scan
	const Vector<String>& pumls = store.GetDiagramSources();
	b << "<div class=\"card\"><div class=\"card-header\"><span>PlantUML sources</span><span class=\"wf-badge wf-badge-cyan\">" << pumls.GetCount() << "</span></div>\n";
	if(pumls.IsEmpty())
		b << "<p style=\"color:var(--wf-fg-inactive,#8b919d);\">No *.puml sources under the scan root.</p>\n";
	else {
		b << "<table><tbody>\n";
		for(int i = 0; i < pumls.GetCount(); i++)
			b << "<tr><td><a href=\"" << SafePath("/data/puml/" + pumls[i]) << "\"><code>" << EscapeHtml(pumls[i]) << "</code></a></td></tr>\n";
		b << "</tbody></table>\n";
	}
	b << "</div>\n";

	PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Diagrams", "data", b, ManagerSkylarkApp::BuildDataTree(store, "data-topology"));
	http.ContentType("text/html; charset=utf-8") << pb.Build();
}

SKYLARK(DataDiagramViewPage, "data/diagram/**")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const DataStore& store = catalog->data;
	String target = JoinWildcards(http, 0);
	Vector<const DataDocument*> docs;
	String scope;
	String canonical_document_id;
	String error_status;
	bool explicit_scope = target.StartsWith("scope/");
	if(explicit_scope) {
		scope = target.Mid(6);
		if(catalog->status == "load_error") error_status = "unavailable";
		else if(catalog->data_status != "loaded") error_status = "incomplete_scope";
		else if(HasInvalidTopologyDocs(store, scope)) error_status = "incomplete_scope";
		else docs = GetTopologyDocs(app, *catalog, scope);
	}
	else {
		CatalogRecordRead read = ReadCatalogRecord(*catalog, target, true);
		if(read.status == "ok") {
			if(MapGetString(read.response, "record_type") != "data_document") error_status = "wrong_kind";
			else {
				String canonical_id = MapGetString(read.response, "qualified_id");
				const DataDocument* document = catalog->FindDocument(canonical_id);
				if(!document) error_status = "ambiguous";
				else if(document->kind != "topology") error_status = "wrong_kind";
				else if(!document->valid) error_status = "invalid_record";
				else { canonical_document_id = canonical_id; docs.Add(document); }
			}
		}
		else if(read.status == "missing") {
			scope = target;
			if(!catalog->IsComplete()) error_status = "unavailable";
			else if(catalog->data_status != "loaded") error_status = "incomplete_scope";
			else if(HasInvalidTopologyDocs(store, scope)) error_status = "incomplete_scope";
			else docs = GetTopologyDocs(app, *catalog, scope);
		}
		else error_status = read.status;
	}

	String display_target = scope.IsEmpty() ? target : "scope/" + scope;
	String b;
	b << "<h1 style=\"margin-top:0;\">" << VividIcon::Get("cpu", 22) << " Topology: " << EscapeHtml(display_target) << "</h1>\n";
	if(!canonical_document_id.IsEmpty()) b << "<p><code>" << EscapeHtml(canonical_document_id) << "</code></p>\n";
	if(!error_status.IsEmpty()) {
		int code = error_status == "unavailable" || error_status == "incomplete_scope" ? 503 : error_status == "ambiguous" || error_status == "unsafe_metadata" || error_status == "invalid_record" ? 409 : 404;
		http.Response(code, error_status == "ambiguous" ? "Ambiguous Topology ID" : "Topology Lookup Failed");
		b << "<div class=\"card\"><p>Status: <code>" << EscapeHtml(error_status) << "</code></p>"
		  << "<p><a href=\"/data/diagrams\" class=\"btn\">All diagrams</a></p></div>\n";
		PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Topology: " + display_target, "data", b, ManagerSkylarkApp::BuildDataTree(store, "data-topology"));
		http.ContentType("text/html; charset=utf-8") << pb.Build();
		return;
	}

	if(docs.IsEmpty()) {
		http.Response(404, "Topology Scope Not Found");
		b << "<div class=\"card\"><p>No topology documents found for <code>" << EscapeHtml(display_target) << "</code>.</p>"
		  << "<p><a href=\"/data/diagrams\" class=\"btn\">All diagrams</a></p></div>\n";
		PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Topology: " + display_target, "data", b, ManagerSkylarkApp::BuildDataTree(store, "data-topology"));
		http.ContentType("text/html; charset=utf-8") << pb.Build();
		return;
	}

	TopologyGraph g = BuildTopologyGraph(store, docs);
	if(g.title.IsEmpty())
		g.title = display_target;
	if(!DiagramRenderer::Layout(g)) {
		b << "<div class=\"wf-alert wf-alert-danger\">Topology layout failed; the diagram was not rendered.</div>\n";
		PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Topology: " + display_target, "data", b, ManagerSkylarkApp::BuildDataTree(store, "diagram/" + target));
		http.ContentType("text/html; charset=utf-8") << pb.Build();
		return;
	}
	String svg = DiagramRenderer::ToSVG(g, true, "diag-topology");

	if(!canonical_document_id.IsEmpty() && catalog->data_status == "loaded_with_errors")
		b << "<div role=\"status\" class=\"wf-alert wf-alert-warning\">Other source records could not be read; this individually validated topology is shown without scope aggregation.</div>\n";
	b << "<div class=\"card\" style=\"margin-bottom:1rem;\"><p style=\"margin:0; font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d);\">"
	  << g.nodes.GetCount() << " nodes, " << g.edges.GetCount() << " edges &mdash; drag to pan, scroll to zoom, or use the toolbar. "
	  << "Click a node to open its document.</p></div>\n";
	b << svg << "\n";

	PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Topology: " + display_target, "data", b, ManagerSkylarkApp::BuildDataTree(store, "diagram/" + target));
	http.ContentType("text/html; charset=utf-8") << pb.Build();
}

SKYLARK(DataPumlPage, "data/puml/**")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	const DataStore& store = catalog->data;
	String rel = JoinWildcards(http, 0);

	String b;
	b << "<h1 style=\"margin-top:0;\">" << VividIcon::Get("cpu", 22) << " Diagram: " << EscapeHtml(rel) << "</h1>\n";

	String svg, err;
	b << "<div class=\"card\" style=\"border-left:4px solid #c83771;\">"
	  << "<p>PlantUML rendering is unavailable because external SVG output is disabled for safety. "
	     "<a href=\"/data/diagrams\">Back to diagrams</a></p></div>\n";

	PageBuilder pb = ManagerSkylarkApp::RenderDataPageChrome("Diagram: " + rel, "data", b, ManagerSkylarkApp::BuildDataTree(store, "data-topology"));
	http.ContentType("text/html; charset=utf-8") << pb.Build();
}

// ---------------------------------------------------------------------------
// REST API: generic data endpoints
// ---------------------------------------------------------------------------

SKYLARK(ApiDataSpec, "api/data/spec")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	if(RejectDataCatalogUnavailable(http, *catalog)) return;
	const DataStore& store = catalog->data;
	Vector<const DataDocument*> generic_docs=GenericVisibleDocuments(app.QueryCatalogDocuments(*catalog));

	ValueMap root;
	root.Add("source_id", store.GetSourceId());
	root.Add("read_only", store.IsReadOnly());
	root.Add("catalog_generation", (int64)catalog->generation);
	root.Add("source_revision", catalog->source_revision);
	root.Add("source_status", catalog->data_status);
	root.Add("documents", generic_docs.GetCount());
	root.Add("generic_records", generic_docs.GetCount());

	ValueArray kinds;
	Vector<String> kind_list; for(const String& k:store.GetKinds()) if(!ReviewDiscussionService::IsPrivateKind(k)) kind_list.Add(k);
	for(int i = 0; i < kind_list.GetCount(); i++) {
		const SchemaCard* sc = store.FindSchemaCard(kind_list[i]);
		ValueMap km;
		km.Add("kind", kind_list[i]);
		int count=0; for(const DataDocument *d:generic_docs) if(d->kind==kind_list[i]) count++; km.Add("count", count);
		ValueArray req;
		if(sc)
			for(int r = 0; r < sc->required.GetCount(); r++)
				req.Add(sc->required[r]);
		km.Add("required", req);
		km.Add("schema_source", sc ? (sc->collection == "builtin" ? "builtin" : "discovered") : "builtin");
		kinds.Add(km);
	}
	root.Add("kinds", kinds);

	ValueArray colls;
	Index<String> collection_set; for(const DataDocument *d:generic_docs) collection_set.FindAdd(d->collection);
	Vector<String> coll_list; for(const String& c:collection_set) coll_list.Add(c);
	for(int i = 0; i < coll_list.GetCount(); i++) {
		ValueMap cm;
		cm.Add("collection", coll_list[i]);
		int count=0; for(const DataDocument *d:generic_docs) if(d->collection==coll_list[i]) count++; cm.Add("count", count);
		colls.Add(cm);
	}
	root.Add("collections", colls);
	root.Add("diagram_sources", store.GetDiagramSources().GetCount());
	root.Add("source_errors", store.GetSourceErrorCount());
	ValueArray diagnostics;
	for(const String& diagnostic : catalog->diagnostics) diagnostics.Add(diagnostic);
	root.Add("diagnostics", diagnostics);
	int invalid_count=0; for(const DataDocument *d:generic_docs) if(!d->valid) invalid_count++; root.Add("invalid_documents", invalid_count);

	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiDataKinds, "api/data/kinds")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	if(RejectDataCatalogUnavailable(http, *catalog)) return;
	const DataStore& store = catalog->data;
	ValueArray va;
	Vector<String> kinds; for(const String& k:store.GetKinds()) if(!ReviewDiscussionService::IsPrivateKind(k)) kinds.Add(k);
	for(int i = 0; i < kinds.GetCount(); i++) {
		ValueMap km;
		km.Add("kind", kinds[i]);
		int count=0; for(const DataDocument *d:app.QueryCatalogDocuments(*catalog,kinds[i])) if(!ReviewDiscussionService::IsPrivateKind(d->kind)) count++; km.Add("count", count);
		va.Add(km);
	}
	ValueMap root;
	root.Add("kinds", va);
	root.Add("catalog_generation", (int64)catalog->generation);
	root.Add("source_revision", catalog->source_revision);
	root.Add("source_status", catalog->data_status);
	root.Add("source_errors", store.GetSourceErrorCount());
	root.Add("diagnostics", ValueArray());
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiDataKindDocs, "api/data/kind/*")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	if(RejectDataCatalogUnavailable(http, *catalog)) return;
	const DataStore& store = catalog->data;
	String kind = http[0];
	if(ReviewDiscussionService::IsPrivateKind(kind)) { http.Response(404,"Kind Not Found"); http.ContentType("application/json")<<"{\"status\":\"missing\"}"; return; }
	ValueArray va;
	Vector<const DataDocument*> docs = GenericVisibleDocuments(app.QueryCatalogDocuments(*catalog, kind));
	for(int i = 0; i < docs.GetCount(); i++) {
			const DataDocument& d = *docs[i];
		ValueMap dm;
		dm.Add("source_id", d.source_id);
		dm.Add("read_only", d.read_only);
		dm.Add("id", d.id);
		dm.Add("title", d.title);
		dm.Add("status", d.status);
		dm.Add("collection", d.collection);
		dm.Add("scope", d.scope);
		dm.Add("valid", d.valid);
		dm.Add("path", d.rel_path);
		va.Add(dm);
	}
	ValueMap root;
	root.Add("kind", kind);
	root.Add("count", va.GetCount());
	root.Add("documents", va);
	root.Add("catalog_generation", (int64)catalog->generation);
	root.Add("source_revision", catalog->source_revision);
	root.Add("source_status", catalog->data_status);
	root.Add("source_errors", store.GetSourceErrorCount());
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiDataDoc, "api/data/doc/**")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	if(RejectDataCatalogUnavailable(http, *catalog)) return;
	const DataStore& store = catalog->data;
	String doc_id = JoinWildcards(http, 0);
	CatalogRecordRead read = ReadCatalogRecord(*catalog, doc_id, true);
	if(read.status == "ambiguous") {
		http.Response(409, "Ambiguous Record ID");
		ValueMap err = read.response;
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}
	if(read.status == "unsafe_metadata") {
		http.Response(409, "Unsafe Record Metadata");
		http.ContentType("application/json") << AsJSON(read.response, true);
		return;
	}
	if(read.status != "ok") {
		http.Response(404, "Record Not Found");
		http.ContentType("application/json") << AsJSON(read.response, true);
		return;
	}
			const DataDocument* d = store.FindDocument(MapGetString(read.response, "qualified_id"));
	if(!d) {
		http.Response(503, "Catalog Changed");
		http.ContentType("application/json") << AsJSON(read.response, true);
			return;
		}
		if(ReviewDiscussionService::IsPrivateKind(d->kind)) { http.Response(404,"Record Not Found"); http.ContentType("application/json")<<"{\"status\":\"missing\"}"; return; }
	ValueMap root = read.response;
	root.Add("source_id", d->source_id);
	root.Add("read_only", d->read_only);
	root.Add("id", d->id);
	root.Add("kind", d->kind);
	root.Add("title", d->title);
	root.Add("document_status", d->status);
	root.Add("collection", d->collection);
	root.Add("scope", d->scope);
	root.Add("path", d->rel_path);
	root.Add("revision", d->revision);
	root.Add("data", MapGet(read.response, "record"));
	root.Add("source_status", catalog->data_status);
	http.ContentType("application/json") << AsJSON(root, true);
}

END_UPP_NAMESPACE
