#include "WebEngine.h"
#include <WebAuth/WebAuth.h>

NAMESPACE_UPP

static const char *CONTENT_LINK_PEPPER = "wf-content-link-v1";

String ContentLevelName(ContentLevel level)
{
	switch(level) {
	case CONTENT_PUBLIC:      return "public";
	case CONTENT_PROTECTED:   return "protected";
	case CONTENT_PRIVATE:     return "private";
	case CONTENT_PUBLIC_ONLY: return "public-only";
	default:                  return "";
	}
}

bool ContentLevelParse(const String& name, ContentLevel& level)
{
	if(name == "public")           level = CONTENT_PUBLIC;
	else if(name == "protected")   level = CONTENT_PROTECTED;
	else if(name == "private")     level = CONTENT_PRIVATE;
	else if(name == "public-only") level = CONTENT_PUBLIC_ONLY;
	else return false;
	return true;
}

ValueMap ContentPolicy::ToValue() const
{
	ValueMap m;
	m.Add("level", ContentLevelName(level));
	ValueArray arr;
	for(const ContentGrant& g : grants) {
		ValueMap gm;
		gm.Add("kind", g.kind);
		gm.Add("value", g.value);
		gm.Add("label", g.label);
		gm.Add("revoked", g.revoked);
		arr.Add(gm);
	}
	m.Add("grants", arr);
	return m;
}

ContentPolicy ContentPolicy::FromValue(const ValueMap& m)
{
	ContentPolicy p;
	ContentLevel lvl;
	if(ContentLevelParse(m.Get("level", ""), lvl))
		p.level = lvl;
	Value grants = m.Get("grants");
	if(grants.Is<ValueArray>()) {
		ValueArray arr = grants;
		for(int i = 0; i < arr.GetCount(); i++) {
			if(!arr[i].Is<ValueMap>())
				continue;
			ValueMap gm = arr[i];
			ContentGrant g;
			g.kind = gm.Get("kind", "");
			g.value = gm.Get("value", "");
			g.label = gm.Get("label", "");
			g.revoked = (bool)gm.Get("revoked", false);
			if((g.kind == "user" || g.kind == "group" || g.kind == "link") && g.value.GetCount())
				p.grants.Add(pick(g));
		}
	}
	return p;
}

String ContentLinkHash(const String& token)
{
	return WebAuthPepperedHash(CONTENT_LINK_PEPPER, token);
}

static bool NamedGrant(const ContentPolicy& p, const ContentPrincipal& who)
{
	if(!who.authenticated)
		return false;
	for(const ContentGrant& g : p.grants) {
		if(g.revoked)
			continue;
		if(g.kind == "user" && g.value == who.username)
			return true;
		if(g.kind == "group")
			for(const String& role : who.groups)
				if(g.value == role)
					return true;
	}
	return false;
}

static bool LinkGrant(const ContentPolicy& p, const ContentPrincipal& who)
{
	if(who.link_token.IsEmpty())
		return false;
	String hash = ContentLinkHash(who.link_token);
	for(const ContentGrant& g : p.grants)
		if(!g.revoked && g.kind == "link" && WebAuthConstantTimeEquals(g.value, hash))
			return true;
	return false;
}

bool ContentVisible(const ContentPolicy& p, const ContentPrincipal& who)
{
	switch(p.level) {
	case CONTENT_PUBLIC:
		return true;
	case CONTENT_PUBLIC_ONLY:
		return !who.authenticated;
	case CONTENT_PRIVATE:
		return who.admin || NamedGrant(p, who);
	case CONTENT_PROTECTED:
		return who.admin || NamedGrant(p, who) || LinkGrant(p, who);
	default:
		return false;
	}
}

ContentPolicy ContentResolve(const Vector<ContentPolicy>& chain, ContentLevel fallback)
{
	for(const ContentPolicy& p : chain)
		if(p.level != CONTENT_UNSET)
			return p;
	ContentPolicy p;
	p.level = fallback;
	return p;
}

