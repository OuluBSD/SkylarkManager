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

END_UPP_NAMESPACE
