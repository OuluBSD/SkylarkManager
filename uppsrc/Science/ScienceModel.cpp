#include "ScienceModel.h"

NAMESPACE_UPP

void ScienceNode::Jsonize(JsonIO& io)
{
	io("id", id)("type", type)("heading", heading)("text", text)("latex", latex)
	  ("caption", caption)("image_ref", image_ref)("ref_key", ref_key)("source_ref", source_ref)
	  ("rows", rows);
}

void ScienceArticleTree::Jsonize(JsonIO& io)
{
	io("paper_id", paper_id)("title", title)("nodes", nodes);
}

static SchemaCard ArticleTreeSchema()
{
	SchemaCard card;
	card.schema_id = "science/article-tree";
	card.version = "1";
	SchemaField f;
	f.name = "paper_id"; f.type = "string"; f.required = true; f.max_len = 200; card.fields.Add(f);
	f.name = "title"; f.type = "string"; f.required = true; f.max_len = 500; card.fields.Add(f);
	f.name = "nodes"; f.type = "array"; f.required = true; card.fields.Add(f);
	return card;
}

void ScienceCorpus::RegisterSchemas()
{
	static bool done = false;
	if(done)
		return;
	done = true;
	SchemaRegistry().Register(ArticleTreeSchema());
}

bool ScienceCorpus::ValidateArticleTree(const ValueMap& tree_doc, String& error)
{
	RegisterSchemas();
	return SchemaRegistry::Validate("science/article-tree", tree_doc, error);
}

// Manual plain-JSON serialization: StoreAsJsonValue degrades structs with
// vector-of-struct fields (nodes) into Upp's typed debug format.
static ValueMap ArticleTreeToValue(const ScienceArticleTree& t)
{
	ValueMap m;
	m.Add("paper_id", t.paper_id);
	m.Add("title", t.title);
	ValueArray nodes;
	for(int i = 0; i < t.nodes.GetCount(); i++) {
		ScienceNode n = t.nodes[i]; // Jsonize is non-const
		nodes.Add(StoreAsJsonValue(n));
	}
	m.Add("nodes", nodes);
	return m;
}

ScienceCorpus::ScienceCorpus(const String& corpus_data_dir_, const String& article_store_root_)
{
	ASSERT_(corpus_data_dir_.GetCount(), "ScienceCorpus needs a corpus data dir");
	corpus_dir = corpus_data_dir_;
	article_root = article_store_root_;
	RealizeDirectory(article_root);
	RegisterSchemas();
}

String ScienceCorpus::SidecarPath(const String& id) const
{
	return AppendFileName(article_root, id + ".article.json");
}

ForgeResult ScienceCorpus::ReadPaperRecord(const String& id, ValueMap& doc) const
{
	if(!JsonFileStore::ValidId(id))
		return IForgeBackend::Failed("get-paper", id, "invalid paper id");
	String path = AppendFileName(corpus_dir, id + ".json");
	if(!FileExists(path))
		return IForgeBackend::Failed("get-paper", id, "paper not found");
	String raw = LoadFile(path);
	Value v;
	try {
		v = ParseJSON(raw);
	}
	catch(Exc& e) {
		return IForgeBackend::Failed("get-paper", id, String("corpus record is not valid JSON: ") + e);
	}
	if(!v.Is<ValueMap>())
		return IForgeBackend::Failed("get-paper", id, "corpus record is not an object");
	doc = v;
	return IForgeBackend::Ok("get-paper", id, ForgeProvenance::LocalPersistent);
}

bool ScienceCorpus::ListPapers(Vector<SciencePaper>& out) const
{
	out.Clear();
	for(const FindFile& ff : FindFile(corpus_dir + "/*.json")) {
		if(ff.GetName().Find(".article.") >= 0)
			continue;
		String raw = LoadFile(ff.GetPath());
		Value v;
		try {
			v = ParseJSON(raw);
		}
		catch(...) {
			continue; // corrupt corpus entries are skipped, never mutated
		}
		if(!v.Is<ValueMap>())
			continue;
		ValueMap m = v;
		SciencePaper p;
		p.id = m.Get("id").IsNull() ? String() : (String)m.Get("id");
		if(p.id.IsEmpty())
			continue;
		p.title = m.Get("title").IsNull() ? String() : (String)m.Get("title");
		p.source = m.Get("source").IsNull() ? String() : (String)m.Get("source");
		p.status = m.Get("status").IsNull() ? String() : (String)m.Get("status");
		p.abstract = m.Get("abstract").IsNull() ? String() : (String)m.Get("abstract");
		p.arxiv_id = m.Get("arxiv_id").IsNull() ? String() : (String)m.Get("arxiv_id");
		p.local_file = m.Get("local_file").IsNull() ? String() : (String)m.Get("local_file");
		Value env = m.Get("envelope");
		if(env.Is<ValueMap>()) {
			ValueMap e = env;
			p.schema_id = e.Get("schema_id").IsNull() ? String() : (String)e.Get("schema_id");
			p.schema_version = e.Get("schema_version").IsNull() ? String() : (String)e.Get("schema_version");
			p.revision = e.Get("revision").IsNull() ? String() : (String)e.Get("revision");
			p.visibility = e.Get("visibility").IsNull() ? String() : (String)e.Get("visibility");
		}
		p.has_article = FileExists(SidecarPath(p.id));
		out.Add(p);
	}
	Vector<String> ids;
	for(const SciencePaper& p : out)
		ids.Add(p.id);
	Vector<int> order = GetSortOrder(ids);
	Vector<SciencePaper> sorted;
	for(int q : order)
		sorted.Add(out[q]);
	out.Clear();
	for(const SciencePaper& p : sorted)
		out.Add(p);
	return true;
}

