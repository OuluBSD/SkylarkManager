#ifndef _WebsiteTemplates_WebsiteTemplates_h_
#define _WebsiteTemplates_WebsiteTemplates_h_

#include <Core/Core.h>

NAMESPACE_UPP

// A template is configuration (identity, page classes and stylesheet) rendered
// by this package; site handlers supply only their page-specific content.
struct WebsiteTemplate : Moveable<WebsiteTemplate> {
	String id;
	String title;
	String description;
	String body_class;
	String content_class;
	String stylesheet;

	String RenderPage(const String& page_title, const String& body,
	                  const String& html_attributes = String(),
	                  const String& head_script = String()) const;
	String RenderContent(const String& body) const;
	String RenderStyles() const;
	String RenderMetaLoginForm(const String& action, const String& csrf, const String& next) const;
	String RenderEngineLoginBody(const String& error, const String& action, const String& csrf,
	                             const String& next, const String& continue_url, bool sso_only) const;
};

const Vector<WebsiteTemplate>& GetWebsiteTemplates();
const WebsiteTemplate* FindWebsiteTemplate(const String& id);
// Returns false for an empty or already-registered id; duplicates never replace
// an existing template.
bool RegisterWebsiteTemplate(const WebsiteTemplate& t);

// Shared host preference rendering. Values are allowlisted to preserve the
// wf_theme/wf_density contract and the task-052 same-host shell broadcast.
String WebsiteThemeHtmlAttributes(const String& theme, const String& density);
String WebsiteThemeHeadScript();
// Shared theme and density styling for every composed webapp: theme tokens for all
// seven host theme ids (vivid-dark is the default), density tokens (dense is the default),
// and the Theme / Density buttons with the script that cycles them.
String WebsiteThemeStyles();
String WebsiteDensityStyles();
String WebsiteThemeControlsHtml();
String WebsiteThemeControlsScript();
// The <style> for a composed page: theme tokens, density tokens and the base page look.
String WebsitePageStyles();

END_UPP_NAMESPACE

#endif