bool ContentPolicyStore::Load(const String& data_root_, String& error)
{
	root = data_root_;
	site = ContentPolicy();
	modules.Clear();
	records.Clear();
	String path = AppendFileName(root, "content_policy.json");
	if(!FileExists(path))
		return true;
	Value v;
	try {
		v = ParseJSON(LoadFile(path));
	}
	catch(Exc& e) {
		error = String("content policy is not valid JSON: ") + e;
		return false;
	}
	if(!v.Is<ValueMap>()) {
		error = "content policy is not an object";
		return false;
	}
	ValueMap doc = v;
	if((int)doc.Get("version", 0) != VERSION) {
		error = "content policy version is not supported";
		return false;
	}
	site = ContentPolicy::FromValue(doc.Get("site").Is<ValueMap>() ? (ValueMap)doc.Get("site") : ValueMap());
	Value mods = doc.Get("modules");
	if(mods.Is<ValueMap>()) {
		ValueMap mm = mods;
		for(int i = 0; i < mm.GetCount(); i++)
			if(mm.GetValue(i).Is<ValueMap>())
				modules.Add(mm.GetKey(i), ContentPolicy::FromValue(mm.GetValue(i)));
	}
	Value recs = doc.Get("records");
	if(recs.Is<ValueMap>()) {
		ValueMap rm = recs;
		for(int i = 0; i < rm.GetCount(); i++)
			if(rm.GetValue(i).Is<ValueMap>())
				records.Add(rm.GetKey(i), ContentPolicy::FromValue(rm.GetValue(i)));
	}
	return true;
}

bool ContentPolicyStore::Save(String& error) const
{
	ValueMap doc;
	doc.Add("version", VERSION);
	doc.Add("site", site.ToValue());
	ValueMap mm;
	for(int i = 0; i < modules.GetCount(); i++)
		mm.Add(modules.GetKey(i), modules[i].ToValue());
	doc.Add("modules", mm);
	ValueMap rm;
	for(int i = 0; i < records.GetCount(); i++)
		rm.Add(records.GetKey(i), records[i].ToValue());
	doc.Add("records", rm);
	String path = AppendFileName(root, "content_policy.json");
	RealizeDirectory(root);
	String tmp = path + ".tmp";
	if(!SaveFile(tmp, AsJSON(doc, true)) || !FileMove(tmp, path)) {
		error = "content policy could not be written";
		return false;
	}
	return true;
}

ContentPolicy ContentPolicyStore::Module(const String& name) const
{
	int i = modules.Find(name);
	return i >= 0 ? modules[i] : ContentPolicy();
}

ContentPolicy ContentPolicyStore::Record(const String& id) const
{
	int i = records.Find(id);
	return i >= 0 ? records[i] : ContentPolicy();
}

void ContentPolicyStore::SetModule(const String& name, const ContentPolicy& p)
{
	modules.GetAdd(name) = p;
}

void ContentPolicyStore::SetRecord(const String& id, const ContentPolicy& p)
{
	records.GetAdd(id) = p;
}

ContentPolicy ContentPolicyStore::Effective(const String& module, const String& record_id, ContentLevel fallback) const
{
	Vector<ContentPolicy> chain;
	chain.Add(Record(record_id));
	chain.Add(Module(module));
	chain.Add(site);
	return ContentResolve(chain, fallback);
}

ContentPolicyStore& ContentPolicyFor(const String& data_root)
{
	static Mutex lock;
	static VectorMap<String, One<ContentPolicyStore>> stores;
	Mutex::Lock __(lock);
	int i = stores.Find(data_root);
	if(i < 0) {
		i = stores.GetCount();
		stores.Add(data_root).Create();
		String err;
		stores[i]->Load(data_root, err);
	}
	return *stores[i];
}

END_UPP_NAMESPACE
