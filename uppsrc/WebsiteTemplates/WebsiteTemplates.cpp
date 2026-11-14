#include "WebsiteTemplates.h"

NAMESPACE_UPP

static String MetaLoginCss()
{
	return R"CSS(body { margin: 0; min-height: 100vh; box-sizing: border-box; padding: 16px; background: #1d015a; color: rgba(255,255,255,.6); font-family: system-ui,-apple-system,"Segoe UI",sans-serif; display: flex; align-items: center; justify-content: center; }
a { color: #7f71fe; text-decoration: none; cursor: pointer; }
.m-container { width: 100%; max-width: 375px; box-sizing: border-box; padding: 30px 0; border-radius: 16px; border: 4px solid rgba(255,255,255,.1); background: linear-gradient(0deg,rgba(4,158,116,.3),rgba(255,255,215,.02)); box-shadow: 0 32px 47px rgba(32,23,23,.09); }
.m-form { width: 100%; padding: 0 30px; box-sizing: border-box; }
.m-form_title { color: #fff; font-size: 26px; font-weight: 500; margin: 0 0 20px; }
.m-form_group { display: flex; flex-wrap: wrap; justify-content: space-between; padding-bottom: 18px; }
.m-form_label { display: block; margin-bottom: 10px; font-size: 15px; }
.m-form_input { width: 100%; appearance: none; outline: 0; line-height: 44px; border: 1px solid rgba(255,255,255,.09); background-color: rgba(255,255,255,.04); border-radius: 3px; padding: 0 15px; font-size: 18px; font-family: inherit; box-sizing: border-box; color: #fff; font-weight: 300; }
.m-form_input:focus { border-color: #7f71fe; }
.m-form_btn { appearance: none; outline: 0; background-color: #7f71fe; border: 0; margin-top: 15px; line-height: 44px; padding: 0 15px; color: #fff; font-weight: 500; border-radius: 3px; width: 100%; cursor: pointer; font-size: 18px; font-family: inherit; }
.m-form_link { text-align: center; }
.m-form_btn-register { display: block; line-height: 44px; color: rgba(255,255,255,.35); cursor: not-allowed; }
.m-form_error { margin: 0 0 18px; padding: 8px 12px; border: 1px solid rgba(255,140,140,.5); border-radius: 3px; color: #ffb8b8; })CSS";
}

static String GlassmorphismCss()
{
	return R"CSS(body { margin: 0; min-height: 100vh; padding: 24px; box-sizing: border-box; color: #eceff4; font: 16px/1.55 system-ui,-apple-system,"Segoe UI",sans-serif; background: radial-gradient(ellipse at 20% 15%,#445d82 0,transparent 45%),linear-gradient(135deg,#111a2a,#253c54); }
.wt-glassmorphism { min-height: calc(100vh - 48px); display: grid; place-items: start center; }
.wt-glass-panel { width: min(100%,900px); box-sizing: border-box; padding: clamp(20px,4vw,40px); border: 1px solid rgba(255,255,255,.22); border-radius: 18px; background: rgba(15,30,48,.68); box-shadow: 0 12px 40px rgba(0,0,0,.28); backdrop-filter: blur(12px) saturate(150%); -webkit-backdrop-filter: blur(12px) saturate(150%); }
.wt-glass-panel a { color: #a9d5ff; }
.wt-glass-panel input,.wt-glass-panel button { font: inherit; }
.wt-glass-panel input { color: inherit; background: rgba(255,255,255,.08); border: 1px solid rgba(255,255,255,.25); border-radius: 6px; padding: 8px 10px; }
.wt-glass-panel button { color: #fff; background: #28659b; border: 0; border-radius: 6px; padding: 8px 14px; cursor: pointer; })CSS";
}

static String EngineLoginCss()
{
	return R"CSS(:root { --bg:#f7f7f5; --fg:#1b1b1b; --muted:#5c5c5c; --line:#d0d0cc; --panel:#fff; --link:#1864ab; }
@media (prefers-color-scheme:dark) { :root:not([data-theme="light"]) { --bg:#151515; --fg:#ececec; --muted:#a0a0a0; --line:#3a3a3a; --panel:#1f1f1f; --link:#74c0fc; } }
:root[data-theme="dark"] { --bg:#151515; --fg:#ececec; --muted:#a0a0a0; --line:#3a3a3a; --panel:#1f1f1f; --link:#74c0fc; }
body { background:var(--bg); color:var(--fg); font:15px/1.45 system-ui,sans-serif; margin:0; padding:16px; max-width:420px; }
h1 { font-size:1.5rem; margin:8px 0 16px; }
label { display:flex; flex-direction:column; gap:4px; font-size:.9rem; }
input { font:inherit; color:var(--fg); background:var(--panel); border:1px solid var(--line); border-radius:6px; padding:6px 8px; }
button { font:inherit; color:#fff; background:var(--link); border:0; border-radius:6px; padding:6px 14px; cursor:pointer; }
.login-error { color:#c92a2a; border:1px solid #c92a2a; border-radius:6px; padding:6px 8px; })CSS";
}

String WebsiteTemplate::RenderStyles() const
{
	return "<style>" + stylesheet + "</style>";
}

String WebsiteTemplate::RenderContent(const String& body) const
{
	return "<main class=\"" + content_class + "\"><div class=\"" + body_class + "\">" + body + "</div></main>";
}

String WebsiteTemplate::RenderPage(const String& page_title, const String& body,
                                   const String& html_attributes, const String& head_script) const
{
	return "<!doctype html><html" + html_attributes + "><head><meta charset=\"utf-8\">"
	       "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
	       + head_script + "<title>" + page_title + "</title>" + RenderStyles()
	       + "</head><body>" + RenderContent(body) + "</body></html>";
}

static String EscapeHtml(const String& value)
{
	StringBuffer out;
	for(int i = 0; i < value.GetCount(); i++) {
		switch(value[i]) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(value[i]);
		}
	}
	return String(out);
}

String WebsiteTemplate::RenderMetaLoginForm(const String& action, const String& csrf, const String& next) const
{
	ASSERT(id == "meta-login");
	return "<form class=\"m-form\" method=\"post\" action=\"" + EscapeHtml(action) + "\">"
	       "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + EscapeHtml(csrf) + "\">"
	       "<h2 class=\"m-form_title\">Account Login</h2>"
	       "<div class=\"m-form_group\"><label class=\"m-form_label\" for=\"username\">Username</label>"
	       "<input class=\"m-form_input\" type=\"text\" id=\"username\" name=\"username\" autocomplete=\"username\" required></div>"
	       "<div class=\"m-form_group\"><label class=\"m-form_label\" for=\"password\">Password</label>"
	       "<input class=\"m-form_input\" type=\"password\" id=\"password\" name=\"password\" autocomplete=\"current-password\" required></div>"
	       "<div class=\"m-form_group\"><label class=\"m-form_label\" for=\"code\">Authentication code</label>"
	       "<input class=\"m-form_input\" type=\"text\" id=\"code\" name=\"code\" inputmode=\"numeric\" autocomplete=\"one-time-code\" placeholder=\"6 digits, if enrolled\"></div>"
	       "<input type=\"hidden\" name=\"next\" value=\"" + EscapeHtml(next) + "\">"
	       "<div class=\"m-form_group\"><button class=\"m-form_btn\" type=\"submit\">Log In</button></div>"
	       "<div class=\"m-form_link\"><span class=\"m-form_btn-register\" aria-disabled=\"true\">Register (not available yet)</span></div></form>";
}

String WebsiteTemplate::RenderEngineLoginBody(const String& error, const String& action, const String& csrf,
                                              const String& next, const String& continue_url, bool sso_only) const
{
	ASSERT(id == "engine-login");
	String body = "<h1>Sign in</h1>";
	if(!error.IsEmpty())
		body << "<p role=\"alert\" class=\"login-error\">" << EscapeHtml(error) << "</p>";
	if(sso_only) {
		body << "<p>Sign in with your MetaAccount to open this site.</p>"
		     << "<p><a href=\"" << EscapeHtml(continue_url) << "\">Continue with MetaAccount</a></p>";
		return body;
	}
	body << "<form method=\"post\" action=\"" << EscapeHtml(action) << "\">";
	if(!next.IsEmpty())
		body << "<input type=\"hidden\" name=\"next\" value=\"" << EscapeHtml(next) << "\">";
	if(!csrf.IsEmpty())
		body << "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" << EscapeHtml(csrf) << "\">";
	body << "<p><label>Username <input name=\"username\" autocomplete=\"username\" required></label></p>"
	     << "<p><label>Password <input type=\"password\" name=\"password\" autocomplete=\"current-password\" required></label></p>"
	     << "<p><button type=\"submit\">Sign in</button></p></form>"
	     << "<p><a href=\"" << EscapeHtml(continue_url) << "\">Continue with MetaAccount</a></p>";
	return body;
}

struct WebsiteTemplateRegistry {
	Vector<WebsiteTemplate> templates;
	bool initialized = false;
};

static WebsiteTemplateRegistry& GetRegistry()
{
	static WebsiteTemplateRegistry registry;
	return registry;
}

static bool AddTemplate(WebsiteTemplateRegistry& registry, const WebsiteTemplate& t)
{
	if(t.id.IsEmpty())
		return false;
	for(const WebsiteTemplate& existing : registry.templates)
		if(existing.id == t.id)
			return false;
	registry.templates.Add(t);
	return true;
}

static void EnsureBuiltInTemplates()
{
	WebsiteTemplateRegistry& registry = GetRegistry();
	if(registry.initialized)
		return;
	registry.initialized = true;

	WebsiteTemplate login;
	login.id = "meta-login";
	login.title = "MetaAccount sign-in";
	login.description = "Violet sign-in card with system-font fallback.";
	login.body_class = "m-container";
	login.content_class = "wt-meta-login";
	login.stylesheet = MetaLoginCss();
	bool added = AddTemplate(registry, login);
	ASSERT(added);

	WebsiteTemplate glass;
	glass.id = "glassmorphism";
	glass.title = "Glassmorphism content";
	glass.description = "Self-contained translucent content surface; no CDN assets.";
	glass.body_class = "wt-glass-panel";
	glass.content_class = "wt-glassmorphism";
	glass.stylesheet = GlassmorphismCss();
	added = AddTemplate(registry, glass);
	ASSERT(added);

	WebsiteTemplate engine_login;
	engine_login.id = "engine-login";
	engine_login.title = "Composed-site sign-in";
	engine_login.description = "Accessible shared sign-in page using host theme preferences.";
	engine_login.body_class = "wt-engine-login";
	engine_login.content_class = "wt-engine-login-page";
	engine_login.stylesheet = EngineLoginCss();
	added = AddTemplate(registry, engine_login);
	ASSERT(added);
}

const Vector<WebsiteTemplate>& GetWebsiteTemplates()
{
	EnsureBuiltInTemplates();
	return GetRegistry().templates;
}

const WebsiteTemplate* FindWebsiteTemplate(const String& id)
{
	const Vector<WebsiteTemplate>& templates = GetWebsiteTemplates();
	for(const WebsiteTemplate& t : templates)
		if(t.id == id)
			return &t;
	return NULL;
}

bool RegisterWebsiteTemplate(const WebsiteTemplate& t)
{
	EnsureBuiltInTemplates();
	return AddTemplate(GetRegistry(), t);
}

static bool IsThemeAllowed(const String& value)
{
	static const char *ids[] = { "vivid-dark", "vivid-light", "vivid-mixed", "github-dark", "github-light", "sourceforge-dark", "sourceforge-classic" };
	for(const char *id : ids)
		if(value == id)
			return true;
	return false;
}

static bool IsDensityAllowed(const String& value)
{
	return value == "dense" || value == "compact" || value == "comfortable" || value == "spacious";
}

String WebsiteThemeHtmlAttributes(const String& theme, const String& density)
{
	return " data-theme=\"" + (IsThemeAllowed(theme) ? theme : String("vivid-dark"))
	       + "\" data-density=\"" + (IsDensityAllowed(density) ? density : String("dense")) + "\"";
}

String WebsiteThemeHeadScript()
{
	return "<script>(function(){"
	       "var T=['vivid-dark','vivid-light','vivid-mixed','github-dark','github-light','sourceforge-dark','sourceforge-classic'];"
	       "var L=['dense','compact','comfortable','spacious'];"
	       "function sameHost(o){try{var u=new URL(o);return(u.protocol==='http:'||u.protocol==='https:')&&u.hostname===location.hostname;}catch(e){return false;}}"
	       "window.addEventListener('message',function(e){if(!sameHost(e.origin)||!e.data)return;var d=e.data,h=document.documentElement;"
	       "if(d.type==='wf-theme'&&T.indexOf(d.value)>=0)h.setAttribute('data-theme',d.value);"
	       "else if(d.type==='wf-density'&&L.indexOf(d.value)>=0)h.setAttribute('data-density',d.value);});})();</script>";
}

// Theme tokens for every host theme id. Each theme sets the same variables, so
// one stylesheet serves all composed webapps. vivid-dark is the default.
String WebsiteThemeStyles()
{
	return R"WTC(
:root, :root[data-theme="vivid-dark"] {
  color-scheme: dark; --bg: #182030; --panel: #1e283b; --fg: #d3dae3; --muted: #8b919d;
  --line: rgba(0,161,197,0.22); --accent: #00acd3; --sea: #16324a; --control: #1e283b;
}
:root[data-theme="vivid-light"] {
  color-scheme: light; --bg: #f8fafc; --panel: #ffffff; --fg: #0f172a; --muted: #475569;
  --line: #e2e8f0; --accent: #0284c7; --sea: #d0ebff; --control: #ffffff;
}
:root[data-theme="vivid-mixed"] {
  color-scheme: light; --bg: #f4f6fa; --panel: #ffffff; --fg: #0f172a; --muted: #475569;
  --line: rgba(15,23,42,0.12); --accent: #0088a6; --sea: #d0ebff; --control: #ffffff;
}
:root[data-theme="github-dark"] {
  color-scheme: dark; --bg: #0d1117; --panel: #161b22; --fg: #e6edf3; --muted: #7d8590;
  --line: #30363d; --accent: #2f81f7; --sea: #13233a; --control: #21262d;
}
:root[data-theme="github-light"] {
  color-scheme: light; --bg: #ffffff; --panel: #f6f8fa; --fg: #1f2328; --muted: #656d76;
  --line: #d0d7de; --accent: #0969da; --sea: #ddf4ff; --control: #f6f8fa;
}
:root[data-theme="sourceforge-dark"] {
  color-scheme: dark; --bg: #1a1d21; --panel: #22262b; --fg: #e6e8eb; --muted: #9aa1a9;
  --line: #393e46; --accent: #ee6524; --sea: #2a2620; --control: #272b30;
}
:root[data-theme="sourceforge-classic"] {
  color-scheme: light; --bg: #f4f6f8; --panel: #ffffff; --fg: #262a2e; --muted: #687078;
  --line: #d6dade; --accent: #00739c; --sea: #e3f1f7; --control: #ffffff;
}
:root {
  --st-hot: #d9480f; --st-active: #2b8a3e; --st-planned: #1971c2; --st-notready: #868e96;
  --st-future: #7048e8; --st-settled: #0c8599; --st-trouble: #c92a2a;
  --positive: #069c56; --negative: #d9230f;
}
.wf-theme-controls { display: inline-flex; gap: 6px; align-items: center; }
.wf-theme-controls button {
  font: inherit; font-size: 12px; color: var(--fg); background: var(--control);
  border: 1px solid var(--line); border-radius: 6px; padding: 3px 8px; cursor: pointer;
}
.wf-theme-controls button:hover { border-color: var(--accent); }
)WTC";
}

// Density tokens: data-density on <html> selects the spacing and text size. Dense is the default.
String WebsiteDensityStyles()
{
	// --wf-container-width matches WebFramework DensityConfig: dense uses the full width.
	return R"WTC(
:root, html[data-density="dense"] { --pad: 4px; --gap: 6px; --fs: 13px; --wf-container-width: 100%; }
html[data-density="compact"] { --pad: 8px; --gap: 10px; --fs: 14px; --wf-container-width: 1500px; }
html[data-density="comfortable"] { --pad: 12px; --gap: 16px; --fs: 15px; --wf-container-width: 1360px; }
html[data-density="spacious"] { --pad: 18px; --gap: 24px; --fs: 16px; --wf-container-width: 1200px; }
)WTC";
}

// Theme and Density buttons for a composed page. The labels are filled by the script.
String WebsiteThemeControlsHtml()
{
	return "<span class=\"wf-theme-controls\">"
	       "<button type=\"button\" id=\"wf-btn-theme\" data-wf-cycle=\"theme\" title=\"Cycle colour theme\">"
	       "Theme: <span id=\"wf-theme-label\">vivid-dark</span></button>"
	       "<button type=\"button\" id=\"wf-btn-density\" data-wf-cycle=\"density\" title=\"Cycle layout density\">"
	       "Density: <span id=\"wf-density-label\">dense</span></button>"
	       "</span>";
}

// Cycles theme and density, writes the host-wide wf_theme / wf_density cookies and sets the
// attributes on <html>. Values come only from the same whitelists the head script uses.
String WebsiteThemeControlsScript()
{
	// Runs after the buttons exist, so the script can sit in <head> or at the end of <body>.
	return R"WTC(<script>(function(){
var T=['vivid-dark','vivid-light','vivid-mixed','github-dark','github-light','sourceforge-dark','sourceforge-classic'];
var L=['dense','compact','comfortable','spacious'];
var h=document.documentElement;
function write(name,value){document.cookie=name+'='+encodeURIComponent(value)+'; path=/; max-age=31536000; SameSite=Lax';}
function label(id,value){var el=document.getElementById(id);if(el)el.textContent=value;}
function next(list,current){var i=list.indexOf(current);return list[(i+1)%list.length];}
function setTheme(v){h.setAttribute('data-theme',v);write('wf_theme',v);label('wf-theme-label',v);}
function setDensity(v){h.setAttribute('data-density',v);write('wf_density',v);label('wf-density-label',v);}
function init(){
  label('wf-theme-label',h.getAttribute('data-theme')||'vivid-dark');
  label('wf-density-label',h.getAttribute('data-density')||'dense');
}
document.addEventListener('click',function(e){
  var b=e.target.closest?e.target.closest('[data-wf-cycle]'):null;
  if(!b)return;
  if(b.getAttribute('data-wf-cycle')==='theme')setTheme(next(T,h.getAttribute('data-theme')||'vivid-dark'));
  else setDensity(next(L,h.getAttribute('data-density')||'dense'));
});
if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',init);else init();
})();</script>)WTC";
}

// The base look of a composed page: body, navigation, forms, tables and messages, all on the shared
// tokens, so every host theme and density applies. A page that needs more adds its own rules after it.
String WebsitePageStyles()
{
	return "<style>" + WebsiteThemeStyles() + WebsiteDensityStyles() + R"WTC(
body { margin: 0; padding: calc(var(--pad) * 2); background: var(--bg); color: var(--fg); font: var(--fs)/1.45 system-ui, sans-serif; }
a { color: var(--accent); }
nav, header { display: flex; flex-wrap: wrap; gap: var(--gap); align-items: center; padding-bottom: var(--pad); margin-bottom: var(--gap); border-bottom: 1px solid var(--line); }
nav a, header a { font-weight: 600; text-decoration: none; }
h1 { font-size: 1.5rem; margin: var(--gap) 0 var(--pad); }
h2 { font-size: 1.15rem; margin-top: calc(var(--gap) * 1.5); }
table { border-collapse: collapse; width: 100%; margin: var(--pad) 0; display: block; overflow-x: auto; }
th, td { border-bottom: 1px solid var(--line); padding: var(--pad) calc(var(--pad) * 2); text-align: left; vertical-align: top; }
input, select, textarea, button { font: inherit; color: var(--fg); background: var(--control); border: 1px solid var(--line); border-radius: 6px; padding: calc(var(--pad) / 2) var(--pad); }
button { cursor: pointer; font-weight: 600; }
button:hover { border-color: var(--accent); }
form { display: flex; flex-wrap: wrap; gap: var(--pad) calc(var(--pad) * 2); align-items: flex-end; margin: var(--gap) 0; }
details { border: 1px solid var(--line); border-radius: 8px; padding: var(--pad) calc(var(--pad) * 2); margin: var(--gap) 0; background: var(--panel); }
details summary { cursor: pointer; font-weight: 600; }
.meta, .hint, .muted { color: var(--muted); }
.err { color: var(--st-trouble); font-weight: 600; }
pre, code { font-family: ui-monospace, monospace; }
)WTC" + "</style>";
}

END_UPP_NAMESPACE
