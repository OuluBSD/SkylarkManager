#ifndef _Science_ScienceModel_h_
#define _Science_ScienceModel_h_

#include <Core/Core.h>
#include <WebStorage/WebStorage.h>
#include <ForgeCore/ForgeCore.h>

NAMESPACE_UPP

// One corpus paper (flat metadata record, schema skylark/paper).
struct SciencePaper : Moveable<SciencePaper> {
	String id;          // stable id (e.g. arxiv id)
	String title;
	String source;
	String status;
	String abstract;
	String arxiv_id;
	String local_file;
	String schema_id;
	String schema_version;
	String revision;
	String visibility;
	bool   has_article = false; // sidecar article tree present

	SciencePaper() {}
	SciencePaper(const SciencePaper& o)
		: id(o.id), title(o.title), source(o.source), status(o.status)
		, abstract(o.abstract), arxiv_id(o.arxiv_id), local_file(o.local_file)
		, schema_id(o.schema_id), schema_version(o.schema_version)
		, revision(o.revision), visibility(o.visibility), has_article(o.has_article) {}
	SciencePaper& operator=(const SciencePaper& p) {
		if(this != &p) {
			id = p.id; title = p.title; source = p.source; status = p.status;
			abstract = p.abstract; arxiv_id = p.arxiv_id; local_file = p.local_file;
			schema_id = p.schema_id; schema_version = p.schema_version;
			revision = p.revision; visibility = p.visibility; has_article = p.has_article;
		}
		return *this;
	}
};

// Article-tree node types.
struct ScienceNode : Moveable<ScienceNode> {
	String id;          // stable node id
	String type;        // section|paragraph|equation|table|figure|citation|reference
	String heading;     // section
	String text;        // paragraph
	String latex;       // equation
	String caption;     // table/figure
	String image_ref;   // figure
	String ref_key;     // citation/reference
	String source_ref;  // citation
	Vector<String> rows; // table rows, one string per row, cells tab-separated

	ScienceNode() {}
	ScienceNode(const ScienceNode& o)
		: id(o.id), type(o.type), heading(o.heading), text(o.text), latex(o.latex)
		, caption(o.caption), image_ref(o.image_ref), ref_key(o.ref_key)
		, source_ref(o.source_ref), rows(clone(o.rows)) {}
	ScienceNode& operator=(const ScienceNode& o) {
		if(this != &o) {
			id = o.id; type = o.type; heading = o.heading; text = o.text; latex = o.latex;
			caption = o.caption; image_ref = o.image_ref; ref_key = o.ref_key;
			source_ref = o.source_ref;
			rows = clone(o.rows);
		}
		return *this;
	}

	void Jsonize(JsonIO& io);
};

struct ScienceArticleTree : Moveable<ScienceArticleTree> {
	String paper_id;
	String title;
	Vector<ScienceNode> nodes;

	ScienceArticleTree() {}
	ScienceArticleTree(const ScienceArticleTree& o)
		: paper_id(o.paper_id), title(o.title), nodes(clone(o.nodes)) {}
	ScienceArticleTree& operator=(const ScienceArticleTree& o) {
		if(this != &o) {
			paper_id = o.paper_id;
			title = o.title;
			nodes = clone(o.nodes);
		}
		return *this;
	}

	void Jsonize(JsonIO& io);
};

// HTTP-style status for a SaveArticleTree / corpus error string.
int ScienceErrorStatus(const String& error);

// Read-only corpus reader + validated article sidecar store.
class ScienceCorpus {
public:
	ScienceCorpus(const String& corpus_data_dir, const String& article_store_root);

	bool        ListPapers(Vector<SciencePaper>& out) const;
	bool        GetPaper(const String& id, SciencePaper& out) const; // full record incl. abstract
	ForgeResult GetArticleTree(const String& id, ScienceArticleTree& out) const;
	bool        HasArticleTree(const String& id) const;
	// Optimistic write: expected_revision is the PAPER envelope revision the
	// tree was authored against. Validates the tree against
	// science/article-tree v1 and writes the sidecar atomically.
	ForgeResult SaveArticleTree(const String& id, const ScienceArticleTree& tree,
	                            const String& expected_paper_revision, const String& author);

	static void  RegisterSchemas();
	static bool  ValidateArticleTree(const ValueMap& tree_doc, String& error);

private:
	String       SidecarPath(const String& id) const;
	ForgeResult  ReadPaperRecord(const String& id, ValueMap& doc) const;

	String corpus_dir;
	String article_root;
};

END_UPP_NAMESPACE

#endif