bool ScienceCorpus::GetPaper(const String& id, SciencePaper& out) const
{
	ValueMap doc;
	if(!ReadPaperRecord(id, doc).ok)
		return false;
	SciencePaper p;
	p.id = doc.Get("id").IsNull() ? String() : (String)doc.Get("id");
	p.title = doc.Get("title").IsNull() ? String() : (String)doc.Get("title");
	p.source = doc.Get("source").IsNull() ? String() : (String)doc.Get("source");
	p.status = doc.Get("status").IsNull() ? String() : (String)doc.Get("status");
	p.abstract = doc.Get("abstract").IsNull() ? String() : (String)doc.Get("abstract");
	p.arxiv_id = doc.Get("arxiv_id").IsNull() ? String() : (String)doc.Get("arxiv_id");
	p.local_file = doc.Get("local_file").IsNull() ? String() : (String)doc.Get("local_file");
	Value env = doc.Get("envelope");
	if(env.Is<ValueMap>()) {
		ValueMap e = env;
		p.schema_id = e.Get("schema_id").IsNull() ? String() : (String)e.Get("schema_id");
		p.schema_version = e.Get("schema_version").IsNull() ? String() : (String)e.Get("schema_version");
		p.revision = e.Get("revision").IsNull() ? String() : (String)e.Get("revision");
		p.visibility = e.Get("visibility").IsNull() ? String() : (String)e.Get("visibility");
	}
	p.has_article = FileExists(SidecarPath(p.id));
	out = p;
	return true;
}

bool ScienceCorpus::HasArticleTree(const String& id) const
{
	return FileExists(SidecarPath(id));
}

ForgeResult ScienceCorpus::GetArticleTree(const String& id, ScienceArticleTree& out) const
{
	if(!JsonFileStore::ValidId(id))
		return IForgeBackend::Failed("get-article", id, "invalid paper id");
	String path = SidecarPath(id);
	if(!FileExists(path))
		return IForgeBackend::Failed("get-article", id, "no structured article for this paper");
	Value v;
	try {
		v = ParseJSON(LoadFile(path));
	}
	catch(Exc& e) {
		return IForgeBackend::Failed("get-article", id, String("article sidecar is not valid JSON: ") + e);
	}
	if(!v.Is<ValueMap>())
		return IForgeBackend::Failed("get-article", id, "article sidecar is not an object");
	ValueMap doc = v;
	Value article = doc.Get("article");
	if(!article.Is<ValueMap>())
		return IForgeBackend::Failed("get-article", id, "article sidecar has no article document");
	ScienceArticleTree tree;
	try {
		LoadFromJsonValue(tree, article);
	}
	catch(Exc& e) {
		return IForgeBackend::Failed("get-article", id, String("article tree malformed: ") + e);
	}
	String err;
	if(!ValidateArticleTree(article, err))
		return IForgeBackend::Failed("get-article", id, "article tree fails schema: " + err);
	out = tree;
	return IForgeBackend::Ok("get-article", id, ForgeProvenance::LocalPersistent);
}

ForgeResult ScienceCorpus::SaveArticleTree(const String& id, const ScienceArticleTree& tree,
                                           const String& expected_paper_revision, const String& author)
{
	if(!JsonFileStore::ValidId(id))
		return IForgeBackend::Failed("save-article", id, "invalid paper id");
	SciencePaper paper;
	if(!GetPaper(id, paper))
		return IForgeBackend::Failed("save-article", id, "paper not found");
	if(!expected_paper_revision.IsEmpty() && paper.revision != expected_paper_revision)
		return IForgeBackend::Failed("save-article", id,
			Format("paper revision drift: expected %s, current %s", ~expected_paper_revision, ~paper.revision));
	ScienceArticleTree t = tree;
	t.paper_id = id;
	if(t.title.IsEmpty())
		t.title = paper.title;
	if(t.nodes.IsEmpty())
		return IForgeBackend::Failed("save-article", id, "article tree has no nodes");
	for(const ScienceNode& n : t.nodes) {
		if(n.id.IsEmpty() || n.type.IsEmpty())
			return IForgeBackend::Failed("save-article", id, "every node needs an id and a type");
	}
	ValueMap article_vm = ArticleTreeToValue(t);
	Value article = article_vm;
	String err;
	if(!ValidateArticleTree(article, err))
		return IForgeBackend::Failed("save-article", id, "article tree fails schema: " + err);

	// Sidecar envelope: schema science/article-tree; revision = paper revision.
	RecordEnvelope env;
	env.schema_id = "science/article-tree";
	env.schema_version = "1";
	env.workspace_id = "science";
	env.stable_id = id + "#article";
	env.record_kind = "article";
	env.revision = paper.revision.GetCount() ? paper.revision : String("1");
	env.visibility = paper.visibility;
	env.provenance_ref = author;

	Value env_v = ParseJSON(StoreAsJson(env));
	ValueMap doc;
	doc.Add("envelope", env_v);
	doc.Add("article", article);
	String error;
	if(!JsonFileStore::WriteAtomic(SidecarPath(id), AsJSON(doc, true), error))
		return IForgeBackend::Failed("save-article", id, "atomic write failed: " + error);
	return IForgeBackend::Ok("save-article", id, ForgeProvenance::LocalPersistent);
}

END_UPP_NAMESPACE
