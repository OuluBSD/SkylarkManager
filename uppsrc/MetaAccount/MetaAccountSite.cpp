#include "MetaAccountSite.h"

NAMESPACE_UPP

// One meta-account site per process (mirroring the engine/skylark app
// singletons): the grants store is file-scoped and set at install time.
static WebGrants *s_grants = NULL;

static WebGrants& Grants()
{
	ASSERT_(s_grants, "meta-account site not installed");
	return *s_grants;
}

// ---------------------------------------------------------------------------
// HTML helpers (minimal, functional pages until the Witz work package)
// ---------------------------------------------------------------------------

static ValueMap JsonOk(bool ok, const String& err = String())
{
	ValueMap m;
	m.Add("ok", ok);
	if(err.GetCount())
		m.Add("error", err);
	return m;
}

static void HtmlPage(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\">"
	       "<title>" + title + "</title></head><body>"
	       "<h1>" + title + "</h1>" + body + "</body></html>");
}

static String HtmlForm(const String& action, const String& csrf, const String& fields)
{
	return "<form method=\"post\" action=\"" + action + "\">"
	       "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + csrf + "\">"
	       + fields + "<input type=\"submit\" value=\"OK\"></form>";
}

// ---------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------

static void MetaHome(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Redirect(r.Site().Url("login"));
		return;
	}
	String body = "<p>Signed in as <b>" + r.User().username + "</b> ("
	            + r.User().user_id + ")</p>"
	            + "<p>Two-factor: " + (r.User().totp_enrolled ? String("enrolled") : String("not enrolled")) + "</p>"
	            + "<p><a href=\"" + r.Site().Url("consent") + "\">Consent grants</a></p>";
	HtmlPage(r, "Meta account", body);
}

static void MetaLoginForm(WebRequest& r)
{
	r.EnsureSession();
	String body = HtmlForm(r.Site().Url("login:POST"), r.CsrfToken(),
		"<input name=\"username\" placeholder=\"username\"><br>"
		"<input name=\"password\" type=\"password\"><br>"
		"<input name=\"code\" placeholder=\"2FA code (if enrolled)\"><br>");
	HtmlPage(r, "Sign in", body);
}

static void MetaLoginSubmit(WebRequest& r)
{
	String username = r.Param("username");
	String password = r.Param("password");
	String code = r.Param("code");
	r.DropSession();
	WebAccountUser u;
	if(!r.Site().Accounts().VerifyPassword(username, password, u).ok) {
		r.Json(JsonOk(false, "bad credentials"), 401);
		return;
	}
	// Enrolled accounts must present a live TOTP code or a recovery code.
	if(u.totp_enrolled && !r.Site().Accounts().VerifyTotpOrRecovery(u.user_id, code)) {
		r.Json(JsonOk(false, "second factor required"), 401);
		return;
	}
	String sid, csrf, err;
	if(!r.Site().Accounts().Login(username, password, r.ClientIp(), sid, csrf, u, err).ok) {
		r.Json(JsonOk(false, err), 401);
		return;
	}
	r.AdoptSession(sid);
	ValueMap m;
	m.Add("ok", true);
	m.Add("user", u.username);
	m.Add("user_id", u.user_id);
	m.Add("totp_enrolled", u.totp_enrolled);
	m.Add("csrf", r.CsrfToken());
	r.Json(m);
}

static void MetaLogout(WebRequest& r)
{
	r.DropSession();
	r.Json(JsonOk(true));
}

static void MetaTotpBegin(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	String secret;
	Vector<String> recovery;
	StoreResult st = r.Site().Accounts().BeginTotpEnrollment(r.User().user_id, r.Param("password"),
	                                                         secret, recovery);
	if(!st.ok) {
		r.Json(JsonOk(false, st.error), 403);
		return;
	}
	ValueArray codes;
	for(const String& c : recovery)
		codes.Add(c);
	ValueMap m;
	m.Add("ok", true);
	m.Add("secret", secret);
	m.Add("recovery", codes);
	m.Add("hint", "confirm with a live code from your authenticator");
	// Recovery codes are shown exactly once, at enrollment.
	r.Json(m);
}

static void MetaTotpConfirm(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	StoreResult st = r.Site().Accounts().ConfirmTotpEnrollment(r.User().user_id, r.Param("code"));
	if(!st.ok) {
		r.Json(JsonOk(false, st.error), 403);
		return;
	}
	r.Json(JsonOk(true));
}

static void MetaTotpDisable(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	StoreResult st = r.Site().Accounts().DisableTotp(r.User().user_id, r.Param("password"));
	if(!st.ok) {
		r.Json(JsonOk(false, st.error), 403);
		return;
	}
	r.Json(JsonOk(true));
}

static void MetaConsentList(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Redirect(r.Site().Url("login"));
		return;
	}
	Vector<WebAccountGrant> all;
	Grants().ListForSubject(r.User().user_id, "", all);
	String body;
	body << "<table border=\"1\"><tr><th>client</th><th>scopes</th><th>state</th><th></th></tr>";
	for(const WebAccountGrant& g : all) {
		body << "<tr><td>" + g.client_site + "/" + g.client_id + "</td><td>"
		   << Join(g.scopes, ", ") << "</td><td>" << g.state << "</td><td>";
		if(g.state == "requested")
			body << HtmlForm(r.Site().Url("consent/decide:POST"), r.CsrfToken(),
			                 "<input type=\"hidden\" name=\"grant_id\" value=\"" + g.grant_id + "\">"
			                 "<input type=\"hidden\" name=\"approve\" value=\"1\">"
			                 "<input type=\"submit\" value=\"Approve\">")
			    << " "
			    << HtmlForm(r.Site().Url("consent/decide:POST"), r.CsrfToken(),
			                "<input type=\"hidden\" name=\"grant_id\" value=\"" + g.grant_id + "\">"
			                "<input type=\"submit\" value=\"Deny\">");
		else if(g.IsActive())
			body << HtmlForm(r.Site().Url("consent/decide:POST"), r.CsrfToken(),
			                 "<input type=\"hidden\" name=\"grant_id\" value=\"" + g.grant_id + "\">"
			                 "<input type=\"hidden\" name=\"approve\" value=\"0\">"
			                 "<input type=\"submit\" value=\"Revoke\">");
		body << "</td></tr>";
	}
	body << "</table>";
	HtmlPage(r, "Consent grants", body);
}

