#include "GitHubTemplates.h"

NAMESPACE_UPP

String RenderGitHubTemplate(Renderer& vars, const char *template_name)
{
	String rendered = vars.RenderString(template_name);
	if(rendered.Find("Error in template") >= 0)
		throw Exc(Format("GitHub template '%s' did not compile", template_name));

	Vector<String> lines = Split(rendered, '\n', false);
	for(String& line : lines)
		if(line.EndsWith("\r"))
			line = line.Left(line.GetCount() - 1);
	return Join(lines, "\n");
}

String GitHubJsEscape(const String& s)
{
	String r;
	for(int i = 0; i < s.GetCount(); i++) {
		byte c = s[i];
		switch(c) {
		case '\\': r << "\\\\"; break;
		case '\'': r << "\\'"; break;
		case '\"': r << "\\\""; break;
		case '<': r << "\\u003c"; break;
		case '>': r << "\\u003e"; break;
		case '&': r << "\\u0026"; break;
		case '\n': r << "\\n"; break;
		case '\r': r << "\\r"; break;
		default:
			if(c < ' ')
				r << Format("\\u%04x", (int)c);
			else
				r.Cat(c);
		}
	}
	return r;
}

END_UPP_NAMESPACE
