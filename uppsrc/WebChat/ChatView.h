#ifndef _WebChat_ChatView_h_
#define _WebChat_ChatView_h_

#include <Core/Core.h>
#include "Session.h"

NAMESPACE_UPP

class WebChatView {
public:
	// Returns full standalone HTML page for chat
	static String RenderPage(const String& current_session_id = "");

	// Returns HTML/CSS/JS snippet for embedding into existing Skylark views
	static String RenderSnippet(const String& current_session_id = "");

	// Returns client-side JavaScript for markdown rendering & SSE streaming
	static String GetClientScript();

	// Returns modern CSS stylesheet for chat
	static String GetStyles();

	// Returns core client-side JavaScript for Markdown parsing & custom viewers
	static String GetMarkdownScript();

	// Returns modern CSS stylesheet for Markdown body & custom viewers
	static String GetMarkdownStyles();
};

END_UPP_NAMESPACE

#endif
