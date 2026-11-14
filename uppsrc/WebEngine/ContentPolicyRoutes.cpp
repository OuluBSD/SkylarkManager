#include "WebEngine.h"
#include <WebAuth/WebAuth.h>

NAMESPACE_UPP

static VectorMap<String, bool> s_policy_secret_links;

static bool PolicySecretLinksEnabled(const WebRequest& r)
{
	int i = s_policy_secret_links.Find(r.Site().id);
	return i >= 0 && s_policy_secret_links[i];
}

// The content policy settings for a site (task 053): an admin page and the same actions as manifest commands.
// Only admins change policy. Secret links travel in URL fragments, then exchange by POST for scoped cookies.

static bool PolicyAdmin(WebRequest& r)
{
	if(!r.HasUser())
		return false;
	for(const String& role : r.User().roles)
		if(role == "admin")
			return true;
	return false;
}

// A policy parameter comes from the query or the form, or else from a JSON body (skylarkctl sends JSON).
static String PolicyParam(WebRequest& r, const char *name)
{
	String v = r.Param(name);
	if(v.GetCount() || r.Body().IsEmpty())
		return v;
	Value parsed;
	try {
		parsed = ParseJSON(r.Body());
	}
	catch(Exc&) {
		return String();
	}
	if(!parsed.Is<ValueMap>())
		return String();
	Value x = ValueMap(parsed)[name];
	if(x.Is<String>())
		return (String)x;
	if(x.Is<int>() || x.Is<int64>() || x.Is<double>())
		return AsString(x);
	return String();
}

static void PolicyDenied(WebRequest& r)
{
	ValueMap m;
	m.Add("ok", false);
	m.Add("error", r.HasUser() ? "content policy is for admins" : "sign in to change content policy");
	r.Json(m, r.HasUser() ? 403 : 401);
}

static bool PolicyStoreReady(WebRequest& r, ContentPolicyStore& store)
{
	if(store.LoadError().IsEmpty())
		return true;
	ValueMap m;
	m.Add("ok", false);
	m.Add("error", String("content policy could not be loaded; repair ") +
	      AppendFileName(r.Site().data_root, "content_policy.json") + ": " + store.LoadError());
	r.Json(m, 503);
	return false;
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
	if(!PolicyStoreReady(r, store)) return;
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
	if(!PolicyStoreReady(r, store)) return;
	String scope = PolicyParam(r, "scope");
	String id = PolicyParam(r, "id");
	ContentLevel level;
	ContentPolicy p;
	if(!ScopeGet(store, scope, id, p) || !ContentLevelParse(PolicyParam(r, "level"), level)) {
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
	if(!PolicyStoreReady(r, store)) return;
	String scope = PolicyParam(r, "scope");
	String id = PolicyParam(r, "id");
	String kind = PolicyParam(r, "kind");
	String value = PolicyParam(r, "value");
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
	if(!PolicyStoreReady(r, store)) return;
	String scope = PolicyParam(r, "scope");
	String id = PolicyParam(r, "id");
	int index = ScanInt(PolicyParam(r, "index"));
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

static bool ValidPolicyTarget(const String& target);

static void PolicyLink(WebRequest& r)
{
	if(!PolicyAdmin(r)) {
		PolicyDenied(r);
		return;
	}
	if(!r.IsSecureTransport()) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "secret links require verified HTTPS transport");
		r.Json(m, 403);
		return;
	}
	ContentPolicyStore& store = ContentPolicyFor(r.Site().data_root);
	if(!PolicyStoreReady(r, store)) return;
	String scope = PolicyParam(r, "scope");
	String id = PolicyParam(r, "id");
	String target = PolicyParam(r, "target");
	ContentPolicy p;
	if(!ScopeGet(store, scope, id, p) || p.level != CONTENT_PROTECTED || !ValidPolicyTarget(target)) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "secret links require an explicitly protected scope and a safe local target path");
		r.Json(m, 400);
		return;
	}
	// The token is returned once as a fragment; browsers never send fragments in HTTP requests.
	String token = WebAuthRandomBytes(24, true);
	ContentGrant g;
	g.kind = "link";
	g.value = ContentLinkHash(token);
	g.label = PolicyParam(r, "label");
	p.grants.Add(pick(g));
	ScopeSet(store, scope, id, p);
	String err;
	if(!store.Save(err)) {
		p.grants.Remove(p.grants.GetCount() - 1);
		ScopeSet(store, scope, id, p);
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", err);
		r.Json(m, 500);
		return;
	}
	ValueMap m;
	m.Add("ok", true);
	m.Add("access_path", "/policy-access");
	m.Add("fragment", "wfcp=" + token + "&to=" + target);
	r.SetHeader("Cache-Control", "no-store").SetHeader("Referrer-Policy", "no-referrer");
	r.Json(m);
}

