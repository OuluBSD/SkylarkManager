#ifndef _GitHubForge_GitHubTemplates_h_
#define _GitHubForge_GitHubTemplates_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>

NAMESPACE_UPP

// Renders templates/<template_name>.witz from the app's default template root
// (the executable directory; deploy-templates.sh copies them there). vars holds
// the values the template reads. Values are HTML-escaped by witz unless wrapped
// in Raw(); user data is never raw in these templates.
//
// String bridge: the GitHub view TUs still append to page.content as a String,
// so this returns the rendered text. Witz preprocessing joins lines with \r\n;
// the result is returned with \n, as the literal HTML was before. Throws when
// the template did not compile, instead of returning witz's error page.
String RenderGitHubTemplate(Renderer& vars, const char *template_name);

// Escapes text for use inside a single-quoted JavaScript string literal in a
// template's <script> block, which witz's HTML escaping does not cover. The
// result is emitted with $raw(...), so it must come only from this function.
String GitHubJsEscape(const String& s);

END_UPP_NAMESPACE

#endif
