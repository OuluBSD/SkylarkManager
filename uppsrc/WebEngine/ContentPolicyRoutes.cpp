#include "WebEngine.h"
#include <WebAuth/WebAuth.h>

NAMESPACE_UPP

// The content policy settings for a site (task 053): an admin page and the same actions as manifest commands.
// Only admins change a policy. A link token is shown once, when it is created, and is kept only as its hash.

static bool PolicyAdmin(WebRequest& r)
{
	if(!r.HasUser())
		return false;
	for(const String& role : r.User().roles)
		if(role == "admin")
			return true;
	return false;
}

static void PolicyDenied(WebRequest& r)
{
	ValueMap m;
	m.Add("ok", false);
	m.Add("error", r.HasUser() ? "content policy is for admins" : "sign in to change content policy");
	r.Json(m, r.HasUser() ? 403 : 401);
}

// The policy a scope names. The site policy has no id; a module or record policy is found by id.
static bool ScopeGet(ContentPolicyStore& store, const String& scope, const String& id, ContentPolicy& p)
{
	if(scope == "site") { p = store.Site(); return true; }
	if(scope == "module" && id.GetCount()) { p = store.Module(id); return true; }
	if(scope == "record" && id.GetCount()) { p = store.Record(id); return true; }
	return false;
}

static void ScopeSet(ContentPolicyStore& store, const String& scope, const String& id, const ContentPolicy& p)
{
	if(scope == "site") store.Site() = p;
	else if(scope == "module") store.SetModule(id, p);
	else store.SetRecord(id, p);
}

static ValueMap PolicyValue(const ContentPolicy& p)
{
	// Link grants show their label and revocation, never their stored hash.
	ValueMap m;
	m.Add("level", ContentLevelName(p.level));
	ValueArray arr;
	for(int i = 0; i < p.grants.GetCount(); i++) {
		const ContentGrant& g = p.grants[i];
		ValueMap gm;
		gm.Add("index", i);
		gm.Add("kind", g.kind);
		gm.Add("value", g.kind == "link" ? String("(link)") : g.value);
		gm.Add("label", g.label);
		gm.Add("revoked", g.revoked);
		arr.Add(gm);
	}
	m.Add("grants", arr);
	return m;
}

static void PolicyGet(WebRequest& r)
{
	if(!PolicyAdmin(r)) {
		PolicyDenied(r);
		return;
	}
	ContentPolicyStore& store = ContentPolicyFor(r.Site().data_root);
	ValueMap m;
	m.Add("ok", true);
	m.Add("site", PolicyValue(store.Site()));
	r.Json(m);
}

static void PolicySet(WebRequest& r)
{
	if(!PolicyAdmin(r)) {
		PolicyDenied(r);
		return;
	}
	ContentPolicyStore& store = ContentPolicyFor(r.Site().data_root);
	String scope = r.Param("scope");
	String id = r.Param("id");
	ContentLevel level;
	ContentPolicy p;
	if(!ScopeGet(store, scope, id, p) || !ContentLevelParse(r.Param("level"), level)) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "scope must be site, module or record, with an id for module and record, and level must be public, protected, private or public-only");
		r.Json(m, 400);
		return;
	}
	p.level = level;
	ScopeSet(store, scope, id, p);
	String err;
	ValueMap m;
	m.Add("ok", store.Save(err));
	if(!err.IsEmpty())
		m.Add("error", err);
	r.Json(m, err.IsEmpty() ? 200 : 500);
}

