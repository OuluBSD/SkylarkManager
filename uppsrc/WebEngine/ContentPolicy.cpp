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

static bool HasOnlyKeys(const ValueMap& m, const char *const *allowed, int count)
{
	for(int i = 0; i < m.GetCount(); i++) {
		bool found = false;
		for(int j = 0; j < count; j++)
			if(m.GetKey(i) == allowed[j]) { found = true; break; }
		if(!found)
			return false;
	}
	return true;
}

bool ContentPolicy::FromValue(const ValueMap& m, ContentPolicy& policy, String& error)
{
	static const char *fields[] = { "level", "grants" };
	if(!HasOnlyKeys(m, fields, __countof(fields)) || m.Find("level") < 0 || m.Find("grants") < 0) {
		error = "policy must contain only level and grants";
		return false;
	}
	Value level_value = m.Get("level");
	ContentPolicy parsed;
	if(!level_value.Is<String>()) {
		error = "policy level must be a string";
		return false;
	}
	String name = level_value;
	if(!name.IsEmpty() && !ContentLevelParse(name, parsed.level)) {
		error = "policy level is not supported";
		return false;
	}
	Value grants = m.Get("grants");
	if(!grants.Is<ValueArray>()) {
		error = "policy grants must be an array";
		return false;
	}
	ValueArray arr = grants;
	static const char *grant_fields[] = { "kind", "value", "label", "revoked" };
	for(int i = 0; i < arr.GetCount(); i++) {
		if(!arr[i].Is<ValueMap>()) {
			error = Format("grant %d must be an object", i);
			return false;
		}
		ValueMap gm = arr[i];
		if(!HasOnlyKeys(gm, grant_fields, __countof(grant_fields)) || gm.Find("kind") < 0 || gm.Find("value") < 0 ||
		   gm.Find("label") < 0 || gm.Find("revoked") < 0) {
			error = Format("grant %d must contain only kind, value, label and revoked", i);
			return false;
		}
		Value kind_value = gm.Get("kind"), value_value = gm.Get("value"), label_value = gm.Get("label"), revoked_value = gm.Get("revoked");
		if(!kind_value.Is<String>() || !value_value.Is<String>() || !label_value.Is<String>() || !revoked_value.Is<bool>()) {
			error = Format("grant %d has a field with the wrong type", i);
			return false;
		}
		ContentGrant g;
		g.kind = kind_value;
		g.value = value_value;
		g.label = label_value;
		g.revoked = revoked_value;
		if((g.kind != "user" && g.kind != "group" && g.kind != "link") || g.value.IsEmpty()) {
			error = Format("grant %d has an invalid kind or empty value", i);
			return false;
		}
		parsed.grants.Add(pick(g));
	}
	policy = parsed;
	return true;
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
	String hash = who.link_hash;
	if(hash.IsEmpty() && !who.link_token.IsEmpty())
		hash = ContentLinkHash(who.link_token);
	if(hash.IsEmpty())
		return false;
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
	load_error.Clear();
	error.Clear();
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
		load_error = error;
		return false;
	}
	if(!v.Is<ValueMap>()) {
		error = "content policy is not an object";
		load_error = error;
		return false;
	}
	ValueMap doc = v;
	static const char *document_fields[] = { "version", "site", "modules", "records" };
	Value version = doc.Get("version");
	bool version_is_integer = version.Is<int>() || version.Is<int64>() || version.Is<double>();
	bool version_matches = (version.Is<int>() && (int)version == VERSION)
	                    || (version.Is<int64>() && (int64)version == VERSION)
	                    || (version.Is<double>() && (double)version == VERSION);
	if(!HasOnlyKeys(doc, document_fields, __countof(document_fields)) || doc.Find("version") < 0 ||
	   !version_is_integer || !version_matches) {
		error = "content policy version is not supported";
		load_error = error;
		return false;
	}
	Value site_value = doc.Get("site"), mods = doc.Get("modules"), recs = doc.Get("records");
	if(!site_value.Is<ValueMap>() || !mods.Is<ValueMap>() || !recs.Is<ValueMap>()) {
		error = "content policy site, modules and records must be objects";
		load_error = error;
		return false;
	}
	String parse_error;
	if(!ContentPolicy::FromValue((ValueMap)site_value, site, parse_error)) {
		error = "invalid site policy: " + parse_error;
		load_error = error;
		return false;
	}
	ValueMap mm = mods;
	for(int i = 0; i < mm.GetCount(); i++) {
		ContentPolicy p;
		String key = mm.GetKey(i);
		parse_error.Clear();
		if(key.IsEmpty() || !mm.GetValue(i).Is<ValueMap>() ||
		   !ContentPolicy::FromValue((ValueMap)mm.GetValue(i), p, parse_error)) {
			error = Format("invalid module policy '%s': %s", key, parse_error.IsEmpty() ? "module key and value must be valid" : parse_error);
			load_error = error;
			return false;
		}
		modules.Add(key, p);
	}
	ValueMap rm = recs;
	for(int i = 0; i < rm.GetCount(); i++) {
		ContentPolicy p;
		String key = rm.GetKey(i);
		parse_error.Clear();
		if(key.IsEmpty() || !rm.GetValue(i).Is<ValueMap>() ||
		   !ContentPolicy::FromValue((ValueMap)rm.GetValue(i), p, parse_error)) {
			error = Format("invalid record policy '%s': %s", key, parse_error.IsEmpty() ? "record key and value must be valid" : parse_error);
			load_error = error;
			return false;
		}
		records.Add(key, p);
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

bool ContentPolicyStore::FindLinkGrant(const String& token, String& scope, String& id, String& hash) const
{
	if(token.IsEmpty() || token.GetCount() > 256)
		return false;
	String wanted_hash = ContentLinkHash(token);
	int matches = 0;
	String found_scope, found_id;
	auto inspect = [&](const ContentPolicy& p, const String& at_scope, const String& at_id) {
		if(p.level != CONTENT_PROTECTED)
			return;
		for(const ContentGrant& g : p.grants)
			if(!g.revoked && g.kind == "link" && WebAuthConstantTimeEquals(g.value, wanted_hash)) {
				matches++;
				found_scope = at_scope;
				found_id = at_id;
			}
	};
	inspect(site, "site", "");
	for(int i = 0; i < modules.GetCount(); i++)
		inspect(modules[i], "module", modules.GetKey(i));
	for(int i = 0; i < records.GetCount(); i++)
		inspect(records[i], "record", records.GetKey(i));
	if(matches != 1)
		return false;
	scope = found_scope;
	id = found_id;
	hash = wanted_hash;
	return true;
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
		if(!stores[i]->Load(data_root, err))
			LOG("Content policy disabled for " << data_root << ": " << err);
	}
	return *stores[i];
}

