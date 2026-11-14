#include "ScienceService.h"

NAMESPACE_UPP

static bool HasScope(const BrokerCallContext& ctx, const char *scope)
{
	for(const String& s : ctx.scopes)
		if(s == scope)
			return true;
	return false;
}

static BrokerResult Refuse(int status, const String& error)
{
	BrokerResult r;
	r.ok = false;
	r.status = status;
	r.error = error;
	return r;
}

static BrokerResult Accept(const ValueMap& payload)
{
	BrokerResult r;
	r.ok = true;
	r.status = 200;
	r.payload = payload;
	return r;
}

static String Arg(const ValueMap& m, const char *key)
{
	Value v = m.Get(key);
	return v.IsNull() ? String() : (String)v;
}

BrokerResult ScienceService::Handle(const BrokerCallContext& ctx, const BrokerCall& call)
{
	const ValueMap& in = call.payload;

	if(call.op == "list-papers") {
		if(!HasScope(ctx, READ_SCOPE()))
			return Refuse(403, "missing scope science.read");
		Vector<SciencePaper> papers;
		corpus.ListPapers(papers);
		ValueArray arr;
		for(const SciencePaper& p : papers) {
			ValueMap m;
			m.Add("id", p.id);
			m.Add("title", p.title);
			m.Add("status", p.status);
			m.Add("revision", p.revision);
			m.Add("has_article", p.has_article);
			arr.Add(m);
		}
		ValueMap out;
		out.Add("papers", arr);
		return Accept(out);
	}

	if(call.op == "get-paper") {
		if(!HasScope(ctx, READ_SCOPE()))
			return Refuse(403, "missing scope science.read");
		SciencePaper p;
		if(!corpus.GetPaper(Arg(in, "id"), p))
			return Refuse(404, "paper not found");
		ValueMap out;
		out.Add("id", p.id);
		out.Add("title", p.title);
		out.Add("abstract", p.abstract);
		out.Add("status", p.status);
		out.Add("revision", p.revision);
		out.Add("has_article", p.has_article);
		return Accept(out);
	}

	if(call.op == "get-article") {
		if(!HasScope(ctx, READ_SCOPE()))
			return Refuse(403, "missing scope science.read");
		String id = Arg(in, "id");
		ScienceArticleTree tree;
		ForgeResult ar = corpus.GetArticleTree(id, tree);
		if(!ar.ok)
			return Refuse(ScienceErrorStatus(ar.error), ar.error);
		ValueMap out;
		out.Add("article", StoreAsJsonValue(tree));
		return Accept(out);
	}

	if(call.op == "put-article") {
		if(!HasScope(ctx, AUTHOR_SCOPE()))
			return Refuse(403, "missing scope science.author");
		ScienceArticleTree tree;
		Value article = in.Get("article");
		if(!LoadFromJson(tree, AsJSON(article)))
			return Refuse(400, "article is not a valid article tree");
		String author = ctx.caller_id.IsEmpty() ? String("broker:") + ctx.from_site : ctx.caller_id;
		ForgeResult res = corpus.SaveArticleTree(Arg(in, "id"), tree, Arg(in, "expected_paper_revision"), author);
		if(!res.ok)
			return Refuse(ScienceErrorStatus(res.error), res.error);
		ValueMap out;
		out.Add("paper_id", Arg(in, "id"));
		return Accept(out);
	}

	return Refuse(400, "unknown science op: " + call.op);
}

String ScienceService::AuditOperation(const String& op) const
{
	if(op == "list-papers" || op == "get-paper" || op == "get-article" || op == "put-article")
		return op;
	return "unclassified";
}

END_UPP_NAMESPACE
