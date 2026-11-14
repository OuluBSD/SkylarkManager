#include "WebEngine.h"
#include "SessionRoutes.h"

NAMESPACE_UPP

WebEngine& TheWebEngine()
{
	static WebEngine x;
	return x;
}

WebEngine::WebEngine()
{
	broker.SetAuditObserver(this);
}

void WebEngine::OnBrokerCall(const String& from_site, const String& to_site,
                             const String& operation, const String& correlation,
                             const String& outcome, int duration_ms)
{
	EngineSite *source = FindSite(from_site);
	EngineSite *target = FindSite(to_site);
	EngineSite *site = source ? source : target;
	if(!site) return;
	WebAuditEvent e;
	e.at = GetSysTime(); e.site = site->id; e.kind = "broker_inprocess";
	e.route = "broker:call"; e.method = "CALL"; e.outcome = outcome;
	e.source_site = source ? source->id : "unregistered";
	e.target_site = target ? target->id : "unregistered";
	e.operation = operation;
	e.correlation_id = correlation; e.duration_ms = duration_ms;
	site->Audit().Record(e);
}

static thread__ int t_listener_port;

int  WebEngine::CurrentListenerPort()          { return t_listener_port; }
void WebEngine::SetCurrentListenerPort(int p)  { t_listener_port = p; }

EngineSite& WebEngine::AddSite(const String& id, const String& title, const String& external_origin,
                               const String& data_root, int session_ttl_hours)
{
	for(int i = 0; i < sites.GetCount(); i++)
		ASSERT_(sites[i]->id != id, "duplicate site id " + id);
	mutex.Enter();
	EngineSite& site = sites.Add().Create();
	site.Init(id, title, external_origin, data_root, session_ttl_hours);
	mutex.Leave();
	return site;
}

void WebEngine::BindPort(int port, const String& site_id)
{
	EngineSite* s = FindSite(site_id);
	ASSERT_(s, "BindPort: unknown site " + site_id);
	ASSERT_(port > 0, "BindPort: invalid port");
	mutex.Enter();
	int idx = -1;
	for(int i = 0; i < sites.GetCount(); i++)
		if(&*sites[i] == s) {
			idx = i;
			break;
		}
	port_site.GetAdd(port) = idx;
	mutex.Leave();
}

void WebEngine::BindDefaultPort(int port, const String& site_id)
{
	EngineSite* s = FindSite(site_id);
	ASSERT_(s, "BindDefaultPort: unknown site " + site_id);
	mutex.Enter();
	for(int i = 0; i < sites.GetCount(); i++)
		if(&*sites[i] == s) {
			default_port_site = i;
			break;
		}
	port_site.GetAdd(port) = default_port_site;
	mutex.Leave();
}

EngineSite* WebEngine::FindSite(const String& id)
{
	mutex.Enter();
	EngineSite* r = NULL;
	for(int i = 0; i < sites.GetCount(); i++)
		if(sites[i]->id == id) {
			r = &*sites[i];
			break;
		}
	mutex.Leave();
	return r;
}

EngineSite* WebEngine::FindSiteByPort(int port)
{
	mutex.Enter();
	EngineSite* r = NULL;
	int idx = -1;
	if(port == 0)
		idx = default_port_site;
	else {
		int q = port_site.Find(port);
		if(q >= 0)
			idx = port_site[q];
	}
	if(idx >= 0 && idx < sites.GetCount())
		r = &*sites[idx];
	mutex.Leave();
	return r;
}

static String OriginAuthorityOf(const Http& http)
{
	String origin = http.GetHeader("origin");
	if(origin.GetCount())
		return origin;
	String referer = http.GetHeader("referer");
	if(!referer.GetCount())
		return String();
	int q = referer.Find("://");
	if(q < 0)
		return String();
	int s = q + 3;
	int e = referer.Find('/', s);
	return referer.Mid(0, e < 0 ? referer.GetCount() : e);
}

Vector<EngineSiteListing> WebEngine::ListSites()
{
	Vector<EngineSiteListing> out;
	for(int i = 0; i < sites.GetCount(); i++) {
		EngineSiteListing l;
		l.id = sites[i]->id;
		l.title = sites[i]->title;
		l.public_url = sites[i]->PublicUrl("/");
		for(int k = 0; k < port_site.GetCount(); k++)
			if(port_site[k] == i) {
				l.port = port_site.GetKey(k);
				break;
			}
		if(l.port)
			out.Add(pick(l));
	}
	return out;
}