static bool ValidPolicyTarget(const String& target)
{
	if(target.GetCount() < 2 || target.GetCount() > 2048 || target[0] != '/' || target[1] == '/')
		return false;
	String segment;
	for(int i = 0; i <= target.GetCount(); i++) {
		int c = i < target.GetCount() ? (byte)target[i] : '/';
		if(c == '/') {
			if(segment == "." || segment == "..")
				return false;
			segment.Clear();
			continue;
		}
		if(!(IsAlpha(c) || IsDigit(c) || c == '-' || c == '_' || c == '.' || c == '~'))
			return false;
		segment.Cat(c);
	}
	return true;
}

static void PolicyAccessPage(WebRequest& r)
{
	if(!r.IsSecureTransport()) {
		r.Status(403, "secret links require verified HTTPS transport");
		return;
	}
	r.EnsureSession();
	r.SetHeader("Cache-Control", "no-store")
	 .SetHeader("Referrer-Policy", "no-referrer")
	 .SetHeader("Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
 String csrf = Esc(r.CsrfToken());
	String html;
	html << "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"referrer\" content=\"no-referrer\"><title>Opening protected link</title></head>"
	     << "<body><p id=\"status\">Opening protected content...</p><script>"
	     << "(function(){var status=document.getElementById('status');var f=location.hash.slice(1);"
	     << "history.replaceState(null,'',location.pathname);"
	     << "var p=new URLSearchParams(f),token=p.get('wfcp'),target=p.get('to');"
	     << "if(!token||!target){status.textContent='This protected link is invalid.';return;}"
	     << "fetch('/api/policy/exchange',{method:'POST',credentials:'same-origin',referrerPolicy:'no-referrer',"
	     << "headers:{'Content-Type':'application/json','X-WF-CSRF':'" << csrf << "'},"
	     << "body:JSON.stringify({token:token,target:target})}).then(function(r){return r.json().then(function(v){return {ok:r.ok,value:v};});})"
	     << ".then(function(result){if(result.ok&&result.value.ok){location.replace(target);return;}"
	     << "status.textContent='This protected link is invalid or expired.';})"
	     << ".catch(function(){status.textContent='The protected link could not be opened.';});})();"
	     << "</script></body></html>";
	r.Html(html);
}

static void PolicyAccessExchange(WebRequest& r)
{
	r.SetHeader("Cache-Control", "no-store").SetHeader("Referrer-Policy", "no-referrer");
	if(!r.IsSecureTransport()) {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "secret links require verified HTTPS transport");
		r.Json(m, 403);
		return;
	}
	ContentPolicyStore& store = ContentPolicyFor(r.Site().data_root);
	if(!PolicyStoreReady(r, store))
		return;
	Value parsed;
	try {
		parsed = ParseJSON(r.Body());
	}
	catch(Exc&) {
		parsed = Null;
	}
	auto InvalidLink = [&]() {
		ValueMap m;
		m.Add("ok", false);
		m.Add("error", "secret link is invalid or expired");
		r.Json(m, 404);
	};
	if(!parsed.Is<ValueMap>()) {
		InvalidLink();
		return;
	}
	ValueMap body = parsed;
	if(body.GetCount() != 2 || body.Find("token") < 0 || body.Find("target") < 0 ||
	   !body.Get("token").Is<String>() || !body.Get("target").Is<String>()) {
		InvalidLink();
		return;
	}
	String token = body.Get("token");
	String target = body.Get("target");
	String scope, id, hash;
	if(!ValidPolicyTarget(target) || !store.FindLinkGrant(token, scope, id, hash) ||
	   !CreateContentPolicyLinkSession(r, scope, id, hash)) {
		InvalidLink();
		return;
	}
	ValueMap response;
	response.Add("ok", true);
	r.Json(response);
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
	bool secret_links_configured = PolicySecretLinksEnabled(r);
	bool allow_secret_links = secret_links_configured && r.IsSecureTransport();
	if(!PolicyAdmin(r)) {
		r.Status(r.HasUser() ? 403 : 401, r.HasUser() ? "content policy is for admins" : "sign in to change content policy");
		return;
	}
	ContentPolicyStore& store = ContentPolicyFor(r.Site().data_root);
	if(!store.LoadError().IsEmpty()) {
		r.Status(503, String("content policy could not be loaded; repair ") +
		         AppendFileName(r.Site().data_root, "content_policy.json") + ": " + store.LoadError());
		return;
	}
	String csrf = "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + Esc(r.CsrfToken()) + "\">";
	String back = "<input type=\"hidden\" name=\"back\" value=\"" + Esc(r.Site().Url("policy")) + "\">";
	String body;
	body << "<h2>Content visibility</h2>"
	     << (allow_secret_links
	         ? String("<p>The site policy applies to everything without a module or record policy. Public content is visible to everyone; protected content needs a grant or a secret link; private content needs a grant; public-only content is hidden from signed-in users.</p>")
	         : secret_links_configured
	           ? String("<p>Secret-link controls require verified HTTPS transport. Public content is visible to everyone; protected/private content needs a named user or group grant; public-only content is hidden from signed-in users.</p>")
	           : String("<p>The site policy applies to everything without a module or record policy. Public content is visible to everyone; protected/private content needs a named user or group grant; public-only content is hidden from signed-in users. Secret links are disabled on this site.</p>"))
	     << ScopeForm(r, "site", "", store.Site(), "");
	body << "<h3>Module</h3><form method=\"get\" action=\"" << Esc(r.Site().Url("policy")) << "\"><input name=\"module\" placeholder=\"module name\"><button>Show</button></form>";
	String module = PolicyParam(r, "module");
	if(module.GetCount())
		body << ScopeForm(r, "module", "", store.Module(module), module);
	body << "<h3>Record</h3><form method=\"get\" action=\"" << Esc(r.Site().Url("policy")) << "\"><input name=\"record\" placeholder=\"record id\"><button>Show</button></form>";
	String record = PolicyParam(r, "record");
	if(record.GetCount())
		body << ScopeForm(r, "record", "", store.Record(record), record);
	if(allow_secret_links)
		body << "<h3>Secret link</h3><p>Creates a protected link shown once as a URL fragment. Browsers never send the fragment in the HTTP request; the server stores only its keyed hash.</p>"
		     << "<form id=\"policy-link-form\" method=\"post\" action=\"" << Esc(r.Site().Url("api/policy/link")) << "\">" << csrf
		     << "<select name=\"scope\"><option value=\"site\">site</option><option value=\"module\">module</option><option value=\"record\">record</option></select>"
		     << "<input name=\"id\" placeholder=\"module or record id\"><input name=\"target\" placeholder=\"/papers/1234.56789\" required>"
		     << "<input name=\"label\" placeholder=\"label\"><button>Create link</button></form>"
		     << "<output id=\"policy-link-output\" aria-live=\"polite\"></output>"
		     << "<script>(function(){var f=document.getElementById('policy-link-form');if(!f)return;"
		     << "f.addEventListener('submit',function(e){e.preventDefault();var out=document.getElementById('policy-link-output');"
		     << "fetch(f.action,{method:'POST',credentials:'same-origin',referrerPolicy:'no-referrer',"
		     << "headers:{'Content-Type':'application/x-www-form-urlencoded','X-WF-CSRF':f.elements['__wf_csrf__'].value},"
		     << "body:new URLSearchParams(new FormData(f))}).then(function(r){return r.json();}).then(function(m){"
		     << "if(!m.ok){out.textContent=m.error||'Link creation failed.';return;}"
		     << "var u=new URL(m.access_path,location.origin);u.hash=m.fragment;out.textContent=u.href;})"
		     << ".catch(function(){out.textContent='Link creation failed.';});});})();</script>";
	body << "<h3>Grant</h3><form method=\"post\" action=\"" << Esc(r.Site().Url("api/policy/grant")) << "\">" << csrf << back
	     << "<select name=\"kind\"><option value=\"user\">user</option><option value=\"group\">group (role)</option></select>"
	     << "<input type=\"hidden\" name=\"scope\" value=\"site\">"
	     << "<input name=\"value\" placeholder=\"username or role\"><button>Grant</button></form>";
	r.SetHeader("Cache-Control", "no-store").SetHeader("Referrer-Policy", "no-referrer");
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"referrer\" content=\"no-referrer\"><title>Content policy</title></head><body>"
	       "<main>" + body + "</main></body></html>");
}

void InstallContentPolicyRoutes(EngineSite& site, bool allow_secret_links)
{
	int i = s_policy_secret_links.Find(site.id);
	if(i < 0)
		s_policy_secret_links.Add(site.id, allow_secret_links);
	else
		s_policy_secret_links[i] = allow_secret_links;
	site.Router().Add("policy", PolicyPage);
	site.Router().Add("api/policy", PolicyGet);
	site.Router().Add("api/policy/set:POST", PolicySet);
	site.Router().Add("api/policy/grant:POST", PolicyGrant);
	site.Router().Add("api/policy/revoke:POST", PolicyRevoke);
	if(allow_secret_links) {
		site.Router().Add("api/policy/link:POST", PolicyLink);
		site.Router().Add("policy-access", PolicyAccessPage);
		site.Router().Add("api/policy/exchange:POST", PolicyAccessExchange);
	}

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
	if(allow_secret_links)
		cm.Add(site.id, site.title, HttpCommand("policy-link", "POST", "api/policy/link", "admin",
		                                        "json", "Create a protected-scope fragment link; token is shown once and exchanged by POST",
		                                        {"scope:string:required", "id:string", "label:string", "target:string:required"}));
}

END_UPP_NAMESPACE
