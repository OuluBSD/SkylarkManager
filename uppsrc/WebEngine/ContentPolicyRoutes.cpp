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

// A form on the settings page sends back = the page's own address; anything else gets the JSON answer.
static void Done(WebRequest& r, ContentPolicyStore& store, const String& error)
{
	String err;
	bool ok = error.IsEmpty() && store.Save(err);
	if(ok && r.Param("back") == r.Site().Url("policy")) {
		r.Redirect(r.Site().Url("policy"));
		return;
	}
	if(!err.IsEmpty())
		ok = false;
	ValueMap m;
	m.Add("ok", ok);
	if(!ok)
		m.Add("error", error.IsEmpty() ? err : error);
	r.Json(m, ok ? 200 : (err.IsEmpty() ? 400 : 500));
}

static String Esc(const String& s)
{
	return EscapeHtml(s);
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
	Done(r, store, "");
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
	Done(r, store, "");
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
	Done(r, store, "");
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

static String ScopeForm(WebRequest& r, const String& scope, const String& id_label, const ContentPolicy& p,
                        const String& id)
{
	String out;
	out << "<h3>" << Esc(scope) << (id.GetCount() ? " " + Esc(id) : String()) << "</h3>"
	    << "<p>Level: <b>" << Esc(ContentLevelName(p.level).IsEmpty() ? String("(inherits)") : ContentLevelName(p.level)) << "</b></p>";
	String csrf = "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + Esc(r.CsrfToken()) + "\">";
	String back = "<input type=\"hidden\" name=\"back\" value=\"" + Esc(r.Site().Url("policy")) + "\">";
	out << "<form method=\"post\" action=\"" << Esc(r.Site().Url("api/policy/set")) << "\">" << csrf << back
	    << "<input type=\"hidden\" name=\"scope\" value=\"" << Esc(scope) << "\">"
	    << "<input type=\"hidden\" name=\"id\" value=\"" << Esc(id) << "\">"
	    << "<select name=\"level\">"
	    << "<option value=\"public\">public</option><option value=\"protected\">protected</option>"
	    << "<option value=\"private\">private</option><option value=\"public-only\">public-only</option></select>"
	    << "<button>Set level</button></form>";
	out << "<ul>";
	for(int i = 0; i < p.grants.GetCount(); i++) {
		const ContentGrant& g = p.grants[i];
		out << "<li>" << Esc(g.kind) << " "
		    << (g.kind == "link" ? String("(link)") + (g.label.GetCount() ? " " + Esc(g.label) : String()) : Esc(g.value))
		    << (g.revoked ? String(" (revoked)") : String());
		if(!g.revoked)
			out << "<form method=\"post\" action=\"" << Esc(r.Site().Url("api/policy/revoke")) << "\" style=\"display:inline\">"
			    << csrf << back
			    << "<input type=\"hidden\" name=\"scope\" value=\"" << Esc(scope) << "\">"
			    << "<input type=\"hidden\" name=\"id\" value=\"" << Esc(id) << "\">"
			    << "<input type=\"hidden\" name=\"index\" value=\"" << i << "\"><button>Revoke</button></form>";
		out << "</li>";
	}
	out << "</ul>";
	return out;
}

static void PolicyPage(WebRequest& r)
{
	if(!PolicyAdmin(r)) {
		r.Status(r.HasUser() ? 403 : 401, r.HasUser() ? "content policy is for admins" : "sign in to change content policy");
		return;
	}
	ContentPolicyStore& store = ContentPolicyFor(r.Site().data_root);
	String csrf = "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + Esc(r.CsrfToken()) + "\">";
	String back = "<input type=\"hidden\" name=\"back\" value=\"" + Esc(r.Site().Url("policy")) + "\">";
	String body;
	body << "<h2>Content visibility</h2>"
	     << "<p>The site policy applies to everything without a module or record policy. Public content is visible to everyone; protected content needs a grant or a secret link; private content needs a grant; public-only content is hidden from signed-in users.</p>"
	     << ScopeForm(r, "site", "", store.Site(), "");
	body << "<h3>Module</h3><form method=\"get\" action=\"" << Esc(r.Site().Url("policy")) << "\"><input name=\"module\" placeholder=\"module name\"><button>Show</button></form>";
	String module = r.Param("module");
	if(module.GetCount())
		body << ScopeForm(r, "module", "", store.Module(module), module);
	body << "<h3>Record</h3><form method=\"get\" action=\"" << Esc(r.Site().Url("policy")) << "\"><input name=\"record\" placeholder=\"record id\"><button>Show</button></form>";
	String record = r.Param("record");
	if(record.GetCount())
		body << ScopeForm(r, "record", "", store.Record(record), record);
	body << "<h3>Secret link</h3><p>The token is shown once, in the answer to the request. Only its hash is kept.</p>"
	     << "<form method=\"post\" action=\"" << Esc(r.Site().Url("api/policy/link")) << "\">" << csrf
	     << "<input type=\"hidden\" name=\"scope\" value=\"site\">"
	     << "<input name=\"label\" placeholder=\"label\"><button>Create link</button></form>";
	body << "<h3>Grant</h3><form method=\"post\" action=\"" << Esc(r.Site().Url("api/policy/grant")) << "\">" << csrf << back
	     << "<select name=\"kind\"><option value=\"user\">user</option><option value=\"group\">group (role)</option></select>"
	     << "<input type=\"hidden\" name=\"scope\" value=\"site\">"
	     << "<input name=\"value\" placeholder=\"username or role\"><button>Grant</button></form>";
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><title>Content policy</title></head><body>"
	       "<main>" + body + "</main></body></html>");
}

void InstallContentPolicyRoutes(EngineSite& site)
{
	site.Router().Add("policy", PolicyPage);
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