void WebEngine::Gateway(Http& http)
{
	uint64 audit_start = msecs();
	// URL authority and origin validation are pinned to the configured public origin.
	EngineSite* site = FindSiteByPort(CurrentListenerPort());
	if(!site) {
		http << "no website is bound to this listener";
		http.Response(404, "Not found");
		return;
	}
	String method = http.GetMethod();

	// The path comes from the raw request target, not from the wildcard arguments,
	// which lose the literal segments of a route they were matched through.
	// Segments are percent-decoded to match what the wildcard arguments carried.
	Vector<String> segs;
	String target = http.GetRequestURI();
	if(target.StartsWith("/")) {
		int q = target.Find('?');
		if(q >= 0)
			target = target.Left(q);
		for(const String& part : Split(target, '/', true))
			segs.Add(UrlDecode(part, false));
	}
	else
		for(int i = 0; i < http.GetParamCount(); i++)
			segs.Add(http[i]);
	String path = "/";
	if(segs.GetCount())
		path << Join(segs, "/");

	// Session resolution (engine-owned; site-stamped). Dispatch stores
	// cookies under their lowercase name prefixed with '@'.
	WebRequest wr(http, *site, method, path, segs);
	String cookie_key = String("@") + site->CookieName();
	String raw_cookie = http[~cookie_key];
	if(raw_cookie.GetCount()) {
		WebAccountSession s;
		bool expired = false;
		if(site->Accounts().ResolveSession(raw_cookie, s, &expired)) {
			wr.session = s;
			wr.has_session = true;
			if(s.user_id.GetCount()) {
				WebAccountUser u;
				if(site->Accounts().GetUser(s.user_id, u) && !u.disabled) {
					wr.user = u;
					wr.has_user = true;
				}
			}
		}
		else {
			WebAuditEvent e; e.at = GetSysTime(); e.site = site->id; e.kind = "session";
			e.route = "session:resolve"; e.method = method; e.outcome = expired ? "expired" : "failed_resolution";
			site->Audit().Record(e);
		}
	}

	// Shared MetaAccount sign-in for a guest (task 039). In-process broker only, and only when enabled.
	if(TheWebEngine().MetaSso())
		SsoAutoSignIn(wr);

	SiteRoute route;
	Vector<String> args;
	bool matched = site->Router().Match(method, segs, args, route);
	String route_id = matched ? WebAuditRouteLabel(route.method, route.pattern) : "UNMATCHED:/";
	bool audit_route = matched && (route.pattern == "api/audit/events" || route.pattern == "api/audit/sessions" ||
	                              route.pattern == "api/audit/health" || route.pattern == "audit");
	auto record_request = [&](int status, const String& outcome) {
		if(audit_route) return;
		WebAuditEvent e;
		e.at = GetSysTime(); e.site = site->id; e.kind = "request"; e.route = route_id;
		e.method = method.GetCount() <= 8 ? method : String("OTHER"); e.outcome = outcome;
		e.status = status; e.duration_ms = min((int)(msecs() - audit_start), 3600000);
		if(wr.HasUser()) e.actor_user_id = wr.User().user_id;
		if(wr.HasSession()) e.session_ref = wr.Session().data.Get("__audit_ref", "");
		site->Audit().Record(e);
		if(matched && method == "POST" && route.pattern.Find("login") >= 0 &&
		   route.pattern != "session/login" && site->id != "meta") {
			WebAuditEvent login = e;
			login.kind = "login";
			login.operation = "password";
			login.outcome = status >= 400 ? "failure" : "success";
			site->Audit().Record(login);
		}
		if(wr.IsMutation()) { e.kind = "action"; site->Audit().Record(e); }
	};

	// Mutation fence: exact origin + server-side CSRF token.
	if(wr.IsMutation()) {
		String authority = OriginAuthorityOf(http);
		if(authority.GetCount() && !site->OriginAllowed(authority)) {
			http << "forbidden: cross-origin mutation";
			http.Response(403, "Forbidden");
			record_request(403, "rejected");
			return;
		}
		if(wr.has_session) {
			String csrf = http["__wf_csrf__"];
			if(csrf.IsEmpty())
				csrf = http.GetHeader("x-wf-csrf");
			if(csrf != wr.session.csrf_token) {
				http << "forbidden: csrf token mismatch";
				http.Response(403, "Forbidden");
				record_request(403, "rejected");
				return;
			}
		}
	}

	if(matched) {
		wr.path_args <<= args;
		route.handler(wr);
		record_request(wr.ResponseStatus(), wr.ResponseStatus() >= 400 ? "error" : "ok");
	}
	else {
		http << "site '" << site->id << "': not found: " << path;
		http.Response(404, "Not found");
		record_request(404, "not_found");
	}
}

END_UPP_NAMESPACE
