#include "WebAppHost.h"
#include <WebEngine/WebEngine.h>
#include <WebsiteTemplates/WebsiteTemplates.h>

NAMESPACE_UPP

// Shared page shell for the generic webapps: the composed-site admin shell with the theme controls,
// so each app supplies only its body. Bodies must escape their own values with WebAppEscape.

String WebAppEscape(const String& s)
{
	String out;
	for(int i = 0; i < s.GetCount(); i++) {
		int c = s[i];
		switch(c) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat((char)c);
		}
	}
	return out;
}

void WebAppRenderPage(WebRequest& r, const String& brand, const String& title, const String& body)
{
	WebsiteShell s;
	s.title = title + " - " + brand;
	s.brand = brand;
	s.brand_href = r.Site().Url("");
	s.topbar_html = WebsiteThemeControlsHtml();
	s.html_attributes = WebsiteThemeHtmlAttributes(r.Cookie("wf_theme"), r.Cookie("wf_density"));
	WebsiteShellLink home;
	home.label = brand;
	home.href = r.Site().Url("");
	s.crumbs.Add(pick(home));
	WebsiteShellLink current;
	current.label = title;
	s.crumbs.Add(pick(current));
	s.body = "<h1>" + WebAppEscape(title) + "</h1>" + body + WebsiteThemeControlsScript();
	r.Html(RenderAdminShell(s));
}

String WebAppSignInHint(WebRequest& r)
{
	return "<p class=\"ad-hint\">Sign in with an account that has the required role: <a href=\""
	       + WebAppEscape(r.Site().Url("session/login")) + "\">sign in</a>.</p>";
}

END_UPP_NAMESPACE