static void PolicyGrant(WebRequest& r)
{
	if(!PolicyAdmin(r)) {
		PolicyDenied(r);
		return;
	}
	ContentPolicyStore& store = ContentPolicyFor(r.Site().data_root);
	String scope = r.Param("scope");
	String id = r.Param("id");
	String kind = r.Param("kind");
	String value = r.Param("value");
	ContentPolicy p;
	if(!ScopeGet(store, scope, id, p) || (kind != "user" && kind != "group") || value.IsEmpty()) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "a grant needs a scope, kind user or group, and a value");
		r.Json(m, 400);
		return;
	}
	ContentGrant g;
	g.kind = kind;
	g.value = value;
	p.grants.Add(pick(g));
	ScopeSet(store, scope, id, p);
	String err;
	ValueMap m;
	m.Add("ok", store.Save(err));
	if(!err.IsEmpty())
		m.Add("error", err);
	r.Json(m, err.IsEmpty() ? 200 : 500);
}

static void PolicyRevoke(WebRequest& r)
{
	if(!PolicyAdmin(r)) {
		PolicyDenied(r);
		return;
	}
	ContentPolicyStore& store = ContentPolicyFor(r.Site().data_root);
	String scope = r.Param("scope");
	String id = r.Param("id");
	int index = ScanInt(r.Param("index"));
	ContentPolicy p;
	if(!ScopeGet(store, scope, id, p) || index < 0 || index >= p.grants.GetCount()) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "no grant at that index");
		r.Json(m, 400);
		return;
	}
	p.grants[index].revoked = true;
	ScopeSet(store, scope, id, p);
	String err;
	ValueMap m;
	m.Add("ok", store.Save(err));
	if(!err.IsEmpty())
		m.Add("error", err);
	r.Json(m, err.IsEmpty() ? 200 : 500);
}

static void PolicyLink(WebRequest& r)
{
	if(!PolicyAdmin(r)) {
		PolicyDenied(r);
		return;
	}
	ContentPolicyStore& store = ContentPolicyFor(r.Site().data_root);
	String scope = r.Param("scope");
	String id = r.Param("id");
	ContentPolicy p;
	if(!ScopeGet(store, scope, id, p)) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "scope must be site, module or record, with an id for module and record");
		r.Json(m, 400);
		return;
	}
	// The token is random and shown once. Only its keyed hash is stored.
	String token = WebAuthRandomBytes(24, true);
	ContentGrant g;
	g.kind = "link";
	g.value = ContentLinkHash(token);
	g.label = r.Param("label");
	p.grants.Add(pick(g));
	ScopeSet(store, scope, id, p);
	String err;
	ValueMap m;
	m.Add("ok", store.Save(err));
	m.Add("token", token);
	m.Add("path", "?key=" + token);
	if(!err.IsEmpty())
		m.Add("error", err);
	r.Json(m, err.IsEmpty() ? 200 : 500);
}

void InstallContentPolicyRoutes(EngineSite& site)
{
	site.Router().Add("api/policy", PolicyGet);
	site.Router().Add("api/policy/set:POST", PolicySet);
	site.Router().Add("api/policy/grant:POST", PolicyGrant);
	site.Router().Add("api/policy/revoke:POST", PolicyRevoke);
	site.Router().Add("api/policy/link:POST", PolicyLink);

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("policy-show", "GET", "api/policy", "admin",
	                                        "json", "Show the site content policy (no link hashes)", {}));
	cm.Add(site.id, site.title, HttpCommand("policy-set", "POST", "api/policy/set", "admin",
	                                        "json", "Set a site, module or record level",
	                                        {"scope:string:required", "id:string", "level:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("policy-grant", "POST", "api/policy/grant", "admin",
	                                        "json", "Grant a user or group access to protected or private content",
	                                        {"scope:string:required", "id:string", "kind:string:required", "value:string:required"}));
	cm.Add(site.id, site.title, HttpCommand("policy-revoke", "POST", "api/policy/revoke", "admin",
	                                        "json", "Revoke a grant by its index",
	                                        {"scope:string:required", "id:string", "index:int:required"}));
	cm.Add(site.id, site.title, HttpCommand("policy-link", "POST", "api/policy/link", "admin",
	                                        "json", "Create a secret link for protected content; the token is shown once",
	                                        {"scope:string:required", "id:string", "label:string"}));
}

END_UPP_NAMESPACE