static void MetaConsentDecide(WebRequest& r)
{
	if(!r.HasUser()) {
		r.Json(JsonOk(false, "sign in first"), 401);
		return;
	}
	String grant_id = r.Param("grant_id");
	bool approve = r.Param("approve") == "1";
	WebAccountGrant g;
	if(grant_id.IsEmpty() || !Grants().GetGrant(grant_id, g)) {
		r.Json(JsonOk(false, "unknown grant"), 404);
		return;
	}
	// The decider must be the grant's subject.
	if(g.subject_user_id != r.User().user_id) {
		r.Json(JsonOk(false, "not your grant"), 403);
		return;
	}
	// Approve/deny only pending requests; revoke only active grants.
	StoreResult st = approve ? Grants().Decide(grant_id, true) : Grants().Revoke(grant_id);
	if(!st.ok) {
		r.Json(JsonOk(false, st.error), 409);
		return;
	}
	r.Json(JsonOk(true));
}

void InstallMetaAccountSite(EngineSite& site, WebGrants& grants)
{
	s_grants = &grants;
	SiteRouter& rt = site.Router();
	rt.Add("", MetaHome);
	rt.Add("login", MetaLoginForm);
	rt.Add("login:POST", MetaLoginSubmit);
	rt.Add("logout:POST", MetaLogout);
	rt.Add("totp/begin:POST", MetaTotpBegin);
	rt.Add("totp/confirm:POST", MetaTotpConfirm);
	rt.Add("totp/disable:POST", MetaTotpDisable);
	rt.Add("consent", MetaConsentList);
	rt.Add("consent/decide:POST", MetaConsentDecide);
}

// ---------------------------------------------------------------------------
// Broker service
// ---------------------------------------------------------------------------

BrokerResult MetaAccountService::Handle(const BrokerCallContext& ctx, const BrokerCall& call)
{
	BrokerResult r;
	r.status = 400;
	if(call.op == "ping") {
		r.ok = true;
		r.status = 200;
		r.payload.Add("pong", true);
		r.payload.Add("site", "meta");
		return r;
	}
	if(call.op == "authenticate") {
		String username = call.payload.Get("username");
		String password = call.payload.Get("password");
		String code = call.payload.Get("totp_code");
		WebAccountUser u;
		StoreResult vr = accounts.VerifyPassword(username, password, u);
		if(!vr.ok) {
			r.status = 401;
			r.error = vr.error;
			return r;
		}
		if(u.totp_enrolled && !accounts.VerifyTotpOrRecovery(u.user_id, code)) {
			r.status = 401;
			r.error = "second factor required";
			return r;
		}
		r.ok = true;
		r.status = 200;
		r.payload.Add("user_id", u.user_id);
		r.payload.Add("username", u.username);
		return r;
	}
	if(call.op == "request-consent") {
		String subject = call.payload.Get("subject_user_id");
		String client_site = call.payload.Get("client_site");
		String client_id = call.payload.Get("client_id");
		Vector<String> scopes;
		Value sv = call.payload.Get("scopes");
		if(sv.Is<ValueArray>())
			for(const Value& s : (ValueArray)sv)
				scopes.Add((String)s);
		String grant_id;
		bool existed = false;
		StoreResult st = grants.RequestGrant(subject, client_site, client_id, scopes, grant_id, existed);
		if(!st.ok) {
			r.status = 400;
			r.error = st.error;
			return r;
		}
		r.ok = true;
		r.status = 200;
		r.payload.Add("grant_id", grant_id);
		r.payload.Add("existed", existed);
		return r;
	}
	if(call.op == "check-grant") {
		String subject = call.payload.Get("subject_user_id");
		String client_site = call.payload.Get("client_site");
		Vector<String> scopes;
		Value sv = call.payload.Get("scopes");
		if(sv.Is<ValueArray>())
			for(const Value& s : (ValueArray)sv)
				scopes.Add((String)s);
		WebAccountGrant g;
		if(!grants.FindActive(subject, client_site, scopes, g)) {
			r.ok = true;
			r.status = 200;
			r.payload.Add("active", false);
			return r;
		}
		r.ok = true;
		r.status = 200;
		r.payload.Add("active", true);
		r.payload.Add("grant_id", g.grant_id);
		return r;
	}
	if(call.op == "revoke-grant") {
		StoreResult st = grants.Revoke(call.payload.Get("grant_id"));
		if(!st.ok) {
			r.status = 409;
			r.error = st.error;
			return r;
		}
		r.ok = true;
		r.status = 200;
		return r;
	}
	r.status = 404;
	r.error = "unknown op";
	return r;
}

void RegisterMetaAccountService(const String& meta_site_id, SiteAccounts& accounts, WebGrants& grants)
{
	TheWebEngine().Broker().Register(meta_site_id, new MetaAccountService(accounts, grants));
}

END_UPP_NAMESPACE