struct ContentPolicyLinkSession : Moveable<ContentPolicyLinkSession> {
	String site_id;
	String data_root;
	String scope;
	String id;
	String hash;
	Time expires;
};

static Mutex s_link_session_lock;
static VectorMap<String, ContentPolicyLinkSession> s_link_sessions;

static String LinkCookieName(const String& site_id)
{
	return "wfcp_link_" + site_id;
}

bool CreateContentPolicyLinkSession(WebRequest& request, const String& scope, const String& id,
                                    const String& link_hash, int lifetime_seconds)
{
	if(link_hash.IsEmpty() || lifetime_seconds <= 0 ||
	   (scope != "site" && scope != "module" && scope != "record") ||
	   (scope != "site" && id.IsEmpty()))
		return false;
	String session_id = WebAuthRandomBytes(32, true);
	Time expires = GetSysTime() + lifetime_seconds;
	{
		Mutex::Lock __(s_link_session_lock);
		for(int i = s_link_sessions.GetCount() - 1; i >= 0; i--)
			if(s_link_sessions[i].expires <= GetSysTime())
				s_link_sessions.Remove(i);
		if(s_link_sessions.GetCount() >= 10000)
			return false;
		ContentPolicyLinkSession session;
		session.site_id = request.Site().id;
		session.data_root = request.Site().data_root;
		session.scope = scope;
		session.id = id;
		session.hash = link_hash;
		session.expires = expires;
		s_link_sessions.Add(session_id, session);
	}
	request.SetCookieValue(LinkCookieName(request.Site().id), session_id, expires, request.IsSecureTransport());
	return true;
}

static bool LinkSessionFor(WebRequest& request, const String& module, const String& record_id, String& hash)
{
	String session_id = request.RequestCookie(LinkCookieName(request.Site().id));
	if(session_id.IsEmpty())
		return false;
	Mutex::Lock __(s_link_session_lock);
	int i = s_link_sessions.Find(session_id);
	if(i < 0)
		return false;
	const ContentPolicyLinkSession& session = s_link_sessions[i];
	if(session.expires <= GetSysTime()) {
		s_link_sessions.Remove(i);
		return false;
	}
	bool in_scope = session.scope == "site" ||
	                (session.scope == "module" && session.id == module) ||
	                (session.scope == "record" && session.id == record_id);
	if(session.site_id != request.Site().id || session.data_root != request.Site().data_root || !in_scope)
		return false;
	hash = session.hash;
	return true;
}

void ContentPolicyApplyLinkSession(WebRequest& request, const String& module, const String& record_id,
                                   ContentPrincipal& principal)
{
	String hash;
	if(LinkSessionFor(request, module, record_id, hash))
		principal.link_hash = hash;
}

int ContentPolicyReadStatus(WebRequest& request, const String& module, const String& record_id,
                            ContentLevel fallback)
{
	ContentPolicyStore& store = ContentPolicyFor(request.Site().data_root);
	if(!store.LoadError().IsEmpty())
		return 503;
	ContentPrincipal who;
	who.authenticated = request.HasUser();
	if(who.authenticated) {
		who.username = request.User().username;
		who.groups <<= request.User().roles;
		for(const String& role : request.User().roles)
			if(role == "admin") {
				who.admin = true;
				break;
		}
	}
	ContentPolicyApplyLinkSession(request, module, record_id, who);
	return ContentVisible(store.Effective(module, record_id, fallback), who) ? 0 : 404;
}

END_UPP_NAMESPACE
